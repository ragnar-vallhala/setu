#!/usr/bin/env python3
# SPDX-License-Identifier: MIT OR Apache-2.0
"""Setu colour schemes, generated from the wallpaper palettes.

Every palette in art/wallpapers/wallgen.py gives a light and a dark scheme:
the accent comes from the palette, the neutrals (window, surface, borders,
text) carry a faint tint of the accent's hue, and every pair a theme check
cares about is adjusted until it reaches WCAG contrast 4.5:1. The output is
theme.ini text (docs/THEMES.md) that a pack can use whole or inherit from.

    schemegen.py list
    schemegen.py ini dawn-light            print one scheme's theme.ini
    schemegen.py build -o OUTDIR           OUTDIR/<scheme>/theme.ini
    schemegen.py sheet -o SHEET.png        a mock window per scheme
"""
import argparse
import colorsys
import os
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'wallpapers'))
from wallgen import PALETTES  # noqa: E402

NAMES = {'setu-blue': 'Setu Blue', 'setu-dark': 'Setu Night', 'dawn': 'Dawn',
         'forest': 'Forest', 'slate': 'Slate', 'ember': 'Ember'}
# accent source per palette (index into acc); slate has no hue, uses Setu blue
ACCENT_FROM = {'setu-blue': '#1F6FB2', 'setu-dark': 1, 'dawn': 0, 'forest': 0, 'slate': '#4A6A94', 'ember': 0}
WALLPAPER_STYLE = {'setu-blue': 'flow', 'setu-dark': 'aurora', 'dawn': 'ridges',
                   'forest': 'contours', 'slate': 'glass', 'ember': 'flow'}


def hx(c):
    c = c.lstrip('#')
    return tuple(int(c[i:i + 2], 16) / 255 for i in (0, 2, 4))


def tohex(rgb):
    return '#' + ''.join(f'{round(min(max(v, 0), 1) * 255):02X}' for v in rgb)


def lum(rgb):
    f = [v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4 for v in rgb]
    return 0.2126 * f[0] + 0.7152 * f[1] + 0.0722 * f[2]


def contrast(a, b):
    la, lb = sorted((lum(a), lum(b)), reverse=True)
    return (la + 0.05) / (lb + 0.05)


def hls(h, l, s):
    return colorsys.hls_to_rgb(h % 1, min(max(l, 0), 1), min(max(s, 0), 1))


def fit(colour, against, need=4.6, darker=True):
    """Move COLOUR's lightness until it contrasts NEED with AGAINST."""
    h, l, s = colorsys.rgb_to_hls(*colour)
    for _ in range(100):
        c = hls(h, l, s)
        if contrast(c, against) >= need:
            return c
        l += -0.01 if darker else 0.01
    return hls(h, l, s)


