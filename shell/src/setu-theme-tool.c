/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * setu-theme: list, check and switch Setu theme packs (docs/THEMES.md).
 *
 *   setu-theme list                 ids, names, mode, where installed
 *   setu-theme current
 *   setu-theme apply <id>           switch now
 *   setu-theme apply --login <id>   what setu-session runs at login (no broadcasts)
 *   setu-theme check <dir>          validate a pack
 *
 * Exit codes: 0 done, 1 usage, 2 pack not found or not valid, 3 a step of
 * apply failed (the others still ran).
 *
 * apply uses documented APIs and registry values only (learn.microsoft.com):
 *   1. RegSetKeyValueW: HKCU\Software\Setu\Theme, Current
 *   2. HKCU\Software\Microsoft\Windows\CurrentVersion\ThemeManager
 *      (DllName, ColorName, SizeName, ThemeActive) from the .theme's
 *      [VisualStyles] (WM_THEMECHANGED is sent in step 7)
 *   3. every token in HKCU\Software\Setu\Theme\Colors and \Metrics (the
 *      window frames read them), then SetSysColors and HKCU\Control
 *      Panel\Colors ("R G B" strings, the names of the .theme file format's
 *      [Control Panel\Colors] section)
 *   4. SystemParametersInfoW(SPI_SETDESKWALLPAPER) with WallpaperStyle and
 *      TileWallpaper in HKCU\Control Panel\Desktop; a wallpaper that isn't
 *      a bitmap is converted with WIC (IWICImagingFactory, BMP encoder).
 *      At login a wallpaper the user chose is kept.
 *   5. HKCU\Software\Wine\Fonts\Overrides (Setu's font override list,
 *      docs/DESIGN.md) and SPI_SETFONTSMOOTHING, SPI_SETFONTSMOOTHINGTYPE,
 *      SPI_SETFONTSMOOTHINGORIENTATION
 *   6. HKLM\Software\Microsoft\Windows\CurrentVersion\Explorer\Shell Icons
 *      (SHSTOCKICONID indexes -> "path,0") and SHChangeNotify(SHCNE_ASSOCCHANGED)
 *   7. SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0,
 *      "ImmersiveColorSet" and "SetuTheme")
 * --login writes the same settings without telling running programs
 * (no SetSysColors, no SPIF_SENDCHANGE, no broadcasts): nothing runs yet.
 */
#ifndef UNICODE
#define UNICODE
#endif
#define _WIN32_WINNT 0x0A00
#define COBJMACROS
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <wincodec.h>
#include <stdio.h>
#include <wchar.h>
#include "setu-theme.h"

static int problems, warnings;

static void out(const WCHAR *fmt, ...)
{
    WCHAR buf[2048];
    char utf8[6144];
    va_list ap;
    DWORD done;
    int n;
    va_start(ap, fmt);
    _vsnwprintf(buf, ARRAYSIZE(buf), fmt, ap);
    va_end(ap);
    buf[ARRAYSIZE(buf) - 1] = 0;
    n = WideCharToMultiByte(CP_UTF8, 0, buf, -1, utf8, sizeof(utf8), NULL, NULL);
    if (n > 1) WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), utf8, n - 1, &done, NULL);
}

#define ERR(...)  do { problems++; out(L"error: " __VA_ARGS__); out(L"\n"); } while (0)
#define WARN(...) do { warnings++; out(L"warning: " __VA_ARGS__); out(L"\n"); } while (0)

/* ------------------------------------------------------------------ */
/* Colours                                                              */

/* COLOR_* and their names in HKCU\Control Panel\Colors and in a .theme's
 * [Control Panel\Colors]; TOKEN is the theme.ini colour used when the
 * .theme doesn't set one. */
