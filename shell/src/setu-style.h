/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * setu-style.h: Setu's design tokens and drawing helpers, shared by the
 * shell (setu-shell.c) and the file manager (setu-files.c).
 *
 * docs/DESIGN.md is the source of every value here: change it there
 * first. Colours live in one table (struct setu_colors) so a dark table
 * can be added later without touching drawing code; code reads them
 * through the ST pointer.
 *
 * Drawing: GDI's RoundRect and pens are aliased, so the rounded shapes,
 * shadows and line icons here are drawn by hand into a 32-bit DIB section
 * (struct setu_canvas), with anti-aliased edges from a signed distance
 * function. Text still goes through GDI (DrawTextW) on the same DIB.
 * Icons are Lucide's (ISC; shell/icons/, NOTICE), flattened into stroke
 * segments by shell/icons/gen-icons.py (setu-icons.h) and rasterized here
 * at the size asked for, so they stay crisp at any DPI.
 *
 * Fonts: Inter (SIL OFL 1.1) when it's installed, else the system's
 * message font (SystemParametersInfoW SPI_GETNONCLIENTMETRICS), so the
 * programs still work in a prefix without Inter.
 *
 * References (learn.microsoft.com): CreateDIBSection, GdiFlush,
 * EnumFontFamiliesExW, CreateFontIndirectW (LOGFONTW), DrawTextW.
 */
#ifndef SETU_STYLE_H
#define SETU_STYLE_H

#include <windows.h>
#include <math.h>
#include "setu-icons.h"

/* ------------------------------------------------------------------ */
/* Colours                                                              */

struct setu_colors
{
    COLORREF accent, accent_hover, accent_pressed, accent_text;
    COLORREF window_bg, surface, surface_hover;
    COLORREF border, border_subtle;
    COLORREF text, text_secondary, text_disabled;
    COLORREF titlebar_active, titlebar_inactive, close_hover;
    COLORREF taskbar;
    COLORREF logo;          /* the Setu bridge mark */
    COLORREF error;         /* error text (not in DESIGN.md's table yet) */
};

static const struct setu_colors setu_light =
{
    RGB(0x1f, 0x6f, 0xb2), RGB(0x2a, 0x7d, 0xc3), RGB(0x19, 0x5e, 0x97), RGB(0xff, 0xff, 0xff),
    RGB(0xf3, 0xf3, 0xf3), RGB(0xff, 0xff, 0xff), RGB(0xea, 0xea, 0xea),
    RGB(0xd1, 0xd1, 0xd1), RGB(0xe5, 0xe5, 0xe5),
    RGB(0x1b, 0x1b, 0x1b), RGB(0x5f, 0x5f, 0x5f), RGB(0xa0, 0xa0, 0xa0),
    RGB(0xff, 0xff, 0xff), RGB(0xf3, 0xf3, 0xf3), RGB(0xc4, 0x2b, 0x1c),
    RGB(0xee, 0xf1, 0xf5),
    RGB(0xf5, 0xa6, 0x23),
    RGB(0xc4, 0x2b, 0x1c),
};

/* the active colour table (a dark one can be chosen here later) */
static const struct setu_colors *ST = &setu_light;

/* A mixed with B; T is B's share, 0 to 255. */
static inline COLORREF st_mix(COLORREF a, COLORREF b, int t)
{
    return RGB((GetRValue(a) * (255 - t) + GetRValue(b) * t) / 255,
               (GetGValue(a) * (255 - t) + GetGValue(b) * t) / 255,
               (GetBValue(a) * (255 - t) + GetBValue(b) * t) / 255);
}

/* Selection: accent at 20 % over the surface, with a 1 px accent outline. */
static inline COLORREF st_selection(void) { return st_mix(ST->surface, ST->accent, 51); }
/* Selection while the control doesn't have the focus. */
static inline COLORREF st_selection_inactive(void) { return st_mix(ST->surface, ST->accent, 26); }

/* ------------------------------------------------------------------ */
/* Shape and spacing (pixels at 96 DPI)                                 */

#define ST_RADIUS_WINDOW   8   /* windows, menus, popups, start menu */
#define ST_RADIUS_CONTROL  4   /* buttons, edits, combo boxes */
#define ST_RADIUS_CHECK    3
#define ST_CONTROL_H      28
#define ST_ROW_H          32   /* list rows */
#define ST_MENU_ITEM_H    30
#define ST_TASKBAR_H      48
#define ST_TASK_BUTTON    40   /* taskbar buttons are 40 x 40 */
#define ST_TASK_ICON      24
#define ST_PILL_H          3
#define ST_SHADOW_BLUR    16

