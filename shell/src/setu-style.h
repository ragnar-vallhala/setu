/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * setu-style.h: Setu's design tokens and drawing helpers, shared by the
 * shell (setu-shell.c) and the file manager (setu-files.c).
 *
 * Every colour, metric, font and icon comes from the active theme pack
 * (setu-theme.c, docs/THEMES.md); the built-in values are setu-light's,
 * from docs/DESIGN.md. Code reads colours through the ST pointer
 * (struct setu_colors, filled by st_theme_reload()), metrics through the
 * ST_* macros and icons by name (TI_*, distro/themes/ICONS.md), so a
 * pack switch only needs st_theme_reload() and a repaint.
 *
 * Drawing: GDI's RoundRect and pens are aliased, so the rounded shapes,
 * shadows and line icons here are drawn by hand into a 32-bit DIB section
 * (struct setu_canvas), with anti-aliased edges from a signed distance
 * function. Text still goes through GDI (DrawTextW) on the same DIB.
 * Icons: the pack's .ico for the name when it has one (monochrome ones are
 * recoloured like glyphs, others drawn as they are), else Lucide's (ISC;
 * shell/icons/, NOTICE), flattened into stroke segments by
 * shell/icons/gen-icons.py (setu-icons.h) and rasterized here at the size
 * asked for, so they stay crisp at any DPI.
 *
 * Fonts: the pack's [Fonts] ui list (Inter, SIL OFL 1.1, in setu-light):
 * the first installed face, else the system's message font
 * (SystemParametersInfoW SPI_GETNONCLIENTMETRICS), so the programs still
 * work in a prefix without it.
 *
 * References (learn.microsoft.com): CreateDIBSection, GdiFlush,
 * EnumFontFamiliesExW, CreateFontIndirectW (LOGFONTW), DrawTextW, LoadImageW,
 * DrawIconEx.
 */
#ifndef SETU_STYLE_H
#define SETU_STYLE_H

#include <windows.h>
#include <math.h>
#include "setu-icons.h"
#include "setu-theme.h"

/* ------------------------------------------------------------------ */
/* Colours                                                              */

/* The active pack's colours (setu-theme.h; the built-in values are
 * setu-light's, docs/DESIGN.md). Filled by st_theme_reload(). */
struct setu_colors
{
    COLORREF accent, accent_hover, accent_pressed, accent_text;
    COLORREF window_bg, surface, surface_hover;
    COLORREF border, border_subtle;
    COLORREF text, text_secondary, text_disabled;
    COLORREF titlebar_active, titlebar_inactive, titlebar_text, close_hover;
    COLORREF taskbar, taskbar_text, menu;
    COLORREF selection, selection_border, shadow, desktop;
    COLORREF logo;          /* the Setu bridge mark */
    COLORREF error;         /* error text */
    COLORREF tile[6];       /* lettered tiles of apps without an icon */
    BYTE selection_alpha, shadow_alpha;
};

static struct setu_colors st_colors;
static const struct setu_colors *ST = &st_colors;

/* A mixed with B; T is B's share, 0 to 255. */
static inline COLORREF st_mix(COLORREF a, COLORREF b, int t)
{
    return RGB((GetRValue(a) * (255 - t) + GetRValue(b) * t) / 255,
               (GetGValue(a) * (255 - t) + GetGValue(b) * t) / 255,
               (GetBValue(a) * (255 - t) + GetBValue(b) * t) / 255);
}

/* Selection: the selection token (accent at 20 %) over the surface, with a
 * 1 px selection-border outline. */
static inline COLORREF st_selection(void) { return st_mix(ST->surface, ST->selection, ST->selection_alpha); }
/* Selection while the control doesn't have the focus: half as strong. */
static inline COLORREF st_selection_inactive(void)
{
    return st_mix(ST->surface, ST->selection, (ST->selection_alpha + 1) / 2);
}

/* ------------------------------------------------------------------ */
/* Shape and spacing (pixels at 96 DPI, from the pack's [Metrics])       */

