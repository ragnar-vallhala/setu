#!/usr/bin/env python3
# SPDX-License-Identifier: MIT OR Apache-2.0
#
# Generate Setu's control theme, personality/wine/dlls/setu.msstyles, from
# Wine's open-source "Light" theme (personality/wine/dlls/light.msstyles,
# LGPL 2.1+, Zhiyi Zhang for CodeWeavers) and the tokens in docs/DESIGN.md.
#
#   tools/setu-theme/make-setu-msstyles.py [--no-bmp]
#
# Steps:
#   1. Every Light SVG is copied as setu_*.svg with Light's palette mapped to
#      the DESIGN.md colours (COLOR_MAP).
#   2. The controls DESIGN.md describes in detail (push buttons, check boxes,
#      radio buttons, scroll bars, edits and combo boxes, tabs, group boxes)
#      are redrawn from scratch here as flat vector SVGs (GENERATED).
#   3. light.rc becomes setu.rc: names, the colour scheme, [SysMetrics]
#      (sizes kept equal to what Windows 10 reports, see the oracle test
#      user32/sys_metrics) and a few properties are rewritten.
#   4. Each SVG is rendered to its BMP with Wine's own tools/buildimage
#      (needs rsvg-convert and ImageMagick's convert; when rsvg-convert is
#      missing, the librsvg-based stand-in next to this script is used).
#
# The SVGs are the sources; the BMPs are committed like Wine commits its own.
# Re-running the script reproduces the directory byte for byte.
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
WINE = os.path.join(REPO, "personality", "wine")
SRC = os.path.join(WINE, "dlls", "light.msstyles")
DST = os.path.join(WINE, "dlls", "setu.msstyles")

# ---- DESIGN.md tokens -----------------------------------------------------
T = {
    "accent": "#1f6fb2",
    "accent_hover": "#2a7dc3",
    "accent_pressed": "#195e97",
    "accent_text": "#ffffff",
    "window_bg": "#f3f3f3",
    "surface": "#ffffff",
    "surface_hover": "#eaeaea",
    "surface_pressed": "#e0e0e0",  # one step past surface-hover
    "border": "#d1d1d1",
    "border_subtle": "#e5e5e5",
    "text": "#1b1b1b",
    "text_secondary": "#5f5f5f",
    "text_disabled": "#a0a0a0",
    "close_hover": "#c42b1c",
}


def rgb(h):
    return " ".join(str(int(h[i:i + 2], 16)) for i in (1, 3, 5))


def mix(a, b, t):
    """a over b at opacity t"""
    ca = [int(a[i:i + 2], 16) for i in (1, 3, 5)]
    cb = [int(b[i:i + 2], 16) for i in (1, 3, 5)]
    return "#" + "".join("%02x" % round(x * t + y * (1 - t)) for x, y in zip(ca, cb))


SEL20 = mix(T["accent"], "#ffffff", 0.20)  # selection fill, DESIGN.md "accent at 20 %"
SEL30 = mix(T["accent"], "#ffffff", 0.30)

# Light's palette -> Setu's (SVG colours).
COLOR_MAP = {
    "#aeaeae": T["border"],
    "#3096fa": T["accent"],
    "#2979ff": T["accent_pressed"],
    "#0091ea": T["accent"],
    "#f5f5f5": T["window_bg"],
    "#ff1744": T["close_hover"],
    "#d50000": "#a52317",
    "#e3f2fd": SEL20,
    "#e1f5fe": SEL20,
    "#bbdefb": SEL30,
    "#b3e5fc": SEL30,
    "#909090": T["text_secondary"],
    "#787878": T["text_secondary"],
    "#5a5a5a": T["text_secondary"],
    "#bdbdbd": T["text_disabled"],
}

# Light's palette -> Setu's (theme ini "R G B" values).
RC_COLOR_MAP = {
    "174 174 174": rgb(T["border"]),
    "48 150 250": rgb(T["accent"]),
    "41 121 255": rgb(T["accent_pressed"]),
    "245 245 245": rgb(T["window_bg"]),
    "244 244 244": rgb(T["window_bg"]),
    "166 166 166": rgb(T["text_disabled"]),
    "189 189 189": rgb(T["text_disabled"]),
    "0 0 0": rgb(T["text"]),
    "213 0 0": rgb(T["close_hover"]),
}

# Checkbox and radio images per DPI (DESIGN.md: 16 px at 96 DPI).
BOX_SIZES = [16, 20, 24, 32, 40, 48, 64]

GENERATED = {}


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


