#!/usr/bin/env python3
# SPDX-License-Identifier: MIT OR Apache-2.0
#
# Generate a Setu control theme (.msstyles) from a theme pack's tokens
# (docs/THEMES.md, theme.ini), starting from Wine's open-source "Light"
# theme (personality/wine/dlls/light.msstyles, LGPL 2.1+, Zhiyi Zhang for
# CodeWeavers).
#
#   tools/setu-theme/make-msstyles.py --tokens THEME_INI --out OUT.msstyles
#                                     [--colors-out COLORS.ini] [--wine-bin DIR]
#   tools/setu-theme/make-msstyles.py --fork-dir [--no-bmp]
#
# --tokens      a theme.ini; keys it doesn't set keep setu-light's values
#               (a pack's Base= chain must already be resolved)
# --out         the .msstyles to write (a data-only PE built with Wine's wrc
#               and winebuild; no cross compiler is needed)
# --colors-out  also write the pack's system colours as the .theme file's
#               [Control Panel\Colors] section, from the same mapping the
#               msstyles [SysMetrics] uses
# --wine-bin    directory holding wrc and winebuild (default: the directory
#               of $SETU_WINE, then build/personality/install/bin, then PATH)
# --fork-dir    regenerate the sources of the Wine fork's built-in theme,
#               personality/wine/dlls/setu.msstyles (setu-light's tokens),
#               SVGs + BMPs + setu.rc, byte for byte
#
# Steps:
#   1. Every Light SVG is copied as setu_*.svg with Light's palette mapped to
#      the pack's colours (COLOR_MAP).
#   2. The controls docs/DESIGN.md describes in detail (push buttons, check
#      boxes, radio buttons, scroll bars, edits and combo boxes, tabs, group
#      boxes) are redrawn from scratch here as flat vector SVGs, using the
#      pack's colours, corner radii and scroll bar widths.
#   3. light.rc becomes setu.rc: names, the colour scheme, [SysMetrics] and
#      a few properties are rewritten. Sizes and fonts in [SysMetrics] stay
#      what Windows 10 reports (oracle test user32/sys_metrics), whatever the
#      pack says: a theme changes how things look, not what apps can query.
#   4. Each SVG is rendered to its BMP with Wine's own tools/buildimage
#      (needs perl with XML::LibXML, rsvg-convert and ImageMagick's convert;
#      when rsvg-convert is missing, the librsvg-based stand-in next to this
#      script is used). A pack image whose SVG is the same as the fork's
#      built-in one reuses the committed BMP.
#
# Exit status: 0 done, 1 bad arguments or tokens, 2 a needed tool is missing.
#
# Colours the generator needs that theme.ini doesn't name (a pressed
# surface, the disabled fill, ...) are derived from the pack's tokens. When
# every token a derived colour depends on has setu-light's value, the
# derived colour is setu-light's exact value, so setu-light's output is the
# fork's built-in setu.msstyles.
import argparse
import concurrent.futures
import configparser
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
WINE = os.path.join(REPO, "personality", "wine")
SRC = os.path.join(WINE, "dlls", "light.msstyles")
FORK_DST = os.path.join(WINE, "dlls", "setu.msstyles")

# ---- tokens ----------------------------------------------------------------
# setu-light's values (docs/DESIGN.md, docs/THEMES.md). Kept in step with
# the fallbacks compiled into win32u (sysparams.c, defwnd.c, menu.c).
LIGHT = {
    "accent": "#1f6fb2",
    "accent-hover": "#2a7dc3",
    "accent-pressed": "#195e97",
    "accent-text": "#ffffff",
    "window-bg": "#f3f3f3",
    "surface": "#ffffff",
    "surface-hover": "#eaeaea",
    "border": "#d1d1d1",
    "border-subtle": "#e5e5e5",
    "text": "#1b1b1b",
    "text-secondary": "#5f5f5f",
    "text-disabled": "#a0a0a0",
    "titlebar-active": "#ffffff",
    "titlebar-inactive": "#f3f3f3",
    "titlebar-text": "#1b1b1b",
    "titlebar-text-inactive": "#5f5f5f",
    "close-hover": "#c42b1c",
    "menu": "#ffffff",
    "selection": "#1f6fb233",
    "desktop": "#1e4f8a",
}

LIGHT_METRICS = {
    "radius-control": 4,
    "radius-check": 3,
    "scrollbar-width": 6,
    "scrollbar-width-hover": 12,
}


def parse_color(s):
    s = s.strip().lower()
    if not re.fullmatch(r"#[0-9a-f]{6}([0-9a-f]{2})?", s):
        raise ValueError("not a #RRGGBB colour: %r" % s)
    return s


def rgb_t(h):
    return tuple(int(h[i:i + 2], 16) for i in (1, 3, 5))


def hexc(t):
    return "#" + "".join("%02x" % max(0, min(255, round(x))) for x in t)


def mix(a, b, t):
    """a over b at opacity t"""
    return hexc(x * t + y * (1 - t) for x, y in zip(rgb_t(a), rgb_t(b)))


