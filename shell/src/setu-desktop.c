/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * setu-desktop: the Setu desktop, the wallpaper and the icons of the
 * Desktop folders.
 *
 * Like the shell's desktop view on Windows, it is an ordinary top-level
 * window that covers the screen and stays below every other window. It
 * shows the items of the user's Desktop folder (FOLDERID_Desktop) merged
 * with the Public Desktop (FOLDERID_PublicDesktop), skipping hidden and
 * system items. setu-shell.exe starts it at login. Only public, documented
 * APIs are used. Main references (learn.microsoft.com):
 *   - KNOWNFOLDERID (FOLDERID_Desktop, FOLDERID_PublicDesktop),
 *     SHGetKnownFolderPath; "Introduction to the Shell Namespace"
 *   - SHGetFileInfoW (SHGFI_DISPLAYNAME: the name without a known
 *     extension unless HideFileExt is 0, SHGFI_SYSICONINDEX), SHGetImageList
 *     (SHIL_EXTRALARGE), ImageList_Draw
 *   - ShellExecuteExW ("open", "properties" with SEE_MASK_INVOKEIDLIST),
 *     IShellLinkW::GetPath
 *   - ReadDirectoryChangesW, SHChangeNotifyRegister, SHChangeNotify
 *   - SHFileOperationW (FO_DELETE with FOF_ALLOWUNDO: the Recycle Bin;
 *     FO_COPY / FO_MOVE for Paste and dropped files), DragQueryFileW,
 *     CF_HDROP and CFSTR_PREFERREDDROPEFFECT
 *   - SystemParametersInfoW (SPI_GETWORKAREA, SPI_GETDESKWALLPAPER), the
 *     WallpaperStyle / TileWallpaper values (DESKTOP_WALLPAPER_POSITION),
 *     WIC (IWICImagingFactory) to read the picture
 *   - SetWindowPos (HWND_BOTTOM), WM_WINDOWPOSCHANGING
 *   - TrackPopupMenu, WM_CONTEXTMENU, GetSystemMetrics (SM_CXDRAG)
 *   - the NoDesktop policy (HKCU/HKLM ...\Policies\Explorer)
 *
 * Command line (a second instance hands its arguments to the running one):
 *   --state-file PATH   write the desktop's state (for tests) to PATH
 *   --exit-after SEC    quit after SEC seconds
 *   --select NAME       select the item called NAME (its label or file name)
 *   --open NAME         open it
 *   --refresh           read the folders again
 *   --arrange           forget the positions and sort by name
 *   --quit              quit
 *
 * Item positions (grid cells) the user chose are kept in
 * HKCU\Software\Setu\Desktop\Positions, one REG_DWORD per item named
 * "u:<file>" (Desktop) or "c:<file>" (Public Desktop): row << 16 | column.
 */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define COBJMACROS
#define _WIN32_WINNT 0x0A00
#define _WIN32_IE 0x0A00
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <knownfolders.h>
#include <objbase.h>
#include <wincodec.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

#include "setu-style.h"

#define DESK_CLASS     L"SetuDesktop"
#define COPYDATA_MAGIC 0x4b534544 /* "DESK" */
#define CMD_SEP        L'\x1f'
#define POS_KEY        L"Software\\Setu\\Desktop\\Positions"

#define WM_APP_RESCAN   (WM_APP + 1)
#define WM_APP_SHNOTIFY (WM_APP + 2)

#define TIMER_RESCAN 1
#define TIMER_EXIT   2
#define TIMER_THEME  3

#define IDM_OPEN       100
#define IDM_RENAME     101
#define IDM_DELETE     102
#define IDM_PROPERTIES 103
#define IDM_NEW_FOLDER 110
#define IDM_NEW_TEXT   111
#define IDM_REFRESH    112
#define IDM_ARRANGE    113
#define IDM_PASTE      114
#define IDM_OPEN_DESK  115

#define LABEL_LINES     2   /* lines of a label, unless it is selected */
#define LABEL_LINES_SEL 5

struct item
{
    WCHAR path[MAX_PATH];
    WCHAR file[MAX_PATH];
    WCHAR label[MAX_PATH];      /* SHGFI_DISPLAYNAME */
    WCHAR shown[MAX_PATH];      /* the label cut to LABEL_LINES, with an ellipsis */
    DWORD attrs;
    BOOL dir, link, common;
    int icon;                   /* system image list index, -1 none */
    int col, row;
    BOOL placed;
};

static HINSTANCE hinst;
static HWND desk, edit_wnd;
static WNDPROC edit_orig_proc;
static UINT dpi = 96;
static WCHAR folders[2][MAX_PATH];  /* [0] Desktop, [1] Public Desktop */
static struct item *items;
static int nitems, items_cap;
static int sel = -1, hot = -1, renaming = -1;
static BOOL focused, tracking_mouse;
static int down_item = -1;
static POINT down_pt, drag_pt;
static BOOL dragging;
static RECT work;                   /* the work area in client coordinates */
static POINT origin;                /* the window's top-left in screen coordinates */
static int cell_w, cell_h, icon_px, margin, line_h, grid_rows;
static HIMAGELIST il;
static struct setu_canvas bb;       /* back buffer, client size */
static HBITMAP wall;                /* the composed wallpaper, client size */
static BOOL no_desktop;
static WCHAR state_path[MAX_PATH];
static int state_seq, rescans, theme_reloads, nwatching;
static WCHAR status_text[512], last_open[MAX_PATH * 2 + 16], menu_text[512];
static int exit_after;
static HANDLE watch_stop;
static ULONG shnotify_id;
static WCHAR pending_path[MAX_PATH]; /* a new item to put in pending_cell */
static POINT pending_cell;
static BOOL rename_closing;

static int S(int v) { return MulDiv(v, (int)dpi, 96); }

/* ------------------------------------------------------------------ */
/* Helpers                                                              */

static UINT window_dpi(HWND hwnd)
{
    UINT (WINAPI *pGetDpiForWindow)(HWND) =
        (void *)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
    UINT d = 0;
    HDC dc;
    if (pGetDpiForWindow && hwnd) d = pGetDpiForWindow(hwnd);
    if (d) return d;
    dc = GetDC(NULL);
    d = GetDeviceCaps(dc, LOGPIXELSY);
    ReleaseDC(NULL, dc);
    return d ? d : 96;
}

static void set_dpi_awareness(void)
{
    BOOL (WINAPI *pSetCtx)(HANDLE) =
        (void *)GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext");
    /* DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 == (HANDLE)-4 */
    if (pSetCtx && pSetCtx((HANDLE)(LONG_PTR)-4)) return;
    SetProcessDPIAware();
}

static void set_status(const WCHAR *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf(status_text, ARRAYSIZE(status_text) - 1, fmt, ap);
    status_text[ARRAYSIZE(status_text) - 1] = 0;
    va_end(ap);
}

static void join_path(WCHAR *out, const WCHAR *dir, const WCHAR *name)
{
    _snwprintf(out, MAX_PATH, L"%ls\\%ls", dir, name);
    out[MAX_PATH - 1] = 0;
}

static void error_text(DWORD err, WCHAR *buf, int len)
{
    if (!FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, err, 0, buf, len, NULL))
        _snwprintf(buf, len, L"Error %lu.", err);
    buf[len - 1] = 0;
}

/* Look for EXE next to us, in the system directory, then on the path. */
static BOOL find_program(const WCHAR *exe, WCHAR *path, DWORD len)
{
    WCHAR dir[MAX_PATH], *slash;
    if (GetModuleFileNameW(NULL, dir, ARRAYSIZE(dir)) && (slash = wcsrchr(dir, L'\\')))
    {
        *slash = 0;
        if (SearchPathW(dir, exe, NULL, len, path, NULL)) return TRUE;
    }
    GetSystemDirectoryW(dir, ARRAYSIZE(dir));
    return SearchPathW(dir, exe, NULL, len, path, NULL) || SearchPathW(NULL, exe, NULL, len, path, NULL);
}

