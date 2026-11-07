#!/usr/bin/env python3
# SPDX-License-Identifier: MIT OR Apache-2.0
"""Setu Light's wallpaper ("silk"): soft gradient waves on a deep background
with one thin ribbon in a highlight colour (docs/DESIGN.md: blues with the
Setu orange #F5A623). Original artwork, drawn here from code, so it can be
rebuilt at any size; nothing is traced or copied.

The colours come from the pack's [Wallpaper] section (theme.ini), so a pack
that inherits this script gets the same picture in its own colours;
distro/themes/render-packs.py calls render() with them.

The picture is defined on a 16:9 canvas; other sizes are a centred crop of
it scaled to cover them (what a "fill" wallpaper does), so every size shows
the same composition. Output is deterministic (ordered dithering, no
random noise).

    wallpaper.py WxH OUT.{png,bmp} [WxH OUT ...]

Needs python3-numpy and python3-pil."""
import sys

import numpy as np
from PIL import Image


def hexrgb(h):
    h = h.lstrip('#')
    return np.array([int(h[i:i + 2], 16) / 255.0 for i in (0, 2, 4)], dtype=np.float32)


# The default palette (setu-light's [Wallpaper]): a deep blue background,
# blue silk ribbons ("light dark": the lit and the shaded side of the fold)
# and one thin orange ribbon.
PALETTE = {
    'background-dark': '#06183A',
    'background-mid': '#0D3778',
    'bloom': '#2F7BD6',
    'ribbon-1': '#1F5FB4 #0B2E6B',
    'ribbon-2': '#3D86DE #123F8A',
    'ribbon-3': '#6FB0F2 #1C55A6',
    'ribbon-4': '#A9D3FA #2A6CC0',
    'highlight': '#F5A623',
}

# Ribbons, drawn back to front. Each is a band around a centre line that
# rises from the lower left to the upper right; its width breathes along x
# (which reads as a twist) and it is shaded across like a fold: light at
# its upper edge, darker at the lower one (colours: ribbon-N).
# (y at left, y at right, (amp, freq, phase)..., width, twist (freq,
#  phase), opacity)
RIBBONS = [
    (0.98, 0.30, [(0.06, 0.8, 0.4), (0.02, 2.0, 1.0)], 0.20, (0.55, 0.6), 0.75),
    (0.92, 0.40, [(0.07, 0.7, 1.6), (0.02, 1.7, 2.2)], 0.15, (0.70, 2.0), 0.80),
    (0.86, 0.50, [(0.06, 0.9, 2.6), (0.02, 2.2, 0.3)], 0.11, (0.80, 3.1), 0.85),
    (0.96, 0.58, [(0.05, 1.0, 3.3), (0.015, 2.6, 1.7)], 0.08, (0.95, 4.0), 0.80),
]
# the highlight ribbon runs along the upper edge of the third one
HIGHLIGHT_RIBBON = (0.86, 0.50, [(0.06, 0.9, 2.6), (0.02, 2.2, 0.3)], -0.105, 0.0055)

ASPECT = 16 / 9


def centre(x, left, right, waves):
    u = x / ASPECT
    y = left + (right - left) * u
    for amp, freq, phase in waves:
        y = y + amp * np.sin(2 * np.pi * freq * u + phase)
    return y


def mix(a, b, t):
    return a + (b - a) * t


def render(w, h, palette=None):
    """The wallpaper at W x H as a PIL image, in PALETTE updated with
    PALETTE (drawn in bands of rows, so a 4K render needs ~150 MB, not
    ~1 GB)."""
    pal = dict(PALETTE, **{k: v for k, v in (palette or {}).items() if k in PALETTE})
    out = np.empty((h, w, 3), dtype=np.uint8)
    for r0 in range(0, h, 256):
        out[r0:r0 + 256] = render_rows(w, h, r0, min(r0 + 256, h), pal)
    return Image.fromarray(out, 'RGB')


