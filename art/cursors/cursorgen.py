#!/usr/bin/env python3
# SPDX-License-Identifier: MIT OR Apache-2.0
"""Setu's generated mouse cursors.

Each cursor is a vector drawing on a 32-unit grid with a hot spot. One
drawing is written in two formats:
  - Windows cursors (.cur, and .ani for the animated busy cursors) for Wine
    programs, listed in a pack's .theme [Control Panel\\Cursors];
  - an X cursor theme (Xcursor files + index.theme) for labwc and Linux
    programs.
Both formats are public: the .cur/.ani layout is the ICO directory with hot
spots (learn.microsoft.com, "Icons" and "About Cursors") and RIFF ACON; the
Xcursor layout is documented in libXcursor's Xcursor(3).

    cursorgen.py build -o OUTDIR     OUTDIR/<set>/{windows,x11}/...
    cursorgen.py sheet -o SHEET.png
    cursorgen.py list
"""
import argparse
import math
import os
import struct
import sys

import cairo

SIZES = (24, 32, 48, 64)
WAIT_FRAMES = 12

SETS = {  # fill, outline, accent
    'setu-light': ('#FFFFFF', '#1B1B1B', '#1F6FB2'),
    'setu-dark': ('#1B1B1B', '#FFFFFF', '#5AA4E2'),
    'setu-accent': ('#1F6FB2', '#FFFFFF', '#F5A623'),
}


def hx(c):
    c = c.lstrip('#')
    return tuple(int(c[i:i + 2], 16) / 255 for i in (0, 2, 4))


# --------------------------------------------------------------- drawings
# Each draw(ctx, fill, line, accent, t) works on the 32 grid; t is the
# animation phase 0..1. Shapes are filled with FILL and outlined with LINE.

def body(ctx, fill, line, w=1.5):
    ctx.set_line_join(cairo.LINE_JOIN_ROUND)
    ctx.set_line_cap(cairo.LINE_CAP_ROUND)
    ctx.set_source_rgb(*line)
    ctx.set_line_width(w * 2)
    ctx.stroke_preserve()
    ctx.set_source_rgb(*fill)
    ctx.fill()


def arrow_path(ctx, s=1.0, x=0, y=0):
    pts = [(4, 3), (4, 24), (9.2, 19.4), (12.6, 27.2), (16.2, 25.6), (12.9, 18), (19.8, 17.8)]
    ctx.move_to(x + pts[0][0] * s, y + pts[0][1] * s)
    for px, py in pts[1:]:
        ctx.line_to(x + px * s, y + py * s)
    ctx.close_path()


def spinner(ctx, cx, cy, r, accent, line, t, w=3):
    ctx.set_line_cap(cairo.LINE_CAP_ROUND)
    ctx.set_source_rgb(*line)
    ctx.set_line_width(w + 2)
    ctx.arc(cx, cy, r, 0, 2 * math.pi)
    ctx.stroke()
    ctx.set_source_rgba(*accent, 0.25)
    ctx.set_line_width(w)
    ctx.arc(cx, cy, r, 0, 2 * math.pi)
    ctx.stroke()
    a0 = t * 2 * math.pi - math.pi / 2
    ctx.set_source_rgb(*accent)
    ctx.arc(cx, cy, r, a0, a0 + 1.9)
    ctx.stroke()


def d_arrow(ctx, f, l, a, t):
    arrow_path(ctx); body(ctx, f, l)


def d_progress(ctx, f, l, a, t):
    arrow_path(ctx, 0.85); body(ctx, f, l)
    spinner(ctx, 22, 22, 5.5, a, l, t, 2.6)


def d_wait(ctx, f, l, a, t):
    ctx.arc(16, 16, 11.5, 0, 2 * math.pi)
    body(ctx, f, l, 1.25)
    spinner(ctx, 16, 16, 7.5, a, f, t, 3.2)