static BOOL policy_no_desktop(void)
{
    static const WCHAR key[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer";
    DWORD v = 0, size = sizeof(v);
    if (!RegGetValueW(HKEY_LOCAL_MACHINE, key, L"NoDesktop", RRF_RT_REG_DWORD, NULL, &v, &size) && v) return TRUE;
    size = sizeof(v);
    v = 0;
    return !RegGetValueW(HKEY_CURRENT_USER, key, L"NoDesktop", RRF_RT_REG_DWORD, NULL, &v, &size) && v;
}

/* ------------------------------------------------------------------ */
/* Geometry                                                             */

static void cell_rect(int col, int row, RECT *r)
{
    r->left = work.left + margin + col * cell_w;
    r->top = work.top + margin + row * cell_h;
    r->right = r->left + cell_w;
    r->bottom = r->top + cell_h;
}

/* the icon's top-left corner; while dragged it follows the mouse */
static void icon_pos(int i, int *x, int *y)
{
    RECT c;
    cell_rect(items[i].col, items[i].row, &c);
    *x = c.left + (cell_w - icon_px) / 2;
    *y = c.top + S(6);
    if (dragging && i == down_item)
    {
        *x += drag_pt.x - down_pt.x;
        *y += drag_pt.y - down_pt.y;
    }
}

static void label_rect(int i, int lines, RECT *r)
{
    int x, y;
    icon_pos(i, &x, &y);
    r->left = x + icon_px / 2 - cell_w / 2 + S(4);
    r->right = r->left + cell_w - S(8);
    r->top = y + icon_px + S(4);
    r->bottom = r->top + lines * line_h;
}

static void measure_label(const WCHAR *text, RECT *r)
{
    HDC dc = GetDC(NULL);
    HGDIOBJ of = SelectObject(dc, st_font[SF_BODY]);
    DrawTextW(dc, text, -1, r, DT_CALCRECT | DT_CENTER | DT_WORDBREAK | DT_EDITCONTROL | DT_NOPREFIX);
    SelectObject(dc, of);
    ReleaseDC(NULL, dc);
}

/* lines the full label needs, at most LABEL_LINES_SEL */
static int label_lines(int i)
{
    RECT r;
    int n;
    label_rect(i, 1, &r);
    measure_label(items[i].label, &r);
    n = (r.bottom - r.top + line_h - 1) / line_h;
    return max(1, min(LABEL_LINES_SEL, n));
}

/* What the item covers: its icon and label, with a little padding. */
static void item_rect(int i, BOOL full, RECT *r)
{
    RECT l;
    int x, y;
    icon_pos(i, &x, &y);
    label_rect(i, full && i == sel ? max(LABEL_LINES, label_lines(i)) : LABEL_LINES, &l);
    r->left = l.left - S(2);
    r->right = l.right + S(2);
    r->top = y - S(4);
    r->bottom = l.bottom + S(4);
}

static void invalidate_item(int i)
{
    RECT r;
    if (i < 0 || i >= nitems || !desk) return;
    item_rect(i, TRUE, &r);
    InflateRect(&r, S(2), S(2));
    InvalidateRect(desk, &r, FALSE);
}

static int hit_test(POINT pt)
{
    int i;
    /* the selected item's taller label is on top */
    if (sel >= 0)
    {
        RECT r;
        item_rect(sel, TRUE, &r);
        if (PtInRect(&r, pt)) return sel;
    }
    for (i = 0; i < nitems; i++)
    {
        RECT r;
        item_rect(i, FALSE, &r);
        if (PtInRect(&r, pt)) return i;
    }
    return -1;
}

static void point_to_cell(POINT pt, int *col, int *row)
{
    *col = (pt.x - work.left - margin) / cell_w;
    *row = (pt.y - work.top - margin) / cell_h;
    if (pt.x < work.left + margin) *col = 0;
    if (pt.y < work.top + margin) *row = 0;
    if (*col < 0) *col = 0;
    if (*row < 0) *row = 0;
    if (*row >= grid_rows) *row = grid_rows - 1;
}

static int item_at_cell(int col, int row, int except)
{
    int i;
    for (i = 0; i < nitems; i++)
        if (i != except && items[i].placed && items[i].col == col && items[i].row == row) return i;
    return -1;
}

static void first_free_cell(int *col, int *row)
{
    int c, r;
    for (c = 0;; c++)
        for (r = 0; r < grid_rows; r++)
            if (item_at_cell(c, r, -1) < 0) { *col = c; *row = r; return; }
}

/* Sizes from the DPI, the font and the icons; the grid from the work area. */
static void layout(void)
{
    RECT wa, vr;
    HDC dc;
    TEXTMETRICW tm;
    HGDIOBJ of;
    int cx = 32, cy = 32;

    if (il) ImageList_GetIconSize(il, &cx, &cy);
    icon_px = cx;
    dc = GetDC(NULL);
    of = SelectObject(dc, st_font[SF_BODY]);
    GetTextMetricsW(dc, &tm);
    SelectObject(dc, of);
    ReleaseDC(NULL, dc);
    line_h = tm.tmHeight;
    margin = S(8);
    cell_w = max(S(88), icon_px + S(32));
    cell_h = S(6) + icon_px + S(4) + LABEL_LINES * line_h + S(10);

    GetWindowRect(desk, &vr);
    origin.x = vr.left;
    origin.y = vr.top;
    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0)) wa = vr;
    OffsetRect(&wa, -origin.x, -origin.y);
    work = wa;
    grid_rows = max(1, (work.bottom - work.top - 2 * margin) / cell_h);
}

/* ------------------------------------------------------------------ */
/* Wallpaper                                                            */

/* Read any picture WIC can decode into a 32-bit DIB section. */
static HBITMAP load_picture(const WCHAR *path, int *w, int *h)
{
    IWICImagingFactory *f = NULL;
    IWICBitmapDecoder *dec = NULL;
    IWICBitmapFrameDecode *frame = NULL;
    IWICFormatConverter *conv = NULL;
    HBITMAP bmp = NULL;
    UINT uw = 0, uh = 0;
    void *bits = NULL;

    if (FAILED(CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory,
                                (void **)&f))) return NULL;
    if (SUCCEEDED(IWICImagingFactory_CreateDecoderFromFilename(f, path, NULL, GENERIC_READ,
                                                               WICDecodeMetadataCacheOnDemand, &dec)) &&
        SUCCEEDED(IWICBitmapDecoder_GetFrame(dec, 0, &frame)) &&
        SUCCEEDED(IWICImagingFactory_CreateFormatConverter(f, &conv)) &&
        SUCCEEDED(IWICFormatConverter_Initialize(conv, (IWICBitmapSource *)frame, &GUID_WICPixelFormat32bppBGR,
                                                 WICBitmapDitherTypeNone, NULL, 0, WICBitmapPaletteTypeCustom)) &&
        SUCCEEDED(IWICFormatConverter_GetSize(conv, &uw, &uh)) && uw && uh && uw < 20000 && uh < 20000)
    {
        BITMAPINFO bi;
        memset(&bi, 0, sizeof(bi));
        bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth = (LONG)uw;
        bi.bmiHeader.biHeight = -(LONG)uh;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bmp = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
        if (bmp && FAILED(IWICFormatConverter_CopyPixels(conv, NULL, uw * 4, uw * uh * 4, bits)))
        {
            DeleteObject(bmp);
            bmp = NULL;
        }
        *w = (int)uw;
        *h = (int)uh;
    }
    if (conv) IWICFormatConverter_Release(conv);
    if (frame) IWICBitmapFrameDecode_Release(frame);
    if (dec) IWICBitmapDecoder_Release(dec);
    IWICImagingFactory_Release(f);
    return bmp;
}

static int desktop_int(const WCHAR *name, int def)
{
    WCHAR buf[32];
    DWORD size = sizeof(buf);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Control Panel\\Desktop", name, RRF_RT_REG_SZ, NULL, buf, &size))
        return def;
    return _wtoi(buf);
}

/* Compose the wallpaper for the client area, as the desktop window does:
 * the background colour, then the picture tiled, centred, stretched,
 * fitted or filled (WallpaperStyle 0, 2, 6, 10, 22; TileWallpaper 1). */
static void load_wallpaper(void)
{
    WCHAR path[MAX_PATH] = L"";
    RECT rc;
    HDC dc, src;
    HBITMAP pic = NULL;
    HBRUSH br;
    int pw = 0, ph = 0, style, tile;

    if (wall) DeleteObject(wall);
    wall = NULL;
    GetClientRect(desk, &rc);
    dc = GetDC(desk);
    wall = CreateCompatibleBitmap(dc, max(1, rc.right), max(1, rc.bottom));
    ReleaseDC(desk, dc);
    if (!wall) return;
    dc = CreateCompatibleDC(NULL);
    SelectObject(dc, wall);
    br = CreateSolidBrush(GetSysColor(COLOR_DESKTOP));
    FillRect(dc, &rc, br);
    DeleteObject(br);

    SystemParametersInfoW(SPI_GETDESKWALLPAPER, ARRAYSIZE(path), path, 0);
    if (path[0]) pic = load_picture(path, &pw, &ph);
    if (pic)
    {
        int w = rc.right, h = rc.bottom;
        style = desktop_int(L"WallpaperStyle", 0);
        tile = desktop_int(L"TileWallpaper", 0);
        src = CreateCompatibleDC(NULL);
        SelectObject(src, pic);
        SetStretchBltMode(dc, HALFTONE);
        SetBrushOrgEx(dc, 0, 0, NULL);
        if (tile)
        {
            int x, y;
            for (y = 0; y < h; y += ph)
                for (x = 0; x < w; x += pw) BitBlt(dc, x, y, pw, ph, src, 0, 0, SRCCOPY);
        }
        else if (style == 2 || style == 6 || style == 10 || style == 22)
        {
            int dw = w, dh = h;
            if (style != 2)
            {
                BOOL wider = (LONGLONG)pw * h > (LONGLONG)w * ph;
                if (wider == (style == 6)) dh = MulDiv(ph, w, pw);
                else dw = MulDiv(pw, h, ph);
            }
            StretchBlt(dc, (w - dw) / 2, (h - dh) / 2, dw, dh, src, 0, 0, pw, ph, SRCCOPY);
        }
        else BitBlt(dc, (w - pw) / 2, (h - ph) / 2, pw, ph, src, 0, 0, SRCCOPY);
        DeleteDC(src);
        DeleteObject(pic);
    }
    DeleteDC(dc);
}

/* ------------------------------------------------------------------ */
/* Items                                                                */