def framed(name, w, fh, states, draw):
    body = ""
    for i, st in enumerate(states):
        body += ' <g transform="translate(0 %d)">\n' % (i * fh)
        body += draw(st, w, fh)
        body += " </g>\n"
    GENERATED[name] = svg(w, fh * len(states), body)


# ---- push button: PBS_NORMAL, HOT, PRESSED, DISABLED, DEFAULTED, DEFAULTED_ANIMATING
def draw_button(st, w, h):
    fill, stroke = {
        "normal": (T["surface"], T["border"]),
        "hot": (T["surface_hover"], T["border"]),
        "pressed": (T["surface_pressed"], T["border"]),
        "disabled": ("#f9f9f9", T["border_subtle"]),
        "default": (T["accent"], T["accent"]),
        "default_anim": (T["accent_hover"], T["accent_hover"]),
    }[st]
    return rect(.5, .5, w - 1, h - 1, 4, fill, stroke)


framed("setu_button", 22, 23,
       ["normal", "hot", "pressed", "disabled", "default", "default_anim"], draw_button)

# ---- check box: (UNCHECKED, CHECKED, MIXED, IMPLICIT, EXCLUDED) x (N, H, P, D)
STATES4 = ["n", "h", "p", "d"]


def box_colors(on, st):
    if on:
        c = {"n": T["accent"], "h": T["accent_hover"], "p": T["accent_pressed"],
             "d": T["text_disabled"]}[st]
        return c, c
    return ({"n": T["surface"], "h": "#f6f6f6", "p": T["surface_hover"], "d": T["surface"]}[st],
            {"n": T["text_secondary"], "h": T["text"], "p": T["text"],
             "d": T["text_disabled"]}[st])


def checkbox(s):
    k = s / 16.0

    def draw(st, w, h):
        kind, sub = st
        on = kind in ("checked", "mixed", "excluded")
        fill, stroke = box_colors(on, sub)
        out = rect(.5 * k, .5 * k, s - k, s - k, 3 * k, fill, stroke, k)
        mark = T["accent_text"]
        if kind == "checked":
            out += path("M%s %sL%s %sL%s %s" % (f(4.2 * k), f(8.3 * k), f(6.8 * k), f(10.9 * k),
                                                f(11.9 * k), f(5.6 * k)), mark, 1.6 * k)
        elif kind == "mixed":
            out += path("M%s %sH%s" % (f(4.5 * k), f(8 * k), f(11.5 * k)), mark, 1.6 * k)
        elif kind == "implicit":
            c = T["text_disabled"] if sub == "d" else T["accent"]
            out += rect(5 * k, 5 * k, 6 * k, 6 * k, 1.5 * k, c)
        elif kind == "excluded":
            out += path("M%s %sL%s %sM%s %sL%s %s" % (f(5 * k), f(5 * k), f(11 * k), f(11 * k),
                                                      f(11 * k), f(5 * k), f(5 * k), f(11 * k)),
                        mark, 1.6 * k)
        return out

    states = [(kind, sub) for kind in ("unchecked", "checked", "mixed", "implicit", "excluded")
              for sub in STATES4]
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
                                                                     T["accent_text"])
        return out

    states = [(on, sub) for on in (False, True) for sub in STATES4]
    framed("setu_radiobutton_%dpx" % s, s, s, states, draw)


for s in BOX_SIZES:
    checkbox(s)
    radio(s)

# ---- scroll bars: thin pill thumbs on a plain track, flat arrows.
# SCRBS_NORMAL, HOT, PRESSED, DISABLED, HOVER (HOVER = pointer over the bar).
THUMB = {"n": (6, .45), "h": (10, .75), "p": (10, .95), "d": (0, 0), "hover": (10, .55)}
SB_STATES = ["n", "h", "p", "d", "hover"]


def draw_thumb_v(st, w, h):
    tw, op = THUMB[st]
    if not tw:
        return ""
    return rect((w - tw) / 2.0, .5, tw, h - 1, min(tw / 2.0, 5), T["text_secondary"], opacity=op)


def draw_thumb_h(st, w, h):
    th, op = THUMB[st]
    if not th:
        return ""
    return rect(.5, (h - th) / 2.0, w - 1, th, min(th / 2.0, 5), T["text_secondary"], opacity=op)


framed("setu_scrollbar_thumb_vertical", 17, 11, SB_STATES, draw_thumb_v)
framed("setu_scrollbar_thumb_horizontal", 20, 17, SB_STATES, draw_thumb_h)

TRACK = {"n": T["surface"], "h": "#fafafa", "p": "#f5f5f5", "d": T["surface"], "hover": "#fafafa"}


def draw_track(st, w, h):
    return rect(0, 0, w, h, 0, TRACK[st])