def d_text(ctx, f, l, a, t):
    def beam():
        ctx.move_to(12, 5); ctx.curve_to(14.5, 5, 16, 6, 16, 8)
        ctx.curve_to(16, 6, 17.5, 5, 20, 5)
        ctx.move_to(16, 8); ctx.line_to(16, 24)
        ctx.move_to(12, 27); ctx.curve_to(14.5, 27, 16, 26, 16, 24)
        ctx.curve_to(16, 26, 17.5, 27, 20, 27)
        ctx.move_to(13.5, 16); ctx.line_to(18.5, 16)
    ctx.set_line_cap(cairo.LINE_CAP_ROUND)
    beam(); ctx.set_source_rgb(*l); ctx.set_line_width(4.6); ctx.stroke()
    beam(); ctx.set_source_rgb(*f); ctx.set_line_width(1.8); ctx.stroke()


def d_hand(ctx, f, l, a, t):
    ctx.move_to(11, 17.5)
    ctx.line_to(11, 5.5); ctx.curve_to(11, 3.2, 14.6, 3.2, 14.6, 5.5)
    ctx.line_to(14.6, 12.5)
    ctx.curve_to(14.6, 10.4, 18, 10.4, 18, 12.6)
    ctx.curve_to(18, 10.8, 21.3, 10.8, 21.3, 13.2)
    ctx.curve_to(21.3, 11.6, 24.5, 11.6, 24.5, 14)
    ctx.line_to(24.5, 20); ctx.curve_to(24.5, 25, 21.5, 28, 17.5, 28)
    ctx.line_to(15.5, 28); ctx.curve_to(12.5, 28, 11, 26.5, 9.2, 24.2)
    ctx.line_to(6, 19.8); ctx.curve_to(4.9, 18.2, 6.6, 16.3, 8.3, 17.4)
    ctx.close_path()
    body(ctx, f, l)
    ctx.set_source_rgb(*l); ctx.set_line_width(1.1)
    for x in (14.6, 18, 21.3):
        ctx.move_to(x, 13.5); ctx.line_to(x, 17.5)
    ctx.stroke()


def d_cross(ctx, f, l, a, t):
    def lines():
        for x1, y1, x2, y2 in ((16, 3, 16, 12), (16, 20, 16, 29), (3, 16, 12, 16), (20, 16, 29, 16)):
            ctx.move_to(x1, y1); ctx.line_to(x2, y2)
    ctx.set_line_cap(cairo.LINE_CAP_ROUND)
    lines(); ctx.set_source_rgb(*l); ctx.set_line_width(4.4); ctx.stroke()
    lines(); ctx.set_source_rgb(*f); ctx.set_line_width(1.8); ctx.stroke()
    ctx.arc(16, 16, 1.6, 0, 7); body(ctx, f, l, 1)


def double_arrow(ctx, f, l, angle, both=True):
    ctx.save()
    ctx.translate(16, 16); ctx.rotate(angle)
    # a horizontal double arrow centred at 0,0
    ctx.move_to(-12, 0); ctx.line_to(-6, -6); ctx.line_to(-6, -2.3); ctx.line_to(6, -2.3)
    ctx.line_to(6, -6); ctx.line_to(12, 0); ctx.line_to(6, 6); ctx.line_to(6, 2.3)
    ctx.line_to(-6, 2.3); ctx.line_to(-6, 6); ctx.close_path()
    ctx.restore()
    body(ctx, f, l)


def d_ew(ctx, f, l, a, t): double_arrow(ctx, f, l, 0)
def d_ns(ctx, f, l, a, t): double_arrow(ctx, f, l, math.pi / 2)
def d_nwse(ctx, f, l, a, t): double_arrow(ctx, f, l, math.pi / 4)
def d_nesw(ctx, f, l, a, t): double_arrow(ctx, f, l, -math.pi / 4)


def d_move(ctx, f, l, a, t):
    ctx.save(); ctx.translate(16, 16)
    pts = []
    for k in range(4):
        ang = -k * math.pi / 2
        for px, py in ((2.2, -8), (5, -8), (0, -13), (-5, -8), (-2.2, -8), (-2.2, -2.2)):
            c, s = math.cos(ang), math.sin(ang)
            pts.append((px * c - py * s, px * s + py * c))
    ctx.move_to(*pts[-1])
    for p in pts:
        ctx.line_to(*p)
    ctx.close_path(); ctx.restore()
    body(ctx, f, l)