#define ST_RADIUS_WINDOW   (setu_theme.metric[TM_RADIUS_WINDOW])   /* windows, menus, popups, start menu */
#define ST_RADIUS_CONTROL  (setu_theme.metric[TM_RADIUS_CONTROL])  /* buttons, edits, combo boxes */
#define ST_RADIUS_CHECK    (setu_theme.metric[TM_RADIUS_CHECK])
#define ST_CONTROL_H       (setu_theme.metric[TM_CONTROL_HEIGHT])
#define ST_ROW_H           (setu_theme.metric[TM_ROW_HEIGHT])      /* list rows */
#define ST_MENU_ITEM_H     (setu_theme.metric[TM_MENU_ITEM_HEIGHT])
#define ST_TASKBAR_H       (setu_theme.metric[TM_TASKBAR_HEIGHT])
#define ST_TASK_BUTTON     (setu_theme.metric[TM_TASKBAR_BUTTON])  /* taskbar buttons are square */
#define ST_TASK_ICON       (setu_theme.metric[TM_TASKBAR_ICON])
#define ST_PILL_H          (setu_theme.metric[TM_TASKBAR_PILL])
#define ST_SHADOW_BLUR     (setu_theme.metric[TM_SHADOW_BLUR])
#define ST_FOCUS_W         (setu_theme.metric[TM_FOCUS_WIDTH])

/* ------------------------------------------------------------------ */
/* Fonts                                                                */

enum setu_font
{
    SF_BODY,      /* [Fonts] ui-size (9 pt) */
    SF_CAPTION,   /* ui-size, caption-weight (semibold) */
    SF_TITLE,     /* title-size (10.5 pt), caption-weight */
    SF_LARGE,     /* header-size (14 pt), caption-weight */
    SF_COUNT
};

static HFONT st_font[SF_COUNT];
static WCHAR st_face[LF_FACESIZE], st_face_bold[LF_FACESIZE];
static WCHAR st_faces_for[LF_FACESIZE * 4 + 8];

static int CALLBACK st_font_found(const LOGFONTW *lf, const TEXTMETRICW *tm, DWORD type, LPARAM found)
{
    (void)lf; (void)tm; (void)type;
    *(BOOL *)found = TRUE;
    return 0;
}

static inline BOOL st_have_face(const WCHAR *face)
{
    LOGFONTW lf;
    BOOL found = FALSE;
    HDC dc = GetDC(NULL);
    memset(&lf, 0, sizeof(lf));
    lf.lfCharSet = DEFAULT_CHARSET;
    lstrcpynW(lf.lfFaceName, face, LF_FACESIZE);
    EnumFontFamiliesExW(dc, &lf, st_font_found, (LPARAM)&found, 0);
    ReleaseDC(NULL, dc);
    return found;
}

/* The first installed face of the pack's [Fonts] ui list, else the system's
 * message font (SPI_GETNONCLIENTMETRICS); and its bold face: static fonts
 * such as Inter's name each weight as its own family ("Inter SemiBold"). */
static inline void st_pick_faces(void)
{
    const WCHAR *list = setu_theme.font_ui, *p = list;
    WCHAR key[ARRAYSIZE(st_faces_for)];

    _snwprintf(key, ARRAYSIZE(key), L"%ls|%d", list, setu_theme.caption_weight);
    key[ARRAYSIZE(key) - 1] = 0;
    if (st_face[0] && !wcscmp(key, st_faces_for)) return;
    lstrcpyW(st_faces_for, key);
    st_face[0] = st_face_bold[0] = 0;
    while (*p && !st_face[0])
    {
        WCHAR face[LF_FACESIZE];
        int n = 0;
        while (*p == L' ' || *p == L',') p++;
        while (p[n] && p[n] != L',') n++;
        if (!n) break;
        lstrcpynW(face, p, min(n + 1, LF_FACESIZE));
        p += n;
        while (n && face[0] && face[wcslen(face) - 1] == L' ') face[wcslen(face) - 1] = 0;
        if (face[0] && st_have_face(face)) lstrcpyW(st_face, face);
    }
    if (!st_face[0])
    {
        NONCLIENTMETRICSW ncm;
        memset(&ncm, 0, sizeof(ncm));
        ncm.cbSize = sizeof(ncm);
        if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
            lstrcpynW(st_face, ncm.lfMessageFont.lfFaceName, LF_FACESIZE);
        if (!st_face[0]) lstrcpyW(st_face, L"MS Shell Dlg 2");
    }
    {
        static const WCHAR *const names[] = { L"Thin", L"ExtraLight", L"Light", L"", L"Medium", L"SemiBold",
                                              L"Bold", L"ExtraBold", L"Black" };
        int w = setu_theme.caption_weight / 100 - 1;
        WCHAR bold[LF_FACESIZE];
        if (w >= 0 && w < (int)ARRAYSIZE(names) && names[w][0])
        {
            _snwprintf(bold, LF_FACESIZE, L"%ls %ls", st_face, names[w]);
            bold[LF_FACESIZE - 1] = 0;
            if (st_have_face(bold)) lstrcpyW(st_face_bold, bold);
        }
    }
}