static const struct { int index; const WCHAR *name; int token; } sys_colors[] =
{
    { COLOR_SCROLLBAR,               L"Scrollbar",             TC_WINDOW_BG },
    { COLOR_BACKGROUND,              L"Background",            TC_DESKTOP },
    { COLOR_ACTIVECAPTION,           L"ActiveTitle",           TC_TITLEBAR_ACTIVE },
    { COLOR_INACTIVECAPTION,         L"InactiveTitle",         TC_TITLEBAR_INACTIVE },
    { COLOR_MENU,                    L"Menu",                  TC_MENU },
    { COLOR_WINDOW,                  L"Window",                TC_SURFACE },
    { COLOR_WINDOWFRAME,             L"WindowFrame",           TC_BORDER },
    { COLOR_MENUTEXT,                L"MenuText",              TC_TEXT },
    { COLOR_WINDOWTEXT,              L"WindowText",            TC_TEXT },
    { COLOR_CAPTIONTEXT,             L"TitleText",             TC_TITLEBAR_TEXT },
    { COLOR_ACTIVEBORDER,            L"ActiveBorder",          TC_TITLEBAR_ACTIVE },
    { COLOR_INACTIVEBORDER,          L"InactiveBorder",        TC_WINDOW_BG },
    { COLOR_APPWORKSPACE,            L"AppWorkspace",          TC_BORDER_SUBTLE },
    { COLOR_HIGHLIGHT,               L"Hilight",               TC_ACCENT },
    { COLOR_HIGHLIGHTTEXT,           L"HilightText",           TC_ACCENT_TEXT },
    { COLOR_BTNFACE,                 L"ButtonFace",            TC_WINDOW_BG },
    { COLOR_BTNSHADOW,               L"ButtonShadow",          TC_BORDER },
    { COLOR_GRAYTEXT,                L"GrayText",              TC_TEXT_DISABLED },
    { COLOR_BTNTEXT,                 L"ButtonText",            TC_TEXT },
    { COLOR_INACTIVECAPTIONTEXT,     L"InactiveTitleText",     TC_TITLEBAR_TEXT_INACTIVE },
    { COLOR_BTNHIGHLIGHT,            L"ButtonHilight",         TC_SURFACE },
    { COLOR_3DDKSHADOW,              L"ButtonDkShadow",        TC_TEXT_DISABLED },
    { COLOR_3DLIGHT,                 L"ButtonLight",           TC_WINDOW_BG },
    { COLOR_INFOTEXT,                L"InfoText",              TC_TEXT },
    { COLOR_INFOBK,                  L"InfoWindow",            TC_SURFACE },
    { 25 /* ButtonAlternateFace */,  L"ButtonAlternateFace",   TC_SURFACE_HOVER },
    { COLOR_HOTLIGHT,                L"HotTrackingColor",      TC_ACCENT },
    { COLOR_GRADIENTACTIVECAPTION,   L"GradientActiveTitle",   TC_TITLEBAR_ACTIVE },
    { COLOR_GRADIENTINACTIVECAPTION, L"GradientInactiveTitle", TC_TITLEBAR_INACTIVE },
    { COLOR_MENUHILIGHT,             L"MenuHilight",           TC_SURFACE_HOVER },
    { COLOR_MENUBAR,                 L"MenuBar",               TC_MENU },
};

/* "R G B" */
static BOOL parse_rgb(const WCHAR *s, COLORREF *c)
{
    int r, g, b;
    WCHAR extra;
    if (!s || swscanf(s, L"%d %d %d %lc", &r, &g, &b, &extra) != 3) return FALSE;
    if (r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255) return FALSE;
    *c = RGB(r, g, b);
    return TRUE;
}

static COLORREF pack_sys_color(const struct setu_theme *t, int i)
{
    COLORREF c;
    if (parse_rgb(setu_theme_get(t, L"theme:Control Panel\\Colors", sys_colors[i].name), &c)) return c;
    return t->color[sys_colors[i].token];
}

/* ------------------------------------------------------------------ */
/* Paths in a .theme                                                    */

/* VALUE from a pack's .theme as a path: environment variables expanded;
 * a path into %WinDir%\Resources\Themes\<id>\ that isn't there (a pack
 * being checked elsewhere) is looked up in the pack's own folder. */
static BOOL pack_path(const struct setu_theme *t, const WCHAR *value, WCHAR *outp, int len)
{
    WCHAR exp[MAX_PATH], marker[96];
    const WCHAR *rest;
    if (!value || !value[0]) return FALSE;
    if (!ExpandEnvironmentStringsW(value, exp, ARRAYSIZE(exp))) lstrcpynW(exp, value, ARRAYSIZE(exp));
    lstrcpynW(outp, exp, len);
    if (GetFileAttributesW(outp) != INVALID_FILE_ATTRIBUTES) return TRUE;
    _snwprintf(marker, ARRAYSIZE(marker), L"\\Resources\\Themes\\%ls\\", t->id);
    marker[ARRAYSIZE(marker) - 1] = 0;
    if (t->dir[0] && (rest = StrStrIW(exp, marker)))
    {
        _snwprintf(outp, len, L"%ls\\%ls", t->dir, rest + wcslen(marker));
        outp[len - 1] = 0;
        if (GetFileAttributesW(outp) != INVALID_FILE_ATTRIBUTES) return TRUE;
    }
    if (exp[0] && exp[1] != L':' && t->dir[0])
    {
        _snwprintf(outp, len, L"%ls\\%ls", t->dir, exp);
        outp[len - 1] = 0;
        if (GetFileAttributesW(outp) != INVALID_FILE_ATTRIBUTES) return TRUE;
    }
    lstrcpynW(outp, exp, len);
    return FALSE;
}

/* ------------------------------------------------------------------ */
/* list / current                                                       */