# Derived colours: name -> (tokens it depends on, setu-light's value, formula).
# With setu-light's inputs the value is setu-light's exactly; otherwise the
# formula, which matches setu-light's value to within a unit.
DERIVED = {
    "surface-pressed": (("text", "surface"), "#e0e0e0",
                        lambda c: mix(c["text"], c["surface"], 0.136)),
    "surface-disabled": (("window-bg", "surface"), "#f9f9f9",
                         lambda c: mix(c["window-bg"], c["surface"], 0.5)),
    "check-hover": (("window-bg", "surface"), "#f6f6f6",
                    lambda c: mix(c["window-bg"], c["surface"], 0.75)),
    "border-hover": (("text-secondary", "border"), "#bdbdbd",
                     lambda c: mix(c["text-secondary"], c["border"], 0.175)),
    "track-hover": (("window-bg", "surface"), "#fafafa",
                    lambda c: mix(c["window-bg"], c["surface"], 0.4)),
    "track-pressed": (("window-bg", "surface"), "#f5f5f5",
                      lambda c: mix(c["window-bg"], c["surface"], 0.8333)),
    "close-pressed": (("close-hover",), "#a52317",
                      lambda c: mix("#000000", c["close-hover"], 0.16)),
    # the selection fill over a surface, and a stronger one for hot+selected
    "selection-fill": (("selection", "surface"), "#d2e2f0",
                       lambda c: sel(c, 1.0)),
    "selection-fill-hot": (("selection", "surface"), "#bcd4e8",
                           lambda c: sel(c, 1.5)),
    # COLOR_BACKGROUND: setu-light has always used accent-pressed here
    "desktop-color": (("desktop",), "#195e97", lambda c: c["desktop"][:7]),
}


def sel(c, k):
    s = c["selection"]
    a = int(s[7:9], 16) / 255.0 if len(s) == 9 else 0.2
    return mix(s[:7], c["surface"], min(1.0, a * k))


class Tokens:
    def __init__(self, colors=None, metrics=None, name="Setu", pack_id=None):
        self.c = dict(LIGHT)
        self.c.update(colors or {})
        self.m = dict(LIGHT_METRICS)
        self.m.update(metrics or {})
        self.name = name
        self.id = pack_id
        for k in list(self.c):
            if k != "selection" and len(self.c[k]) == 9:
                self.c[k] = self.c[k][:7]   # alpha only means something for selection
        for d, (inputs, light, fn) in DERIVED.items():
            if d in (colors or {}):
                continue
            if all(self.c[i] == LIGHT[i] for i in inputs):
                self.c[d] = light
            else:
                self.c[d] = fn(self.c)

    def __getitem__(self, k):
        return self.c[k]

    def is_light(self, *names):
        """True when every named token has setu-light's value."""
        return all(self.c[n] == (LIGHT.get(n) or DERIVED[n][1]) for n in names)


def load_tokens(path):
    # ';' starts a comment only after whitespace; colours are '#RRGGBB'
    cp = configparser.ConfigParser(inline_comment_prefixes=(";",), interpolation=None,
                                   strict=False)
    cp.optionxform = str
    with open(path, encoding="utf-8-sig") as fh:
        cp.read_file(fh)
    colors, metrics = {}, {}
    if cp.has_section("Colors"):
        for k, v in cp.items("Colors"):
            colors[k.strip().lower()] = parse_color(v)
    if cp.has_section("Metrics"):
        for k, v in cp.items("Metrics"):
            try:
                metrics[k.strip().lower()] = float(v.strip())
            except ValueError:
                raise ValueError("[Metrics] %s: not a number: %r" % (k, v))
    name = cp.get("Theme", "Name", fallback="Setu").strip() or "Setu"
    pid = cp.get("Theme", "Id", fallback="").strip() or None
    return Tokens(colors, metrics, name, pid)


def rgb(h):
    return " ".join(str(x) for x in rgb_t(h))


BOX_SIZES = [16, 20, 24, 32, 40, 48, 64]
STATES4 = ["n", "h", "p", "d"]
SB_STATES = ["n", "h", "p", "d", "hover"]
ARROW_STATES = [(d, s) for d in ("up", "down", "left", "right") for s in STATES4] + \
               [(d, "hover") for d in ("up", "down", "left", "right")]


def svg(w, h, body):
    return ('<?xml version="1.0" encoding="UTF-8"?>\n'
            '<svg id="bitmap:%d-32" width="%d" height="%d" version="1.1" viewBox="0 0 %d %d" '
            'xmlns="http://www.w3.org/2000/svg">\n%s</svg>\n' % (w, w, h, w, h, body))


def f(v):
    s = ("%.3f" % v).rstrip("0").rstrip(".")
    return s if s not in ("-0", "") else "0"


def rect(x, y, w, h, r=0, fill="none", stroke=None, sw=1, opacity=None):
    a = 'x="%s" y="%s" width="%s" height="%s"' % (f(x), f(y), f(w), f(h))
    if r:
        a += ' rx="%s"' % f(r)
    a += ' fill="%s"' % fill
    if stroke:
        a += ' stroke="%s" stroke-width="%s"' % (stroke, f(sw))
    if opacity is not None:
        a += ' fill-opacity="%s"' % f(opacity)
    return " <rect %s/>\n" % a


def path(d, stroke, sw, fill="none", cap="round"):
    return (' <path d="%s" fill="%s" stroke="%s" stroke-width="%s" stroke-linecap="%s" '
            'stroke-linejoin="round"/>\n' % (d, fill, stroke, f(sw), cap))