for part in ("upper", "lower"):
    framed("setu_scrollbar_%s_track_vertical" % part, 14, 1, SB_STATES, draw_track)
    framed("setu_scrollbar_%s_track_horizontal" % part, 1, 8, SB_STATES, draw_track)

# ABS_UP/DOWN/LEFT/RIGHT x (NORMAL, HOT, PRESSED, DISABLED), then UP/DOWN/LEFT/RIGHT HOVER
ARROW_STATES = [(d, s) for d in ("up", "down", "left", "right") for s in STATES4] + \
               [(d, "hover") for d in ("up", "down", "left", "right")]


def draw_arrow_bg(st, w, h):
    c = {"n": T["surface"], "h": T["surface_hover"], "p": T["border"], "d": T["surface"],
         "hover": T["surface"]}[st[1]]
    return rect(1, 1, w - 2, h - 2, 3, c)


framed("setu_scrollbar_arrows", 17, 17, ARROW_STATES, draw_arrow_bg)


def arrow_glyphs(s):
    k = s / 16.0

    def draw(st, w, h):
        d, sub = st
        c = {"n": T["text_secondary"], "h": T["text"], "p": T["text"], "d": T["text_disabled"],
             "hover": T["text_secondary"]}[sub]
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

# ---- edits and editable combo boxes: 1 px border, 2 px accent underline when focused.
# EPSN_NORMAL, HOT, FOCUSED, DISABLED (CBB_* has the same order).


def draw_edit_border(st, w, h):
    stroke = {"n": T["border"], "h": "#bdbdbd", "f": T["border"], "d": T["border_subtle"]}[st]
    fill = "#f9f9f9" if st == "d" else T["surface"]
    out = rect(.5, .5, w - 1, h - 1, 1, fill, stroke)
    if st == "f":
        out += rect(0, h - 2, w, 2, 0, T["accent"])
    return out


for name in ("edit_border_noscroll", "edit_border_vscroll", "edit_border_hscroll",
             "edit_border_hvscroll", "combobox_border"):
    framed("setu_" + name, 5, 5, ["n", "h", "f", "d"], draw_edit_border)


# CBRO_NORMAL, HOT, PRESSED, DISABLED: a drop-down list looks like a push button.
def draw_ro(st, w, h):
    return draw_button({"n": "normal", "h": "hot", "p": "pressed", "d": "disabled"}[st], w, h)


framed("setu_combobox_readonly", 7, 21, STATES4, lambda st, w, h: (
    rect(.5, .5, w - 1, h - 1, 3, *{
        "n": (T["surface"], T["border"]), "h": (T["surface_hover"], T["border"]),
        "p": (T["surface_pressed"], T["border"]),
        "d": ("#f9f9f9", T["border_subtle"])}[st])))


# CBXS_NORMAL, HOT, PRESSED, DISABLED: flat buttons inside the edit border.
def draw_dropdown(st, w, h):
    c = {"n": None, "h": T["surface_hover"], "p": T["border"], "d": None}[st]
    return rect(1, 1, w - 2, h - 2, 2, c) if c else ""


framed("setu_combobox_dropdownbutton", 7, 21, STATES4, draw_dropdown)
framed("setu_combobox_dropdownbutton_right", 5, 23, STATES4, draw_dropdown)
framed("setu_combobox_dropdownbutton_left", 6, 23, STATES4, draw_dropdown)


# ---- tabs: text tabs, a 3 px accent underline on the selected one.
# TIS_NORMAL, HOT, SELECTED, DISABLED, FOCUSED
def draw_tab(st, w, h):
    if st in ("n", "d"):
        return ""
    if st == "h":
        return rect(0, 0, w, h - 2, 2, T["surface_hover"])
    return rect(0, 0, w, h, 0, T["surface"]) + rect(0, h - 3, w, 3, 0, T["accent"])


for name in ("tab_item", "tab_item_left", "tab_item_right"):
    framed("setu_" + name, 5, 19, ["n", "h", "s", "d", "s"], draw_tab)

GENERATED["setu_tab_pane_edge"] = svg(48, 48, rect(.5, .5, 47, 47, 2, T["surface"],
                                                    T["border_subtle"]))
GENERATED["setu_tab_background"] = svg(10, 600, rect(0, 0, 10, 600, 0, T["surface"]))
GENERATED["setu_groupbox"] = svg(22, 20, rect(.5, .5, 21, 19, 4, "none", T["border_subtle"]))

