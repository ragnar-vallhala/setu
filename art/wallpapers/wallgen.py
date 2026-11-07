#!/usr/bin/env python3
# SPDX-License-Identifier: MIT OR Apache-2.0
"""Setu's generated wallpapers.

Every wallpaper is an algorithm plus a palette plus a seed, so a theme pack
can recolour any of them, and they render sharp at any size. Output is
deterministic: the same style, palette, seed and size give the same pixels.

Coordinates are scaled by the image height: x runs over [0, W/H], y over
[0, 1], so a composition looks the same at every resolution and wider
screens just see more of it.

    wallgen.py render STYLE [-p PALETTE] [-s SEED] [--size WxH] -o OUT.png
    wallgen.py sheet [-p PALETTE ...] [--styles a,b,...] -o SHEET.png
    wallgen.py list

Needs python3-numpy, python3-pil and python3-cairo.
"""
import argparse
import math
import sys

import cairo
import numpy as np
from PIL import Image, ImageDraw, ImageFont


# ----------------------------------------------------------------- palettes

def rgb(h):
    h = h.lstrip('#')
    return np.array([int(h[i:i + 2], 16) / 255.0 for i in (0, 2, 4)], dtype=np.float32)


# bg: [dark, light] background; acc: accents, roughly from cool/main to the
# highlight colour (last but one) and a near-white (last).
PALETTES = {
    'setu-blue': {'bg': ['#06183A', '#1F6FB2'],
                  'acc': ['#2F7BD6', '#5AA9E6', '#9CC9F0', '#F5A623', '#EAF2FB']},
    'setu-dark': {'bg': ['#0A0E15', '#1A2333'],
                  'acc': ['#2A7DC3', '#3E5C8A', '#6C86AD', '#F5A623', '#C9D6E8']},
    'dawn':      {'bg': ['#2B1B3D', '#C9707D'],
                  'acc': ['#E26D5C', '#F2A07B', '#F7C59F', '#FFD9A0', '#FFF1E6']},
    'forest':    {'bg': ['#0C1C17', '#2F5D50'],
                  'acc': ['#3F7D63', '#6FA67B', '#A8CFA0', '#E9C46A', '#EEF5E6']},
    'slate':     {'bg': ['#16191E', '#4A525E'],
                  'acc': ['#5E6774', '#7A8494', '#AEB6C2', '#D7DCE3', '#F2F4F7']},
    'ember':     {'bg': ['#140808', '#6E1B1B'],
                  'acc': ['#A8321F', '#E2552B', '#F59E42', '#FFD08A', '#FFF0DC']},
}


class Palette:
    def __init__(self, spec):
        self.bg = [rgb(c) for c in spec['bg']]
        self.acc = [rgb(c) for c in spec['acc']]

    def ramp(self, t):
        """t in [0, 1] (array) -> colours along bg dark, bg light, accents."""
        stops = [self.bg[0], self.bg[1]] + self.acc[:3]
        t = np.clip(np.asarray(t, dtype=np.float32), 0, 1) * (len(stops) - 1)
        i = np.minimum(t.astype(int), len(stops) - 2)
        f = (t - i)[..., None]
        s = np.stack(stops)
        return s[i] * (1 - f) + s[i + 1] * f


# -------------------------------------------------------------------- noise

def fade(t):
    return t * t * t * (t * (t * 6 - 15) + 10)


class Noise:
    """2D gradient (Perlin) noise, seeded; values roughly in [-0.7, 0.7]."""

    def __init__(self, seed):
        rng = np.random.default_rng(seed)
        p = rng.permutation(256)
        self.p = np.concatenate([p, p])
        a = rng.uniform(0, 2 * np.pi, 256)
        self.g = np.stack([np.cos(a), np.sin(a)], -1).astype(np.float32)

    def __call__(self, x, y):
        x = np.asarray(x, dtype=np.float32)
        y = np.asarray(y, dtype=np.float32)
        xi = np.floor(x).astype(np.int64)
        yi = np.floor(y).astype(np.int64)
        xf, yf = x - xi, y - yi
        xi &= 255
        yi &= 255
        p, g = self.p, self.g

        def dot(ix, iy, dx, dy):
            gg = g[p[p[ix] + iy]]
            return gg[..., 0] * dx + gg[..., 1] * dy

        x1, y1 = (xi + 1) & 255, (yi + 1) & 255
        u, v = fade(xf), fade(yf)
        a = dot(xi, yi, xf, yf) * (1 - u) + dot(x1, yi, xf - 1, yf) * u
        b = dot(xi, y1, xf, yf - 1) * (1 - u) + dot(x1, y1, xf - 1, yf - 1) * u
        return a * (1 - v) + b * v

    def fbm(self, x, y, octaves=5, lac=2.0, gain=0.5):
        s, amp, f, norm = 0.0, 1.0, 1.0, 0.0
        for o in range(octaves):
            s = s + amp * self(x * f + 17.3 * o, y * f - 9.1 * o)
            norm += amp
            amp *= gain
            f *= lac
        return s / norm


