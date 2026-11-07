/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * setu-files: the Setu file manager (Milestone 3).
 *
 * A Win32 program built with mingw-w64 that runs on the Setu personality.
 * It uses only public, documented APIs (user32, gdi32, comctl32, comdlg32,
 * shell32, shlwapi, ole32, advapi32, kernel32), so everything it does to
 * files goes through the personality's shell. Main references
 * (learn.microsoft.com):
 *   - SHGetKnownFolderPath (FOLDERID_Profile, _Desktop, _Documents,
 *     _Downloads), GetLogicalDriveStringsW, GetDiskFreeSpaceExW
 *   - FindFirstFileExW, SHGetFileInfoW (SHGFI_SYSICONINDEX, SHGFI_TYPENAME),
 *     StrFormatByteSizeW, StrCmpLogicalW, SHAutoComplete
 *   - List-View controls (LVN_GETDISPINFO, LVN_ENDLABELEDIT, LVN_BEGINDRAG,
 *     NM_CUSTOMDRAW), Image Lists (ImageList_BeginDrag)
 *   - SHFileOperationW (FO_COPY, FO_MOVE, FO_RENAME, FO_DELETE with
 *     FOF_ALLOWUNDO), its documented DE_* return values
 *   - ShellExecuteExW (default verb, SEE_MASK_CLASSKEY)
 *   - AssocQueryStringW; "File Types", "Programmatic Identifiers",
 *     "Registering an Application for Use with a Specific File Type"
 *     (OpenWithProgids, OpenWithList, Applications\<exe>), "Perceived Types"
 *   - Recycle Bin: SHGetSpecialFolderLocation(CSIDL_BITBUCKET),
 *     IShellFolder2::EnumObjects/GetDetailsOf, IContextMenu,
 *     SHQueryRecycleBinW, SHEmptyRecycleBinW
 *   - Shell clipboard formats (CF_HDROP, CFSTR_PREFERREDDROPEFFECT),
 *     DragAcceptFiles/DragQueryFileW, AddClipboardFormatListener
 *   - SHChangeNotifyRegister, FindFirstChangeNotificationW,
 *     MsgWaitForMultipleObjectsEx
 *   - GetFileAttributesExW, SetFileAttributesW, GetVolumeInformationW
 *
 * Command line:
 *   setu-files [PATH]        open PATH (a folder, "Drives" or "Recycle Bin")
 *   --state-file PATH        write the window's state (for tests) to PATH
 *   --exit-after SEC         quit after SEC seconds
 * A running window also accepts commands by WM_COPYDATA (shell/tests/
 * setu-files-probe.c sends them); see apply_commands() for the list.
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
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <knownfolders.h>
#include <objbase.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

#define MAIN_CLASS     L"SetuFiles"
#define SIDE_CLASS     L"SetuFilesSidebar"
#define PROPS_CLASS    L"SetuFilesProps"
#define COPYDATA_MAGIC 0x53454c46 /* "FLES" */
#define CMD_SEP        L'\x1f'

#define WM_APP_COMMANDS (WM_APP + 1)
#define WM_APP_SHNOTIFY (WM_APP + 2)
#define WM_APP_SIZE     (WM_APP + 3)

#define TIMER_RELOAD 1
#define TIMER_EXIT   2
#define TIMER_THEME  3

#define ID_LIST    100
#define ID_ADDR    101
#define ID_SIDEBAR 102
#define ID_P_READONLY 110
#define ID_P_HIDDEN   111
#define ID_P_ARCHIVE  112
#define ID_P_OK       113
#define ID_P_CANCEL   114
#define ID_P_APPLY    115

enum
{
    IDM_BACK = 1000, IDM_FORWARD, IDM_UP, IDM_REFRESH, IDM_NEWFOLDER, IDM_VIEW_DETAILS, IDM_VIEW_ICONS,
    IDM_OPEN, IDM_RENAME, IDM_COPY, IDM_CUT, IDM_PASTE, IDM_DELETE, IDM_DELETE_PERM, IDM_PROPERTIES,
    IDM_SELECT_ALL, IDM_EMPTY_RB, IDM_SHOW_HIDDEN, IDM_CHOOSE_PROGRAM, IDM_ADDRESS, IDM_NEW_WINDOW,
    IDM_COPY_PATH,
};
#define IDM_SORT_BASE      1100
#define IDM_OPENWITH_BASE  1200
#define IDM_SIDE_BASE      1400
#define SHELLMENU_FIRST    2000
#define SHELLMENU_LAST     2999

#include "setu-style.h"

/* Colours: docs/DESIGN.md's tokens, from setu-style.h. */
#define C_BAR      (ST->window_bg)        /* command bar, places, status bar */
#define C_EDGE     (ST->border_subtle)
#define C_HOVER    (ST->surface_hover)
#define C_TEXT     (ST->text)
#define C_MUTED    (ST->text_secondary)
#define C_DIM      (ST->text_disabled)
#define C_ACCENT   (ST->accent)
#define C_PANEL    (ST->surface)          /* the content */
#define C_ERROR    (ST->error)

/* ------------------------------------------------------------------ */
/* State                                                                */

enum loc { LOC_FS, LOC_DRIVES, LOC_RECYCLE };

struct item
{
    WCHAR name[MAX_PATH];
    WCHAR path[MAX_PATH];     /* full path (folders, files, drive roots) */
    WCHAR type[80];
    WCHAR col1[MAX_PATH];     /* Recycle Bin: original location */
    WCHAR col2[64];           /* Recycle Bin: date deleted (the shell's text) */
    WCHAR col3[32];           /* Recycle Bin: size (the shell's text) */
    DWORD attrs;
    ULONGLONG size, total, free;
    FILETIME mtime;
    int icon;
    BOOL dir;
    LPITEMIDLIST pidl;        /* Recycle Bin items, relative to the bin */
};

struct place
{
    int kind;                 /* 0 header, 1 entry */
    WCHAR label[96];
    WCHAR target[MAX_PATH];   /* path, "::drives" or "::recycle" */
    int icon;                 /* system image list index (unused for drawing) */
    int licon;                /* our named icon (enum setu_icon_name) */
    int y, h;
};

struct handler
{
    WCHAR name[128];
    WCHAR exe[MAX_PATH];
    HKEY root;                /* class key to run it with (SEE_MASK_CLASSKEY) */
    WCHAR key[MAX_PATH];
    BOOL is_default;
    const WCHAR *source;      /* where it was found, for tests */
};

/* a command bar button: an icon, with its label beside it when WIDE */
struct tbtn
{
    int id;
    int icon;
    RECT rc;
    BOOL enabled;
    BOOL toggled;
    BOOL wide;
    const WCHAR *name;        /* for tests */
    const WCHAR *label;       /* tooltip, and the text of wide buttons */
};

/* the address bar's breadcrumbs */
struct crumb
{
    WCHAR label[MAX_PATH];
    WCHAR target[MAX_PATH];
    RECT rc;
};
#define MAX_CRUMBS 32

#define MAX_HIST     64
#define MAX_PLACES   48
#define MAX_HANDLERS 24
#define NUM_TBTN     13

static HINSTANCE hinst;
static HWND main_wnd, lv, addr, sidebar, props_wnd;
static WNDPROC addr_orig_proc;
static HIMAGELIST il_small, il_large;
static UINT dpi = 96;
static HFONT font_ui, font_bold, font_title;   /* aliases of st_font[] */
static HBRUSH field_brush, panel_brush, bar_brush;
static HWND tips;
static HICON app_icon_big, app_icon_small;

static enum loc cur_loc = LOC_FS;
static WCHAR cur_path[MAX_PATH];
static struct item *items;
static int nitems, cap_items;
static int sort_col;
static BOOL sort_desc;
static BOOL view_icons;
static BOOL show_hidden;

static WCHAR back_stack[MAX_HIST][MAX_PATH];
static int nback;
static WCHAR fwd_stack[MAX_HIST][MAX_PATH];
static int nfwd;

static struct place places[MAX_PLACES];
static int nplaces;
static int side_sel = -1, side_hot = -1;

static struct tbtn tbtns[NUM_TBTN];
static int tb_hot = -1;
static BOOL tracking_mouse;
static RECT addr_field_rc, header_rc, status_rc;
static int col_w[4];

static struct handler handlers[MAX_HANDLERS];
static int nhandlers;
static WCHAR handlers_for[MAX_PATH];

static HANDLE change_handle = INVALID_HANDLE_VALUE;
static ULONG shnotify_id;
static UINT cf_effect;
static DWORD our_clip_seq;
static WCHAR *cut_paths;     /* double-null list of paths cut to the clipboard */

static BOOL dragging;
static int drag_target = -1;      /* list item index under the cursor */
static int drag_place = -1;       /* sidebar entry under the cursor */
static BOOL drag_image;

static WCHAR state_path[MAX_PATH];
static WCHAR status_text[512];
static BOOL status_error;
static WCHAR last_op[256];
static LONG cmd_seq;
static int exit_after_sec;
static int theme_reloads;    /* theme packs applied since the start */
static BOOL automation;      /* driven by a test: never block on our own message boxes */

static IContextMenu *rb_menu_cm;
static HMENU rb_menu;
static WCHAR rb_menu_text[16][64];
static int rb_menu_ids[16], rb_menu_count;

/* ------------------------------------------------------------------ */
/* Helpers                                                              */

static int S(int v) { return MulDiv(v, (int)dpi, 96); }

static UINT get_window_dpi(HWND hwnd)
{
    static UINT (WINAPI *pGetDpiForWindow)(HWND);
    static BOOL looked;
    if (!looked)
    {
        pGetDpiForWindow = (void *)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
        looked = TRUE;
    }
    if (pGetDpiForWindow && hwnd)
    {
        UINT d = pGetDpiForWindow(hwnd);
        if (d) return d;
    }
    HDC dc = GetDC(NULL);
    UINT d = GetDeviceCaps(dc, LOGPIXELSY);
    ReleaseDC(NULL, dc);
    return d ? d : 96;
}

static void set_dpi_awareness(void)
{
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    BOOL (WINAPI *pSetCtx)(HANDLE) = (void *)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
    /* DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 == (HANDLE)-4 */
    if (pSetCtx && pSetCtx((HANDLE)(LONG_PTR)-4)) return;
    SetProcessDPIAware();
}

/* Inter at docs/DESIGN.md's sizes (setu-style.h); the old names stay as
 * aliases for the controls' WM_SETFONT. */
static void make_fonts(void)
{
    st_make_fonts(dpi);
    font_ui = st_font[SF_BODY];
    font_bold = st_font[SF_CAPTION];
    font_title = st_font[SF_LARGE];
}

/* Paint a part of a DC (an owner-drawn item) through a canvas: FN gets
 * the canvas and the rect in its coordinates (0, 0, w, h). */
static void paint_into(HDC dc, const RECT *r, void (*fn)(struct setu_canvas *, const RECT *, void *), void *ctx)
{
    struct setu_canvas cv;
    RECT local = { 0, 0, r->right - r->left, r->bottom - r->top };
    if (!st_canvas_begin(&cv, local.right, local.bottom)) return;
    fn(&cv, &local, ctx);
    GdiFlush();
    BitBlt(dc, r->left, r->top, local.right, local.bottom, cv.dc, 0, 0, SRCCOPY);
    st_canvas_end(&cv);
}

/* The window icon: a folder line icon on an accent tile. It's drawn twice,
 * on black and on white, which gives the anti-aliased edges' alpha. */
static void draw_app_icon_on(struct setu_canvas *cv, int size, COLORREF bg)
{
    RECT r = { 0, 0, size, size };
    st_fill(cv, &r, bg);
    InflateRect(&r, -max(1, size / 16), -max(1, size / 16));
    st_round(cv, &r, max(2, size / 5), ST->accent);
    st_icon_in(cv, TI_FOLDER, &r, size * 5 / 8, ST->accent_text);
}

static HICON make_app_icon(int size)
{
    struct setu_canvas black, white;
    BITMAPINFO bi = { { sizeof(BITMAPINFOHEADER), size, -size, 1, 32, BI_RGB } };
    DWORD *bits;
    HDC dc;
    HBITMAP color, mask;
    ICONINFO ii = { TRUE };
    HICON icon = NULL;
    int i;

    if (!st_canvas_begin(&black, size, size)) return NULL;
    if (!st_canvas_begin(&white, size, size)) { st_canvas_end(&black); return NULL; }
    draw_app_icon_on(&black, size, RGB(0, 0, 0));
    draw_app_icon_on(&white, size, RGB(0xff, 0xff, 0xff));
    GdiFlush();
    dc = CreateCompatibleDC(NULL);
    color = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, (void **)&bits, NULL, 0);
    mask = CreateBitmap(size, size, 1, 1, NULL);
    if (color && mask)
    {
        for (i = 0; i < size * size; i++)
        {
            DWORD b = black.bits[i], w = white.bits[i];
            unsigned a = 255 - (((w >> 8) & 0xff) - ((b >> 8) & 0xff));
            /* premultiplied on black: un-premultiply */
            unsigned rr = a ? min(255u, ((b >> 16) & 0xff) * 255 / a) : 0;
            unsigned gg = a ? min(255u, ((b >> 8) & 0xff) * 255 / a) : 0;
            unsigned bb = a ? min(255u, (b & 0xff) * 255 / a) : 0;
            bits[i] = (a << 24) | (rr << 16) | (gg << 8) | bb;
        }
        ii.hbmColor = color;
        ii.hbmMask = mask;
        icon = CreateIconIndirect(&ii);
    }
    if (color) DeleteObject(color);
    if (mask) DeleteObject(mask);
    DeleteDC(dc);
    st_canvas_end(&black);
    st_canvas_end(&white);
    return icon;
}

static void set_status_v(BOOL error, const WCHAR *fmt, va_list ap)
{
    _vsnwprintf(status_text, ARRAYSIZE(status_text) - 1, fmt, ap);
    status_text[ARRAYSIZE(status_text) - 1] = 0;
    status_error = error;
    if (main_wnd)
    {
        InvalidateRect(main_wnd, &status_rc, FALSE);
        UpdateWindow(main_wnd);
    }
}

static void set_status(const WCHAR *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    set_status_v(FALSE, fmt, ap);
    va_end(ap);
}

static void set_error(const WCHAR *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    set_status_v(TRUE, fmt, ap);
    va_end(ap);
}

/* Show an error the user has to see: the status bar always, plus a
 * message box unless a test is driving us. */
static void report_error(const WCHAR *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    set_status_v(TRUE, fmt, ap);
    va_end(ap);
    if (!automation) MessageBoxW(main_wnd, status_text, L"Files", MB_OK | MB_ICONWARNING);
}

static void format_time(const FILETIME *ft, WCHAR *buf, int len)
{
    SYSTEMTIME utc, st;
    WCHAR d[64], t[64];

    buf[0] = 0;
    if (!ft->dwLowDateTime && !ft->dwHighDateTime) return;
    if (!FileTimeToSystemTime(ft, &utc) || !SystemTimeToTzSpecificLocalTime(NULL, &utc, &st)) return;
    GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &st, NULL, d, ARRAYSIZE(d));
    GetTimeFormatW(LOCALE_USER_DEFAULT, TIME_NOSECONDS, &st, NULL, t, ARRAYSIZE(t));
    _snwprintf(buf, len, L"%ls %ls", d, t);
    buf[len - 1] = 0;
}

static void format_bytes(ULONGLONG n, WCHAR *buf, int len)
{
    StrFormatByteSizeW((LONGLONG)n, buf, len);
}

/* "1,234,567" in the user's locale */
static void format_count(ULONGLONG n, WCHAR *buf, int len)
{
    WCHAR raw[32], sep[8] = L",";
    NUMBERFMTW nf = { 0, 0, 3, (WCHAR *)L".", sep, 1 };
    GetLocaleInfoW(LOCALE_USER_DEFAULT, LOCALE_STHOUSAND, sep, ARRAYSIZE(sep));
    _snwprintf(raw, ARRAYSIZE(raw), L"%I64u", n);
    raw[ARRAYSIZE(raw) - 1] = 0;
    if (!GetNumberFormatW(LOCALE_USER_DEFAULT, 0, raw, &nf, buf, len) || !buf[0]) lstrcpynW(buf, raw, len);
}

static BOOL is_root(const WCHAR *path)
{
    return PathIsRootW(path);
}

static void loc_string(WCHAR *buf, int len)
{
    if (cur_loc == LOC_DRIVES) lstrcpynW(buf, L"::drives", len);
    else if (cur_loc == LOC_RECYCLE) lstrcpynW(buf, L"::recycle", len);
    else lstrcpynW(buf, cur_path, len);
}

static const WCHAR *loc_display(void)
{
    if (cur_loc == LOC_DRIVES) return L"Drives";
    if (cur_loc == LOC_RECYCLE) return L"Recycle Bin";
    return cur_path;
}

/* Join a list of paths into a double-null-terminated string. */
static WCHAR *multi_sz(WCHAR **paths, int n)
{
    size_t len = 1;
    WCHAR *buf, *p;
    int i;
    for (i = 0; i < n; i++) len += wcslen(paths[i]) + 1;
    p = buf = calloc(len, sizeof(WCHAR));
    if (!buf) return NULL;
    for (i = 0; i < n; i++)
    {
        wcscpy(p, paths[i]);
        p += wcslen(paths[i]) + 1;
    }
    return buf;
}

static int multi_count(const WCHAR *m)
{
    int n = 0;
    for (; m && *m; m += wcslen(m) + 1) n++;
    return n;
}

static BOOL in_multi(const WCHAR *m, const WCHAR *path)
{
    for (; m && *m; m += wcslen(m) + 1)
        if (!lstrcmpiW(m, path)) return TRUE;
    return FALSE;
}

static void same_parent(const WCHAR *path, WCHAR *parent)
{
    lstrcpynW(parent, path, MAX_PATH);
    PathRemoveBackslashW(parent);
    PathRemoveFileSpecW(parent);
    PathAddBackslashW(parent);
}

/* ------------------------------------------------------------------ */
/* Loading a location                                                   */

static struct item *new_item(void)
{
    if (nitems == cap_items)
    {
        int cap = cap_items ? cap_items * 2 : 256;
        struct item *n = realloc(items, cap * sizeof(*items));
        if (!n) return NULL;
        items = n;
        cap_items = cap;
    }
    memset(&items[nitems], 0, sizeof(items[0]));
    return &items[nitems++];
}

static void free_items(void)
{
    int i;
    for (i = 0; i < nitems; i++)
        if (items[i].pidl) CoTaskMemFree(items[i].pidl);
    nitems = 0;
}

/* ------------------------------------------------------------------ */
/* Pack icons in the list                                               */

/* The list views draw from the system image lists. Where the active pack
 * has an icon for what shell32 shows with a generic icon (a plain folder, a
 * file of no known type, a drive, the Recycle Bin), the pack's icon is
 * added to both lists (ImageList_ReplaceIcon) and used instead. The slots
 * are kept and replaced in place when another pack is applied. */
static int sysicon_slot[TI_COUNT];           /* index + 1 in both lists, 0 none */
static BOOL sysicon_live[TI_COUNT];          /* the slot holds the active pack's icon */
static int generic_folder_icon = -1, generic_file_icon = -1;

static void theme_sysicons_reset(void)
{
    memset(sysicon_live, 0, sizeof(sysicon_live));
}

/* the image list index for icon NAME from the pack, or FALLBACK */
static int pack_sysicon(int name, int fallback)
{
    HICON small, large;
    int a, b, slot;

    if (!il_small || !il_large || !st_icon_from_pack(name)) return fallback;
    if (sysicon_live[name]) return sysicon_slot[name] - 1;
    small = (HICON)LoadImageW(NULL, setu_theme.icon_file[name], IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                              GetSystemMetrics(SM_CYSMICON), LR_LOADFROMFILE);
    large = (HICON)LoadImageW(NULL, setu_theme.icon_file[name], IMAGE_ICON, GetSystemMetrics(SM_CXICON),
                              GetSystemMetrics(SM_CYICON), LR_LOADFROMFILE);
    slot = sysicon_slot[name] - 1;
    a = small && large ? ImageList_ReplaceIcon(il_small, slot, small) : -1;
    b = a >= 0 ? ImageList_ReplaceIcon(il_large, slot >= 0 ? slot : a, large) : -1;
    if (small) DestroyIcon(small);
    if (large) DestroyIcon(large);
    if (a < 0 || b != a) return fallback;
    sysicon_slot[name] = a + 1;
    sysicon_live[name] = TRUE;
    return a;
}

/* ICON (a system image list index) for a folder or file, or the pack's */
static int themed_item_icon(int icon, BOOL dir)
{
    SHFILEINFOW sfi;
    if (generic_folder_icon < 0)
    {
        memset(&sfi, 0, sizeof(sfi));
        SHGetFileInfoW(L"folder", FILE_ATTRIBUTE_DIRECTORY, &sfi, sizeof(sfi),
                       SHGFI_USEFILEATTRIBUTES | SHGFI_SYSICONINDEX | SHGFI_SMALLICON);
        generic_folder_icon = sfi.iIcon;
        memset(&sfi, 0, sizeof(sfi));
        SHGetFileInfoW(L"file", FILE_ATTRIBUTE_NORMAL, &sfi, sizeof(sfi),
                       SHGFI_USEFILEATTRIBUTES | SHGFI_SYSICONINDEX | SHGFI_SMALLICON);
        generic_file_icon = sfi.iIcon;
    }
    if (dir && icon == generic_folder_icon) return pack_sysicon(TI_FOLDER, icon);
    if (!dir && icon == generic_file_icon) return pack_sysicon(TI_FILE, icon);
    return icon;
}

static int drive_icon_name(const WCHAR *root)
{
    switch (GetDriveTypeW(root))
    {
    case DRIVE_REMOVABLE: return TI_DRIVE_REMOVABLE;
    case DRIVE_CDROM:     return TI_DRIVE_OPTICAL;
    default:              return TI_DRIVE_FIXED;
    }
}

/* Per-extension cache of type names and icons, so a big folder needs one
 * SHGetFileInfoW call per file type instead of per file. */
static struct { WCHAR ext[32]; WCHAR type[80]; int icon; } type_cache[128];
static int ntype_cache;

static void file_type_and_icon(struct item *it)
{
    SHFILEINFOW sfi;
    const WCHAR *ext = PathFindExtensionW(it->name);
    BOOL own_icon = !it->dir && (!lstrcmpiW(ext, L".exe") || !lstrcmpiW(ext, L".lnk") || !lstrcmpiW(ext, L".ico"));
    int i;

    if (!own_icon && wcslen(ext) < 32)
    {
        const WCHAR *key = it->dir ? L"\\" : ext;
        for (i = 0; i < ntype_cache; i++)
            if (!lstrcmpiW(type_cache[i].ext, key))
            {
                lstrcpyW(it->type, type_cache[i].type);
                it->icon = type_cache[i].icon;
                return;
            }
    }
    memset(&sfi, 0, sizeof(sfi));
    if (own_icon)
        SHGetFileInfoW(it->path, 0, &sfi, sizeof(sfi), SHGFI_SYSICONINDEX | SHGFI_TYPENAME);
    else
        SHGetFileInfoW(it->name, it->dir ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL, &sfi, sizeof(sfi),
                       SHGFI_USEFILEATTRIBUTES | SHGFI_SYSICONINDEX | SHGFI_TYPENAME);
    lstrcpynW(it->type, sfi.szTypeName, ARRAYSIZE(it->type));
    if (!it->type[0])
    {
        if (it->dir) lstrcpyW(it->type, L"Folder");
        else if (*ext)
        {
            _snwprintf(it->type, ARRAYSIZE(it->type), L"%ls file", ext + 1);
            CharUpperBuffW(it->type, (DWORD)wcslen(ext + 1));
        }
        else lstrcpyW(it->type, L"File");
    }
    it->icon = themed_item_icon(sfi.iIcon, it->dir);
    if (!own_icon && wcslen(ext) < 32 && ntype_cache < (int)ARRAYSIZE(type_cache))
    {
        lstrcpyW(type_cache[ntype_cache].ext, it->dir ? L"\\" : ext);
        lstrcpyW(type_cache[ntype_cache].type, it->type);
        type_cache[ntype_cache].icon = it->icon;
        ntype_cache++;
    }
}

