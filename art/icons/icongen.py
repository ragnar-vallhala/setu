#!/usr/bin/env python3
# SPDX-License-Identifier: MIT OR Apache-2.0
"""Setu's generated icon sets and logo.

Each icon is drawn once, as simple geometry on a 24-unit grid, with every
part tagged by its role (body, detail, knock-out, emblem...). A *style*
then decides how roles are painted, so one drawing gives several complete
sets: line, bold, duotone, solid and colour. Original artwork; names follow
distro/themes/ICONS.md (freedesktop names where one fits).

    icongen.py build [-o OUTDIR]        write OUTDIR/<set>/<name>.svg + logo/
    icongen.py sheet -o SHEET.png       preview of every set
    icongen.py list

Monochrome icons are drawn in #1B1B1B; Setu tints them to a theme colour.
Needs python3-cairo and librsvg with GObject introspection (gir1.2-rsvg-2.0)
for the preview sheet only.
"""
import argparse
import math
import os
import sys

# ------------------------------------------------------------------ drawings
# Roles:
#   b    body: outlined; filled in duotone/solid/colour
#   b1   second body (front panel, lid...): like b, lighter in colour
#   p    paper: like b, white in colour
#   d    detail stroke, always drawn as a line (also outside bodies)
#   k    knock-out detail: a line, cut out of the body in solid
#   e    emblem line: a line, accent-coloured in colour, cut out in solid
#   ed   emblem line that is part of the shape: accent in colour, body elsewhere
#   ef   emblem fill: outlined, accent-filled in colour, cut out in solid
#   dot  small filled dot (cut out in solid)
#   logo the Setu bridge, own stroke widths, orange in colour
# Elements: (role, kind, *args); kinds: path d | line x1 y1 x2 y2 |
#   poly [(x,y)...] | rect x y w h r | circle cx cy r


def arch_y(x, cx=12, cy=15.375, r=8.25):
    return cy - math.sqrt(max(r * r - (x - cx) ** 2, 0))


def bridge_24():
    """The Setu mark on the 24 grid (scaled from its 128 design)."""
    k = 24 / 128
    cx, cy, r = 64 * k, 82 * k, 44 * k
    els = [('logo', 'path', f'M{cx - r:.3f} {cy:.3f}A{r:.3f} {r:.3f} 0 0 1 {cx + r:.3f} {cy:.3f}', 10 * k)]
    for x in (42, 64, 86):
        xx = x * k
        top = cy - math.sqrt(max((r - 5 * k) ** 2 - (xx - cx) ** 2, 0))
        els.append(('logo', 'line', xx, top, xx, 82 * k, 7 * k))
    els.append(('logo', 'line', 12 * k, 84 * k, 116 * k, 84 * k, 9 * k))
    return els


FOLDER_BACK = ('b', 'path', 'M3 6.5A1.5 1.5 0 0 1 4.5 5H9l2 2h8.5A1.5 1.5 0 0 1 21 8.5v9A1.5 1.5 0 0 1 19.5 19h-15A1.5 1.5 0 0 1 3 17.5z')
FOLDER_FRONT = ('b1', 'path', 'M3 10h18v7.5a1.5 1.5 0 0 1-1.5 1.5h-15A1.5 1.5 0 0 1 3 17.5z')
FILE = ('p', 'path', 'M6.5 3H14l5 5v11.5a1.5 1.5 0 0 1-1.5 1.5h-11A1.5 1.5 0 0 1 5 19.5v-15A1.5 1.5 0 0 1 6.5 3z')
FILE_FOLD = ('k', 'path', 'M14 3v5h5')
BIN = [('b', 'path', 'M6 6.5 7 19.5A1.5 1.5 0 0 0 8.5 21h7a1.5 1.5 0 0 0 1.5-1.5L18 6.5z'),
       ('d', 'line', 4, 6.5, 20, 6.5), ('d', 'path', 'M9.5 6.5V4.5h5v2')]