def render_rows(w, h, r0, r1, pal):
    BG_DARK, BG_MID, BLOOM, ORANGE = (hexrgb(pal[k]) for k in
                                      ('background-dark', 'background-mid', 'bloom', 'highlight'))
    # canvas coordinates: x in [0, ASPECT], y in [0, 1], scaled to cover w x h
    scale = max(w / ASPECT, h)  # pixels per canvas unit
    xs = (np.arange(w, dtype=np.float32) + 0.5 - w / 2) / scale + ASPECT / 2
    ys = (np.arange(r0, r1, dtype=np.float32) + 0.5 - h / 2) / scale + 0.5
    x, y = np.meshgrid(xs, ys)
    u = x / ASPECT

    # background: dark at the corners, a soft blue glow behind the ribbons
    r = np.sqrt(((u - 0.55) / 0.75) ** 2 + ((y - 0.55) / 0.75) ** 2)[..., None]
    img = mix(BG_MID, BG_DARK, np.clip(r, 0, 1) ** 1.3)
    glow = np.exp(-(((u - 0.62) / 0.35) ** 2 + ((y - 0.48) / 0.30) ** 2))[..., None]
    img = mix(img, BLOOM, 0.55 * glow)

    for i, (left, right, waves, width, (tf, tp), opacity) in enumerate(RIBBONS):
        light, dark = pal[f'ribbon-{i + 1}'].split()
        yc = centre(x, left, right, waves)
        hw = width * (0.30 + 0.70 * np.cos(np.pi * tf * u + tp) ** 2)
        s = (y - yc) / hw  # -1 at the upper edge, +1 at the lower one
        edge = np.clip((1 - np.abs(s)) * hw * scale / 1.5, 0, 1)  # ~1.5 px edges
        edge = edge * edge * (3 - 2 * edge)
        # shading across the fold: a highlight near the upper edge
        t = np.clip((s + 1) / 2, 0, 1)
        shade = mix(hexrgb(light), hexrgb(dark), (t ** 0.8)[..., None])
        hl = np.exp(-((s + 0.72) / 0.18) ** 2)[..., None]
        shade = mix(shade, np.ones(3, np.float32), 0.35 * hl)
        # thin parts (the twist) catch more light
        shade = mix(shade, hexrgb(light), (0.35 * (1 - hw / width))[..., None])
        a = (opacity * edge)[..., None]
        img = mix(img, shade, a)
        # a faint soft shadow under each ribbon
        sh = np.exp(-(np.maximum(s - 1, 0) / 0.6) ** 2) * (s > 1)
        img = img * (1 - 0.18 * sh[..., None])

    left, right, waves, offset, width = HIGHLIGHT_RIBBON
    yc = centre(x, left, right, waves) + offset * (0.30 + 0.70 * np.cos(np.pi * 0.80 * u + 3.1) ** 2)
    d = (y - yc) / width
    fade = np.clip(np.sin(np.pi * np.clip(u * 1.1 - 0.05, 0, 1)), 0, 1) ** 1.2
    core = (np.exp(-d ** 2) * 0.95 * fade)[..., None]
    halo = (np.exp(-(d / 5) ** 2) * 0.25 * fade)[..., None]
    img = mix(img, ORANGE, halo)
    img = mix(img, mix(ORANGE, np.ones(3, np.float32), 0.25 * np.exp(-d ** 2)[..., None]), core)

    # 8-bit with a 4x4 ordered dither (no banding in the long gradients)
    bayer = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]],
                     dtype=np.float32) / 16 - 0.5 + 1 / 32
    dither = np.tile(bayer, ((r1 - r0) // 4 + 1, w // 4 + 1))[:r1 - r0, :w][..., None]
    out = np.clip(np.floor(np.clip(img, 0, 1) * 255 + 0.5 + dither), 0, 255)
    return out.astype(np.uint8)


def main(args):
    if not args or len(args) % 2:
        sys.exit(__doc__)
    for size, out in zip(args[::2], args[1::2]):
        w, h = (int(v) for v in size.lower().split('x'))
        im = render(w, h)
        if out.lower().endswith('.bmp'):
            im.save(out, 'BMP')  # 24-bit, bottom-up: what LoadImage reads
        else:
            im.save(out, optimize=True)
        print(f'{out}: {w}x{h}')


if __name__ == '__main__':
    main(sys.argv[1:])