static void list_in(const WCHAR *root, const WCHAR *where, const WCHAR *active, WCHAR seen[][64], int *nseen)
{
    WCHAR pat[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;

    _snwprintf(pat, ARRAYSIZE(pat), L"%ls\\*", root);
    pat[MAX_PATH - 1] = 0;
    if ((h = FindFirstFileW(pat, &fd)) == INVALID_HANDLE_VALUE) return;
    do
    {
        struct setu_theme t;
        WCHAR dir[MAX_PATH], found[MAX_PATH];
        enum setu_theme_where w;
        int i;
        BOOL dup = FALSE;

        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == L'.') continue;
        /* only what setu_theme_find resolves the id to: a user pack hides a system one */
        if (!setu_theme_find(fd.cFileName, found, ARRAYSIZE(found), &w)) continue;
        _snwprintf(dir, ARRAYSIZE(dir), L"%ls\\%ls", root, fd.cFileName);
        dir[MAX_PATH - 1] = 0;
        if (lstrcmpiW(dir, found)) continue;
        for (i = 0; i < *nseen; i++) if (!lstrcmpiW(seen[i], fd.cFileName)) dup = TRUE;
        if (dup || *nseen >= 256) continue;
        lstrcpynW(seen[(*nseen)++], fd.cFileName, 64);
        setu_theme_load(&t, fd.cFileName, NULL, NULL, 0);
        out(L"%lc %ls\t%ls\t%ls\t%ls\t%ls\n", lstrcmpiW(active, t.id) ? L' ' : L'*', t.id, t.name,
            t.dark ? L"dark" : L"light", where, dir);
        setu_theme_free(&t);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

static int cmd_list(void)
{
    static WCHAR seen[256][64];
    WCHAR active[64], root[MAX_PATH], base[MAX_PATH];
    int nseen = 0;
    DWORD n;

    setu_theme_current_id(active, ARRAYSIZE(active));
    n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, ARRAYSIZE(base));
    if (n && n < ARRAYSIZE(base))
    {
        _snwprintf(root, ARRAYSIZE(root), L"%ls\\Setu\\Themes", base);
        root[MAX_PATH - 1] = 0;
        list_in(root, L"user", active, seen, &nseen);
    }
    n = GetWindowsDirectoryW(base, ARRAYSIZE(base));
    if (n && n < ARRAYSIZE(base))
    {
        _snwprintf(root, ARRAYSIZE(root), L"%ls\\Resources\\Themes", base);
        root[MAX_PATH - 1] = 0;
        list_in(root, L"system", active, seen, &nseen);
    }
    {
        int i;
        BOOL light = FALSE;
        for (i = 0; i < nseen; i++) if (!lstrcmpiW(seen[i], L"setu-light")) light = TRUE;
        if (!light)
            out(L"%lc setu-light\tSetu Light\tlight\tbuilt-in\t-\n", lstrcmpiW(active, L"setu-light") ? L' ' : L'*');
    }
    return 0;
}

static int cmd_current(void)
{
    WCHAR id[64];
    setu_theme_current_id(id, ARRAYSIZE(id));
    out(L"%ls\n", id);
    return 0;
}

/* ------------------------------------------------------------------ */
/* check                                                                */

static BOOL is_number(const WCHAR *v)
{
    WCHAR *end;
    double d;
    if (!v || !v[0]) return FALSE;
    d = wcstod(v, &end);
    return !*end && d >= 0;
}

static void check_contrast(const struct setu_theme *t, int fg, int bg)
{
    int c = setu_contrast100(t->color[fg], t->color[bg]);
    if (c < 450)
        ERR(L"contrast of %hs on %hs is %d.%02d:1, below 4.5:1", setu_color_key[fg], setu_color_key[bg], c / 100, c % 100);
}

static int check_pack(const WCHAR *dir_in)
{
    struct setu_theme t;
    WCHAR dir[MAX_PATH], err[512], p[MAX_PATH];
    struct setu_kv *kv = NULL;
    int nkv = 0, bad = 0, badline = 0, i;
    const WCHAR *id;
    DWORD attrs;

    if (!GetFullPathNameW(dir_in, ARRAYSIZE(dir), dir, NULL)) lstrcpynW(dir, dir_in, ARRAYSIZE(dir));
    while (wcslen(dir) > 3 && (dir[wcslen(dir) - 1] == L'\\' || dir[wcslen(dir) - 1] == L'/')) dir[wcslen(dir) - 1] = 0;
    attrs = GetFileAttributesW(dir);
    if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY))
    {
        ERR(L"%ls is not a folder", dir);
        return 2;
    }
    setu_theme_load(&t, NULL, dir, err, ARRAYSIZE(err));
    if (err[0]) ERR(L"%ls", err);

    /* the pack's own theme.ini, key by key */
    _snwprintf(p, ARRAYSIZE(p), L"%ls\\theme.ini", dir);
    p[MAX_PATH - 1] = 0;
    {
        const WCHAR *tok = setu_theme_get(&t, L"theme:Setu", L"Tokens");
        if (tok && tok[0] && setu_theme_level(&t, L"theme:Setu", L"Tokens") == 0)
        {
            _snwprintf(p, ARRAYSIZE(p), L"%ls\\%ls", dir, tok);
            p[MAX_PATH - 1] = 0;
        }
    }
    if (!setu_ini_parse(p, L"", &kv, &nkv, 0, &bad, &badline)) ERR(L"%ls: missing or unreadable", p);
    for (i = 0; i < nkv; i++)
    {
        const struct setu_kv *e = &kv[i];
        if (!lstrcmpiW(e->section, L"Colors") || (!lstrcmpiW(e->section, L"Boot") && !lstrcmpiW(e->key, L"background")))
        {
            if (!setu_parse_color(e->value, NULL, NULL))
                ERR(L"[%ls] %ls=%ls: not a #RRGGBB or #RRGGBBAA colour", e->section, e->key, e->value);
        }
        else if (!lstrcmpiW(e->section, L"Metrics") || (!lstrcmpiW(e->section, L"Shell") && wcsncmp(e->key, L"start-", 6) == 0) ||
                 (!lstrcmpiW(e->section, L"Cursor") && !lstrcmpiW(e->key, L"size")) ||
                 (!lstrcmpiW(e->section, L"Fonts") && (wcsstr(e->key, L"-size") || !lstrcmpiW(e->key, L"caption-weight"))))
        {
            if (!is_number(e->value)) ERR(L"[%ls] %ls=%ls: not a number >= 0", e->section, e->key, e->value);
        }
        else if (!lstrcmpiW(e->section, L"Fonts") && !lstrcmpiW(e->key, L"smoothing"))
        {
            if (lstrcmpiW(e->value, L"none") && lstrcmpiW(e->value, L"grayscale") && lstrcmpiW(e->value, L"rgb"))
                ERR(L"[Fonts] smoothing=%ls: not none, grayscale or rgb", e->value);
        }
        else if (!lstrcmpiW(e->section, L"Shell") && !lstrcmpiW(e->key, L"taskbar-alignment"))
        {
            if (lstrcmpiW(e->value, L"center") && lstrcmpiW(e->value, L"left"))
                ERR(L"[Shell] taskbar-alignment=%ls: not center or left", e->value);
        }
        else if (!lstrcmpiW(e->section, L"Theme") && !lstrcmpiW(e->key, L"Mode"))
        {
            if (lstrcmpiW(e->value, L"light") && lstrcmpiW(e->value, L"dark")) ERR(L"[Theme] Mode=%ls: not light or dark", e->value);
        }
        else if (!lstrcmpiW(e->section, L"Boot") && !lstrcmpiW(e->key, L"logo") && e->value[0])
        {
            WCHAR f[MAX_PATH];
            if (!pack_path(&t, e->value, f, ARRAYSIZE(f))) ERR(L"[Boot] logo=%ls: no such file", e->value);
        }
    }
    free(kv);
    id = setu_theme_get(&t, L"Theme", L"Id");
    if (setu_theme_level(&t, L"Theme", L"Id") != 0) WARN(L"[Theme] Id is not set");
    else
    {
        const WCHAR *slash = wcsrchr(dir, L'\\');
        if (lstrcmpW(id, slash ? slash + 1 : dir)) WARN(L"[Theme] Id=%ls but the folder is %ls", id, slash ? slash + 1 : dir);
    }

    /* icons: every file the pack's index.ini names */
    for (i = 0; i < t.nicons; i++)
    {
        const struct setu_kv *e = &t.icons[i];
        HICON h;
        if (e->level != 0) continue;
        if (GetFileAttributesW(e->value) == INVALID_FILE_ATTRIBUTES)
        {
            ERR(L"icon %ls: no such file %ls", e->key, e->value);
            continue;
        }
        if (!(h = (HICON)LoadImageW(NULL, e->value, IMAGE_ICON, 32, 32, LR_LOADFROMFILE)))
            ERR(L"icon %ls: %ls is not an icon", e->key, e->value);
        else DestroyIcon(h);
    }

    /* the .theme */
    {
        static const struct { const WCHAR *section, *key; } paths[] = {
            { L"theme:VisualStyles", L"Path" },
            { L"theme:Control Panel\\Desktop", L"Wallpaper" },
        };
        for (i = 0; i < (int)ARRAYSIZE(paths); i++)
        {
            const WCHAR *v = setu_theme_get(&t, paths[i].section, paths[i].key);
            WCHAR f[MAX_PATH];
            if (v && v[0] && setu_theme_level(&t, paths[i].section, paths[i].key) == 0 && !pack_path(&t, v, f, ARRAYSIZE(f)))
                ERR(L"[%ls] %ls=%ls: no such file", paths[i].section + 6, paths[i].key, v);
        }
        for (i = 0; i < t.nkv; i++)
        {
            COLORREF c;
            if (t.kv[i].level == 0 && !lstrcmpiW(t.kv[i].section, L"theme:Control Panel\\Colors") && !parse_rgb(t.kv[i].value, &c))
                ERR(L"[Control Panel\\Colors] %ls=%ls: not \"R G B\"", t.kv[i].key, t.kv[i].value);
        }
    }

    check_contrast(&t, TC_TEXT, TC_SURFACE);
    check_contrast(&t, TC_ACCENT_TEXT, TC_ACCENT);
    if (problems) out(L"FAIL: %ls: %d error(s), %d warning(s)\n", dir, problems, warnings);
    else out(L"ok: %ls (%ls, %d warning(s))\n", dir, t.id, warnings);
    setu_theme_free(&t);
    return problems ? 2 : 0;
}