static void make_shown(struct item *it)
{
    RECT r = { 0, 0, cell_w - S(8), 0 }, m;
    int lo, hi, len = lstrlenW(it->label);
    WCHAR buf[MAX_PATH + 2];

    m = r;
    measure_label(it->label, &m);
    if (m.bottom - m.top <= LABEL_LINES * line_h)
    {
        lstrcpyW(it->shown, it->label);
        return;
    }
    /* the longest start of the label that fits with an ellipsis */
    lo = 0;
    hi = len;
    while (lo < hi)
    {
        int mid = (lo + hi + 1) / 2;
        memcpy(buf, it->label, mid * sizeof(WCHAR));
        buf[mid] = 0x2026;
        buf[mid + 1] = 0;
        m = r;
        measure_label(buf, &m);
        if (m.bottom - m.top <= LABEL_LINES * line_h) lo = mid;
        else hi = mid - 1;
    }
    memcpy(it->shown, it->label, lo * sizeof(WCHAR));
    it->shown[lo] = 0x2026;
    it->shown[lo + 1] = 0;
}

static void fill_item(struct item *it)
{
    SHFILEINFOW sfi;
    memset(&sfi, 0, sizeof(sfi));
    it->icon = -1;
    if (SHGetFileInfoW(it->path, 0, &sfi, sizeof(sfi), SHGFI_DISPLAYNAME | SHGFI_SYSICONINDEX))
    {
        it->icon = sfi.iIcon;
        lstrcpynW(it->label, sfi.szDisplayName[0] ? sfi.szDisplayName : it->file, MAX_PATH);
    }
    else lstrcpynW(it->label, it->file, MAX_PATH);
    if (it->icon < 0 &&
        SHGetFileInfoW(it->file, it->dir ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL, &sfi, sizeof(sfi),
                       SHGFI_USEFILEATTRIBUTES | SHGFI_SYSICONINDEX))
        it->icon = sfi.iIcon;
    make_shown(it);
}

static void position_name(const struct item *it, WCHAR *name)
{
    _snwprintf(name, MAX_PATH + 3, L"%lc:%ls", it->common ? L'c' : L'u', it->file);
    name[MAX_PATH + 2] = 0;
}

static BOOL load_position(const struct item *it, int *col, int *row)
{
    WCHAR name[MAX_PATH + 3];
    DWORD v, size = sizeof(v);
    position_name(it, name);
    if (RegGetValueW(HKEY_CURRENT_USER, POS_KEY, name, RRF_RT_REG_DWORD, NULL, &v, &size)) return FALSE;
    *col = LOWORD(v);
    *row = HIWORD(v);
    return *row < grid_rows && *col < 512;
}

static void save_position(const struct item *it)
{
    WCHAR name[MAX_PATH + 3];
    DWORD v = MAKELONG(it->col, it->row);
    HKEY key;
    position_name(it, name);
    if (RegCreateKeyExW(HKEY_CURRENT_USER, POS_KEY, 0, NULL, 0, KEY_SET_VALUE, NULL, &key, NULL)) return;
    RegSetValueExW(key, name, 0, REG_DWORD, (const BYTE *)&v, sizeof(v));
    RegCloseKey(key);
}

static void forget_position(const struct item *it)
{
    WCHAR name[MAX_PATH + 3];
    position_name(it, name);
    RegDeleteKeyValueW(HKEY_CURRENT_USER, POS_KEY, name);
}