/* ------------------------------------------------------------------ */
/* Fonts                                                                */

enum setu_font
{
    SF_BODY,      /* 9 pt */
    SF_CAPTION,   /* 9 pt semibold */
    SF_TITLE,     /* 10.5 pt semibold */
    SF_LARGE,     /* 14 pt semibold */
    SF_COUNT
};

static HFONT st_font[SF_COUNT];
static BOOL st_inter, st_inter_semibold;
static WCHAR st_fallback_face[LF_FACESIZE];

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

/* (Re)create the fonts for DPI. */
static inline void st_make_fonts(UINT dpi)
{
    static const struct { int tenths; int weight; } spec[SF_COUNT] =
    {
        { 90, FW_NORMAL }, { 90, FW_SEMIBOLD }, { 105, FW_SEMIBOLD }, { 140, FW_SEMIBOLD },
    };
    static BOOL looked;
    NONCLIENTMETRICSW ncm;
    int i;

    if (!looked)
    {
        memset(&ncm, 0, sizeof(ncm));
        ncm.cbSize = sizeof(ncm);
        st_inter = st_have_face(L"Inter");
        /* static Inter fonts name each weight as its own family */
        st_inter_semibold = st_have_face(L"Inter SemiBold");
        if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
            lstrcpynW(st_fallback_face, ncm.lfMessageFont.lfFaceName, LF_FACESIZE);
        if (!st_fallback_face[0]) lstrcpyW(st_fallback_face, L"MS Shell Dlg 2");
        looked = TRUE;
    }
    for (i = 0; i < SF_COUNT; i++)
    {
        LOGFONTW lf;
        memset(&lf, 0, sizeof(lf));
        lf.lfHeight = -MulDiv(spec[i].tenths, (int)dpi, 720);
        lf.lfWeight = spec[i].weight;
        lf.lfCharSet = DEFAULT_CHARSET;
        lf.lfQuality = ANTIALIASED_QUALITY;
        if (spec[i].weight >= FW_SEMIBOLD && st_inter_semibold) lstrcpyW(lf.lfFaceName, L"Inter SemiBold");
        else if (st_inter) lstrcpyW(lf.lfFaceName, L"Inter");
        else lstrcpynW(lf.lfFaceName, st_fallback_face, LF_FACESIZE);
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

/* a soft shadow BLUR px wide around R (call before drawing R itself) */
static inline void st_shadow(struct setu_canvas *cv, const RECT *r, int radius, int blur, int alpha)
{
    st_shape(cv, r, radius, RGB(0, 0, 0), alpha, ST_SHADOW, 0, blur);
}

/* the keyboard focus ring: 2 px accent, inset 1 px */
static inline void st_focus_ring(struct setu_canvas *cv, const RECT *r, int radius, UINT dpi)
{
    RECT f = *r;
    int inset = MulDiv(1, (int)dpi, 96);
    InflateRect(&f, -inset, -inset);
    st_frame(cv, &f, max(0, radius - inset), max(2, MulDiv(2, (int)dpi, 96)), ST->accent);
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

struct st_icon_mask { int icon, size; BYTE *cov; };
static struct st_icon_mask st_icon_cache[96];
static int st_icon_cached;

static inline const BYTE *st_icon_mask(int icon, int size)
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

/* Draw ICON (enum setu_icon, or ICON_SETU_MARK) SIZE px square at X,Y. */
static inline void st_icon(struct setu_canvas *cv, int icon, int x, int y, int size, COLORREF color)
{
    const BYTE *m = st_icon_mask(icon, size);
    DWORD c = st_px(color);
    int i, j;
    if (!m) return;
    GdiFlush();
    for (j = 0; j < size; j++)
    {
        if (y + j < cv->clip.top || y + j >= cv->clip.bottom) continue;
        for (i = 0; i < size; i++)
        {
            if (x + i < cv->clip.left || x + i >= cv->clip.right) continue;
            st_blend_px(&cv->bits[(size_t)(y + j) * cv->w + x + i], c, m[j * size + i]);
        }
    }
}

/* An icon centred in R. */
static inline void st_icon_in(struct setu_canvas *cv, int icon, const RECT *r, int size, COLORREF color)
{
    st_icon(cv, icon, (r->left + r->right - size) / 2, (r->top + r->bottom - size) / 2, size, color);
}

#endif /* SETU_STYLE_H */
