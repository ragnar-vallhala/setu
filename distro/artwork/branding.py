#!/usr/bin/env python3
# SPDX-License-Identifier: MIT OR Apache-2.0
"""Setu's branding images, drawn from code in the docs/DESIGN.md colours with
Inter: the bridge mark, the installer's (Calamares) logo, welcome banner and
slide, and the boot splash's (Plymouth) logo and spinner. Original artwork;
the PNGs are committed, run this after changing the design:

    distro/artwork/branding.py

Needs python3-numpy, python3-pil and the Inter font (fonts-inter)."""
import math
import os
import sys

from PIL import Image, ImageDraw, ImageFont

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wallpaper  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
DISTRO = os.path.dirname(HERE)
CALAMARES = os.path.join(DISTRO, 'installer/rootfs/usr/share/calamares/branding/setu')
PLYMOUTH = os.path.join(DISTRO, 'rootfs/usr/share/plymouth/themes/setu')

# docs/DESIGN.md
ORANGE = '#F5A623'
ACCENT = '#1F6FB2'
WINDOW_BG = '#F3F3F3'
TEXT = '#1B1B1B'
TEXT_SECONDARY = '#5F5F5F'
WHITE = '#FFFFFF'

FONT_DIRS = ['/usr/share/fonts/opentype/inter', '/usr/share/fonts/truetype/inter']


def font(weight, size):
    for d in FONT_DIRS:
        p = os.path.join(d, f'Inter-{weight}.otf')
        if os.path.exists(p):
            return ImageFont.truetype(p, size)
    sys.exit(f'Inter-{weight}.otf not found in {FONT_DIRS} (install fonts-inter)')


def bridge(size, colour=ORANGE, ss=4):
    """The Setu mark: a bridge (an arch over a deck, three hangers), on a
    transparent square of SIZE px, drawn at SS x and scaled down."""
    s = size * ss
    k = s / 128
    im = Image.new('RGBA', (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    w_arch, w_deck, w_post = 10 * k, 9 * k, 7 * k
    # arch: the upper half of a circle around (64, 88)
    cx, cy, r = 64 * k, 82 * k, 44 * k
    d.arc([cx - r, cy - r, cx + r, cy + r], 180, 360, fill=colour, width=round(w_arch))
    for a in (180, 360):  # round the arch's feet
        x = cx + (r - w_arch / 2) * math.cos(math.radians(a))
        d.ellipse([x - w_arch / 2, cy - w_arch / 2, x + w_arch / 2, cy + w_arch / 2], fill=colour)
    # hangers from the arch down to the deck
    for x in (42, 64, 86):
        dx = (x - 64) * k
        top = cy - math.sqrt(max((r - w_arch / 2) ** 2 - dx ** 2, 0))
        d.line([x * k, top, x * k, 82 * k], fill=colour, width=round(w_post))
    # the deck, with round ends
    y = 84 * k
    d.line([12 * k, y, 116 * k, y], fill=colour, width=round(w_deck))
    for x in (12 * k, 116 * k):
        d.ellipse([x - w_deck / 2, y - w_deck / 2, x + w_deck / 2, y + w_deck / 2], fill=colour)
    return im.resize((size, size), Image.LANCZOS)


def rounded(im, radius):
    mask = Image.new('L', im.size, 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, im.size[0] - 1, im.size[1] - 1], radius, fill=255)
    out = im.convert('RGBA')
    out.putalpha(mask)
    return out


def shaded(w, h, strength):
    """The wallpaper at W x H, darkened from the left (where text goes)."""
    import numpy as np
    im = np.asarray(wallpaper.render(w, h), dtype=np.float32)
    a = strength * np.clip(1 - np.arange(w, dtype=np.float32) / (0.85 * w), 0, 1) ** 1.2
    dark = np.array([6, 24, 58], dtype=np.float32)  # the wallpaper's darkest blue
    im = im + (dark - im) * a[None, :, None]
    return Image.fromarray(im.round().astype('uint8'), 'RGB').convert('RGBA')


def save(im, path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    im.save(path, optimize=True)
    print(f'{os.path.relpath(path, DISTRO)}: {im.size[0]}x{im.size[1]}')


def calamares():
    save(bridge(128), os.path.join(CALAMARES, 'setu-logo.png'))

    # welcome banner: a card of the wallpaper with the mark and the name
    w, h = 720, 300
    card = shaded(w, h, 0.45)
    d = ImageDraw.Draw(card)
    card.alpha_composite(bridge(132), (44, 54))
    d.text((196, 52), 'Setu', font=font('SemiBold', 104), fill=WHITE)
    d.text((202, 186), 'Windows programs on a Linux desktop', font=font('Regular', 26), fill=WHITE)
    save(rounded(card, 8), os.path.join(CALAMARES, 'setu-welcome.png'))

    # the slide shown while installing (same text as before)
    w, h = 800, 440
    slide = shaded(w, h, 0.75)
    d = ImageDraw.Draw(slide)
    slide.alpha_composite(bridge(96), (56, 52))
    d.text((176, 70), 'Installing Setu', font=font('SemiBold', 48), fill=WHITE)
    body = font('Regular', 23)
    d.text((60, 214), 'Setu runs Windows programs on a stock Linux kernel.', font=body, fill=WHITE)
    d.text((60, 250), 'Your C: drive is its own case-insensitive volume.', font=body, fill=WHITE)
    strong = font('SemiBold', 23)
    d.text((60, 326), 'After the restart, Setu asks for your language,', font=strong, fill=ORANGE)
    d.text((60, 360), 'keyboard, time zone, account and computer name.', font=strong, fill=ORANGE)
    save(rounded(slide, 8), os.path.join(CALAMARES, 'setu-slide.png'))


def plymouth():
    # the mark above the name, centred, on a transparent background (the
    # theme's script fills the screen with window-bg)
    w, h = 320, 220
    im = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    im.alpha_composite(bridge(160), ((w - 160) // 2, 0))
    d = ImageDraw.Draw(im)
    f = font('SemiBold', 48)
    tw = d.textlength('Setu', font=f)
    d.text(((w - tw) / 2, 150), 'Setu', font=f, fill=TEXT)
    save(im, os.path.join(PLYMOUTH, 'logo.png'))

    # spinner: an accent arc with a fading tail, 40 px (rotated by the script)
    ss, size = 4, 40
    s = size * ss
    sp = Image.new('RGBA', (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(sp)
    r, width = s / 2 - 3 * ss, round(3.5 * ss)
    acc = tuple(int(ACCENT[i:i + 2], 16) for i in (1, 3, 5))
    steps = 90
    for i in range(steps):  # 270 degrees, alpha rising to the head
        a0 = -90 + i * 3
        alpha = int(255 * ((i + 1) / steps) ** 1.6)
        d.arc([s / 2 - r, s / 2 - r, s / 2 + r, s / 2 + r], a0, a0 + 4, fill=acc + (alpha,), width=width)
    hx = s / 2 + (r - width / 2) * math.cos(math.radians(180))
    hy = s / 2 + (r - width / 2) * math.sin(math.radians(180))
    d.ellipse([hx - width / 2, hy - width / 2, hx + width / 2, hy + width / 2], fill=acc + (255,))
    save(sp.resize((size, size), Image.LANCZOS), os.path.join(PLYMOUTH, 'spinner.png'))


if __name__ == '__main__':
    calamares()
    plymouth()