/* (Re)create the fonts for DPI. */
static inline void st_make_fonts(UINT dpi)
{
    int tenths[SF_COUNT], i;
    int quality = setu_theme.smoothing == SMOOTH_NONE ? NONANTIALIASED_QUALITY :
                  setu_theme.smoothing == SMOOTH_RGB ? CLEARTYPE_QUALITY : ANTIALIASED_QUALITY;

    tenths[SF_BODY] = tenths[SF_CAPTION] = setu_theme.ui_size10;
    tenths[SF_TITLE] = setu_theme.title_size10;
    tenths[SF_LARGE] = setu_theme.header_size10;
    st_pick_faces();
    for (i = 0; i < SF_COUNT; i++)
    {
        LOGFONTW lf;
        BOOL heavy = i != SF_BODY;
        memset(&lf, 0, sizeof(lf));
        lf.lfHeight = -MulDiv(tenths[i], (int)dpi, 720);
        lf.lfWeight = heavy ? setu_theme.caption_weight : FW_NORMAL;
        lf.lfCharSet = DEFAULT_CHARSET;
        lf.lfQuality = quality;
        lstrcpynW(lf.lfFaceName, heavy && st_face_bold[0] ? st_face_bold : st_face, LF_FACESIZE);
        if (st_font[i]) DeleteObject(st_font[i]);
        st_font[i] = CreateFontIndirectW(&lf);
    }
}

/* ------------------------------------------------------------------ */
/* Canvas: a 32-bit top-down DIB section with a DC                      */

struct setu_canvas
{
    HDC dc;
    HBITMAP bmp;
    HGDIOBJ old;
    DWORD *bits;   /* 0x00RRGGBB, top-down */
    int w, h;
    RECT clip;     /* what the hand-drawn shapes may touch */
};

static inline BOOL st_canvas_begin(struct setu_canvas *cv, int w, int h)
{
    BITMAPINFO bi;
    memset(cv, 0, sizeof(*cv));
    memset(&bi, 0, sizeof(bi));
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    cv->dc = CreateCompatibleDC(NULL);
    if (!cv->dc) return FALSE;
    cv->bmp = CreateDIBSection(cv->dc, &bi, DIB_RGB_COLORS, (void **)&cv->bits, NULL, 0);
    if (!cv->bmp) { DeleteDC(cv->dc); cv->dc = NULL; return FALSE; }
    cv->old = SelectObject(cv->dc, cv->bmp);
    cv->w = w;
    cv->h = h;
    SetRect(&cv->clip, 0, 0, w, h);
    SetBkMode(cv->dc, TRANSPARENT);
    return TRUE;
}

/* Limit drawing (shapes, icons and GDI) to R, or lift the limit (NULL). */
static inline void st_clip(struct setu_canvas *cv, const RECT *r)
{
    RECT all = { 0, 0, cv->w, cv->h };
    if (r) IntersectRect(&cv->clip, r, &all);
    else cv->clip = all;
    if (r)
    {
        HRGN rgn = CreateRectRgnIndirect(&cv->clip);
        SelectClipRgn(cv->dc, rgn);
        DeleteObject(rgn);
    }
    else SelectClipRgn(cv->dc, NULL);
}