def generate(T):
    """The SVGs drawn from scratch, {name: text}."""
    G = {}
    rc_ = max(0.0, T.m["radius-control"])
    rcheck = max(0.0, T.m["radius-check"])

    def framed(name, w, fh, states, draw):
        body = ""
        for i, st in enumerate(states):
            body += ' <g transform="translate(0 %d)">\n' % (i * fh)
            body += draw(st, w, fh)
            body += " </g>\n"
        G[name] = svg(w, fh * len(states), body)

    # ---- push button: PBS_NORMAL, HOT, PRESSED, DISABLED, DEFAULTED, DEFAULTED_ANIMATING
    def draw_button(st, w, h):
        fill, stroke = {
            "normal": (T["surface"], T["border"]),
            "hot": (T["surface-hover"], T["border"]),
            "pressed": (T["surface-pressed"], T["border"]),
            "disabled": (T["surface-disabled"], T["border-subtle"]),
            "default": (T["accent"], T["accent"]),
            "default_anim": (T["accent-hover"], T["accent-hover"]),
        }[st]
        return rect(.5, .5, w - 1, h - 1, rc_, fill, stroke)

    framed("setu_button", 22, 23,
           ["normal", "hot", "pressed", "disabled", "default", "default_anim"], draw_button)

    # ---- check box: (UNCHECKED, CHECKED, MIXED, IMPLICIT, EXCLUDED) x (N, H, P, D)
    def box_colors(on, st):
        if on:
            c = {"n": T["accent"], "h": T["accent-hover"], "p": T["accent-pressed"],
                 "d": T["text-disabled"]}[st]
            return c, c
        return ({"n": T["surface"], "h": T["check-hover"], "p": T["surface-hover"],
                 "d": T["surface"]}[st],
                {"n": T["text-secondary"], "h": T["text"], "p": T["text"],
                 "d": T["text-disabled"]}[st])

    def checkbox(s):
        k = s / 16.0

        def draw(st, w, h):
            kind, sub = st
            on = kind in ("checked", "mixed", "excluded")
            fill, stroke = box_colors(on, sub)
            out = rect(.5 * k, .5 * k, s - k, s - k, rcheck * k, fill, stroke, k)
            mark = T["accent-text"]
            if kind == "checked":
                out += path("M%s %sL%s %sL%s %s" % (f(4.2 * k), f(8.3 * k), f(6.8 * k),
                                                    f(10.9 * k), f(11.9 * k), f(5.6 * k)),
                            mark, 1.6 * k)
            elif kind == "mixed":
                out += path("M%s %sH%s" % (f(4.5 * k), f(8 * k), f(11.5 * k)), mark, 1.6 * k)
            elif kind == "implicit":
                c = T["text-disabled"] if sub == "d" else T["accent"]
                out += rect(5 * k, 5 * k, 6 * k, 6 * k, 1.5 * k, c)
            elif kind == "excluded":
                out += path("M%s %sL%s %sM%s %sL%s %s" % (f(5 * k), f(5 * k), f(11 * k),
                                                          f(11 * k), f(11 * k), f(5 * k),
                                                          f(5 * k), f(11 * k)), mark, 1.6 * k)
            return out

        states = [(kind, sub) for kind in ("unchecked", "checked", "mixed", "implicit",
                                           "excluded") for sub in STATES4]
        framed("setu_checkbox_%dpx" % s, s, s, states, draw)

    def radio(s):
        k = s / 16.0

        def draw(st, w, h):
            on, sub = st
            fill, stroke = box_colors(on, sub)
            c = s / 2.0
            out = ' <circle cx="%s" cy="%s" r="%s" fill="%s" stroke="%s" stroke-width="%s"/>\n' % (
                f(c), f(c), f(c - .5 * k), fill, stroke, f(k))
            if on:
                dot = {"n": 3.5, "h": 4, "p": 3, "d": 3.5}[sub] * k
                out += ' <circle cx="%s" cy="%s" r="%s" fill="%s"/>\n' % (f(c), f(c), f(dot),
                                                                         T["accent-text"])
            return out

        states = [(on, sub) for on in (False, True) for sub in STATES4]
        framed("setu_radiobutton_%dpx" % s, s, s, states, draw)

    for s in BOX_SIZES:
        checkbox(s)
        radio(s)

    # ---- scroll bars: thin pill thumbs on a plain track, flat arrows.
    # SCRBS_NORMAL, HOT, PRESSED, DISABLED, HOVER (HOVER = pointer over the bar).
    # The images keep Light's sizes (the scroll bar metrics stay Windows');
    # the thumb drawn inside is scrollbar-width wide, 2 px less than
    # scrollbar-width-hover when the pointer is on it.
    thin = max(1.0, min(15.0, T.m["scrollbar-width"]))
    wide = max(1.0, min(15.0, T.m["scrollbar-width-hover"] - 2))
    thumb = {"n": (thin, .45), "h": (wide, .75), "p": (wide, .95), "d": (0, 0),
             "hover": (wide, .55)}

    def draw_thumb_v(st, w, h):
        tw, op = thumb[st]
        if not tw:
            return ""
        return rect((w - tw) / 2.0, .5, tw, h - 1, min(tw / 2.0, 5), T["text-secondary"],
                    opacity=op)

    def draw_thumb_h(st, w, h):
        th, op = thumb[st]
        if not th:
            return ""
        return rect(.5, (h - th) / 2.0, w - 1, th, min(th / 2.0, 5), T["text-secondary"],
                    opacity=op)

    framed("setu_scrollbar_thumb_vertical", 17, 11, SB_STATES, draw_thumb_v)
    framed("setu_scrollbar_thumb_horizontal", 20, 17, SB_STATES, draw_thumb_h)

    track = {"n": T["surface"], "h": T["track-hover"], "p": T["track-pressed"],
             "d": T["surface"], "hover": T["track-hover"]}

    def draw_track(st, w, h):
        return rect(0, 0, w, h, 0, track[st])

    for part in ("upper", "lower"):
        framed("setu_scrollbar_%s_track_vertical" % part, 14, 1, SB_STATES, draw_track)
        framed("setu_scrollbar_%s_track_horizontal" % part, 1, 8, SB_STATES, draw_track)

    def draw_arrow_bg(st, w, h):
        c = {"n": T["surface"], "h": T["surface-hover"], "p": T["border"], "d": T["surface"],
             "hover": T["surface"]}[st[1]]
        return rect(1, 1, w - 2, h - 2, max(0.0, rc_ - 1), c)

    framed("setu_scrollbar_arrows", 17, 17, ARROW_STATES, draw_arrow_bg)

    def arrow_glyphs(s):
        k = s / 16.0

        def draw(st, w, h):
            d, sub = st
            c = {"n": T["text-secondary"], "h": T["text"], "p": T["text"],
                 "d": T["text-disabled"], "hover": T["text-secondary"]}[sub]
            a, b, m = 5.5 * k, 10.5 * k, 8 * k
            p = {"up": (a, 9.25 * k, m, 6.75 * k, b, 9.25 * k),
                 "down": (a, 6.75 * k, m, 9.25 * k, b, 6.75 * k)}.get(d)
            if p:
                pts = "M%s %sL%s %sL%s %s" % tuple(f(v) for v in p)
            elif d == "left":
                pts = "M%s %sL%s %sL%s %s" % tuple(f(v) for v in (9.25 * k, a, 6.75 * k, m,
                                                                   9.25 * k, b))
            else:
                pts = "M%s %sL%s %sL%s %s" % tuple(f(v) for v in (6.75 * k, a, 9.25 * k, m,
                                                                   6.75 * k, b))
            return path(pts, c, 1.25 * k)

        framed("setu_scrollbar_arrow_glyphs_%dpx" % s, s, s, ARROW_STATES, draw)

    for s in (13, 16, 20, 26, 32, 39, 52):
        arrow_glyphs(s)

    # ---- edits and editable combo boxes: 1 px border, 2 px accent underline
    # when focused. EPSN_NORMAL, HOT, FOCUSED, DISABLED (CBB_* has the same order).
    def draw_edit_border(st, w, h):
        stroke = {"n": T["border"], "h": T["border-hover"], "f": T["border"],
                  "d": T["border-subtle"]}[st]
        fill = T["surface-disabled"] if st == "d" else T["surface"]
        out = rect(.5, .5, w - 1, h - 1, min(1.0, max(0.0, rc_ - 3)), fill, stroke)
        if st == "f":
            out += rect(0, h - 2, w, 2, 0, T["accent"])
        return out

    for name in ("edit_border_noscroll", "edit_border_vscroll", "edit_border_hscroll",
                 "edit_border_hvscroll", "combobox_border"):
        framed("setu_" + name, 5, 5, ["n", "h", "f", "d"], draw_edit_border)

    # CBRO_NORMAL, HOT, PRESSED, DISABLED: a drop-down list looks like a push button.
    framed("setu_combobox_readonly", 7, 21, STATES4, lambda st, w, h: (
        rect(.5, .5, w - 1, h - 1, max(0.0, rc_ - 1), *{
            "n": (T["surface"], T["border"]), "h": (T["surface-hover"], T["border"]),
            "p": (T["surface-pressed"], T["border"]),
            "d": (T["surface-disabled"], T["border-subtle"])}[st])))

    # CBXS_NORMAL, HOT, PRESSED, DISABLED: flat buttons inside the edit border.
    def draw_dropdown(st, w, h):
        c = {"n": None, "h": T["surface-hover"], "p": T["border"], "d": None}[st]
        return rect(1, 1, w - 2, h - 2, max(0.0, rc_ - 2), c) if c else ""

    framed("setu_combobox_dropdownbutton", 7, 21, STATES4, draw_dropdown)
    framed("setu_combobox_dropdownbutton_right", 5, 23, STATES4, draw_dropdown)
    framed("setu_combobox_dropdownbutton_left", 6, 23, STATES4, draw_dropdown)

    # ---- tabs: text tabs, a 3 px accent underline on the selected one.
    # TIS_NORMAL, HOT, SELECTED, DISABLED, FOCUSED
    def draw_tab(st, w, h):
        if st in ("n", "d"):
            return ""
        if st == "h":
            return rect(0, 0, w, h - 2, rc_ / 2.0, T["surface-hover"])
        return rect(0, 0, w, h, 0, T["surface"]) + rect(0, h - 3, w, 3, 0, T["accent"])

    for name in ("tab_item", "tab_item_left", "tab_item_right"):
        framed("setu_" + name, 5, 19, ["n", "h", "s", "d", "s"], draw_tab)

    G["setu_tab_pane_edge"] = svg(48, 48, rect(.5, .5, 47, 47, rc_ / 2.0, T["surface"],
                                               T["border-subtle"]))
    G["setu_tab_background"] = svg(10, 600, rect(0, 0, 10, 600, 0, T["surface"]))
    G["setu_groupbox"] = svg(22, 20, rect(.5, .5, 21, 19, rc_, "none", T["border-subtle"]))
    return G


