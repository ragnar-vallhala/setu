#!/usr/bin/env python3
# SPDX-License-Identifier: MIT OR Apache-2.0
"""Turn the Lucide line icons in shell/icons/lucide/*.svg into C data.

Each icon becomes a list of straight stroke segments in the icons' 24 x 24
grid (curves and arcs are flattened here), stored as int16 in 1/256 units.
setu-style.h rasterizes them at run time at any size, with anti-aliasing,
in any colour, so the icons stay crisp at every DPI.

Usage: shell/icons/gen-icons.py > shell/src/setu-icons.h
Only the standard library is used. The SVGs are the unmodified Lucide
files (ISC licence, shell/icons/lucide/LICENSE).
"""
import math
import os
import re
import sys
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "lucide")
SCALE = 256


def flatten_cubic(p0, p1, p2, p3, out):
    n = max(4, int(math.dist(p0, p1) + math.dist(p1, p2) + math.dist(p2, p3)) * 3)
    for i in range(1, n + 1):
        t = i / n
        u = 1 - t
        out.append((u**3 * p0[0] + 3 * u * u * t * p1[0] + 3 * u * t * t * p2[0] + t**3 * p3[0],
                    u**3 * p0[1] + 3 * u * u * t * p1[1] + 3 * u * t * t * p2[1] + t**3 * p3[1]))


def flatten_arc(p0, rx, ry, phi, large, sweep, p1, out):
    """SVG elliptical arc (implementation notes, SVG 1.1 appendix F.6)."""
    if p0 == p1:
        return
    rx, ry = abs(rx), abs(ry)
    if rx == 0 or ry == 0:
        out.append(p1)
        return
    phi = math.radians(phi)
    cp, sp = math.cos(phi), math.sin(phi)
    dx, dy = (p0[0] - p1[0]) / 2, (p0[1] - p1[1]) / 2
    x1 = cp * dx + sp * dy
    y1 = -sp * dx + cp * dy
    lam = x1 * x1 / (rx * rx) + y1 * y1 / (ry * ry)
    if lam > 1:
        rx *= math.sqrt(lam)
        ry *= math.sqrt(lam)
    num = rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1
    den = rx * rx * y1 * y1 + ry * ry * x1 * x1
    co = math.sqrt(max(0, num / den)) if den else 0
    if large == sweep:
        co = -co
    cx1 = co * rx * y1 / ry
    cy1 = -co * ry * x1 / rx
    cx = cp * cx1 - sp * cy1 + (p0[0] + p1[0]) / 2
    cy = sp * cx1 + cp * cy1 + (p0[1] + p1[1]) / 2

    def ang(ux, uy, vx, vy):
        a = math.atan2(ux * vy - uy * vx, ux * vx + uy * vy)
        return a

    t1 = ang(1, 0, (x1 - cx1) / rx, (y1 - cy1) / ry)
    dt = ang((x1 - cx1) / rx, (y1 - cy1) / ry, (-x1 - cx1) / rx, (-y1 - cy1) / ry)
    if not sweep and dt > 0:
        dt -= 2 * math.pi
    elif sweep and dt < 0:
        dt += 2 * math.pi
    n = max(4, int(abs(dt) * max(rx, ry) * 3))
    for i in range(1, n + 1):
        t = t1 + dt * i / n
        x = rx * math.cos(t)
        y = ry * math.sin(t)
        out.append((cp * x - sp * y + cx, sp * x + cp * y + cy))
    out[-1] = p1


TOKEN = re.compile(r"[A-Za-z]|-?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?")


def parse_path(d):
    """Return a list of polylines (lists of points)."""
    toks = TOKEN.findall(d)
    i = 0
    polys = []
    cur = None
    pos = (0.0, 0.0)
    start = (0.0, 0.0)
    cmd = None

    def num():
        nonlocal i
        v = float(toks[i])
        i += 1
        return v

    def flag():
        # arc flags may be written without separators ("a2 2 0 0 0 2-2")
        nonlocal i
        t = toks[i]
        if len(t) > 1 and t[0] in "01" and not t.startswith(("0.", "1.")):
            toks[i] = t[1:]
            return int(t[0])
        i += 1
        return int(float(t))

    while i < len(toks):
        t = toks[i]
        if t.isalpha():
            cmd = t
            i += 1
        elif cmd is None:
            raise ValueError("path data starts with a number")
        rel = cmd.islower()
        c = cmd.upper()
        ox, oy = pos if rel else (0.0, 0.0)
        if c == "M":
            pos = (ox + num(), oy + num())
            start = pos
            cur = [pos]
            polys.append(cur)
            cmd = "l" if rel else "L"
        elif c == "L":
            pos = (ox + num(), oy + num())
            cur.append(pos)
        elif c == "H":
            pos = ((pos[0] if rel else 0) + num(), pos[1])
            cur.append(pos)
        elif c == "V":
            pos = (pos[0], (pos[1] if rel else 0) + num())
            cur.append(pos)
        elif c == "C":
            p1 = (ox + num(), oy + num())
            p2 = (ox + num(), oy + num())
            p3 = (ox + num(), oy + num())
            flatten_cubic(pos, p1, p2, p3, cur)
            pos = p3
        elif c == "A":
            rx, ry, phi = num(), num(), num()
            large, sweep = flag(), flag()
            p1 = (ox + num(), oy + num())
            flatten_arc(pos, rx, ry, phi, large, sweep, p1, cur)
            pos = p1
        elif c == "Z":
            cur.append(start)
            pos = start
            cur = [pos]
            polys.append(cur)
        else:
            raise ValueError("unsupported path command " + cmd)
    return [p for p in polys if len(p) > 1]