def d_no(ctx, f, l, a, t):
    ctx.arc(16, 16, 11, 0, 2 * math.pi)
    ctx.new_sub_path(); ctx.arc_negative(16, 16, 7.2, 0, -2 * math.pi)
    ctx.set_fill_rule(cairo.FILL_RULE_EVEN_ODD)
    body(ctx, hx('#C42B1C'), l, 1.1)
    ctx.save(); ctx.translate(16, 16); ctx.rotate(math.pi / 4)
    ctx.rectangle(-1.9, -8.6, 3.8, 17.2); ctx.restore()
    ctx.set_source_rgb(*hx('#C42B1C')); ctx.fill()


def d_help(ctx, f, l, a, t):
    arrow_path(ctx, 0.85); body(ctx, f, l)
    ctx.arc(23, 21, 6.5, 0, 2 * math.pi); body(ctx, a, l, 1)
    ctx.set_source_rgb(1, 1, 1); ctx.set_line_width(1.8); ctx.set_line_cap(cairo.LINE_CAP_ROUND)
    ctx.arc(23, 19.4, 2.2, math.pi * 1.05, math.pi * 2.3); ctx.line_to(23, 22.2); ctx.stroke()
    ctx.arc(23, 24.8, 1.05, 0, 7); ctx.fill()


def d_pen(ctx, f, l, a, t):
    ctx.move_to(4, 28); ctx.line_to(5.5, 21.5); ctx.line_to(21, 6); ctx.line_to(26, 11)
    ctx.line_to(10.5, 26.5); ctx.close_path()
    body(ctx, f, l)
    ctx.set_source_rgb(*l); ctx.set_line_width(1.2)
    ctx.move_to(18, 9); ctx.line_to(23, 14); ctx.stroke()


def d_up(ctx, f, l, a, t):
    ctx.move_to(16, 3); ctx.line_to(25, 13); ctx.line_to(19, 13); ctx.line_to(19, 28)
    ctx.line_to(13, 28); ctx.line_to(13, 13); ctx.line_to(7, 13); ctx.close_path()
    body(ctx, f, l)


# name: (draw, hot x, hot y, animated, Windows role, X names)
CURSORS = {
    'arrow': (d_arrow, 4, 3, False, 'Arrow', ['default', 'left_ptr', 'arrow', 'top_left_arrow']),
    'help': (d_help, 4, 3, False, 'Help', ['help', 'question_arrow', 'whats_this', 'left_ptr_help']),
    'progress': (d_progress, 4, 3, True, 'AppStarting', ['progress', 'left_ptr_watch', 'half-busy']),
    'wait': (d_wait, 16, 16, True, 'Wait', ['wait', 'watch']),
    'crosshair': (d_cross, 16, 16, False, 'Crosshair', ['crosshair', 'cross', 'tcross']),
    'text': (d_text, 16, 16, False, 'IBeam', ['text', 'xterm', 'ibeam']),
    'pen': (d_pen, 4, 28, False, 'NWPen', ['pencil', 'draft']),
    'not-allowed': (d_no, 16, 16, False, 'No', ['not-allowed', 'no-drop', 'forbidden', 'circle', 'crossed_circle']),
    'ns-resize': (d_ns, 16, 16, False, 'SizeNS', ['ns-resize', 'size_ver', 'sb_v_double_arrow', 'v_double_arrow', 'n-resize', 's-resize', 'row-resize', 'top_side', 'bottom_side']),
    'ew-resize': (d_ew, 16, 16, False, 'SizeWE', ['ew-resize', 'size_hor', 'sb_h_double_arrow', 'h_double_arrow', 'e-resize', 'w-resize', 'col-resize', 'left_side', 'right_side']),
    'nwse-resize': (d_nwse, 16, 16, False, 'SizeNWSE', ['nwse-resize', 'size_fdiag', 'nw-resize', 'se-resize', 'top_left_corner', 'bottom_right_corner']),
    'nesw-resize': (d_nesw, 16, 16, False, 'SizeNESW', ['nesw-resize', 'size_bdiag', 'ne-resize', 'sw-resize', 'top_right_corner', 'bottom_left_corner']),
    'move': (d_move, 16, 16, False, 'SizeAll', ['move', 'fleur', 'all-scroll', 'size_all', 'grabbing']),
    'up-arrow': (d_up, 16, 3, False, 'UpArrow', ['up-arrow', 'center_ptr', 'sb_up_arrow']),
    'pointer': (d_hand, 12.8, 4, False, 'Hand', ['pointer', 'hand2', 'hand1', 'hand', 'pointing_hand', 'grab', 'openhand']),
}