TRASH_RIBS = [('k', 'line', 10, 10, 10, 17.5), ('k', 'line', 14, 10, 14, 17.5)]

ICONS = {
    # ---- glyphs (monochrome in every set)
    'nav-back': [('d', 'line', 19, 12, 5, 12), ('d', 'poly', [(11, 6), (5, 12), (11, 18)])],
    'nav-forward': [('d', 'line', 5, 12, 19, 12), ('d', 'poly', [(13, 6), (19, 12), (13, 18)])],
    'nav-up': [('d', 'line', 12, 19, 12, 5), ('d', 'poly', [(6, 11), (12, 5), (18, 11)])],
    'refresh': [('d', 'path', 'M19 12A7 7 0 1 1 16.95 7.05'), ('d', 'poly', [(17.3, 3.2), (17.3, 7.4), (21.4, 7.4)])],
    'cmd-new-folder': [FOLDER_BACK, ('k', 'line', 12, 10.5, 12, 15.5), ('k', 'line', 9.5, 13, 14.5, 13)],
    'cmd-cut': [('b', 'circle', 6.5, 17.5, 3), ('b', 'circle', 17.5, 17.5, 3),
                ('d', 'line', 8.6, 15.4, 17.5, 3.5), ('d', 'line', 15.4, 15.4, 6.5, 3.5)],
    'cmd-copy': [('d', 'path', 'M16 8V5.5A1.5 1.5 0 0 0 14.5 4h-9A1.5 1.5 0 0 0 4 5.5v9A1.5 1.5 0 0 0 5.5 16H8'),
                 ('b', 'rect', 8, 8, 12, 12, 1.5)],
    'cmd-paste': [('b', 'rect', 5, 4.5, 14, 16.5, 2), ('k', 'rect', 9, 3, 6, 3.5, 1),
                  ('k', 'line', 9, 11.5, 15, 11.5), ('k', 'line', 9, 15, 13, 15)],
    'cmd-rename': [('b', 'path', 'M15.5 4.5 19.5 8.5 9 19H5v-4z'), ('k', 'line', 13.5, 6.5, 17.5, 10.5),
                   ('d', 'line', 13, 19.5, 20, 19.5)],
    'cmd-delete': BIN + TRASH_RIBS,
    'cmd-properties': [('b', 'circle', 12, 12, 9), ('k', 'line', 12, 11, 12, 16.5), ('dot', 'circle', 12, 7.8, 1.1)],
    'cmd-empty-recycle-bin': BIN + [('k', 'line', 10, 11.5, 14, 15.5), ('k', 'line', 14, 11.5, 10, 15.5)],
    'view-details': [('b', 'rect', 4, 5, 3, 3, 0.75), ('b', 'rect', 4, 10.5, 3, 3, 0.75), ('b', 'rect', 4, 16, 3, 3, 0.75),
                     ('d', 'line', 10, 6.5, 20, 6.5), ('d', 'line', 10, 12, 20, 12), ('d', 'line', 10, 17.5, 20, 17.5)],
    'view-icons': [('b', 'rect', 4, 4, 7, 7, 1.5), ('b', 'rect', 13, 4, 7, 7, 1.5),
                   ('b', 'rect', 4, 13, 7, 7, 1.5), ('b', 'rect', 13, 13, 7, 7, 1.5)],
    'search': [('b', 'circle', 10.5, 10.5, 6.5), ('d', 'line', 15.4, 15.4, 20, 20)],
    'chevron-left': [('d', 'poly', [(14.5, 6), (8.5, 12), (14.5, 18)])],
    'chevron-right': [('d', 'poly', [(9.5, 6), (15.5, 12), (9.5, 18)])],
    'chevron-up': [('d', 'poly', [(6, 14.5), (12, 8.5), (18, 14.5)])],
    'chevron-down': [('d', 'poly', [(6, 9.5), (12, 15.5), (18, 9.5)])],
    'pin': [('b', 'path', 'M9 3.5h6l-1 5 3 3v2H7v-2l3-3z'), ('d', 'line', 12, 13.5, 12, 20.5)],
    'power': [('d', 'path', 'M7.4 6.6A7.5 7.5 0 1 0 16.6 6.6'), ('d', 'line', 12, 3.5, 12, 11.5)],
    'user': [('b', 'circle', 12, 8, 4), ('b', 'path', 'M4.5 20.5c0-4 3.4-6.5 7.5-6.5s7.5 2.5 7.5 6.5z')],
    'setu-logo': bridge_24(),
    # window caption buttons
    'caption-minimize': [('d', 'line', 7, 12, 17, 12)],
    'caption-maximize': [('d', 'rect', 7, 7, 10, 10, 1)],
    'caption-restore': [('d', 'rect', 7, 9, 8, 8, 1), ('d', 'path', 'M9.5 7H16a1 1 0 0 1 1 1v6.5')],
    'caption-close': [('d', 'line', 7, 7, 17, 17), ('d', 'line', 17, 7, 7, 17)],
    # ---- places and objects (full colour in the colour set)
    'folder': [FOLDER_BACK, FOLDER_FRONT],
    'folder-open': [FOLDER_BACK, ('b1', 'path', 'M5.2 10H22l-2.3 7.7A1.8 1.8 0 0 1 18 19H4.5A1.5 1.5 0 0 1 3.1 17z')],
    'folder-documents': [FOLDER_BACK, FOLDER_FRONT, ('e', 'line', 9, 13.2, 15, 13.2), ('e', 'line', 9, 16, 13, 16)],
    'folder-download': [FOLDER_BACK, FOLDER_FRONT, ('e', 'line', 12, 11.8, 12, 16.6), ('e', 'poly', [(9.6, 14.4), (12, 16.8), (14.4, 14.4)])],
    'file': [FILE, FILE_FOLD],
    'file-text': [FILE, FILE_FOLD, ('k', 'line', 8.5, 12.5, 15.5, 12.5), ('k', 'line', 8.5, 15.5, 15.5, 15.5),
                  ('k', 'line', 8.5, 9.5, 11, 9.5)],
    'user-home': [('b', 'path', 'M4.5 10.5 12 4.2l7.5 6.3v8.8a1 1 0 0 1-1 1h-13a1 1 0 0 1-1-1z'),
                  ('k', 'path', 'M10 20.3v-5.3h4v5.3'), ('ed', 'poly', [(3, 11.6), (12, 4.2), (21, 11.6)])],
    'user-desktop': [('b', 'rect', 3, 4, 18, 12, 2), ('ef', 'rect', 5.5, 6.5, 13, 7, 0.8),
                     ('d', 'line', 12, 16, 12, 19.5), ('d', 'line', 8, 20, 16, 20)],
    'computer': [('b', 'rect', 5, 5, 14, 10.5, 1.5), ('ef', 'rect', 7, 7, 10, 6.5, 0.6),
                 ('b1', 'path', 'M2.5 19h19l-1.6-3.5H4.1z')],
    'drive-fixed': [('b1', 'path', 'M5 12.5 7 6h10l2 6.5'), ('b', 'rect', 3, 12, 18, 7.5, 2),
                    ('k', 'line', 6.5, 15.75, 11, 15.75), ('dot', 'circle', 16.5, 15.75, 1.1)],
    'drive-removable': [('b1', 'rect', 9, 3.5, 6, 5.5, 0.6), ('b', 'rect', 7, 9, 10, 12, 2),
                        ('k', 'line', 10.75, 5.5, 10.75, 6.75), ('k', 'line', 13.25, 5.5, 13.25, 6.75),
                        ('dot', 'circle', 12, 13, 1.1)],
    'drive-optical': [('b', 'circle', 12, 12, 9), ('k', 'circle', 12, 12, 2.5), ('e', 'path', 'M12 5.5A6.5 6.5 0 0 1 18.5 12')],
    'recycle-bin-empty': BIN + TRASH_RIBS,
    'recycle-bin-full': [('p', 'path', 'M7.5 6.5 8.6 2.8l3.6 1.6 2.6-2 1.9 4.1z')] + BIN + TRASH_RIBS,
}