static BOOL load_fs(const WCHAR *dir)
{
    WCHAR pat[MAX_PATH + 4];
    WIN32_FIND_DATAW fd;
    HANDLE h;

    PathCombineW(pat, dir, L"*");
    h = FindFirstFileExW(pat, FindExInfoBasic, &fd, FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE)
    {
        DWORD err = GetLastError();
        return err == ERROR_FILE_NOT_FOUND || err == ERROR_NO_MORE_FILES;
    }
    do
    {
        struct item *it;
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        if (!show_hidden && (fd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) ==
            (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) continue;
        if (!show_hidden && (fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN)) continue;
        if (!(it = new_item())) break;
        lstrcpynW(it->name, fd.cFileName, ARRAYSIZE(it->name));
        PathCombineW(it->path, dir, fd.cFileName);
        it->attrs = fd.dwFileAttributes;
        it->dir = !!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
        it->size = ((ULONGLONG)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
        it->mtime = fd.ftLastWriteTime;
        file_type_and_icon(it);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return TRUE;
}

static const WCHAR *drive_type_name(UINT t)
{
    switch (t)
    {
    case DRIVE_REMOVABLE: return L"Removable drive";
    case DRIVE_FIXED:     return L"Local drive";
    case DRIVE_REMOTE:    return L"Network drive";
    case DRIVE_CDROM:     return L"Optical drive";
    case DRIVE_RAMDISK:   return L"RAM drive";
    default:              return L"Drive";
    }
}

/* "System (C:)" style names: the volume label, else the kind of drive. */
static void drive_label(const WCHAR *root, WCHAR *buf, int len, int *icon)
{
    WCHAR label[MAX_PATH] = L"";
    if (icon)
    {
        SHFILEINFOW sfi;
        memset(&sfi, 0, sizeof(sfi));
        SHGetFileInfoW(root, 0, &sfi, sizeof(sfi), SHGFI_SYSICONINDEX | SHGFI_SMALLICON);
        *icon = sfi.iIcon;
    }
    if (GetDriveTypeW(root) != DRIVE_REMOVABLE && GetDriveTypeW(root) != DRIVE_CDROM)
        GetVolumeInformationW(root, label, ARRAYSIZE(label), NULL, NULL, NULL, NULL, 0);
    _snwprintf(buf, len, L"%ls (%c:)", label[0] ? label : drive_type_name(GetDriveTypeW(root)), root[0]);
    buf[len - 1] = 0;
}

/* The drive roots as a double-null list. GetLogicalDriveStringsW first;
 * if that lists nothing, ask GetDriveTypeW about every letter. */
static BOOL drive_roots(WCHAR *buf, int len)
{
    WCHAR root[4] = L"A:\\", *p = buf;
    UINT n = GetLogicalDriveStringsW(len, buf);
    if (n && n < (UINT)len && buf[0]) return TRUE;
    for (root[0] = L'A'; root[0] <= L'Z' && p - buf + 5 < len; root[0]++)
    {
        UINT t = GetDriveTypeW(root);
        if (t == DRIVE_NO_ROOT_DIR || t == DRIVE_UNKNOWN) continue;
        wcscpy(p, root);
        p += 4;
    }
    *p = 0;
    return p != buf;
}

static void load_drives(void)
{
    WCHAR roots[512], *r;
    if (!drive_roots(roots, ARRAYSIZE(roots))) return;
    for (r = roots; *r; r += wcslen(r) + 1)
    {
        struct item *it = new_item();
        UINT t = GetDriveTypeW(r);
        ULARGE_INTEGER avail, total, free;
        if (!it) break;
        lstrcpynW(it->path, r, ARRAYSIZE(it->path));
        drive_label(r, it->name, ARRAYSIZE(it->name), &it->icon);
        it->icon = pack_sysicon(drive_icon_name(r), it->icon);
        lstrcpyW(it->type, drive_type_name(t));
        it->dir = TRUE;
        it->attrs = FILE_ATTRIBUTE_DIRECTORY;
        if ((t == DRIVE_FIXED || t == DRIVE_RAMDISK) && GetDiskFreeSpaceExW(r, &avail, &total, &free))
        {
            it->total = total.QuadPart;
            it->free = avail.QuadPart;
        }
    }
}

static IShellFolder2 *open_recycle_bin(LPITEMIDLIST *out_pidl)
{
    IShellFolder *desktop = NULL;
    IShellFolder2 *bin = NULL;
    LPITEMIDLIST pidl = NULL;

    if (FAILED(SHGetSpecialFolderLocation(NULL, CSIDL_BITBUCKET, &pidl))) return NULL;
    if (SUCCEEDED(SHGetDesktopFolder(&desktop)))
    {
        IShellFolder_BindToObject(desktop, pidl, NULL, &IID_IShellFolder2, (void **)&bin);
        IShellFolder_Release(desktop);
    }
    if (out_pidl) *out_pidl = pidl;
    else CoTaskMemFree(pidl);
    return bin;
}

static void strret_to(IShellFolder2 *sf, LPCITEMIDLIST pidl, STRRET *sr, WCHAR *buf, int len)
{
    buf[0] = 0;
    if (FAILED(StrRetToBufW(sr, pidl, buf, len))) buf[0] = 0;
    (void)sf;
}

static void load_recycle(void)
{
    LPITEMIDLIST bin_pidl = NULL;
    IShellFolder2 *bin = open_recycle_bin(&bin_pidl);
    IEnumIDList *en = NULL;
    LPITEMIDLIST child;
    ULONG got;

    if (!bin) { CoTaskMemFree(bin_pidl); return; }
    if (IShellFolder2_EnumObjects(bin, main_wnd, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS | SHCONTF_INCLUDEHIDDEN, &en) == S_OK && en)
    {
        while (IEnumIDList_Next(en, 1, &child, &got) == S_OK && got)
        {
            struct item *it = new_item();
            SHELLDETAILS sd;
            STRRET sr;
            SFGAOF attr = SFGAO_FOLDER;
            LPITEMIDLIST abs;
            SHFILEINFOW sfi;

            if (!it) { CoTaskMemFree(child); break; }
            it->pidl = child;
            if (SUCCEEDED(IShellFolder2_GetDisplayNameOf(bin, child, SHGDN_INFOLDER, &sr)))
                strret_to(bin, child, &sr, it->name, ARRAYSIZE(it->name));
            if (SUCCEEDED(IShellFolder2_GetDetailsOf(bin, child, 1, &sd)))
                strret_to(bin, child, &sd.str, it->col1, ARRAYSIZE(it->col1));
            if (SUCCEEDED(IShellFolder2_GetDetailsOf(bin, child, 2, &sd)))
                strret_to(bin, child, &sd.str, it->col2, ARRAYSIZE(it->col2));
            if (SUCCEEDED(IShellFolder2_GetDetailsOf(bin, child, 3, &sd)))
                strret_to(bin, child, &sd.str, it->col3, ARRAYSIZE(it->col3));
            IShellFolder2_GetAttributesOf(bin, 1, (LPCITEMIDLIST *)&child, &attr);
            it->dir = !!(attr & SFGAO_FOLDER);
            abs = ILCombine(bin_pidl, child);
            memset(&sfi, 0, sizeof(sfi));
            if (abs && SHGetFileInfoW((LPCWSTR)abs, 0, &sfi, sizeof(sfi), SHGFI_PIDL | SHGFI_SYSICONINDEX | SHGFI_TYPENAME))
            {
                it->icon = themed_item_icon(sfi.iIcon, it->dir);
                lstrcpynW(it->type, sfi.szTypeName, ARRAYSIZE(it->type));
            }
            else
            {
                /* the item's own type, from its name */
                memset(&sfi, 0, sizeof(sfi));
                SHGetFileInfoW(it->name, it->dir ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL, &sfi, sizeof(sfi),
                               SHGFI_USEFILEATTRIBUTES | SHGFI_SYSICONINDEX | SHGFI_TYPENAME);
                it->icon = themed_item_icon(sfi.iIcon, it->dir);
                lstrcpynW(it->type, sfi.szTypeName, ARRAYSIZE(it->type));
            }
            ILFree(abs);
            PathCombineW(it->path, it->col1, it->name);
        }
        IEnumIDList_Release(en);
    }
    IShellFolder2_Release(bin);
    CoTaskMemFree(bin_pidl);
}

static int recycle_count(void)
{
    SHQUERYRBINFO qi = { sizeof(qi) };
    if (FAILED(SHQueryRecycleBinW(NULL, &qi))) return -1;
    return (int)qi.i64NumItems;
}

/* ------------------------------------------------------------------ */
/* The list                                                             */

static const WCHAR *const col_names[3][4] =
{
    { L"Name", L"Size", L"Type", L"Modified" },
    { L"Name", L"Type", L"Total size", L"Free space" },
    { L"Name", L"Original location", L"Deleted", L"Size" },
};
static const BOOL col_right[3][4] = { { 0, 1, 0, 0 }, { 0, 0, 1, 1 }, { 0, 0, 0, 1 } };
static const int col_base_w[3][4] = { { 0, 100, 170, 150 }, { 0, 140, 110, 110 }, { 0, 230, 150, 100 } };

static void cell_text(const struct item *it, int col, WCHAR *buf, int len)
{
    buf[0] = 0;
    if (col == 0) { lstrcpynW(buf, it->name, len); return; }
    switch (cur_loc)
    {
    case LOC_FS:
        if (col == 1 && !it->dir) format_bytes(it->size, buf, len);
        else if (col == 2) lstrcpynW(buf, it->type, len);
        else if (col == 3) format_time(&it->mtime, buf, len);
        break;
    case LOC_DRIVES:
        if (col == 1) lstrcpynW(buf, it->type, len);
        else if (col == 2 && it->total) format_bytes(it->total, buf, len);
        else if (col == 3 && it->total) format_bytes(it->free, buf, len);
        break;
    case LOC_RECYCLE:
        if (col == 1) lstrcpynW(buf, it->col1, len);
        else if (col == 2) lstrcpynW(buf, it->col2, len);
        else if (col == 3) lstrcpynW(buf, it->col3, len);
        break;
    }
}

static int CALLBACK compare_items(LPARAM a, LPARAM b, LPARAM unused)
{
    const struct item *x = &items[a], *y = &items[b];
    int r = 0;

    if (cur_loc != LOC_DRIVES && x->dir != y->dir) return x->dir ? -1 : 1; /* folders first, both ways */
    switch (cur_loc == LOC_FS ? sort_col : cur_loc == LOC_DRIVES ? sort_col + 10 : sort_col + 20)
    {
    case 1: r = x->size < y->size ? -1 : x->size > y->size; break;
    case 2: case 11: r = lstrcmpiW(x->type, y->type); break;
    case 3: r = CompareFileTime(&x->mtime, &y->mtime); break;
    case 12: r = x->total < y->total ? -1 : x->total > y->total; break;
    case 13: r = x->free < y->free ? -1 : x->free > y->free; break;
    case 21: r = lstrcmpiW(x->col1, y->col1); break;
    case 22: r = lstrcmpiW(x->col2, y->col2); break;
    case 23: r = StrCmpLogicalW(x->col3, y->col3); break;
    case 10: r = lstrcmpiW(x->path, y->path); break;
    }
    if (!r) r = StrCmpLogicalW(x->name, y->name);
    return sort_desc ? -r : r;
}

static int item_of(int lv_index)
{
    LVITEMW li = { LVIF_PARAM };
    li.iItem = lv_index;
    if (lv_index < 0 || !ListView_GetItem(lv, &li)) return -1;
    return (int)li.lParam;
}

static int lv_index_of(int item)
{
    LVFINDINFOW fi = { LVFI_PARAM };
    fi.lParam = item;
    return ListView_FindItem(lv, -1, &fi);
}

static int find_item_by_name(const WCHAR *name)
{
    int i;
    for (i = 0; i < nitems; i++)
        if (!lstrcmpiW(items[i].name, name)) return i;
    return -1;
}

/* selected items, as indices into items[] */
static int get_selection(int *out, int max)
{
    int n = 0, i = -1;
    while (n < max && (i = ListView_GetNextItem(lv, i, LVNI_SELECTED)) >= 0)
    {
        int it = item_of(i);
        if (it >= 0) out[n++] = it;
    }
    return n;
}

static int selection_count(void) { return ListView_GetSelectedCount(lv); }

static int focused_item(void)
{
    int i = ListView_GetNextItem(lv, -1, LVNI_FOCUSED);
    if (i >= 0 && !(ListView_GetItemState(lv, i, LVIS_SELECTED) & LVIS_SELECTED)) i = -1;
    if (i < 0) i = ListView_GetNextItem(lv, -1, LVNI_SELECTED);
    return item_of(i);
}

static void select_only(int item, BOOL edit)
{
    int i = lv_index_of(item);
    ListView_SetItemState(lv, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    if (i < 0) return;
    ListView_SetItemState(lv, i, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_SetSelectionMark(lv, i);
    ListView_EnsureVisible(lv, i, FALSE);
    if (edit)
    {
        SetFocus(lv);
        ListView_EditLabel(lv, i);
    }
}

static void layout(void);
static void update_toolbar(void);
static void paint_sidebar_later(void) { if (sidebar) InvalidateRect(sidebar, NULL, FALSE); }

static void fill_list(WCHAR **keep_sel, int nkeep, const WCHAR *keep_focus)
{
    LVITEMW li;
    int i;

    SendMessageW(lv, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(lv);
    ListView_SetImageList(lv, il_small, LVSIL_SMALL);
    ListView_SetImageList(lv, il_large, LVSIL_NORMAL);
    for (i = 0; i < nitems; i++)
    {
        memset(&li, 0, sizeof(li));
        li.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_PARAM | LVIF_STATE;
        li.iItem = i;
        li.pszText = LPSTR_TEXTCALLBACKW;
        li.iImage = items[i].icon;
        li.lParam = i;
        li.stateMask = LVIS_CUT | LVIS_SELECTED;
        if (cut_paths && in_multi(cut_paths, items[i].path)) li.state |= LVIS_CUT;
        if (keep_sel)
        {
            int k;
            for (k = 0; k < nkeep; k++)
                if (!lstrcmpiW(keep_sel[k], items[i].name)) li.state |= LVIS_SELECTED;
        }
        ListView_InsertItem(lv, &li);
    }
    ListView_SortItems(lv, compare_items, 0);
    if (keep_focus && keep_focus[0])
    {
        int it = find_item_by_name(keep_focus), idx = it >= 0 ? lv_index_of(it) : -1;
        if (idx >= 0)
        {
            ListView_SetItemState(lv, idx, LVIS_FOCUSED, LVIS_FOCUSED);
            ListView_SetSelectionMark(lv, idx);
            ListView_EnsureVisible(lv, idx, FALSE);
        }
    }
    else if (ListView_GetItemCount(lv) > 0 && !ListView_GetSelectedCount(lv))
        ListView_SetItemState(lv, 0, LVIS_FOCUSED, LVIS_FOCUSED);
    SendMessageW(lv, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(lv, NULL, TRUE);
}

static void write_state(void);
static void watch_current(void);

/* Re-read the current location, keeping the selection by name. */
static void reload(void)
{
    WCHAR **keep = NULL, focus_name[MAX_PATH] = L"";
    int sel[4096], n, i, f;

    if (lv && ListView_GetEditControl(lv)) return; /* don't pull the list out from under a rename */
    n = get_selection(sel, ARRAYSIZE(sel));
    if (n) keep = malloc(n * sizeof(*keep));
    for (i = 0; keep && i < n; i++) keep[i] = _wcsdup(items[sel[i]].name);
    f = item_of(ListView_GetNextItem(lv, -1, LVNI_FOCUSED));
    if (f >= 0) lstrcpyW(focus_name, items[f].name);

    free_items();
    if (cur_loc == LOC_FS) load_fs(cur_path);
    else if (cur_loc == LOC_DRIVES) load_drives();
    else load_recycle();
    fill_list(keep, keep ? n : 0, focus_name);
    for (i = 0; keep && i < n; i++) free(keep[i]);
    free(keep);
    handlers_for[0] = 0;
    update_toolbar();
    paint_sidebar_later();
    InvalidateRect(main_wnd, &status_rc, FALSE);
    write_state();
}

/* ------------------------------------------------------------------ */
/* Places sidebar                                                       */

static int side_icon(const WCHAR *path)
{
    SHFILEINFOW sfi;
    memset(&sfi, 0, sizeof(sfi));
    SHGetFileInfoW(path, 0, &sfi, sizeof(sfi), SHGFI_SYSICONINDEX | SHGFI_SMALLICON);
    return sfi.iIcon;
}

static void add_place(int kind, const WCHAR *label, const WCHAR *target, int icon)
{
    struct place *p;
    if (nplaces >= MAX_PLACES) return;
    p = &places[nplaces++];
    memset(p, 0, sizeof(*p));
    p->kind = kind;
    lstrcpynW(p->label, label, ARRAYSIZE(p->label));
    if (target) lstrcpynW(p->target, target, ARRAYSIZE(p->target));
    p->icon = icon;
}

static void add_known_place(REFKNOWNFOLDERID id, const WCHAR *label, int licon)
{
    WCHAR *path = NULL;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, NULL, &path)) && path)
    {
        if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES)
        {
            add_place(1, label, path, side_icon(path));
            places[nplaces - 1].licon = licon;
        }
        CoTaskMemFree(path);
    }
}

static void load_places(void)
{
    WCHAR roots[512], *r, label[96];
    LPITEMIDLIST bin = NULL;
    int bin_icon = 0;

    nplaces = 0;
    add_place(0, L"Places", NULL, 0);
    add_known_place(&FOLDERID_Profile, L"Home", TI_USER_HOME);
    add_known_place(&FOLDERID_Desktop, L"Desktop", TI_USER_DESKTOP);
    add_known_place(&FOLDERID_Documents, L"Documents", TI_FOLDER_DOCUMENTS);
    add_known_place(&FOLDERID_Downloads, L"Downloads", TI_FOLDER_DOWNLOAD);
    add_place(0, L"Drives", L"::drives", 0);
    if (drive_roots(roots, ARRAYSIZE(roots)))
        for (r = roots; *r; r += wcslen(r) + 1)
        {
            int icon;
            drive_label(r, label, ARRAYSIZE(label), &icon);
            add_place(1, label, r, icon);
            places[nplaces - 1].licon = drive_icon_name(r);
        }
    add_place(0, L"", NULL, 0);
    if (SUCCEEDED(SHGetSpecialFolderLocation(NULL, CSIDL_BITBUCKET, &bin)))
    {
        SHFILEINFOW sfi;
        memset(&sfi, 0, sizeof(sfi));
        if (SHGetFileInfoW((LPCWSTR)bin, 0, &sfi, sizeof(sfi), SHGFI_PIDL | SHGFI_SYSICONINDEX | SHGFI_SMALLICON))
            bin_icon = sfi.iIcon;
        CoTaskMemFree(bin);
    }
    add_place(1, L"Recycle Bin", L"::recycle", bin_icon);
    places[nplaces - 1].licon = TI_RECYCLE_BIN_EMPTY;
}

static int place_row_h(const struct place *p)
{
    if (p->kind == 0) return p->label[0] ? S(36) : S(14);
    return S(32);
}

static void layout_places(void)
{
    int i, y = S(8);
    for (i = 0; i < nplaces; i++)
    {
        places[i].y = y;
        places[i].h = place_row_h(&places[i]);
        y += places[i].h;
    }
}

static BOOL place_is_current(const struct place *p)
{
    WCHAR a[MAX_PATH], b[MAX_PATH];
    if (p->kind != 1) return FALSE;
    if (!wcscmp(p->target, L"::recycle")) return cur_loc == LOC_RECYCLE;
    if (cur_loc != LOC_FS) return FALSE;
    lstrcpynW(a, p->target, MAX_PATH);
    lstrcpynW(b, cur_path, MAX_PATH);
    PathRemoveBackslashW(a);
    PathRemoveBackslashW(b);
    return !lstrcmpiW(a, b);
}

static int place_at(int y)
{
    int i;
    for (i = 0; i < nplaces; i++)
        if (y >= places[i].y && y < places[i].y + places[i].h) return i;
    return -1;
}

static void paint_sidebar(struct setu_canvas *cv, const RECT *rc)
{
    BOOL focused = GetFocus() == sidebar;
    int i, isz = S(16), rad = S(ST_RADIUS_CONTROL);
    RECT r;

    st_fill(cv, rc, C_BAR);
    for (i = 0; i < nplaces; i++)
    {
        const struct place *p = &places[i];
        BOOL cur = place_is_current(p);
        SetRect(&r, S(8), p->y, rc->right - S(8), p->y + p->h);
        if (p->kind == 0)
        {
            if (!p->label[0])
            {
                RECT line = { S(16), p->y + p->h / 2, rc->right - S(16), p->y + p->h / 2 + max(1, S(1)) };
                st_fill(cv, &line, C_EDGE);
                continue;
            }
            r.left += S(12);
            r.bottom -= S(6);
            st_text(cv, p->label, &r, SF_CAPTION, C_MUTED, DT_SINGLELINE | DT_BOTTOM | DT_LEFT);
            continue;
        }
        if (i == drag_place) st_round(cv, &r, rad, st_selection());
        else if (cur) st_round(cv, &r, rad, st_mix(C_BAR, ST->text, 20));
        else if (i == side_hot) st_round(cv, &r, rad, st_mix(C_BAR, ST->text, 12));
        if (i == drag_place) st_frame(cv, &r, rad, max(1, S(1)), ST->selection_border);
        if (cur)
        {
            /* the accent pill marks where we are */
            RECT pill = { r.left, r.top + S(9), r.left + S(ST_PILL_H), r.bottom - S(9) };
            st_round(cv, &pill, S(2), ST->accent);
        }
        if (focused && i == side_sel) st_focus_ring(cv, &r, rad, dpi);
        st_icon(cv, p->licon == TI_RECYCLE_BIN_EMPTY && recycle_count() > 0 ? TI_RECYCLE_BIN_FULL : p->licon,
                r.left + S(14), r.top + (p->h - isz) / 2, isz, cur ? ST->accent : ST->text);
        r.left += S(14) + isz + S(12);
        if (!wcscmp(p->target, L"::recycle"))
        {
            int n = recycle_count();
            if (n > 0)
            {
                WCHAR cnt[16];
                RECT cr = r;
                _snwprintf(cnt, ARRAYSIZE(cnt), L"%d", n);
                cr.right -= S(10);
                st_text(cv, cnt, &cr, SF_BODY, C_MUTED, DT_SINGLELINE | DT_VCENTER | DT_RIGHT);
                r.right -= S(34);
            }
        }
        st_text(cv, p->label, &r, cur ? SF_CAPTION : SF_BODY, C_TEXT, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
    }
}

static void navigate(const WCHAR *target, int mode);
static void show_menu_for_place(int i, POINT pt);

static void side_move(int delta)
{
    int i = side_sel, n = 0;
    if (i < 0) for (i = 0; i < nplaces && !place_is_current(&places[i]); i++);
    do
    {
        i += delta;
        if (i < 0 || i >= nplaces) return;
        n++;
    } while (places[i].kind != 1 && n < MAX_PLACES);
    if (places[i].kind == 1) side_sel = i;
    InvalidateRect(sidebar, NULL, FALSE);
}

static LRESULT CALLBACK sidebar_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_PAINT:
        st_paint_window(hwnd, paint_sidebar);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        if (msg == WM_SETFOCUS && side_sel < 0)
        {
            int i;
            for (i = 0; i < nplaces; i++)
                if (place_is_current(&places[i])) side_sel = i;
            if (side_sel < 0) side_move(1);
        }
        InvalidateRect(hwnd, NULL, FALSE);
        write_state();
        return 0;
    case WM_MOUSEMOVE:
    {
        int i = place_at(GET_Y_LPARAM(lp));
        if (i >= 0 && places[i].kind != 1) i = -1;
        if (i != side_hot) { side_hot = i; InvalidateRect(hwnd, NULL, FALSE); }
        if (!tracking_mouse)
        {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
            tracking_mouse = TrackMouseEvent(&tme);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        tracking_mouse = FALSE;
        side_hot = -1;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_LBUTTONDOWN:
    {
        int i = place_at(GET_Y_LPARAM(lp));
        SetFocus(hwnd);
        if (i >= 0 && (places[i].kind == 1 || places[i].target[0]))
        {
            side_sel = i;
            navigate(places[i].target, 0);
        }
        return 0;
    }
    case WM_RBUTTONUP:
    {
        int i = place_at(GET_Y_LPARAM(lp));
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        if (i >= 0 && places[i].kind == 1)
        {
            side_sel = i;
            InvalidateRect(hwnd, NULL, FALSE);
            ClientToScreen(hwnd, &pt);
            show_menu_for_place(i, pt);
        }
        return 0;
    }
    case WM_CONTEXTMENU:
        if (GET_X_LPARAM(lp) == -1 && side_sel >= 0)
        {
            POINT pt = { S(40), places[side_sel].y + places[side_sel].h / 2 };
            ClientToScreen(hwnd, &pt);
            show_menu_for_place(side_sel, pt);
        }
        return 0;
    case WM_GETDLGCODE:
        return DLGC_WANTARROWS;
    case WM_KEYDOWN:
        switch (wp)
        {
        case VK_UP: side_move(-1); return 0;
        case VK_DOWN: side_move(1); return 0;
        case VK_HOME: side_sel = -1; side_move(1); return 0;
        case VK_END: side_sel = nplaces; side_move(-1); return 0;
        case VK_RETURN: case VK_SPACE:
            if (side_sel >= 0 && places[side_sel].kind == 1) navigate(places[side_sel].target, 0);
            return 0;
        case VK_APPS:
            SendMessageW(hwnd, WM_CONTEXTMENU, (WPARAM)hwnd, MAKELPARAM(-1, -1));
            return 0;
        }
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ------------------------------------------------------------------ */
/* Change notification                                                  */

static void watch_current(void)
{
    SHChangeNotifyEntry entry;
    LPITEMIDLIST pidl = NULL;

    if (change_handle != INVALID_HANDLE_VALUE) FindCloseChangeNotification(change_handle);
    change_handle = INVALID_HANDLE_VALUE;
    if (shnotify_id) SHChangeNotifyDeregister(shnotify_id);
    shnotify_id = 0;

    if (cur_loc == LOC_FS)
    {
        change_handle = FindFirstChangeNotificationW(cur_path, FALSE,
            FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_ATTRIBUTES |
            FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE);
        pidl = ILCreateFromPathW(cur_path);
    }
    else if (cur_loc == LOC_RECYCLE)
        SHGetSpecialFolderLocation(NULL, CSIDL_BITBUCKET, &pidl);
    /* Shell-level notifications (SHChangeNotify) cover changes other than
     * the folder's own files: the Recycle Bin, drives, associations. */
    entry.pidl = pidl;
    entry.fRecursive = FALSE;
    shnotify_id = SHChangeNotifyRegister(main_wnd, SHCNRF_ShellLevel | SHCNRF_InterruptLevel,
                                         SHCNE_ALLEVENTS, WM_APP_SHNOTIFY, 1, &entry);
    if (pidl) CoTaskMemFree(pidl);
}

static void schedule_reload(void)
{
    SetTimer(main_wnd, TIMER_RELOAD, 200, NULL);
}

/* ------------------------------------------------------------------ */
/* Navigation                                                           */

enum { NAV_NEW, NAV_BACK, NAV_FORWARD, NAV_UP };

static void update_title(void)
{
    WCHAR title[MAX_PATH + 32];
    const WCHAR *name = cur_loc == LOC_FS ? PathFindFileNameW(cur_path) : loc_display();
    WCHAR tmp[MAX_PATH];
    if (cur_loc == LOC_FS && is_root(cur_path))
    {
        drive_label(cur_path, tmp, ARRAYSIZE(tmp), NULL);
        name = tmp;
    }
    _snwprintf(title, ARRAYSIZE(title), L"%ls - Files", name && *name ? name : cur_path);
    title[ARRAYSIZE(title) - 1] = 0;
    SetWindowTextW(main_wnd, title);
}

/* Parse what the user typed or what a place points at. Returns 1 for a
 * folder (fills *loc and path), 2 for a file (path), 0 if not found. */
static int resolve(const WCHAR *in, enum loc *loc, WCHAR *path)
{
    WCHAR buf[MAX_PATH], exp[MAX_PATH], full[MAX_PATH];
    DWORD attrs;
    int len;

    while (*in == L' ') in++;
    lstrcpynW(buf, in, ARRAYSIZE(buf));
    len = (int)wcslen(buf);
    while (len && buf[len - 1] == L' ') buf[--len] = 0;
    if (len >= 2 && buf[0] == L'"' && buf[len - 1] == L'"') { buf[len - 1] = 0; memmove(buf, buf + 1, len * sizeof(WCHAR)); }

    if (!buf[0] || !lstrcmpiW(buf, L"::drives") || !lstrcmpiW(buf, L"Drives") || !lstrcmpiW(buf, L"shell:MyComputerFolder"))
    {
        *loc = LOC_DRIVES;
        path[0] = 0;
        return 1;
    }
    if (!lstrcmpiW(buf, L"::recycle") || !lstrcmpiW(buf, L"Recycle Bin") || !lstrcmpiW(buf, L"shell:RecycleBinFolder"))
    {
        *loc = LOC_RECYCLE;
        path[0] = 0;
        return 1;
    }
    if (buf[0] == L'~' && (!buf[1] || buf[1] == L'\\' || buf[1] == L'/'))
    {
        WCHAR *home = NULL;
        if (SUCCEEDED(SHGetKnownFolderPath(&FOLDERID_Profile, 0, NULL, &home)) && home)
        {
            _snwprintf(exp, ARRAYSIZE(exp), L"%ls%ls", home, buf + 1);
            exp[ARRAYSIZE(exp) - 1] = 0;
            CoTaskMemFree(home);
            lstrcpynW(buf, exp, ARRAYSIZE(buf));
        }
    }
    ExpandEnvironmentStringsW(buf, exp, ARRAYSIZE(exp));
    if (cur_loc == LOC_FS && PathIsRelativeW(exp) && !(exp[0] && exp[1] == L':'))
    {
        WCHAR joined[MAX_PATH];
        PathCombineW(joined, cur_path, exp);
        lstrcpynW(exp, joined, ARRAYSIZE(exp));
    }
    if (exp[0] && exp[1] == L':' && !exp[2]) wcscat(exp, L"\\"); /* "C:" means the root */
    if (!GetFullPathNameW(exp, ARRAYSIZE(full), full, NULL)) return 0;
    attrs = GetFileAttributesW(full);
    if (attrs == INVALID_FILE_ATTRIBUTES) return 0;
    if (!is_root(full)) PathRemoveBackslashW(full);
    lstrcpynW(path, full, MAX_PATH);
    *loc = LOC_FS;
    return (attrs & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 2;
}

static void open_file(const WCHAR *path);

static void push_hist(WCHAR stack[][MAX_PATH], int *n, const WCHAR *s)
{
    if (*n == MAX_HIST)
    {
        memmove(stack[0], stack[1], (MAX_HIST - 1) * sizeof(stack[0]));
        (*n)--;
    }
    lstrcpynW(stack[(*n)++], s, MAX_PATH);
}

static void navigate(const WCHAR *target, int mode)
{
    enum loc loc;
    WCHAR path[MAX_PATH], prev[MAX_PATH], came_from[MAX_PATH] = L"";
    int r = resolve(target, &loc, path);

    if (r == 0)
    {
        report_error(L"Can't find \"%ls\".", target);
        SetWindowTextW(addr, loc_display());
        return;
    }
    if (r == 2)
    {
        open_file(path);
        SetWindowTextW(addr, loc_display());
        return;
    }
    loc_string(prev, ARRAYSIZE(prev));
    if (mode == NAV_UP && cur_loc == LOC_FS) lstrcpyW(came_from, PathFindFileNameW(cur_path));
    if (mode == NAV_UP && cur_loc == LOC_FS && is_root(cur_path)) drive_label(cur_path, came_from, ARRAYSIZE(came_from), NULL);
    if (loc == cur_loc && (loc != LOC_FS || !lstrcmpiW(path, cur_path)) && mode == NAV_NEW && nitems)
    {
        reload();
        return;
    }
    if (prev[0] && wcscmp(prev, L"::none"))
    {
        if (mode == NAV_BACK) push_hist(fwd_stack, &nfwd, prev);
        else
        {
            push_hist(back_stack, &nback, prev);
            if (mode != NAV_FORWARD) nfwd = 0;
        }
    }
    cur_loc = loc;
    lstrcpynW(cur_path, path, ARRAYSIZE(cur_path));
    if (cur_loc == LOC_RECYCLE) sort_col = 0;
    sort_col = 0;
    sort_desc = FALSE;
    side_sel = -1;
    SetWindowTextW(addr, loc_display());
    update_title();
    free_items();
    if (cur_loc == LOC_FS && !load_fs(cur_path))
        set_error(L"Can't open this folder: error %lu.", GetLastError());
    else
    {
        if (cur_loc == LOC_DRIVES) load_drives();
        else if (cur_loc == LOC_RECYCLE) load_recycle();
        status_text[0] = 0;
        status_error = FALSE;
    }
    fill_list(NULL, 0, NULL);
    if (came_from[0])
    {
        int it = find_item_by_name(came_from);
        if (it >= 0) select_only(it, FALSE);
    }
    watch_current();
    layout();
    update_toolbar();
    InvalidateRect(main_wnd, NULL, FALSE);
    paint_sidebar_later();
    write_state();
}

static void go_back(void)
{
    WCHAR t[MAX_PATH];
    if (!nback) return;
    lstrcpyW(t, back_stack[--nback]);
    navigate(t, NAV_BACK);
}

static void go_forward(void)
{
    WCHAR t[MAX_PATH];
    if (!nfwd) return;
    lstrcpyW(t, fwd_stack[--nfwd]);
    navigate(t, NAV_FORWARD);
}

static BOOL can_go_up(void) { return cur_loc == LOC_FS; }

static void go_up(void)
{
    WCHAR t[MAX_PATH];
    if (cur_loc != LOC_FS) return;
    if (is_root(cur_path)) { navigate(L"::drives", NAV_UP); return; }
    lstrcpyW(t, cur_path);
    PathRemoveFileSpecW(t);
    if (t[0] && t[1] == L':' && !t[2]) wcscat(t, L"\\");
    navigate(t, NAV_UP);
}

/* ------------------------------------------------------------------ */
/* Opening and "Open with"                                              */

static BOOL run_with_class(HKEY root, const WCHAR *key, const WCHAR *file)
{
    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    HKEY h;
    BOOL ok;
    WCHAR dir[MAX_PATH];

    if (RegOpenKeyExW(root, key, 0, KEY_READ, &h)) return FALSE;
    lstrcpynW(dir, file, MAX_PATH);
    PathRemoveFileSpecW(dir);
    sei.fMask = SEE_MASK_CLASSKEY | SEE_MASK_FLAG_NO_UI;
    sei.hkeyClass = h;
    sei.hwnd = main_wnd;
    sei.lpVerb = L"open";
    sei.lpFile = file;
    sei.lpDirectory = dir;
    sei.nShow = SW_SHOWNORMAL;
    ok = ShellExecuteExW(&sei);
    if (!ok) report_error(L"Couldn't open \"%ls\": error %lu.", PathFindFileNameW(file), GetLastError());
    RegCloseKey(h);
    return ok;
}

/* The per-user choice saved by "Choose another program…" (in
 * HKCU\Software\Classes\.ext). On Windows the merged HKCR view shows it;
 * we look it up directly so it works whether or not HKCR merges it. */
static BOOL user_choice(const WCHAR *ext, WCHAR *key, int len)
{
    WCHAR sub[MAX_PATH], progid[MAX_PATH];
    DWORD size = sizeof(progid);
    HKEY h;

    if (!ext[0]) return FALSE;
    _snwprintf(sub, ARRAYSIZE(sub), L"Software\\Classes\\%ls", ext);
    sub[ARRAYSIZE(sub) - 1] = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, sub, NULL, RRF_RT_REG_SZ, NULL, progid, &size) || !progid[0]) return FALSE;
    _snwprintf(key, len, L"Software\\Classes\\%ls\\shell\\open\\command", progid);
    key[len - 1] = 0;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, key, 0, KEY_READ, &h)) return FALSE;
    RegCloseKey(h);
    _snwprintf(key, len, L"Software\\Classes\\%ls", progid);
    key[len - 1] = 0;
    return TRUE;
}

static void open_file(const WCHAR *path)
{
    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    WCHAR dir[MAX_PATH], key[MAX_PATH];
    const WCHAR *ext = PathFindExtensionW(path);

    if (user_choice(ext, key, ARRAYSIZE(key)))
    {
        if (run_with_class(HKEY_CURRENT_USER, key, path)) set_status(L"Opened %ls", PathFindFileNameW(path));
        return;
    }
    lstrcpynW(dir, path, MAX_PATH);
    PathRemoveFileSpecW(dir);
    sei.fMask = SEE_MASK_FLAG_NO_UI;
    sei.hwnd = main_wnd;
    sei.lpVerb = NULL;          /* the default verb */
    sei.lpFile = path;
    sei.lpDirectory = dir;
    sei.nShow = SW_SHOWNORMAL;
    if (ShellExecuteExW(&sei))
    {
        set_status(L"Opened %ls", PathFindFileNameW(path));
        return;
    }
    if (GetLastError() == ERROR_NO_ASSOCIATION || (INT_PTR)sei.hInstApp == SE_ERR_NOASSOC)
    {
        set_error(L"No program is set to open %ls files. Use Open with to choose one.", *ext ? ext : L"these");
        if (!automation) PostMessageW(main_wnd, WM_COMMAND, IDM_CHOOSE_PROGRAM, 0);
        return;
    }
    report_error(L"Couldn't open \"%ls\": error %lu.", PathFindFileNameW(path), GetLastError());
}

static void open_item(int it)
{
    if (it < 0) return;
    if (cur_loc == LOC_RECYCLE)
    {
        set_status(L"\"%ls\" is in the Recycle Bin. Restore it to open it.", items[it].name);
        return;
    }
    if (items[it].dir) navigate(items[it].path, NAV_NEW);
    else open_file(items[it].path);
}

static void exe_from_command(const WCHAR *cmd, WCHAR *exe, int len)
{
    WCHAR expanded[MAX_PATH * 2];
    const WCHAR *p;
    ExpandEnvironmentStringsW(cmd, expanded, ARRAYSIZE(expanded));
    p = expanded;
    while (*p == L' ') p++;
    if (*p == L'"')
    {
        const WCHAR *e = wcschr(p + 1, L'"');
        int n = e ? (int)(e - p - 1) : (int)wcslen(p + 1);
        lstrcpynW(exe, p + 1, min(n + 1, len));
    }
    else
    {
        const WCHAR *e = wcschr(p, L' ');
        int n = e ? (int)(e - p) : (int)wcslen(p);
        lstrcpynW(exe, p, min(n + 1, len));
    }
}

static void pretty_exe_name(const WCHAR *exe, WCHAR *buf, int len)
{
    WCHAR *dot;
    lstrcpynW(buf, PathFindFileNameW(exe), len);
    if ((dot = wcsrchr(buf, L'.')) && !lstrcmpiW(dot, L".exe")) *dot = 0;
    if (buf[0]) buf[0] = towupper(buf[0]);
}

static void add_handler(const WCHAR *name, const WCHAR *exe, HKEY root, const WCHAR *key, BOOL is_default,
                        const WCHAR *source)
{
    struct handler *h;
    WCHAR full[MAX_PATH];
    int i;

    if (nhandlers >= MAX_HANDLERS || !exe[0]) return;
    lstrcpynW(full, exe, MAX_PATH);
    if (PathIsRelativeW(full) || !wcschr(full, L'\\'))
        SearchPathW(NULL, exe, NULL, ARRAYSIZE(full), full, NULL);
    for (i = 0; i < nhandlers; i++)
        if (!lstrcmpiW(handlers[i].exe, full) || !lstrcmpiW(PathFindFileNameW(handlers[i].exe), PathFindFileNameW(full)))
        {
            if (is_default) handlers[i].is_default = TRUE;
            return;
        }
    h = &handlers[nhandlers++];
    memset(h, 0, sizeof(*h));
    if (name && name[0]) lstrcpynW(h->name, name, ARRAYSIZE(h->name));
    else pretty_exe_name(full, h->name, ARRAYSIZE(h->name));
    lstrcpynW(h->exe, full, ARRAYSIZE(h->exe));
    h->root = root;
    lstrcpynW(h->key, key, ARRAYSIZE(h->key));
    h->is_default = is_default;
    h->source = source;
}

/* A ProgID's handler, looked up through the association API. */
static void add_progid_handler(const WCHAR *progid, BOOL is_default, const WCHAR *source)
{
    WCHAR exe[MAX_PATH] = L"", name[128] = L"", key[MAX_PATH];
    DWORD n = ARRAYSIZE(exe);
    HKEY h;

    _snwprintf(key, ARRAYSIZE(key), L"%ls\\shell", progid);
    key[ARRAYSIZE(key) - 1] = 0;
    if (RegOpenKeyExW(HKEY_CLASSES_ROOT, key, 0, KEY_READ, &h)) return;
    RegCloseKey(h);
    if (FAILED(AssocQueryStringW(ASSOCF_NOTRUNCATE, ASSOCSTR_EXECUTABLE, progid, L"open", exe, &n))) return;
    n = ARRAYSIZE(name);
    if (FAILED(AssocQueryStringW(ASSOCF_NOTRUNCATE, ASSOCSTR_FRIENDLYAPPNAME, progid, L"open", name, &n))) name[0] = 0;
    add_handler(name, exe, HKEY_CLASSES_ROOT, progid, is_default, source);
}

/* An Applications\<exe> registration (from an OpenWithList). */
static void add_app_handler(HKEY root, const WCHAR *classes, const WCHAR *exe_name, const WCHAR *source)
{
    WCHAR key[MAX_PATH], cmd[MAX_PATH * 2], exe[MAX_PATH], name[128] = L"";
    DWORD size = sizeof(cmd);

    _snwprintf(key, ARRAYSIZE(key), L"%lsApplications\\%ls\\shell\\open\\command", classes, exe_name);
    key[ARRAYSIZE(key) - 1] = 0;
    if (RegGetValueW(root, key, NULL, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND, NULL, cmd, &size))
    {
        /* no registration: the bare program, if it can be found */
        if (!SearchPathW(NULL, exe_name, NULL, ARRAYSIZE(exe), exe, NULL)) return;
        _snwprintf(key, ARRAYSIZE(key), L"%lsApplications\\%ls", classes, exe_name);
        add_handler(NULL, exe, root, key, FALSE, source);
        return;
    }
    exe_from_command(cmd, exe, ARRAYSIZE(exe));
    _snwprintf(key, ARRAYSIZE(key), L"%lsApplications\\%ls", classes, exe_name);
    key[ARRAYSIZE(key) - 1] = 0;
    size = sizeof(name);
    {
        WCHAR k2[MAX_PATH];
        _snwprintf(k2, ARRAYSIZE(k2), L"%lsApplications\\%ls", classes, exe_name);
        k2[ARRAYSIZE(k2) - 1] = 0;
        if (RegGetValueW(root, k2, L"FriendlyAppName", RRF_RT_REG_SZ, NULL, name, &size)) name[0] = 0;
    }
    add_handler(name, exe, root, key, FALSE, source);
}

static void scan_open_with_list(HKEY root, const WCHAR *classes, const WCHAR *sub, const WCHAR *source)
{
    WCHAR key[MAX_PATH], name[MAX_PATH];
    DWORD i, n;
    HKEY h;

    _snwprintf(key, ARRAYSIZE(key), L"%ls%ls\\OpenWithList", classes, sub);
    key[ARRAYSIZE(key) - 1] = 0;
    if (RegOpenKeyExW(root, key, 0, KEY_READ, &h)) return;
    for (i = 0; n = ARRAYSIZE(name), !RegEnumKeyExW(h, i, name, &n, NULL, NULL, NULL, NULL); i++)
        add_app_handler(root, classes, name, source);
    RegCloseKey(h);
}

static void scan_open_with_progids(HKEY root, const WCHAR *classes, const WCHAR *ext, const WCHAR *source)
{
    WCHAR key[MAX_PATH], name[MAX_PATH];
    DWORD i, n;
    HKEY h;

    _snwprintf(key, ARRAYSIZE(key), L"%ls%ls\\OpenWithProgids", classes, ext);
    key[ARRAYSIZE(key) - 1] = 0;
    if (RegOpenKeyExW(root, key, 0, KEY_READ, &h)) return;
    for (i = 0; n = ARRAYSIZE(name), !RegEnumValueW(h, i, name, &n, NULL, NULL, NULL, NULL); i++)
        add_progid_handler(name, FALSE, source);
    RegCloseKey(h);
}

/* Programs that can open files like PATH, default first. */
static void gather_handlers(const WCHAR *path)
{
    const WCHAR *ext = PathFindExtensionW(path);
    WCHAR progid[MAX_PATH] = L"", key[MAX_PATH], perceived[64] = L"";
    DWORD size;

    if (!lstrcmpiW(handlers_for, path)) return;
    lstrcpynW(handlers_for, path, ARRAYSIZE(handlers_for));
    nhandlers = 0;

    /* the user's own choice first */
    if (user_choice(ext, key, ARRAYSIZE(key)))
    {
        WCHAR cmd[MAX_PATH * 2], exe[MAX_PATH], k2[MAX_PATH];
        size = sizeof(cmd);
        _snwprintf(k2, ARRAYSIZE(k2), L"%ls\\shell\\open\\command", key);
        if (!RegGetValueW(HKEY_CURRENT_USER, k2, NULL, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND, NULL, cmd, &size))
        {
            exe_from_command(cmd, exe, ARRAYSIZE(exe));
            add_handler(NULL, exe, HKEY_CURRENT_USER, key, TRUE, L"user-choice");
        }
    }
    if (ext[0])
    {
        size = sizeof(progid);
        if (RegGetValueW(HKEY_CLASSES_ROOT, ext, NULL, RRF_RT_REG_SZ, NULL, progid, &size)) progid[0] = 0;
        if (progid[0]) add_progid_handler(progid, nhandlers == 0, L"default");
        scan_open_with_progids(HKEY_CURRENT_USER, L"Software\\Classes\\", ext, L"user-progids");
        scan_open_with_progids(HKEY_CLASSES_ROOT, L"", ext, L"progids");
        scan_open_with_list(HKEY_CURRENT_USER, L"Software\\Classes\\", ext, L"user-list");
        scan_open_with_list(HKEY_CLASSES_ROOT, L"", ext, L"list");
        size = sizeof(perceived);
        if (!RegGetValueW(HKEY_CLASSES_ROOT, ext, L"PerceivedType", RRF_RT_REG_SZ, NULL, perceived, &size) && perceived[0])
        {
            _snwprintf(key, ARRAYSIZE(key), L"SystemFileAssociations\\%ls", perceived);
            key[ARRAYSIZE(key) - 1] = 0;
            scan_open_with_list(HKEY_CLASSES_ROOT, L"", key, L"perceived");
        }
    }
    scan_open_with_list(HKEY_CLASSES_ROOT, L"", L"*", L"any");
}

static BOOL set_reg_sz(HKEY root, const WCHAR *key, const WCHAR *name, const WCHAR *value)
{
    HKEY h;
    LONG r = RegCreateKeyExW(root, key, 0, NULL, 0, KEY_SET_VALUE, NULL, &h, NULL);
    if (r) return FALSE;
    r = RegSetValueExW(h, name, 0, REG_SZ, (const BYTE *)value, (DWORD)((wcslen(value) + 1) * sizeof(WCHAR)));
    RegCloseKey(h);
    return !r;
}

/* Remember EXE as a program for files with extension EXT (per user, in
 * HKCU\Software\Classes), and optionally make it the default. */
static BOOL save_choice(const WCHAR *ext, const WCHAR *exe, BOOL make_default)
{
    WCHAR key[MAX_PATH], cmd[MAX_PATH + 16], progid[MAX_PATH];
    const WCHAR *exe_name = PathFindFileNameW(exe);
    HKEY h;
    BOOL ok;

    if (!ext[0]) return FALSE;
    _snwprintf(cmd, ARRAYSIZE(cmd), L"\"%ls\" \"%%1\"", exe);
    cmd[ARRAYSIZE(cmd) - 1] = 0;
    _snwprintf(key, ARRAYSIZE(key), L"Software\\Classes\\Applications\\%ls\\shell\\open\\command", exe_name);
    ok = set_reg_sz(HKEY_CURRENT_USER, key, NULL, cmd);
    _snwprintf(key, ARRAYSIZE(key), L"Software\\Classes\\%ls\\OpenWithList\\%ls", ext, exe_name);
    if (!RegCreateKeyExW(HKEY_CURRENT_USER, key, 0, NULL, 0, KEY_READ, NULL, &h, NULL)) RegCloseKey(h);
    else ok = FALSE;
    if (make_default)
    {
        WCHAR base[MAX_PATH], *dot;
        lstrcpynW(base, exe_name, ARRAYSIZE(base));
        if ((dot = wcsrchr(base, L'.'))) *dot = 0;
        _snwprintf(progid, ARRAYSIZE(progid), L"Setu.Choice.%ls%ls", base, ext);
        _snwprintf(key, ARRAYSIZE(key), L"Software\\Classes\\%ls\\shell\\open\\command", progid);
        ok = set_reg_sz(HKEY_CURRENT_USER, key, NULL, cmd) && ok;
        _snwprintf(key, ARRAYSIZE(key), L"Software\\Classes\\%ls", ext);
        ok = set_reg_sz(HKEY_CURRENT_USER, key, NULL, progid) && ok;
        _snwprintf(key, ARRAYSIZE(key), L"Software\\Classes\\%ls\\OpenWithProgids", ext);
        if (!RegCreateKeyExW(HKEY_CURRENT_USER, key, 0, NULL, 0, KEY_SET_VALUE, NULL, &h, NULL))
        {
            RegSetValueExW(h, progid, 0, REG_NONE, NULL, 0);
            RegCloseKey(h);
        }
    }
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
    handlers_for[0] = 0;
    return ok;
}

static void run_handler(int i, const WCHAR *path)
{
    if (i < 0 || i >= nhandlers) return;
    if (run_with_class(handlers[i].root, handlers[i].key, path))
        set_status(L"Opened %ls with %ls", PathFindFileNameW(path), handlers[i].name);
}

static void choose_program(const WCHAR *path, const WCHAR *exe_in, int always /* -1 ask */)
{
    WCHAR exe[MAX_PATH] = L"", ext_desc[64];
    const WCHAR *ext = PathFindExtensionW(path);
    int answer;

    if (exe_in) lstrcpynW(exe, exe_in, ARRAYSIZE(exe));
    else
    {
        OPENFILENAMEW ofn = { sizeof(ofn) };
        ofn.hwndOwner = main_wnd;
        ofn.lpstrFilter = L"Programs (*.exe)\0*.exe\0All files\0*.*\0";
        ofn.lpstrFile = exe;
        ofn.nMaxFile = ARRAYSIZE(exe);
        ofn.lpstrTitle = L"Choose a program";
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
        if (!GetOpenFileNameW(&ofn)) return;
    }
    if (always < 0)
    {
        WCHAR q[512];
        _snwprintf(ext_desc, ARRAYSIZE(ext_desc), L"%ls", *ext ? ext : L"(no extension)");
        _snwprintf(q, ARRAYSIZE(q), L"Always use %ls to open %ls files?\n\nYes: make it the default.\nNo: open this file once.",
                   PathFindFileNameW(exe), ext_desc);
        answer = MessageBoxW(main_wnd, q, L"Open with", MB_YESNOCANCEL | MB_ICONQUESTION);
        if (answer == IDCANCEL) return;
        always = answer == IDYES;
    }
    if (!save_choice(ext, exe, always)) set_error(L"Couldn't save the program choice.");
    gather_handlers(path);
    {
        int i;
        for (i = 0; i < nhandlers; i++)
            if (!lstrcmpiW(PathFindFileNameW(handlers[i].exe), PathFindFileNameW(exe))) { run_handler(i, path); break; }
        if (i == nhandlers) report_error(L"Couldn't run %ls.", exe);
    }
    write_state();
}

/* ------------------------------------------------------------------ */
/* File operations (SHFileOperationW, so the personality does the work) */

static const WCHAR *fileop_error_text(int code)
{
    /* the documented SHFileOperation return values, in our words */
    switch (code)
    {
    case 0x71: return L"The source and destination are the same file.";
    case 0x72: return L"Several source files were given with one destination file.";
    case 0x73: return L"Rename only works within one folder.";
    case 0x74: return L"A drive root can't be moved or renamed.";
    case 0x75: return L"The operation was cancelled.";
    case 0x76: return L"The destination is inside the source folder.";
    case 0x78: return L"Access to the source was denied.";
    case 0x79: return L"The path is too deep.";
    case 0x7A: return L"There are more destinations than sources.";
    case 0x7C: return L"A source or destination path isn't valid.";
    case 0x7D: return L"The source and destination are the same folder.";
    case 0x7E: return L"A file exists where a folder should be created.";
    case 0x80: return L"A folder exists with the destination file's name.";
    case 0x81: return L"The name is too long.";
    case 0x85: return L"The destination is full.";
    case 0x86: return L"The destination is read-only.";
    case 0xB7: return L"An item with that name already exists.";
    case 0x402: return L"An unknown error occurred.";
    case 0x10000: return L"An error occurred at the destination.";
    }
    return NULL;
}

static void fileop_failed(const WCHAR *what, int code)
{
    const WCHAR *t = fileop_error_text(code);
    WCHAR sys[256] = L"";
    if (!t && FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, code, 0, sys,
                             ARRAYSIZE(sys), NULL))
    {
        WCHAR *e = sys + wcslen(sys);
        while (e > sys && (e[-1] == L'\n' || e[-1] == L'\r')) *--e = 0;
        t = sys;
    }
    report_error(L"Couldn't %ls: %ls (0x%x)", what, t ? t : L"error", code);
}

/* Run one SHFileOperationW. VERB is like "copy", DONE like "Copied". */
static BOOL file_op(UINT func, const WCHAR *from, const WCHAR *to, FILEOP_FLAGS flags, const WCHAR *verb,
                    const WCHAR *done, const WCHAR *ing)
{
    SHFILEOPSTRUCTW op;
    int n = multi_count(from), ret;

    memset(&op, 0, sizeof(op));
    op.hwnd = main_wnd;
    op.wFunc = func;
    op.pFrom = from;
    op.pTo = to;
    op.fFlags = flags;
    op.lpszProgressTitle = ing;
    set_status(L"%ls %d item%ls…", ing, n, n == 1 ? L"" : L"s");
    ret = SHFileOperationW(&op);
    _snwprintf(last_op, ARRAYSIZE(last_op), L"%ls|%d|0x%x|%d", verb, n, ret, op.fAnyOperationsAborted);
    if (op.fAnyOperationsAborted) set_status(L"Cancelled.");
    else if (ret) fileop_failed(verb, ret);
    else set_status(L"%ls %d item%ls.", done, n, n == 1 ? L"" : L"s");
    schedule_reload();
    paint_sidebar_later();
    return !ret && !op.fAnyOperationsAborted;
}

static WCHAR *selected_paths(int *count)
{
    int sel[4096], n = get_selection(sel, ARRAYSIZE(sel)), i;
    WCHAR **paths, *m;

    *count = 0;
    if (!n || cur_loc == LOC_DRIVES) return NULL;
    paths = malloc(n * sizeof(*paths));
    if (!paths) return NULL;
    for (i = 0; i < n; i++) paths[i] = items[sel[i]].path;
    m = multi_sz(paths, n);
    free(paths);
    *count = n;
    return m;
}

/* "name - Copy.ext", "name - Copy (2).ext"... that doesn't exist in DIR. */
static void copy_name(const WCHAR *dir, const WCHAR *name, BOOL is_dir, WCHAR *out)
{
    WCHAR base[MAX_PATH], ext[MAX_PATH] = L"", cand[MAX_PATH];
    int k;
    lstrcpynW(base, name, MAX_PATH);
    if (!is_dir)
    {
        WCHAR *dot = wcsrchr(base, L'.');
        if (dot && dot != base) { lstrcpyW(ext, dot); *dot = 0; }
    }
    for (k = 1; k < 1000; k++)
    {
        if (k == 1) _snwprintf(cand, MAX_PATH, L"%ls - Copy%ls", base, ext);
        else _snwprintf(cand, MAX_PATH, L"%ls - Copy (%d)%ls", base, k, ext);
        cand[MAX_PATH - 1] = 0;
        PathCombineW(out, dir, cand);
        if (GetFileAttributesW(out) == INVALID_FILE_ATTRIBUTES) return;
    }
}

/* Copy or move the double-null list FROM into folder DEST. */
static BOOL transfer(const WCHAR *from, const WCHAR *dest, BOOL move)
{
    const WCHAR *p;
    WCHAR parent[MAX_PATH], dst[MAX_PATH];
    BOOL same = FALSE, all_same = TRUE, ok;
    int n = 0;

    lstrcpynW(dst, dest, MAX_PATH);
    PathAddBackslashW(dst);
    for (p = from; *p; p += wcslen(p) + 1, n++)
    {
        WCHAR t[MAX_PATH];
        same_parent(p, parent);
        if (!lstrcmpiW(parent, dst)) same = TRUE;
        else all_same = FALSE;
        lstrcpynW(t, p, MAX_PATH);
        PathAddBackslashW(t);
        if (!_wcsnicmp(dst, t, wcslen(t)))
        {
            report_error(L"Can't %ls \"%ls\" into itself.", move ? L"move" : L"copy", PathFindFileNameW(p));
            return FALSE;
        }
    }
    if (!n) return FALSE;
    if (move && all_same) { set_status(L"Already in this folder."); return TRUE; }
    if (!move && same)
    {
        /* Copies next to the originals get "- Copy" names. The personality
         * doesn't do FOF_RENAMEONCOLLISION yet, so name each one here. */
        WCHAR *tos = calloc((size_t)n * MAX_PATH + 1, sizeof(WCHAR)), *q = tos;
        if (!tos) return FALSE;
        for (p = from; *p; p += wcslen(p) + 1)
        {
            WCHAR target[MAX_PATH];
            DWORD a = GetFileAttributesW(p);
            same_parent(p, parent);
            if (!lstrcmpiW(parent, dst)) copy_name(dest, PathFindFileNameW(p), a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY), target);
            else PathCombineW(target, dest, PathFindFileNameW(p));
            wcscpy(q, target);
            q += wcslen(target) + 1;
        }
        *q = 0;
        ok = file_op(FO_COPY, from, tos, FOF_ALLOWUNDO | FOF_MULTIDESTFILES, L"copy", L"Copied", L"Copying");
        free(tos);
        return ok;
    }
    {
        WCHAR *to = calloc(wcslen(dest) + 2, sizeof(WCHAR));
        if (!to) return FALSE;
        wcscpy(to, dest);
        ok = file_op(move ? FO_MOVE : FO_COPY, from, to, FOF_ALLOWUNDO, move ? L"move" : L"copy",
                     move ? L"Moved" : L"Copied", move ? L"Moving" : L"Copying");
        free(to);
        return ok;
    }
}

static void set_cut_paths(const WCHAR *m)
{
    size_t len = 0;
    const WCHAR *p;
    free(cut_paths);
    cut_paths = NULL;
    if (m)
    {
        for (p = m; *p; p += wcslen(p) + 1);
        len = p - m + 1;
        cut_paths = malloc(len * sizeof(WCHAR));
        if (cut_paths) memcpy(cut_paths, m, len * sizeof(WCHAR));
    }
    {
        int i;
        for (i = 0; i < ListView_GetItemCount(lv); i++)
        {
            int it = item_of(i);
            UINT want = it >= 0 && cut_paths && in_multi(cut_paths, items[it].path) ? LVIS_CUT : 0;
            ListView_SetItemState(lv, i, want, LVIS_CUT);
        }
    }
}

/* Put the selection on the clipboard as CF_HDROP plus the preferred drop
 * effect (copy or move), so other programs can paste it too. */
static void clipboard_put(BOOL cut)
{
    int n;
    WCHAR *m = selected_paths(&n);
    const WCHAR *p;
    size_t chars = 1;
    HGLOBAL g, ge;
    DROPFILES *df;

    if (!m) return;
    for (p = m; *p; p += wcslen(p) + 1) chars += wcslen(p) + 1;
    g = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DROPFILES) + chars * sizeof(WCHAR));
    ge = GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD));
    if (!g || !ge) { free(m); return; }
    df = GlobalLock(g);
    df->pFiles = sizeof(DROPFILES);
    df->fWide = TRUE;
    memcpy((BYTE *)df + sizeof(DROPFILES), m, chars * sizeof(WCHAR));
    GlobalUnlock(g);
    *(DWORD *)GlobalLock(ge) = cut ? DROPEFFECT_MOVE : DROPEFFECT_COPY | DROPEFFECT_LINK;
    GlobalUnlock(ge);
    if (!OpenClipboard(main_wnd)) { GlobalFree(g); GlobalFree(ge); free(m); report_error(L"The clipboard is busy."); return; }
    EmptyClipboard();
    SetClipboardData(CF_HDROP, g);
    SetClipboardData(cf_effect, ge);
    CloseClipboard();
    our_clip_seq = GetClipboardSequenceNumber();
    set_cut_paths(cut ? m : NULL);
    set_status(L"%ls %d item%ls.", cut ? L"Cut" : L"Copied", n, n == 1 ? L"" : L"s");
    free(m);
}