/* ------------------------------------------------------------------ */
/* apply                                                                */

static BOOL step_failed;

static void reg_sz(HKEY root, const WCHAR *key, const WCHAR *name, const WCHAR *value)
{
    LSTATUS r = RegSetKeyValueW(root, key, name, REG_SZ, value, (DWORD)((wcslen(value) + 1) * sizeof(WCHAR)));
    if (r)
    {
        WARN(L"cannot write %ls\\%ls (error %ld)", key, name, (long)r);
        step_failed = TRUE;
    }
}

static int CALLBACK face_found(const LOGFONTW *lf, const TEXTMETRICW *tm, DWORD type, LPARAM found)
{
    (void)lf; (void)tm; (void)type;
    *(BOOL *)found = TRUE;
    return 0;
}

/* the first face of LIST that's installed, else the first one */
static void first_face(const WCHAR *list, WCHAR *face)
{
    const WCHAR *p = list;
    face[0] = 0;
    while (*p)
    {
        WCHAR f[LF_FACESIZE];
        LOGFONTW lf;
        BOOL found = FALSE;
        HDC dc;
        int n = 0;
        while (*p == L' ' || *p == L',') p++;
        while (p[n] && p[n] != L',') n++;
        if (!n) break;
        lstrcpynW(f, p, min(n + 1, LF_FACESIZE));
        p += n;
        while (f[0] && f[wcslen(f) - 1] == L' ') f[wcslen(f) - 1] = 0;
        if (!face[0]) lstrcpyW(face, f);
        memset(&lf, 0, sizeof(lf));
        lf.lfCharSet = DEFAULT_CHARSET;
        lstrcpyW(lf.lfFaceName, f);
        dc = GetDC(NULL);
        EnumFontFamiliesExW(dc, &lf, face_found, (LPARAM)&found, 0);
        ReleaseDC(NULL, dc);
        if (found) { lstrcpyW(face, f); return; }
    }
}