static BOOL add_found(const WCHAR *dir, const WIN32_FIND_DATAW *fd, BOOL common)
{
    struct item *it;
    if (!wcscmp(fd->cFileName, L".") || !wcscmp(fd->cFileName, L"..")) return FALSE;
    if (fd->dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) return FALSE;
    if (!lstrcmpiW(fd->cFileName, L"desktop.ini")) return FALSE;
    if (nitems == items_cap)
    {
        int cap = items_cap ? items_cap * 2 : 64;
        struct item *n = realloc(items, cap * sizeof(*n));
        if (!n) return FALSE;
        items = n;
        items_cap = cap;
    }
    it = &items[nitems++];
    memset(it, 0, sizeof(*it));
    lstrcpynW(it->file, fd->cFileName, MAX_PATH);
    join_path(it->path, dir, fd->cFileName);
    it->attrs = fd->dwFileAttributes;
    it->dir = (fd->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    it->link = !it->dir && !lstrcmpiW(PathFindExtensionW(it->file), L".lnk");
    it->common = common;
    return TRUE;
}

static int __cdecl cmp_items(const void *a, const void *b)
{
    const struct item *x = a, *y = b;
    int c;
    if (x->dir != y->dir) return x->dir ? -1 : 1;
    if ((c = StrCmpLogicalW(x->label, y->label))) return c;
    return x->common - y->common;
}

static void write_state(void);

/* Read both folders again. Items that were already there keep their cell;
 * new ones take their saved cell if it is free, else the first free cell
 * (top to bottom, then left to right), in name order. */
static void rescan(void)
{
    struct item *old = items;
    int nold = nitems, i, j, f;
    WCHAR sel_path[MAX_PATH] = L"", ren_path[MAX_PATH] = L"";

    if (sel >= 0 && sel < nitems) lstrcpyW(sel_path, items[sel].path);
    if (renaming >= 0 && renaming < nitems) lstrcpyW(ren_path, items[renaming].path);
    items = NULL;
    nitems = items_cap = 0;
    no_desktop = policy_no_desktop();
    for (f = 0; f < 2 && !no_desktop; f++)
    {
        WCHAR glob[MAX_PATH];
        WIN32_FIND_DATAW fd;
        HANDLE h;
        if (!folders[f][0]) continue;
        join_path(glob, folders[f], L"*");
        h = FindFirstFileExW(glob, FindExInfoBasic, &fd, FindExSearchNameMatch, NULL, 0);
        if (h == INVALID_HANDLE_VALUE) continue;
        do add_found(folders[f], &fd, f == 1); while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    for (i = 0; i < nitems; i++) fill_item(&items[i]);
    if (nitems) qsort(items, nitems, sizeof(*items), cmp_items);

    /* keep the cells of known items */
    for (i = 0; i < nitems; i++)
        for (j = 0; j < nold; j++)
            if (old[j].placed && !lstrcmpiW(old[j].path, items[i].path))
            {
                items[i].col = old[j].col;
                items[i].row = old[j].row;
                items[i].placed = old[j].row < grid_rows && item_at_cell(old[j].col, old[j].row, i) < 0;
                break;
            }
    /* a new item made at a place (New, a drop) */
    for (i = 0; i < nitems && pending_path[0]; i++)
        if (!items[i].placed && !lstrcmpiW(items[i].path, pending_path) &&
            item_at_cell(pending_cell.x, pending_cell.y, i) < 0)
        {
            items[i].col = pending_cell.x;
            items[i].row = pending_cell.y;
            items[i].placed = TRUE;
            save_position(&items[i]);
        }
    pending_path[0] = 0;
    /* saved cells */
    for (i = 0; i < nitems; i++)
    {
        int c, r;
        if (!items[i].placed && load_position(&items[i], &c, &r) && item_at_cell(c, r, i) < 0)
        {
            items[i].col = c;
            items[i].row = r;
            items[i].placed = TRUE;
        }
    }
    for (i = 0; i < nitems; i++)
        if (!items[i].placed)
        {
            first_free_cell(&items[i].col, &items[i].row);
            items[i].placed = TRUE;
        }
    free(old);

    sel = renaming = -1;
    for (i = 0; i < nitems; i++)
    {
        if (sel_path[0] && !lstrcmpiW(items[i].path, sel_path)) sel = i;
        if (ren_path[0] && !lstrcmpiW(items[i].path, ren_path)) renaming = i;
    }
    if (renaming < 0 && edit_wnd)
    {
        /* the item being renamed went away */
        rename_closing = TRUE;
        DestroyWindow(edit_wnd);
        edit_wnd = NULL;
        rename_closing = FALSE;
    }
    hot = -1;
    rescans++;
    if (desk) InvalidateRect(desk, NULL, FALSE);
    write_state();
}

static int find_item(const WCHAR *name)
{
    int i;
    for (i = 0; i < nitems; i++)
        if (!lstrcmpiW(items[i].label, name) || !lstrcmpiW(items[i].file, name) || !lstrcmpiW(items[i].path, name))
            return i;
    return -1;
}

static void select_item(int i)
{
    if (i == sel) return;
    invalidate_item(sel);
    sel = i;
    invalidate_item(sel);
    write_state();
}

/* ------------------------------------------------------------------ */
/* Drawing                                                              */

/* Text with a soft dark shadow under it, readable on any wallpaper. */
static void shadow_text(struct setu_canvas *cv, const WCHAR *s, const RECT *r, UINT fmt)
{
    struct setu_canvas m;
    int pad = S(2) + 1, w = r->right - r->left + 2 * pad, h = r->bottom - r->top + 2 * pad;
    int x, y, ox = r->left - pad + max(1, S(1) / 2), oy = r->top - pad + max(1, S(1));
    BYTE *cov;
    RECT rr = { pad, pad, pad + r->right - r->left, pad + r->bottom - r->top };
    HGDIOBJ of;

    if (w <= 0 || h <= 0 || !st_canvas_begin(&m, w, h)) return;
    cov = malloc((size_t)w * h);
    if (!cov) { st_canvas_end(&m); return; }
    of = SelectObject(m.dc, st_font[SF_BODY]);
    SetTextColor(m.dc, RGB(255, 255, 255));
    DrawTextW(m.dc, s, -1, &rr, fmt | DT_NOPREFIX);
    SelectObject(m.dc, of);
    GdiFlush();
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
        {
            DWORD p = m.bits[y * w + x];
            cov[y * w + x] = (BYTE)max(max((p >> 16) & 0xff, (p >> 8) & 0xff), p & 0xff);
        }
    GdiFlush();
    for (y = 0; y < h; y++)
    {
        int cy = oy + y;
        if (cy < cv->clip.top || cy >= cv->clip.bottom) continue;
        for (x = 0; x < w; x++)
        {
            int cx = ox + x, dx, dy, sum = 0, a;
            if (cx < cv->clip.left || cx >= cv->clip.right) continue;
            for (dy = -1; dy <= 1; dy++)
                for (dx = -1; dx <= 1; dx++)
                {
                    int sx = x + dx, sy = y + dy;
                    if (sx >= 0 && sy >= 0 && sx < w && sy < h) sum += cov[sy * w + sx];
                }
            a = min(255, sum / 9 * 3 / 2 + cov[y * w + x] / 2) * 210 / 255;
            if (a) st_blend_px(&cv->bits[(size_t)cy * cv->w + cx], 0, (unsigned)a);
        }
    }
    free(cov);
    st_canvas_end(&m);
}

/* a small shortcut badge in the icon's bottom-left corner */
static void draw_link_badge(struct setu_canvas *cv, int x, int y)
{
    int b = max(12, icon_px * 3 / 8), g = b - S(4);
    RECT r = { x, y + icon_px - b, x + b, y + icon_px };
    const BYTE *mask;
    int i, j;
    st_round(cv, &r, S(3), RGB(255, 255, 255));
    st_frame(cv, &r, S(3), 1, st_mix(RGB(255, 255, 255), RGB(0, 0, 0), 90));
    mask = st_glyph_mask(ICON_EXTERNAL_LINK, g);
    if (!mask) return;
    GdiFlush();
    for (j = 0; j < g; j++)
        for (i = 0; i < g; i++)
        {
            int px = r.left + (b - g) / 2 + i, py = r.top + (b - g) / 2 + j;
            if (px < cv->clip.left || px >= cv->clip.right || py < cv->clip.top || py >= cv->clip.bottom) continue;
            st_blend_px(&cv->bits[(size_t)py * cv->w + px], st_px(ST->accent), mask[j * g + i]);
        }
}

static void draw_item(struct setu_canvas *cv, int i)
{
    const struct item *it = &items[i];
    BOOL is_sel = i == sel, full = is_sel && renaming != i;
    RECT box, lr;
    int x, y, lines = full ? max(LABEL_LINES, label_lines(i)) : LABEL_LINES;
    UINT fmt = DT_CENTER | DT_WORDBREAK | DT_EDITCONTROL;

    item_rect(i, full, &box);
    if (box.right < cv->clip.left || box.left > cv->clip.right ||
        box.bottom < cv->clip.top || box.top > cv->clip.bottom) return;
    icon_pos(i, &x, &y);
    if (is_sel)
    {
        /* the theme's accent: a translucent fill and a solid outline,
         * fainter while the desktop doesn't have the focus */
        st_round_alpha(cv, &box, S(ST_RADIUS_CONTROL), ST->accent, focused ? 110 : 70);
        st_frame(cv, &box, S(ST_RADIUS_CONTROL), 1, focused ? ST->selection_border :
                 st_mix(ST->selection_border, RGB(255, 255, 255), 100));
    }
    else if (i == hot && !dragging)
        st_round_alpha(cv, &box, S(ST_RADIUS_CONTROL), RGB(255, 255, 255), 40);

    if (il && it->icon >= 0)
    {
        GdiFlush();
        ImageList_Draw(il, it->icon, cv->dc, x, y, ILD_TRANSPARENT);
    }
    else st_icon(cv, it->dir ? TI_FOLDER : TI_FILE, x, y, icon_px, RGB(255, 255, 255));
    if (it->link) draw_link_badge(cv, x, y);

    if (renaming == i) return;
    label_rect(i, lines, &lr);
    if (!full) fmt |= DT_END_ELLIPSIS;
    shadow_text(cv, full ? it->label : it->shown, &lr, fmt);
    st_text(cv, full ? it->label : it->shown, &lr, SF_BODY, RGB(255, 255, 255), fmt);
}

static void paint(HDC dc, const RECT *upd)
{
    RECT rc;
    int i;
    GetClientRect(desk, &rc);
    if (!bb.dc || bb.w != rc.right || bb.h != rc.bottom)
    {
        st_canvas_end(&bb);
        if (!st_canvas_begin(&bb, rc.right, rc.bottom)) return;
    }
    st_clip(&bb, upd);
    if (wall)
    {
        HDC m = CreateCompatibleDC(bb.dc);
        HGDIOBJ o = SelectObject(m, wall);
        BitBlt(bb.dc, upd->left, upd->top, upd->right - upd->left, upd->bottom - upd->top, m, upd->left,
               upd->top, SRCCOPY);
        SelectObject(m, o);
        DeleteDC(m);
    }
    else st_fill(&bb, upd, GetSysColor(COLOR_DESKTOP));
    for (i = 0; i < nitems; i++)
        if (i != sel && !(dragging && i == down_item)) draw_item(&bb, i);
    if (sel >= 0 && !(dragging && sel == down_item)) draw_item(&bb, sel);
    if (dragging && down_item >= 0) draw_item(&bb, down_item);
    st_clip(&bb, NULL);
    GdiFlush();
    BitBlt(dc, upd->left, upd->top, upd->right - upd->left, upd->bottom - upd->top, bb.dc, upd->left, upd->top,
           SRCCOPY);
}

/* ------------------------------------------------------------------ */
/* Actions                                                              */

static BOOL open_in_files(const WCHAR *dir)
{
    WCHAR files[MAX_PATH], args[MAX_PATH + 4];
    SHELLEXECUTEINFOW sei;
    BOOL ok;

    memset(&sei, 0, sizeof(sei));
    sei.cbSize = sizeof(sei);
    sei.hwnd = desk;
    sei.nShow = SW_SHOWNORMAL;
    if (find_program(L"setu-files.exe", files, ARRAYSIZE(files)))
    {
        _snwprintf(args, ARRAYSIZE(args), L"\"%ls\"", dir);
        args[ARRAYSIZE(args) - 1] = 0;
        sei.lpFile = files;
        sei.lpParameters = args;
        ok = ShellExecuteExW(&sei);
        _snwprintf(last_open, ARRAYSIZE(last_open), L"files|%ls", dir);
    }
    else
    {
        sei.lpVerb = L"open";
        sei.lpFile = dir;
        ok = ShellExecuteExW(&sei);
        _snwprintf(last_open, ARRAYSIZE(last_open), L"shell|%ls", dir);
    }
    last_open[ARRAYSIZE(last_open) - 1] = 0;
    if (ok) set_status(L"Opened %ls", dir);
    else set_status(L"Could not open %ls (error %lu)", dir, GetLastError());
    return ok;
}

/* A shortcut's target when it is a folder. */
static BOOL link_folder(const WCHAR *lnk, WCHAR *target)
{
    IShellLinkW *sl = NULL;
    IPersistFile *pf = NULL;
    BOOL ok = FALSE;
    DWORD a;

    if (FAILED(CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkW, (void **)&sl)))
        return FALSE;
    if (SUCCEEDED(IShellLinkW_QueryInterface(sl, &IID_IPersistFile, (void **)&pf)) &&
        SUCCEEDED(IPersistFile_Load(pf, lnk, STGM_READ)) &&
        SUCCEEDED(IShellLinkW_GetPath(sl, target, MAX_PATH, NULL, 0)) && target[0] &&
        (a = GetFileAttributesW(target)) != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY))
        ok = TRUE;
    if (pf) IPersistFile_Release(pf);
    IShellLinkW_Release(sl);
    return ok;
}

/* Open an item: folders (and shortcuts to folders) in setu-files, the rest
 * with its "open" verb. */
static void open_item(int i)
{
    struct item *it;
    WCHAR target[MAX_PATH], dir[MAX_PATH], *slash;
    SHELLEXECUTEINFOW sei;
    BOOL ok;

    if (i < 0 || i >= nitems) return;
    it = &items[i];
    if (it->dir) { open_in_files(it->path); write_state(); return; }
    if (it->link && link_folder(it->path, target)) { open_in_files(target); write_state(); return; }
    lstrcpyW(dir, it->path);
    if ((slash = wcsrchr(dir, L'\\'))) *slash = 0;
    memset(&sei, 0, sizeof(sei));
    sei.cbSize = sizeof(sei);
    sei.hwnd = desk;
    sei.lpVerb = L"open";
    sei.lpFile = it->path;
    sei.lpDirectory = dir;
    sei.nShow = SW_SHOWNORMAL;
    ok = ShellExecuteExW(&sei);
    if (!ok && GetLastError() == ERROR_NO_ASSOCIATION)
    {
        /* no "open" verb: the default one */
        sei.lpVerb = NULL;
        ok = ShellExecuteExW(&sei);
    }
    _snwprintf(last_open, ARRAYSIZE(last_open), L"shell|%ls", it->path);
    last_open[ARRAYSIZE(last_open) - 1] = 0;
    if (ok) set_status(L"Opened %ls", it->label);
    else set_status(L"Could not open %ls (error %lu)", it->label, GetLastError());
    write_state();
}