# ---- Light's palette -> the pack's --------------------------------------------
# Light colour -> (token, the colour setu-light uses). With setu-light's
# token value the colour setu-light has always used is kept (some differ
# slightly from the token, like Light's near-whites); otherwise the token.
COLOR_MAP = {
    "#aeaeae": ("border", "#d1d1d1"),
    "#3096fa": ("accent", "#1f6fb2"),
    "#2979ff": ("accent-pressed", "#195e97"),
    "#0091ea": ("accent", "#1f6fb2"),
    "#f5f5f5": ("window-bg", "#f3f3f3"),
    "#ff1744": ("close-hover", "#c42b1c"),
    "#d50000": ("close-pressed", "#a52317"),
    "#e3f2fd": ("selection-fill", "#d2e2f0"),
    "#e1f5fe": ("selection-fill", "#d2e2f0"),
    "#bbdefb": ("selection-fill-hot", "#bcd4e8"),
    "#b3e5fc": ("selection-fill-hot", "#bcd4e8"),
    "#909090": ("text-secondary", "#5f5f5f"),
    "#787878": ("text-secondary", "#5f5f5f"),
    "#5a5a5a": ("text-secondary", "#5f5f5f"),
    "#bdbdbd": ("text-disabled", "#a0a0a0"),
    "#a6a6a6": ("border", None),
    "#fff": ("surface", None),
    "#ffffff": ("surface", None),
    "#fffffe": ("surface", None),
    "#fefefe": ("surface", None),
    "#fffff9": ("surface", None),
    "#fdffff": ("surface", None),
    "#000": ("text", None),
    "#000000": ("text", None),
    "#0a0a0a": ("text", None),
    "#282828": ("text", None),
    "#2d2d2d": ("text", None),
}

