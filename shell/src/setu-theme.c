/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * setu-theme.c: the theme pack loader (setu-theme.h, docs/THEMES.md).
 *
 * References (learn.microsoft.com): RegGetValueW, GetEnvironmentVariableW
 * (LOCALAPPDATA), GetWindowsDirectoryW, CreateFileW/ReadFile,
 * MultiByteToWideChar (CP_UTF8), "Theme file format" (the .theme INI file
 * and its [VisualStyles], [Control Panel\Colors] and [Control Panel\Desktop]
 * sections), WM_SETTINGCHANGE, WM_THEMECHANGED. Contrast: WCAG 2.x
 * "contrast ratio" and "relative luminance" (w3.org/TR/WCAG21).
 */
#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include <stdlib.h>
#include <wchar.h>
#include <math.h>
#include "setu-theme.h"

struct setu_theme setu_theme;

#define SETU_X_KEY(id, key, def) key,
const char *const setu_color_key[TC_COUNT] = { SETU_COLOR_TOKENS(SETU_X_KEY) };
const char *const setu_metric_key[TM_COUNT] = { SETU_METRIC_TOKENS(SETU_X_KEY) };
const char *const setu_icon_key[TI_COUNT] = { SETU_ICON_NAMES(SETU_X_KEY) };
#undef SETU_X_KEY

/* ------------------------------------------------------------------ */
/* The built-in pack: setu-light, docs/DESIGN.md                        */

struct builtin { const WCHAR *section, *key, *value; };

#define SETU_X_COLOR(id, key, def) { L"Colors", L"" key, L"" def },
#define SETU_X_STR(x) L"" #x
#define SETU_X_METRIC(id, key, def) { L"Metrics", L"" key, SETU_X_STR(def) },
static const struct builtin builtin[] =
{
    { L"Theme", L"Id", L"setu-light" },
    { L"Theme", L"Name", L"Setu Light" },
    { L"Theme", L"Version", L"1" },
    { L"Theme", L"Mode", L"light" },
    SETU_COLOR_TOKENS(SETU_X_COLOR)
    { L"Fonts", L"ui", L"Inter" },
    { L"Fonts", L"ui-size", L"9" },
    { L"Fonts", L"caption-weight", L"600" },
    { L"Fonts", L"title-size", L"10.5" },
    { L"Fonts", L"header-size", L"14" },
    { L"Fonts", L"mono", L"JetBrains Mono, Noto Sans Mono" },
    { L"Fonts", L"smoothing", L"grayscale" },
    SETU_METRIC_TOKENS(SETU_X_METRIC)
    { L"Shell", L"taskbar-alignment", L"center" },
    { L"Shell", L"start-columns", L"6" },
    { L"Shell", L"start-rows", L"4" },
    { L"Cursor", L"theme", L"Adwaita" },
    { L"Cursor", L"size", L"24" },
    { L"Boot", L"background", L"#F3F3F3" },
};
#undef SETU_X_COLOR
#undef SETU_X_METRIC
#undef SETU_X_STR

/* ------------------------------------------------------------------ */
/* Files                                                                */

/* the whole file as a NUL-terminated wide string (UTF-8 or UTF-16LE) */
static WCHAR *read_text_file(const WCHAR *path)
{
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    LARGE_INTEGER size;
    BYTE *raw;
    DWORD got = 0;
    WCHAR *text = NULL;

    if (f == INVALID_HANDLE_VALUE) return NULL;
    if (!GetFileSizeEx(f, &size) || size.QuadPart > 4 * 1024 * 1024 || !(raw = malloc((size_t)size.QuadPart + 2)))
    {
        CloseHandle(f);
        return NULL;
    }
    if (ReadFile(f, raw, (DWORD)size.QuadPart, &got, NULL))
    {
        if (got >= 2 && raw[0] == 0xff && raw[1] == 0xfe)
        {
            if ((text = malloc(got + 2)))
            {
                memcpy(text, raw + 2, got - 2);
                text[(got - 2) / 2] = 0;
            }
        }
        else
        {
            const char *s = (const char *)raw;
            int n, len = (int)got;
            if (len >= 3 && raw[0] == 0xef && raw[1] == 0xbb && raw[2] == 0xbf) { s += 3; len -= 3; }
            n = MultiByteToWideChar(CP_UTF8, 0, s, len, NULL, 0);
            if ((text = malloc(((size_t)n + 1) * sizeof(WCHAR))))
            {
                MultiByteToWideChar(CP_UTF8, 0, s, len, text, n);
                text[n] = 0;
            }
        }
    }
    free(raw);
    CloseHandle(f);
    return text;
}