static void show_properties(int i)
{
    SHELLEXECUTEINFOW sei;
    if (i < 0 || i >= nitems) return;
    memset(&sei, 0, sizeof(sei));
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_INVOKEIDLIST;
    sei.hwnd = desk;
    sei.lpVerb = L"properties";
    sei.lpFile = items[i].path;
    sei.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&sei)) set_status(L"No properties for %ls (error %lu)", items[i].label, GetLastError());
    write_state();
}

static void delete_item(int i, BOOL permanent)
{
    WCHAR from[MAX_PATH + 2];
    SHFILEOPSTRUCTW op;
    int r;

    if (i < 0 || i >= nitems) return;
    memset(from, 0, sizeof(from));
    lstrcpynW(from, items[i].path, MAX_PATH);
    memset(&op, 0, sizeof(op));
    op.hwnd = desk;
    op.wFunc = FO_DELETE;
    op.pFrom = from;
    op.fFlags = permanent ? 0 : FOF_ALLOWUNDO;
    r = SHFileOperationW(&op);
    if (!r && !op.fAnyOperationsAborted)
    {
        set_status(permanent ? L"Deleted %ls" : L"Moved %ls to the Recycle Bin", items[i].label);
        forget_position(&items[i]);
    }
    else set_status(L"Did not delete %ls (%d)", items[i].label, r);
    rescan();
}

/* ---- renaming in place ---- */

static void end_rename(BOOL commit);

static LRESULT CALLBACK edit_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_GETDLGCODE:
        return DLGC_WANTALLKEYS;
    case WM_KEYDOWN:
        if (wp == VK_RETURN) { end_rename(TRUE); return 0; }
        if (wp == VK_ESCAPE) { end_rename(FALSE); return 0; }
        break;
    case WM_CHAR:
        if (wp == L'\r' || wp == 27) return 0;
        break;
    case WM_KILLFOCUS:
        if (!rename_closing) PostMessageW(desk, WM_APP_RESCAN, 1, 0); /* commit once the focus moved */
        break;
    }
    return CallWindowProcW(edit_orig_proc, hwnd, msg, wp, lp);
}

static void begin_rename(int i)
{
    RECT lr;
    const struct item *it;
    int len, ext;

    if (i < 0 || i >= nitems) return;
    if (edit_wnd) end_rename(TRUE);
    select_item(i);
    it = &items[i];
    renaming = i;
    label_rect(i, 1, &lr);
    InflateRect(&lr, S(8), S(3));
    edit_wnd = CreateWindowExW(0, L"EDIT", it->label, WS_CHILD | WS_BORDER | ES_CENTER | ES_AUTOHSCROLL,
                               lr.left, lr.top, lr.right - lr.left, lr.bottom - lr.top, desk, NULL, hinst, NULL);
    if (!edit_wnd) { renaming = -1; return; }
    SendMessageW(edit_wnd, WM_SETFONT, (WPARAM)st_font[SF_BODY], FALSE);
    SendMessageW(edit_wnd, EM_LIMITTEXT, MAX_PATH - 1 - lstrlenW(folders[it->common]) - 1, 0);
    edit_orig_proc = (WNDPROC)SetWindowLongPtrW(edit_wnd, GWLP_WNDPROC, (LONG_PTR)edit_proc);
    /* a file's name is selected without its extension */
    len = lstrlenW(it->label);
    ext = it->dir ? len : (int)(PathFindExtensionW(it->label) - it->label);
    SendMessageW(edit_wnd, EM_SETSEL, 0, ext > 0 ? ext : len);
    ShowWindow(edit_wnd, SW_SHOW);
    SetFocus(edit_wnd);
    invalidate_item(i);
    write_state();
}

static void end_rename(BOOL commit)
{
    WCHAR text[MAX_PATH], newname[MAX_PATH], newpath[MAX_PATH], dir[MAX_PATH], msg[600], err[300];
    struct item *it;
    int i = renaming, n;
    const WCHAR *ext;

    if (!edit_wnd || rename_closing) return;
    GetWindowTextW(edit_wnd, text, ARRAYSIZE(text));
    rename_closing = TRUE;
    DestroyWindow(edit_wnd);
    edit_wnd = NULL;
    rename_closing = FALSE;
    renaming = -1;
    if (desk && GetFocus() != desk && GetForegroundWindow() == desk) SetFocus(desk);
    if (i < 0 || i >= nitems) { write_state(); return; }
    invalidate_item(i);
    it = &items[i];
    /* trailing spaces and dots, and leading spaces, aren't part of a name */
    n = lstrlenW(text);
    while (n && (text[n - 1] == L' ' || text[n - 1] == L'.')) text[--n] = 0;
    while (text[0] == L' ') memmove(text, text + 1, (lstrlenW(text) + 1) * sizeof(WCHAR));
    if (!commit || !text[0] || !wcscmp(text, it->label)) { write_state(); return; }
    if (wcspbrk(text, L"\\/:*?\"<>|"))
    {
        MessageBoxW(desk, L"A name can't contain any of these characters:\n\\ / : * ? \" < > |", L"Rename",
                    MB_OK | MB_ICONWARNING);
        write_state();
        return;
    }
    /* a hidden extension stays */
    ext = PathFindExtensionW(it->file);
    if (!it->dir && lstrcmpiW(it->label, it->file) && ext[0] && lstrcmpiW(PathFindExtensionW(it->label), ext))
        _snwprintf(newname, MAX_PATH, L"%ls%ls", text, ext);
    else lstrcpynW(newname, text, MAX_PATH);
    newname[MAX_PATH - 1] = 0;
    lstrcpyW(dir, folders[it->common]);
    join_path(newpath, dir, newname);
    if (!MoveFileW(it->path, newpath))
    {
        error_text(GetLastError(), err, ARRAYSIZE(err));
        _snwprintf(msg, ARRAYSIZE(msg), L"Could not rename \"%ls\" to \"%ls\".\n\n%ls", it->label, newname, err);
        msg[ARRAYSIZE(msg) - 1] = 0;
        MessageBoxW(desk, msg, L"Rename", MB_OK | MB_ICONERROR);
        write_state();
        return;
    }
    SHChangeNotify(it->dir ? SHCNE_RENAMEFOLDER : SHCNE_RENAMEITEM, SHCNF_PATHW, it->path, newpath);
    set_status(L"Renamed %ls to %ls", it->file, newname);
    /* the item keeps its place */
    forget_position(it);
    lstrcpyW(it->path, newpath);
    lstrcpyW(it->file, newname);
    save_position(it);
    rescan();
}

/* ---- new items, paste, drop ---- */

static void place_next(const WCHAR *path, POINT *at)
{
    int col, row;
    if (!at) return;
    lstrcpynW(pending_path, path, MAX_PATH);
    point_to_cell(*at, &col, &row);
    pending_cell.x = col;
    pending_cell.y = row;
}

static void new_item(BOOL folder, POINT *at)
{
    const WCHAR *base = folder ? L"New folder" : L"New Text Document", *ext = folder ? L"" : L".txt";
    WCHAR name[MAX_PATH], path[MAX_PATH];
    int n, i;
    BOOL ok = FALSE;

    if (!folders[0][0]) return;
    for (n = 1; n < 1000 && !ok; n++)
    {
        if (n == 1) _snwprintf(name, ARRAYSIZE(name), L"%ls%ls", base, ext);
        else _snwprintf(name, ARRAYSIZE(name), L"%ls (%d)%ls", base, n, ext);
        name[ARRAYSIZE(name) - 1] = 0;
        join_path(path, folders[0], name);
        if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) continue;
        if (folder) ok = CreateDirectoryW(path, NULL);
        else
        {
            HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
            ok = h != INVALID_HANDLE_VALUE;
            if (ok) CloseHandle(h);
        }
        if (!ok) break;
    }
    if (!ok)
    {
        set_status(L"Could not create %ls (error %lu)", name, GetLastError());
        write_state();
        return;
    }
    SHChangeNotify(folder ? SHCNE_MKDIR : SHCNE_CREATE, SHCNF_PATHW, path, NULL);
    place_next(path, at);
    rescan();
    if ((i = find_item(path)) >= 0) begin_rename(i);
}

/* Copy (or move) files into the Desktop folder. */
static void put_files(const WCHAR *list, BOOL move, POINT *at)
{
    SHFILEOPSTRUCTW op;
    WCHAR to[MAX_PATH + 2], first[MAX_PATH], *name;
    int r;

    memset(to, 0, sizeof(to));
    lstrcpynW(to, folders[0], MAX_PATH);
    memset(&op, 0, sizeof(op));
    op.hwnd = desk;
    op.wFunc = move ? FO_MOVE : FO_COPY;
    op.pFrom = list;
    op.pTo = to;
    op.fFlags = FOF_ALLOWUNDO;
    /* pasting a file next to itself makes a copy with a new name */
    lstrcpynW(first, list, MAX_PATH);
    name = PathFindFileNameW(first);
    if (name > first)
    {
        name[-1] = 0;
        if (!lstrcmpiW(first, folders[0])) op.fFlags |= FOF_RENAMEONCOLLISION;
        name[-1] = L'\\';
    }
    if (at && !list[lstrlenW(list) + 1])
    {
        WCHAR dest[MAX_PATH];
        join_path(dest, folders[0], PathFindFileNameW(list));
        place_next(dest, at);
    }
    r = SHFileOperationW(&op);
    set_status(r ? L"Could not put the files on the desktop (%d)" : L"Put the files on the desktop", r);
    rescan();
}