static inline void st_canvas_end(struct setu_canvas *cv)
{
    if (!cv->dc) return;
    SelectObject(cv->dc, cv->old);
    DeleteObject(cv->bmp);
    DeleteDC(cv->dc);
    cv->dc = NULL;
}

/* Paint a window through a canvas: BeginPaint, PAINT(canvas, client
 * rect), copy, EndPaint. */
static inline void st_paint_window(HWND hwnd, void (*paint)(struct setu_canvas *, const RECT *))
{
    PAINTSTRUCT ps;
    struct setu_canvas cv;
    RECT rc;
    HDC dc = BeginPaint(hwnd, &ps);
    GetClientRect(hwnd, &rc);
    if (st_canvas_begin(&cv, rc.right, rc.bottom))
    {
        paint(&cv, &rc);
        GdiFlush();
        BitBlt(dc, 0, 0, rc.right, rc.bottom, cv.dc, 0, 0, SRCCOPY);
        st_canvas_end(&cv);
    }
    EndPaint(hwnd, &ps);
}

static inline DWORD st_px(COLORREF c) { return (GetRValue(c) << 16) | (GetGValue(c) << 8) | GetBValue(c); }

/* blend colour C into one pixel with coverage A (0..255) */
static inline void st_blend_px(DWORD *p, DWORD c, unsigned a)
{
    DWORD d = *p;
    if (a >= 255) { *p = c; return; }
    if (!a) return;
    *p = ((((d >> 16) & 0xff) * (255 - a) + ((c >> 16) & 0xff) * a) / 255) << 16 |
         ((((d >> 8) & 0xff) * (255 - a) + ((c >> 8) & 0xff) * a) / 255) << 8 |
         (((d & 0xff) * (255 - a) + (c & 0xff) * a) / 255);
}

static inline void st_fill(struct setu_canvas *cv, const RECT *r, COLORREF c)
{
    HBRUSH b = CreateSolidBrush(c);
    FillRect(cv->dc, r, b);
    DeleteObject(b);
}

/* signed distance from (px,py) to the rounded rectangle (negative inside) */
static inline float st_sdf(float px, float py, float cx, float cy, float hw, float hh, float rad)
{
    float qx = fabsf(px - cx) - (hw - rad), qy = fabsf(py - cy) - (hh - rad);
    float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
    float in = qx > qy ? qx : qy;
    return sqrtf(ox * ox + oy * oy) + (in < 0 ? in : 0) - rad;
}