# Light's theme ini "R G B" values -> (token, setu-light's value); a
# (property, value) key wins over a plain value.
RC_COLOR_MAP = {
    "174 174 174": ("border", "209 209 209"),
    "48 150 250": ("accent", "31 111 178"),
    "41 121 255": ("accent-pressed", "25 94 151"),
    "245 245 245": ("window-bg", "243 243 243"),
    "244 244 244": ("window-bg", "243 243 243"),
    "166 166 166": ("text-disabled", "160 160 160"),
    "189 189 189": ("text-disabled", "160 160 160"),
    "0 0 0": ("text", "27 27 27"),
    "213 0 0": ("close-hover", "196 43 28"),
    "255 255 255": ("surface", None),
    "227 227 227": ("surface-hover", None),
    ("TextColor", "255 255 255"): ("accent-text", None),
}


def map_svg_colors(T, s):
    def sub(m):
        src = m.group(0).lower()
        e = COLOR_MAP.get(src)
        if not e:
            return m.group(0)
        tok, light = e
        if T.is_light(tok):
            return light or m.group(0)
        return T[tok]
    return re.sub(r"#(?:[0-9a-fA-F]{6}|[0-9a-fA-F]{3})\b", sub, s)


def map_rc_colors(T, text):
    def sub(m):
        prop, val = m.group(1), m.group(2)
        e = RC_COLOR_MAP.get((prop, val)) or RC_COLOR_MAP.get(val)
        if not e:
            return m.group(0)
        tok, light = e
        if T.is_light(tok):
            new = light or val
        else:
            new = rgb(T[tok])
        return '"%s = %s\\r\\n"' % (prop, new)
    return re.sub(r'"(\w+) = (\d+ \d+ \d+)\\r\\n"', sub, text)


# ---- theme ini -------------------------------------------------------------
GLOBALS = """\
"[Globals]\\r\\n"
"EdgeLightColor = {surface}\\r\\n"
"EdgeHighLightColor = {surface}\\r\\n"
"EdgeShadowColor = {border}\\r\\n"
"EdgeDkShadowColor = {disabled}\\r\\n"
"EdgeFillColor = {surface}\\r\\n"

"\\r\\n[SysMetrics]\\r\\n"
"; Sizes and fonts are the ones light.msstyles reports, so applications\\r\\n"
"; see the same metrics; Setu's look comes from drawing (win32u draws the\\r\\n"
"; window frames, and draws these faces with Inter when it is installed).\\r\\n"
"ScrollbarWidth = 17\\r\\n"
"ScrollbarHeight = 17\\r\\n"

"\\r\\n; System colors (docs/DESIGN.md)\\r\\n"
{colors}
"\\r\\n; Nonclient metrics\\r\\n"
"CaptionFont = Tahoma, 10, bold\\r\\n"
"CaptionBarHeight = 25\\r\\n"
"SmallCaptionFont = Tahoma, 8, bold\\r\\n"
"SMCaptionBarHeight = 17\\r\\n"
"SMCaptionBarWidth = 17\\r\\n"
"MenuFont = Tahoma, 8\\r\\n"
"StatusFont = Tahoma, 8\\r\\n"
"MsgBoxFont = Tahoma, 8\\r\\n"
"IconTitleFont = Tahoma, 8\\r\\n"

"\\r\\n; Flat menus\\r\\n"
"FlatMenus = true\\r\\n"
"""