static void trim(WCHAR *s)
{
    WCHAR *b = s, *e;
    while (*b == L' ' || *b == L'\t') b++;
    if (b != s) memmove(s, b, (wcslen(b) + 1) * sizeof(WCHAR));
    e = s + wcslen(s);
    while (e > s && (e[-1] == L' ' || e[-1] == L'\t' || e[-1] == L'\r')) *--e = 0;
}

static void kv_add(struct setu_kv **list, int *n, const WCHAR *sec, const WCHAR *key, const WCHAR *val, int level)
{
    struct setu_kv *e;
    if (!(*n & 63))
    {
        struct setu_kv *p = realloc(*list, sizeof(**list) * (*n + 64));
        if (!p) return;
        *list = p;
    }
    e = &(*list)[(*n)++];
    lstrcpynW(e->section, sec, ARRAYSIZE(e->section));
    lstrcpynW(e->key, key, ARRAYSIZE(e->key));
    lstrcpynW(e->value, val, ARRAYSIZE(e->value));
    e->level = level;
}

/* Parse INI text: [section], key=value, ';' or '#' comment lines and
 * " ;" inline comments. PREFIX is put before every section name. Lines
 * that are none of these are counted in *BAD (first one in BADLINE). */
BOOL setu_ini_parse(const WCHAR *path, const WCHAR *prefix, struct setu_kv **list, int *n, int level,
                    int *bad, int *badline)
{
    WCHAR *text = read_text_file(path), *line, *next, sec[40] = L"", full[40];
    int lineno = 0;

    if (!text) return FALSE;
    for (line = text; line; line = next)
    {
        WCHAR *eq, *c;
        lineno++;
        if ((next = wcschr(line, L'\n'))) *next++ = 0;
        trim(line);
        if (!line[0] || line[0] == L';' || line[0] == L'#') continue;
        if (line[0] == L'[')
        {
            if (!(c = wcschr(line, L']'))) { if (bad && !(*bad)++) *badline = lineno; continue; }
            *c = 0;
            lstrcpynW(sec, line + 1, ARRAYSIZE(sec));
            trim(sec);
            continue;
        }
        if (!(eq = wcschr(line, L'=')))
        {
            if (bad && !(*bad)++) *badline = lineno;
            continue;
        }
        *eq++ = 0;
        /* inline comment: a ';' after a blank */
        for (c = eq; *c; c++)
            if (*c == L';' && (c == eq || c[-1] == L' ' || c[-1] == L'\t')) { *c = 0; break; }
        trim(line);
        trim(eq);
        _snwprintf(full, ARRAYSIZE(full), L"%ls%ls", prefix, sec);
        full[ARRAYSIZE(full) - 1] = 0;
        kv_add(list, n, full, line, eq, level);
    }
    free(text);
    return TRUE;
}

/* ------------------------------------------------------------------ */
/* Finding packs                                                        */

static BOOL valid_id(const WCHAR *id)
{
    const WCHAR *p;
    if (!id || !id[0] || wcslen(id) >= 64) return FALSE;
    for (p = id; *p; p++)
        if (!((*p >= L'a' && *p <= L'z') || (*p >= L'0' && *p <= L'9') || *p == L'-')) return FALSE;
    return TRUE;
}

