#!/usr/bin/env python3
# SPDX-License-Identifier: MIT OR Apache-2.0
"""Setu app icons for Wine's accessories in the Start menu.

Each icon is a white line glyph on the Setu app tile (the rounded Setu-blue
square of art/icons/icongen.py's setu-app logo), so the accessories match
the Start button. Original artwork, drawn here as simple geometry on a
24-unit grid.

Usage: distro/apps/icons.py OUTDIR NAME...   (writes OUTDIR/NAME.ico)
Names: wordpad, clock, notepad
"""

import io
import os
import sys
import warnings

SIZES = [16, 20, 24, 32, 48, 64, 256]

GLYPHS = {
    # a page with text lines and a pen nib
    'wordpad': '<path d="M6.5 4h7l4 4v11.5a1 1 0 0 1-1 1h-10a1 1 0 0 1-1-1v-14.5a1 1 0 0 1 1-1z"/>'
               '<path d="M13.5 4v4h4"/><path d="M8.5 11.5h6M8.5 14.5h6M8.5 17.5h3.5"/>',
    # a dial with two hands
    'clock': '<circle cx="12" cy="12" r="8"/><path d="M12 7.5V12l3 2"/>',
    # a page with lines
    'notepad': '<rect x="6" y="4.5" width="12" height="16" rx="1.5"/>'
               '<path d="M9 3v3M12 3v3M15 3v3M9 10.5h6M9 13.5h6M9 16.5h4"/>',
}


def svg(name):
    return ('<svg xmlns="http://www.w3.org/2000/svg" width="128" height="128" viewBox="0 0 128 128">'
            '<defs><linearGradient id="t" x1="0" y1="0" x2="1" y2="1">'
            '<stop offset="0" stop-color="#1F6FB2"/><stop offset="1" stop-color="#0B2E63"/></linearGradient></defs>'
            '<rect x="4" y="4" width="120" height="120" rx="28" fill="url(#t)"/>'
            '<g transform="translate(16 16) scale(4)" fill="none" stroke="#FFFFFF" stroke-width="1.6" '
            'stroke-linecap="round" stroke-linejoin="round">' + GLYPHS[name] + '</g></svg>')


def render(text, size):
    warnings.filterwarnings('ignore', category=DeprecationWarning)
    import cairo
    import gi
    gi.require_version('Rsvg', '2.0')
    from gi.repository import Rsvg
    from PIL import Image
    h = Rsvg.Handle.new_from_data(text.encode())
    surf = cairo.ImageSurface(cairo.FORMAT_ARGB32, size, size)
    ctx = cairo.Context(surf)
    vp = Rsvg.Rectangle()
    vp.x, vp.y, vp.width, vp.height = 0, 0, size, size
    h.render_document(ctx, vp)
    buf = io.BytesIO()
    surf.write_to_png(buf)
    buf.seek(0)
    return Image.open(buf).convert('RGBA')


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    for name in sys.argv[2:]:
        if name not in GLYPHS:
            sys.exit('icons.py: no icon named %s' % name)
        imgs = [render(svg(name), s) for s in SIZES]
        imgs[-1].save(os.path.join(out, name + '.ico'), format='ICO',
                      sizes=[(s, s) for s in SIZES], append_images=imgs[:-1])
    return 0


if __name__ == '__main__':
    sys.exit(main())