static WCHAR *hdrop_list(HDROP drop)
{
    UINT n = DragQueryFileW(drop, 0xFFFFFFFF, NULL, 0), i;
    size_t len = 1, pos = 0;
    WCHAR *list;
    for (i = 0; i < n; i++) len += DragQueryFileW(drop, i, NULL, 0) + 1;
    if (!n || !(list = calloc(len + 1, sizeof(WCHAR)))) return NULL;
    for (i = 0; i < n; i++) pos += DragQueryFileW(drop, i, list + pos, (UINT)(len - pos)) + 1;
    return list;
}

static void paste(POINT *at)
{
    HDROP drop;
    WCHAR *list = NULL;
    BOOL move = FALSE;
    UINT effect_fmt = RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT);

    if (!OpenClipboard(desk)) return;
    if ((drop = GetClipboardData(CF_HDROP))) list = hdrop_list(drop);
    if (effect_fmt && IsClipboardFormatAvailable(effect_fmt))
    {
        HGLOBAL g = GetClipboardData(effect_fmt);
        DWORD *e = g ? GlobalLock(g) : NULL;
        if (e) { move = (*e & DROPEFFECT_MOVE) != 0; GlobalUnlock(g); }
    }
    if (move && list) EmptyClipboard();   /* a cut is pasted once */
    CloseClipboard();
    if (list) put_files(list, move, at);
    free(list);
}

static BOOL same_volume(const WCHAR *a, const WCHAR *b)
{
    WCHAR va[MAX_PATH], vb[MAX_PATH];
    if (!GetVolumePathNameW(a, va, MAX_PATH) || !GetVolumePathNameW(b, vb, MAX_PATH)) return FALSE;
    return !lstrcmpiW(va, vb);
}

/* ---- menus ---- */

static int track_menu(HMENU m, POINT pt)
{
    WCHAR text[128];
    int i, n = GetMenuItemCount(m), len = 0, cmd;

    menu_text[0] = 0;
    for (i = 0; i < n; i++)
    {
        MENUITEMINFOW mi = { sizeof(mi) };
        mi.fMask = MIIM_STRING | MIIM_FTYPE | MIIM_SUBMENU;
        mi.dwTypeData = text;
        mi.cch = ARRAYSIZE(text);
        if (!GetMenuItemInfoW(m, i, TRUE, &mi) || (mi.fType & MFT_SEPARATOR)) continue;
        len += _snwprintf(menu_text + len, ARRAYSIZE(menu_text) - len - 1, L"%ls%ls", len ? L"|" : L"", text);
        if (len >= (int)ARRAYSIZE(menu_text) - 1) break;
    }
    menu_text[ARRAYSIZE(menu_text) - 1] = 0;
    write_state();
    SetForegroundWindow(desk);
    cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, pt.x, pt.y, 0, desk, NULL);
    PostMessageW(desk, WM_NULL, 0, 0);
    menu_text[0] = 0;
    write_state();
    return cmd;
}

static void item_menu(int i, POINT screen)
{
    HMENU m = CreatePopupMenu();
    int cmd;
    AppendMenuW(m, MF_STRING, IDM_OPEN, L"&Open");
    SetMenuDefaultItem(m, IDM_OPEN, FALSE);
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, IDM_RENAME, L"Rena&me");
    AppendMenuW(m, MF_STRING, IDM_DELETE, L"&Delete");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, IDM_PROPERTIES, L"P&roperties");
    cmd = track_menu(m, screen);
    DestroyMenu(m);
    if (i >= nitems) return;
    switch (cmd)
    {
    case IDM_OPEN: open_item(i); break;
    case IDM_RENAME: begin_rename(i); break;
    case IDM_DELETE: delete_item(i, FALSE); break;
    case IDM_PROPERTIES: show_properties(i); break;
    }
}

static void background_menu(POINT screen)
{
    HMENU m = CreatePopupMenu(), sub = CreatePopupMenu();
    POINT at = { screen.x - origin.x, screen.y - origin.y };
    int cmd;
    AppendMenuW(m, MF_STRING, IDM_ARRANGE, L"&Arrange by name");
    AppendMenuW(m, MF_STRING, IDM_REFRESH, L"R&efresh");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING | (IsClipboardFormatAvailable(CF_HDROP) ? 0 : MF_GRAYED), IDM_PASTE, L"&Paste");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(sub, MF_STRING, IDM_NEW_FOLDER, L"&Folder");
    AppendMenuW(sub, MF_STRING, IDM_NEW_TEXT, L"&Text Document");
    AppendMenuW(m, MF_POPUP, (UINT_PTR)sub, L"Ne&w");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, IDM_OPEN_DESK, L"&Open Desktop folder");
    cmd = track_menu(m, screen);
    DestroyMenu(m);
    switch (cmd)
    {
    case IDM_ARRANGE:
    {
        int i;
        RegDeleteTreeW(HKEY_CURRENT_USER, POS_KEY);
        for (i = 0; i < nitems; i++) items[i].placed = FALSE;
        rescan();
        set_status(L"Arranged by name");
        break;
    }
    case IDM_REFRESH: load_wallpaper(); rescan(); set_status(L"Refreshed"); break;
    case IDM_PASTE: paste(&at); break;
    case IDM_NEW_FOLDER: new_item(TRUE, &at); break;
    case IDM_NEW_TEXT: new_item(FALSE, &at); break;
    case IDM_OPEN_DESK: open_in_files(folders[0]); break;
    }
    write_state();
}

/* ---- keyboard ---- */

/* the nearest item from SEL in direction DX, DY (one of them 0) */
static int neighbour(int dx, int dy)
{
    int i, best = -1, best_score = 0;
    if (sel < 0) return nitems ? 0 : -1;
    for (i = 0; i < nitems; i++)
    {
        int c = items[i].col - items[sel].col, r = items[i].row - items[sel].row, along, across, score;
        if (i == sel) continue;
        along = dx ? c * dx : r * dy;
        across = abs(dx ? r : c);
        if (along <= 0) continue;
        if (dy && across) continue;           /* up and down stay in the column */
        score = along * 1000 + across;
        if (best < 0 || score < best_score) { best = i; best_score = score; }
    }
    return best;
}

static void key_down(WPARAM vk)
{
    BOOL ctrl = GetKeyState(VK_CONTROL) < 0, shift = GetKeyState(VK_SHIFT) < 0;
    int n;
    switch (vk)
    {
    case VK_LEFT: if ((n = neighbour(-1, 0)) >= 0) select_item(n); break;
    case VK_RIGHT: if ((n = neighbour(1, 0)) >= 0) select_item(n); break;
    case VK_UP: if ((n = neighbour(0, -1)) >= 0) select_item(n); break;
    case VK_DOWN: if ((n = neighbour(0, 1)) >= 0) select_item(n); break;
    case VK_RETURN: open_item(sel); break;
    case VK_F2: begin_rename(sel); break;
    case VK_DELETE: delete_item(sel, shift); break;
    case VK_F5: load_wallpaper(); rescan(); break;
    case VK_ESCAPE: select_item(-1); break;
    case 'V': if (ctrl) paste(NULL); break;
    }
}

/* ------------------------------------------------------------------ */
/* State for tests                                                      */

