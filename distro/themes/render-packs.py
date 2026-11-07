#!/usr/bin/env python3
# SPDX-License-Identifier: MIT OR Apache-2.0
"""Build Setu's theme packs (docs/THEMES.md) from their sources in
distro/themes/<id>/ into a staging tree shaped like the image's root file
system. distro/build-image.sh runs it on the host and copies the tree into
the image; nothing it writes is committed.

    distro/themes/render-packs.py --out DIR [options]

Writes, under DIR:
  usr/share/setu/themes/<id>/        each built pack: <id>.theme, theme.ini,
                                     icons/ (ICO+SVG+index.ini), wallpaper/
                                     (4K wallpaper.bmp and .png), visual/
                                     <id>.msstyles, boot/, installer/,
                                     linux/ (templates), preview.png, LICENSE;
                                     the packs generated from art/schemes
                                     have no wallpaper/ (their .theme names
                                     a collection JPEG) and no installer/
  usr/share/setu/icons/<set>/        every icon set of art/icons/icongen.py
  usr/share/setu/cursors/<set>/      .cur/.ani of art/cursors/cursorgen.py
  usr/share/icons/<set>/             the same cursors as X cursor themes
  usr/share/setu/sounds/<scheme>/    the WAVs of art/sounds/soundgen.py
  usr/share/plymouth/themes/<id>/    a Plymouth theme per pack
  etc/plymouth/plymouthd.conf        the default pack's splash
  usr/share/calamares/branding/setu/ the installer's branding (default pack)
  etc/xdg/labwc/                     labwc theme, fonts, cursor (default pack)

Options:
  --default ID        the default pack (setu-light)
  --packs ID,...      only these packs (default: every distro/themes/<id> and
                      every art/schemes/schemegen.py scheme)
  --wine-bin DIR      Wine build tools for the msstyles generator
                      (tools/setu-theme/make-msstyles.py --wine-bin)
  --personality DIR   personality install; while the generator is missing,
                      its setu.msstyles is copied as every pack's msstyles
  --previews          also copy each preview.png back to distro/themes/<id>/

Host needs: python3-numpy, -pil, -cairo, librsvg's GObject bindings
(gir1.2-rsvg-2.0) and the Inter font."""
import argparse
import functools
import io
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import warnings

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import themelib  # noqa: E402

import cairo  # noqa: E402
import gi  # noqa: E402
from PIL import Image, ImageDraw, ImageFont  # noqa: E402

warnings.filterwarnings('ignore', category=DeprecationWarning)
gi.require_version('Rsvg', '2.0')
from gi.repository import Rsvg  # noqa: E402

ICONGEN = os.path.join(REPO, 'art', 'icons', 'icongen.py')
WALLGEN = os.path.join(REPO, 'art', 'wallpapers', 'wallgen.py')
CURSORGEN = os.path.join(REPO, 'art', 'cursors', 'cursorgen.py')
SOUNDGEN = os.path.join(REPO, 'art', 'sounds', 'soundgen.py')
SCHEMEGEN = os.path.join(REPO, 'art', 'schemes', 'schemegen.py')
MSSTYLES_TOOL = os.path.join(REPO, 'tools', 'setu-theme', 'make-msstyles.py')
SETU_THEME_LINUX = os.path.join(REPO, 'distro', 'rootfs', 'usr', 'lib', 'setu', 'setu-theme-linux')
ICO_SIZES = (16, 20, 24, 32, 48, 64, 256)
WALLPAPER_SIZE = (3840, 2160)


def log(msg):
    print(f'[render-packs] {msg}', flush=True)


def link(src, dst):
    """DST as a hard link to SRC (identical files shared between packs keep
    the image small; cp -a and the image's tar keep the links), else a copy."""
    if os.path.isdir(dst):
        dst = os.path.join(dst, os.path.basename(src))
    try:
        os.link(src, dst)
    except OSError:
        shutil.copy(src, dst)


def load_module(name, path):
    import importlib.util
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