# System colours: (theme ini name, Control Panel\Colors name, token), in
# COLOR_* order. win32u's compiled defaults (dlls/win32u/sysparams.c) are
# setu-light's values of the same tokens.
SYS_COLORS = [
    ("Scrollbar", "Scrollbar", "window-bg"),
    ("Background", "Background", "desktop-color"),
    ("ActiveCaption", "ActiveTitle", "titlebar-active"),
    ("InactiveCaption", "InactiveTitle", "titlebar-inactive"),
    ("Menu", "Menu", "menu"),
    ("Window", "Window", "surface"),
    ("WindowFrame", "WindowFrame", "border"),
    ("MenuText", "MenuText", "text"),
    ("WindowText", "WindowText", "text"),
    ("CaptionText", "TitleText", "titlebar-text"),
    ("ActiveBorder", "ActiveBorder", "titlebar-active"),
    ("InactiveBorder", "InactiveBorder", "titlebar-inactive"),
    ("AppWorkSpace", "AppWorkspace", "border-subtle"),
    ("Highlight", "Hilight", "accent"),
    ("HighlightText", "HilightText", "accent-text"),
    ("BtnFace", "ButtonFace", "window-bg"),
    ("BtnShadow", "ButtonShadow", "border"),
    ("GrayText", "GrayText", "text-disabled"),
    ("BtnText", "ButtonText", "text"),
    ("InactiveCaptionText", "InactiveTitleText", "titlebar-text-inactive"),
    ("BtnHighlight", "ButtonHilight", "surface"),
    ("DkShadow3d", "ButtonDkShadow", "text-disabled"),
    ("Light3d", "ButtonLight", "window-bg"),
    ("InfoText", "InfoText", "text"),
    ("InfoBk", "InfoWindow", "surface"),
    ("ButtonAlternateFace", "ButtonAlternateFace", "surface-hover"),
    ("HotTracking", "HotTrackingColor", "accent"),
    ("GradientActiveCaption", "GradientActiveTitle", "titlebar-active"),
    ("GradientInactiveCaption", "GradientInactiveTitle", "titlebar-inactive"),
    ("MenuHilight", "MenuHilight", "surface-hover"),
    ("MenuBar", "MenuBar", "menu"),
]


def make_rc(T, text, color_name, display_name):
    text = text.replace("blue_", "setu_").replace("BLUE_INI", "SETU_INI")
    # Setu's strings are not in Wine's translations.
    text = text.replace("#pragma makedep po\n\n", "")
    text = text.replace('"Blue\\0"', '"%s\\0"' % color_name)
    text = re.sub(r'(IDS_COLOR_(?:DISPLAY_NAME|TOOLTIP)_BLUE\s+)"Blue"',
                  lambda m: '%s"%s"' % (m.group(1), display_name), text)
    text = text.replace("DisplayName = Light", "DisplayName = %s" % display_name)
    text = text.replace("ToolTip = Light Visual Style", "ToolTip = %s Visual Style" % display_name)
    text = text.replace("/* Light theme definition */\n/* Blue theme */",
                        "/* Setu theme definition, generated by tools/setu-theme/"
                        "make-setu-msstyles.py from Wine's Light theme */")
    if "Copyright 2021-2024 Zhiyi Zhang" in text:
        text = text.replace(" * Copyright 2021-2024 Zhiyi Zhang for CodeWeavers\n",
                            " * Copyright 2021-2024 Zhiyi Zhang for CodeWeavers\n"
                            " * Setu changes: colours, shapes and metrics from Setu's docs/DESIGN.md\n")

    # Colours in the control sections (Light's palette only: done before the
    # pack's own values go in, so they are never mapped twice).
    text = map_rc_colors(T, text)

    # Globals + SysMetrics up to the first control section.
    start = text.index('"[Globals]\\r\\n"')
    end = text.index('"\\r\\n; Button\\r\\n"')
    colors = "".join('"%s = %s\\r\\n"\n' % (n, rgb(T[tok])) for n, _, tok in SYS_COLORS)
    text = text[:start] + GLOBALS.format(surface=rgb(T["surface"]), border=rgb(T["border"]),
                                         disabled=rgb(T["text-disabled"]),
                                         colors=colors) + "\n" + text[end:]

    # Check boxes and radio buttons: 16 px at 96 DPI.
    for kind in ("checkbox", "radiobutton"):
        def sub_files(m):
            idx = int(m.group(1))
            return "ImageFile%d = setu_%s_%dpx.bmp" % (idx, kind, BOX_SIZES[idx - 1])
        text = re.sub(r"ImageFile(\d) = setu_%s_\d+px\.bmp" % kind, sub_files, text)

    def section(name, fn):
        nonlocal text
        i = text.index('"[%s]\\r\\n"' % name)
        j = text.find('"[', i + 1)
        k = text.find('"\\r\\n;', i + 1)
        j = min(x for x in (j, k) if x != -1)
        text = text[:i] + fn(text[i:j]) + text[j:]

    section("ScrollBar.ThumbBtnHorz",
            lambda s: s.replace("SizingMargins = 11, 8, 8, 8", "SizingMargins = 8, 8, 8, 8")
                       .replace("ContentMargins = 4, 4, 1, 1", "ContentMargins = 0, 0, 0, 0"))
    section("ScrollBar.ThumbBtnVert",
            lambda s: s.replace("ContentMargins = 0, 0, 4, 4", "ContentMargins = 0, 0, 0, 0"))
    # Light draws pressed buttons in its accent; Setu's pressed button is a
    # surface, so its text is the normal text colour.
    section("Button.Pushbutton(Pressed)",
            lambda s: re.sub(r"TextColor = \d+ \d+ \d+", "TextColor = %s" % rgb(T["text"]), s))
    text = text.replace('"[Button.Pushbutton(Disabled)]\\r\\n"',
                        '"[Button.Pushbutton(Defaulted)]\\r\\n"\n'
                        '"TextColor = %s\\r\\n"\n\n'
                        '"[Button.Pushbutton(DefaultedAnimating)]\\r\\n"\n'
                        '"TextColor = %s\\r\\n"\n\n'
                        '"[Button.Pushbutton(Disabled)]\\r\\n"'
                        % (rgb(T["accent-text"]), rgb(T["accent-text"])), 1)
    # Progress: a flat track in border-subtle, accent fill.
    section("Progress", lambda s: re.sub(r"BorderColor = \d+ \d+ \d+",
                                         "BorderColor = %s" % rgb(T["border-subtle"]), s))
    section("Progress.Bar", lambda s: re.sub(r"FillColor = \d+ \d+ \d+",
                                             "FillColor = %s" % rgb(T["border-subtle"]), s))
    section("Progress.BarVert", lambda s: re.sub(r"FillColor = \d+ \d+ \d+",
                                                 "FillColor = %s" % rgb(T["border-subtle"]), s))
    text = text.replace("collapse_button_46px", "collapse_button_45px")
    text = text.replace("expand_button_46px", "expand_button_45px")
    text = text.replace('"Light Theme"', '"Setu Theme"').replace('"light.msstyles"',
                                                                 '"setu.msstyles"')
    return text