void setu_theme_current_id(WCHAR *id, int len)
{
    WCHAR buf[128];
    DWORD size = sizeof(buf);
    if (!RegGetValueW(HKEY_CURRENT_USER, L"Software\\Setu\\Theme", L"Current", RRF_RT_REG_SZ, NULL, buf, &size)
        && valid_id(buf))
        lstrcpynW(id, buf, len);
    else
        lstrcpynW(id, L"setu-light", len);
}

static BOOL is_pack_dir(const WCHAR *dir, const WCHAR *id)
{
    WCHAR p[MAX_PATH];
    _snwprintf(p, ARRAYSIZE(p), L"%ls\\theme.ini", dir);
    p[MAX_PATH - 1] = 0;
    if (GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES) return TRUE;
    _snwprintf(p, ARRAYSIZE(p), L"%ls\\%ls.theme", dir, id);
    p[MAX_PATH - 1] = 0;
    return GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES;
}

BOOL setu_theme_find(const WCHAR *id, WCHAR *dir, int len, enum setu_theme_where *where)
{
    WCHAR base[MAX_PATH], p[MAX_PATH];
    DWORD n;

    if (!valid_id(id)) return FALSE;
    n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, ARRAYSIZE(base));
    if (n && n < ARRAYSIZE(base))
    {
        _snwprintf(p, ARRAYSIZE(p), L"%ls\\Setu\\Themes\\%ls", base, id);
        p[MAX_PATH - 1] = 0;
        if (is_pack_dir(p, id))
        {
            lstrcpynW(dir, p, len);
            if (where) *where = THEME_USER;
            return TRUE;
        }
    }
    n = GetWindowsDirectoryW(base, ARRAYSIZE(base));
    if (n && n < ARRAYSIZE(base))
    {
        _snwprintf(p, ARRAYSIZE(p), L"%ls\\Resources\\Themes\\%ls", base, id);
        p[MAX_PATH - 1] = 0;
        if (is_pack_dir(p, id))
        {
            lstrcpynW(dir, p, len);
            if (where) *where = THEME_SYSTEM;
            return TRUE;
        }
    }
    return FALSE;
}

/* ------------------------------------------------------------------ */
/* Lookups                                                              */

const WCHAR *setu_theme_get(const struct setu_theme *t, const WCHAR *section, const WCHAR *key)
{
    int i, best = -1;
    for (i = 0; i < t->nkv; i++)
        if (!lstrcmpiW(t->kv[i].section, section) && !lstrcmpiW(t->kv[i].key, key))
            if (best < 0 || t->kv[i].level < t->kv[best].level) best = i;
    if (best >= 0) return t->kv[best].value;
    for (i = 0; i < (int)ARRAYSIZE(builtin); i++)
        if (!lstrcmpiW(builtin[i].section, section) && !lstrcmpiW(builtin[i].key, key)) return builtin[i].value;
    return NULL;
}

int setu_theme_level(const struct setu_theme *t, const WCHAR *section, const WCHAR *key)
{
    int i, best = -2;
    for (i = 0; i < t->nkv; i++)
        if (!lstrcmpiW(t->kv[i].section, section) && !lstrcmpiW(t->kv[i].key, key))
            if (best < 0 || t->kv[i].level < best) best = t->kv[i].level;
    if (best >= 0) return best;
    for (i = 0; i < (int)ARRAYSIZE(builtin); i++)
        if (!lstrcmpiW(builtin[i].section, section) && !lstrcmpiW(builtin[i].key, key)) return -1;
    return -2;
}

const WCHAR *setu_theme_icon_lookup(const struct setu_theme *t, const WCHAR *name)
{
    int i, best = -1;
    for (i = 0; i < t->nicons; i++)
        if (!lstrcmpiW(t->icons[i].key, name) && (best < 0 || t->icons[i].level < t->icons[best].level)) best = i;
    return best >= 0 ? t->icons[best].value : NULL;
}

static int hexval(WCHAR c)
{
    if (c >= L'0' && c <= L'9') return c - L'0';
    if (c >= L'a' && c <= L'f') return c - L'a' + 10;
    if (c >= L'A' && c <= L'F') return c - L'A' + 10;
    return -1;
}