/* Every [Colors] and [Metrics] key of the resolved pack, for the window
 * frames (theme-fork): HKCU\Software\Setu\Theme\Colors (REG_SZ "#RRGGBB"
 * or "#RRGGBBAA") and \Metrics (REG_DWORD, pixels at 96 DPI). Keys the
 * tokens don't know are written too, so new frame tokens need no change
 * here; the previous pack's values are cleared first. */
static void write_tokens(const struct setu_theme *t)
{
    static const WCHAR *const keys[] = { L"Software\\Setu\\Theme\\Colors", L"Software\\Setu\\Theme\\Metrics" };
    static const WCHAR *const sections[] = { L"Colors", L"Metrics" };
    int k, i;

    for (k = 0; k < 2; k++)
    {
        HKEY h;
        RegDeleteKeyW(HKEY_CURRENT_USER, keys[k]);
        if (RegCreateKeyExW(HKEY_CURRENT_USER, keys[k], 0, NULL, 0, KEY_SET_VALUE, NULL, &h, NULL))
        {
            WARN(L"cannot create %ls", keys[k]);
            step_failed = TRUE;
            continue;
        }
        /* the built-in tokens, as resolved */
        if (!k)
            for (i = 0; i < TC_COUNT; i++)
            {
                WCHAR name[64], val[16];
                MultiByteToWideChar(CP_UTF8, 0, setu_color_key[i], -1, name, ARRAYSIZE(name));
                if (t->alpha[i] == 255)
                    _snwprintf(val, ARRAYSIZE(val), L"#%02X%02X%02X", GetRValue(t->color[i]), GetGValue(t->color[i]),
                               GetBValue(t->color[i]));
                else
                    _snwprintf(val, ARRAYSIZE(val), L"#%02X%02X%02X%02X", GetRValue(t->color[i]),
                               GetGValue(t->color[i]), GetBValue(t->color[i]), t->alpha[i]);
                RegSetValueExW(h, name, 0, REG_SZ, (const BYTE *)val, (DWORD)((wcslen(val) + 1) * sizeof(WCHAR)));
            }
        else
            for (i = 0; i < TM_COUNT; i++)
            {
                WCHAR name[64];
                DWORD d = (DWORD)t->metric[i];
                MultiByteToWideChar(CP_UTF8, 0, setu_metric_key[i], -1, name, ARRAYSIZE(name));
                RegSetValueExW(h, name, 0, REG_DWORD, (const BYTE *)&d, sizeof(d));
            }
        /* keys of the pack chain the tokens don't know (closest pack wins) */
        for (i = 0; i < t->nkv; i++)
        {
            const struct setu_kv *e = &t->kv[i];
            const WCHAR *v;
            WCHAR *end;
            if (lstrcmpiW(e->section, sections[k])) continue;
            if (setu_theme_level(t, e->section, e->key) != e->level) continue;
            v = setu_theme_get(t, e->section, e->key);
            if (!k)
            {
                if (!setu_parse_color(v, NULL, NULL) || RegQueryValueExW(h, e->key, NULL, NULL, NULL, NULL) == ERROR_SUCCESS)
                    continue;
                RegSetValueExW(h, e->key, 0, REG_SZ, (const BYTE *)v, (DWORD)((wcslen(v) + 1) * sizeof(WCHAR)));
            }
            else
            {
                double d = wcstod(v, &end);
                DWORD dw;
                if (end == v || *end || d < 0) continue;
                if (RegQueryValueExW(h, e->key, NULL, NULL, NULL, NULL) == ERROR_SUCCESS) continue;
                dw = (DWORD)(d + 0.5);
                RegSetValueExW(h, e->key, 0, REG_DWORD, (const BYTE *)&dw, sizeof(dw));
            }
        }
        RegCloseKey(h);
    }
}

/* Is the current wallpaper unset or one of Setu's (a theme pack's, Setu's
 * wallpaper collection, or one we converted)? Then a login may replace it. */