def bitmap_list(text, names):
    """Replace the BITMAP resource list with one entry per generated image."""
    i = text.index("/* Button */\n/* @makedep:")
    j = text.index("#define WINE_FILEDESCRIPTION_STR")
    items = "".join('/* @makedep: %s.bmp */\n%s_BMP BITMAP "%s.bmp"\n\n' % (n, n.upper(), n)
                    for n in sorted(names))
    return text[:i] + items + text[j:]


def build_sources(T, color_name, display_name):
    """Returns ({image name: svg text}, rc text)."""
    svgs = {}
    for name in sorted(os.listdir(SRC)):
        if not name.endswith(".svg"):
            continue
        base = "setu_" + name[len("blue_"):-4]
        with open(os.path.join(SRC, name), encoding="utf-8") as fh:
            svgs[base] = map_svg_colors(T, fh.read())
    # Replaced sizes of the box images.
    for kind in ("checkbox", "radiobutton"):
        for name in [n for n in svgs if n.startswith("setu_%s_" % kind)]:
            del svgs[name]
    # Grippers on the thumbs: none.
    for name in [n for n in svgs if "scrollbar_gripper" in n]:
        m = re.search(r'width="(\d+)" height="(\d+)"', svgs[name])
        svgs[name] = svg(int(m.group(1)), int(m.group(2)), "")
    svgs.update(generate(T))

    with open(os.path.join(SRC, "light.rc"), encoding="utf-8") as fh:
        rc = make_rc(T, fh.read(), color_name, display_name)
    rc = bitmap_list(rc, svgs)
    used = set(re.findall(r"(setu_[a-z0-9_]+)\.bmp", rc))
    missing = sorted(used - set(svgs))
    if missing:
        sys.exit("setu.rc references images with no SVG: %s" % ", ".join(missing))
    return svgs, rc


def render_env():
    env = dict(os.environ)
    if not shutil.which("rsvg-convert"):
        env["PATH"] = HERE + os.pathsep + env["PATH"]
    env["SOURCE_DATE_EPOCH"] = "0"
    return env


def check_render_tools():
    missing = [t for t in ("perl", "convert") if not shutil.which(t)]
    if not shutil.which("rsvg-convert"):
        try:
            subprocess.run([sys.executable, "-c", "import cairo, gi; gi.require_version('Rsvg', "
                            "'2.0'); from gi.repository import Rsvg"], check=True,
                           stderr=subprocess.DEVNULL)
        except subprocess.CalledProcessError:
            missing.append("rsvg-convert (or python3-gi + gir1.2-rsvg-2.0 + python3-cairo)")
    if not missing and subprocess.run(["perl", "-MXML::LibXML", "-e", "1"],
                                      stderr=subprocess.DEVNULL).returncode:
        missing.append("perl XML::LibXML (libxml-libxml-perl)")
    return missing


def render(dirpath, names, jobs):
    env = render_env()
    tool = os.path.join(WINE, "tools", "buildimage")

    def one(base):
        subprocess.run(["perl", tool, os.path.join(dirpath, base + ".svg"),
                        os.path.join(dirpath, base + ".bmp")], check=True, env=env)

    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as ex:
        for fut in [ex.submit(one, b) for b in sorted(names)]:
            fut.result()