# ---- theme ini -------------------------------------------------------------
GLOBALS = """\
"[Globals]\\r\\n"
"EdgeLightColor = 255 255 255\\r\\n"
"EdgeHighLightColor = 255 255 255\\r\\n"
"EdgeShadowColor = {border}\\r\\n"
"EdgeDkShadowColor = {disabled}\\r\\n"
"EdgeFillColor = 255 255 255\\r\\n"

"\\r\\n[SysMetrics]\\r\\n"
"; Sizes and fonts are what Windows 10 reports at 96 DPI (oracle test\\r\\n"
"; user32/sys_metrics); Setu's look comes from drawing, not from metrics.\\r\\n"
"ScrollbarWidth = 17\\r\\n"
"ScrollbarHeight = 17\\r\\n"
"CaptionBarWidth = 36\\r\\n"
"CaptionBarHeight = 22\\r\\n"
"SMCaptionBarWidth = 22\\r\\n"
"SMCaptionBarHeight = 22\\r\\n"
"MenuBarWidth = 19\\r\\n"
"MenuBarHeight = 19\\r\\n"

"\\r\\n; System colors (docs/DESIGN.md)\\r\\n"
{colors}
"\\r\\n; Nonclient metrics\\r\\n"
"CaptionFont = Segoe UI, 9\\r\\n"
"SmallCaptionFont = Segoe UI, 9\\r\\n"
"MenuFont = Segoe UI, 9\\r\\n"
"StatusFont = Segoe UI, 9\\r\\n"
"MsgBoxFont = Segoe UI, 9\\r\\n"
"IconTitleFont = Segoe UI, 9\\r\\n"

"\\r\\n; Flat menus\\r\\n"
"FlatMenus = true\\r\\n"
"""

# System colours, in the theme's ini names. Kept in step with wine.inf's
# [Control Panel\Colors] defaults.
SYS_COLORS = [
    ("Scrollbar", T["window_bg"]),
    ("Background", T["accent_pressed"]),
    ("ActiveCaption", "#ffffff"),
    ("InactiveCaption", T["window_bg"]),
    ("Menu", T["surface"]),
    ("Window", T["surface"]),
    ("WindowFrame", T["border"]),
    ("MenuText", T["text"]),
    ("WindowText", T["text"]),
    ("CaptionText", T["text"]),
    ("ActiveBorder", "#ffffff"),
    ("InactiveBorder", T["window_bg"]),
    ("AppWorkSpace", T["border_subtle"]),
    ("Highlight", T["accent"]),
    ("HighlightText", T["accent_text"]),
    ("BtnFace", T["window_bg"]),
    ("BtnShadow", T["border"]),
    ("GrayText", T["text_disabled"]),
    ("BtnText", T["text"]),
    ("InactiveCaptionText", T["text_secondary"]),
    ("BtnHighlight", "#ffffff"),
    ("DkShadow3d", T["text_disabled"]),
    ("Light3d", T["window_bg"]),
    ("InfoText", T["text"]),
    ("InfoBk", T["surface"]),
    ("ButtonAlternateFace", T["surface_hover"]),
    ("HotTracking", T["accent"]),
    ("GradientActiveCaption", "#ffffff"),
    ("GradientInactiveCaption", T["window_bg"]),
    ("MenuHilight", T["surface_hover"]),
    ("MenuBar", T["surface"]),
]