BOOL setu_parse_color(const WCHAR *s, COLORREF *c, BYTE *alpha)
{
    int v[8], i, n;
    if (!s || s[0] != L'#') return FALSE;
    n = (int)wcslen(s + 1);
    if (n != 6 && n != 8) return FALSE;
    for (i = 0; i < n; i++)
        if ((v[i] = hexval(s[1 + i])) < 0) return FALSE;
    if (c) *c = RGB(v[0] * 16 + v[1], v[2] * 16 + v[3], v[4] * 16 + v[5]);
    if (alpha) *alpha = n == 8 ? (BYTE)(v[6] * 16 + v[7]) : 255;
    return TRUE;
}

static const WCHAR *builtin_value(const WCHAR *section, const WCHAR *key)
{
    int i;
    for (i = 0; i < (int)ARRAYSIZE(builtin); i++)
        if (!lstrcmpiW(builtin[i].section, section) && !lstrcmpiW(builtin[i].key, key)) return builtin[i].value;
    return L"";
}

static double get_number(const struct setu_theme *t, const WCHAR *section, const WCHAR *key, double lo, double hi)
{
    const WCHAR *v = setu_theme_get(t, section, key);
    WCHAR *end;
    double d = v ? wcstod(v, &end) : 0;
    if (!v || end == v || *end || d < lo || d > hi) d = wcstod(builtin_value(section, key), NULL);
    return d;
}

/* ------------------------------------------------------------------ */
/* Loading                                                              */

void setu_theme_free(struct setu_theme *t)
{
    free(t->kv);
    free(t->icons);
    memset(t, 0, sizeof(*t));
}

static void join(WCHAR *out, int len, const WCHAR *dir, const WCHAR *rel)
{
    if (rel[0] && (rel[1] == L':' || (rel[0] == L'\\' && rel[1] == L'\\'))) lstrcpynW(out, rel, len);
    else _snwprintf(out, len, L"%ls\\%ls", dir, rel);
    out[len - 1] = 0;
    for (; *out; out++) if (*out == L'/') *out = L'\\';
}

/* the folder part of PATH */
static void dir_of(WCHAR *out, int len, const WCHAR *path)
{
    WCHAR *s;
    lstrcpynW(out, path, len);
    if ((s = wcsrchr(out, L'\\'))) *s = 0;
}

/* Add the [Icons] of index.ini PATH at LEVEL, files made absolute. Icon
 * levels are twice the pack levels, so that a set (below) can sit just
 * before the pack that names it. */
static BOOL load_index(struct setu_theme *t, const WCHAR *path, int level, WCHAR *err, int errlen)
{
    WCHAR idir[MAX_PATH];
    int first = t->nicons, bad = 0, badline = 0, i, j;

    dir_of(idir, ARRAYSIZE(idir), path);
    if (!setu_ini_parse(path, L"", &t->icons, &t->nicons, level, &bad, &badline)) return FALSE;
    for (i = j = first; i < t->nicons; i++)
    {
        if (lstrcmpiW(t->icons[i].section, L"Icons")) continue;
        WCHAR rel[MAX_PATH];
        lstrcpynW(rel, t->icons[i].value, MAX_PATH);
        t->icons[j] = t->icons[i];
        join(t->icons[j].value, MAX_PATH, idir, rel);
        j++;
    }
    t->nicons = j;
    if (bad && err && !err[0]) _snwprintf(err, errlen, L"%ls:%d: not a section or key=value line", path, badline);
    return TRUE;
}

/* [Icons] set=NAME: an icon set, %LOCALAPPDATA%\Setu\Icons\NAME (a user's)
 * or %WINDIR%\Resources\Setu\Icons\NAME. The set named closest to the
 * pack wins; its icons come before those of the pack that names it and
 * of that pack's bases, after those of packs deriving from it. */