def write_sources(dst, svgs, rc):
    for base, text in sorted(svgs.items()):
        with open(os.path.join(dst, base + ".svg"), "w", encoding="utf-8") as fh:
            fh.write(text)
    with open(os.path.join(dst, "setu.rc"), "w", encoding="utf-8") as fh:
        fh.write(rc)
    shutil.copy(os.path.join(SRC, "resources.h"), os.path.join(dst, "resources.h"))


def fork_dir(no_bmp, jobs):
    T = Tokens()
    svgs, rc = build_sources(T, "Setu", "Setu")
    if os.path.isdir(FORK_DST):
        shutil.rmtree(FORK_DST)
    os.makedirs(FORK_DST)
    write_sources(FORK_DST, svgs, rc)
    with open(os.path.join(FORK_DST, "Makefile.in"), "w", encoding="utf-8") as fh:
        fh.write("MODULE = setu.msstyles\n\nEXTRADLLFLAGS = -Wb,--data-only\n\nSOURCES = \\\n")
        for base in sorted(svgs):
            fh.write("\t%s.svg \\\n" % base)
        fh.write("\tsetu.rc\n")
    if not no_bmp:
        missing = check_render_tools()
        if missing:
            print("make-msstyles: missing: %s" % ", ".join(missing), file=sys.stderr)
            return 2
        render(FORK_DST, svgs, jobs)
    print("setu.msstyles: %d images" % len(svgs))
    return 0


def find_wine_bin(opt):
    cands = []
    if opt:
        cands.append(opt)
    if os.environ.get("SETU_WINE"):
        cands.append(os.path.dirname(os.environ["SETU_WINE"]))
    cands.append(os.path.join(REPO, "build", "personality", "install", "bin"))
    for d in cands:
        if all(os.access(os.path.join(d, t), os.X_OK) for t in ("wrc", "winebuild")):
            return os.path.join(d, "wrc"), os.path.join(d, "winebuild")
    if shutil.which("wrc") and shutil.which("winebuild"):
        return shutil.which("wrc"), shutil.which("winebuild")
    return None, None


def pack(args):
    try:
        T = load_tokens(args.tokens)
    except (OSError, ValueError, configparser.Error) as e:
        print("make-msstyles: %s: %s" % (args.tokens, e), file=sys.stderr)
        return 1
    wrc, winebuild = find_wine_bin(args.wine_bin)
    missing = check_render_tools()
    if not wrc:
        missing.append("wrc and winebuild (Wine's install bin; --wine-bin DIR or $SETU_WINE)")
    if missing:
        print("make-msstyles: missing: %s" % ", ".join(missing), file=sys.stderr)
        return 2

    svgs, rc = build_sources(T, "NormalColor", T.name)
    with tempfile.TemporaryDirectory(prefix="setu-msstyles-") as tmp:
        write_sources(tmp, svgs, rc)
        # Images that are the same as the built-in theme's reuse its BMPs.
        todo = []
        for base, text in svgs.items():
            fsvg = os.path.join(FORK_DST, base + ".svg")
            fbmp = os.path.join(FORK_DST, base + ".bmp")
            same = False
            if os.path.exists(fsvg) and os.path.exists(fbmp):
                with open(fsvg, encoding="utf-8") as fh:
                    same = fh.read() == text
            if same:
                shutil.copy(fbmp, os.path.join(tmp, base + ".bmp"))
            else:
                todo.append(base)
        render(tmp, todo, args.jobs)
        res = os.path.join(tmp, "setu.res")
        inc = os.path.join(WINE, "include")
        subprocess.run([wrc, "-I" + tmp, "-I" + inc, "-I" + os.path.join(inc, "msvcrt"),
                        "-o", res, os.path.join(tmp, "setu.rc")], check=True)
        out = os.path.abspath(args.out)
        os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
        subprocess.run([winebuild, "--target", "x86_64-w64-mingw32", "--data-only", "--dll",
                        "-o", out + ".tmp", "-r" + res], check=True)
        os.replace(out + ".tmp", out)

    if args.colors_out:
        with open(args.colors_out, "w", encoding="utf-8", newline="\r\n") as fh:
            fh.write("[Control Panel\\Colors]\n")
            for _, cp, tok in SYS_COLORS:
                fh.write("%s=%s\n" % (cp, rgb(T[tok])))
    print("%s: %d images (%d rendered)" % (args.out, len(svgs), len(todo)))
    return 0


def main():
    ap = argparse.ArgumentParser(description="Generate a Setu .msstyles from theme tokens.")
    ap.add_argument("--tokens", help="theme.ini of the pack (Base= chain resolved)")
    ap.add_argument("--out", help="the .msstyles to write")
    ap.add_argument("--colors-out", help="write the [Control Panel\\Colors] section here")
    ap.add_argument("--wine-bin", help="directory with wrc and winebuild")
    ap.add_argument("--fork-dir", action="store_true",
                    help="regenerate personality/wine/dlls/setu.msstyles")
    ap.add_argument("--no-bmp", action="store_true", help="with --fork-dir: skip the BMPs")
    ap.add_argument("-j", "--jobs", type=int, default=3, help="parallel renders (default 3)")
    try:
        args = ap.parse_args()
    except SystemExit as e:
        return 1 if e.code else 0
    if args.fork_dir:
        return fork_dir(args.no_bmp, args.jobs)
    if not args.tokens or not args.out:
        ap.print_usage(sys.stderr)
        return 1
    return pack(args)


if __name__ == "__main__":
    sys.exit(main())