# ------------------------------------------------------------------ helpers

def grid(w, h):
    """Pixel-centre coordinates scaled by the height."""
    y, x = np.mgrid[0:h, 0:w].astype(np.float32)
    return (x + 0.5) / h, (y + 0.5) / h


def smoothstep(a, b, x):
    t = np.clip((x - a) / (b - a), 0, 1)
    return t * t * (3 - 2 * t)


BAYER = (np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]],
                  dtype=np.float32) + 0.5) / 16 - 0.5


def to_image(img):
    """float RGB [0,1] -> 8-bit PIL image, ordered dithering against banding."""
    h, w, _ = img.shape
    d = np.tile(BAYER, (h // 4 + 1, w // 4 + 1))[:h, :w, None]
    return Image.fromarray(np.clip(img * 255 + d, 0, 255).astype(np.uint8), 'RGB')


def cairo_canvas(w, h):
    surf = cairo.ImageSurface(cairo.FORMAT_RGB24, w, h)
    ctx = cairo.Context(surf)
    ctx.scale(h, h)  # draw in height units, like grid()
    return surf, ctx


def cairo_to_array(surf):
    w, h = surf.get_width(), surf.get_height()
    buf = np.frombuffer(surf.get_data(), dtype=np.uint8).reshape(h, surf.get_stride() // 4, 4)
    return buf[:, :w, 2::-1].astype(np.float32) / 255.0  # BGRx -> RGB


def vgradient(w, h, top, bottom, x=None, y=None):
    if y is None:
        x, y = grid(w, h)
    t = smoothstep(0, 1, y)[..., None]
    return top * (1 - t) + bottom * t


def set_rgb(ctx, c, a=1.0):
    ctx.set_source_rgba(float(c[0]), float(c[1]), float(c[2]), a)


# ------------------------------------------------------------------- styles
# Each style: f(w, h, palette, rng, noise) -> float RGB array (h, w, 3).

STYLES = {}


def style(name, desc):
    def reg(f):
        STYLES[name] = (f, desc)
        return f
    return reg


@style('aurora', 'curtains of light over a dark horizon')
def aurora(w, h, pal, rng, noise):
    x, y = grid(w, h)
    xs = x[0]
    sky_top = pal.bg[0] * 0.55 + pal.bg[1] * 0.25
    sky_low = pal.bg[0] * 0.35
    img = vgradient(w, h, sky_top, sky_low, x, y)
    stars = np.zeros((h, w), np.float32)
    n = int(0.00015 * w * h)
    sy, sx = rng.integers(0, h, n), rng.integers(0, w, n)
    stars[sy, sx] = rng.uniform(0.15, 0.9, n)
    img = img + (stars * np.clip(1.2 - 1.6 * y, 0, 1))[..., None] * pal.acc[4]
    cols = [pal.acc[1], pal.acc[0], pal.acc[2], pal.acc[3]]
    for i in range(4):
        base = rng.uniform(0.38, 0.6)
        curve = base + 0.12 * noise.fbm(xs * 1.1 + 31 * i, np.full_like(xs, 5.0 * i), 3) \
            + 0.05 * np.sin(xs * rng.uniform(2, 4) + rng.uniform(0, 6))
        d = y - curve[None, :]
        height = rng.uniform(0.14, 0.32)
        band = np.where(d > 0, np.exp(-(d / 0.018) ** 2), np.exp(-(d / height) ** 2) * (1 + 3 * d / height).clip(0.2, 1))
        # rays: frequency and phase wander along the curtain
        warp = xs * 30 + 18 * noise.fbm(xs * 0.7 + 3 * i, np.full_like(xs, 9.0 + i), 2)
        rays = 0.5 + 0.5 * np.sin(warp + 6 * noise(xs * 4 + i, np.full_like(xs, 1.7 * i)))
        rays = (0.35 + 0.65 * rays ** 2)[None, :] * (1 - 0.4 * np.clip(-d / height, 0, 1))
        strength = np.clip(noise.fbm(xs * 0.5 + 11 * i, np.full_like(xs, 2.0 + i), 2) * 2.4 + 0.5, 0, 1) ** 1.5
        inten = (band * rays * strength[None, :])[..., None]
        c = cols[i]
        k = 1.0 if i < 3 else 0.35
        img = img + c * inten * k
    img = 1 - np.exp(-img * 1.3)
    # dark horizon of hills to ground it
    hz = 0.8 + 0.05 * noise.fbm(xs * 2.2 + 70, np.full_like(xs, 1.0), 4)
    cov = np.clip((y - hz[None, :]) * h + 0.5, 0, 1)[..., None]
    return img * (1 - cov) + pal.bg[0] * 0.5 * cov


@style('flow', 'thousands of fine lines following an invisible current')
def flow(w, h, pal, rng, noise):
    surf, ctx = cairo_canvas(w, h)
    aspect = w / h
    # background gradient
    g = cairo.LinearGradient(0, 0, aspect, 1)
    g.add_color_stop_rgb(0, *map(float, pal.bg[0]))
    g.add_color_stop_rgb(1, *map(float, pal.bg[1] * 0.6 + pal.bg[0] * 0.4))
    ctx.set_source(g)
    ctx.paint()
    ctx.set_line_cap(cairo.LINE_CAP_ROUND)
    ctx.set_line_width(1.1 / 1080)
    n = int(2600 * aspect)
    step = 0.004
    for i in range(n):
        px, py = rng.uniform(-0.1, aspect + 0.1), rng.uniform(-0.1, 1.1)
        t = rng.uniform()
        c = pal.ramp(0.35 + 0.65 * t)
        if rng.uniform() < 0.03:
            c = pal.acc[3]
        set_rgb(ctx, c, 0.10 + 0.25 * t)
        ctx.move_to(px, py)
        for _ in range(int(rng.uniform(60, 220))):
            a = float(noise.fbm(px * 1.4, py * 1.4, 3)) * 2 * math.pi * 1.6 + 0.6
            px += math.cos(a) * step
            py += math.sin(a) * step
            ctx.line_to(px, py)
        ctx.stroke()
    return cairo_to_array(surf)


@style('lowpoly', 'faceted triangles catching light from one side')
def lowpoly(w, h, pal, rng, noise):
    surf, ctx = cairo_canvas(w, h)
    aspect = w / h
    rows = 9
    cols = int(math.ceil(rows * aspect * 1.15))
    cw, rh = aspect / cols, 1.0 / rows
    pts = np.zeros((rows + 1, cols + 1, 2))
    for r in range(rows + 1):
        for c in range(cols + 1):
            jx = rng.uniform(-0.38, 0.38) * cw if 0 < c < cols else 0
            jy = rng.uniform(-0.38, 0.38) * rh if 0 < r < rows else 0
            pts[r, c] = (c * cw + jx, r * rh + jy)
    light = np.array([-0.6, -0.8])
    for r in range(rows):
        for c in range(cols):
            a, b, d, e = pts[r, c], pts[r, c + 1], pts[r + 1, c], pts[r + 1, c + 1]
            tris = [(a, b, e), (a, e, d)] if (r + c) % 2 else [(a, b, d), (b, e, d)]
            for tri in tris:
                cx = sum(p[0] for p in tri) / 3
                cy = sum(p[1] for p in tri) / 3
                base = 0.15 + 0.7 * (cx / aspect * 0.6 + (1 - cy) * 0.4)
                base += 0.25 * float(noise.fbm(cx * 2, cy * 2, 2))
                col = pal.ramp(np.clip(base, 0, 1))
                # fake a normal from the triangle's corners for shading
                v1 = np.array([tri[1][0] - tri[0][0], tri[1][1] - tri[0][1]])
                v2 = np.array([tri[2][0] - tri[0][0], tri[2][1] - tri[0][1]])
                nrm = np.array([v1[1] - v2[1], v2[0] - v1[0]])
                nrm /= (np.linalg.norm(nrm) + 1e-9)
                shade = 0.88 + 0.18 * float(nrm @ light) + rng.uniform(-0.04, 0.04)
                set_rgb(ctx, np.clip(col * shade, 0, 1))
                ctx.move_to(*tri[0])
                ctx.line_to(*tri[1])
                ctx.line_to(*tri[2])
                ctx.close_path()
                ctx.fill_preserve()
                ctx.set_line_width(0.6 / 1080)
                ctx.stroke()
    return cairo_to_array(surf)


@style('contours', 'topographic map lines over gently shaded terrain')
def contours(w, h, pal, rng, noise):
    x, y = grid(w, h)
    f = noise.fbm(x * 1.6, y * 1.6, 5) + 0.35 * (x / (w / h)) - 0.2 * y
    t = (f - f.min()) / (np.ptp(f) + 1e-9)
    img = pal.ramp(0.05 + 0.55 * t)
    levels = 26
    v = t * levels
    gy, gx = np.gradient(v)
    gm = np.sqrt(gx * gx + gy * gy) + 1e-6
    d = np.abs(v - np.round(v)) / gm  # distance to the nearest line, in px
    major = (np.round(v) % 5 == 0)
    width = np.where(major, 1.3, 0.6) * (h / 1080)
    line = 1 - smoothstep(width * 0.5, width * 0.5 + 1.0, d)
    lc = np.where(major[..., None], pal.acc[4], pal.acc[2])
    alpha = np.where(major, 0.55, 0.28)[..., None] * line[..., None]
    img = img * (1 - alpha) + lc * alpha
    # one highlighted line in the accent colour
    hl = (np.round(v) == int(levels * 0.62))
    a2 = (line * hl)[..., None] * 0.9
    return img * (1 - a2) + pal.acc[3] * a2


@style('ridges', 'layered hills fading into a hazy sky, with a low sun')
def ridges(w, h, pal, rng, noise):
    x, y = grid(w, h)
    aspect = w / h
    sky = vgradient(w, h, pal.bg[1] * 0.5 + pal.acc[2] * 0.5, pal.acc[4] * 0.6 + pal.acc[3] * 0.4, x, y)
    img = sky
    sx, sy = aspect * rng.uniform(0.55, 0.8), rng.uniform(0.42, 0.55)
    r = np.sqrt((x - sx) ** 2 + (y - sy) ** 2)
    glow = np.exp(-(r / 0.25) ** 2)[..., None] * 0.5
    img = img * (1 - glow) + pal.acc[4] * glow
    sun = (1 - smoothstep(0.055, 0.058, r))[..., None]
    img = img * (1 - sun) + (pal.acc[3] * 0.4 + pal.acc[4] * 0.6) * sun
    layers = 6
    xs = x[0]
    for i in range(layers):
        k = i / (layers - 1)
        base = 0.50 + 0.09 * i
        amp = 0.07 + 0.03 * i
        hgt = base + amp * noise.fbm(xs * (1.2 + 0.5 * i) + 13 * i, np.full_like(xs, 3.7 * i), 4)
        cov = np.clip((y - hgt[None, :]) * h + 0.5, 0, 1)[..., None]
        near = pal.bg[0]
        far = pal.bg[1] * 0.55 + pal.acc[2] * 0.45
        col = far * (1 - k) + near * k
        # a little vertical shading inside each layer
        shade = 1 - 0.18 * np.clip((y - hgt[None, :]) * 4, 0, 1)[..., None]
        img = img * (1 - cov) + col * shade * cov
    return img


@style('glass', 'stained-glass cells with soft inner light')
def glass(w, h, pal, rng, noise):
    aspect = w / h
    n = int(70 * aspect)
    # relax the seeds a little (Lloyd on a coarse grid) for even cells
    seeds = np.stack([rng.uniform(0, aspect, n), rng.uniform(0, 1, n)], -1).astype(np.float32)
    cx, cy = grid(int(160 * aspect), 160)
    for _ in range(3):
        d = (cx[..., None] - seeds[:, 0]) ** 2 + (cy[..., None] - seeds[:, 1]) ** 2
        idx = d.argmin(-1)
        for k in range(n):
            m = idx == k
            if m.any():
                seeds[k] = (cx[m].mean(), cy[m].mean())
    t = np.clip(0.2 + 0.6 * (seeds[:, 0] / aspect * 0.5 + (1 - seeds[:, 1]) * 0.5)
                + 0.25 * noise.fbm(seeds[:, 0] * 2, seeds[:, 1] * 2, 2)
                + rng.uniform(-0.08, 0.08, n), 0, 1)
    cell_col = pal.ramp(t)
    hot = rng.choice(n, max(1, n // 25), replace=False)
    cell_col[hot] = pal.acc[3]
    img = np.empty((h, w, 3), np.float32)
    xs = (np.arange(w, dtype=np.float32) + 0.5) / h
    lead = pal.bg[0] * 0.6
    for r0 in range(0, h, 24):
        r1 = min(h, r0 + 24)
        ys = (np.arange(r0, r1, dtype=np.float32) + 0.5) / h
        X, Y = np.meshgrid(xs, ys)
        d = (X[..., None] - seeds[:, 0]) ** 2 + (Y[..., None] - seeds[:, 1]) ** 2
        part = np.argpartition(d, 1, axis=-1)[..., :2]
        d1 = np.take_along_axis(d, part, -1)
        order = d1.argsort(-1)
        i1 = np.take_along_axis(part, order[..., :1], -1)[..., 0]
        d1 = np.sqrt(np.sort(d1, -1))
        edge = (d1[..., 1] - d1[..., 0]) * h  # ~px from the border
        c = cell_col[i1]
        inner = 1.0 + 0.25 * (1 - smoothstep(0, 0.12, d1[..., 0]))[..., None]
        c = np.clip(c * inner, 0, 1)
        bw = 2.2 * h / 1080
        a = (1 - smoothstep(bw * 0.5, bw * 0.5 + 1.2, edge))[..., None]
        img[r0:r1] = c * (1 - a) + lead * a
    return img


@style('orbits', 'concentric arcs and a few bright planets')
def orbits(w, h, pal, rng, noise):
    surf, ctx = cairo_canvas(w, h)
    aspect = w / h
    g = cairo.RadialGradient(aspect * 0.72, 0.62, 0.0, aspect * 0.72, 0.62, 1.3)
    g.add_color_stop_rgb(0, *map(float, pal.bg[1] * 0.7 + pal.bg[0] * 0.3))
    g.add_color_stop_rgb(1, *map(float, pal.bg[0]))
    ctx.set_source(g)
    ctx.paint()
    ox, oy = aspect * 0.72, 0.62
    ctx.set_line_cap(cairo.LINE_CAP_ROUND)
    rad = 0.06
    i = 0
    while rad < 1.6:
        i += 1
        rad += rng.uniform(0.018, 0.05)
        start = rng.uniform(0, 2 * math.pi)
        span = rng.uniform(0.6, 2 * math.pi)
        lw = rng.choice([1.0, 1.6, 2.4, 4.0, 8.0], p=[0.35, 0.3, 0.2, 0.1, 0.05]) / 1080
        c = pal.ramp(rng.uniform(0.45, 1.0))
        set_rgb(ctx, c, rng.uniform(0.45, 0.95))
        ctx.set_line_width(lw)
        ctx.new_sub_path()
        ctx.arc(ox, oy, rad, start, start + span)
        ctx.stroke()
        if rng.uniform() < 0.12:
            a = start + span
            px, py = ox + rad * math.cos(a), oy + rad * math.sin(a)
            pr = rng.uniform(0.006, 0.02)
            set_rgb(ctx, pal.acc[3] if rng.uniform() < 0.5 else pal.acc[4])
            ctx.arc(px, py, pr, 0, 2 * math.pi)
            ctx.fill()
    set_rgb(ctx, pal.acc[3])
    ctx.arc(ox, oy, 0.035, 0, 2 * math.pi)
    ctx.fill()
    return cairo_to_array(surf)


@style('tiles', 'a grid of quarter circles and half moons, Bauhaus style')
def tiles(w, h, pal, rng, noise):
    surf, ctx = cairo_canvas(w, h)
    aspect = w / h
    rows = 6
    s = 1.0 / rows
    cols = int(math.ceil(aspect / s))
    set_rgb(ctx, pal.bg[0])
    ctx.paint()
    fills = [pal.bg[1], pal.acc[0], pal.acc[1], pal.acc[2], pal.acc[3], pal.acc[4], pal.bg[0]]
    weights = np.array([0.22, 0.2, 0.17, 0.13, 0.06, 0.08, 0.14])
    weights /= weights.sum()
    pad = s * 0.04
    for r in range(rows):
        for c in range(cols):
            x0, y0 = c * s, r * s
            bg = fills[rng.choice(len(fills), p=weights)]
            fg = fills[rng.choice(len(fills), p=weights)]
            while np.allclose(fg, bg):
                fg = fills[rng.choice(len(fills), p=weights)]
            set_rgb(ctx, bg)
            ctx.rectangle(x0 + pad, y0 + pad, s - 2 * pad, s - 2 * pad)
            ctx.fill()
            set_rgb(ctx, fg)
            kind = rng.integers(0, 4)
            q = rng.integers(0, 4)
            inner = s - 2 * pad
            corners = [(x0 + pad, y0 + pad), (x0 + s - pad, y0 + pad),
                       (x0 + s - pad, y0 + s - pad), (x0 + pad, y0 + s - pad)]
            ctx.save()
            ctx.rectangle(x0 + pad, y0 + pad, inner, inner)
            ctx.clip()
            if kind == 0:  # quarter circle from a corner
                cx, cy = corners[q]
                ctx.arc(cx, cy, inner, 0, 2 * math.pi)
                ctx.fill()
            elif kind == 1:  # half moon on an edge
                mids = [(x0 + s / 2, y0 + pad), (x0 + s - pad, y0 + s / 2),
                        (x0 + s / 2, y0 + s - pad), (x0 + pad, y0 + s / 2)]
                ctx.arc(*mids[q], inner / 2, 0, 2 * math.pi)
                ctx.fill()
            elif kind == 2:  # circle
                ctx.arc(x0 + s / 2, y0 + s / 2, inner * 0.36, 0, 2 * math.pi)
                ctx.fill()
            else:  # triangle
                a, b, d = corners[q], corners[(q + 1) % 4], corners[(q + 3) % 4]
                ctx.move_to(*a)
                ctx.line_to(*b)
                ctx.line_to(*d)
                ctx.close_path()
                ctx.fill()
            ctx.restore()
    return cairo_to_array(surf)


# ---------------------------------------------------------------------- CLI

def render(name, palette='setu-blue', seed=1, size=(1920, 1080)):
    f, _ = STYLES[name]
    pal = Palette(PALETTES[palette] if isinstance(palette, str) else palette)
    rng = np.random.default_rng(seed)
    noise = Noise(seed * 7919 + 13)
    w, h = size
    img = f(w, h, pal, rng, noise)
    return to_image(np.clip(img, 0, 1))


def parse_size(s):
    w, h = s.lower().split('x')
    return int(w), int(h)


def sheet(styles, palettes, seed, tile=(480, 270), out='sheet.png'):
    pad, label = 12, 22
    tw, th = tile
    W = pad + len(styles) * (tw + pad)
    H = pad + len(palettes) * (th + label + pad)
    canvas = Image.new('RGB', (W, H), (24, 26, 30))
    draw = ImageDraw.Draw(canvas)
    try:
        font = ImageFont.truetype('/usr/share/fonts/opentype/inter/Inter-Regular.otf', 14)
    except OSError:
        font = ImageFont.load_default()
    for j, p in enumerate(palettes):
        for i, s in enumerate(styles):
            x, y = pad + i * (tw + pad), pad + j * (th + label + pad)
            canvas.paste(render(s, p, seed, tile), (x, y))
            draw.text((x, y + th + 4), f'{s} · {p}', fill=(210, 214, 220), font=font)
    canvas.save(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    sub = ap.add_subparsers(dest='cmd', required=True)
    r = sub.add_parser('render')
    r.add_argument('style', choices=sorted(STYLES))
    r.add_argument('-p', '--palette', default='setu-blue', choices=sorted(PALETTES))
    r.add_argument('-s', '--seed', type=int, default=1)
    r.add_argument('--size', type=parse_size, default=(1920, 1080))
    r.add_argument('-o', '--out', required=True)
    sh = sub.add_parser('sheet')
    sh.add_argument('-p', '--palette', action='append', choices=sorted(PALETTES))
    sh.add_argument('--styles', default=','.join(STYLES))
    sh.add_argument('-s', '--seed', type=int, default=1)
    sh.add_argument('-o', '--out', required=True)
    sub.add_parser('list')
    a = ap.parse_args()
    if a.cmd == 'list':
        for k, (_, d) in STYLES.items():
            print(f'{k:10} {d}')
        print('palettes:', ', '.join(PALETTES))
    elif a.cmd == 'render':
        render(a.style, a.palette, a.seed, a.size).save(a.out)
    else:
        sheet(a.styles.split(','), a.palette or ['setu-blue'], a.seed, out=a.out)


if __name__ == '__main__':
    sys.exit(main())
