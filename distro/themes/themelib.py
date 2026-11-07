# SPDX-License-Identifier: MIT OR Apache-2.0
"""Reading Setu theme packs (docs/THEMES.md) for the image build: theme.ini
with its Base= chain, colour helpers and the @token@ templates the packs'
*.in files use. Used by render-packs.py and the packs' artwork scripts."""
import configparser
import os
import re

THEMES = os.path.dirname(os.path.abspath(__file__))
DEFAULT = 'setu-light'
ID_RE = re.compile(r'^[a-z0-9-]+$')


def read_ini(path):
    cp = configparser.ConfigParser(interpolation=None, inline_comment_prefixes=(';',),
                                   comment_prefixes=(';', '#'), strict=False)
    cp.optionxform = str  # keys keep their case
    with open(path, encoding='utf-8-sig') as f:
        cp.read_file(f)
    return {s: dict(cp[s]) for s in cp.sections()}


class Pack:
    """A source pack: distro/themes/<id>/ and the packs it inherits from."""

    def __init__(self, ident, roots=(THEMES,), _seen=()):
        if not ID_RE.match(ident):
            raise ValueError(f'bad pack id {ident!r}')
        self.id = ident
        root = next((r for r in roots if os.path.isfile(os.path.join(r, ident, 'theme.ini'))), None)
        if root is None:
            raise ValueError(f'pack {ident} not found in {", ".join(roots)}')
        self.generated = root != THEMES
        self.dir = os.path.join(root, ident)
        self.own = read_ini(os.path.join(self.dir, 'theme.ini'))
        got = self.own.get('Theme', {}).get('Id', ident)
        if got != ident:
            raise ValueError(f'{self.dir}: Id={got} but the folder is {ident}')
        base = self.own.get('Theme', {}).get('Base', '').strip()
        if not base and ident != DEFAULT:
            base = DEFAULT  # THEMES.md: missing keys fall back to the default pack
        if base in _seen + (ident,):
            raise ValueError(f'Base= loop: {" -> ".join(_seen + (ident, base))}')
        self.base = Pack(base, roots, _seen + (ident,)) if base else None
        self.chain = [self] + (self.base.chain if self.base else [])
        # merged tokens: own values over the base's
        self.tokens = {}
        for p in reversed(self.chain):
            for s, kv in p.own.items():
                self.tokens.setdefault(s, {}).update(kv)
        self.tokens['Theme'] = dict(self.tokens.get('Theme', {}), **self.own.get('Theme', {}))
        self.tokens['Theme']['Id'] = ident

    def get(self, section, key, default=None):
        return self.tokens.get(section, {}).get(key, default)

    def color(self, key):
        return self.get('Colors', key)

    def find(self, rel):
        """The first file REL in the chain (own pack first), or None."""
        for p in self.chain:
            f = os.path.join(p.dir, rel)
            if os.path.exists(f):
                return f
        return None

    def icons(self):
        """{name: svg path}: every icons/*.svg in the chain, own ones first."""
        out = {}
        for p in reversed(self.chain):
            d = os.path.join(p.dir, 'icons')
            if os.path.isdir(d):
                for f in sorted(os.listdir(d)):
                    if f.endswith('.svg'):
                        out[f[:-4]] = os.path.join(d, f)
        return dict(sorted(out.items()))

    def merged_ini(self):
        """theme.ini text with the chain resolved (every key present)."""
        lines = [f'; {self.id}: theme.ini with Base= resolved ({" -> ".join(p.id for p in self.chain)})']
        for s, kv in self.tokens.items():
            lines.append(f'\n[{s}]')
            lines += [f'{k}={v}' for k, v in kv.items()]
        return '\n'.join(lines) + '\n'


def source_packs(root=THEMES):
    return sorted(d for d in os.listdir(root)
                  if ID_RE.match(d) and os.path.isfile(os.path.join(root, d, 'theme.ini')))


# ---- colours ----------------------------------------------------------------
def rgba(h):
    h = h.strip().lstrip('#')
    if len(h) not in (6, 8) or not re.fullmatch(r'[0-9A-Fa-f]+', h):
        raise ValueError(f'bad colour #{h}')
    v = [int(h[i:i + 2], 16) for i in range(0, len(h), 2)]
    return tuple(v) if len(v) == 4 else tuple(v) + (255,)


def rgb(h):
    return rgba(h)[:3]


def hexc(c):
    return '#%02X%02X%02X' % tuple(max(0, min(255, round(v))) for v in c[:3])


def mix(a, b, t):
    """A + (B - A) * T, colours as '#RRGGBB'."""
    a, b = rgb(a), rgb(b)
    return hexc([x + (y - x) * t for x, y in zip(a, b)])


def over(fg, bg):
    """FG ('#RRGGBBAA') composited over the opaque BG."""
    r, g, b, a = rgba(fg)
    return mix(bg, hexc((r, g, b)), a / 255)


def luminance(h):
    def lin(c):
        c /= 255
        return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4
    r, g, b = (lin(v) for v in rgb(h))
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def contrast(a, b):
    la, lb = sorted((luminance(a), luminance(b)), reverse=True)
    return (la + 0.05) / (lb + 0.05)


# ---- templates ----------------------------------------------------------------
def derived(pack):
    """Tokens computed from others, for templates (the [Derived] section)."""
    c = pack.color
    dark = pack.get('Theme', 'Mode', 'light') == 'dark'
    sidebar = pack.get('Installer', 'sidebar', c('taskbar'))
    return {
        'surface-pressed': mix(c('surface-hover'), c('text'), 0.05),
        'surface-alt': mix(c('surface'), c('text'), 0.02),
        'selection-solid': over(c('selection'), c('surface')),
        'scrollbar': mix(c('surface'), c('text-secondary'), 0.5),
        'scrollbar-hover': mix(c('surface'), c('text-secondary'), 0.8),
        'sidebar-hover': mix(sidebar, c('text'), 0.07 if dark else 0.05),
        'combo-hover': mix(c('surface'), c('text'), 0.02),
    }


TOKEN_RE = re.compile(r'@([A-Za-z]+\.)?([A-Za-z0-9-]+)(\|[a-z]+)?@')


def fill(text, pack):
    """Replace @key@ (a [Colors] key), @Section.key@ and @...|filter@ (rgbf:
    '0.953, 0.953, 0.953'; int: rounded; face: the first font of a list;
    weight: 'bold' at 600 and up, else 'normal'; nohash: without '#')."""
    der = derived(pack)

    def sub(m):
        sec = (m.group(1) or 'Colors.')[:-1]
        key, flt = m.group(2), (m.group(3) or '|')[1:]
        v = der.get(key) if sec == 'Derived' else pack.get(sec, key)
        if v is None:
            raise KeyError(f'{pack.id}: template token {m.group(0)} has no value')
        if flt == 'rgbf':
            v = ', '.join('%.3f' % (x / 255) for x in rgb(v))
        elif flt == 'int':
            v = str(round(float(v)))
        elif flt == 'face':
            v = v.split(',')[0].strip()
        elif flt == 'weight':
            v = 'bold' if int(v) >= 600 else 'normal'
        elif flt == 'nohash':
            v = v.lstrip('#')
        elif flt:
            raise KeyError(f'unknown template filter |{flt}')
        return v
    return TOKEN_RE.sub(sub, text)