static void load_icon_set(struct setu_theme *t, WCHAR *err, int errlen)
{
    WCHAR base[MAX_PATH], p[MAX_PATH];
    const WCHAR *set = NULL;
    int i, level = -1;
    DWORD n;

    for (i = 0; i < t->nkv; i++)
        if (!lstrcmpiW(t->kv[i].section, L"Icons") && !lstrcmpiW(t->kv[i].key, L"set") && t->kv[i].value[0] &&
            (level < 0 || t->kv[i].level < level))
        {
            set = t->kv[i].value;
            level = t->kv[i].level;
        }
    if (!set) return;
    lstrcpynW(t->icon_set, set, ARRAYSIZE(t->icon_set));
    n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, ARRAYSIZE(base));
    if (n && n < ARRAYSIZE(base))
    {
        _snwprintf(p, ARRAYSIZE(p), L"%ls\\Setu\\Icons\\%ls\\index.ini", base, set);
        p[MAX_PATH - 1] = 0;
        if (load_index(t, p, level * 2 - 1, err, errlen)) return;
    }
    n = GetWindowsDirectoryW(base, ARRAYSIZE(base));
    if (n && n < ARRAYSIZE(base))
    {
        _snwprintf(p, ARRAYSIZE(p), L"%ls\\Resources\\Setu\\Icons\\%ls\\index.ini", base, set);
        p[MAX_PATH - 1] = 0;
        if (load_index(t, p, level * 2 - 1, err, errlen)) return;
    }
    if (err && !err[0]) _snwprintf(err, errlen, L"icon set '%ls' is not installed", set);
}

/* Load one pack of the chain at LEVEL. */
static void load_level(struct setu_theme *t, const WCHAR *dir, const WCHAR *id, int level, WCHAR *err, int errlen)
{
    WCHAR p[MAX_PATH], tokens[MAX_PATH] = L"theme.ini", icons[MAX_PATH] = L"icons\\index.ini";
    int i, first = t->nkv, bad = 0, badline = 0;

    _snwprintf(p, ARRAYSIZE(p), L"%ls\\%ls.theme", dir, id);
    p[MAX_PATH - 1] = 0;
    if (setu_ini_parse(p, L"theme:", &t->kv, &t->nkv, level, NULL, NULL))
        for (i = first; i < t->nkv; i++)
        {
            if (lstrcmpiW(t->kv[i].section, L"theme:Setu")) continue;
            if (!lstrcmpiW(t->kv[i].key, L"Tokens") && t->kv[i].value[0]) lstrcpynW(tokens, t->kv[i].value, MAX_PATH);
            if (!lstrcmpiW(t->kv[i].key, L"Icons") && t->kv[i].value[0]) lstrcpynW(icons, t->kv[i].value, MAX_PATH);
        }
    join(p, ARRAYSIZE(p), dir, tokens);
    if (!setu_ini_parse(p, L"", &t->kv, &t->nkv, level, &bad, &badline))
    {
        if (err && !err[0]) _snwprintf(err, errlen, L"%ls: cannot read", p);
    }
    else if (bad && err && !err[0]) _snwprintf(err, errlen, L"%ls:%d: not a section or key=value line", p, badline);

    join(p, ARRAYSIZE(p), dir, icons);
    load_index(t, p, level * 2, err, errlen);
    if (err) err[errlen - 1] = 0;
}