static inline float st_clampf(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

/* The workhorse: for every pixel of R grown by GROW, coverage = F(sdf). */
enum st_shape_mode { ST_FILL, ST_FRAME, ST_SHADOW };

static inline void st_shape(struct setu_canvas *cv, const RECT *r, int radius, COLORREF color, int alpha,
                     enum st_shape_mode mode, float width, int grow)
{
    float cx = (r->left + r->right) / 2.0f, cy = (r->top + r->bottom) / 2.0f;
    float hw = (r->right - r->left) / 2.0f, hh = (r->bottom - r->top) / 2.0f;
    float rad = (float)radius;
    int x0 = max((int)cv->clip.left, (int)r->left - grow), x1 = min((int)cv->clip.right, (int)r->right + grow);
    int y0 = max((int)cv->clip.top, (int)r->top - grow), y1 = min((int)cv->clip.bottom, (int)r->bottom + grow);
    DWORD c = st_px(color);
    int x, y;

    if (hw <= 0 || hh <= 0) return;
    if (rad > hw) rad = hw;
    if (rad > hh) rad = hh;
    GdiFlush();
    for (y = y0; y < y1; y++)
    {
        DWORD *row = cv->bits + (size_t)y * cv->w;
        float py = y + 0.5f;
        /* rows clear of the corners only need their two edge pixels */
        BOOL plain = mode == ST_FILL && py > r->top + rad + 1 && py < r->bottom - rad - 1 && alpha >= 255;
        for (x = x0; x < x1; x++)
        {
            float d, cov;
            if (plain && x > r->left + 1 && x < r->right - 2) { row[x] = c; continue; }
            d = st_sdf(x + 0.5f, py, cx, cy, hw, hh, rad);
            if (mode == ST_FILL) cov = st_clampf(0.5f - d);
            else if (mode == ST_FRAME) cov = st_clampf(0.5f - d) - st_clampf(0.5f - d - width);
            else
            {
                float t = d <= 0 ? 1 : st_clampf(1 - d / (float)grow);
                cov = t * t;
                if (d < -1) cov = 0;  /* never under the shape itself */
            }
            if (cov > 0) st_blend_px(&row[x], c, (unsigned)(cov * alpha + 0.5f));
        }
    }
}

/* anti-aliased rounded rectangle */
static inline void st_round(struct setu_canvas *cv, const RECT *r, int radius, COLORREF c)
{
    st_shape(cv, r, radius, c, 255, ST_FILL, 0, 0);
}

static inline void st_round_alpha(struct setu_canvas *cv, const RECT *r, int radius, COLORREF c, int alpha)
{
    st_shape(cv, r, radius, c, alpha, ST_FILL, 0, 0);
}

/* a WIDTH px outline just inside R */
static inline void st_frame(struct setu_canvas *cv, const RECT *r, int radius, int width, COLORREF c)
{
    st_shape(cv, r, radius, c, 255, ST_FRAME, (float)width, 0);
}

/* A soft shadow BLUR px wide around R (call before drawing R itself), in
 * the shadow token's colour. Its coverage falls off quadratically, so the
 * peak is 1.4 x the token's alpha (setu-light: 0x33 -> 72), which gives
 * about the token's alpha on average across the blur. */
static inline void st_shadow(struct setu_canvas *cv, const RECT *r, int radius, int blur)
{
    st_shape(cv, r, radius, ST->shadow, min(255, ST->shadow_alpha * 72 / 51), ST_SHADOW, 0, blur);
}

/* the keyboard focus ring: focus-width (2 px) accent, inset 1 px */
static inline void st_focus_ring(struct setu_canvas *cv, const RECT *r, int radius, UINT dpi)
{
    RECT f = *r;
    int inset = MulDiv(1, (int)dpi, 96);
    InflateRect(&f, -inset, -inset);
    st_frame(cv, &f, max(0, radius - inset), max(1, MulDiv(ST_FOCUS_W, (int)dpi, 96)), ST->accent);
}

static inline void st_text(struct setu_canvas *cv, const WCHAR *s, RECT *r, enum setu_font f, COLORREF c, UINT fmt)
{
    HGDIOBJ of = SelectObject(cv->dc, st_font[f]);
    SetTextColor(cv->dc, c);
    SetBkMode(cv->dc, TRANSPARENT);
    DrawTextW(cv->dc, s, -1, r, fmt | DT_NOPREFIX);
    SelectObject(cv->dc, of);
}

static inline int st_text_width(struct setu_canvas *cv, const WCHAR *s, enum setu_font f)
{
    SIZE sz = { 0, 0 };
    HGDIOBJ of = SelectObject(cv->dc, st_font[f]);
    GetTextExtentPoint32W(cv->dc, s, lstrlenW(s), &sz);
    SelectObject(cv->dc, of);
    return sz.cx;
}

/* ------------------------------------------------------------------ */
/* Line icons                                                           */

/* The Setu bridge mark on the icons' 24 x 24 grid: deck, arch, hangers. */
#define ST_MARK_ARCH 12
static short st_mark_segs[4 + ST_MARK_ARCH][4];

static inline const short (*st_mark(void))[4]
{
    static BOOL done;
    int i;
    if (!done)
    {
        static const short fixed[4][4] =
        {
            { 2 * 256, 7 * 256, 22 * 256, 7 * 256 },       /* deck */
            { 12 * 256, 7 * 256, 12 * 256, 13 * 256 },     /* middle hanger */
            { 4 * 256, 7 * 256, 4 * 256, 21 * 256 },       /* piers */
            { 20 * 256, 7 * 256, 20 * 256, 21 * 256 },
        };
        memcpy(st_mark_segs, fixed, sizeof(fixed));
        for (i = 0; i < ST_MARK_ARCH; i++)
        {
            double a0 = M_PI + M_PI * i / ST_MARK_ARCH, a1 = M_PI + M_PI * (i + 1) / ST_MARK_ARCH;
            st_mark_segs[4 + i][0] = (short)((12 + 8 * cos(a0)) * 256);
            st_mark_segs[4 + i][1] = (short)((21 + 8 * sin(a0)) * 256);
            st_mark_segs[4 + i][2] = (short)((12 + 8 * cos(a1)) * 256);
            st_mark_segs[4 + i][3] = (short)((21 + 8 * sin(a1)) * 256);
        }
        done = TRUE;
    }
    return (const short (*)[4])st_mark_segs;
}

#define ICON_SETU_MARK ICON_COUNT   /* pseudo-icon: the Setu mark */

/* the built-in glyphs' coverage masks, by Lucide icon and size */
struct st_icon_mask { int icon, size; BYTE *cov; };
static struct st_icon_mask st_icon_cache[96];
static int st_icon_cached;

static inline const BYTE *st_glyph_mask(int icon, int size)
{
    const short (*segs)[4];
    int nseg, i, x, y, k;
    float scale, half;
    BYTE *cov;
    struct st_icon_mask *e;

    for (i = 0; i < st_icon_cached; i++)
        if (st_icon_cache[i].icon == icon && st_icon_cache[i].size == size) return st_icon_cache[i].cov;
    if (icon == ICON_SETU_MARK)
    {
        segs = st_mark();
        nseg = 4 + ST_MARK_ARCH;
    }
    else if (icon >= 0 && icon < ICON_COUNT)
    {
        segs = setu_icon_segs + setu_icon_first[icon];
        nseg = setu_icon_first[icon + 1] - setu_icon_first[icon];
    }
    else return NULL;
    if (size <= 0 || size > 256 || !(cov = calloc((size_t)size * size, 1))) return NULL;

    /* Lucide strokes are 2 units wide; Setu uses 1.5 px at 24 px (DESIGN.md),
     * kept at least 1.25 px wide so small icons don't fade; the mark is bolder */
    scale = size / (24.0f * 256.0f);
    half = (icon == ICON_SETU_MARK ? 2.4f : 1.5f) * size / 24.0f;
    if (half < 1.25f) half = 1.25f;
    half /= 2;
    for (y = 0; y < size; y++)
        for (x = 0; x < size; x++)
        {
            float px = x + 0.5f, py = y + 0.5f, best = 1e9f;
            for (k = 0; k < nseg; k++)
            {
                float x0 = segs[k][0] * scale, y0 = segs[k][1] * scale;
                float dx = segs[k][2] * scale - x0, dy = segs[k][3] * scale - y0;
                float l = dx * dx + dy * dy, t = 0, ex, ey, d;
                if (l > 0) t = st_clampf(((px - x0) * dx + (py - y0) * dy) / l);
                ex = px - x0 - t * dx;
                ey = py - y0 - t * dy;
                d = ex * ex + ey * ey;
                if (d < best) best = d;
            }
            cov[y * size + x] = (BYTE)(st_clampf(half + 0.5f - sqrtf(best)) * 255 + 0.5f);
        }
    if (st_icon_cached == (int)ARRAYSIZE(st_icon_cache))
    {
        free(st_icon_cache[0].cov);
        memmove(st_icon_cache, st_icon_cache + 1, sizeof(st_icon_cache) - sizeof(st_icon_cache[0]));
        st_icon_cached--;
    }
    e = &st_icon_cache[st_icon_cached++];
    e->icon = icon;
    e->size = size;
    e->cov = cov;
    return cov;
}

/* ------------------------------------------------------------------ */
/* Named icons (setu-theme.h, distro/themes/ICONS.md)                   */

/* the built-in Lucide glyph of each name */
#define SETU_X_FALLBACK(id, key, def) def,
static const int st_icon_fallback[TI_COUNT] = { SETU_ICON_NAMES(SETU_X_FALLBACK) };
#undef SETU_X_FALLBACK

/* A pack's icon at one size: composited over black (premultiplied colour)
 * and its alpha. MONO when every visible pixel has the same colour: such
 * icons are glyphs and get recoloured to the colour asked for. */
struct st_pack_icon { int name, size; BOOL ok, mono; BYTE *alpha; DWORD *px; };
static struct st_pack_icon st_pack_cache[64];
static int st_pack_cached;

static inline void st_icon_flush(void)
{
    int i;
    for (i = 0; i < st_pack_cached; i++)
    {
        free(st_pack_cache[i].alpha);
        free(st_pack_cache[i].px);
    }
    st_pack_cached = 0;
}

static inline const struct st_pack_icon *st_pack_icon(int name, int size)
{
    struct st_pack_icon *e;
    struct setu_canvas black, white;
    HICON h;
    int i, n = size * size;
    DWORD first = 0;
    BOOL have_first = FALSE;

    if (name < 0 || name >= TI_COUNT || !setu_theme.icon_file[name][0] || size <= 0 || size > 256) return NULL;
    for (i = 0; i < st_pack_cached; i++)
        if (st_pack_cache[i].name == name && st_pack_cache[i].size == size)
            return st_pack_cache[i].ok ? &st_pack_cache[i] : NULL;
    if (st_pack_cached == (int)ARRAYSIZE(st_pack_cache))
    {
        free(st_pack_cache[0].alpha);
        free(st_pack_cache[0].px);
        memmove(st_pack_cache, st_pack_cache + 1, sizeof(st_pack_cache) - sizeof(st_pack_cache[0]));
        st_pack_cached--;
    }
    e = &st_pack_cache[st_pack_cached++];
    memset(e, 0, sizeof(*e));
    e->name = name;
    e->size = size;
    h = (HICON)LoadImageW(NULL, setu_theme.icon_file[name], IMAGE_ICON, size, size, LR_LOADFROMFILE);
    if (!h) return NULL;
    if (st_canvas_begin(&black, size, size))
    {
        if (st_canvas_begin(&white, size, size))
        {
            RECT r = { 0, 0, size, size };
            st_fill(&black, &r, RGB(0, 0, 0));
            st_fill(&white, &r, RGB(0xff, 0xff, 0xff));
            DrawIconEx(black.dc, 0, 0, h, size, size, 0, NULL, DI_NORMAL);
            DrawIconEx(white.dc, 0, 0, h, size, size, 0, NULL, DI_NORMAL);
            GdiFlush();
            e->alpha = malloc(n);
            e->px = malloc(n * sizeof(DWORD));
            if (e->alpha && e->px)
            {
                e->ok = e->mono = TRUE;
                for (i = 0; i < n; i++)
                {
                    DWORD k = black.bits[i] & 0xffffff, w = white.bits[i] & 0xffffff;
                    int ar = 255 - (int)(((w >> 16) & 0xff) - ((k >> 16) & 0xff));
                    int ag = 255 - (int)(((w >> 8) & 0xff) - ((k >> 8) & 0xff));
                    int ab = 255 - (int)((w & 0xff) - (k & 0xff));
                    int al = max(ar, max(ag, ab));
                    if (al < 0) al = 0;
                    if (al > 255) al = 255;
                    e->alpha[i] = (BYTE)al;
                    e->px[i] = k;
                    if (al >= 128)
                    {
                        /* the pixel's own colour */
                        DWORD c = (min(255u, ((k >> 16) & 0xff) * 255 / al) << 16) |
                                  (min(255u, ((k >> 8) & 0xff) * 255 / al) << 8) | min(255u, (k & 0xff) * 255 / al);
                        if (!have_first) { first = c; have_first = TRUE; }
                        else if (abs((int)((c >> 16) & 0xff) - (int)((first >> 16) & 0xff)) > 40 ||
                                 abs((int)((c >> 8) & 0xff) - (int)((first >> 8) & 0xff)) > 40 ||
                                 abs((int)(c & 0xff) - (int)(first & 0xff)) > 40)
                            e->mono = FALSE;
                    }
                }
            }
            st_canvas_end(&white);
        }
        st_canvas_end(&black);
    }
    DestroyIcon(h);
    return e->ok ? e : NULL;
}

/* Is NAME drawn from the active pack (rather than the built-in glyph)? */
static inline BOOL st_icon_from_pack(int name) { return name >= 0 && name < TI_COUNT && setu_theme.icon_file[name][0]; }

/* Draw icon NAME (enum setu_icon_name) SIZE px square at X,Y: the pack's
 * icon when it has one (monochrome ones recoloured to COLOR), else the
 * built-in line glyph in COLOR. */
static inline void st_icon(struct setu_canvas *cv, int name, int x, int y, int size, COLORREF color)
{
    const struct st_pack_icon *pi = st_pack_icon(name, size);
    const BYTE *m = NULL;
    DWORD c = st_px(color);
    int i, j;

    if (!pi && !(m = st_glyph_mask(name >= 0 && name < TI_COUNT ? st_icon_fallback[name] : -1, size))) return;
    GdiFlush();
    for (j = 0; j < size; j++)
    {
        if (y + j < cv->clip.top || y + j >= cv->clip.bottom) continue;
        for (i = 0; i < size; i++)
        {
            DWORD *d;
            int k = j * size + i;
            if (x + i < cv->clip.left || x + i >= cv->clip.right) continue;
            d = &cv->bits[(size_t)(y + j) * cv->w + x + i];
            if (!pi) st_blend_px(d, c, m[k]);
            else if (pi->mono) st_blend_px(d, c, pi->alpha[k]);
            else if (pi->alpha[k])
            {
                /* premultiplied source over the canvas */
                DWORD s = pi->px[k], o = *d;
                unsigned a = 255 - pi->alpha[k];
                *d = (min(255u, ((s >> 16) & 0xff) + ((o >> 16) & 0xff) * a / 255) << 16) |
                     (min(255u, ((s >> 8) & 0xff) + ((o >> 8) & 0xff) * a / 255) << 8) |
                     min(255u, (s & 0xff) + (o & 0xff) * a / 255);
            }
        }
    }
}

/* An icon centred in R. */
static inline void st_icon_in(struct setu_canvas *cv, int name, const RECT *r, int size, COLORREF color)
{
    st_icon(cv, name, (r->left + r->right - size) / 2, (r->top + r->bottom - size) / 2, size, color);
}

/* ------------------------------------------------------------------ */
/* Reloading                                                            */

/* (Re)load the active pack: colours, metrics, named icons. The caller
 * remakes its fonts (st_make_fonts) and brushes, and repaints. */
static inline void st_theme_reload(void)
{
    struct setu_colors *c = &st_colors;
    const COLORREF *t;
    int i;

    setu_theme_reload();
    t = setu_theme.color;
    c->accent = t[TC_ACCENT];
    c->accent_hover = t[TC_ACCENT_HOVER];
    c->accent_pressed = t[TC_ACCENT_PRESSED];
    c->accent_text = t[TC_ACCENT_TEXT];
    c->window_bg = t[TC_WINDOW_BG];
    c->surface = t[TC_SURFACE];
    c->surface_hover = t[TC_SURFACE_HOVER];
    c->border = t[TC_BORDER];
    c->border_subtle = t[TC_BORDER_SUBTLE];
    c->text = t[TC_TEXT];
    c->text_secondary = t[TC_TEXT_SECONDARY];
    c->text_disabled = t[TC_TEXT_DISABLED];
    c->titlebar_active = t[TC_TITLEBAR_ACTIVE];
    c->titlebar_inactive = t[TC_TITLEBAR_INACTIVE];
    c->titlebar_text = t[TC_TITLEBAR_TEXT];
    c->close_hover = t[TC_CLOSE_HOVER];
    c->taskbar = t[TC_TASKBAR];
    c->taskbar_text = t[TC_TASKBAR_TEXT];
    c->menu = t[TC_MENU];
    c->selection = t[TC_SELECTION];
    c->selection_alpha = setu_theme.alpha[TC_SELECTION];
    c->selection_border = t[TC_SELECTION_BORDER];
    c->shadow = t[TC_SHADOW];
    c->shadow_alpha = setu_theme.alpha[TC_SHADOW];
    c->desktop = t[TC_DESKTOP];
    c->logo = t[TC_LOGO];
    c->error = t[TC_ERROR];
    for (i = 0; i < 6; i++) c->tile[i] = t[TC_TILE_1 + i];
    st_icon_flush();
}

#endif /* SETU_STYLE_H */