static BOOL wallpaper_is_setus(void)
{
    WCHAR cur[MAX_PATH] = L"", la[MAX_PATH], mine[MAX_PATH];
    DWORD size = sizeof(cur), n;
    if (RegGetValueW(HKEY_CURRENT_USER, L"Control Panel\\Desktop", L"Wallpaper", RRF_RT_REG_SZ, NULL, cur, &size) ||
        !cur[0])
        return TRUE;
    if (StrStrIW(cur, L"\\Resources\\Themes\\") || StrStrIW(cur, L"\\Web\\Wallpaper\\Setu\\") ||
        StrStrIW(cur, L"\\setu\\wallpapers\\") || StrStrIW(cur, L"/setu/wallpapers/"))
        return TRUE;
    n = GetEnvironmentVariableW(L"LOCALAPPDATA", la, ARRAYSIZE(la));
    if (n && n < ARRAYSIZE(la))
    {
        _snwprintf(mine, ARRAYSIZE(mine), L"%ls\\Setu\\Wallpaper\\", la);
        mine[MAX_PATH - 1] = 0;
        if (!StrCmpNIW(cur, mine, lstrlenW(mine))) return TRUE;
    }
    return FALSE;
}

/* The desktop draws bitmaps only: a wallpaper in another format (JPEG,
 * PNG, ...) is converted with WIC to %LOCALAPPDATA%\Setu\Wallpaper\<name>.bmp,
 * and PATH becomes that file. */
static BOOL to_bmp(WCHAR *path, int len)
{
    IWICImagingFactory *factory = NULL;
    IWICBitmapDecoder *dec = NULL;
    IWICBitmapFrameDecode *frame = NULL;
    IWICFormatConverter *conv = NULL;
    IWICBitmapEncoder *enc = NULL;
    IWICBitmapFrameEncode *fenc = NULL;
    IWICStream *stream = NULL;
    WCHAR la[MAX_PATH], dir[MAX_PATH], out_path[MAX_PATH], name[MAX_PATH];
    const WCHAR *ext = PathFindExtensionW(path);
    UINT w = 0, h = 0;
    WICPixelFormatGUID fmt = GUID_WICPixelFormat24bppBGR;
    HRESULT hr, init;
    DWORD n;

    if (!lstrcmpiW(ext, L".bmp") || !lstrcmpiW(ext, L".dib")) return TRUE;
    n = GetEnvironmentVariableW(L"LOCALAPPDATA", la, ARRAYSIZE(la));
    if (!n || n >= ARRAYSIZE(la)) return FALSE;
    _snwprintf(dir, ARRAYSIZE(dir), L"%ls\\Setu\\Wallpaper", la);
    dir[MAX_PATH - 1] = 0;
    SHCreateDirectoryExW(NULL, dir, NULL);
    lstrcpynW(name, PathFindFileNameW(path), ARRAYSIZE(name));
    PathRemoveExtensionW(name);
    _snwprintf(out_path, ARRAYSIZE(out_path), L"%ls\\%ls.bmp", dir, name);
    out_path[MAX_PATH - 1] = 0;

    init = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory,
                          (void **)&factory);
    if (SUCCEEDED(hr))
        hr = factory->lpVtbl->CreateDecoderFromFilename(factory, path, NULL, GENERIC_READ,
                                                         WICDecodeMetadataCacheOnDemand, &dec);
    if (SUCCEEDED(hr)) hr = dec->lpVtbl->GetFrame(dec, 0, &frame);
    if (SUCCEEDED(hr)) hr = factory->lpVtbl->CreateFormatConverter(factory, &conv);
    if (SUCCEEDED(hr))
        hr = conv->lpVtbl->Initialize(conv, (IWICBitmapSource *)frame, &GUID_WICPixelFormat24bppBGR,
                                      WICBitmapDitherTypeNone, NULL, 0.0, WICBitmapPaletteTypeCustom);
    if (SUCCEEDED(hr)) hr = conv->lpVtbl->GetSize(conv, &w, &h);
    if (SUCCEEDED(hr)) hr = factory->lpVtbl->CreateStream(factory, &stream);
    if (SUCCEEDED(hr)) hr = stream->lpVtbl->InitializeFromFilename(stream, out_path, GENERIC_WRITE);
    if (SUCCEEDED(hr)) hr = factory->lpVtbl->CreateEncoder(factory, &GUID_ContainerFormatBmp, NULL, &enc);
    if (SUCCEEDED(hr)) hr = enc->lpVtbl->Initialize(enc, (IStream *)stream, WICBitmapEncoderNoCache);
    if (SUCCEEDED(hr)) hr = enc->lpVtbl->CreateNewFrame(enc, &fenc, NULL);
    if (SUCCEEDED(hr)) hr = fenc->lpVtbl->Initialize(fenc, NULL);
    if (SUCCEEDED(hr)) hr = fenc->lpVtbl->SetSize(fenc, w, h);
    if (SUCCEEDED(hr)) hr = fenc->lpVtbl->SetPixelFormat(fenc, &fmt);
    if (SUCCEEDED(hr)) hr = fenc->lpVtbl->WriteSource(fenc, (IWICBitmapSource *)conv, NULL);
    if (SUCCEEDED(hr)) hr = fenc->lpVtbl->Commit(fenc);
    if (SUCCEEDED(hr)) hr = enc->lpVtbl->Commit(enc);
    if (fenc) fenc->lpVtbl->Release(fenc);
    if (enc) enc->lpVtbl->Release(enc);
    if (stream) stream->lpVtbl->Release(stream);
    if (conv) conv->lpVtbl->Release(conv);
    if (frame) frame->lpVtbl->Release(frame);
    if (dec) dec->lpVtbl->Release(dec);
    if (factory) factory->lpVtbl->Release(factory);
    if (SUCCEEDED(init)) CoUninitialize();
    if (FAILED(hr)) return FALSE;
    lstrcpynW(path, out_path, len);
    return TRUE;
}