static void write_state(void)
{
    static WCHAR buf[65536];
    static char utf8[196608];
    WCHAR tmp[MAX_PATH + 8];
    RECT wr;
    int len = 0, i;
    HANDLE h;
    DWORD done;
    HWND below;

    if (!state_path[0]) return;
#define EMIT(...) do { int room_ = (int)ARRAYSIZE(buf) - len - 1; if (room_ > 0) { int n_ = _snwprintf(buf + len, room_, __VA_ARGS__); len += n_ >= 0 ? n_ : room_; } } while (0)
    GetWindowRect(desk, &wr);
    /* any visible top-level window under us? */
    for (below = GetWindow(desk, GW_HWNDNEXT); below && !IsWindowVisible(below); below = GetWindow(below, GW_HWNDNEXT));
    EMIT(L"seq=%d\r\n", ++state_seq);
    EMIT(L"dpi=%u\r\n", dpi);
    EMIT(L"window=%ld,%ld,%ld,%ld\r\n", wr.left, wr.top, wr.right, wr.bottom);
    EMIT(L"bottom=%d\r\n", below == NULL);
    EMIT(L"workarea=%ld,%ld,%ld,%ld\r\n", work.left + origin.x, work.top + origin.y, work.right + origin.x,
         work.bottom + origin.y);
    EMIT(L"cell=%d,%d\r\n", cell_w, cell_h);
    EMIT(L"icon_size=%d\r\n", icon_px);
    EMIT(L"rows=%d\r\n", grid_rows);
    EMIT(L"folder=user|%ls\r\n", folders[0]);
    EMIT(L"folder=common|%ls\r\n", folders[1]);
    EMIT(L"watching=%d\r\n", nwatching);
    EMIT(L"shnotify=%d\r\n", shnotify_id != 0);
    EMIT(L"no_desktop=%d\r\n", no_desktop);
    EMIT(L"wallpaper=%d\r\n", wall != NULL);
    EMIT(L"rescans=%d\r\n", rescans);
    EMIT(L"theme=%ls\r\n", setu_theme.id);
    EMIT(L"theme_reloads=%d\r\n", theme_reloads);
    EMIT(L"accent=#%02X%02X%02X\r\n", GetRValue(ST->accent), GetGValue(ST->accent), GetBValue(ST->accent));
    EMIT(L"focused=%d\r\n", focused);
    /* item=label|file|kind|source|icon centre x,y|label centre x,y|col,row|icon|selected */
    for (i = 0; i < nitems; i++)
    {
        const struct item *it = &items[i];
        RECT lr;
        int x, y;
        icon_pos(i, &x, &y);
        label_rect(i, LABEL_LINES, &lr);
        EMIT(L"item=%ls|%ls|%ls|%ls|%ld,%ld|%ld,%ld|%d,%d|%ls|%ls\r\n", it->label, it->file,
             it->dir ? L"dir" : it->link ? L"link" : L"file", it->common ? L"common" : L"user",
             x + icon_px / 2 + origin.x, y + icon_px / 2 + origin.y,
             (lr.left + lr.right) / 2 + origin.x, lr.top + line_h / 2 + origin.y, it->col, it->row,
             it->icon >= 0 ? L"icon" : L"glyph", i == sel ? L"selected" : L"-");
    }
    EMIT(L"selected=%ls\r\n", sel >= 0 ? items[sel].label : L"");
    EMIT(L"renaming=%d\r\n", renaming >= 0);
    EMIT(L"menu=%ls\r\n", menu_text);
    EMIT(L"last_open=%ls\r\n", last_open);
    EMIT(L"status=%ls\r\n", status_text);
    EMIT(L"end=1\r\n");
#undef EMIT
    buf[len] = 0;
    len = WideCharToMultiByte(CP_UTF8, 0, buf, len, utf8, sizeof(utf8), NULL, NULL);
    _snwprintf(tmp, ARRAYSIZE(tmp), L"%ls.tmp", state_path);
    tmp[ARRAYSIZE(tmp) - 1] = 0;
    h = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    WriteFile(h, utf8, len, &done, NULL);
    CloseHandle(h);
    MoveFileExW(tmp, state_path, MOVEFILE_REPLACE_EXISTING);
}

/* ------------------------------------------------------------------ */
/* Watching the folders                                                 */

static DWORD WINAPI watch_thread(void *arg)
{
    HANDLE dirs[2], events[3];
    OVERLAPPED ov[2];
    static DWORD buf[2][2048];
    int n = 0, i, f;
    const DWORD filter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_ATTRIBUTES;

    (void)arg;
    for (f = 0; f < 2; f++)
    {
        HANDLE h;
        if (!folders[f][0]) continue;
        h = CreateFileW(folders[f], FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, NULL);
        if (h == INVALID_HANDLE_VALUE) continue;
        memset(&ov[n], 0, sizeof(ov[n]));
        ov[n].hEvent = events[n] = CreateEventW(NULL, FALSE, FALSE, NULL);
        dirs[n] = h;
        if (!ReadDirectoryChangesW(h, buf[n], sizeof(buf[n]), FALSE, filter, NULL, &ov[n], NULL))
        {
            CloseHandle(h);
            CloseHandle(events[n]);
            continue;
        }
        n++;
    }
    nwatching = n;
    PostMessageW(desk, WM_APP_RESCAN, 2, 0); /* report the watches */
    events[n] = watch_stop;
    for (;;)
    {
        DWORD r = WaitForMultipleObjects(n + 1, events, FALSE, INFINITE), got;
        if (r >= WAIT_OBJECT_0 + (DWORD)n) break;
        i = (int)(r - WAIT_OBJECT_0);
        GetOverlappedResult(dirs[i], &ov[i], &got, FALSE);
        PostMessageW(desk, WM_APP_RESCAN, 0, 0);
        if (!ReadDirectoryChangesW(dirs[i], buf[i], sizeof(buf[i]), FALSE, filter, NULL, &ov[i], NULL))
            Sleep(500); /* the folder went away; the next rescan copes */
    }
    for (i = 0; i < n; i++)
    {
        CancelIo(dirs[i]);
        CloseHandle(dirs[i]);
        CloseHandle(events[i]);
    }
    return 0;
}

static void register_shnotify(void)
{
    SHChangeNotifyEntry e = { NULL, TRUE };
    shnotify_id = SHChangeNotifyRegister(desk, SHCNRF_ShellLevel | SHCNRF_InterruptLevel | SHCNRF_NewDelivery,
                                         SHCNE_ASSOCCHANGED | SHCNE_UPDATEIMAGE, WM_APP_SHNOTIFY, 1, &e);
}

/* ------------------------------------------------------------------ */
/* Window                                                               */

static void fit_screen(void)
{
    SetWindowPos(desk, HWND_BOTTOM, GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
                 GetSystemMetrics(SM_CXVIRTUALSCREEN), GetSystemMetrics(SM_CYVIRTUALSCREEN), SWP_NOACTIVATE);
    layout();
    load_wallpaper();
}

static void apply_theme(void)
{
    st_theme_reload();
    st_make_fonts(dpi);
    theme_reloads++;
    if (edit_wnd) SendMessageW(edit_wnd, WM_SETFONT, (WPARAM)st_font[SF_BODY], TRUE);
    layout();
    load_wallpaper();
    rescan();
}

BOOL apply_commands(int argc, WCHAR **argv);