OBJECTS = {  # name -> colour category
    'folder': 'folder', 'folder-open': 'folder', 'folder-documents': 'folder',
    'folder-download': 'folder', 'file': 'paper', 'file-text': 'paper',
    'user-home': 'house', 'user-desktop': 'metal', 'computer': 'metal',
    'drive-fixed': 'metal', 'drive-removable': 'metal', 'drive-optical': 'disc',
    'recycle-bin-empty': 'glass', 'recycle-bin-full': 'glass',
}

# ------------------------------------------------------------------- styles
MONO = '#1B1B1B'
ORANGE = '#F5A623'
ACCENT = '#1F6FB2'
CATS = {  # category -> (b fill, b1 fill, stroke)
    'folder': ('#E8A93A', '#F7C55C', '#B67D14'),
    'paper': ('#FFFFFF', '#FFFFFF', '#7D8896'),
    'house': ('#FFFFFF', '#FFFFFF', '#6B7584'),
    'metal': ('#C9D0D9', '#E4E8ED', '#5E6876'),
    'disc': ('#DCE4EE', '#DCE4EE', '#5E6876'),
    'glass': ('#D6E4F2', '#D6E4F2', '#4F6E8E'),
}

SETS = {
    'setu-line': {'stroke': 1.5, 'mode': 'line', 'desc': 'thin outlines (the default)'},
    'setu-bold': {'stroke': 2.25, 'mode': 'line', 'desc': 'heavier outlines, easier to see'},
    'setu-duotone': {'stroke': 1.5, 'mode': 'duo', 'desc': 'outlines over a soft tinted fill'},
    'setu-solid': {'stroke': 1.75, 'mode': 'solid', 'desc': 'filled shapes with cut-out details'},
    'setu-colour': {'stroke': 1.5, 'mode': 'colour', 'desc': 'full-colour folders, files and drives; line glyphs'},
}


