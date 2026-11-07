#!/usr/bin/env python3
# SPDX-License-Identifier: MIT OR Apache-2.0
"""Render items of Setu's generated wallpaper collection
(art/wallpapers/wallgen.py) as 4K JPEGs, the files of
C:\\Windows\\Web\\Wallpaper\\Setu\\.

    render-collection.py list WALLGEN               every <style>-<palette>
    render-collection.py render WALLGEN OUTDIR ITEM...

ITEM is <style>-<palette> (seed 1); the output is OUTDIR/ITEM.jpg (3840x2160,
JPEG quality 90). An existing file is kept, so OUTDIR works as a cache.
The image build runs this in its builder VM, several at a time
(distro/builder/build-rootfs.sh). Needs python3-numpy, -pil and -cairo."""
import importlib.util
import os
import sys

SIZE = (3840, 2160)
QUALITY = 90
SEED = 1


def load_wallgen(path):
    spec = importlib.util.spec_from_file_location('wallgen', path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def split_item(wg, item):
    """'ridges-setu-dark' -> ('ridges', 'setu-dark'): styles have no '-'."""
    style, _, palette = item.partition('-')
    if style not in wg.STYLES or palette not in wg.PALETTES:
        raise SystemExit(f'unknown collection item {item!r}')
    return style, palette


def main(argv):
    if argv[:1] == ['list'] and len(argv) == 2:
        wg = load_wallgen(argv[1])
        for p in wg.PALETTES:
            for s in wg.STYLES:
                print(f'{s}-{p}')
        return 0
    if argv[:1] != ['render'] or len(argv) < 4:
        sys.exit(__doc__)
    wg = load_wallgen(argv[1])
    out = argv[2]
    os.makedirs(out, exist_ok=True)
    for item in argv[3:]:
        dst = os.path.join(out, item + '.jpg')
        if os.path.exists(dst):
            continue
        style, palette = split_item(wg, item)
        im = wg.render(style, palette, SEED, SIZE)
        im.convert('RGB').save(dst + '.part', 'JPEG', quality=QUALITY, optimize=True)
        os.replace(dst + '.part', dst)
        print(f'{item}.jpg', flush=True)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