static void fill_typed(struct setu_theme *t)
{
    const WCHAR *v;
    int i;

    for (i = 0; i < TC_COUNT; i++)
    {
        WCHAR key[64];
        MultiByteToWideChar(CP_UTF8, 0, setu_color_key[i], -1, key, ARRAYSIZE(key));
        if (!setu_parse_color(setu_theme_get(t, L"Colors", key), &t->color[i], &t->alpha[i]))
            setu_parse_color(builtin_value(L"Colors", key), &t->color[i], &t->alpha[i]);
    }
    /* a pack that sets its accent but not the selection gets an accent selection */
    if (setu_theme_level(t, L"Colors", L"accent") >= 0)
    {
        if (setu_theme_level(t, L"Colors", L"selection") < 0)
        {
            t->color[TC_SELECTION] = t->color[TC_ACCENT];
            t->alpha[TC_SELECTION] = 0x33;
        }
        if (setu_theme_level(t, L"Colors", L"selection-border") < 0)
            t->color[TC_SELECTION_BORDER] = t->color[TC_ACCENT];
    }
    for (i = 0; i < TM_COUNT; i++)
    {
        WCHAR key[64];
        MultiByteToWideChar(CP_UTF8, 0, setu_metric_key[i], -1, key, ARRAYSIZE(key));
        t->metric[i] = (int)floor(get_number(t, L"Metrics", key, 0, 512) + 0.5);
    }
    lstrcpynW(t->font_ui, (v = setu_theme_get(t, L"Fonts", L"ui")) && v[0] ? v : L"Inter", ARRAYSIZE(t->font_ui));
    lstrcpynW(t->font_mono, (v = setu_theme_get(t, L"Fonts", L"mono")) && v[0] ? v : L"JetBrains Mono",
              ARRAYSIZE(t->font_mono));
    t->ui_size10 = (int)floor(get_number(t, L"Fonts", L"ui-size", 4, 72) * 10 + 0.5);
    t->title_size10 = (int)floor(get_number(t, L"Fonts", L"title-size", 4, 72) * 10 + 0.5);
    t->header_size10 = (int)floor(get_number(t, L"Fonts", L"header-size", 4, 96) * 10 + 0.5);
    t->caption_weight = (int)get_number(t, L"Fonts", L"caption-weight", 100, 900);
    v = setu_theme_get(t, L"Fonts", L"smoothing");
    t->smoothing = v && !lstrcmpiW(v, L"none") ? SMOOTH_NONE : v && !lstrcmpiW(v, L"rgb") ? SMOOTH_RGB : SMOOTH_GRAYSCALE;

    v = setu_theme_get(t, L"Shell", L"taskbar-alignment");
    t->taskbar_left = v && !lstrcmpiW(v, L"left");
    t->start_columns = (int)get_number(t, L"Shell", L"start-columns", 1, 12);
    t->start_rows = (int)get_number(t, L"Shell", L"start-rows", 1, 8);
    v = setu_theme_get(t, L"Theme", L"Mode");
    t->dark = v && !lstrcmpiW(v, L"dark");
    v = setu_theme_get(t, L"Theme", L"Name");
    lstrcpynW(t->name, v && v[0] ? v : t->id, ARRAYSIZE(t->name));

    for (i = 0; i < TI_COUNT; i++)
    {
        WCHAR name[64];
        const WCHAR *f;
        MultiByteToWideChar(CP_UTF8, 0, setu_icon_key[i], -1, name, ARRAYSIZE(name));
        t->icon_file[i][0] = 0;
        if ((f = setu_theme_icon_lookup(t, name)) && GetFileAttributesW(f) != INVALID_FILE_ATTRIBUTES)
            lstrcpynW(t->icon_file[i], f, MAX_PATH);
    }
}

/* a pack next to DIR (packs checked or built side by side) */
static BOOL sibling_pack(const WCHAR *dir, const WCHAR *id, WCHAR *out, int len)
{
    WCHAR p[MAX_PATH], *s;
    lstrcpynW(p, dir, ARRAYSIZE(p));
    if (!(s = wcsrchr(p, L'\\'))) return FALSE;
    *s = 0;
    _snwprintf(out, len, L"%ls\\%ls", p, id);
    out[len - 1] = 0;
    return valid_id(id) && is_pack_dir(out, id);
}