def shape(kind, args):
    if kind == 'path':
        return f'<path d="{args[0]}"/>'
    if kind == 'line':
        x1, y1, x2, y2 = args[:4]
        return f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}"/>'
    if kind == 'poly':
        return '<polyline points="' + ' '.join(f'{x},{y}' for x, y in args[0]) + '"/>'
    if kind == 'rect':
        x, y, w, h, r = args
        return f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{r}"/>'
    if kind == 'circle':
        cx, cy, r = args
        return f'<circle cx="{cx}" cy="{cy}" r="{r}"/>'
    raise ValueError(kind)


def g(content, **attrs):
    a = ' '.join(f'{k.replace("_", "-")}="{v}"' for k, v in attrs.items())
    return f'<g {a}>{content}</g>'


def svg(name, style):
    sw = style['stroke']
    mode = style['mode']
    els = ICONS[name]
    cat = OBJECTS.get(name)
    if mode == 'colour' and not cat:
        mode = 'line'
    out = []
    logo = [e for e in els if e[0] == 'logo']
    rest = [e for e in els if e[0] != 'logo']
    if mode in ('line', 'duo'):
        for role, kind, *args in rest:
            s = shape(kind, args)
            if role == 'dot':
                out.append(g(s, fill=MONO))
                continue
            if mode == 'duo' and role in ('b', 'b1', 'p', 'ef'):
                out.append(g(s, fill=MONO, fill_opacity='0.18' if role != 'ef' else '0.3'))
            out.append(g(s, stroke=MONO, stroke_width=sw))
    elif mode == 'solid':
        fills = [e for e in rest if e[0] in ('b', 'b1', 'p')]
        if not fills:
            for role, kind, *args in rest:
                out.append(g(shape(kind, args), stroke=MONO, stroke_width=sw, fill=MONO if role == 'dot' else 'none'))
        else:
            white, black = [], []
            for role, kind, *args in rest:
                s = shape(kind, args)
                if role in ('b', 'b1', 'p'):
                    white.append(g(s, fill='#fff', stroke='#fff', stroke_width=sw))
                elif role in ('d', 'ed'):
                    white.append(g(s, stroke='#fff', stroke_width=sw))
                elif role in ('k', 'e'):
                    black.append(g(s, stroke='#000', stroke_width=sw))
                elif role in ('ef', 'dot'):
                    black.append(g(s, fill='#000'))
            out.append(f'<mask id="m" maskUnits="userSpaceOnUse" x="0" y="0" width="24" height="24">'
                       f'{"".join(white)}{"".join(black)}</mask>'
                       f'<rect width="24" height="24" fill="{MONO}" mask="url(#m)"/>')
    else:  # colour
        bfill, b1fill, stroke = CATS[cat]
        for role, kind, *args in rest:
            s = shape(kind, args)
            if role == 'b':
                out.append(g(s, fill=bfill, stroke=stroke, stroke_width=sw))
            elif role == 'b1':
                out.append(g(s, fill=b1fill, stroke=stroke, stroke_width=sw))
            elif role == 'p':
                out.append(g(s, fill='#FFFFFF', stroke=CATS['paper'][2], stroke_width=sw))
            elif role in ('d', 'k'):
                out.append(g(s, stroke=stroke, stroke_width=sw))
            elif role in ('e', 'ed'):
                out.append(g(s, stroke=ACCENT, stroke_width=sw + 0.25))
            elif role == 'ef':
                out.append(g(s, fill=ACCENT, stroke=stroke, stroke_width=sw * 0.6))
            elif role == 'dot':
                out.append(g(s, fill=ACCENT))
    for role, kind, *args in logo:
        w = args[-1]
        colour = ORANGE if style['mode'] == 'colour' else MONO
        out.append(g(shape(kind, args[:-1]), stroke=colour, stroke_width=f'{w:.3f}'))
    return ('<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" '
            'fill="none" stroke-linecap="round" stroke-linejoin="round">'
            + ''.join(out) + '</svg>\n')