static int cmd_apply(const WCHAR *id, BOOL login)
{
    struct setu_theme t;
    WCHAR err[512], path[MAX_PATH];
    const WCHAR *v;
    UINT spi = SPIF_UPDATEINIFILE | (login ? 0 : SPIF_SENDCHANGE);
    BOOL found;
    int i;

    found = setu_theme_load(&t, id, NULL, err, ARRAYSIZE(err));
    if (!found && lstrcmpiW(id, L"setu-light"))
    {
        out(L"error: %ls\n", err);
        setu_theme_free(&t);
        return 2;
    }
    if (!found) out(L"note: setu-light is not installed; using the built-in setu-light values\n");
    else if (err[0]) WARN(L"%ls", err);

    /* 1. the active pack */
    reg_sz(HKEY_CURRENT_USER, L"Software\\Setu\\Theme", L"Current", t.id);

    /* 2. the control theme (msstyles): before the colours (theme-fork) */
    v = setu_theme_get(&t, L"theme:VisualStyles", L"Path");
    if (v && v[0])
    {
        if (pack_path(&t, v, path, ARRAYSIZE(path)))
        {
            const WCHAR *key = L"Software\\Microsoft\\Windows\\CurrentVersion\\ThemeManager";
            const WCHAR *color = setu_theme_get(&t, L"theme:VisualStyles", L"ColorStyle");
            const WCHAR *size = setu_theme_get(&t, L"theme:VisualStyles", L"Size");
            reg_sz(HKEY_CURRENT_USER, key, L"DllName", path);
            reg_sz(HKEY_CURRENT_USER, key, L"ColorName", color && color[0] ? color : L"NormalColor");
            reg_sz(HKEY_CURRENT_USER, key, L"SizeName", size && size[0] ? size : L"NormalSize");
            reg_sz(HKEY_CURRENT_USER, key, L"ThemeActive", L"1");
        }
        else WARN(L"control theme %ls not found; keeping the current one", v);
    }

    /* 3. every token, for the window frames (HKCU\Software\Setu\Theme\Colors
     * and \Metrics), then the system colours */
    write_tokens(&t);
    {
        INT idx[ARRAYSIZE(sys_colors)];
        COLORREF val[ARRAYSIZE(sys_colors)];
        for (i = 0; i < (int)ARRAYSIZE(sys_colors); i++)
        {
            WCHAR rgb[16];
            idx[i] = sys_colors[i].index;
            val[i] = pack_sys_color(&t, i);
            _snwprintf(rgb, ARRAYSIZE(rgb), L"%d %d %d", GetRValue(val[i]), GetGValue(val[i]), GetBValue(val[i]));
            reg_sz(HKEY_CURRENT_USER, L"Control Panel\\Colors", sys_colors[i].name, rgb);
        }
        if (!login && !SetSysColors(ARRAYSIZE(sys_colors), idx, val))
        {
            WARN(L"SetSysColors failed (error %lu)", GetLastError());
            step_failed = TRUE;
        }
    }

    /* 4. wallpaper: the .theme's, else the pack's wallpaper\wallpaper.bmp */
    v = setu_theme_get(&t, L"theme:Control Panel\\Desktop", L"Wallpaper");
    path[0] = 0;
    if (v && v[0]) { if (!pack_path(&t, v, path, ARRAYSIZE(path))) { WARN(L"wallpaper %ls not found", v); path[0] = 0; } }
    else
    {
        for (i = 0; i < t.levels && !path[0]; i++)
        {
            _snwprintf(path, ARRAYSIZE(path), L"%ls\\wallpaper\\wallpaper.bmp", t.chain[i]);
            path[MAX_PATH - 1] = 0;
            if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) path[0] = 0;
        }
    }
    if (path[0] && login && !wallpaper_is_setus())
    {
        out(L"note: keeping the wallpaper the user chose\n");
        path[0] = 0;
    }
    if (path[0] && !to_bmp(path, ARRAYSIZE(path)))
    {
        WARN(L"cannot convert the wallpaper %ls to a bitmap", path);
        path[0] = 0;
    }
    if (path[0])
    {
        const WCHAR *style = setu_theme_get(&t, L"theme:Control Panel\\Desktop", L"WallpaperStyle");
        const WCHAR *tile = setu_theme_get(&t, L"theme:Control Panel\\Desktop", L"TileWallpaper");
        reg_sz(HKEY_CURRENT_USER, L"Control Panel\\Desktop", L"WallpaperStyle", style && style[0] ? style : L"10");
        reg_sz(HKEY_CURRENT_USER, L"Control Panel\\Desktop", L"TileWallpaper", tile && tile[0] ? tile : L"0");
        if (!SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, path, spi))
        {
            WARN(L"SPI_SETDESKWALLPAPER failed (error %lu)", GetLastError());
            step_failed = TRUE;
        }
    }

    /* 5. fonts */
    {
        static const WCHAR *const ui_names[] = { L"MS Shell Dlg", L"MS Shell Dlg 2", L"Tahoma", L"MS Sans Serif",
                                                 L"Microsoft Sans Serif", L"Segoe UI" };
        WCHAR face[LF_FACESIZE];
        first_face(t.font_ui, face);
        for (i = 0; face[0] && i < (int)ARRAYSIZE(ui_names); i++)
            reg_sz(HKEY_CURRENT_USER, L"Software\\Wine\\Fonts\\Overrides", ui_names[i], face);
        SystemParametersInfoW(SPI_SETFONTSMOOTHING, t.smoothing != SMOOTH_NONE, NULL, spi);
        if (t.smoothing != SMOOTH_NONE)
            SystemParametersInfoW(SPI_SETFONTSMOOTHINGTYPE, 0, (void *)(UINT_PTR)FE_FONTSMOOTHINGCLEARTYPE, spi);
        if (t.smoothing == SMOOTH_RGB)
            SystemParametersInfoW(SPI_SETFONTSMOOTHINGORIENTATION, 0, (void *)(UINT_PTR)FE_FONTSMOOTHINGORIENTATIONRGB, spi);
        else /* no orientation: ClearType text is drawn in grayscale (setu-session) */
            RegDeleteKeyValueW(HKEY_CURRENT_USER, L"Control Panel\\Desktop", L"FontSmoothingOrientation");
    }

    /* 6. shell icons for other programs */
    {
        static const struct { int index; int name; } shell_icons[] = {
            { 0, TI_FILE },               /* SIID_DOCNOASSOC */
            { 3, TI_FOLDER },             /* SIID_FOLDER */
            { 4, TI_FOLDER_OPEN },        /* SIID_FOLDEROPEN */
            { 7, TI_DRIVE_REMOVABLE },    /* SIID_DRIVEREMOVE */
            { 8, TI_DRIVE_FIXED },        /* SIID_DRIVEFIXED */
            { 11, TI_DRIVE_OPTICAL },     /* SIID_DRIVECD */
            { 15, TI_COMPUTER },          /* the computer */
            { 31, TI_RECYCLE_BIN_EMPTY }, /* SIID_RECYCLER */
            { 32, TI_RECYCLE_BIN_FULL },  /* SIID_RECYCLERFULL */
        };
        const WCHAR *key = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Shell Icons";
        for (i = 0; i < (int)ARRAYSIZE(shell_icons); i++)
        {
            WCHAR name[8], val[MAX_PATH + 8];
            _snwprintf(name, ARRAYSIZE(name), L"%d", shell_icons[i].index);
            if (t.icon_file[shell_icons[i].name][0])
            {
                _snwprintf(val, ARRAYSIZE(val), L"%ls,0", t.icon_file[shell_icons[i].name]);
                val[ARRAYSIZE(val) - 1] = 0;
                reg_sz(HKEY_LOCAL_MACHINE, key, name, val);
            }
            else RegDeleteKeyValueW(HKEY_LOCAL_MACHINE, key, name);
        }
        if (!login) SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
    }

    /* 7. tell running programs */
    if (!login)
    {
        static const WCHAR *const areas[] = { L"ImmersiveColorSet", L"SetuTheme" };
        DWORD_PTR res;
        SendMessageTimeoutW(HWND_BROADCAST, WM_THEMECHANGED, 0, 0, SMTO_ABORTIFHUNG, 2000, &res);
        for (i = 0; i < (int)ARRAYSIZE(areas); i++)
            SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)areas[i], SMTO_ABORTIFHUNG, 2000, &res);
        out(L"note: the cursor theme (%ls), the window manager theme and the boot splash follow at the next login\n",
            (v = setu_theme_get(&t, L"Cursor", L"theme")) ? v : L"");
    }
    out(L"applied %ls (%ls)\n", t.id,
        !found ? L"built-in" : t.where == THEME_USER ? t.dir : t.dir);
    setu_theme_free(&t);
    return step_failed ? 3 : 0;
}

/* ------------------------------------------------------------------ */

static int usage(void)
{
    out(L"usage: setu-theme list\n"
        L"       setu-theme current\n"
        L"       setu-theme apply [--login] <id>\n"
        L"       setu-theme check <dir>\n");
    return 1;
}

int wmain(int argc, WCHAR **argv)
{
    if (argc < 2) return usage();
    if (!wcscmp(argv[1], L"list") && argc == 2) return cmd_list();
    if (!wcscmp(argv[1], L"current") && argc == 2) return cmd_current();
    if (!wcscmp(argv[1], L"check") && argc == 3) return check_pack(argv[2]);
    if (!wcscmp(argv[1], L"apply"))
    {
        if (argc == 3 && argv[2][0] != L'-') return cmd_apply(argv[2], FALSE);
        if (argc == 4 && !wcscmp(argv[2], L"--login")) return cmd_apply(argv[3], TRUE);
        if (argc == 4 && !wcscmp(argv[3], L"--login")) return cmd_apply(argv[2], TRUE);
    }
    return usage();
}