def make_rc(text):
    text = text.replace("blue_", "setu_").replace("BLUE_INI", "SETU_INI")
    # Setu's strings are not in Wine's translations.
    text = text.replace("#pragma makedep po\n\n", "")
    text = text.replace('"Blue\\0"', '"Setu\\0"')
    text = re.sub(r'(IDS_COLOR_(?:DISPLAY_NAME|TOOLTIP)_BLUE\s+)"Blue"', r'\1"Setu"', text)
    text = text.replace("DisplayName = Light", "DisplayName = Setu")
    text = text.replace("ToolTip = Light Visual Style", "ToolTip = Setu Visual Style")
    text = text.replace("/* Light theme definition */\n/* Blue theme */",
                        "/* Setu theme definition, generated by tools/setu-theme/"
                        "make-setu-msstyles.py from Wine's Light theme */")
    if "Copyright 2021-2024 Zhiyi Zhang" in text:
        text = text.replace(" * Copyright 2021-2024 Zhiyi Zhang for CodeWeavers\n",
                            " * Copyright 2021-2024 Zhiyi Zhang for CodeWeavers\n"
                            " * Setu changes: colours, shapes and metrics from Setu's docs/DESIGN.md\n")

    # Globals + SysMetrics up to the first control section.
    start = text.index('"[Globals]\\r\\n"')
    end = text.index('"\\r\\n; Button\\r\\n"')
    colors = "".join('"%s = %s\\r\\n"\n' % (n, rgb(c)) for n, c in SYS_COLORS)
    text = text[:start] + GLOBALS.format(border=rgb(T["border"]), disabled=rgb(T["text_disabled"]),
                                         colors=colors) + "\n" + text[end:]

    # Colours in the control sections.
    text = re.sub(r"= (\d+ \d+ \d+)\\r\\n",
                  lambda m: "= %s\\r\\n" % RC_COLOR_MAP.get(m.group(1), m.group(1)), text)

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
    section("Button.Pushbutton(Pressed)",
            lambda s: s.replace("TextColor = 255 255 255", "TextColor = %s" % rgb(T["text"])))
    text = text.replace('"[Button.Pushbutton(Disabled)]\\r\\n"',
                        '"[Button.Pushbutton(Defaulted)]\\r\\n"\n'
                        '"TextColor = 255 255 255\\r\\n"\n\n'
                        '"[Button.Pushbutton(DefaultedAnimating)]\\r\\n"\n'
                        '"TextColor = 255 255 255\\r\\n"\n\n'
                        '"[Button.Pushbutton(Disabled)]\\r\\n"', 1)
    # Progress: a flat track in border-subtle, accent fill.
    section("Progress", lambda s: s.replace("BorderColor = %s" % rgb(T["border"]),
                                            "BorderColor = %s" % rgb(T["border_subtle"])))
    section("Progress.Bar", lambda s: s.replace("FillColor = 255 255 255",
                                                "FillColor = %s" % rgb(T["border_subtle"])))
    section("Progress.BarVert", lambda s: s.replace("FillColor = 255 255 255",
                                                    "FillColor = %s" % rgb(T["border_subtle"])))
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


def map_colors(s):
    return re.sub(r"#[0-9a-fA-F]{6}\b", lambda m: COLOR_MAP.get(m.group(0).lower(), m.group(0)),
                  s)


def main():
    render = "--no-bmp" not in sys.argv
    if os.path.isdir(DST):
        shutil.rmtree(DST)
    os.makedirs(DST)

    svgs = {}
    for name in sorted(os.listdir(SRC)):
        if not name.endswith(".svg"):
            continue
        base = "setu_" + name[len("blue_"):-4]
        with open(os.path.join(SRC, name), encoding="utf-8") as fh:
            svgs[base] = map_colors(fh.read())
    # Replaced sizes of the box images.
    for kind in ("checkbox", "radiobutton"):
        for name in [n for n in svgs if n.startswith("setu_%s_" % kind)]:
            del svgs[name]
    # Grippers on the thumbs: none.
    for name in [n for n in svgs if "scrollbar_gripper" in n]:
        m = re.search(r'width="(\d+)" height="(\d+)"', svgs[name])
        svgs[name] = svg(int(m.group(1)), int(m.group(2)), "")
    svgs.update(GENERATED)

    for base, text in sorted(svgs.items()):
        with open(os.path.join(DST, base + ".svg"), "w", encoding="utf-8") as fh:
            fh.write(text)

    with open(os.path.join(SRC, "light.rc"), encoding="utf-8") as fh:
        rc = make_rc(fh.read())
    rc = bitmap_list(rc, svgs)
    with open(os.path.join(DST, "setu.rc"), "w", encoding="utf-8") as fh:
        fh.write(rc)
    shutil.copy(os.path.join(SRC, "resources.h"), os.path.join(DST, "resources.h"))

    used = set(re.findall(r"(setu_[a-z0-9_]+)\.bmp", rc))
    missing = sorted(used - set(svgs))
    if missing:
        sys.exit("setu.rc references images with no SVG: %s" % ", ".join(missing))

    with open(os.path.join(DST, "Makefile.in"), "w", encoding="utf-8") as fh:
        fh.write("MODULE = setu.msstyles\n\nEXTRADLLFLAGS = -Wb,--data-only\n\nSOURCES = \\\n")
        for base in sorted(svgs):
            fh.write("\t%s.svg \\\n" % base)
        fh.write("\tsetu.rc\n")

    if render:
        env = dict(os.environ)
        if not shutil.which("rsvg-convert"):
            env["PATH"] = HERE + os.pathsep + env["PATH"]
        env["SOURCE_DATE_EPOCH"] = "0"
        for base in sorted(svgs):
            subprocess.run(["perl", os.path.join(WINE, "tools", "buildimage"),
                            os.path.join(DST, base + ".svg"), os.path.join(DST, base + ".bmp")],
                           check=True, env=env)
    print("setu.msstyles: %d images" % len(svgs))


if __name__ == "__main__":
    main()
