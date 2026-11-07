#!/usr/bin/env python3
# SPDX-License-Identifier: MIT OR Apache-2.0
"""Setu's branding images, drawn from code in a pack's colours with Inter:
the bridge mark, the installer's (Calamares) logo, welcome banner and slide,
and the boot splash's (Plymouth) logo and spinner. Original artwork.

distro/themes/render-packs.py calls calamares() and plymouth() for every
pack whose chain has this script (theme.ini [Installer] script=), with the
pack's tokens and its rendered wallpaper; the images are not committed.

Needs python3-numpy, python3-pil and the Inter font (fonts-inter)."""
import math
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

FONT_DIRS = ['/usr/share/fonts/opentype/inter', '/usr/share/fonts/truetype/inter']


def font(weight, size):
    for d in FONT_DIRS:
        p = os.path.join(d, f'Inter-{weight}.otf')
        if os.path.exists(p):
            return ImageFont.truetype(p, size)
    sys.exit(f'Inter-{weight}.otf not found in {FONT_DIRS} (install fonts-inter)')


def bridge(size, colour, ss=4):
    """The Setu mark: a bridge (an arch over a deck, three hangers), on a
    transparent square of SIZE px, drawn at SS x and scaled down. The same
    shape as the setu-logo icon (icons/setu-logo.svg)."""
    s = size * ss
    k = s / 128
    im = Image.new('RGBA', (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    w_arch, w_deck, w_post = 10 * k, 9 * k, 7 * k
    # arch: the upper half of a circle around (64, 82)
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


def shaded(wallpaper, w, h, strength, dark):
    """WALLPAPER (a PIL image) cropped to W x H ("fill"), darkened towards
    DARK from the left, where text goes."""
    sw, sh = wallpaper.size
    scale = max(w / sw, h / sh)
    im = wallpaper.resize((max(w, round(sw * scale)), max(h, round(sh * scale))), Image.LANCZOS)
    x0, y0 = (im.size[0] - w) // 2, (im.size[1] - h) // 2
    im = np.asarray(im.crop((x0, y0, x0 + w, y0 + h)).convert('RGB'), dtype=np.float32)
    a = strength * np.clip(1 - np.arange(w, dtype=np.float32) / (0.85 * w), 0, 1) ** 1.2
    dark = np.array(dark, dtype=np.float32)
    im = im + (dark - im) * a[None, :, None]
    return Image.fromarray(im.round().astype('uint8'), 'RGB').convert('RGBA')


def save(im, path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    im.save(path, optimize=True)


def hexrgb(h):
    h = h.lstrip('#')
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


def calamares(pack, wallpaper, out):
    """setu-logo.png, setu-welcome.png and setu-slide.png into OUT."""
    logo = pack.color('logo')
    white = '#FFFFFF'
    dark = hexrgb(pack.get('Wallpaper', 'background-dark', '#06183A'))
    save(bridge(128, logo), os.path.join(out, 'setu-logo.png'))

    # welcome banner: a card of the wallpaper with the mark and the name
    w, h = 720, 300
    card = shaded(wallpaper, w, h, 0.45, dark)
    d = ImageDraw.Draw(card)
    card.alpha_composite(bridge(132, logo), (44, 54))
    d.text((196, 52), 'Setu', font=font('SemiBold', 104), fill=white)
    d.text((202, 186), 'Windows programs on a Linux desktop', font=font('Regular', 26), fill=white)
    save(rounded(card, 8), os.path.join(out, 'setu-welcome.png'))

    # the slide shown while installing
    w, h = 800, 440
    slide = shaded(wallpaper, w, h, 0.75, dark)
    d = ImageDraw.Draw(slide)
    slide.alpha_composite(bridge(96, logo), (56, 52))
    d.text((176, 70), 'Installing Setu', font=font('SemiBold', 48), fill=white)
    body = font('Regular', 23)
    d.text((60, 214), 'Setu runs Windows programs on a stock Linux kernel.', font=body, fill=white)
    d.text((60, 250), 'Your C: drive is its own case-insensitive volume.', font=body, fill=white)
    strong = font('SemiBold', 23)
    d.text((60, 326), 'After the restart, Setu asks for your language,', font=strong, fill=logo)
    d.text((60, 360), 'keyboard, time zone, account and computer name.', font=strong, fill=logo)
    save(rounded(slide, 8), os.path.join(out, 'setu-slide.png'))


def plymouth(pack, out):
    """logo.png and spinner.png into OUT (the pack's boot/)."""
    text = pack.get('Boot', 'text', pack.color('text'))
    # the mark above the name, centred, on a transparent background (the
    # theme's script fills the screen with [Boot] background)
    w, h = 320, 220
    im = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    im.alpha_composite(bridge(160, pack.color('logo')), ((w - 160) // 2, 0))
    d = ImageDraw.Draw(im)
    f = font('SemiBold', 48)
    tw = d.textlength('Setu', font=f)
    d.text(((w - tw) / 2, 150), 'Setu', font=f, fill=text)
    save(im, os.path.join(out, 'logo.png'))

    # spinner: an accent arc with a fading tail, 40 px (rotated by the script)
    ss, size = 4, 40
    s = size * ss
    sp = Image.new('RGBA', (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(sp)
    r, width = s / 2 - 3 * ss, round(3.5 * ss)
    acc = hexrgb(pack.color('accent'))
    steps = 90
    for i in range(steps):  # 270 degrees, alpha rising to the head
        a0 = -90 + i * 3
        alpha = int(255 * ((i + 1) / steps) ** 1.6)
        d.arc([s / 2 - r, s / 2 - r, s / 2 + r, s / 2 + r], a0, a0 + 4, fill=acc + (alpha,), width=width)
    hx = s / 2 + (r - width / 2) * math.cos(math.radians(180))
    hy = s / 2 + (r - width / 2) * math.sin(math.radians(180))
    d.ellipse([hx - width / 2, hy - width / 2, hx + width / 2, hy + width / 2], fill=acc + (255,))
    save(sp.resize((size, size), Image.LANCZOS), os.path.join(out, 'spinner.png'))