/* What's on the clipboard: a double-null list of paths, and the effect. */
static WCHAR *clipboard_get(BOOL *move)
{
    HDROP drop;
    WCHAR *m = NULL, *q;
    UINT n, i;
    size_t chars = 1;

    *move = FALSE;
    if (!IsClipboardFormatAvailable(CF_HDROP) || !OpenClipboard(main_wnd)) return NULL;
    if ((drop = GetClipboardData(CF_HDROP)))
    {
        HGLOBAL ge = GetClipboardData(cf_effect);
        if (ge)
        {
            DWORD *e = GlobalLock(ge);
            if (e) { *move = (*e & DROPEFFECT_MOVE) && !(*e & DROPEFFECT_COPY); GlobalUnlock(ge); }
        }
        n = DragQueryFileW(drop, 0xffffffff, NULL, 0);
        for (i = 0; i < n; i++) chars += DragQueryFileW(drop, i, NULL, 0) + 1;
        q = m = calloc(chars, sizeof(WCHAR));
        for (i = 0; m && i < n; i++)
        {
            UINT len = DragQueryFileW(drop, i, q, (UINT)(chars - (q - m)));
            q += len + 1;
        }
    }
    CloseClipboard();
    return m;
}

static void paste(void)
{
    BOOL move;
    WCHAR *m;
    if (cur_loc != LOC_FS) { set_error(L"Can't paste here."); return; }
    if (!(m = clipboard_get(&move)) || !m[0]) { set_status(L"Nothing to paste."); free(m); return; }
    if (transfer(m, cur_path, move) && move)
    {
        /* a moved item can't be pasted again */
        if (OpenClipboard(main_wnd)) { EmptyClipboard(); CloseClipboard(); }
        set_cut_paths(NULL);
    }
    free(m);
}