# --------------------------------------------------------------- rendering

def render(name, set_id, size, t=0.0):
    """-> BGRA premultiplied bytes (cairo ARGB32, little endian), size x size."""
    draw = CURSORS[name][0]
    f, l, a = (hx(c) for c in SETS[set_id])
    surf = cairo.ImageSurface(cairo.FORMAT_ARGB32, size, size)
    ctx = cairo.Context(surf)
    ctx.scale(size / 32, size / 32)
    draw(ctx, f, l, a, t)
    surf.flush()
    return surf


def unpremul(surf):
    data = bytearray(surf.get_data())
    for i in range(0, len(data), 4):
        al = data[i + 3]
        if 0 < al < 255:
            for k in range(3):
                data[i + k] = min(255, data[i + k] * 255 // al)
    return bytes(data)


def frames(name, set_id, size):
    n = WAIT_FRAMES if CURSORS[name][3] else 1
    return [render(name, set_id, size, i / n) for i in range(n)]


def cur_image(surf, size):
    """One CUR directory image: BITMAPINFOHEADER + bottom-up BGRA + AND mask."""
    px = unpremul(surf)
    stride = size * 4
    rows = b''.join(px[(size - 1 - y) * stride:(size - y) * stride] for y in range(size))
    mask_stride = ((size + 31) // 32) * 4
    mask = b'\0' * (mask_stride * size)
    hdr = struct.pack('<IiiHHIIiiII', 40, size, size * 2, 1, 32, 0, len(rows) + len(mask), 0, 0, 0, 0)
    return hdr + rows + mask


def cur_file(images):
    """images: [(size, hotx, hoty, data)] -> .cur bytes."""
    out = struct.pack('<HHH', 0, 2, len(images))
    off = 6 + 16 * len(images)
    body_ = b''
    for size, hx_, hy, data in images:
        out += struct.pack('<BBBBHHII', size % 256, size % 256, 0, 0, hx_, hy, len(data), off + len(body_))
        body_ += data
    return out + body_


def ani_file(frame_curs, jiffies=4):
    def chunk(tag, data):
        pad = b'\0' if len(data) % 2 else b''
        return tag + struct.pack('<I', len(data)) + data + pad
    n = len(frame_curs)
    anih = struct.pack('<9I', 36, n, n, 0, 0, 0, 0, jiffies, 1)  # flags: AF_ICON
    icons = b''.join(chunk(b'icon', c) for c in frame_curs)
    fram = b'fram' + icons
    data = b'ACON' + chunk(b'anih', anih) + chunk(b'LIST', fram)
    return b'RIFF' + struct.pack('<I', len(data)) + data


def xcursor_file(name, set_id):
    """Xcursor: header, TOC of image chunks, then chunks (nominal size per image)."""
    chunks = []
    _, hxp, hyp = CURSORS[name][:3]
    anim = CURSORS[name][3]
    for size in SIZES:
        for surf in frames(name, set_id, size):
            px = bytes(surf.get_data())  # premultiplied ARGB32 little endian = Xcursor's format
            hdr = struct.pack('<IIIIIIIII', 36, 0xfffd0002, size, 1, size, size,
                              round(hxp * size / 32), round(hyp * size / 32), 50 if anim else 0)
            chunks.append((size, hdr + px))
    ntoc = len(chunks)
    out = struct.pack('<4sIII', b'Xcur', 16, 0x10000, ntoc)
    pos = 16 + ntoc * 12
    toc, data = b'', b''
    for size, c in chunks:
        toc += struct.pack('<III', 0xfffd0002, size, pos + len(data))
        data += c
    return out + toc + data


def build(outdir):
    for set_id in SETS:
        wdir = os.path.join(outdir, set_id, 'windows')
        xdir = os.path.join(outdir, set_id, 'x11', set_id, 'cursors')
        os.makedirs(wdir, exist_ok=True)
        os.makedirs(xdir, exist_ok=True)
        roles = []
        for name, (draw, hxp, hyp, anim, role, xnames) in CURSORS.items():
            win_sizes = (32, 48, 64)
            if anim:
                per_frame = [[] for _ in range(WAIT_FRAMES)]
                for size in win_sizes:
                    for i, surf in enumerate(frames(name, set_id, size)):
                        per_frame[i].append((size, round(hxp * size / 32), round(hyp * size / 32), cur_image(surf, size)))
                fn = name + '.ani'
                with open(os.path.join(wdir, fn), 'wb') as f:
                    f.write(ani_file([cur_file(imgs) for imgs in per_frame]))
            else:
                imgs = [(s, round(hxp * s / 32), round(hyp * s / 32), cur_image(render(name, set_id, s), s)) for s in win_sizes]
                fn = name + '.cur'
                with open(os.path.join(wdir, fn), 'wb') as f:
                    f.write(cur_file(imgs))
            roles.append((role, fn))
            with open(os.path.join(xdir, xnames[0]), 'wb') as f:
                f.write(xcursor_file(name, set_id))
            for alias in xnames[1:]:
                p = os.path.join(xdir, alias)
                if os.path.lexists(p):
                    os.remove(p)
                os.symlink(xnames[0], p)
        with open(os.path.join(wdir, 'cursors.ini'), 'w') as f:
            f.write(f'; {set_id}: lines for a .theme [Control Panel\\Cursors] section.\n'
                    f'; Generated by art/cursors/cursorgen.py; DIR = where these files are installed.\n')
            f.write('[Control Panel\\Cursors]\n')
            f.write(f'DefaultValue={set_id}\n')
            for role, fn in roles:
                f.write(f'{role}=DIR\\{fn}\n')
        with open(os.path.join(outdir, set_id, 'x11', set_id, 'index.theme'), 'w') as f:
            f.write(f'[Icon Theme]\nName={set_id}\nComment=Setu cursors, generated by art/cursors/cursorgen.py\n')


def sheet(out):
    names = list(CURSORS)
    S, pad = 48, 14
    W = 120 + len(names) * (S + pad)
    H = pad + len(SETS) * (S + pad) + 20
    surf = cairo.ImageSurface(cairo.FORMAT_RGB24, W, H)
    ctx = cairo.Context(surf)
    ctx.select_font_face('Inter'); ctx.set_font_size(11)
    for r, set_id in enumerate(SETS):
        y = pad + r * (S + pad)
        bg = (0.86, 0.88, 0.91) if r != 1 else (0.30, 0.34, 0.40)
        ctx.set_source_rgb(*bg); ctx.rectangle(0, y - pad / 2, W, S + pad); ctx.fill()
        ctx.set_source_rgb(*((0.1,) * 3 if r != 1 else (1, 1, 1)))
        ctx.move_to(10, y + S / 2 + 4); ctx.show_text(set_id)
        for i, n in enumerate(names):
            ctx.set_source_surface(render(n, set_id, S, 0.15), 120 + i * (S + pad), y)
            ctx.paint()
    ctx.set_source_rgb(0.95, 0.95, 0.95); ctx.rectangle(0, H - 20, W, 20); ctx.fill()
    ctx.set_source_rgb(0.2, 0.2, 0.2); ctx.set_font_size(9)
    for i, n in enumerate(names):
        ctx.move_to(120 + i * (S + pad), H - 7); ctx.show_text(n[:10])
    surf.write_to_png(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    sub = ap.add_subparsers(dest='cmd', required=True)
    b = sub.add_parser('build'); b.add_argument('-o', '--out', required=True)
    s = sub.add_parser('sheet'); s.add_argument('-o', '--out', required=True)
    sub.add_parser('list')
    a = ap.parse_args()
    if a.cmd == 'build':
        build(a.out)
    elif a.cmd == 'sheet':
        sheet(a.out)
    else:
        print('sets:', ' '.join(SETS))
        for n, c in CURSORS.items():
            print(f'{n:12} {c[4]:12} {"animated" if c[3] else ""}')


if __name__ == '__main__':
    sys.exit(main())