def scheme(pal_id, mode):
    pal = PALETTES[pal_id]
    src = ACCENT_FROM[pal_id]
    base = hx(src if isinstance(src, str) else pal['acc'][src])
    h, l, s = colorsys.rgb_to_hls(*base)
    s = max(s, 0.45)
    ns = 0.0 if pal_id == 'slate' else 0.10  # neutral tint saturation
    nh = colorsys.rgb_to_hls(*hx(pal['bg'][1]))[0] if pal_id != 'slate' else h
    white, ink = (1, 1, 1), hx('#1B1B1B')
    c = {}
    if mode == 'light':
        acc = fit(hls(h, min(l, 0.45), s), white, darker=True)
        ah, al, as_ = colorsys.rgb_to_hls(*acc)
        c['accent'] = acc
        c['accent-hover'] = hls(ah, al + 0.06, as_)
        c['accent-pressed'] = hls(ah, al - 0.06, as_)
        c['accent-text'] = white
        c['window-bg'] = hls(nh, 0.953, ns)
        c['surface'] = hls(nh, 0.995, ns)
        c['surface-hover'] = hls(nh, 0.918, ns)
        c['border'] = hls(nh, 0.82, ns)
        c['border-subtle'] = hls(nh, 0.898, ns)
        c['text'] = hls(nh, 0.106, ns)
        c['text-secondary'] = fit(hls(nh, 0.40, ns), c['window-bg'], darker=True)
        c['text-disabled'] = hls(nh, 0.63, ns)
        c['titlebar-active'] = c['surface']
        c['titlebar-inactive'] = c['window-bg']
        c['titlebar-text'] = c['text']
        c['titlebar-text-inactive'] = c['text-secondary']
        c['taskbar'] = hls(nh, 0.94, ns * 1.6 + 0.04)
        c['taskbar-text'] = c['text']
        c['menu'] = c['surface']
        c['shadow'] = (0, 0, 0)
    else:
        acc = fit(hls(h, max(l, 0.62), s), ink, darker=False)
        ah, al, as_ = colorsys.rgb_to_hls(*acc)
        c['accent'] = acc
        c['accent-hover'] = hls(ah, al + 0.06, as_)
        c['accent-pressed'] = hls(ah, al - 0.06, as_)
        c['accent-text'] = ink if contrast(ink, acc) >= contrast(white, acc) else white
        c['window-bg'] = hls(nh, 0.125, ns)
        c['surface'] = hls(nh, 0.17, ns)
        c['surface-hover'] = hls(nh, 0.23, ns)
        c['border'] = hls(nh, 0.29, ns)
        c['border-subtle'] = hls(nh, 0.22, ns)
        c['text'] = hls(nh, 0.95, ns * 0.5)
        c['text-secondary'] = fit(hls(nh, 0.68, ns), c['window-bg'], darker=False)
        c['text-disabled'] = hls(nh, 0.45, ns)
        c['titlebar-active'] = c['surface']
        c['titlebar-inactive'] = c['window-bg']
        c['titlebar-text'] = c['text']
        c['titlebar-text-inactive'] = c['text-secondary']
        c['taskbar'] = hls(nh, 0.15, ns * 1.6 + 0.04)
        c['taskbar-text'] = c['text']
        c['menu'] = c['surface']
        c['shadow'] = (0, 0, 0)
    c['close-hover'] = hx('#C42B1C')
    c['error'] = hx('#C42B1C') if mode == 'light' else hx('#FF6B5E')
    c['logo'] = hx('#F5A623')
    c['desktop'] = hx(pal['bg'][0] if mode == 'dark' else pal['bg'][1])
    out = {k: tohex(v) for k, v in c.items()}
    out['selection'] = out['accent'] + '33'
    out['selection-border'] = out['accent']
    out['shadow'] = '#00000033' if mode == 'light' else '#00000066'
    return out


def schemes():
    for p in PALETTES:
        for m in ('light', 'dark'):
            yield f'{p}-{m}', p, m


CHECKS = [('text', 'surface'), ('text', 'window-bg'), ('accent-text', 'accent'),
          ('text-secondary', 'window-bg'), ('titlebar-text', 'titlebar-active'),
          ('taskbar-text', 'taskbar')]


def check(c):
    return [(a, b, contrast(hx(c[a]), hx(c[b]))) for a, b in CHECKS]


def ini(sid):
    p, m = sid.rsplit('-', 1)
    c = scheme(p, m)
    name = f'{NAMES[p]} {"Light" if m == "light" else "Dark"}'
    lines = ['; Generated by art/schemes/schemegen.py; edit the generator, not this file.',
             '[Theme]', f'Id=setu-{sid}' if not sid.startswith('setu-') else f'Id={sid}',
             f'Name={name}', 'Version=1', f'Mode={m}', f'Base=setu-{m}', '', '[Colors]']
    lines += [f'{k}={v}' for k, v in c.items()]
    lines += ['', '[Wallpaper]', f'collection={WALLPAPER_STYLE[p]}-{p}', '']
    return '\n'.join(lines)