def ellipse(cx, cy, rx, ry):
    n = max(12, int(max(rx, ry) * 8))
    return [[(cx + rx * math.cos(2 * math.pi * k / n), cy + ry * math.sin(2 * math.pi * k / n)) for k in range(n + 1)]]


def rect(x, y, w, h, rx, ry):
    if not rx and not ry:
        return [[(x, y), (x + w, y), (x + w, y + h), (x, y + h), (x, y)]]
    rx = rx or ry
    ry = ry or rx
    d = (f"M{x + rx} {y}H{x + w - rx}A{rx} {ry} 0 0 1 {x + w} {y + ry}V{y + h - ry}"
         f"A{rx} {ry} 0 0 1 {x + w - rx} {y + h}H{x + rx}A{rx} {ry} 0 0 1 {x} {y + h - ry}"
         f"V{y + ry}A{rx} {ry} 0 0 1 {x + rx} {y}Z")
    return parse_path(d)


def shapes(svg):
    root = ET.parse(svg).getroot()
    polys = []
    for el in root.iter():
        tag = el.tag.split("}")[-1]
        a = el.attrib
        f = lambda k, dflt=0.0: float(a.get(k, dflt))
        if tag == "path":
            polys += parse_path(a["d"])
        elif tag == "line":
            polys.append([(f("x1"), f("y1")), (f("x2"), f("y2"))])
        elif tag in ("polyline", "polygon"):
            v = [float(t) for t in TOKEN.findall(a["points"])]
            p = list(zip(v[0::2], v[1::2]))
            if tag == "polygon":
                p.append(p[0])
            polys.append(p)
        elif tag == "circle":
            polys += ellipse(f("cx"), f("cy"), f("r"), f("r"))
        elif tag == "ellipse":
            polys += ellipse(f("cx"), f("cy"), f("rx"), f("ry"))
        elif tag == "rect":
            polys += rect(f("x"), f("y"), f("width"), f("height"), f("rx"), f("ry"))
    return polys


def simplify(poly, tol=0.02):
    """Drop points that lie (almost) on the line between their neighbours."""
    out = [poly[0]]
    for k in range(1, len(poly) - 1):
        a, b, c = out[-1], poly[k], poly[k + 1]
        ux, uy = c[0] - a[0], c[1] - a[1]
        l = math.hypot(ux, uy)
        if l and abs((b[0] - a[0]) * uy - (b[1] - a[1]) * ux) / l < tol and \
           0 <= (b[0] - a[0]) * ux + (b[1] - a[1]) * uy <= l * l:
            continue
        out.append(b)
    out.append(poly[-1])
    return out


def main():
    names = sorted(f[:-4] for f in os.listdir(SRC) if f.endswith(".svg"))
    w = sys.stdout.write
    w("/* SPDX-License-Identifier: ISC */\n")
    w("/* Generated by shell/icons/gen-icons.py from the Lucide icons in\n")
    w(" * shell/icons/lucide/ (lucide-static 0.460.0, ISC licence, see\n")
    w(" * shell/icons/lucide/LICENSE and NOTICE). Do not edit by hand.\n")
    w(" * Stroke segments x0,y0,x1,y1 on the 24 x 24 grid, in 1/256 units. */\n")
    w("#ifndef SETU_ICONS_H\n#define SETU_ICONS_H\n\n")
    w("enum setu_icon\n{\n")
    for n in names:
        w(f"    ICON_{n.upper().replace('-', '_')},\n")
    w("    ICON_COUNT\n};\n\n")
    offs = []
    data = []
    for n in names:
        offs.append(len(data) // 4)
        for poly in shapes(os.path.join(SRC, n + ".svg")):
            poly = simplify(poly)
            for (x0, y0), (x1, y1) in zip(poly, poly[1:]):
                if (x0, y0) == (x1, y1):
                    continue
                data += [round(x0 * SCALE), round(y0 * SCALE), round(x1 * SCALE), round(y1 * SCALE)]
    offs.append(len(data) // 4)
    w("static const short setu_icon_segs[][4] =\n{\n")
    for k in range(0, len(data), 4):
        w("    { %d, %d, %d, %d },\n" % tuple(data[k:k + 4]))
    w("};\n\n")
    w("/* first segment of each icon; the last entry ends the table */\n")
    w("static const unsigned short setu_icon_first[ICON_COUNT + 1] =\n{\n")
    for k in range(0, len(offs), 12):
        w("    " + ", ".join(str(o) for o in offs[k:k + 12]) + ",\n")
    w("};\n\n#endif /* SETU_ICONS_H */\n")


if __name__ == "__main__":
    main()