static void delete_selection(BOOL permanent)
{
    int n;
    WCHAR *m;
    if (cur_loc == LOC_RECYCLE)
    {
        set_status(L"Use the Recycle Bin's menu (right-click) to restore or delete items.");
        return;
    }
    if (!(m = selected_paths(&n))) return;
    file_op(FO_DELETE, m, NULL, permanent ? 0 : FOF_ALLOWUNDO, permanent ? L"delete" : L"recycle",
            permanent ? L"Deleted" : L"Moved to the Recycle Bin:", L"Deleting");
    free(m);
}

static BOOL valid_name(const WCHAR *name)
{
    int len = (int)wcslen(name);
    if (!len || wcspbrk(name, L"\\/:*?\"<>|")) return FALSE;
    if (name[len - 1] == L'.' || name[len - 1] == L' ') return FALSE;
    return wcscmp(name, L".") && wcscmp(name, L"..");
}

static BOOL rename_item(int it, const WCHAR *new_name)
{
    WCHAR from[MAX_PATH + 1], to[MAX_PATH + 1];
    BOOL ok;

    if (!valid_name(new_name))
    {
        report_error(L"A name can't be empty, end with a dot or space, or contain any of \\ / : * ? \" < > |");
        return FALSE;
    }
    if (!wcscmp(new_name, items[it].name)) return TRUE;
    memset(from, 0, sizeof(from));
    memset(to, 0, sizeof(to));
    lstrcpynW(from, items[it].path, MAX_PATH);
    PathCombineW(to, cur_path, new_name);
    to[wcslen(to) + 1] = 0;
    ok = file_op(FO_RENAME, from, to, FOF_ALLOWUNDO, L"rename", L"Renamed", L"Renaming");
    if (ok)
    {
        lstrcpynW(items[it].name, new_name, ARRAYSIZE(items[it].name));
        lstrcpynW(items[it].path, to, ARRAYSIZE(items[it].path));
        set_status(L"Renamed to %ls.", new_name);
    }
    return ok;
}

static void new_folder(void)
{
    WCHAR path[MAX_PATH], name[64];
    int k, it;

    if (cur_loc != LOC_FS) return;
    for (k = 1; k < 1000; k++)
    {
        if (k == 1) lstrcpyW(name, L"New folder");
        else _snwprintf(name, ARRAYSIZE(name), L"New folder (%d)", k);
        PathCombineW(path, cur_path, name);
        if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) break;
    }
    if (!CreateDirectoryW(path, NULL))
    {
        report_error(L"Couldn't create a folder: error %lu.", GetLastError());
        return;
    }
    SHChangeNotify(SHCNE_MKDIR, SHCNF_PATHW, path, NULL);
    KillTimer(main_wnd, TIMER_RELOAD);
    ListView_SetItemState(lv, -1, 0, LVIS_SELECTED);
    reload();
    it = find_item_by_name(name);
    set_status(L"Created %ls.", name);
    if (it >= 0) select_only(it, TRUE);
    write_state();
}

static void empty_recycle_bin(void)
{
    HRESULT hr = SHEmptyRecycleBinW(main_wnd, NULL, automation ? SHERB_NOCONFIRMATION : 0);
    _snwprintf(last_op, ARRAYSIZE(last_op), L"empty|0|0x%lx|0", hr);
    if (FAILED(hr) && hr != HRESULT_FROM_WIN32(ERROR_CANCELLED)) report_error(L"Couldn't empty the Recycle Bin (0x%lx).", hr);
    else if (hr == S_OK) set_status(L"Emptied the Recycle Bin.");
    schedule_reload();
    paint_sidebar_later();
}

/* ------------------------------------------------------------------ */
/* Recycle Bin item menu: the bin's own IContextMenu                    */

static void rb_menu_free(void)
{
    if (rb_menu) DestroyMenu(rb_menu);
    if (rb_menu_cm) IContextMenu_Release(rb_menu_cm);
    rb_menu = NULL;
    rb_menu_cm = NULL;
    rb_menu_count = 0;
}

static BOOL rb_menu_build(void)
{
    int sel[1024], n = get_selection(sel, ARRAYSIZE(sel)), i, count;
    LPCITEMIDLIST pidls[1024];
    IShellFolder2 *bin;
    HRESULT hr;

    rb_menu_free();
    if (cur_loc != LOC_RECYCLE || !n) return FALSE;
    if (!(bin = open_recycle_bin(NULL))) return FALSE;
    for (i = 0; i < n; i++) pidls[i] = items[sel[i]].pidl;
    hr = IShellFolder2_GetUIObjectOf(bin, main_wnd, n, pidls, &IID_IContextMenu, NULL, (void **)&rb_menu_cm);
    IShellFolder2_Release(bin);
    if (FAILED(hr) || !rb_menu_cm) { rb_menu_cm = NULL; return FALSE; }
    rb_menu = CreatePopupMenu();
    hr = IContextMenu_QueryContextMenu(rb_menu_cm, rb_menu, 0, SHELLMENU_FIRST, SHELLMENU_LAST, CMF_NORMAL);
    if (FAILED(hr)) { rb_menu_free(); return FALSE; }
    count = GetMenuItemCount(rb_menu);
    for (i = 0; i < count && rb_menu_count < (int)ARRAYSIZE(rb_menu_ids); i++)
    {
        MENUITEMINFOW mi = { sizeof(mi), MIIM_ID | MIIM_FTYPE | MIIM_STRING };
        WCHAR text[64] = L"", *s, *d;
        mi.dwTypeData = text;
        mi.cch = ARRAYSIZE(text);
        if (!GetMenuItemInfoW(rb_menu, i, TRUE, &mi) || (mi.fType & MFT_SEPARATOR)) continue;
        for (s = d = text; *s; s++) if (*s != L'&') *d++ = *s;
        *d = 0;
        lstrcpynW(rb_menu_text[rb_menu_count], text, 64);
        rb_menu_ids[rb_menu_count++] = mi.wID;
    }
    return TRUE;
}

static void rb_menu_invoke(UINT id)
{
    CMINVOKECOMMANDINFO ci = { sizeof(ci) };
    HRESULT hr;
    if (!rb_menu_cm || id < SHELLMENU_FIRST || id > SHELLMENU_LAST) return;
    ci.hwnd = main_wnd;
    ci.lpVerb = MAKEINTRESOURCEA(id - SHELLMENU_FIRST);
    ci.nShow = SW_SHOWNORMAL;
    hr = IContextMenu_InvokeCommand(rb_menu_cm, &ci);
    _snwprintf(last_op, ARRAYSIZE(last_op), L"bin-menu|%u|0x%lx|0", id - SHELLMENU_FIRST, hr);
    if (FAILED(hr)) report_error(L"The Recycle Bin couldn't do that (0x%lx).", hr);
    else set_status(L"Done.");
    rb_menu_free();
    schedule_reload();
    paint_sidebar_later();
}

/* ------------------------------------------------------------------ */
/* Properties                                                           */

struct props
{
    LONG refs;
    volatile LONG cancel, done;
    LONG shown_done; /* done, as of the text on screen */
    WCHAR *paths;             /* double-null list */
    int n;
    WCHAR title[MAX_PATH];
    WCHAR rows[10][2][MAX_PATH];
    int nrows;
    int size_row, contains_row;
    DWORD attrs_and, attrs_or;
    BOOL has_attrs;
    HICON icon;
    volatile ULONGLONG size;
    volatile LONG files, dirs;
};

static struct props *props;
static HWND p_ro, p_hidden, p_archive, p_ok, p_cancel, p_apply;

static void props_release(struct props *p)
{
    if (InterlockedDecrement(&p->refs)) return;
    free(p->paths);
    if (p->icon) DestroyIcon(p->icon);
    free(p);
}