static LRESULT CALLBACK desk_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        if (!IsRectEmpty(&ps.rcPaint)) paint(dc, &ps.rcPaint);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_WINDOWPOSCHANGING:
    {
        /* always below every other window */
        WINDOWPOS *pos = (WINDOWPOS *)lp;
        pos->hwndInsertAfter = HWND_BOTTOM;
        pos->flags &= ~SWP_NOZORDER;
        return 0;
    }
    case WM_SETFOCUS:
        focused = TRUE;
        invalidate_item(sel);
        write_state();
        return 0;
    case WM_KILLFOCUS:
        focused = FALSE;
        invalidate_item(sel);
        write_state();
        return 0;
    case WM_MOUSEMOVE:
    {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        int h;
        if (!tracking_mouse)
        {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
            tracking_mouse = TrackMouseEvent(&tme);
        }
        if (down_item >= 0 && GetCapture() == hwnd)
        {
            if (!dragging && (abs(pt.x - down_pt.x) > GetSystemMetrics(SM_CXDRAG) ||
                              abs(pt.y - down_pt.y) > GetSystemMetrics(SM_CYDRAG)))
                dragging = TRUE;
            if (dragging)
            {
                invalidate_item(down_item);
                drag_pt = pt;
                invalidate_item(down_item);
            }
            return 0;
        }
        h = hit_test(pt);
        if (h != hot)
        {
            invalidate_item(hot);
            hot = h;
            invalidate_item(hot);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        tracking_mouse = FALSE;
        invalidate_item(hot);
        hot = -1;
        return 0;
    case WM_LBUTTONDOWN:
    {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        int h;
        if (edit_wnd) end_rename(TRUE);
        SetFocus(hwnd);
        h = hit_test(pt);
        select_item(h);
        down_item = h;
        down_pt = drag_pt = pt;
        dragging = FALSE;
        if (h >= 0) SetCapture(hwnd);
        return 0;
    }
    case WM_LBUTTONUP:
    {
        int i = down_item;
        /* release the capture last: WM_CAPTURECHANGED ends the drag */
        if (dragging && i >= 0 && i < nitems)
        {
            /* drop the icon in the cell under its centre; an item already
             * there swaps places with it */
            int x, y, col, row, other;
            POINT c;
            icon_pos(i, &x, &y);
            c.x = x + icon_px / 2;
            c.y = y + icon_px / 2;
            point_to_cell(c, &col, &row);
            invalidate_item(i);
            dragging = FALSE;
            if ((other = item_at_cell(col, row, i)) >= 0)
            {
                invalidate_item(other);
                items[other].col = items[i].col;
                items[other].row = items[i].row;
                save_position(&items[other]);
                invalidate_item(other);
            }
            items[i].col = col;
            items[i].row = row;
            save_position(&items[i]);
            invalidate_item(i);
            write_state();
        }
        dragging = FALSE;
        down_item = -1;
        if (GetCapture() == hwnd) ReleaseCapture();
        return 0;
    }
    case WM_CAPTURECHANGED:
        if (dragging) { invalidate_item(down_item); dragging = FALSE; invalidate_item(down_item); }
        down_item = -1;
        return 0;
    case WM_LBUTTONDBLCLK:
    {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        int h = hit_test(pt);
        if (h >= 0) { select_item(h); open_item(h); }
        return 0;
    }
    case WM_RBUTTONDOWN:
    {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        if (edit_wnd) end_rename(TRUE);
        SetFocus(hwnd);
        select_item(hit_test(pt));
        return 0;
    }
    case WM_RBUTTONUP:
    {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ClientToScreen(hwnd, &pt);
        SendMessageW(hwnd, WM_CONTEXTMENU, (WPARAM)hwnd, MAKELPARAM(pt.x, pt.y));
        return 0;
    }
    case WM_CONTEXTMENU:
    {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        if (no_desktop) return 0;
        if (lp == -1)
        {
            /* from the keyboard: at the selected item, else the top left */
            if (sel >= 0)
            {
                int x, y;
                icon_pos(sel, &x, &y);
                pt.x = x + icon_px / 2;
                pt.y = y + icon_px / 2;
            }
            else { pt.x = work.left + margin; pt.y = work.top + margin; }
            ClientToScreen(hwnd, &pt);
        }
        else
        {
            POINT c = pt;
            ScreenToClient(hwnd, &c);
            if (hit_test(c) != sel) select_item(hit_test(c));
        }
        if (sel >= 0) item_menu(sel, pt);
        else background_menu(pt);
        return 0;
    }
    case WM_KEYDOWN:
        key_down(wp);
        return 0;
    case WM_DROPFILES:
    {
        HDROP drop = (HDROP)wp;
        POINT pt;
        WCHAR *list = hdrop_list(drop);
        DragQueryPoint(drop, &pt);
        DragFinish(drop);
        if (list)
        {
            put_files(list, same_volume(list, folders[0]), &pt);
            free(list);
        }
        return 0;
    }
    case WM_TIMER:
        if (wp == TIMER_RESCAN) { KillTimer(hwnd, TIMER_RESCAN); rescan(); }
        else if (wp == TIMER_EXIT) DestroyWindow(hwnd);
        else if (wp == TIMER_THEME) { KillTimer(hwnd, TIMER_THEME); apply_theme(); }
        return 0;
    case WM_APP_RESCAN:
        if (wp == 1) end_rename(TRUE);         /* the rename box lost the focus */
        else if (wp == 2) write_state();       /* the watches are up */
        else SetTimer(hwnd, TIMER_RESCAN, 150, NULL);
        return 0;
    case WM_APP_SHNOTIFY:
    {
        PIDLIST_ABSOLUTE *pidls;
        LONG event = 0;
        HANDLE lock = SHChangeNotification_Lock((HANDLE)wp, (DWORD)lp, &pidls, &event);
        if (lock) SHChangeNotification_Unlock(lock);
        SetTimer(hwnd, TIMER_RESCAN, 150, NULL);
        return 0;
    }
    case WM_THEMECHANGED:
        SetTimer(hwnd, TIMER_THEME, 50, NULL);
        break;
    case WM_SETTINGCHANGE:
        if (setu_theme_changed_msg(msg, wp, lp)) SetTimer(hwnd, TIMER_THEME, 50, NULL);
        else if (wp == SPI_SETDESKWALLPAPER) { load_wallpaper(); InvalidateRect(hwnd, NULL, FALSE); }
        else if (wp == SPI_SETWORKAREA) { layout(); rescan(); }
        else if (!wp) SetTimer(hwnd, TIMER_RESCAN, 150, NULL); /* policies, HideFileExt, ... */
        return 0;
    case WM_SYSCOLORCHANGE:
        load_wallpaper();
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_DISPLAYCHANGE:
        fit_screen();
        rescan();
        return 0;
    case WM_DPICHANGED:
        dpi = HIWORD(wp);
        st_make_fonts(dpi);
        layout();
        rescan();
        return 0;
    case WM_COPYDATA:
    {
        const COPYDATASTRUCT *cds = (const COPYDATASTRUCT *)lp;
        WCHAR *buf, *argv[32], *p;
        int argc = 0;
        BOOL ok;
        if (!cds || cds->dwData != COPYDATA_MAGIC || cds->cbData < sizeof(WCHAR)) return FALSE;
        if (!(buf = malloc(cds->cbData + sizeof(WCHAR)))) return FALSE;
        memcpy(buf, cds->lpData, cds->cbData);
        buf[cds->cbData / sizeof(WCHAR)] = 0;
        for (p = buf; *p && argc < 32;)
        {
            argv[argc++] = p;
            while (*p && *p != CMD_SEP) p++;
            if (*p) *p++ = 0;
        }
        ok = apply_commands(argc, argv);
        free(buf);
        return ok;
    }
    case WM_CLOSE:
        return 0;   /* Alt+F4 on the desktop shouldn't remove it */
    case WM_DESTROY:
        if (watch_stop) SetEvent(watch_stop);
        if (shnotify_id) SHChangeNotifyDeregister(shnotify_id);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

BOOL apply_commands(int argc, WCHAR **argv)
{
    BOOL ok = TRUE;
    int i;
    for (i = 0; i < argc; i++)
    {
        const WCHAR *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!wcscmp(a, L"--quit")) DestroyWindow(desk);
        else if (!wcscmp(a, L"--refresh")) rescan();
        else if (!wcscmp(a, L"--arrange"))
        {
            int k;
            RegDeleteTreeW(HKEY_CURRENT_USER, POS_KEY);
            for (k = 0; k < nitems; k++) items[k].placed = FALSE;
            rescan();
        }
        else if (v && !wcscmp(a, L"--state-file"))
        {
            GetFullPathNameW(v, ARRAYSIZE(state_path), state_path, NULL);
            i++;
        }
        else if (v && !wcscmp(a, L"--exit-after")) { exit_after = _wtoi(v); i++; }
        else if (v && (!wcscmp(a, L"--select") || !wcscmp(a, L"--open")))
        {
            int k = find_item(v);
            if (k < 0) { set_status(L"No item called \"%ls\"", v); ok = FALSE; }
            else
            {
                select_item(k);
                if (!wcscmp(a, L"--open")) open_item(k);
            }
            i++;
        }
        else { set_status(L"Unknown option %ls", a); ok = FALSE; }
    }
    if (exit_after > 0 && desk) SetTimer(desk, TIMER_EXIT, exit_after * 1000, NULL);
    write_state();
    return ok;
}

static int forward_to_running(int argc, WCHAR **argv)
{
    HWND other = FindWindowW(DESK_CLASS, NULL);
    COPYDATASTRUCT cds;
    WCHAR *buf;
    size_t len = 0;
    int i;
    DWORD_PTR result = 0;

    if (!other) return -1;
    if (argc < 2) return 0;
    for (i = 1; i < argc; i++) len += wcslen(argv[i]) + 1;
    if (!(buf = calloc(len + 1, sizeof(WCHAR)))) return 1;
    for (i = 1; i < argc; i++)
    {
        if (i > 1) buf[wcslen(buf)] = CMD_SEP;
        wcscat(buf, argv[i]);
    }
    cds.dwData = COPYDATA_MAGIC;
    cds.cbData = (DWORD)(wcslen(buf) * sizeof(WCHAR));
    cds.lpData = buf;
    AllowSetForegroundWindow(ASFW_ANY);
    if (!SendMessageTimeoutW(other, WM_COPYDATA, 0, (LPARAM)&cds, SMTO_ABORTIFHUNG, 15000, &result)) result = 0;
    free(buf);
    return result ? 0 : 2;
}

static void known_folder(REFKNOWNFOLDERID id, WCHAR *out)
{
    WCHAR *p = NULL;
    out[0] = 0;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_CREATE, NULL, &p)) && p) lstrcpynW(out, p, MAX_PATH);
    CoTaskMemFree(p);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmdline, int show)
{
    /* IID_IImageList {46EB5926-582E-4017-9FDF-E8998DAA0950} */
    static const GUID iid_imagelist = { 0x46eb5926, 0x582e, 0x4017, { 0x9f, 0xdf, 0xe8, 0x99, 0x8d, 0xaa, 0x09, 0x50 } };
    WNDCLASSEXW wc;
    WCHAR **argv;
    int argc, fwd;
    MSG msg;

    (void)prev; (void)cmdline; (void)show;
    hinst = inst;
    argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    set_dpi_awareness();
    if ((fwd = forward_to_running(argc, argv)) >= 0) return fwd;

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    {
        INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_WIN95_CLASSES };
        InitCommonControlsEx(&icc);
    }
    st_theme_reload();
    known_folder(&FOLDERID_Desktop, folders[0]);
    known_folder(&FOLDERID_PublicDesktop, folders[1]);
    if (FAILED(SHGetImageList(SHIL_EXTRALARGE, &iid_imagelist, (void **)&il)) || !il)
        SHGetImageList(SHIL_LARGE, &iid_imagelist, (void **)&il);

    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpfnWndProc = desk_proc;
    wc.lpszClassName = DESK_CLASS;
    RegisterClassExW(&wc);
    desk = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_ACCEPTFILES, DESK_CLASS, L"Setu desktop",
                           WS_POPUP | WS_CLIPCHILDREN, GetSystemMetrics(SM_XVIRTUALSCREEN),
                           GetSystemMetrics(SM_YVIRTUALSCREEN), GetSystemMetrics(SM_CXVIRTUALSCREEN),
                           GetSystemMetrics(SM_CYVIRTUALSCREEN), NULL, NULL, inst, NULL);
    if (!desk) return 1;
    dpi = window_dpi(desk);
    st_make_fonts(dpi);
    layout();
    load_wallpaper();
    rescan();
    if (argc > 1) apply_commands(argc - 1, argv + 1);
    SetWindowPos(desk, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    UpdateWindow(desk);
    watch_stop = CreateEventW(NULL, TRUE, FALSE, NULL);
    CloseHandle(CreateThread(NULL, 0, watch_thread, NULL, 0, NULL));
    register_shnotify();
    LocalFree(argv);
    write_state();

    while (GetMessageW(&msg, NULL, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    CoUninitialize();
    return (int)msg.wParam;
}