def sheet(out):
    import cairo
    W, H, pad = 300, 190, 14
    items = list(schemes())
    cols = 4
    rows = (len(items) + cols - 1) // cols
    surf = cairo.ImageSurface(cairo.FORMAT_RGB24, cols * (W + pad) + pad, rows * (H + pad + 18) + pad)
    ctx = cairo.Context(surf)
    ctx.set_source_rgb(0.5, 0.5, 0.5)
    ctx.paint()
    ctx.select_font_face('Inter')

    def col(h):
        ctx.set_source_rgb(*hx(h[:7]))

    def rrect(x, y, w, h, r):
        ctx.new_sub_path()
        ctx.arc(x + w - r, y + r, r, -1.5708, 0)
        ctx.arc(x + w - r, y + h - r, r, 0, 1.5708)
        ctx.arc(x + r, y + h - r, r, 1.5708, 3.1416)
        ctx.arc(x + r, y + r, r, 3.1416, 4.7124)
        ctx.close_path()

    for i, (sid, p, m) in enumerate(items):
        c = scheme(p, m)
        x0 = pad + (i % cols) * (W + pad)
        y0 = pad + (i // cols) * (H + pad + 18)
        col(c['desktop']); ctx.rectangle(x0, y0, W, H); ctx.fill()
        # taskbar
        col(c['taskbar']); ctx.rectangle(x0, y0 + H - 22, W, 22); ctx.fill()
        col(c['logo']); ctx.arc(x0 + W / 2 - 20, y0 + H - 11, 5, 0, 6.3); ctx.fill()
        col(c['text-secondary']); ctx.arc(x0 + W / 2, y0 + H - 11, 5, 0, 6.3); ctx.fill()
        col(c['accent']); rrect(x0 + W / 2 - 4, y0 + H - 3, 8, 2, 1); ctx.fill()
        # window
        wx, wy, ww, wh = x0 + 20, y0 + 14, W - 40, H - 50
        col(c['border']); rrect(wx - 1, wy - 1, ww + 2, wh + 2, 7); ctx.fill()
        col(c['window-bg']); rrect(wx, wy, ww, wh, 6); ctx.fill()
        col(c['titlebar-active']); rrect(wx, wy, ww, 20, 6); ctx.fill(); ctx.rectangle(wx, wy + 10, ww, 10); ctx.fill()
        col(c['titlebar-text']); ctx.set_font_size(10); ctx.move_to(wx + 8, wy + 14); ctx.show_text(sid)
        col(c['text']); ctx.move_to(wx + ww - 14, wy + 14); ctx.show_text('×')
        # list surface with selection
        col(c['surface']); ctx.rectangle(wx + 8, wy + 28, ww - 16, 58); ctx.fill()
        for r in range(3):
            if r == 1:
                col(c['selection-border']); ctx.rectangle(wx + 10, wy + 30 + r * 18, ww - 20, 16); ctx.fill()
                ctx.set_source_rgba(*hx(c['accent']), 1); ctx.rectangle(wx + 11, wy + 31 + r * 18, ww - 22, 14)
                a = 0.2; s = hx(c['surface']); acc = hx(c['accent'])
                ctx.set_source_rgb(*[s[k] * (1 - a) + acc[k] * a for k in range(3)]); ctx.fill()
            col(c['text'] if r != 2 else c['text-secondary'])
            ctx.set_font_size(9); ctx.move_to(wx + 16, wy + 42 + r * 18); ctx.show_text(['Documents', 'Pictures', 'modified 2 days ago'][r])
        # buttons
        col(c['accent']); rrect(wx + ww - 138, wy + wh - 26, 62, 20, 4); ctx.fill()
        col(c['accent-text']); ctx.move_to(wx + ww - 126, wy + wh - 12); ctx.show_text('Open')
        col(c['border']); rrect(wx + ww - 70, wy + wh - 26, 62, 20, 4); ctx.fill()
        col(c['surface']); rrect(wx + ww - 69, wy + wh - 25, 60, 18, 3); ctx.fill()
        col(c['text']); ctx.move_to(wx + ww - 58, wy + wh - 12); ctx.show_text('Cancel')
        ok = min(r for *_, r in check(c))
        ctx.set_source_rgb(1, 1, 1); ctx.set_font_size(10)
        ctx.move_to(x0, y0 + H + 13); ctx.show_text(f'{sid}   min contrast {ok:.1f}')
    surf.write_to_png(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    sub = ap.add_subparsers(dest='cmd', required=True)
    sub.add_parser('list')
    i = sub.add_parser('ini'); i.add_argument('scheme')
    b = sub.add_parser('build'); b.add_argument('-o', '--out', required=True)
    s = sub.add_parser('sheet'); s.add_argument('-o', '--out', required=True)
    a = ap.parse_args()
    bad = 0
    if a.cmd == 'list':
        for sid, p, m in schemes():
            worst = min(check(scheme(p, m)), key=lambda t: t[2])
            print(f'{sid:18} accent {scheme(p, m)["accent"]}  worst {worst[0]}/{worst[1]} {worst[2]:.2f}')
            bad += worst[2] < 4.5
    elif a.cmd == 'ini':
        print(ini(a.scheme))
    elif a.cmd == 'build':
        for sid, p, m in schemes():
            d = os.path.join(a.out, sid)
            os.makedirs(d, exist_ok=True)
            with open(os.path.join(d, 'theme.ini'), 'w') as f:
                f.write(ini(sid))
    else:
        sheet(a.out)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