static void size_walk(struct props *p, const WCHAR *dir, int depth)
{
    WCHAR pat[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    if (depth > 64 || p->cancel) return;
    PathCombineW(pat, dir, L"*");
    h = FindFirstFileExW(pat, FindExInfoBasic, &fd, FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE) return;
    do
    {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            WCHAR sub[MAX_PATH];
            InterlockedIncrement(&p->dirs);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue; /* don't follow links */
            PathCombineW(sub, dir, fd.cFileName);
            size_walk(p, sub, depth + 1);
        }
        else
        {
            p->size += ((ULONGLONG)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
            if (!(InterlockedIncrement(&p->files) % 512) && props_wnd) PostMessageW(props_wnd, WM_APP_SIZE, 0, 0);
        }
    } while (!p->cancel && FindNextFileW(h, &fd));
    FindClose(h);
}

static DWORD WINAPI size_thread(void *arg)
{
    struct props *p = arg;
    const WCHAR *s;
    for (s = p->paths; *s && !p->cancel; s += wcslen(s) + 1)
    {
        WIN32_FILE_ATTRIBUTE_DATA fa;
        if (!GetFileAttributesExW(s, GetFileExInfoStandard, &fa)) continue;
        if (fa.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            if (p->n > 1) InterlockedIncrement(&p->dirs);
            size_walk(p, s, 0);
        }
        else
        {
            p->size += ((ULONGLONG)fa.nFileSizeHigh << 32) | fa.nFileSizeLow;
            InterlockedIncrement(&p->files);
        }
    }
    InterlockedExchange(&p->done, 1);
    if (props_wnd) PostMessageW(props_wnd, WM_APP_SIZE, 1, 0);
    props_release(p);
    return 0;
}

static void props_size_text(void)
{
    WCHAR a[64], b[64];
    if (!props || props->size_row < 0) return;
    props->shown_done = props->done;
    format_bytes(props->size, a, ARRAYSIZE(a));
    format_count(props->size, b, ARRAYSIZE(b));
    _snwprintf(props->rows[props->size_row][1], MAX_PATH, L"%ls (%ls bytes)%ls", a, b, props->shown_done ? L"" : L"…");
    if (props->contains_row >= 0)
        _snwprintf(props->rows[props->contains_row][1], MAX_PATH, L"%ld file%ls, %ld folder%ls", props->files,
                   props->files == 1 ? L"" : L"s", props->dirs, props->dirs == 1 ? L"" : L"s");
}

static void props_add_row(const WCHAR *k, const WCHAR *v)
{
    if (props->nrows >= 10) return;
    lstrcpynW(props->rows[props->nrows][0], k, MAX_PATH);
    lstrcpynW(props->rows[props->nrows][1], v, MAX_PATH);
    props->nrows++;
}

static void props_close(void)
{
    if (props_wnd) DestroyWindow(props_wnd);
}

static int props_row_y(int i) { return S(84) + i * S(30); }

static void props_layout(void)
{
    RECT rc;
    int y, x = S(24), bw = S(84), bh = S(30), gap = S(8);
    if (!props_wnd) return;
    GetClientRect(props_wnd, &rc);
    y = props_row_y(props->nrows) + S(22);
    if (props->has_attrs)
    {
        MoveWindow(p_ro, x + S(96), y, S(120), S(22), TRUE);
        MoveWindow(p_hidden, x + S(96), y + S(26), S(120), S(22), TRUE);
        MoveWindow(p_archive, x + S(96), y + S(52), S(120), S(22), TRUE);
    }
    MoveWindow(p_apply, rc.right - S(20) - bw, rc.bottom - S(18) - bh, bw, bh, TRUE);
    MoveWindow(p_cancel, rc.right - S(20) - 2 * bw - gap, rc.bottom - S(18) - bh, bw, bh, TRUE);
    MoveWindow(p_ok, rc.right - S(20) - 3 * bw - 2 * gap, rc.bottom - S(18) - bh, bw, bh, TRUE);
}

static BOOL props_apply(void)
{
    const WCHAR *s;
    DWORD set = 0, clear = 0;
    struct { HWND w; DWORD bit; } boxes[] = { { p_ro, FILE_ATTRIBUTE_READONLY }, { p_hidden, FILE_ATTRIBUTE_HIDDEN },
                                             { p_archive, FILE_ATTRIBUTE_ARCHIVE } };
    int i, failed = 0;

    if (!props || !props->has_attrs) return TRUE;
    for (i = 0; i < 3; i++)
    {
        LRESULT st = SendMessageW(boxes[i].w, BM_GETCHECK, 0, 0);
        BOOL was_all = (props->attrs_and & boxes[i].bit) != 0, was_any = (props->attrs_or & boxes[i].bit) != 0;
        if (st == BST_CHECKED && !was_all) set |= boxes[i].bit;
        if (st == BST_UNCHECKED && was_any) clear |= boxes[i].bit;
    }
    if (!set && !clear) return TRUE;
    for (s = props->paths; *s; s += wcslen(s) + 1)
    {
        DWORD a = GetFileAttributesW(s);
        if (a == INVALID_FILE_ATTRIBUTES) { failed++; continue; }
        a = (a | set) & ~clear;
        a &= FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_ARCHIVE |
             FILE_ATTRIBUTE_NOT_CONTENT_INDEXED;
        if (!a) a = FILE_ATTRIBUTE_NORMAL;
        if (!SetFileAttributesW(s, a)) failed++;
    }
    props->attrs_and = (props->attrs_and | set) & ~clear;
    props->attrs_or = (props->attrs_or | set) & ~clear;
    _snwprintf(last_op, ARRAYSIZE(last_op), L"attributes|%d|0x%x|0", props->n, failed);
    if (failed) report_error(L"Couldn't change the attributes of %d item%ls.", failed, failed == 1 ? L"" : L"s");
    else set_status(L"Attributes changed.");
    schedule_reload();
    return !failed;
}

static void props_paint(struct setu_canvas *cv, const RECT *rc)
{
    RECT r;
    int i, isz = S(32);

    st_fill(cv, rc, C_PANEL);
    if (!props) return;
    if (props->icon) DrawIconEx(cv->dc, S(24), S(22), props->icon, isz, isz, 0, NULL, DI_NORMAL);
    SetRect(&r, S(24) + isz + S(14), S(18), rc->right - S(20), S(58));
    st_text(cv, props->title, &r, SF_TITLE, C_TEXT, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
    SetRect(&r, S(20), S(70), rc->right - S(20), S(70) + max(1, S(1)));
    st_fill(cv, &r, C_EDGE);
    for (i = 0; i < props->nrows; i++)
    {
        int y = props_row_y(i);
        SetRect(&r, S(24), y, S(24) + S(92), y + S(26));
        st_text(cv, props->rows[i][0], &r, SF_BODY, C_MUTED, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
        SetRect(&r, S(24) + S(96), y, rc->right - S(20), y + S(26));
        st_text(cv, props->rows[i][1], &r, SF_BODY, C_TEXT, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_PATH_ELLIPSIS);
    }
    if (props->has_attrs)
    {
        int y = props_row_y(props->nrows) + S(8);
        SetRect(&r, S(20), y, rc->right - S(20), y + max(1, S(1)));
        st_fill(cv, &r, C_EDGE);
        SetRect(&r, S(24), y + S(14), S(24) + S(92), y + S(14) + S(22));
        st_text(cv, L"Attributes", &r, SF_BODY, C_MUTED, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
    }
    /* the button area at the bottom, on window-bg */
    SetRect(&r, 0, rc->bottom - S(66), rc->right, rc->bottom);
    st_fill(cv, &r, C_BAR);
    r.bottom = r.top + max(1, S(1));
    st_fill(cv, &r, C_EDGE);
}

struct button_ctx { const DRAWITEMSTRUCT *di; BOOL primary; };

static void paint_dialog_button(struct setu_canvas *cv, const RECT *rc, void *ctx)
{
    const struct button_ctx *b = ctx;
    WCHAR text[64];
    RECT r = *rc;
    BOOL pressed = (b->di->itemState & ODS_SELECTED) != 0, focus = (b->di->itemState & ODS_FOCUS) != 0;
    int rad = S(ST_RADIUS_CONTROL);

    st_fill(cv, &r, C_BAR);
    if (b->primary) st_round(cv, &r, rad, pressed ? ST->accent_pressed : ST->accent);
    else
    {
        st_round(cv, &r, rad, pressed ? ST->surface_hover : ST->surface);
        st_frame(cv, &r, rad, max(1, S(1)), ST->border);
    }
    if (focus)
    {
        RECT f = r;
        if (b->primary) { InflateRect(&f, -S(2), -S(2)); st_frame(cv, &f, max(0, rad - S(2)), max(1, S(1)), ST->accent_text); }
        else st_focus_ring(cv, &r, rad, dpi);
    }
    GetWindowTextW(b->di->hwndItem, text, ARRAYSIZE(text));
    st_text(cv, text, &r, SF_BODY, b->primary ? ST->accent_text : ST->text, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
}

static void draw_button(const DRAWITEMSTRUCT *di, BOOL primary)
{
    struct button_ctx ctx = { di, primary };
    paint_into(di->hDC, &di->rcItem, paint_dialog_button, &ctx);
}

static LRESULT CALLBACK props_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_PAINT:
        st_paint_window(hwnd, props_paint);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
        SetTextColor((HDC)wp, C_TEXT);
        SetBkColor((HDC)wp, C_PANEL);
        return (LRESULT)panel_brush;
    case WM_DRAWITEM:
        draw_button((const DRAWITEMSTRUCT *)lp, wp == ID_P_OK);
        return TRUE;
    case WM_APP_SIZE:
        props_size_text();
        InvalidateRect(hwnd, NULL, FALSE);
        write_state();
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wp))
        {
        case ID_P_OK: case IDOK:
            if (props_apply()) props_close();
            return 0;
        case ID_P_CANCEL: case IDCANCEL:
            props_close();
            return 0;
        case ID_P_APPLY:
            props_apply();
            write_state();
            return 0;
        }
        break;
    case WM_CLOSE:
        props_close();
        return 0;
    case WM_DESTROY:
        if (props)
        {
            InterlockedExchange(&props->cancel, 1);
            props_release(props);
            props = NULL;
        }
        props_wnd = NULL;
        write_state();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void show_properties_of(WCHAR **paths, int n, const WCHAR *recycle_name, int item_index)
{
    WIN32_FILE_ATTRIBUTE_DATA fa;
    WCHAR buf[MAX_PATH], loc[MAX_PATH];
    SHFILEINFOW sfi;
    RECT mr;
    int px, py;
    int i, w = S(440), h;
    BOOL drive = FALSE;

    props_close();
    props = calloc(1, sizeof(*props));
    if (!props) return;
    props->refs = 1;
    props->size_row = props->contains_row = -1;
    props->n = n;
    props->paths = multi_sz(paths, n);
    props->attrs_and = ~0u;

    if (recycle_name)
    {
        const struct item *it = &items[item_index];
        lstrcpynW(props->title, recycle_name, ARRAYSIZE(props->title));
        props_add_row(L"Type", it->type);
        props_add_row(L"Was in", it->col1);
        props_add_row(L"Deleted", it->col2);
        props_add_row(L"Size", it->col3);
        props->icon = ImageList_GetIcon(il_large, it->icon, ILD_TRANSPARENT);
        n = 0;
    }
    else if (n == 1 && is_root(paths[0]))
    {
        WCHAR fs[64] = L"", label[MAX_PATH] = L"";
        ULARGE_INTEGER avail, total, free;
        drive = TRUE;
        drive_label(paths[0], props->title, ARRAYSIZE(props->title), NULL);
        props_add_row(L"Type", drive_type_name(GetDriveTypeW(paths[0])));
        if (GetVolumeInformationW(paths[0], label, ARRAYSIZE(label), NULL, NULL, NULL, fs, ARRAYSIZE(fs)))
        {
            props_add_row(L"Label", label[0] ? label : L"(none)");
            props_add_row(L"File system", fs);
        }
        if (GetDiskFreeSpaceExW(paths[0], &avail, &total, &free))
        {
            WCHAR a[64], b[64];
            format_bytes(total.QuadPart - free.QuadPart, a, ARRAYSIZE(a));
            format_count(total.QuadPart - free.QuadPart, b, ARRAYSIZE(b));
            _snwprintf(buf, ARRAYSIZE(buf), L"%ls (%ls bytes)", a, b);
            props_add_row(L"Used", buf);
            format_bytes(avail.QuadPart, a, ARRAYSIZE(a));
            format_count(avail.QuadPart, b, ARRAYSIZE(b));
            _snwprintf(buf, ARRAYSIZE(buf), L"%ls (%ls bytes)", a, b);
            props_add_row(L"Free", buf);
            format_bytes(total.QuadPart, a, ARRAYSIZE(a));
            props_add_row(L"Capacity", a);
        }
    }
    else
    {
        BOOL same_type = TRUE, any_dir = FALSE;
        WCHAR type[80] = L"";
        for (i = 0; i < n; i++)
        {
            DWORD a = GetFileAttributesW(paths[i]);
            memset(&sfi, 0, sizeof(sfi));
            SHGetFileInfoW(paths[i], 0, &sfi, sizeof(sfi), SHGFI_TYPENAME);
            if (!i) lstrcpynW(type, sfi.szTypeName, ARRAYSIZE(type));
            else if (lstrcmpiW(type, sfi.szTypeName)) same_type = FALSE;
            if (a != INVALID_FILE_ATTRIBUTES)
            {
                props->attrs_and &= a;
                props->attrs_or |= a;
                if (a & FILE_ATTRIBUTE_DIRECTORY) any_dir = TRUE;
            }
        }
        if (props->attrs_and == ~0u) props->attrs_and = 0;
        props->has_attrs = TRUE;
        if (n == 1) lstrcpynW(props->title, PathFindFileNameW(paths[0]), ARRAYSIZE(props->title));
        else _snwprintf(props->title, ARRAYSIZE(props->title), L"%d items", n);
        props_add_row(L"Type", same_type ? (type[0] ? type : L"File") : L"Several types");
        lstrcpynW(loc, paths[0], ARRAYSIZE(loc));
        PathRemoveFileSpecW(loc);
        props_add_row(L"Location", loc);
        props->size_row = props->nrows;
        props_add_row(L"Size", L"…");
        if (any_dir)
        {
            props->contains_row = props->nrows;
            props_add_row(L"Contains", L"…");
        }
        if (n == 1 && GetFileAttributesExW(paths[0], GetFileExInfoStandard, &fa))
        {
            format_time(&fa.ftCreationTime, buf, ARRAYSIZE(buf));
            if (buf[0]) props_add_row(L"Created", buf);
            format_time(&fa.ftLastWriteTime, buf, ARRAYSIZE(buf));
            if (buf[0]) props_add_row(L"Modified", buf);
            format_time(&fa.ftLastAccessTime, buf, ARRAYSIZE(buf));
            if (buf[0]) props_add_row(L"Accessed", buf);
        }
    }
    if (!props->icon && n >= 1)
    {
        memset(&sfi, 0, sizeof(sfi));
        if (SHGetFileInfoW(paths[0], 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_LARGEICON)) props->icon = sfi.hIcon;
    }

    h = props_row_y(props->nrows) + (props->has_attrs ? S(100) : S(16)) + S(70);
    GetWindowRect(main_wnd, &mr);
    {
        /* Let the window stick out past an edge of the main window: on the
         * personality a popup lying wholly inside another top-level window
         * of the same process is never painted (BUG-FOR shell-api, room
         * setu-m3 #28). Left edge if there is room, else the right edge. */
        RECT wa;
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
        px = mr.left - S(40);
        if (px < wa.left) px = mr.right - w + S(40);
        if (px + w > wa.right) px = wa.right - w;
        if (px < wa.left) px = wa.left;
        py = mr.top + S(60);
        if (py + h > wa.bottom) py = wa.bottom - h;
        if (py < wa.top) py = wa.top;
    }
    _snwprintf(buf, ARRAYSIZE(buf), L"%ls Properties", props->title);
    props_wnd = CreateWindowExW(WS_EX_DLGMODALFRAME, PROPS_CLASS, buf, WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN,
                                px, py, w, h, main_wnd, NULL, hinst, NULL);
    if (!props_wnd) { props_release(props); props = NULL; return; }
    {
        RECT wr, cr;
        GetWindowRect(props_wnd, &wr);
        GetClientRect(props_wnd, &cr);
        SetWindowPos(props_wnd, NULL, 0, 0, w + (wr.right - wr.left - cr.right), h + (wr.bottom - wr.top - cr.bottom),
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (props->has_attrs)
    {
        DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX;
        p_ro = CreateWindowExW(0, L"BUTTON", L"Read-only", style, 0, 0, 10, 10, props_wnd, (HMENU)ID_P_READONLY, hinst, NULL);
        p_hidden = CreateWindowExW(0, L"BUTTON", L"Hidden", style, 0, 0, 10, 10, props_wnd, (HMENU)ID_P_HIDDEN, hinst, NULL);
        p_archive = CreateWindowExW(0, L"BUTTON", L"Archive", style, 0, 0, 10, 10, props_wnd, (HMENU)ID_P_ARCHIVE, hinst, NULL);
        SendMessageW(p_ro, WM_SETFONT, (WPARAM)font_ui, 0);
        SendMessageW(p_hidden, WM_SETFONT, (WPARAM)font_ui, 0);
        SendMessageW(p_archive, WM_SETFONT, (WPARAM)font_ui, 0);
        SendMessageW(p_ro, BM_SETCHECK, (props->attrs_and & FILE_ATTRIBUTE_READONLY) ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(p_hidden, BM_SETCHECK, (props->attrs_and & FILE_ATTRIBUTE_HIDDEN) ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(p_archive, BM_SETCHECK, (props->attrs_and & FILE_ATTRIBUTE_ARCHIVE) ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    p_ok = CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 10, 10,
                           props_wnd, (HMENU)ID_P_OK, hinst, NULL);
    p_cancel = CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 10, 10,
                               props_wnd, (HMENU)ID_P_CANCEL, hinst, NULL);
    p_apply = CreateWindowExW(0, L"BUTTON", L"Apply", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 10, 10,
                              props_wnd, (HMENU)ID_P_APPLY, hinst, NULL);
    props_layout();
    if (!drive && n > 0 && props->size_row >= 0)
    {
        HANDLE t;
        InterlockedIncrement(&props->refs);
        if ((t = CreateThread(NULL, 0, size_thread, props, 0, NULL))) CloseHandle(t);
        else props_release(props);
    }
    props_size_text();
    ShowWindow(props_wnd, SW_SHOW);
    SetFocus(p_ok);
    write_state();
}

static void show_properties(void)
{
    int sel[1024], n, i;
    WCHAR **paths;

    if (cur_loc == LOC_RECYCLE)
    {
        int it = focused_item();
        if (it >= 0) show_properties_of(NULL, 0, items[it].name, it);
        return;
    }
    n = get_selection(sel, ARRAYSIZE(sel));
    if (!n)
    {
        /* the folder itself */
        WCHAR *p = cur_path;
        if (cur_loc == LOC_FS) show_properties_of(&p, 1, NULL, -1);
        return;
    }
    paths = malloc(n * sizeof(*paths));
    if (!paths) return;
    for (i = 0; i < n; i++) paths[i] = items[sel[i]].path;
    show_properties_of(paths, n, NULL, -1);
    free(paths);
}

/* ------------------------------------------------------------------ */
/* Context menus                                                        */

static void track_and_run(HMENU m, POINT pt)
{
    UINT id = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, main_wnd, NULL);
    if (id >= SHELLMENU_FIRST && id <= SHELLMENU_LAST) rb_menu_invoke(id);
    else if (id) SendMessageW(main_wnd, WM_COMMAND, id, 0);
}

static void show_item_menu(POINT pt)
{
    HMENU m, ow, view;
    int it = focused_item(), n = selection_count(), i;
    BOOL paste_ok = IsClipboardFormatAvailable(CF_HDROP);

    if (cur_loc == LOC_RECYCLE && n)
    {
        if (!rb_menu_build()) return;
        AppendMenuW(rb_menu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(rb_menu, MF_STRING, IDM_PROPERTIES, L"Properties");
        track_and_run(rb_menu, pt);
        rb_menu_free();
        return;
    }
    m = CreatePopupMenu();
    if (n && it >= 0)
    {
        AppendMenuW(m, MF_STRING, IDM_OPEN, L"&Open");
        SetMenuDefaultItem(m, IDM_OPEN, FALSE);
        if (cur_loc == LOC_FS && !items[it].dir)
        {
            gather_handlers(items[it].path);
            ow = CreatePopupMenu();
            for (i = 0; i < nhandlers; i++)
            {
                WCHAR label[160];
                _snwprintf(label, ARRAYSIZE(label), L"%ls%ls", handlers[i].name, handlers[i].is_default ? L"  (default)" : L"");
                AppendMenuW(ow, MF_STRING, IDM_OPENWITH_BASE + i, label);
            }
            if (nhandlers) AppendMenuW(ow, MF_SEPARATOR, 0, NULL);
            AppendMenuW(ow, MF_STRING, IDM_CHOOSE_PROGRAM, L"Choose another program…");
            AppendMenuW(m, MF_POPUP, (UINT_PTR)ow, L"Open &with");
        }
        if (items[it].dir) AppendMenuW(m, MF_STRING, IDM_NEW_WINDOW, L"Open in new &window");
        AppendMenuW(m, MF_SEPARATOR, 0, NULL);
        if (cur_loc == LOC_FS)
        {
            AppendMenuW(m, MF_STRING, IDM_CUT, L"Cu&t\tCtrl+X");
            AppendMenuW(m, MF_STRING, IDM_COPY, L"&Copy\tCtrl+C");
            if (items[it].dir) AppendMenuW(m, MF_STRING | (paste_ok ? 0 : MF_GRAYED), IDM_PASTE, L"&Paste into folder\tCtrl+V");
            AppendMenuW(m, MF_STRING, IDM_COPY_PATH, L"Copy p&ath");
            AppendMenuW(m, MF_SEPARATOR, 0, NULL);
            AppendMenuW(m, MF_STRING, IDM_RENAME, L"Rena&me\tF2");
            AppendMenuW(m, MF_STRING, IDM_DELETE, L"&Delete\tDel");
            AppendMenuW(m, MF_SEPARATOR, 0, NULL);
        }
        AppendMenuW(m, MF_STRING, IDM_PROPERTIES, L"P&roperties\tAlt+Enter");
    }
    else
    {
        view = CreatePopupMenu();
        AppendMenuW(view, MF_STRING | (view_icons ? 0 : MF_CHECKED), IDM_VIEW_DETAILS, L"&Details\tCtrl+1");
        AppendMenuW(view, MF_STRING | (view_icons ? MF_CHECKED : 0), IDM_VIEW_ICONS, L"&Icons\tCtrl+2");
        AppendMenuW(view, MF_SEPARATOR, 0, NULL);
        AppendMenuW(view, MF_STRING | (show_hidden ? MF_CHECKED : 0), IDM_SHOW_HIDDEN, L"&Hidden items\tCtrl+H");
        AppendMenuW(m, MF_POPUP, (UINT_PTR)view, L"&View");
        {
            HMENU sort = CreatePopupMenu();
            for (i = 0; i < 4; i++)
                AppendMenuW(sort, MF_STRING | (sort_col == i ? MF_CHECKED : 0), IDM_SORT_BASE + i,
                            col_names[cur_loc][i]);
            AppendMenuW(m, MF_POPUP, (UINT_PTR)sort, L"&Sort by");
        }
        AppendMenuW(m, MF_STRING, IDM_REFRESH, L"R&efresh\tF5");
        AppendMenuW(m, MF_SEPARATOR, 0, NULL);
        if (cur_loc == LOC_FS)
        {
            AppendMenuW(m, MF_STRING | (paste_ok ? 0 : MF_GRAYED), IDM_PASTE, L"&Paste\tCtrl+V");
            AppendMenuW(m, MF_STRING, IDM_NEWFOLDER, L"&New folder\tCtrl+Shift+N");
            AppendMenuW(m, MF_SEPARATOR, 0, NULL);
            AppendMenuW(m, MF_STRING, IDM_PROPERTIES, L"P&roperties");
        }
        else if (cur_loc == LOC_RECYCLE)
            AppendMenuW(m, MF_STRING | (nitems ? 0 : MF_GRAYED), IDM_EMPTY_RB, L"&Empty Recycle Bin");
    }
    track_and_run(m, pt);
    DestroyMenu(m);
}

static void show_menu_for_place(int i, POINT pt)
{
    HMENU m = CreatePopupMenu();
    BOOL bin = !wcscmp(places[i].target, L"::recycle");
    UINT id;
    AppendMenuW(m, MF_STRING, IDM_SIDE_BASE + 1, L"&Open");
    AppendMenuW(m, MF_STRING, IDM_SIDE_BASE + 2, L"Open in new &window");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    if (bin) AppendMenuW(m, MF_STRING | (recycle_count() > 0 ? 0 : MF_GRAYED), IDM_EMPTY_RB, L"&Empty Recycle Bin");
    else AppendMenuW(m, MF_STRING, IDM_SIDE_BASE + 3, L"P&roperties");
    id = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, main_wnd, NULL);
    DestroyMenu(m);
    if (id == IDM_SIDE_BASE + 1) navigate(places[i].target, NAV_NEW);
    else if (id == IDM_SIDE_BASE + 2)
    {
        WCHAR self[MAX_PATH], args[MAX_PATH + 4];
        GetModuleFileNameW(NULL, self, ARRAYSIZE(self));
        _snwprintf(args, ARRAYSIZE(args), L"\"%ls\"", places[i].target);
        ShellExecuteW(main_wnd, NULL, self, args, NULL, SW_SHOWNORMAL);
    }
    else if (id == IDM_SIDE_BASE + 3)
    {
        WCHAR *p = places[i].target;
        show_properties_of(&p, 1, NULL, -1);
    }
    else if (id == IDM_EMPTY_RB) empty_recycle_bin();
}

/* ------------------------------------------------------------------ */
/* Drag and drop within the window                                      */

static int drop_target_at(POINT screen, int *place)
{
    POINT p = screen;
    LVHITTESTINFO ht;
    HWND under = WindowFromPoint(screen);

    *place = -1;
    if (under == sidebar)
    {
        int i;
        ScreenToClient(sidebar, &p);
        i = place_at(p.y);
        if (i >= 0 && places[i].kind == 1 && wcscmp(places[i].target, L"::drives")) *place = i;
        return -1;
    }
    if (under != lv) return -1;
    ScreenToClient(lv, &p);
    memset(&ht, 0, sizeof(ht));
    ht.pt = p;
    if (ListView_HitTest(lv, &ht) >= 0 && (ht.flags & LVHT_ONITEM))
    {
        int it = item_of(ht.iItem);
        if (it >= 0 && items[it].dir && cur_loc != LOC_RECYCLE &&
            !(ListView_GetItemState(lv, ht.iItem, LVIS_SELECTED) & LVIS_SELECTED)) return it;
    }
    return -1;
}

static void set_drop_highlight(int it, int place)
{
    if (drag_target != it)
    {
        if (drag_target >= 0) ListView_SetItemState(lv, lv_index_of(drag_target), 0, LVIS_DROPHILITED);
        if (it >= 0) ListView_SetItemState(lv, lv_index_of(it), LVIS_DROPHILITED, LVIS_DROPHILITED);
        drag_target = it;
    }
    if (drag_place != place)
    {
        drag_place = place;
        InvalidateRect(sidebar, NULL, FALSE);
        UpdateWindow(sidebar);
    }
}

static void begin_drag(int lv_index, POINT pt)
{
    POINT origin;
    HIMAGELIST img;
    if (cur_loc != LOC_FS || !selection_count()) return;
    dragging = TRUE;
    drag_target = drag_place = -1;
    drag_image = FALSE;
    img = ListView_CreateDragImage(lv, lv_index, &origin);
    if (img)
    {
        POINT scr = pt;
        ClientToScreen(lv, &scr);
        ImageList_BeginDrag(img, 0, pt.x - origin.x, pt.y - origin.y);
        ImageList_DragEnter(NULL, scr.x, scr.y);
        ImageList_Destroy(img); /* BeginDrag keeps its own copy */
        drag_image = TRUE;
    }
    SetCapture(main_wnd);
    set_status(L"Drop on a folder to move (Ctrl: copy). Esc cancels.");
}

static void end_drag(BOOL drop)
{
    int target = drag_target, place = drag_place;
    BOOL copy = GetKeyState(VK_CONTROL) < 0, force_move = GetKeyState(VK_SHIFT) < 0;
    WCHAR dest[MAX_PATH] = L"";
    int n;
    WCHAR *m;

    if (!dragging) return;
    dragging = FALSE;
    if (drag_image)
    {
        ImageList_DragLeave(NULL);
        ImageList_EndDrag();
    }
    ReleaseCapture();
    set_drop_highlight(-1, -1);
    if (!drop) { set_status(L"Drag cancelled."); return; }
    if (target >= 0) lstrcpyW(dest, items[target].path);
    else if (place >= 0) lstrcpyW(dest, places[place].target);
    if (!dest[0]) { set_status(L""); return; }
    if (!(m = selected_paths(&n))) return;
    if (!wcscmp(dest, L"::recycle"))
        file_op(FO_DELETE, m, NULL, FOF_ALLOWUNDO, L"recycle", L"Moved to the Recycle Bin:", L"Deleting");
    else
    {
        /* same drive: move by default; another drive: copy */
        BOOL same_drive = towupper(m[0]) == towupper(dest[0]) && m[1] == L':' && dest[1] == L':';
        BOOL move = force_move || (!copy && same_drive);
        transfer(m, dest, move);
    }
    free(m);
}

/* ------------------------------------------------------------------ */
/* Layout and painting of the main window                               */

/* The command bar (row 0) and the navigation row with the address bar
 * (row 1), both on window-bg; then places | content; the status bar. */
#define ROW_H S(44)
static int toolbar_h(void) { return 2 * ROW_H + S(4); }
static int status_h(void) { return S(28); }
static int sidebar_w(void) { return S(212); }
static int header_h(void) { return view_icons ? 0 : S(32); }

enum { TB_BACK, TB_FORWARD, TB_UP, TB_REFRESH, TB_NEW, TB_DETAILS, TB_ICONS,
       TB_CUT, TB_COPY, TB_PASTE, TB_RENAME, TB_DELETE, TB_PROPS };

static void update_tips(void);

static void update_toolbar(void)
{
    int i, nsel = lv ? selection_count() : 0;
    BOOL fs = cur_loc == LOC_FS;
    for (i = 0; i < NUM_TBTN; i++)
    {
        struct tbtn *b = &tbtns[i];
        switch (b->id)
        {
        case IDM_BACK: b->enabled = nback > 0; break;
        case IDM_FORWARD: b->enabled = nfwd > 0; break;
        case IDM_UP: b->enabled = can_go_up(); break;
        case IDM_NEWFOLDER:
            b->icon = cur_loc == LOC_RECYCLE ? TI_CMD_EMPTY_RECYCLE_BIN : TI_CMD_NEW_FOLDER;
            b->name = cur_loc == LOC_RECYCLE ? L"empty" : L"newfolder";
            b->label = cur_loc == LOC_RECYCLE ? L"Empty Recycle Bin" : L"New folder";
            b->enabled = fs || (cur_loc == LOC_RECYCLE && nitems > 0);
            break;
        case IDM_VIEW_DETAILS: b->toggled = !view_icons; b->enabled = TRUE; break;
        case IDM_VIEW_ICONS: b->toggled = view_icons; b->enabled = TRUE; break;
        case IDM_CUT: case IDM_COPY: case IDM_DELETE: b->enabled = fs && nsel > 0; break;
        case IDM_RENAME: b->enabled = fs && nsel == 1; break;
        case IDM_PASTE: b->enabled = fs && IsClipboardFormatAvailable(CF_HDROP); break;
        case IDM_PROPERTIES: b->enabled = fs || nsel > 0; break;
        default: b->enabled = TRUE; break;
        }
    }
    if (main_wnd)
    {
        RECT r = { 0, 0, 30000, toolbar_h() };
        InvalidateRect(main_wnd, &r, FALSE);
    }
}

static void init_toolbar(void)
{
    static const struct { int id, icon; const WCHAR *name, *label; } defs[NUM_TBTN] =
    {
        { IDM_BACK, TI_NAV_BACK, L"back", L"Back (Alt+Left)" },
        { IDM_FORWARD, TI_NAV_FORWARD, L"forward", L"Forward (Alt+Right)" },
        { IDM_UP, TI_NAV_UP, L"up", L"Up (Alt+Up)" },
        { IDM_REFRESH, TI_REFRESH, L"refresh", L"Refresh (F5)" },
        { IDM_NEWFOLDER, TI_CMD_NEW_FOLDER, L"newfolder", L"New folder" },
        { IDM_VIEW_DETAILS, TI_VIEW_DETAILS, L"details", L"Details (Ctrl+1)" },
        { IDM_VIEW_ICONS, TI_VIEW_ICONS, L"icons", L"Icons (Ctrl+2)" },
        { IDM_CUT, TI_CMD_CUT, L"cut", L"Cut (Ctrl+X)" },
        { IDM_COPY, TI_CMD_COPY, L"copy", L"Copy (Ctrl+C)" },
        { IDM_PASTE, TI_CMD_PASTE, L"paste", L"Paste (Ctrl+V)" },
        { IDM_RENAME, TI_CMD_RENAME, L"rename", L"Rename (F2)" },
        { IDM_DELETE, TI_CMD_DELETE, L"delete", L"Delete (Del)" },
        { IDM_PROPERTIES, TI_CMD_PROPERTIES, L"properties", L"Properties (Alt+Enter)" },
    };
    int i;
    for (i = 0; i < NUM_TBTN; i++)
    {
        tbtns[i].id = defs[i].id;
        tbtns[i].icon = defs[i].icon;
        tbtns[i].name = defs[i].name;
        tbtns[i].label = defs[i].label;
        tbtns[i].wide = defs[i].id == IDM_NEWFOLDER;
    }
}

static int text_width(HFONT f, const WCHAR *s)
{
    HDC dc = GetDC(main_wnd);
    HGDIOBJ of = SelectObject(dc, f);
    SIZE sz = { 0, 0 };
    GetTextExtentPoint32W(dc, s, lstrlenW(s), &sz);
    SelectObject(dc, of);
    ReleaseDC(main_wnd, dc);
    return sz.cx;
}

static void layout(void)
{
    RECT rc;
    int tb = toolbar_h(), st = status_h(), sw = sidebar_w(), hh = header_h();
    int bs = S(32), gap = S(4), y0 = (ROW_H - bs) / 2 + S(2), y1 = ROW_H + (ROW_H - bs) / 2, x, i, lw, rest;
    static const int cmds[] = { TB_CUT, TB_COPY, TB_PASTE, TB_RENAME, TB_DELETE };

    if (!main_wnd) return;
    GetClientRect(main_wnd, &rc);

    /* row 0: New folder (with its label), the edit commands, Properties;
     * the view switch on the right */
    x = S(12);
    {
        int w = S(12) + S(16) + S(8) + text_width(font_ui, cur_loc == LOC_RECYCLE ? L"Empty Recycle Bin" : L"New folder") + S(14);
        SetRect(&tbtns[TB_NEW].rc, x, y0, x + w, y0 + bs);
        x += w + S(17); /* room for a separator */
    }
    for (i = 0; i < (int)ARRAYSIZE(cmds); i++, x += bs + gap) SetRect(&tbtns[cmds[i]].rc, x, y0, x + bs, y0 + bs);
    x += S(13);
    SetRect(&tbtns[TB_PROPS].rc, x, y0, x + bs, y0 + bs);
    SetRect(&tbtns[TB_ICONS].rc, rc.right - S(12) - bs, y0, rc.right - S(12), y0 + bs);
    SetRect(&tbtns[TB_DETAILS].rc, rc.right - S(12) - 2 * bs, y0, rc.right - S(12) - bs, y0 + bs);

    /* row 1: back, forward, up, refresh, and the address bar */
    x = S(12);
    for (i = TB_BACK; i <= TB_REFRESH; i++, x += bs + gap) SetRect(&tbtns[i].rc, x, y1, x + bs, y1 + bs);
    SetRect(&addr_field_rc, x + S(8), y1, max(x + S(60), (int)rc.right - S(12)), y1 + bs);
    {
        TEXTMETRICW tm;
        HDC dc = GetDC(main_wnd);
        HGDIOBJ of = SelectObject(dc, font_ui);
        int eh;
        GetTextMetricsW(dc, &tm);
        SelectObject(dc, of);
        ReleaseDC(main_wnd, dc);
        eh = tm.tmHeight + S(2);
        MoveWindow(addr, addr_field_rc.left + S(12), (addr_field_rc.top + addr_field_rc.bottom - eh) / 2,
                   addr_field_rc.right - addr_field_rc.left - S(24), eh, TRUE);
    }
    MoveWindow(sidebar, 0, tb, sw, rc.bottom - tb - st, TRUE);
    SetRect(&header_rc, sw, tb, rc.right, tb + hh);
    SetRect(&status_rc, 0, rc.bottom - st, rc.right, rc.bottom);
    MoveWindow(lv, sw, tb + hh, rc.right - sw, rc.bottom - tb - hh - st, TRUE);

    /* columns: Name takes what the others leave, so there's no sideways scrolling */
    lw = rc.right - sw - GetSystemMetrics(SM_CXVSCROLL) - S(2);
    rest = 0;
    for (i = 1; i < 4; i++) { col_w[i] = S(col_base_w[cur_loc][i]); rest += col_w[i]; }
    if (lw - rest < S(180))
    {
        /* shrink the others proportionally on narrow windows */
        int avail = max(lw - S(180), S(150));
        for (i = 1; i < 4; i++) col_w[i] = MulDiv(col_w[i], avail, rest);
        rest = avail;
    }
    col_w[0] = max(S(120), lw - rest);
    for (i = 0; i < 4; i++)
    {
        LVCOLUMNW c = { LVCF_WIDTH | LVCF_FMT };
        c.cx = col_w[i];
        c.fmt = col_right[cur_loc][i] ? LVCFMT_RIGHT : LVCFMT_LEFT;
        if (i == 0) c.fmt = LVCFMT_LEFT;
        ListView_SetColumn(lv, i, &c);
    }
    update_tips();
    InvalidateRect(main_wnd, NULL, FALSE);
}

/* --- tooltips for the icon buttons --- */

static void update_tips(void)
{
    int i;
    if (!tips) return;
    for (i = 0; i < NUM_TBTN; i++)
    {
        TTTOOLINFOW ti;
        memset(&ti, 0, sizeof(ti));
        ti.cbSize = sizeof(ti);
        ti.hwnd = main_wnd;
        ti.uId = i + 1;
        ti.rect = tbtns[i].rc;
        ti.uFlags = TTF_SUBCLASS;
        ti.lpszText = LPSTR_TEXTCALLBACKW;
        if (!SendMessageW(tips, TTM_NEWTOOLRECTW, 0, (LPARAM)&ti)) SendMessageW(tips, TTM_ADDTOOLW, 0, (LPARAM)&ti);
    }
}

/* --- breadcrumbs --- */

static struct crumb crumbs[MAX_CRUMBS];
static int ncrumbs, crumb_first, crumb_hot = -1;

static void add_crumb(const WCHAR *label, const WCHAR *target)
{
    if (ncrumbs >= MAX_CRUMBS)
    {
        memmove(crumbs + 1, crumbs + 2, (MAX_CRUMBS - 2) * sizeof(*crumbs));
        ncrumbs--;
    }
    lstrcpynW(crumbs[ncrumbs].label, label, MAX_PATH);
    lstrcpynW(crumbs[ncrumbs].target, target, MAX_PATH);
    SetRectEmpty(&crumbs[ncrumbs].rc);
    ncrumbs++;
}

/* Drives > C: > users > setu, Recycle Bin, or Drives; laid out in the
 * address field from the right, dropping the first ones that don't fit. */
static void build_crumbs(struct setu_canvas *cv, int *icon)
{
    int i, x, avail, total, chev = S(20), pad = S(8);

    ncrumbs = 0;
    *icon = TI_DRIVE_FIXED;
    if (cur_loc == LOC_RECYCLE)
    {
        *icon = recycle_count() > 0 ? TI_RECYCLE_BIN_FULL : TI_RECYCLE_BIN_EMPTY;
        add_crumb(L"Recycle Bin", L"::recycle");
    }
    else
    {
        add_crumb(L"Drives", L"::drives");
        if (cur_loc == LOC_FS && cur_path[0])
        {
            WCHAR acc[MAX_PATH], part[MAX_PATH];
            const WCHAR *p = cur_path, *e;
            acc[0] = 0;
            while (*p)
            {
                size_t n;
                e = wcschr(p, L'\\');
                n = e ? (size_t)(e - p) : wcslen(p);
                if (n && n < MAX_PATH - wcslen(acc) - 2)
                {
                    memcpy(part, p, n * sizeof(WCHAR));
                    part[n] = 0;
                    wcscat(acc, part);
                    wcscat(acc, L"\\");
                    add_crumb(part, acc);
                }
                if (!e) break;
                p = e + 1;
            }
        }
    }
    avail = addr_field_rc.right - addr_field_rc.left - S(36) - S(8);
    for (i = 0, total = 0; i < ncrumbs; i++)
        total += st_text_width(cv, crumbs[i].label, i == ncrumbs - 1 ? SF_CAPTION : SF_BODY) + 2 * pad + (i ? chev : 0);
    crumb_first = 0;
    while (total > avail && crumb_first < ncrumbs - 1)
    {
        total -= st_text_width(cv, crumbs[crumb_first].label, SF_BODY) + 2 * pad + chev;
        crumb_first++;
    }
    x = addr_field_rc.left + S(36);
    for (i = 0; i < ncrumbs; i++)
    {
        int w;
        if (i < crumb_first) { SetRectEmpty(&crumbs[i].rc); continue; }
        if (i > crumb_first) x += chev;
        w = st_text_width(cv, crumbs[i].label, i == ncrumbs - 1 ? SF_CAPTION : SF_BODY) + 2 * pad;
        w = min(w, (int)addr_field_rc.right - S(4) - x);
        SetRect(&crumbs[i].rc, x, addr_field_rc.top + S(4), x + max(0, w), addr_field_rc.bottom - S(4));
        x += w;
    }
}

static int crumb_hit(POINT pt)
{
    int i;
    if (GetFocus() == addr) return -1;
    for (i = crumb_first; i < ncrumbs; i++)
        if (PtInRect(&crumbs[i].rc, pt)) return i;
    return -1;
}

static void paint_button(struct setu_canvas *cv, int i)
{
    struct tbtn *b = &tbtns[i];
    COLORREF c = b->enabled ? ST->text : ST->text_disabled;
    int isz = S(16), rad = S(ST_RADIUS_CONTROL);

    if (b->toggled) st_round(cv, &b->rc, rad, st_mix(C_BAR, ST->text, 20));
    else if (i == tb_hot && b->enabled) st_round(cv, &b->rc, rad, st_mix(C_BAR, ST->text, 12));
    if (b->toggled) c = ST->accent;
    if (b->wide)
    {
        RECT t = b->rc;
        st_icon(cv, b->icon, b->rc.left + S(12), (b->rc.top + b->rc.bottom - isz) / 2, isz, b->enabled ? ST->accent : c);
        t.left += S(12) + isz + S(8);
        st_text(cv, b->label, &t, SF_BODY, c, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
    }
    else st_icon_in(cv, b->icon, &b->rc, isz, c);
}

static void paint_main(struct setu_canvas *cv, const RECT *rc)
{
    RECT r;
    int i, rad = S(ST_RADIUS_CONTROL), line = max(1, S(1));

    st_fill(cv, rc, C_PANEL);

    /* command bar and navigation row */
    SetRect(&r, 0, 0, rc->right, toolbar_h());
    st_fill(cv, &r, C_BAR);
    SetRect(&r, 0, toolbar_h() - line, rc->right, toolbar_h());
    st_fill(cv, &r, C_EDGE);
    for (i = 0; i < NUM_TBTN; i++) paint_button(cv, i);
    /* separators around the edit commands */
    SetRect(&r, tbtns[TB_NEW].rc.right + S(8), tbtns[TB_NEW].rc.top + S(6), tbtns[TB_NEW].rc.right + S(8) + line,
            tbtns[TB_NEW].rc.bottom - S(6));
    st_fill(cv, &r, ST->border);
    OffsetRect(&r, tbtns[TB_PROPS].rc.left - S(8) - r.left - S(1), 0);
    st_fill(cv, &r, ST->border);

    /* address bar: breadcrumbs, or the edit box while it has the focus */
    st_round(cv, &addr_field_rc, rad, ST->surface);
    if (GetFocus() == addr)
    {
        RECT u = { addr_field_rc.left, addr_field_rc.bottom - S(2), addr_field_rc.right, addr_field_rc.bottom };
        st_frame(cv, &addr_field_rc, rad, line, ST->border);
        st_clip(cv, &u);
        st_round(cv, &addr_field_rc, rad, ST->accent);
        st_clip(cv, NULL);
    }
    else
    {
        int icon;
        st_frame(cv, &addr_field_rc, rad, line, ST->border);
        build_crumbs(cv, &icon);
        st_icon(cv, icon, addr_field_rc.left + S(12), (addr_field_rc.top + addr_field_rc.bottom - S(16)) / 2, S(16),
                ST->text_secondary);
        st_clip(cv, &addr_field_rc);
        for (i = crumb_first; i < ncrumbs; i++)
        {
            RECT t = crumbs[i].rc;
            BOOL last = i == ncrumbs - 1;
            if (i == crumb_hot) st_round(cv, &t, rad, ST->surface_hover);
            if (i > crumb_first)
                st_icon(cv, TI_CHEVRON_RIGHT, t.left - S(18), (t.top + t.bottom - S(14)) / 2, S(14), ST->text_secondary);
            st_text(cv, crumbs[i].label, &t, last ? SF_CAPTION : SF_BODY, last ? ST->text : ST->text_secondary,
                    DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_END_ELLIPSIS);
        }
        st_clip(cv, NULL);
    }

    /* column header strip (details view) */
    if (!view_icons)
    {
        int x = header_rc.left;
        st_fill(cv, &header_rc, C_PANEL);
        SetRect(&r, header_rc.left, header_rc.bottom - line, header_rc.right, header_rc.bottom);
        st_fill(cv, &r, C_EDGE);
        for (i = 0; i < 4; i++)
        {
            BOOL right = col_right[cur_loc][i];
            /* the same insets as the rows */
            RECT cr = { x + (i ? S(8) : S(12) + S(16) + S(10) + S(4)), header_rc.top, x + col_w[i] - S(8), header_rc.bottom };
            const WCHAR *t = col_names[cur_loc][i];
            if (i > 0)
            {
                RECT sep = { x, header_rc.top + S(8), x + line, header_rc.bottom - S(8) };
                st_fill(cv, &sep, C_EDGE);
            }
            st_text(cv, t, &cr, SF_BODY, i == sort_col ? ST->text : C_MUTED,
                    DT_SINGLELINE | DT_VCENTER | (right ? DT_RIGHT : DT_LEFT) | DT_END_ELLIPSIS);
            if (i == sort_col)
            {
                /* a chevron after the title shows the direction */
                int tw = st_text_width(cv, t, SF_BODY), isz = S(12);
                int ax = right ? cr.right - tw - S(4) - isz : cr.left + tw + S(4);
                st_icon(cv, sort_desc ? TI_CHEVRON_DOWN : TI_CHEVRON_UP, ax,
                        (header_rc.top + header_rc.bottom - isz) / 2, isz, C_MUTED);
            }
            x += col_w[i];
        }
    }

    /* status bar */
    st_fill(cv, &status_rc, C_BAR);
    SetRect(&r, 0, status_rc.top, rc->right, status_rc.top + line);
    st_fill(cv, &r, C_EDGE);
    {
        WCHAR left[128], sz[32];
        int nsel = lv ? selection_count() : 0;
        RECT lr = { S(14), status_rc.top, S(14) + S(260), status_rc.bottom };
        RECT rr = { lr.right + S(10), status_rc.top, rc->right - S(14), status_rc.bottom };
        if (nsel)
        {
            int sel[4096], n = get_selection(sel, ARRAYSIZE(sel)), k;
            ULONGLONG total = 0;
            BOOL files_only = cur_loc == LOC_FS;
            for (k = 0; k < n; k++) { if (items[sel[k]].dir) files_only = FALSE; total += items[sel[k]].size; }
            format_bytes(total, sz, ARRAYSIZE(sz));
            _snwprintf(left, ARRAYSIZE(left), L"%d of %d selected%ls%ls", nsel, nitems,
                       files_only ? L"  ·  " : L"", files_only ? sz : L"");
        }
        else _snwprintf(left, ARRAYSIZE(left), L"%d item%ls", nitems, nitems == 1 ? L"" : L"s");
        left[ARRAYSIZE(left) - 1] = 0;
        st_text(cv, left, &lr, SF_BODY, C_MUTED, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
        st_text(cv, status_text, &rr, SF_BODY, status_error ? C_ERROR : C_MUTED,
                DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_END_ELLIPSIS);
    }
}

/* --- the list's rows (details view, owner-drawn, 32 px) --- */

struct row_ctx { const DRAWITEMSTRUCT *di; };

static void paint_row(struct setu_canvas *cv, const RECT *rc, void *ctx)
{
    const DRAWITEMSTRUCT *di = ((struct row_ctx *)ctx)->di;
    int idx = (int)di->itemID, it = (int)di->itemData, i, x;
    UINT st = ListView_GetItemState(lv, idx, LVIS_SELECTED | LVIS_CUT | LVIS_DROPHILITED | LVIS_FOCUSED);
    BOOL focused = GetFocus() == lv, sel = (st & (LVIS_SELECTED | LVIS_DROPHILITED)) != 0;
    int rad = S(ST_RADIUS_CONTROL), isz = GetSystemMetrics(SM_CXSMICON);
    COLORREF text = ST->text, sub = ST->text_secondary;
    RECT r = *rc, sr;
    WCHAR cell[MAX_PATH];

    st_fill(cv, rc, C_PANEL);
    if (it < 0 || it >= nitems) return;
    SetRect(&sr, S(4), S(1), rc->right - S(4), rc->bottom - S(1));
    if (sel)
    {
        BOOL strong = focused || (st & LVIS_DROPHILITED);
        st_round(cv, &sr, rad, strong ? st_selection() : st_selection_inactive());
        if (strong) st_frame(cv, &sr, rad, max(1, S(1)), ST->selection_border);
    }
    else if (focused && (st & LVIS_FOCUSED)) st_frame(cv, &sr, rad, max(1, S(1)), ST->border);
    if (items[it].attrs & FILE_ATTRIBUTE_HIDDEN) text = ST->text_secondary;
    if (st & LVIS_CUT) text = sub = ST->text_disabled;

    ImageList_DrawEx(il_small, items[it].icon, cv->dc, S(12), (rc->bottom - isz) / 2, 0, 0, CLR_NONE, CLR_NONE,
                     ILD_TRANSPARENT | ((st & LVIS_CUT) ? ILD_BLEND50 : 0));
    for (i = 0, x = 0; i < 4; x += col_w[i], i++)
    {
        RECT cr = { x + S(8), r.top, x + col_w[i] - S(8), r.bottom };
        if (i == 0) cr.left = S(12) + isz + S(10);
        cell_text(&items[it], i, cell, ARRAYSIZE(cell));
        st_text(cv, cell, &cr, SF_BODY, i ? sub : text,
                DT_SINGLELINE | DT_VCENTER | (i && col_right[cur_loc][i] ? DT_RIGHT : DT_LEFT) | DT_END_ELLIPSIS);
    }
}

static int toolbar_hit(POINT pt)
{
    int i;
    for (i = 0; i < NUM_TBTN; i++)
        if (PtInRect(&tbtns[i].rc, pt)) return i;
    return -1;
}

static int header_hit(POINT pt)
{
    int i, x = header_rc.left;
    if (view_icons || !PtInRect(&header_rc, pt)) return -1;
    for (i = 0; i < 4; i++)
    {
        if (pt.x >= x && pt.x < x + col_w[i]) return i;
        x += col_w[i];
    }
    return -1;
}

static void set_sort(int col)
{
    if (col < 0 || col > 3) return;
    if (col == sort_col) sort_desc = !sort_desc;
    else { sort_col = col; sort_desc = FALSE; }
    ListView_SortItems(lv, compare_items, 0);
    {
        int f = ListView_GetNextItem(lv, -1, LVNI_FOCUSED);
        if (f >= 0) ListView_EnsureVisible(lv, f, FALSE);
    }
    InvalidateRect(main_wnd, &header_rc, FALSE);
    write_state();
}

static void set_view(BOOL icons)
{
    LONG_PTR style = GetWindowLongPtrW(lv, GWL_STYLE);
    view_icons = icons;
    style = (style & ~LVS_TYPEMASK) | (icons ? LVS_ICON : LVS_REPORT);
    SetWindowLongPtrW(lv, GWL_STYLE, style);
    if (icons) ListView_Arrange(lv, LVA_DEFAULT);
    layout();
    update_toolbar();
    write_state();
}

/* ------------------------------------------------------------------ */
/* State file (for tests)                                               */

static const WCHAR *focus_name(void)
{
    HWND f = GetFocus();
    if (!f) return L"none";
    if (f == lv) return L"list";
    if (f == addr) return L"address";
    if (f == sidebar) return L"sidebar";
    if (lv && f == ListView_GetEditControl(lv)) return L"rename";
    if (props_wnd && (f == props_wnd || IsChild(props_wnd, f))) return L"properties";
    return L"other";
}

static void write_state(void)
{
    WCHAR tmp[MAX_PATH + 8];
    static WCHAR buf[262144];
    static char utf8[786432];
    int len = 0, i, nsel, sel[4096];
    WCHAR cell[MAX_PATH];
    HANDLE h;
    DWORD done;
    RECT wr;

    if (!state_path[0] || !main_wnd) return;

#define EMIT(...) do { int room_ = (int)ARRAYSIZE(buf) - len - 1; if (room_ > 0) { int n_ = _snwprintf(buf + len, room_, __VA_ARGS__); len += n_ >= 0 ? n_ : room_; } } while (0)
    GetWindowRect(main_wnd, &wr);
    EMIT(L"seq=%ld\r\n", cmd_seq);
    EMIT(L"location=%ls|%ls\r\n", cur_loc == LOC_FS ? L"fs" : cur_loc == LOC_DRIVES ? L"drives" : L"recycle", cur_path);
    EMIT(L"address=");
    { WCHAR a[MAX_PATH]; GetWindowTextW(addr, a, ARRAYSIZE(a)); EMIT(L"%ls\r\n", a); }
    { WCHAR t[MAX_PATH]; GetWindowTextW(main_wnd, t, ARRAYSIZE(t)); EMIT(L"title=%ls\r\n", t); }
    EMIT(L"window=%ld,%ld,%ld,%ld\r\n", wr.left, wr.top, wr.right, wr.bottom);
    EMIT(L"view=%ls\r\n", view_icons ? L"icons" : L"details");
    EMIT(L"sort=%ls,%ls\r\n", col_names[cur_loc][sort_col], sort_desc ? L"desc" : L"asc");
    EMIT(L"history=%d,%d\r\n", nback, nfwd);
    EMIT(L"focus=%ls\r\n", focus_name());
    EMIT(L"status=%ls%ls\r\n", status_error ? L"error: " : L"", status_text);
    EMIT(L"last_op=%ls\r\n", last_op);
    EMIT(L"recycle_count=%d\r\n", recycle_count());
    EMIT(L"theme=%ls\r\n", setu_theme.id);
    EMIT(L"theme_reloads=%d\r\n", theme_reloads);
    EMIT(L"clipboard=%ls|%d\r\n", cut_paths ? L"cut" : IsClipboardFormatAvailable(CF_HDROP) ? L"files" : L"none",
         cut_paths ? multi_count(cut_paths) : 0);
    EMIT(L"count=%d\r\n", nitems);
    for (i = 0; i < ListView_GetItemCount(lv) && i < 400; i++)
    {
        int it = item_of(i), c;
        RECT ir;
        POINT ctr = { -1, -1 };
        if (it < 0) continue;
        EMIT(L"item=%ls|%ls", items[it].name, items[it].dir ? L"dir" : L"file");
        for (c = 1; c < 4; c++) { cell_text(&items[it], c, cell, ARRAYSIZE(cell)); EMIT(L"|%ls", cell); }
        if (cur_loc == LOC_FS)
            EMIT(L"|%ls%ls%ls", (items[it].attrs & FILE_ATTRIBUTE_READONLY) ? L"R" : L"",
                 (items[it].attrs & FILE_ATTRIBUTE_HIDDEN) ? L"H" : L"", (items[it].attrs & FILE_ATTRIBUTE_ARCHIVE) ? L"A" : L"");
        if (ListView_GetItemRect(lv, i, &ir, LVIR_ICON))
        {
            RECT cr;
            GetClientRect(lv, &cr);
            ctr.x = (ir.left + ir.right) / 2;
            ctr.y = (ir.top + ir.bottom) / 2;
            if (ctr.y >= 0 && ctr.y < cr.bottom) ClientToScreen(lv, &ctr);
            else ctr.x = ctr.y = -1;
        }
        EMIT(L"|%ld,%ld\r\n", ctr.x, ctr.y);
    }
    nsel = get_selection(sel, ARRAYSIZE(sel));
    for (i = 0; i < nsel; i++) EMIT(L"selected=%ls\r\n", items[sel[i]].name);
    for (i = 0; i < nplaces; i++)
    {
        POINT c = { sidebar_w() / 2, places[i].y + places[i].h / 2 };
        if (places[i].kind != 1) continue;
        ClientToScreen(sidebar, &c);
        EMIT(L"place=%ls|%ls|%ls|%ld,%ld\r\n", places[i].label, places[i].target, place_is_current(&places[i]) ? L"current" : L"",
             c.x, c.y);
    }
    for (i = 0; i < NUM_TBTN; i++)
    {
        POINT c = { (tbtns[i].rc.left + tbtns[i].rc.right) / 2, (tbtns[i].rc.top + tbtns[i].rc.bottom) / 2 };
        ClientToScreen(main_wnd, &c);
        EMIT(L"button=%ls|%ls|%ld,%ld\r\n", tbtns[i].name, tbtns[i].enabled ? L"enabled" : L"disabled", c.x, c.y);
    }
    if (handlers_for[0])
    {
        EMIT(L"handlers_for=%ls\r\n", PathFindFileNameW(handlers_for));
        for (i = 0; i < nhandlers; i++)
            EMIT(L"handler=%d|%ls|%ls|%ls|%ls\r\n", i, handlers[i].name, PathFindFileNameW(handlers[i].exe),
                 handlers[i].source, handlers[i].is_default ? L"default" : L"");
    }
    for (i = 0; i < rb_menu_count; i++) EMIT(L"bin_menu=%d|%ls\r\n", i, rb_menu_text[i]);
    if (props_wnd && props)
    {
        EMIT(L"properties=%ls\r\n", props->title);
        for (i = 0; i < props->nrows; i++) EMIT(L"prop=%ls|%ls\r\n", props->rows[i][0], props->rows[i][1]);
        if (props->has_attrs)
            EMIT(L"prop_attrs=readonly:%d,hidden:%d,archive:%d\r\n",
                 (int)SendMessageW(p_ro, BM_GETCHECK, 0, 0), (int)SendMessageW(p_hidden, BM_GETCHECK, 0, 0),
                 (int)SendMessageW(p_archive, BM_GETCHECK, 0, 0));
        EMIT(L"prop_done=%ld\r\n", props->shown_done);
    }
    else EMIT(L"properties=\r\n");
    EMIT(L"renaming=%d\r\n", ListView_GetEditControl(lv) != NULL);
    EMIT(L"end=1\r\n");
#undef EMIT

    buf[len] = 0;
    len = WideCharToMultiByte(CP_UTF8, 0, buf, len, utf8, sizeof(utf8), NULL, NULL);
    swprintf(tmp, ARRAYSIZE(tmp), L"%ls.tmp", state_path);
    h = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    WriteFile(h, utf8, len, &done, NULL);
    CloseHandle(h);
    MoveFileExW(tmp, state_path, MOVEFILE_REPLACE_EXISTING);
}

/* ------------------------------------------------------------------ */
/* Commands                                                             */

static void focus_address(void)
{
    ShowWindow(addr, SW_SHOW);
    SetFocus(addr);
    SendMessageW(addr, EM_SETSEL, 0, -1);
}

static void copy_path_text(void)
{
    int n;
    WCHAR *m = selected_paths(&n), *p;
    size_t len = 0;
    HGLOBAL g;
    WCHAR *t;
    if (!m) return;
    for (p = m; *p; p += wcslen(p) + 1) len += wcslen(p) + 4;
    g = GlobalAlloc(GMEM_MOVEABLE, (len + 1) * sizeof(WCHAR));
    if (!g) { free(m); return; }
    t = GlobalLock(g);
    t[0] = 0;
    for (p = m; *p; p += wcslen(p) + 1)
    {
        if (t[0]) wcscat(t, L"\r\n");
        wcscat(t, L"\"");
        wcscat(t, p);
        wcscat(t, L"\"");
    }
    GlobalUnlock(g);
    if (OpenClipboard(main_wnd))
    {
        EmptyClipboard();
        SetClipboardData(CF_UNICODETEXT, g);
        CloseClipboard();
        set_cut_paths(NULL);
        set_status(L"Copied the path%ls.", n == 1 ? L"" : L"s");
    }
    else GlobalFree(g);
    free(m);
}

static void on_command(int id)
{
    int it;
    switch (id)
    {
    case IDM_BACK: go_back(); break;
    case IDM_FORWARD: go_forward(); break;
    case IDM_UP: go_up(); break;
    case IDM_REFRESH:
        load_places();
        layout_places();
        ntype_cache = 0;
        reload();
        set_status(L"Refreshed.");
        break;
    case IDM_NEWFOLDER:
        if (cur_loc == LOC_RECYCLE) empty_recycle_bin();
        else new_folder();
        break;
    case IDM_EMPTY_RB: empty_recycle_bin(); break;
    case IDM_VIEW_DETAILS: set_view(FALSE); break;
    case IDM_VIEW_ICONS: set_view(TRUE); break;
    case IDM_SHOW_HIDDEN: show_hidden = !show_hidden; reload(); break;
    case IDM_OPEN: open_item(focused_item()); break;
    case IDM_NEW_WINDOW:
        if ((it = focused_item()) >= 0 && items[it].dir)
        {
            WCHAR self[MAX_PATH], args[MAX_PATH + 4];
            GetModuleFileNameW(NULL, self, ARRAYSIZE(self));
            _snwprintf(args, ARRAYSIZE(args), L"\"%ls\"", items[it].path);
            ShellExecuteW(main_wnd, NULL, self, args, NULL, SW_SHOWNORMAL);
        }
        break;
    case IDM_RENAME:
        if (cur_loc == LOC_FS && (it = focused_item()) >= 0) select_only(it, TRUE);
        break;
    case IDM_COPY: if (cur_loc == LOC_FS) clipboard_put(FALSE); break;
    case IDM_CUT: if (cur_loc == LOC_FS) clipboard_put(TRUE); break;
    case IDM_COPY_PATH: copy_path_text(); break;
    case IDM_PASTE:
    {
        /* into the selected folder when that's the only selection */
        int sel[2];
        if (cur_loc == LOC_FS && get_selection(sel, 2) == 1 && items[sel[0]].dir && GetFocus() == lv &&
            ListView_GetSelectedCount(lv) == 1)
        {
            BOOL move;
            WCHAR *m = clipboard_get(&move);
            if (m && m[0] && transfer(m, items[sel[0]].path, move) && move)
            {
                if (OpenClipboard(main_wnd)) { EmptyClipboard(); CloseClipboard(); }
                set_cut_paths(NULL);
            }
            free(m);
        }
        else paste();
        break;
    }
    case IDM_DELETE: delete_selection(FALSE); break;
    case IDM_DELETE_PERM: delete_selection(TRUE); break;
    case IDM_PROPERTIES: show_properties(); break;
    case IDM_SELECT_ALL: ListView_SetItemState(lv, -1, LVIS_SELECTED, LVIS_SELECTED); break;
    case IDM_ADDRESS: focus_address(); break;
    case IDM_CHOOSE_PROGRAM:
        if ((it = focused_item()) >= 0 && !items[it].dir && cur_loc == LOC_FS) choose_program(items[it].path, NULL, -1);
        break;
    default:
        if (id >= IDM_SORT_BASE && id < IDM_SORT_BASE + 4) set_sort(id - IDM_SORT_BASE);
        else if (id >= IDM_OPENWITH_BASE && id < IDM_OPENWITH_BASE + MAX_HANDLERS && (it = focused_item()) >= 0)
            run_handler(id - IDM_OPENWITH_BASE, items[it].path);
        break;
    }
    InvalidateRect(main_wnd, &status_rc, FALSE);
    write_state();
}

static const struct { const WCHAR *name; int id; } command_names[] =
{
    { L"back", IDM_BACK }, { L"forward", IDM_FORWARD }, { L"up", IDM_UP }, { L"refresh", IDM_REFRESH },
    { L"newfolder", IDM_NEWFOLDER }, { L"details", IDM_VIEW_DETAILS }, { L"icons", IDM_VIEW_ICONS },
    { L"open", IDM_OPEN }, { L"rename", IDM_RENAME }, { L"copy", IDM_COPY }, { L"cut", IDM_CUT },
    { L"paste", IDM_PASTE }, { L"delete", IDM_DELETE }, { L"delete-permanent", IDM_DELETE_PERM },
    { L"properties", IDM_PROPERTIES }, { L"select-all", IDM_SELECT_ALL }, { L"empty-recycle-bin", IDM_EMPTY_RB },
    { L"hidden", IDM_SHOW_HIDDEN }, { L"address", IDM_ADDRESS },
};

/*
 * Commands (from the command line or WM_COPYDATA, one argument each):
 *   --go PATH | --cmd NAME (see command_names) | --select NAME |
 *   --select-add NAME | --select-none | --sort COLUMN-INDEX |
 *   --rename-to NAME (finish an in-place rename with NAME) |
 *   --handlers (list "Open with" programs for the focused file) |
 *   --open-with INDEX | --choose-program EXE always|once |
 *   --bin-menu (list the Recycle Bin menu for the selection) |
 *   --bin-invoke INDEX | --props-check readonly|hidden|archive 0|1 |
 *   --props-button ok|cancel|apply | --focus | --state-file PATH |
 *   --exit-after SEC | --quit
 */
static BOOL apply_commands(int argc, WCHAR **argv)
{
    BOOL ok = TRUE;
    int i, k;

    for (i = 0; i < argc; i++)
    {
        const WCHAR *a = argv[i];
        const WCHAR *v = i + 1 < argc ? argv[i + 1] : NULL;
        const WCHAR *v2 = i + 2 < argc ? argv[i + 2] : NULL;

        if (!wcscmp(a, L"--quit")) { DestroyWindow(main_wnd); return TRUE; }
        else if (!wcscmp(a, L"--focus"))
        {
            ShowWindow(main_wnd, SW_RESTORE);
            SetForegroundWindow(main_wnd);
            SetFocus(lv);
        }
        else if (!wcscmp(a, L"--select-none")) { ListView_SetItemState(lv, -1, 0, LVIS_SELECTED); }
        else if (!wcscmp(a, L"--handlers"))
        {
            int it = focused_item();
            if (it >= 0 && !items[it].dir) { handlers_for[0] = 0; gather_handlers(items[it].path); }
            else ok = FALSE;
        }
        else if (!wcscmp(a, L"--bin-menu")) ok = rb_menu_build() && ok;
        else if (v && !wcscmp(a, L"--bin-invoke"))
        {
            k = _wtoi(v);
            if (!rb_menu_cm) rb_menu_build();
            if (k >= 0 && k < rb_menu_count) rb_menu_invoke(rb_menu_ids[k]);
            else ok = FALSE;
            i++;
        }
        else if (v && !wcscmp(a, L"--state-file"))
        {
            GetFullPathNameW(v, ARRAYSIZE(state_path), state_path, NULL);
            automation = TRUE;
            i++;
        }
        else if (v && !wcscmp(a, L"--exit-after")) { exit_after_sec = _wtoi(v); i++; }
        else if (v && !wcscmp(a, L"--go")) { navigate(v, NAV_NEW); i++; }
        else if (v && !wcscmp(a, L"--cmd"))
        {
            for (k = 0; k < (int)ARRAYSIZE(command_names); k++)
                if (!wcscmp(command_names[k].name, v)) { on_command(command_names[k].id); break; }
            if (k == (int)ARRAYSIZE(command_names)) { set_error(L"Unknown command %ls", v); ok = FALSE; }
            i++;
        }
        else if (v && (!wcscmp(a, L"--select") || !wcscmp(a, L"--select-add")))
        {
            int it = find_item_by_name(v);
            if (it < 0) { set_error(L"No item called %ls", v); ok = FALSE; }
            else if (!wcscmp(a, L"--select")) select_only(it, FALSE);
            else
            {
                int idx = lv_index_of(it);
                ListView_SetItemState(lv, idx, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            }
            i++;
        }
        else if (v && !wcscmp(a, L"--sort")) { set_sort(_wtoi(v)); i++; }
        else if (v && !wcscmp(a, L"--rename-to"))
        {
            HWND e = ListView_GetEditControl(lv);
            if (!e) { on_command(IDM_RENAME); e = ListView_GetEditControl(lv); }
            if (e)
            {
                SetWindowTextW(e, v);
                /* finish the edit the way Enter does */
                SendMessageW(e, WM_KEYDOWN, VK_RETURN, 0);
                if (ListView_GetEditControl(lv)) SetFocus(lv);
            }
            else ok = FALSE;
            i++;
        }
        else if (v && !wcscmp(a, L"--open-with"))
        {
            int it = focused_item();
            if (it >= 0) { gather_handlers(items[it].path); run_handler(_wtoi(v), items[it].path); }
            i++;
        }
        else if (v && v2 && !wcscmp(a, L"--choose-program"))
        {
            int it = focused_item();
            if (it >= 0 && !items[it].dir) choose_program(items[it].path, v, !wcscmp(v2, L"always"));
            else ok = FALSE;
            i += 2;
        }
        else if (v && v2 && !wcscmp(a, L"--props-check"))
        {
            HWND b = !wcscmp(v, L"readonly") ? p_ro : !wcscmp(v, L"hidden") ? p_hidden : !wcscmp(v, L"archive") ? p_archive : NULL;
            if (props_wnd && b) SendMessageW(b, BM_SETCHECK, _wtoi(v2) ? BST_CHECKED : BST_UNCHECKED, 0);
            else ok = FALSE;
            i += 2;
        }
        else if (v && !wcscmp(a, L"--props-button"))
        {
            int id = !wcscmp(v, L"ok") ? ID_P_OK : !wcscmp(v, L"cancel") ? ID_P_CANCEL : ID_P_APPLY;
            if (props_wnd) SendMessageW(props_wnd, WM_COMMAND, id, 0);
            else ok = FALSE;
            i++;
        }
        else if (a[0] != L'-' && main_wnd) navigate(a, NAV_NEW);
        else { set_error(L"Unknown option %ls", a); ok = FALSE; }
    }
    if (exit_after_sec > 0 && main_wnd) SetTimer(main_wnd, TIMER_EXIT, exit_after_sec * 1000, NULL);
    return ok;
}

/* Keyboard shortcuts for the whole window. Returns TRUE if handled. */
static BOOL handle_key(MSG *m)
{
    BOOL ctrl = GetKeyState(VK_CONTROL) < 0, shift = GetKeyState(VK_SHIFT) < 0;
    BOOL alt = m->message == WM_SYSKEYDOWN || GetKeyState(VK_MENU) < 0;
    HWND f = GetFocus();
    HWND edit = lv ? ListView_GetEditControl(lv) : NULL;
    UINT vk = (UINT)m->wParam;

    if (m->message != WM_KEYDOWN && m->message != WM_SYSKEYDOWN) return FALSE;
    if (!main_wnd || (m->hwnd != main_wnd && !IsChild(main_wnd, m->hwnd))) return FALSE;
    if (dragging && vk == VK_ESCAPE) { end_drag(FALSE); return TRUE; }
    if (f && f == edit) return FALSE; /* the rename box handles its own keys */

    if (vk == VK_TAB && !ctrl && !alt)
    {
        HWND order[3] = { addr, sidebar, lv };
        int i, cur = 2;
        for (i = 0; i < 3; i++) if (order[i] == f) cur = i;
        cur = (cur + (shift ? 2 : 1)) % 3;
        if (order[cur] == addr) focus_address();
        else SetFocus(order[cur]);
        InvalidateRect(main_wnd, &addr_field_rc, FALSE);
        write_state();
        return TRUE;
    }
    if (f == addr)
    {
        if (vk == VK_RETURN)
        {
            WCHAR t[MAX_PATH];
            GetWindowTextW(addr, t, ARRAYSIZE(t));
            navigate(t, NAV_NEW);
            SetFocus(lv);
            return TRUE;
        }
        if (vk == VK_ESCAPE)
        {
            SetWindowTextW(addr, loc_display());
            SetFocus(lv);
            return TRUE;
        }
        if (!alt && !(ctrl && (vk == 'L')) && vk != VK_F5 && vk != VK_F4) return FALSE;
    }

    if (alt && vk == VK_LEFT) { go_back(); return TRUE; }
    if (alt && vk == VK_RIGHT) { go_forward(); return TRUE; }
    if (alt && vk == VK_UP) { go_up(); return TRUE; }
    if (alt && vk == VK_RETURN) { on_command(IDM_PROPERTIES); return TRUE; }
    if ((alt && vk == 'D') || (ctrl && vk == 'L') || vk == VK_F4) { focus_address(); return TRUE; }
    if (alt) return FALSE;
    if (vk == VK_F5) { on_command(IDM_REFRESH); return TRUE; }
    if (ctrl && shift && vk == 'N') { on_command(IDM_NEWFOLDER); return TRUE; }
    if (ctrl && vk == '1') { on_command(IDM_VIEW_DETAILS); return TRUE; }
    if (ctrl && vk == '2') { on_command(IDM_VIEW_ICONS); return TRUE; }
    if (ctrl && vk == 'H') { on_command(IDM_SHOW_HIDDEN); return TRUE; }
    if (f != lv) return FALSE;
    if (vk == VK_BACK) { go_back(); return TRUE; }
    if (vk == VK_RETURN) { on_command(IDM_OPEN); return TRUE; }
    if (vk == VK_F2) { on_command(IDM_RENAME); return TRUE; }
    if (vk == VK_DELETE) { on_command(shift ? IDM_DELETE_PERM : IDM_DELETE); return TRUE; }
    if (ctrl && vk == 'C') { on_command(IDM_COPY); return TRUE; }
    if (ctrl && vk == 'X') { on_command(IDM_CUT); return TRUE; }
    if (ctrl && vk == 'V') { on_command(IDM_PASTE); return TRUE; }
    if (ctrl && vk == 'A') { on_command(IDM_SELECT_ALL); return TRUE; }
    return FALSE;
}

/* ------------------------------------------------------------------ */
/* Window procedures                                                    */

static LRESULT on_notify(NMHDR *nm)
{
    if (tips && nm->hwndFrom == tips && nm->code == TTN_GETDISPINFOW)
    {
        NMTTDISPINFOW *di = (NMTTDISPINFOW *)nm;
        UINT_PTR i = di->hdr.idFrom;
        if (i >= 1 && i <= NUM_TBTN) di->lpszText = (WCHAR *)tbtns[i - 1].label;
        return 0;
    }
    if (nm->hwndFrom != lv) return 0;
    switch (nm->code)
    {
    case LVN_GETDISPINFOW:
    {
        NMLVDISPINFOW *di = (NMLVDISPINFOW *)nm;
        int it = (int)di->item.lParam;
        if (it < 0 || it >= nitems) return 0;
        if (di->item.mask & LVIF_TEXT) cell_text(&items[it], di->item.iSubItem, di->item.pszText, di->item.cchTextMax);
        return 0;
    }
    case NM_CUSTOMDRAW:
    {
        NMLVCUSTOMDRAW *cd = (NMLVCUSTOMDRAW *)nm;
        switch (cd->nmcd.dwDrawStage)
        {
        case CDDS_PREPAINT:
            return CDRF_NOTIFYITEMDRAW;
        case CDDS_ITEMPREPAINT:
        {
            /* the icons view (details rows are owner-drawn): our
             * selection colour under the label instead of the system's */
            int idx = (int)cd->nmcd.dwItemSpec;
            UINT st = ListView_GetItemState(lv, idx, LVIS_SELECTED | LVIS_CUT | LVIS_DROPHILITED);
            int it = (int)cd->nmcd.lItemlParam;
            cd->clrText = C_TEXT;
            cd->clrTextBk = C_PANEL;
            if (it >= 0 && it < nitems && (items[it].attrs & FILE_ATTRIBUTE_HIDDEN)) cd->clrText = C_MUTED;
            if (st & LVIS_CUT) cd->clrText = C_DIM;
            if (st & (LVIS_SELECTED | LVIS_DROPHILITED))
            {
                cd->clrTextBk = GetFocus() == lv || (st & LVIS_DROPHILITED) ? st_selection() : st_selection_inactive();
                cd->nmcd.uItemState &= ~(CDIS_SELECTED | CDIS_FOCUS);
            }
            return CDRF_DODEFAULT;
        }
        }
        return CDRF_DODEFAULT;
    }
    case NM_DBLCLK:
    {
        NMITEMACTIVATE *ia = (NMITEMACTIVATE *)nm;
        if (ia->iItem >= 0) open_item(item_of(ia->iItem));
        return 0;
    }
    case LVN_ITEMCHANGED:
        InvalidateRect(main_wnd, &status_rc, FALSE);
        update_toolbar();
        return 0;
    case NM_SETFOCUS:
    case NM_KILLFOCUS:
        /* the selection is drawn stronger while the list has the focus */
        InvalidateRect(lv, NULL, FALSE);
        return 0;
    case LVN_BEGINLABELEDITW:
    {
        NMLVDISPINFOW *di = (NMLVDISPINFOW *)nm;
        int it = (int)di->item.lParam;
        HWND e;
        if (cur_loc != LOC_FS || it < 0) return TRUE; /* no renaming drives or binned items */
        if ((e = ListView_GetEditControl(lv)) && !items[it].dir)
        {
            /* select the name without its extension, like most file managers */
            const WCHAR *dot = wcsrchr(items[it].name, L'.');
            if (dot && dot != items[it].name) PostMessageW(e, EM_SETSEL, 0, dot - items[it].name);
        }
        write_state();
        return FALSE;
    }
    case LVN_ENDLABELEDITW:
    {
        NMLVDISPINFOW *di = (NMLVDISPINFOW *)nm;
        int it = (int)di->item.lParam;
        BOOL ok;
        if (!di->item.pszText || it < 0) { write_state(); return FALSE; }
        ok = rename_item(it, di->item.pszText);
        PostMessageW(main_wnd, WM_TIMER, TIMER_RELOAD, 0);
        return ok;
    }
    case LVN_BEGINDRAG:
    {
        NMLISTVIEW *l = (NMLISTVIEW *)nm;
        begin_drag(l->iItem, l->ptAction);
        return 0;
    }
    case LVN_KEYDOWN:
        return 0;
    }
    return 0;
}

/* The theme pack changed (setu-theme apply: WM_SETTINGCHANGE "SetuTheme"
 * or "ImmersiveColorSet", WM_THEMECHANGED): reload every colour, font,
 * metric and icon, and redraw without restarting. */
static void apply_theme(void)
{
    HBRUSH old[3] = { field_brush, panel_brush, bar_brush };
    HICON old_big = app_icon_big, old_small = app_icon_small;
    int i;

    st_theme_reload();
    theme_reloads++;
    make_fonts();
    field_brush = CreateSolidBrush(ST->surface);
    panel_brush = CreateSolidBrush(C_PANEL);
    bar_brush = CreateSolidBrush(C_BAR);
    /* the classes' background brushes */
    SetClassLongPtrW(main_wnd, GCLP_HBRBACKGROUND, (LONG_PTR)panel_brush);
    if (sidebar) SetClassLongPtrW(sidebar, GCLP_HBRBACKGROUND, (LONG_PTR)bar_brush);
    if (props_wnd) SetClassLongPtrW(props_wnd, GCLP_HBRBACKGROUND, (LONG_PTR)panel_brush);
    for (i = 0; i < 3; i++) if (old[i]) DeleteObject(old[i]);

    app_icon_big = make_app_icon(GetSystemMetrics(SM_CXICON));
    app_icon_small = make_app_icon(GetSystemMetrics(SM_CXSMICON));
    SetClassLongPtrW(main_wnd, GCLP_HICON, (LONG_PTR)app_icon_big);
    SetClassLongPtrW(main_wnd, GCLP_HICONSM, (LONG_PTR)app_icon_small);
    SendMessageW(main_wnd, WM_SETICON, ICON_BIG, (LPARAM)app_icon_big);
    SendMessageW(main_wnd, WM_SETICON, ICON_SMALL, (LPARAM)app_icon_small);
    if (old_big) DestroyIcon(old_big);
    if (old_small) DestroyIcon(old_small);

    ListView_SetBkColor(lv, C_PANEL);
    ListView_SetTextBkColor(lv, C_PANEL);
    ListView_SetTextColor(lv, C_TEXT);
    /* owner-drawn rows get their (row-height) height from WM_MEASUREITEM,
     * which list views send again when their font changes */
    SendMessageW(lv, WM_SETFONT, (WPARAM)font_ui, TRUE);
    SendMessageW(addr, WM_SETFONT, (WPARAM)font_ui, FALSE);
    if (tips) SendMessageW(tips, WM_SETFONT, (WPARAM)font_ui, FALSE);

    /* the list's icons: the new pack's, or shell32's again */
    theme_sysicons_reset();
    ntype_cache = 0;
    load_places();
    layout_places();
    layout();
    reload();
    update_toolbar();
    InvalidateRect(main_wnd, NULL, TRUE);
    RedrawWindow(main_wnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
    if (props_wnd) RedrawWindow(props_wnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
    write_state();
}

static LRESULT CALLBACK main_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_CREATE:
        return 0;
    case WM_SIZE:
        layout();
        return 0;
    case WM_GETMINMAXINFO:
    {
        MINMAXINFO *mm = (MINMAXINFO *)lp;
        mm->ptMinTrackSize.x = S(560);
        mm->ptMinTrackSize.y = S(320);
        return 0;
    }
    case WM_PAINT:
        st_paint_window(hwnd, paint_main);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_MEASUREITEM:
    {
        MEASUREITEMSTRUCT *mi = (MEASUREITEMSTRUCT *)lp;
        if (mi->CtlType == ODT_LISTVIEW) { mi->itemHeight = S(ST_ROW_H); return TRUE; }
        break;
    }
    case WM_DRAWITEM:
    {
        const DRAWITEMSTRUCT *di = (const DRAWITEMSTRUCT *)lp;
        if (di->CtlType == ODT_LISTVIEW)
        {
            struct row_ctx ctx = { di };
            RECT r = di->rcItem, cr;
            /* the whole width, so the selection reaches the right edge */
            GetClientRect(lv, &cr);
            r.right = max(r.right, cr.right);
            {
                DRAWITEMSTRUCT copy = *di;
                copy.rcItem = r;
                ctx.di = &copy;
                paint_into(di->hDC, &r, paint_row, &ctx);
            }
            return TRUE;
        }
        break;
    }
    case WM_CTLCOLOREDIT:
        if ((HWND)lp == addr)
        {
            SetTextColor((HDC)wp, C_TEXT);
            SetBkColor((HDC)wp, ST->surface);
            return (LRESULT)field_brush;
        }
        break;
    case WM_MOUSEMOVE:
    {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        if (dragging)
        {
            POINT scr = pt;
            int place, it;
            ClientToScreen(hwnd, &scr);
            if (drag_image) ImageList_DragShowNolock(FALSE);
            it = drop_target_at(scr, &place);
            set_drop_highlight(it, place);
            if (drag_image) { ImageList_DragShowNolock(TRUE); ImageList_DragMove(scr.x, scr.y); }
            SetCursor(LoadCursorW(NULL, (it >= 0 || place >= 0) ? IDC_ARROW : IDC_NO));
            return 0;
        }
        {
            int h = toolbar_hit(pt), c = crumb_hit(pt);
            if (h != tb_hot) { tb_hot = h; update_toolbar(); }
            if (c != crumb_hot) { crumb_hot = c; InvalidateRect(hwnd, &addr_field_rc, FALSE); }
            if (!tracking_mouse)
            {
                TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
                tracking_mouse = TrackMouseEvent(&tme);
            }
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        tracking_mouse = FALSE;
        if (tb_hot != -1) { tb_hot = -1; update_toolbar(); }
        if (crumb_hot != -1) { crumb_hot = -1; InvalidateRect(hwnd, &addr_field_rc, FALSE); }
        return 0;
    case WM_LBUTTONUP:
        if (dragging) { end_drag(TRUE); return 0; }
        {
            POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            int b = toolbar_hit(pt), c = header_hit(pt), k = crumb_hit(pt);
            if (b >= 0 && tbtns[b].enabled) on_command(tbtns[b].id);
            else if (c >= 0) set_sort(c);
            else if (k >= 0) navigate(crumbs[k].target, 0);
            else if (PtInRect(&addr_field_rc, pt)) focus_address();
        }
        return 0;
    case WM_CAPTURECHANGED:
        if (dragging && (HWND)lp != hwnd) end_drag(FALSE);
        return 0;
    case WM_RBUTTONUP:
        if (dragging) { end_drag(FALSE); return 0; }
        break;
    case WM_CONTEXTMENU:
        if ((HWND)wp == lv)
        {
            POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            if (pt.x == -1 && pt.y == -1)
            {
                RECT ir;
                int i = ListView_GetNextItem(lv, -1, LVNI_FOCUSED);
                if (i >= 0 && ListView_GetItemRect(lv, i, &ir, LVIR_LABEL)) { pt.x = ir.left + S(20); pt.y = ir.bottom; }
                else { pt.x = S(40); pt.y = S(40); }
                ClientToScreen(lv, &pt);
            }
            else
            {
                /* right-click on empty space clears the selection */
                LVHITTESTINFO ht = { 0 };
                ht.pt = pt;
                ScreenToClient(lv, &ht.pt);
                if (ListView_HitTest(lv, &ht) < 0) ListView_SetItemState(lv, -1, 0, LVIS_SELECTED);
            }
            show_item_menu(pt);
            return 0;
        }
        break;
    case WM_COMMAND:
        if (LOWORD(wp) == ID_ADDR)
        {
            if (HIWORD(wp) == EN_KILLFOCUS) ShowWindow(addr, SW_HIDE); /* back to the breadcrumbs */
            if (HIWORD(wp) == EN_SETFOCUS || HIWORD(wp) == EN_KILLFOCUS) InvalidateRect(hwnd, &addr_field_rc, FALSE);
            return 0;
        }
        on_command(LOWORD(wp));
        return 0;
    case WM_APPCOMMAND:
        switch (GET_APPCOMMAND_LPARAM(lp))
        {
        case APPCOMMAND_BROWSER_BACKWARD: go_back(); return TRUE;
        case APPCOMMAND_BROWSER_FORWARD: go_forward(); return TRUE;
        case APPCOMMAND_BROWSER_REFRESH: on_command(IDM_REFRESH); return TRUE;
        }
        break;
    case WM_NOTIFY:
        return on_notify((NMHDR *)lp);
    case WM_TIMER:
        if (wp == TIMER_RELOAD)
        {
            KillTimer(hwnd, TIMER_RELOAD);
            if (ListView_GetEditControl(lv)) schedule_reload();
            else reload();
        }
        else if (wp == TIMER_EXIT) DestroyWindow(hwnd);
        else if (wp == TIMER_THEME)
        {
            KillTimer(hwnd, TIMER_THEME);
            apply_theme();
        }
        return 0;
    case WM_APP_SHNOTIFY:
        schedule_reload();
        return 0;
    case WM_CLIPBOARDUPDATE:
        if (cut_paths && GetClipboardSequenceNumber() != our_clip_seq) set_cut_paths(NULL);
        update_toolbar();
        return 0;
    case WM_DROPFILES:
    {
        /* files dropped from another program: copy them here */
        HDROP drop = (HDROP)wp;
        UINT n = DragQueryFileW(drop, 0xffffffff, NULL, 0), i;
        size_t chars = 1;
        WCHAR *m, *q;
        for (i = 0; i < n; i++) chars += DragQueryFileW(drop, i, NULL, 0) + 1;
        q = m = calloc(chars, sizeof(WCHAR));
        for (i = 0; m && i < n; i++) q += DragQueryFileW(drop, i, q, (UINT)(chars - (q - m))) + 1;
        DragFinish(drop);
        if (m && cur_loc == LOC_FS) transfer(m, cur_path, FALSE);
        free(m);
        return 0;
    }
    case WM_COPYDATA:
    {
        /* queue the commands, so the sender never waits on a dialog */
        COPYDATASTRUCT *cds = (COPYDATASTRUCT *)lp;
        WCHAR *copy;
        if (cds->dwData != COPYDATA_MAGIC || !cds->lpData) return FALSE;
        copy = calloc(cds->cbData / sizeof(WCHAR) + 1, sizeof(WCHAR));
        if (!copy) return FALSE;
        memcpy(copy, cds->lpData, cds->cbData);
        PostMessageW(hwnd, WM_APP_COMMANDS, 0, (LPARAM)copy);
        return TRUE;
    }
    case WM_APP_COMMANDS:
    {
        WCHAR *buf = (WCHAR *)lp, *p, *argv[64];
        int argc = 0;
        for (p = buf; argc < 64; )
        {
            WCHAR *sep = wcschr(p, CMD_SEP);
            argv[argc++] = p;
            if (!sep) break;
            *sep = 0;
            p = sep + 1;
        }
        apply_commands(argc, argv);
        free(buf);
        InterlockedIncrement(&cmd_seq);
        write_state();
        return 0;
    }
    case WM_THEMECHANGED:
        SetTimer(hwnd, TIMER_THEME, 50, NULL);
        break;
    case WM_SETTINGCHANGE:
        if (setu_theme_changed_msg(msg, wp, lp)) SetTimer(hwnd, TIMER_THEME, 50, NULL);
        break;
    case WM_DPICHANGED:
    {
        RECT *r = (RECT *)lp;
        dpi = HIWORD(wp);
        make_fonts();
        SendMessageW(lv, WM_SETFONT, (WPARAM)font_ui, TRUE);
        SendMessageW(addr, WM_SETFONT, (WPARAM)font_ui, TRUE);
        layout_places();
        SetWindowPos(hwnd, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_ACTIVATE:
        if (LOWORD(wp) != WA_INACTIVE) PostMessageW(hwnd, WM_APP_SHNOTIFY + 100, 0, 0);
        break;
    case WM_APP_SHNOTIFY + 100:
        if (!GetFocus() || GetFocus() == hwnd) SetFocus(lv);
        return 0;
    case WM_DESTROY:
        if (shnotify_id) SHChangeNotifyDeregister(shnotify_id);
        RemoveClipboardFormatListener(hwnd);
        props_close();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ------------------------------------------------------------------ */

static void default_start(WCHAR *out)
{
    WCHAR *home = NULL;
    if (SUCCEEDED(SHGetKnownFolderPath(&FOLDERID_Profile, 0, NULL, &home)) && home)
    {
        lstrcpynW(out, home, MAX_PATH);
        CoTaskMemFree(home);
    }
    else lstrcpyW(out, L"C:\\");
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmdline, int show)
{
    WNDCLASSEXW wc;
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES };
    WCHAR **argv, start[MAX_PATH];
    int argc, i;
    MSG msg;
    SHFILEINFOW sfi;
    RECT wa;

    (void)prev; (void)cmdline;
    hinst = inst;
    argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    set_dpi_awareness();
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    OleInitialize(NULL);
    InitCommonControlsEx(&icc);
    dpi = get_window_dpi(NULL);
    st_theme_reload();
    make_fonts();
    field_brush = CreateSolidBrush(ST->surface);
    panel_brush = CreateSolidBrush(C_PANEL);
    bar_brush = CreateSolidBrush(C_BAR);
    cf_effect = RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT);
    app_icon_big = make_app_icon(GetSystemMetrics(SM_CXICON));
    app_icon_small = make_app_icon(GetSystemMetrics(SM_CXSMICON));

    il_small = (HIMAGELIST)SHGetFileInfoW(L"C:\\", 0, &sfi, sizeof(sfi), SHGFI_SYSICONINDEX | SHGFI_SMALLICON);
    il_large = (HIMAGELIST)SHGetFileInfoW(L"C:\\", 0, &sfi, sizeof(sfi), SHGFI_SYSICONINDEX | SHGFI_LARGEICON);

    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hIcon = app_icon_big;
    wc.hIconSm = app_icon_small;
    wc.lpfnWndProc = main_proc;
    wc.lpszClassName = MAIN_CLASS;
    wc.hbrBackground = panel_brush;
    RegisterClassExW(&wc);
    wc.lpfnWndProc = sidebar_proc;
    wc.lpszClassName = SIDE_CLASS;
    wc.hbrBackground = bar_brush;
    RegisterClassExW(&wc);
    wc.lpfnWndProc = props_proc;
    wc.lpszClassName = PROPS_CLASS;
    wc.hbrBackground = panel_brush;
    RegisterClassExW(&wc);

    init_toolbar();
    default_start(start);
    /* the first plain argument is where to start */
    for (i = 1; i < argc; i++)
    {
        if (argv[i][0] == L'-' && argv[i][1] == L'-')
        {
            if (!wcscmp(argv[i], L"--state-file") || !wcscmp(argv[i], L"--exit-after")) i++;
            continue;
        }
        lstrcpynW(start, argv[i], ARRAYSIZE(start));
        break;
    }

    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    {
        int w = min(S(1000), wa.right - wa.left - S(40)), h = min(S(640), wa.bottom - wa.top - S(40));
        main_wnd = CreateWindowExW(WS_EX_ACCEPTFILES | WS_EX_APPWINDOW, MAIN_CLASS, L"Files",
                                   WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, wa.left + (wa.right - wa.left - w) / 2,
                                   wa.top + (wa.bottom - wa.top - h) / 2, w, h, NULL, NULL, inst, NULL);
    }
    if (!main_wnd) return 1;
    dpi = get_window_dpi(main_wnd);
    make_fonts();

    /* shown while it has the focus; the breadcrumbs are drawn otherwise */
    addr = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL, 0, 0, 10, 10, main_wnd,
                           (HMENU)(INT_PTR)ID_ADDR, inst, NULL);
    SendMessageW(addr, WM_SETFONT, (WPARAM)font_ui, FALSE);
    SHAutoComplete(addr, SHACF_FILESYS_DIRS | SHACF_AUTOSUGGEST_FORCE_ON);
    (void)addr_orig_proc;

    sidebar = CreateWindowExW(0, SIDE_CLASS, L"Places", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 10, 10, main_wnd,
                              (HMENU)(INT_PTR)ID_SIDEBAR, inst, NULL);
    lv = CreateWindowExW(0, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_NOCOLUMNHEADER |
                         LVS_OWNERDRAWFIXED |
                         LVS_SHOWSELALWAYS | LVS_EDITLABELS | LVS_SHAREIMAGELISTS | LVS_AUTOARRANGE,
                         0, 0, 10, 10, main_wnd, (HMENU)(INT_PTR)ID_LIST, inst, NULL);
    ListView_SetExtendedListViewStyle(lv, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP);
    ListView_SetBkColor(lv, C_PANEL);
    ListView_SetTextBkColor(lv, C_PANEL);
    ListView_SetTextColor(lv, C_TEXT);
    SendMessageW(lv, WM_SETFONT, (WPARAM)font_ui, FALSE);
    for (i = 0; i < 4; i++)
    {
        LVCOLUMNW c = { LVCF_WIDTH | LVCF_FMT | LVCF_SUBITEM };
        c.cx = S(100);
        c.iSubItem = i;
        c.fmt = LVCFMT_LEFT;
        ListView_InsertColumn(lv, i, &c);
    }
    AddClipboardFormatListener(main_wnd);
    tips = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, NULL, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
                           CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, main_wnd, NULL, inst, NULL);
    if (tips) SendMessageW(tips, WM_SETFONT, (WPARAM)font_ui, FALSE);

    load_places();
    layout_places();
    layout();

    /* options first (the state file), then where to go */
    if (argc > 1)
    {
        WCHAR **opts = malloc(argc * sizeof(*opts));
        int n = 0;
        for (i = 1; opts && i < argc; i++)
        {
            if (argv[i][0] != L'-' || argv[i][1] != L'-') continue;
            opts[n++] = argv[i];
            if (i + 1 < argc && argv[i + 1][0] != L'-') opts[n++] = argv[++i];
        }
        if (opts) apply_commands(n, opts);
        free(opts);
    }
    LocalFree(argv);
    navigate(start, NAV_NEW);
    if (!nitems && cur_loc != LOC_FS && wcscmp(start, L"::drives") && lstrcmpiW(start, L"Drives") &&
        lstrcmpiW(start, L"Recycle Bin") && wcscmp(start, L"::recycle"))
    {
        WCHAR home[MAX_PATH];
        default_start(home);
        navigate(home, NAV_NEW);
    }
    ShowWindow(main_wnd, show ? show : SW_SHOWNORMAL);
    UpdateWindow(main_wnd);
    SetFocus(lv);
    write_state();

    for (;;)
    {
        DWORD r;
        HANDLE hs[1];
        DWORD nh = 0;
        if (change_handle != INVALID_HANDLE_VALUE) hs[nh++] = change_handle;
        r = MsgWaitForMultipleObjectsEx(nh, hs, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (nh && r == WAIT_OBJECT_0)
        {
            /* the folder changed outside our own operations */
            FindNextChangeNotification(change_handle);
            schedule_reload();
            continue;
        }
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT) goto done;
            if (props_wnd && IsDialogMessageW(props_wnd, &msg)) continue;
            if (handle_key(&msg)) continue;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
done:
    if (change_handle != INVALID_HANDLE_VALUE) FindCloseChangeNotification(change_handle);
    free_items();
    OleUninitialize();
    CoUninitialize();
    return (int)msg.wParam;
}