BOOL setu_theme_load(struct setu_theme *t, const WCHAR *id, const WCHAR *dir, WCHAR *err, int errlen)
{
    WCHAR cur[64], d[MAX_PATH];
    enum setu_theme_where where = THEME_BUILTIN;
    BOOL found;
    int level;

    memset(t, 0, sizeof(*t));
    if (err && errlen) err[0] = 0;
    if (dir)
    {
        const WCHAR *slash = wcsrchr(dir, L'\\');
        WCHAR guess[64];
        lstrcpynW(d, dir, ARRAYSIZE(d));
        lstrcpynW(guess, slash ? slash + 1 : dir, ARRAYSIZE(guess));
        lstrcpynW(cur, id ? id : guess, ARRAYSIZE(cur));
        found = GetFileAttributesW(d) != INVALID_FILE_ATTRIBUTES;
        where = THEME_USER;
    }
    else
    {
        if (id) lstrcpynW(cur, id, ARRAYSIZE(cur));
        else setu_theme_current_id(cur, ARRAYSIZE(cur));
        found = setu_theme_find(cur, d, ARRAYSIZE(d), &where);
    }
    lstrcpynW(t->id, cur, ARRAYSIZE(t->id));
    t->where = found ? where : THEME_BUILTIN;
    if (!found)
    {
        if (err && errlen) _snwprintf(err, errlen, L"theme pack '%ls' is not installed", cur);
        fill_typed(t);
        return FALSE;
    }
    lstrcpynW(t->dir, d, ARRAYSIZE(t->dir));
    for (level = 0; level < (int)ARRAYSIZE(t->chain); level++)
    {
        const WCHAR *base;
        WCHAR next[64];
        int i;

        lstrcpynW(t->chain[level], d, MAX_PATH);
        t->levels = level + 1;
        load_level(t, d, cur, level, err, errlen);
        /* Base= of this level only */
        next[0] = 0;
        for (i = 0; i < t->nkv; i++)
            if (t->kv[i].level == level && !lstrcmpiW(t->kv[i].section, L"Theme") && !lstrcmpiW(t->kv[i].key, L"Base"))
                lstrcpynW(next, t->kv[i].value, ARRAYSIZE(next));
        base = next;
        if (!base[0]) break;
        {
            BOOL loop = FALSE;
            WCHAR bd[MAX_PATH];
            int k;
            if (!setu_theme_find(base, bd, ARRAYSIZE(bd), NULL) && !sibling_pack(t->chain[level], base, bd, ARRAYSIZE(bd)))
            {
                /* setu-light is built in: its values are the fallbacks anyway */
                if (lstrcmpiW(base, L"setu-light") && err && errlen && !err[0])
                    _snwprintf(err, errlen, L"base pack '%ls' is not installed", base);
                break;
            }
            for (k = 0; k <= level; k++)
                if (!lstrcmpiW(t->chain[k], bd)) loop = TRUE;
            if (loop)
            {
                if (err && errlen && !err[0]) _snwprintf(err, errlen, L"Base= chain loops at '%ls'", base);
                break;
            }
            lstrcpynW(d, bd, ARRAYSIZE(d));
            lstrcpynW(cur, base, ARRAYSIZE(cur));
        }
    }
    load_icon_set(t, err, errlen);
    if (err && errlen) err[errlen - 1] = 0;
    fill_typed(t);
    return TRUE;
}

void setu_theme_reload(void)
{
    struct setu_theme t;
    setu_theme_load(&t, NULL, NULL, NULL, 0);
    setu_theme_free(&setu_theme);
    setu_theme = t;
}

BOOL setu_theme_changed_msg(UINT msg, WPARAM wp, LPARAM lp)
{
    (void)wp;
    if (msg == WM_THEMECHANGED) return TRUE;
    if (msg == WM_SETTINGCHANGE && lp)
        return !lstrcmpiW((const WCHAR *)lp, L"SetuTheme") || !lstrcmpiW((const WCHAR *)lp, L"ImmersiveColorSet");
    return FALSE;
}

static double lin(int c)
{
    double v = c / 255.0;
    return v <= 0.04045 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4);
}

int setu_contrast100(COLORREF a, COLORREF b)
{
    double la = 0.2126 * lin(GetRValue(a)) + 0.7152 * lin(GetGValue(a)) + 0.0722 * lin(GetBValue(a));
    double lb = 0.2126 * lin(GetRValue(b)) + 0.7152 * lin(GetGValue(b)) + 0.0722 * lin(GetBValue(b));
    double hi = la > lb ? la : lb, lo = la > lb ? lb : la;
    return (int)floor((hi + 0.05) / (lo + 0.05) * 100);
}