# ---- icons ------------------------------------------------------------------
def render_svg(path, size):
    """SVG -> straight-alpha RGBA PIL image of SIZE x SIZE."""
    handle = Rsvg.Handle.new_from_file(path)
    surf = cairo.ImageSurface(cairo.FORMAT_ARGB32, size, size)
    ctx = cairo.Context(surf)
    vp = Rsvg.Rectangle()
    vp.x, vp.y, vp.width, vp.height = 0, 0, size, size
    handle.render_document(ctx, vp)
    surf.flush()
    im = Image.frombuffer('RGBa', (size, size), bytes(surf.get_data()), 'raw', 'BGRa', 0, 1)
    return im.convert('RGBA')


def write_ico(svg, dst):
    """A multi-size .ico: 32-bit DIBs (with an AND mask) up to 64 px and a
    PNG at 256 px, the usual layout Windows' icon loaders read."""
    entries = []
    for s in ICO_SIZES:
        im = render_svg(svg, s)
        if s >= 256:
            buf = io.BytesIO()
            im.save(buf, 'PNG', optimize=True)
            data = buf.getvalue()
        else:
            px = im.tobytes('raw', 'BGRA', 0, -1)  # bottom-up rows
            stride = ((s + 31) // 32) * 4
            mask = bytearray(stride * s)
            for y in range(s):  # AND mask: 1 = transparent (bottom-up too)
                row = im.crop((0, s - 1 - y, s, s - y)).getchannel('A').tobytes()
                for x, a in enumerate(row):
                    if a == 0:
                        mask[y * stride + x // 8] |= 0x80 >> (x % 8)
            hdr = struct.pack('<IiiHHIIiiII', 40, s, s * 2, 1, 32, 0, len(px) + len(mask), 0, 0, 0, 0)
            data = hdr + px + bytes(mask)
        entries.append((s, data))
    out = struct.pack('<HHH', 0, 1, len(entries))
    offset = 6 + 16 * len(entries)
    for s, data in entries:
        out += struct.pack('<BBBBHHII', s % 256, s % 256, 0, 0, 1, 32, len(data), offset)
        offset += len(data)
    with open(dst, 'wb') as f:
        f.write(out + b''.join(d for _, d in entries))


def build_icon_sets(stage, tmp):
    """Every icongen set into usr/share/setu/icons/<set>/; returns {set: dir}."""
    src = os.path.join(tmp, 'icongen')
    subprocess.run([sys.executable, ICONGEN, 'build', '-o', src], check=True, stdout=subprocess.DEVNULL)
    logo = os.path.join(src, 'logo')
    sets = {}
    for name in sorted(os.listdir(src)):
        d = os.path.join(src, name)
        if name == 'logo' or not os.path.isfile(os.path.join(d, 'index.ini')):
            continue
        out = os.path.join(stage, 'usr/share/setu/icons', name)
        os.makedirs(out, exist_ok=True)
        # the app tile (the Setu logo as an app icon: start button, installer)
        shutil.copy(os.path.join(logo, 'setu-app.svg'), os.path.join(d, 'setu-app.svg'))
        index = open(os.path.join(d, 'index.ini')).read().rstrip('\n') + '\nsetu-app=setu-app.ico\n'
        for f in sorted(os.listdir(d)):
            if f.endswith('.svg'):
                shutil.copy(os.path.join(d, f), out)
                write_ico(os.path.join(d, f), os.path.join(out, f[:-4] + '.ico'))
        open(os.path.join(out, 'index.ini'), 'w').write(index)
        sets[name] = out
    shutil.copytree(logo, os.path.join(stage, 'usr/share/setu/icons/logo'), dirs_exist_ok=True)
    log(f'icon sets: {", ".join(sets)} ({len(os.listdir(next(iter(sets.values())))) // 2} files each)')
    return sets


def pack_icons(pack, sets, out):
    """The pack's icons/: its set, then the chain's own icons/*.svg over it."""
    os.makedirs(out, exist_ok=True)
    name = pack.get('Icons', 'set', '')
    names = {}
    if name:
        if name not in sets:
            raise SystemExit(f'{pack.id}: [Icons] set={name} is not one of {", ".join(sets)}')
        for f in os.listdir(sets[name]):
            if f.endswith(('.svg', '.ico')):
                link(os.path.join(sets[name], f), out)
                names[f[:-4]] = f[:-4] + '.ico'
    for n, svg in pack.icons().items():
        shutil.copy(svg, os.path.join(out, n + '.svg'))
        write_ico(svg, os.path.join(out, n + '.ico'))
        names[n] = n + '.ico'
    with open(os.path.join(out, 'index.ini'), 'w') as f:
        f.write(f'; {pack.id}: icon set {name or "(none)"} with the pack\'s own icons over it\n[Icons]\n')
        f.writelines(f'{n}={v}\n' for n, v in sorted(names.items()))


# ---- cursors, sounds, generated schemes ----------------------------------------
def build_cursors(stage, tmp):
    """cursorgen's sets: X themes in usr/share/icons/<set>/, Windows .cur/.ani
    in usr/share/setu/cursors/<set>/; returns {set: cursors.ini text}."""
    src = os.path.join(tmp, 'cursorgen')
    subprocess.run([sys.executable, CURSORGEN, 'build', '-o', src], check=True, stdout=subprocess.DEVNULL)
    out = {}
    for name in sorted(os.listdir(src)):
        win, x11 = os.path.join(src, name, 'windows'), os.path.join(src, name, 'x11', name)
        if not os.path.isdir(win):
            continue
        shutil.copytree(x11, os.path.join(stage, 'usr/share/icons', name), symlinks=True, dirs_exist_ok=True)
        dst = os.path.join(stage, 'usr/share/setu/cursors', name)
        shutil.copytree(win, dst, dirs_exist_ok=True)
        out[name] = open(os.path.join(win, 'cursors.ini')).read()
    log(f'cursor sets: {", ".join(out)}')
    return out


def build_sounds(stage, tmp):
    """soundgen's schemes in usr/share/setu/sounds/<scheme>/; returns
    {scheme: {event: file}}."""
    src = os.path.join(tmp, 'soundgen')
    subprocess.run([sys.executable, SOUNDGEN, 'build', '-o', src], check=True, stdout=subprocess.DEVNULL)
    out = {}
    for name in sorted(os.listdir(src)):
        d = os.path.join(src, name)
        if not os.path.isfile(os.path.join(d, 'sounds.ini')):
            continue  # the demo .wav files
        shutil.copytree(d, os.path.join(stage, 'usr/share/setu/sounds', name), dirs_exist_ok=True)
        out[name] = themelib.read_ini(os.path.join(d, 'sounds.ini'))['Sounds']
    log(f'sound schemes: {", ".join(out)}')
    return out


def generate_schemes(tmp):
    """schemegen's colour schemes as source packs tmp/schemes/<id>/theme.ini;
    returns that directory."""
    src, root = os.path.join(tmp, 'schemegen'), os.path.join(tmp, 'schemes')
    subprocess.run([sys.executable, SCHEMEGEN, 'build', '-o', src], check=True, stdout=subprocess.DEVNULL)
    os.makedirs(root, exist_ok=True)
    for name in sorted(os.listdir(src)):
        ini = os.path.join(src, name, 'theme.ini')
        if os.path.isfile(ini):
            ident = themelib.read_ini(ini)['Theme']['Id']
            if os.path.isdir(os.path.join(themelib.THEMES, ident)):
                continue  # the distro's own pack of that id wins
            os.makedirs(os.path.join(root, ident))
            shutil.copy(ini, os.path.join(root, ident, 'theme.ini'))
    return root


def cursor_section(pack, cursors):
    name = pack.get('Cursor', 'theme', '')
    if name not in cursors:
        return ''  # an X cursor theme only (e.g. a Debian package's)
    text = cursors[name].replace('DIR\\', f'%WinDir%\\Cursors\\Setu\\{name}\\')
    return '\n'.join(l for l in text.splitlines() if not l.startswith(';'))


def sounds_section(pack, sounds):
    name = pack.get('Sounds', 'scheme', '')
    if not name:
        return ''
    if name not in sounds:
        raise SystemExit(f'{pack.id}: [Sounds] scheme={name} is not one of {", ".join(sounds)}')
    out = [f'; sound scheme {name}']
    for event, f in sounds[name].items():
        out.append(f'[AppEvents\\Schemes\\Apps\\.Default\\{event}\\.Current]\n'
                   f'DefaultValue=%WinDir%\\Media\\Setu\\{name}\\{f}\n')
    return '\n'.join(out).rstrip('\n')


# ---- wallpaper --------------------------------------------------------------
@functools.lru_cache(maxsize=None)
def collection_item(style, palette, seed, size):
    """A collection wallpaper (the light and dark scheme of a palette share
    one), drawn once per run."""
    return load_module('wallgen', WALLGEN).render(style, palette, seed, size).convert('RGB')


def wallpaper(pack, size=WALLPAPER_SIZE):
    """The pack's wallpaper at SIZE (PIL image), how it was made, and its
    collection item if it is one: from the first pack in the chain that
    names a collection= item or a script=."""
    for p in pack.chain:
        wp = p.own.get('Wallpaper', {})
        if wp.get('collection'):
            wg = load_module('wallgen', WALLGEN)
            style, _, palette = wp['collection'].partition('-')
            if style not in wg.STYLES or palette not in wg.PALETTES:
                raise SystemExit(f'{pack.id}: unknown collection={wp["collection"]}')
            seed = int(wp.get('seed', 1) or 1)
            return (collection_item(style, palette, seed, size),
                    f'collection {wp["collection"]} seed {seed}', wp['collection'] if seed == 1 else None)
        if wp.get('script'):
            mod = load_module(f'wallpaper_{p.id.replace("-", "_")}', os.path.join(p.dir, wp['script']))
            return mod.render(*size, pack.tokens.get('Wallpaper', {})), f'{p.id}/{wp["script"]}', None
    raise SystemExit(f'{pack.id}: no [Wallpaper] collection= or script= in its chain')


# ---- control theme ------------------------------------------------------------
COLOR_MAP = [  # .theme [Control Panel\Colors] name -> token (used while
    # the msstyles generator, which owns this mapping, is not available)
    ('ActiveTitle', 'titlebar-active'), ('InactiveTitle', 'titlebar-inactive'),
    ('GradientActiveTitle', 'titlebar-active'), ('GradientInactiveTitle', 'titlebar-inactive'),
    ('TitleText', 'titlebar-text'), ('InactiveTitleText', 'titlebar-text-inactive'),
    ('ActiveBorder', 'border'), ('InactiveBorder', 'border'), ('WindowFrame', 'border'),
    ('ButtonFace', 'window-bg'), ('ButtonAlternateFace', 'window-bg'), ('ButtonText', 'text'),
    ('ButtonShadow', 'border'), ('ButtonDkShadow', 'text-secondary'), ('ButtonHilight', 'surface'),
    ('ButtonLight', 'border-subtle'), ('Window', 'surface'), ('WindowText', 'text'),
    ('Menu', 'menu'), ('MenuBar', 'window-bg'), ('MenuText', 'text'), ('MenuHilight', 'accent'),
    ('Hilight', 'accent'), ('HilightText', 'accent-text'), ('HotTrackingColor', 'accent'),
    ('GrayText', 'text-disabled'), ('Background', 'desktop'), ('AppWorkspace', 'window-bg'),
    ('Scrollbar', 'window-bg'), ('InfoWindow', 'surface'), ('InfoText', 'text'),
]


STUB_COPY = None


def control_theme(pack, out_msstyles, args, tmp):
    """visual/<id>.msstyles and the .theme's [Control Panel\\Colors] text."""
    os.makedirs(os.path.dirname(out_msstyles), exist_ok=True)
    merged = os.path.join(tmp, f'{pack.id}.merged.ini')
    open(merged, 'w').write(pack.merged_ini())
    colors = os.path.join(tmp, f'{pack.id}.colors.ini')
    if os.path.exists(MSSTYLES_TOOL):
        cmd = [sys.executable, MSSTYLES_TOOL, '--tokens', merged, '--out', out_msstyles, '--colors-out', colors]
        if args.wine_bin:
            cmd += ['--wine-bin', args.wine_bin]
        subprocess.run(cmd, check=True)
        text = open(colors).read().strip()
        if not text.startswith('['):
            text = '[Control Panel\\Colors]\n' + text
        return text, 'tools/setu-theme/make-msstyles.py'
    # stub until theme-fork's generator lands: the personality's own theme
    stub = os.path.join(args.personality or '', 'lib/wine/x86_64-windows/setu.msstyles')
    if args.personality and os.path.exists(stub):
        global STUB_COPY
        if STUB_COPY is None:
            shutil.copy(stub, out_msstyles)
            STUB_COPY = out_msstyles
        else:
            link(STUB_COPY, out_msstyles)
        how = 'stub: the personality\'s setu.msstyles'
    else:
        how = 'none (no generator, no personality)'
    lines = ['[Control Panel\\Colors]']
    lines += [f'{n}={" ".join(str(v) for v in themelib.rgb(pack.color(t)))}' for n, t in COLOR_MAP]
    return '\n'.join(lines), how


# ---- preview ------------------------------------------------------------------
def preview(pack, wall, icons_dir):
    """640x400: the wallpaper, a window in the pack's colours, the taskbar."""
    W, H, ss = 640, 400, 2
    c = pack.color
    sw, sh = wall.size
    scale = max(W * ss / sw, H * ss / sh)
    im = wall.resize((round(sw * scale), round(sh * scale)), Image.LANCZOS)
    x0, y0 = (im.size[0] - W * ss) // 2, (im.size[1] - H * ss) // 2
    im = im.crop((x0, y0, x0 + W * ss, y0 + H * ss)).convert('RGBA')
    d = ImageDraw.Draw(im)

    def font(weight, size):
        for p in (f'/usr/share/fonts/opentype/inter/Inter-{weight}.otf',
                  f'/usr/share/fonts/truetype/inter/Inter-{weight}.ttf'):
            if os.path.exists(p):
                return ImageFont.truetype(p, size * ss)
        return ImageFont.load_default()

    def s(*v):
        return [x * ss for x in v]
    # a window
    shadow = Image.new('RGBA', im.size, (0, 0, 0, 0))
    ImageDraw.Draw(shadow).rounded_rectangle(s(94, 46, 506, 326), 10 * ss, fill=(0, 0, 0, 70))
    from PIL import ImageFilter
    im.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(8 * ss)))
    d.rounded_rectangle(s(90, 40, 500, 320), 8 * ss, fill=c('window-bg'), outline=c('border'), width=ss)
    d.rounded_rectangle(s(90, 40, 500, 70), 8 * ss, fill=c('titlebar-active'))
    d.rectangle(s(91, 60, 499, 70), fill=c('titlebar-active'))
    d.text(s(102, 48), pack.get('Theme', 'Name'), font=font('SemiBold', 11), fill=c('titlebar-text'))
    for i, glyph in enumerate(('–', '□', '×')):
        d.text(s(445 + i * 18, 47), glyph, font=font('Regular', 11), fill=c('titlebar-text'))
    # a list with a selected row, on surface
    d.rounded_rectangle(s(104, 80, 486, 270), 4 * ss, fill=c('surface'), outline=c('border-subtle'), width=ss)
    rows = ['Documents', 'Downloads', 'Pictures', 'notes.txt']
    for i, name in enumerate(rows):
        y = 88 + i * 30
        if i == 1:
            d.rounded_rectangle(s(110, y, 480, y + 26), 4 * ss, fill=themelib.over(c('selection'), c('surface')),
                                outline=c('selection-border'), width=ss)
        icon = 'file-text' if '.' in name else 'folder'
        p = os.path.join(icons_dir, icon + '.svg')
        if os.path.exists(p):
            im.alpha_composite(render_svg(p, 16 * ss), tuple(s(118, y + 5)))
        d.text(s(142, y + 6), name, font=font('Regular', 11), fill=c('text'))
    d.text(s(110, 212), 'Secondary text', font=font('Regular', 10), fill=c('text-secondary'))
    # buttons
    d.rounded_rectangle(s(318, 282, 398, 308), 4 * ss, fill=c('surface'), outline=c('border'), width=ss)
    d.text(s(338, 288), 'Cancel', font=font('Regular', 11), fill=c('text'))
    d.rounded_rectangle(s(406, 282, 486, 308), 4 * ss, fill=c('accent'))
    d.text(s(434, 288), 'OK', font=font('SemiBold', 11), fill=c('accent-text'))
    # the taskbar, centred buttons and the clock
    d.rectangle(s(0, H - 34, W, H), fill=c('taskbar'))
    d.line(s(0, H - 34, W, H - 34), fill=c('border-subtle'), width=ss)
    for i, icon in enumerate(('setu-app', 'folder', 'file-text', 'computer')):
        x = W // 2 - 64 + i * 32
        p = os.path.join(icons_dir, icon + '.svg')
        if os.path.exists(p):
            im.alpha_composite(render_svg(p, 20 * ss), tuple(s(x + 6, H - 28)))
    d.rounded_rectangle(s(W // 2 - 26, H - 5, W // 2 - 14, H - 3), ss, fill=c('accent'))
    d.text(s(W - 50, H - 25), '12:00', font=font('Regular', 11), fill=c('taskbar-text'))
    return im.resize((W, H), Image.LANCZOS).convert('RGB')


# ---- one pack -------------------------------------------------------------------
def build_pack(pack, args, sets, cursors, sounds, tmp):
    stage = args.out
    out = os.path.join(stage, 'usr/share/setu/themes', pack.id)
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)
    shutil.copy(os.path.join(pack.dir, 'theme.ini'), out)
    for sub in ('linux',):  # the pack's own templates (the chain finds the rest)
        if os.path.isdir(os.path.join(pack.dir, sub)):
            shutil.copytree(os.path.join(pack.dir, sub), os.path.join(out, sub))
    pack_icons(pack, sets, os.path.join(out, 'icons'))

    # The wallpaper: the distro's own packs carry a 4K BMP (the desktop
    # loads only BMPs); generated packs point at their collection JPEG,
    # which setu-theme converts when it applies the pack.
    if pack.generated:
        wall, how, item = wallpaper(pack, (960, 540))  # for preview.png only
        if not item:
            raise SystemExit(f'{pack.id}: a generated pack needs a seed-1 collection= wallpaper')
        path = f'%WinDir%\\Web\\Wallpaper\\Setu\\{item}.jpg'
    else:
        wall, how, _ = wallpaper(pack)
        os.makedirs(os.path.join(out, 'wallpaper'))
        wall.save(os.path.join(out, 'wallpaper/wallpaper.bmp'), 'BMP')  # 24-bit: what LoadImage reads
        wall.save(os.path.join(out, 'wallpaper/wallpaper.png'), compress_level=6)
        path = f'%WinDir%\\Resources\\Themes\\{pack.id}\\wallpaper\\wallpaper.bmp'
    pack.tokens.setdefault('Wallpaper', {})['path'] = path
    pack.tokens['Wallpaper'].setdefault('style', '10')

    colors, mshow = control_theme(pack, os.path.join(out, 'visual', pack.id + '.msstyles'), args, tmp)
    tpl = (open(pack.find('pack.theme.in')).read().replace('@@COLORS@@', colors)
           .replace('@@CURSORS@@', cursor_section(pack, cursors))
           .replace('@@SOUNDS@@', sounds_section(pack, sounds)))
    with open(os.path.join(out, pack.id + '.theme'), 'w', newline='\r\n') as f:
        f.write(re.sub(r'\n{3,}', '\n\n', themelib.fill(tpl, pack)))

    branding = load_module('branding', pack.find(pack.get('Installer', 'script', 'artwork/branding.py')))
    branding.plymouth(pack, os.path.join(out, 'boot'))
    if not pack.generated:  # installer images: only the default pack's are used
        branding.calamares(pack, wall, os.path.join(out, 'installer'))
        shutil.copy(os.path.join(out, 'icons', 'setu-app.svg'), os.path.join(out, 'installer'))
        render_svg(os.path.join(out, 'icons', 'setu-app.svg'), 128).save(os.path.join(out, 'installer', 'setu-app.png'))

    # Plymouth theme (system packs only: the boot splash is system-wide)
    ply = os.path.join(stage, 'usr/share/plymouth/themes', pack.id)
    shutil.rmtree(ply, ignore_errors=True)
    os.makedirs(ply)
    open(os.path.join(ply, pack.id + '.plymouth'), 'w').write(themelib.fill(open(pack.find('boot/theme.plymouth.in')).read(), pack))
    open(os.path.join(ply, 'setu.script'), 'w').write(themelib.fill(open(pack.find('boot/setu.script.in')).read(), pack))
    for f in ('logo.png', 'spinner.png'):
        shutil.copy(os.path.join(out, 'boot', f), ply)

    pv = preview(pack, wall, os.path.join(out, 'icons'))
    pv.save(os.path.join(out, 'preview.png'), optimize=True)
    if args.previews and not pack.generated:
        pv.save(os.path.join(pack.dir, 'preview.png'), optimize=True)
    with open(os.path.join(out, 'LICENSE'), 'w') as f:
        f.write(f'{pack.get("Theme", "Name")} ({pack.id}), a Setu theme pack.\n\n'
                'Everything in this pack is original Setu work under MIT OR Apache-2.0\n'
                '(https://github.com/ragnar-vallhala/setu, NOTICE section 6): the tokens, the\n'
                'icons (art/icons/icongen.py), the wallpaper (drawn from code), the boot and\n'
                'installer images (drawn from code; text set in Inter, SIL OFL 1.1), except\n'
                'visual/*.msstyles, the control theme: generated from Wine\'s Light theme,\n'
                'LGPL-2.1-or-later like the personality it belongs to. Cursors and sounds\n'
                '(art/cursors, art/sounds, also original MIT OR Apache-2.0) and fonts are\n'
                'installed separately and only named here.\n')
    log(f'{pack.id}: wallpaper {how}; msstyles {mshow}; {len(os.listdir(os.path.join(out, "icons"))) // 2} icons')
    return out


def check(pack):
    """The rules of THEMES.md 'Rules for packs' that the build can check."""
    c = pack.color
    for k, v in pack.tokens.get('Colors', {}).items():
        themelib.rgba(v)
    for fg, bg, need in (('text', 'surface', 4.5), ('accent-text', 'accent', 4.5)):
        r = themelib.contrast(c(fg), c(bg))
        if r < need:
            raise SystemExit(f'{pack.id}: contrast {fg} on {bg} is {r:.2f}:1, needs {need}:1')


def main():
    ap = argparse.ArgumentParser(usage=__doc__)
    ap.add_argument('--out', required=True)
    ap.add_argument('--default', default=themelib.DEFAULT)
    ap.add_argument('--packs')
    ap.add_argument('--wine-bin')
    ap.add_argument('--personality')
    ap.add_argument('--previews', action='store_true')
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        roots = (themelib.THEMES, generate_schemes(tmp))
        ids = args.packs.split(',') if args.packs else themelib.source_packs() + themelib.source_packs(roots[1])
        if args.default not in ids:
            ids.append(args.default)
        packs = [themelib.Pack(i, roots) for i in ids]
        for p in packs:
            check(p)
        sets = build_icon_sets(args.out, tmp)
        cursors = build_cursors(args.out, tmp)
        sounds = build_sounds(args.out, tmp)
        built = {p.id: build_pack(p, args, sets, cursors, sounds, tmp) for p in packs}

    default = built[args.default]
    # the default boot splash
    os.makedirs(os.path.join(args.out, 'etc/plymouth'), exist_ok=True)
    with open(os.path.join(args.out, 'etc/plymouth/plymouthd.conf'), 'w') as f:
        f.write('# The boot splash: the default theme pack\'s (distro/themes/render-packs.py).\n'
                '# setu-session switches it to the active pack (/usr/lib/setu/setu-boot-theme).\n'
                f'[Daemon]\nTheme={args.default}\nShowDelay=0\n')
    # the installer's branding
    pack = next(p for p in packs if p.id == args.default)
    cal = os.path.join(args.out, 'usr/share/calamares/branding/setu')
    os.makedirs(cal, exist_ok=True)
    for f in ('branding.desc', 'stylesheet.qss'):
        open(os.path.join(cal, f), 'w').write(themelib.fill(open(pack.find(f'installer/{f}.in')).read(), pack))
    shutil.copy(os.path.join(default, 'installer/setu-app.png'), os.path.join(cal, 'setu-logo.png'))
    for f in ('setu-welcome.png', 'setu-slide.png'):
        shutil.copy(os.path.join(default, 'installer', f), cal)
    # labwc defaults (every labwc session: desktop, installer, wizard)
    env = dict(os.environ, SETU_THEME_PATH=os.path.join(args.out, 'usr/share/setu/themes'))
    subprocess.run(['sh', SETU_THEME_LINUX, 'labwc', args.default, os.path.join(args.out, 'etc/xdg/labwc')],
                   check=True, env=env, stdout=subprocess.DEVNULL)
    log(f'done: {", ".join(built)} (default {args.default}) in {args.out}')


if __name__ == '__main__':
    main()