# ---------------------------------------------------------------------- logo

def logo_svgs():
    """The Setu mark and its variants, on the 128 design grid."""
    def mark(colour, extra=''):
        k = 1
        cx, cy, r = 64, 82, 44
        parts = [f'<path d="M{cx - r} {cy}A{r} {r} 0 0 1 {cx + r} {cy}" stroke-width="10"/>']
        for x in (42, 64, 86):
            top = cy - math.sqrt(max((r - 5) ** 2 - (x - cx) ** 2, 0))
            parts.append(f'<line x1="{x}" y1="{top:.2f}" x2="{x}" y2="82" stroke-width="7"/>')
        parts.append('<line x1="12" y1="84" x2="116" y2="84" stroke-width="9"/>')
        return f'<g fill="none" stroke="{colour}" stroke-linecap="round" {extra}>' + ''.join(parts) + '</g>'

    head = '<svg xmlns="http://www.w3.org/2000/svg" width="128" height="128" viewBox="0 0 128 128">'
    tile = ('<defs><linearGradient id="t" x1="0" y1="0" x2="1" y2="1">'
            '<stop offset="0" stop-color="#1F6FB2"/><stop offset="1" stop-color="#0B2E63"/></linearGradient></defs>'
            '<rect x="4" y="4" width="120" height="120" rx="28" fill="url(#t)"/>')
    return {
        'setu-mark.svg': head + mark(ORANGE) + '</svg>\n',
        'setu-mark-mono.svg': head + mark(MONO) + '</svg>\n',
        'setu-mark-white.svg': head + mark('#FFFFFF') + '</svg>\n',
        'setu-app.svg': head + tile + mark(ORANGE, 'transform="translate(64 66) scale(0.78) translate(-64 -66)"') + '</svg>\n',
    }


# ----------------------------------------------------------------------- CLI

def build(outdir):
    for sname, st in SETS.items():
        d = os.path.join(outdir, sname)
        os.makedirs(d, exist_ok=True)
        for name in ICONS:
            with open(os.path.join(d, name + '.svg'), 'w') as f:
                f.write(svg(name, st))
        with open(os.path.join(d, 'index.ini'), 'w') as f:
            f.write(f'; {sname}: {st["desc"]}. Generated by art/icons/icongen.py.\n[Icons]\n')
            for name in ICONS:
                f.write(f'{name}={name}.ico\n')
    ld = os.path.join(outdir, 'logo')
    os.makedirs(ld, exist_ok=True)
    for fn, s in logo_svgs().items():
        with open(os.path.join(ld, fn), 'w') as f:
            f.write(s)


def render_svg(text, size):
    import warnings
    warnings.filterwarnings('ignore', category=DeprecationWarning)
    import cairo
    import gi
    gi.require_version('Rsvg', '2.0')
    from gi.repository import Rsvg
    h = Rsvg.Handle.new_from_data(text.encode())
    surf = cairo.ImageSurface(cairo.FORMAT_ARGB32, size, size)
    ctx = cairo.Context(surf)
    vp = Rsvg.Rectangle()
    vp.x, vp.y, vp.width, vp.height = 0, 0, size, size
    h.render_document(ctx, vp)
    return surf


def sheet(out, size=32):
    import cairo
    names = list(ICONS)
    pad, cell, label_w = 16, size + 16, 130
    W = label_w + len(names) * cell + pad
    rows = list(SETS)
    H = pad + (len(rows) + 1) * (cell + 8) + 150
    surf = cairo.ImageSurface(cairo.FORMAT_RGB24, W, H)
    ctx = cairo.Context(surf)
    ctx.set_source_rgb(0.953, 0.953, 0.953)
    ctx.paint()
    ctx.select_font_face('Inter')
    ctx.set_font_size(12)
    for r, sname in enumerate(rows):
        y = pad + r * (cell + 8)
        ctx.set_source_rgb(0.1, 0.1, 0.1)
        ctx.move_to(pad, y + cell / 2 + 4)
        ctx.show_text(sname)
        for i, n in enumerate(names):
            ctx.set_source_surface(render_svg(svg(n, SETS[sname]), size), label_w + i * cell + 8, y + 8)
            ctx.paint()
    # logo variants, large
    y = pad + len(rows) * (cell + 8) + 10
    ctx.set_source_rgb(0.1, 0.1, 0.1)
    ctx.move_to(pad, y + 60)
    ctx.show_text('logo')
    x = label_w
    for fn, s in logo_svgs().items():
        if 'white' in fn:
            ctx.set_source_rgb(0.12, 0.31, 0.54)
            ctx.rectangle(x, y, 128, 128)
            ctx.fill()
        ctx.set_source_surface(render_svg(s, 128), x, y)
        ctx.paint()
        x += 150
    surf.write_to_png(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    sub = ap.add_subparsers(dest='cmd', required=True)
    b = sub.add_parser('build')
    b.add_argument('-o', '--out', default=os.path.join(os.path.dirname(os.path.abspath(__file__)), 'sets'))
    s = sub.add_parser('sheet')
    s.add_argument('-o', '--out', required=True)
    s.add_argument('--size', type=int, default=32)
    sub.add_parser('list')
    a = ap.parse_args()
    if a.cmd == 'build':
        build(a.out)
    elif a.cmd == 'sheet':
        sheet(a.out, a.size)
    else:
        for k, v in SETS.items():
            print(f'{k:14} {v["desc"]}')
        print(f'{len(ICONS)} icons:', ' '.join(ICONS))


if __name__ == '__main__':
    sys.exit(main())
