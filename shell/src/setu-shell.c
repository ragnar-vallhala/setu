/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * setu-shell: the Setu desktop shell (taskbar and start menu).
 *
 * A Win32 program built with mingw-w64 that runs on the Setu personality.
 * It uses only public, documented APIs (user32, gdi32, shell32, ole32,
 * advapi32, kernel32). Main references (learn.microsoft.com):
 *   - "Using Application Desktop Toolbars" / SHAppBarMessage
 *   - "The Taskbar" > "Managing Taskbar Buttons" (which windows get a button)
 *   - "Shell Links" / IShellLinkW, IPersistFile
 *   - SHGetFolderPathW (CSIDL_PROGRAMS, CSIDL_COMMON_PROGRAMS)
 *   - ShellExecuteExW, SHDefExtractIconW, SHGetFileInfoW
 *   - "Uninstall Registry Key" (Windows Installer): DisplayName,
 *     UninstallString, InstallLocation; ARPPRODUCTICON (DisplayIcon),
 *     ARPSYSTEMCOMPONENT (SystemComponent)
 *   - FindFirstChangeNotificationW, RegNotifyChangeKeyValue,
 *     SHChangeNotifyRegister (refresh when installers add shortcuts)
 *   - RegisterHotKey, LowLevelKeyboardProc (Ctrl+Esc and the Windows key)
 *   - RegisterShellHookWindow, SetProcessDpiAwarenessContext, GetDpiForWindow
 *   - WM_COPYDATA (used to forward commands to a running shell)
 *
 * Command line (also accepted by a second instance, which forwards them to
 * the running shell and exits):
 *   --open-launcher | --close-launcher | --toggle-launcher
 *   --launch NAME      start the menu entry called NAME
 *   --run CMD          run CMD as if typed into the menu's search box
 *   --search TEXT      type TEXT into the search box
 *   --pin NAME | --unpin NAME   pin or unpin a menu entry
 *   --reload           rescan the Start Menu and the Uninstall keys
 *   --state-file PATH  write the shell's state (for tests) to PATH
 *   --exit-after SEC   quit after SEC seconds (first instance only)
 *   --quit             quit the running shell
 *
 * Pins are kept in HKCU\Software\Setu\Shell, value "Pinned" (REG_MULTI_SZ).
 */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define COBJMACROS
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commctrl.h>
#include <objbase.h>
#include <stdio.h>
#include <wchar.h>

#define BAR_CLASS      L"SetuShellTaskbar"
#define LAUNCHER_CLASS L"SetuShellLauncher"
#define COPYDATA_MAGIC 0x55544553 /* "SETU" */
#define CMD_SEP        L'\x1f'

#define WM_APP_APPBAR  (WM_APP + 1)
#define WM_APP_CHANGED (WM_APP + 2)   /* Start Menu or Uninstall keys changed */
#define WM_APP_SHNOTIFY (WM_APP + 3)  /* SHChangeNotifyRegister */
#define WM_APP_TOGGLE  (WM_APP + 4)   /* Windows key tapped */
#define TIMER_REFRESH  1
#define TIMER_CLOCK    2
#define TIMER_EXIT     3
#define TIMER_RELOAD   4
#define HOTKEY_MENU    1
#define ID_EDIT        100
#define IDM_CLOSE      200
#define IDM_MINIMIZE   201
#define IDM_RESTORE    202
#define IDM_OPEN       210
#define IDM_PIN        211
#define IDM_UNPIN      212
#define IDM_LOCATION   213
#define IDM_UNINSTALL  214
#define IDM_EXPAND     215

#define IDM_SIGNOUT    220
#define IDM_RESTART    221
#define IDM_SHUTDOWN   222
#define IDM_HOME       223
#define TIMER_TIPS     5

/* Colours, fonts, shapes and icons: docs/DESIGN.md, via setu-style.h. */
#include "setu-style.h"

/* ------------------------------------------------------------------ */
/* State                                                                */

enum app_kind { APP_BUILTIN, APP_SHORTCUT, APP_URL, APP_OTHER };

struct app
{
    enum app_kind kind;
    WCHAR name[128];
    WCHAR key[MAX_PATH];        /* stable id for pins: "Folder\\Name" */
    WCHAR file[MAX_PATH];       /* what ShellExecuteExW opens */
    WCHAR args[1024];
    WCHAR dir[MAX_PATH];
    WCHAR lnk[MAX_PATH];        /* the .lnk/.url itself (fallback target) */
    WCHAR desc[256];
    WCHAR folder[128];          /* top-level Start Menu folder, "" if none */
    WCHAR icon_file[MAX_PATH];
    int icon_index;
    WCHAR uninstall[1024];      /* UninstallString of the matching entry */
    WCHAR location[MAX_PATH];   /* InstallLocation (other apps) */
    int group;                  /* index into groups[], -1 at top level */
    BOOL common;                /* found in the common Programs folder */
    BOOL pinned;
    HICON icon;                 /* owned by the icon cache */
};

struct group
{
    WCHAR name[128];
    WCHAR path[MAX_PATH];       /* a folder holding it, for "Open file location" */
    WCHAR uninstall[1024];
    int count;
    BOOL expanded;
};

struct task
{
    HWND hwnd;
    WCHAR title[256];
    HICON icon;
    BOOL active;
    BOOL minimized;
    RECT rc;
};

enum row_type { ROW_LETTER, ROW_SECTION, ROW_APP, ROW_GROUP, ROW_HINT };

struct row
{
    enum row_type type;
    int index;                  /* app or group index */
    int indent;
    WCHAR text[32];             /* letter or section title */
};

#define MAX_APPS   1024
#define MAX_GROUPS 256
#define MAX_TASKS  64
#define MAX_ROWS   (MAX_APPS + MAX_GROUPS + 64)
#define MAX_PINS   24          /* a grid of 6 x 4 */

static struct app apps[MAX_APPS];
static int napps;
static struct group groups[MAX_GROUPS];
static int ngroups;
static struct task tasks[MAX_TASKS];
static int ntasks;

static struct row rows[MAX_ROWS];
static int nrows;
static int pins[MAX_PINS];      /* app indices of the visible pins */
static int npins;
static WCHAR pin_keys[MAX_PINS][MAX_PATH];
static int npin_keys;
enum { SEL_NONE, SEL_TILE, SEL_ROW, SEL_HEADER, SEL_USER, SEL_POWER };
/* the start menu shows the pins, or the A-Z list; a search shows results */
enum { VIEW_PINNED, VIEW_ALL };
static int view = VIEW_PINNED;
static BOOL bar_left_aligned;   /* HKCU\\Software\\Setu\\Shell TaskbarAlignment = 1 */
static int sel_kind = SEL_NONE, sel_index = -1;
static int scroll_px;
static WCHAR search_text[256];
static int reload_count;
static BOOL in_menu;

static HINSTANCE hinst;
static HWND bar, launcher, edit;
static WNDPROC edit_orig_proc;
static UINT dpi = 96;
static HBRUSH field_brush;
static HWND tips;
static int hot = -100;          /* -1 start, >= 0 task index, -100 none */
static BOOL tracking_mouse;
static RECT start_rc, tray_rc;
static HWND last_active;
static BOOL appbar_ok, shellhook_ok, workarea_set;
static RECT saved_workarea;
static UINT shellhook_msg;
static WCHAR state_path[MAX_PATH];
static WCHAR status_text[256];
static DWORD launcher_hidden_at;
static WCHAR clock_text[64], date_text[64];
static BOOL hotkey_ok, kbd_hook_ok;
static HHOOK kbd_hook;
static ULONG shnotify_id;
static int nwatches;
static HANDLE watch_stop;

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

static void make_fonts(void)
{
    st_make_fonts(dpi);
}

static BOOL ends_with_i(const WCHAR *s, const WCHAR *suffix)
{
    size_t a = wcslen(s), b = wcslen(suffix);
    return a >= b && !lstrcmpiW(s + a - b, suffix);
}

static void set_status(const WCHAR *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf(status_text, ARRAYSIZE(status_text) - 1, fmt, ap);
    status_text[ARRAYSIZE(status_text) - 1] = 0;
    va_end(ap);
    if (launcher) InvalidateRect(launcher, NULL, FALSE);
}

/* ------------------------------------------------------------------ */
/* State file (for tests)                                               */

static void tile_rect(int i, RECT *r);
static int row_y(int row);
static int row_height(int row);
static int list_top(void);
static int list_bottom(void);
static int panel_left(void);
static RECT header_btn, user_btn, power_btn;

static void write_state(void)
{
    WCHAR tmp[MAX_PATH + 8];
    static WCHAR buf[32768];
    static char utf8[98304];
    int len = 0, i;
    RECT wa, br;
    HANDLE h;
    DWORD done;

    if (!state_path[0]) return;

#define EMIT(...) do { int room_ = (int)ARRAYSIZE(buf) - len - 1; if (room_ > 0) { int n_ = _snwprintf(buf + len, room_, __VA_ARGS__); len += n_ >= 0 ? n_ : room_; } } while (0)
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    GetWindowRect(bar, &br);
    EMIT(L"dpi=%u\r\n", dpi);
    EMIT(L"appbar=%d\r\n", appbar_ok);
    EMIT(L"shellhook=%d\r\n", shellhook_ok);
    EMIT(L"workarea_set=%d\r\n", workarea_set);
    EMIT(L"taskbar_rect=%ld,%ld,%ld,%ld\r\n", br.left, br.top, br.right, br.bottom);
    EMIT(L"workarea=%ld,%ld,%ld,%ld\r\n", wa.left, wa.top, wa.right, wa.bottom);
    EMIT(L"launcher=%ls\r\n", IsWindowVisible(launcher) ? L"open" : L"closed");
    EMIT(L"clock=%ls\r\n", clock_text);
    EMIT(L"status=%ls\r\n", status_text);
    EMIT(L"hotkey=%d\r\n", hotkey_ok);
    EMIT(L"winkey_hook=%d\r\n", kbd_hook_ok);
    EMIT(L"watches=%d\r\n", nwatches);
    EMIT(L"shnotify=%d\r\n", shnotify_id != 0);
    EMIT(L"reloads=%d\r\n", reload_count);
    EMIT(L"search=%ls\r\n", search_text);
    EMIT(L"view=%ls\r\n", search_text[0] ? L"search" : view == VIEW_ALL ? L"all" : L"pinned");
    EMIT(L"taskbar_alignment=%ls\r\n", bar_left_aligned ? L"left" : L"center");
    {
        /* the parts of the launcher, centres in screen coordinates */
        static const struct { const WCHAR *name; const RECT *rc; int kind; } parts[] = {
            { L"header_button", &header_btn, SEL_HEADER },
            { L"user_button", &user_btn, SEL_USER },
            { L"power_button", &power_btn, SEL_POWER },
        };
        for (i = 0; i < (int)ARRAYSIZE(parts); i++)
        {
            POINT c = { (parts[i].rc->left + parts[i].rc->right) / 2, (parts[i].rc->top + parts[i].rc->bottom) / 2 };
            ClientToScreen(launcher, &c);
            EMIT(L"%ls=%ls|%ld,%ld%ls\r\n", parts[i].name,
                 parts[i].kind == SEL_HEADER ? (view == VIEW_ALL ? L"Back" : L"All apps") : L"-",
                 c.x, c.y, sel_kind == parts[i].kind ? L"|selected" : L"");
        }
    }
    /* app=kind|name|file|folder|icon|pinned|uninstall|source */
    for (i = 0; i < napps; i++)
    {
        static const WCHAR *const kinds[] = { L"builtin", L"shortcut", L"url", L"other" };
        const struct app *a = &apps[i];
        EMIT(L"app=%ls|%ls|%ls|%ls|%ls|%ls|%ls|%ls\r\n", kinds[a->kind], a->name, a->file, a->folder,
             a->icon ? L"icon" : L"glyph", a->pinned ? L"pinned" : L"-", a->uninstall[0] ? L"uninstall" : L"-",
             a->kind == APP_SHORTCUT || a->kind == APP_URL ? (a->common ? L"common" : L"user") : L"-");
    }
    for (i = 0; i < ngroups; i++)
        EMIT(L"group=%ls|%ls|%d\r\n", groups[i].name, groups[i].expanded ? L"expanded" : L"collapsed",
             groups[i].count);
    /* pins and rows with their centres in screen coordinates, for clicks */
    for (i = 0; i < npins; i++)
    {
        RECT r;
        POINT c;
        tile_rect(i, &r);
        c.x = (r.left + r.right) / 2;
        c.y = (r.top + r.bottom) / 2;
        ClientToScreen(launcher, &c);
        EMIT(L"pin=%d|%ls|%ld,%ld%ls\r\n", i, apps[pins[i]].name, c.x, c.y,
             sel_kind == SEL_TILE && sel_index == i ? L"|selected" : L"");
    }
    for (i = 0; i < nrows; i++)
    {
        static const WCHAR *const types[] = { L"letter", L"section", L"app", L"group", L"hint" };
        const struct row *rw = &rows[i];
        const WCHAR *label = rw->type == ROW_APP ? apps[rw->index].name :
                             rw->type == ROW_GROUP ? groups[rw->index].name : rw->text;
        POINT c = { panel_left() + S(200), row_y(i) + row_height(i) / 2 };
        BOOL vis = row_y(i) >= list_top() && row_y(i) + row_height(i) <= list_bottom();
        ClientToScreen(launcher, &c);
        EMIT(L"row=%d|%ls|%ls|%d|%ld,%ld|%ls%ls\r\n", i, types[rw->type], label, rw->indent, c.x, c.y,
             vis ? L"visible" : L"hidden", sel_kind == SEL_ROW && sel_index == i ? L"|selected" : L"");
    }
    for (i = 0; i < ntasks; i++)
    {
        /* button centres in screen coordinates, so tests can click them */
        POINT c = { (tasks[i].rc.left + tasks[i].rc.right) / 2, (tasks[i].rc.top + tasks[i].rc.bottom) / 2 };
        ClientToScreen(bar, &c);
        EMIT(L"task=%p|%ls|%ls%ls|%ld,%ld\r\n", tasks[i].hwnd, tasks[i].title,
            tasks[i].active ? L"active" : L"inactive", tasks[i].minimized ? L",minimized" : L"", c.x, c.y);
    }
    {
        POINT c = { (start_rc.left + start_rc.right) / 2, (start_rc.top + start_rc.bottom) / 2 };
        ClientToScreen(bar, &c);
        EMIT(L"start_button=%ld,%ld\r\n", c.x, c.y);
    }
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
/* Icons, cached per (file, index, size)                                */

struct icon_entry { WCHAR file[MAX_PATH]; int index, size; HICON icon; };
static struct icon_entry *icon_cache;
static int nicon_cache, icon_cache_cap;

static void flush_icon_cache(void)
{
    int i;
    for (i = 0; i < nicon_cache; i++)
        if (icon_cache[i].icon) DestroyIcon(icon_cache[i].icon);
    nicon_cache = 0;
}

/* The icon at FILE,INDEX, SIZE pixels square, or NULL. Misses are cached
 * as well, so a file without icons is only read once. */
static HICON cached_icon(const WCHAR *file, int index, int size)
{
    HICON icon = NULL;
    int i;

    if (!file || !file[0]) return NULL;
    for (i = 0; i < nicon_cache; i++)
        if (icon_cache[i].index == index && icon_cache[i].size == size && !lstrcmpiW(icon_cache[i].file, file))
            return icon_cache[i].icon;
    if (SHDefExtractIconW(file, index, 0, &icon, NULL, MAKELONG(size, size)) != S_OK) icon = NULL;
    if (nicon_cache == icon_cache_cap)
    {
        int cap = icon_cache_cap ? icon_cache_cap * 2 : 64;
        struct icon_entry *n = realloc(icon_cache, cap * sizeof(*n));
        if (!n) return icon;
        icon_cache = n;
        icon_cache_cap = cap;
    }
    lstrcpynW(icon_cache[nicon_cache].file, file, MAX_PATH);
    icon_cache[nicon_cache].index = index;
    icon_cache[nicon_cache].size = size;
    icon_cache[nicon_cache++].icon = icon;
    return icon;
}

/* A program's own icon, or a document's icon from its file association. */
static HICON file_icon(const WCHAR *file, int size)
{
    SHFILEINFOW sfi;

    if (!file || !file[0]) return NULL;
    if (ends_with_i(file, L".exe") || ends_with_i(file, L".ico") || ends_with_i(file, L".dll"))
        return cached_icon(file, 0, size);
    memset(&sfi, 0, sizeof(sfi));
    if (SHGetFileInfoW(file, 0, &sfi, sizeof(sfi), SHGFI_ICONLOCATION) && sfi.szDisplayName[0])
        return cached_icon(sfi.szDisplayName, sfi.iIcon, size);
    return NULL;
}

/* An app's icon: the link's icon location, else its target's icon. NULL
 * means "draw our own glyph". */
static HICON app_icon(const struct app *a, int size)
{
    WCHAR f[MAX_PATH];
    HICON icon;

    if (a->icon_file[0])
    {
        ExpandEnvironmentStringsW(a->icon_file, f, ARRAYSIZE(f));
        if ((icon = cached_icon(f, a->icon_index, size))) return icon;
    }
    return file_icon(a->file, size);
}

/* ------------------------------------------------------------------ */
/* Apps: built-ins, Start Menu shortcuts and Uninstall entries          */

static WCHAR user_programs[MAX_PATH], common_programs[MAX_PATH];

static struct app *find_app(const WCHAR *name)
{
    int i;
    for (i = 0; i < napps; i++)
        if (!lstrcmpiW(apps[i].name, name)) return &apps[i];
    for (i = 0; i < napps; i++)
        if (!lstrcmpiW(apps[i].key, name)) return &apps[i];
    for (i = 0; i < napps; i++)
        if (!_wcsnicmp(apps[i].name, name, wcslen(name))) return &apps[i];
    return NULL;
}

static struct app *find_key(const WCHAR *key)
{
    int i;
    for (i = 0; i < napps; i++)
        if (!lstrcmpiW(apps[i].key, key)) return &apps[i];
    return NULL;
}

/* Look for EXE next to setu-shell.exe, in the system directory, then on
 * the search path. */
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

static struct app *new_app(enum app_kind kind)
{
    struct app *a;
    if (napps >= MAX_APPS) return NULL;
    a = &apps[napps];
    memset(a, 0, sizeof(*a));
    a->kind = kind;
    a->group = -1;
    return a;
}

static void add_builtin(const WCHAR *name, const WCHAR *const *exes, const WCHAR *args)
{
    WCHAR path[MAX_PATH];
    struct app *a;

    for (; *exes; exes++)
        if (find_program(*exes, path, ARRAYSIZE(path))) break;
    if (!*exes || !(a = new_app(APP_BUILTIN))) return;
    lstrcpynW(a->name, name, ARRAYSIZE(a->name));
    lstrcpynW(a->key, name, ARRAYSIZE(a->key));
    lstrcpynW(a->file, path, ARRAYSIZE(a->file));
    if (args) lstrcpynW(a->args, args, ARRAYSIZE(a->args));
    napps++;
}

/* A .lnk or .url file found under a Programs folder. REL is its folder
 * relative to Programs ("" at the top). */
static void add_link(const WCHAR *path, const WCHAR *rel, BOOL common)
{
    IShellLinkW *sl = NULL;
    IPersistFile *pf = NULL;
    SHFILEINFOW sfi;
    struct app *a;
    WCHAR raw[MAX_PATH];
    const WCHAR *base, *slash;
    BOOL url = ends_with_i(path, L".url");

    if (!(a = new_app(url ? APP_URL : APP_SHORTCUT))) return;
    a->common = common;
    lstrcpynW(a->lnk, path, ARRAYSIZE(a->lnk));

    /* the name the shell shows for the file (no extension for links) */
    memset(&sfi, 0, sizeof(sfi));
    if (SHGetFileInfoW(path, 0, &sfi, sizeof(sfi), SHGFI_DISPLAYNAME) && sfi.szDisplayName[0])
        lstrcpynW(a->name, sfi.szDisplayName, ARRAYSIZE(a->name));
    else
    {
        base = wcsrchr(path, L'\\');
        lstrcpynW(a->name, base ? base + 1 : path, ARRAYSIZE(a->name));
    }
    if (ends_with_i(a->name, L".lnk") || ends_with_i(a->name, L".url")) a->name[wcslen(a->name) - 4] = 0;

    if (rel[0])
    {
        slash = wcschr(rel, L'\\');
        lstrcpynW(a->folder, rel, slash ? min((int)ARRAYSIZE(a->folder), (int)(slash - rel) + 1) : (int)ARRAYSIZE(a->folder));
        swprintf(a->key, ARRAYSIZE(a->key), L"%ls\\%ls", rel, a->name);
    }
    else lstrcpynW(a->key, a->name, ARRAYSIZE(a->key));

    /* the per-user folder is scanned first and wins over the common one */
    if (find_key(a->key)) return;

    if (!url &&
        SUCCEEDED(CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkW, (void **)&sl)) &&
        SUCCEEDED(IShellLinkW_QueryInterface(sl, &IID_IPersistFile, (void **)&pf)) &&
        SUCCEEDED(IPersistFile_Load(pf, path, STGM_READ)))
    {
        /* SLR_NO_UI: never show UI; the timeout is in the high word. */
        IShellLinkW_Resolve(sl, NULL, SLR_NO_UI | SLR_NOUPDATE | SLR_NOSEARCH | SLR_NOTRACK | (500 << 16));
        raw[0] = 0;
        if (SUCCEEDED(IShellLinkW_GetPath(sl, raw, ARRAYSIZE(raw), NULL, 0)) && raw[0])
            ExpandEnvironmentStringsW(raw, a->file, ARRAYSIZE(a->file));
        IShellLinkW_GetArguments(sl, a->args, ARRAYSIZE(a->args));
        raw[0] = 0;
        if (SUCCEEDED(IShellLinkW_GetWorkingDirectory(sl, raw, ARRAYSIZE(raw))) && raw[0])
            ExpandEnvironmentStringsW(raw, a->dir, ARRAYSIZE(a->dir));
        IShellLinkW_GetDescription(sl, a->desc, ARRAYSIZE(a->desc));
        if (FAILED(IShellLinkW_GetIconLocation(sl, a->icon_file, ARRAYSIZE(a->icon_file), &a->icon_index)))
            a->icon_file[0] = 0;
    }
    if (pf) IPersistFile_Release(pf);
    if (sl) IShellLinkW_Release(sl);

    /* An Internet shortcut, or a link without a file-system target (e.g.
     * an advertised shortcut), is opened through the file itself. */
    if (!a->file[0]) lstrcpynW(a->file, path, ARRAYSIZE(a->file));
    napps++;
}

static void scan_programs(const WCHAR *dir, const WCHAR *rel, int depth, BOOL common)
{
    WCHAR pattern[MAX_PATH], path[MAX_PATH], sub[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;

    if (depth > 4) return;
    if (swprintf(pattern, ARRAYSIZE(pattern), L"%ls\\*", dir) < 0) return;
    h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do
    {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        if (swprintf(path, ARRAYSIZE(path), L"%ls\\%ls", dir, fd.cFileName) < 0) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            if (swprintf(sub, ARRAYSIZE(sub), rel[0] ? L"%ls\\%ls" : L"%ls%ls", rel, fd.cFileName) < 0) continue;
            scan_programs(path, sub, depth + 1, common);
        }
        else if (fd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM))
            continue; /* desktop.ini and friends */
        else if (ends_with_i(fd.cFileName, L".lnk") || ends_with_i(fd.cFileName, L".url"))
            add_link(path, rel, common);
        /* .appref-ms (ClickOnce) is not publicly documented: not listed */
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

/* Entries of the Uninstall key ("Uninstall Registry Key" on
 * learn.microsoft.com). */
struct uninst
{
    WCHAR name[128];
    WCHAR cmd[1024];            /* UninstallString */
    WCHAR icon[MAX_PATH];       /* DisplayIcon: "file[,index]" */
    int icon_index;
    WCHAR location[MAX_PATH];   /* InstallLocation */
    BOOL used;
};
#define MAX_UNINST 256
static struct uninst *uninsts;
static int nuninsts;

static void get_reg_sz(HKEY key, const WCHAR *sub, const WCHAR *value, WCHAR *buf, DWORD len)
{
    DWORD size = len * sizeof(WCHAR);
    if (RegGetValueW(key, sub, value, RRF_RT_REG_SZ, NULL, buf, &size)) buf[0] = 0;
}

static void read_uninstall(HKEY root, REGSAM view)
{
    WCHAR sub[256];
    HKEY key;
    DWORD i, len, sys, size;
    struct uninst *u;
    int j;

    if (RegOpenKeyExW(root, L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall", 0,
                      KEY_READ | view, &key)) return;
    for (i = 0; nuninsts < MAX_UNINST; i++)
    {
        WCHAR *comma;
        len = ARRAYSIZE(sub);
        if (RegEnumKeyExW(key, i, sub, &len, NULL, NULL, NULL, NULL)) break;
        u = &uninsts[nuninsts];
        memset(u, 0, sizeof(*u));
        get_reg_sz(key, sub, L"DisplayName", u->name, ARRAYSIZE(u->name));
        if (!u->name[0]) continue;
        size = sizeof(sys);
        /* ARPSYSTEMCOMPONENT: not shown in the list of installed programs */
        if (!RegGetValueW(key, sub, L"SystemComponent", RRF_RT_REG_DWORD, NULL, &sys, &size) && sys == 1) continue;
        for (j = 0; j < nuninsts; j++)
            if (!lstrcmpiW(uninsts[j].name, u->name)) break;
        if (j < nuninsts) continue;
        get_reg_sz(key, sub, L"UninstallString", u->cmd, ARRAYSIZE(u->cmd));
        get_reg_sz(key, sub, L"InstallLocation", u->location, ARRAYSIZE(u->location));
        get_reg_sz(key, sub, L"DisplayIcon", u->icon, ARRAYSIZE(u->icon));
        /* "path,index", the path possibly quoted */
        if ((comma = wcsrchr(u->icon, L',')) && (iswdigit(comma[1]) || comma[1] == L'-'))
        {
            u->icon_index = _wtoi(comma + 1);
            *comma = 0;
        }
        if (u->icon[0] == L'"')
        {
            memmove(u->icon, u->icon + 1, wcslen(u->icon) * sizeof(WCHAR));
            if ((comma = wcschr(u->icon, L'"'))) *comma = 0;
        }
        len = (DWORD)wcslen(u->location);
        while (len && u->location[len - 1] == L'\\') u->location[--len] = 0;
        nuninsts++;
    }
    RegCloseKey(key);
}

/* Is FILE inside directory DIR? */
static BOOL path_under(const WCHAR *file, const WCHAR *dir)
{
    size_t n = wcslen(dir);
    return n > 3 && !_wcsnicmp(file, dir, n) && file[n] == L'\\';
}

/* The directory of the program a command line starts with, or "". The
 * Windows and system directories don't count: too many things live there. */
static void program_dir(const WCHAR *cmd, WCHAR *dir)
{
    WCHAR win[MAX_PATH], sys[MAX_PATH], low[1024], *p;
    const WCHAR *end;

    if (*cmd == L'"')
    {
        cmd++;
        end = wcschr(cmd, L'"');
    }
    else
    {
        lstrcpynW(low, cmd, ARRAYSIZE(low));
        CharLowerW(low);
        p = wcsstr(low, L".exe");
        end = p ? cmd + (p - low) + 4 : NULL;
    }
    if (!end) end = cmd + wcslen(cmd);
    lstrcpynW(low, cmd, min(MAX_PATH, (int)(end - cmd) + 1));
    if ((p = wcsrchr(low, L'\\'))) *p = 0;
    else low[0] = 0;
    GetWindowsDirectoryW(win, ARRAYSIZE(win));
    GetSystemDirectoryW(sys, ARRAYSIZE(sys));
    if (!lstrcmpiW(low, win) || !lstrcmpiW(low, sys) || wcslen(low) <= 3) low[0] = 0;
    lstrcpynW(dir, low, MAX_PATH);
}

/* Does the Uninstall entry U belong to the shortcut A? */
static BOOL uninst_matches(const struct uninst *u, const struct app *a)
{
    WCHAR dir[MAX_PATH], quoted[MAX_PATH + 2];
    size_t n;

    if (a->kind != APP_SHORTCUT && a->kind != APP_URL) return FALSE;
    if (!lstrcmpiW(u->name, a->name)) return TRUE;
    if (a->folder[0])
    {
        n = wcslen(a->folder);
        /* the "Foo" folder for the "Foo 1.2 (x64)" entry */
        if (!_wcsnicmp(u->name, a->folder, n) && (!u->name[n] || u->name[n] == L' ')) return TRUE;
    }
    if (a->kind != APP_SHORTCUT || !lstrcmpiW(a->file, a->lnk)) return FALSE;
    if (u->location[0] && path_under(a->file, u->location)) return TRUE;
    program_dir(u->cmd, dir);
    if (dir[0] && path_under(a->file, dir)) return TRUE;
    if (u->icon[0] && ends_with_i(u->icon, L".exe"))
    {
        swprintf(quoted, ARRAYSIZE(quoted), L"\"%ls\"", u->icon);
        program_dir(quoted, dir);
        if (dir[0] && path_under(a->file, dir)) return TRUE;
    }
    return FALSE;
}

static void load_uninstall(void)
{
    int i, j, n = napps;

    if (!uninsts && !(uninsts = malloc(MAX_UNINST * sizeof(*uninsts)))) return;
    nuninsts = 0;
    read_uninstall(HKEY_CURRENT_USER, 0);
    read_uninstall(HKEY_LOCAL_MACHINE, KEY_WOW64_64KEY);
    read_uninstall(HKEY_LOCAL_MACHINE, KEY_WOW64_32KEY);

    for (i = 0; i < nuninsts; i++)
        for (j = 0; j < n; j++)
            if (uninst_matches(&uninsts[i], &apps[j]))
            {
                if (!apps[j].uninstall[0]) lstrcpynW(apps[j].uninstall, uninsts[i].cmd, ARRAYSIZE(apps[j].uninstall));
                uninsts[i].used = TRUE;
            }

    /* "Other apps": installed programs without a shortcut */
    for (i = 0; i < nuninsts; i++)
    {
        struct uninst *u = &uninsts[i];
        struct app *a;
        const WCHAR *base;

        if (u->used || !(a = new_app(APP_OTHER))) continue;
        lstrcpynW(a->name, u->name, ARRAYSIZE(a->name));
        swprintf(a->key, ARRAYSIZE(a->key), L"?other\\%ls", u->name);
        lstrcpynW(a->uninstall, u->cmd, ARRAYSIZE(a->uninstall));
        lstrcpynW(a->location, u->location, ARRAYSIZE(a->location));
        lstrcpynW(a->icon_file, u->icon, ARRAYSIZE(a->icon_file));
        a->icon_index = u->icon_index;
        /* DisplayIcon usually names the program: start that, unless it is
         * the uninstaller itself */
        base = wcsrchr(u->icon, L'\\');
        base = base ? base + 1 : u->icon;
        if (ends_with_i(u->icon, L".exe") && _wcsnicmp(base, L"unins", 5) &&
            GetFileAttributesW(u->icon) != INVALID_FILE_ATTRIBUTES)
            lstrcpynW(a->file, u->icon, ARRAYSIZE(a->file));
        napps++;
    }
}

static int name_cmp(const WCHAR *a, const WCHAR *b)
{
    return CompareStringEx(LOCALE_NAME_USER_DEFAULT, NORM_IGNORECASE, a, -1, b, -1, NULL, NULL, 0) - 2;
}

static int __cdecl cmp_apps(const void *a, const void *b)
{
    const struct app *x = a, *y = b;
    int r = name_cmp(x->name, y->name);
    return r ? r : lstrcmpiW(x->key, y->key);
}

/* Folders become groups; a folder holding a single app is collapsed into
 * that app, which is then shown at the top level. */
static void build_groups(void)
{
    int i, j;

    ngroups = 0;
    for (i = 0; i < napps; i++)
    {
        struct app *a = &apps[i];
        int count = 0;
        if (!a->folder[0] || a->kind == APP_OTHER) continue;
        for (j = 0; j < ngroups; j++)
            if (!lstrcmpiW(groups[j].name, a->folder)) break;
        if (j < ngroups) { a->group = j; groups[j].count++; continue; }
        for (j = 0; j < napps; j++)
            if (!lstrcmpiW(apps[j].folder, a->folder) && apps[j].kind != APP_OTHER) count++;
        if (count < 2 || ngroups == MAX_GROUPS) continue;
        memset(&groups[ngroups], 0, sizeof(groups[0]));
        lstrcpynW(groups[ngroups].name, a->folder, ARRAYSIZE(groups[0].name));
        swprintf(groups[ngroups].path, MAX_PATH, L"%ls\\%ls", a->common ? common_programs : user_programs, a->folder);
        groups[ngroups].count = 1;
        a->group = ngroups++;
    }
    for (i = 0; i < napps; i++)
        if (apps[i].group >= 0 && apps[i].uninstall[0] && !groups[apps[i].group].uninstall[0])
            lstrcpynW(groups[apps[i].group].uninstall, apps[i].uninstall, ARRAYSIZE(groups[0].uninstall));
}

/* Pins: HKCU\Software\Setu\Shell, "Pinned" (REG_MULTI_SZ, in order). */
#define SHELL_KEY L"Software\\Setu\\Shell"

/* TaskbarAlignment (REG_DWORD): 0 centres the buttons (the default), 1
 * puts them at the left. Changes apply on WM_SETTINGCHANGE "Setu.Shell". */
static void load_settings(void)
{
    DWORD v = 0, size = sizeof(v);
    bar_left_aligned = !RegGetValueW(HKEY_CURRENT_USER, SHELL_KEY, L"TaskbarAlignment", RRF_RT_REG_DWORD, NULL,
                                     &v, &size) && v == 1;
}

static void map_pins(void)
{
    int i;
    struct app *a;

    for (i = 0; i < napps; i++) apps[i].pinned = FALSE;
    npins = 0;
    for (i = 0; i < npin_keys; i++)
        if ((a = find_key(pin_keys[i])) && !a->pinned)
        {
            a->pinned = TRUE;
            pins[npins++] = (int)(a - apps);
        }
}

static void load_pins(void)
{
    static const WCHAR *const defaults[] = { L"Files", L"Notepad", L"Command Prompt" };
    static WCHAR buf[MAX_PINS * MAX_PATH + 2];
    WCHAR *p;
    DWORD size = sizeof(buf) - 2 * sizeof(WCHAR);
    int i;

    npin_keys = 0;
    memset(buf, 0, sizeof(buf));
    if (!RegGetValueW(HKEY_CURRENT_USER, SHELL_KEY, L"Pinned", RRF_RT_REG_MULTI_SZ, NULL, buf, &size))
    {
        for (p = buf; *p && npin_keys < MAX_PINS; p += wcslen(p) + 1)
            lstrcpynW(pin_keys[npin_keys++], p, MAX_PATH);
    }
    else /* nothing saved yet */
        for (i = 0; i < (int)ARRAYSIZE(defaults); i++)
            lstrcpynW(pin_keys[npin_keys++], defaults[i], MAX_PATH);
}

static BOOL save_pins(void)
{
    static WCHAR buf[MAX_PINS * MAX_PATH + 2];
    WCHAR *p = buf;
    int i;

    for (i = 0; i < npin_keys; i++)
    {
        wcscpy(p, pin_keys[i]);
        p += wcslen(p) + 1;
    }
    if (!npin_keys) *p++ = 0;
    *p++ = 0;
    return !RegSetKeyValueW(HKEY_CURRENT_USER, SHELL_KEY, L"Pinned", REG_MULTI_SZ, buf,
                            (DWORD)((p - buf) * sizeof(WCHAR)));
}

static BOOL pin_app(const struct app *a, BOOL pin)
{
    int i;

    for (i = 0; i < npin_keys; i++)
        if (!lstrcmpiW(pin_keys[i], a->key)) break;
    if (pin)
    {
        if (i < npin_keys) return TRUE;
        if (npin_keys == MAX_PINS) return FALSE;
        lstrcpynW(pin_keys[npin_keys++], a->key, MAX_PATH);
    }
    else
    {
        if (i == npin_keys) return TRUE;
        memmove(pin_keys[i], pin_keys[i + 1], (npin_keys - i - 1) * sizeof(pin_keys[0]));
        npin_keys--;
    }
    map_pins();
    return save_pins();
}

static void free_apps(void)
{
    napps = 0;
    ngroups = 0;
    npins = 0;
}

static void load_apps(void)
{
    static const WCHAR *const notepad[] = { L"notepad.exe", NULL };
    static const WCHAR *const cmd[] = { L"cmd.exe", NULL };
    static const WCHAR *const files[] = { L"setu-files.exe", L"explorer.exe", L"winefile.exe", NULL };
    static const WCHAR *const regedit[] = { L"regedit.exe", NULL };
    static WCHAR expanded[MAX_GROUPS][128];
    int nexpanded = 0, i, j;

    for (i = 0; i < ngroups; i++)
        if (groups[i].expanded) lstrcpynW(expanded[nexpanded++], groups[i].name, 128);

    free_apps();
    add_builtin(L"Files", files, NULL);
    add_builtin(L"Notepad", notepad, NULL);
    add_builtin(L"Command Prompt", cmd, NULL);
    add_builtin(L"Registry Editor", regedit, NULL);

    /* CSIDL_FLAG_CREATE: the folders must exist to be watched */
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_PROGRAMS | CSIDL_FLAG_CREATE, NULL, SHGFP_TYPE_CURRENT, user_programs)))
        user_programs[0] = 0;
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_COMMON_PROGRAMS | CSIDL_FLAG_CREATE, NULL, SHGFP_TYPE_CURRENT, common_programs)))
        common_programs[0] = 0;
    if (user_programs[0]) scan_programs(user_programs, L"", 0, FALSE);
    if (common_programs[0] && lstrcmpiW(user_programs, common_programs)) scan_programs(common_programs, L"", 0, TRUE);
    load_uninstall();

    qsort(apps, napps, sizeof(apps[0]), cmp_apps);
    build_groups();
    for (i = 0; i < ngroups; i++)
        for (j = 0; j < nexpanded; j++)
            if (!lstrcmpiW(groups[i].name, expanded[j])) groups[i].expanded = TRUE;
    for (i = 0; i < napps; i++) apps[i].icon = app_icon(&apps[i], S(24));
    map_pins();
    reload_count++;
}

/* ------------------------------------------------------------------ */
/* Launching                                                            */

static BOOL shell_execute(const WCHAR *file, const WCHAR *args, const WCHAR *dir)
{
    SHELLEXECUTEINFOW sei;

    memset(&sei, 0, sizeof(sei));
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    sei.hwnd = bar;
    sei.lpFile = file;
    sei.lpParameters = (args && args[0]) ? args : NULL;
    sei.lpDirectory = (dir && dir[0]) ? dir : NULL;
    sei.nShow = SW_SHOWNORMAL;
    return ShellExecuteExW(&sei);
}

static void hide_launcher(void);
BOOL apply_commands(int argc, WCHAR **argv);

static BOOL launch_app(const struct app *a)
{
    BOOL ok;
    DWORD err;

    if (!a->file[0])
    {
        /* an installed program whose Uninstall entry names no program */
        set_status(L"%ls has no program to start", a->name);
        write_state();
        return FALSE;
    }
    ok = shell_execute(a->file, a->args, a->dir);
    err = ok ? 0 : GetLastError();
    if (!ok && a->kind == APP_SHORTCUT && lstrcmpiW(a->file, a->lnk))
        ok = shell_execute(a->lnk, NULL, NULL);
    if (ok)
    {
        set_status(L"Started %ls", a->name);
        hide_launcher();
    }
    else set_status(L"Could not start %ls (error %lu)", a->name, err);
    write_state();
    return ok;
}

/* Show FOLDER, with ITEM selected if given: setu-files.exe if it's
 * installed, else explorer.exe (/select). */
static BOOL open_location(const WCHAR *folder, const WCHAR *item)
{
    WCHAR files[MAX_PATH], args[MAX_PATH * 2 + 16];
    BOOL ok;

    if (!folder[0]) return FALSE;
    if (find_program(L"setu-files.exe", files, ARRAYSIZE(files)))
    {
        swprintf(args, ARRAYSIZE(args), L"\"%ls\"", folder);
        ok = shell_execute(files, args, NULL);
    }
    else
    {
        if (item && item[0]) swprintf(args, ARRAYSIZE(args), L"/select,\"%ls\"", item);
        else swprintf(args, ARRAYSIZE(args), L"\"%ls\"", folder);
        ok = shell_execute(L"explorer.exe", args, NULL);
    }
    if (ok)
    {
        set_status(L"Opened %ls", folder);
        hide_launcher();
    }
    else set_status(L"Could not open %ls", folder);
    write_state();
    return ok;
}

static BOOL open_app_location(const struct app *a)
{
    WCHAR dir[MAX_PATH], *slash;
    const WCHAR *item = a->kind == APP_BUILTIN ? a->file : a->lnk;

    if (a->kind == APP_OTHER) return open_location(a->location, NULL);
    lstrcpynW(dir, item, ARRAYSIZE(dir));
    if ((slash = wcsrchr(dir, L'\\'))) *slash = 0;
    return open_location(dir, item);
}

/* Run an UninstallString, which is a command line. */
static BOOL run_uninstall(const WCHAR *name, const WCHAR *cmd)
{
    WCHAR line[2048];
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    BOOL ok;

    if (!cmd[0]) return FALSE;
    ExpandEnvironmentStringsW(cmd, line, ARRAYSIZE(line));
    ok = CreateProcessW(NULL, line, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
    if (ok)
    {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        set_status(L"Uninstalling %ls", name);
        hide_launcher();
    }
    else set_status(L"Could not start the uninstaller of %ls (error %lu)", name, GetLastError());
    write_state();
    return ok;
}

/* Split "cmd" into the program and its arguments. A quoted first token,
 * or the whole string if it names an existing file, is the program. */
static void split_command(const WCHAR *cmd, WCHAR *file, int file_len, const WCHAR **args)
{
    const WCHAR *p = cmd, *end;

    if (GetFileAttributesW(cmd) != INVALID_FILE_ATTRIBUTES)
    {
        lstrcpynW(file, cmd, file_len);
        *args = L"";
        return;
    }
    if (*p == L'"')
    {
        end = wcschr(++p, L'"');
        if (!end) end = p + wcslen(p);
        lstrcpynW(file, p, min(file_len, (int)(end - p) + 1));
        p = *end ? end + 1 : end;
    }
    else
    {
        end = p;
        while (*end && *end != L' ' && *end != L'\t') end++;
        lstrcpynW(file, p, min(file_len, (int)(end - p) + 1));
        p = end;
    }
    while (*p == L' ' || *p == L'\t') p++;
    *args = p;
}

static BOOL run_command(const WCHAR *input)
{
    WCHAR cmd[2048], file[MAX_PATH], msi_args[2048];
    const WCHAR *args;
    BOOL ok;
    DWORD err;
    int n;

    ExpandEnvironmentStringsW(input, cmd, ARRAYSIZE(cmd));
    for (n = (int)wcslen(cmd); n > 0 && (cmd[n - 1] == L' ' || cmd[n - 1] == L'\t'); n--) cmd[n - 1] = 0;
    args = cmd;
    while (*args == L' ' || *args == L'\t') args++;
    if (!*args) return FALSE;
    memmove(cmd, args, (wcslen(args) + 1) * sizeof(WCHAR));

    split_command(cmd, file, ARRAYSIZE(file), &args);
    ok = shell_execute(file, args, NULL);
    err = ok ? 0 : GetLastError();
    if (!ok && ends_with_i(file, L".msi"))
    {
        /* No file association for .msi: hand it to the installer directly. */
        swprintf(msi_args, ARRAYSIZE(msi_args), L"/i \"%ls\" %ls", file, args);
        ok = shell_execute(L"msiexec.exe", msi_args, NULL);
    }
    if (ok)
    {
        set_status(L"Ran %ls", cmd);
        hide_launcher();
    }
    else set_status(L"Could not run \"%ls\" (error %lu)", file, err);
    write_state();
    return ok;
}

/* ------------------------------------------------------------------ */
/* Taskbar windows                                                      */

/* "Managing Taskbar Buttons": an unowned window gets a button unless it
 * has WS_EX_TOOLWINDOW; WS_EX_APPWINDOW forces a button. Only visible
 * windows are shown. */
static BOOL wants_button(HWND hwnd)
{
    LONG ex;
    if (hwnd == bar || hwnd == launcher) return FALSE;
    if (!IsWindowVisible(hwnd)) return FALSE;
    ex = GetWindowLongW(hwnd, GWL_EXSTYLE);
    if (ex & WS_EX_APPWINDOW) return TRUE;
    if (GetWindow(hwnd, GW_OWNER)) return FALSE;
    if (ex & WS_EX_TOOLWINDOW) return FALSE;
    return TRUE;
}

/* An icon handle is usable here if GetIconInfo accepts it. Icons are user
 * objects, whose handles are public to all processes ("User Objects" on
 * learn.microsoft.com); where that does not hold, fall back to the icon of
 * the window's executable. */
static BOOL icon_usable(HICON icon)
{
    ICONINFO ii;
    if (!icon || !GetIconInfo(icon, &ii)) return FALSE;
    if (ii.hbmMask) DeleteObject(ii.hbmMask);
    if (ii.hbmColor) DeleteObject(ii.hbmColor);
    return TRUE;
}

static HICON exe_icon(HWND hwnd)
{
    WCHAR path[MAX_PATH];
    DWORD pid = 0, len = ARRAYSIZE(path);
    HANDLE proc;

    GetWindowThreadProcessId(hwnd, &pid);
    if (!(proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid))) return NULL;
    if (!QueryFullProcessImageNameW(proc, 0, path, &len)) path[0] = 0;
    CloseHandle(proc);
    return path[0] ? file_icon(path, S(ST_TASK_ICON)) : NULL;
}

/* The button's icon. The program's own icon, read from its file at the
 * button's size, stays sharp at 24 px; a window icon (WM_GETICON, the
 * class icon) is the fallback, the big one first since it scales better. */
static HICON window_icon(HWND hwnd)
{
    static const WPARAM kinds[] = { ICON_BIG, ICON_SMALL2, ICON_SMALL };
    DWORD_PTR r;
    HICON icon;
    int i;

    if ((icon = exe_icon(hwnd))) return icon;
    for (i = 0; i < (int)ARRAYSIZE(kinds); i++)
    {
        r = 0;
        if (SendMessageTimeoutW(hwnd, WM_GETICON, kinds[i], 0, SMTO_ABORTIFHUNG, 50, &r) &&
            icon_usable((HICON)r))
            return (HICON)r;
    }
    if (icon_usable(icon = (HICON)GetClassLongPtrW(hwnd, GCLP_HICON))) return icon;
    if (icon_usable(icon = (HICON)GetClassLongPtrW(hwnd, GCLP_HICONSM))) return icon;
    return LoadIconW(NULL, IDI_APPLICATION);
}

struct enum_ctx { HWND found[MAX_TASKS]; int n; };

static BOOL CALLBACK enum_proc(HWND hwnd, LPARAM lp)
{
    struct enum_ctx *ctx = (struct enum_ctx *)lp;
    if (ctx->n < MAX_TASKS && wants_button(hwnd)) ctx->found[ctx->n++] = hwnd;
    return TRUE;
}

static BOOL is_ours(HWND hwnd)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    return pid == GetCurrentProcessId();
}

static void layout_tasks(void);

static void refresh_tasks(void)
{
    struct enum_ctx ctx;
    struct task next[MAX_TASKS];
    int nnext = 0, i, j;
    HWND fg = GetForegroundWindow(), fg_root;
    BOOL changed;

    if (fg && !is_ours(fg)) last_active = fg;
    if (last_active && !IsWindow(last_active)) last_active = NULL;
    fg_root = last_active ? GetAncestor(last_active, GA_ROOTOWNER) : NULL;

    ctx.n = 0;
    EnumWindows(enum_proc, (LPARAM)&ctx);

    /* keep existing buttons in place, append new windows at the end */
    for (i = 0; i < ntasks; i++)
        for (j = 0; j < ctx.n; j++)
            if (ctx.found[j] == tasks[i].hwnd)
            {
                next[nnext++] = tasks[i];
                ctx.found[j] = NULL;
                break;
            }
    for (j = ctx.n - 1; j >= 0; j--) /* EnumWindows is top-first; oldest last */
        if (ctx.found[j] && nnext < MAX_TASKS)
        {
            memset(&next[nnext], 0, sizeof(next[0]));
            next[nnext++].hwnd = ctx.found[j];
        }

    for (i = 0; i < nnext; i++)
    {
        struct task *t = &next[i];
        WCHAR title[256] = L"";
        InternalGetWindowText(t->hwnd, title, ARRAYSIZE(title));
        if (!title[0]) lstrcpynW(title, L"(untitled)", ARRAYSIZE(title));
        lstrcpynW(t->title, title, ARRAYSIZE(t->title));
        t->icon = window_icon(t->hwnd);
        t->minimized = IsIconic(t->hwnd);
        t->active = !t->minimized && (t->hwnd == last_active || t->hwnd == fg_root);
    }

    changed = nnext != ntasks;
    for (i = 0; !changed && i < nnext; i++)
        changed = next[i].hwnd != tasks[i].hwnd || next[i].active != tasks[i].active ||
                  next[i].minimized != tasks[i].minimized || next[i].icon != tasks[i].icon ||
                  wcscmp(next[i].title, tasks[i].title);
    memcpy(tasks, next, nnext * sizeof(tasks[0]));
    ntasks = nnext;
    if (changed)
    {
        layout_tasks();
        InvalidateRect(bar, NULL, FALSE);
    }
    write_state();
}

static void activate_task(struct task *t)
{
    if (!IsWindow(t->hwnd)) return;
    if (IsIconic(t->hwnd))
    {
        ShowWindow(t->hwnd, SW_RESTORE);
        SetForegroundWindow(t->hwnd);
    }
    else if (t->active)
    {
        ShowWindow(t->hwnd, SW_MINIMIZE);
        last_active = NULL;
    }
    else SetForegroundWindow(t->hwnd);
    refresh_tasks();
}

static void task_menu(struct task *t, POINT pt)
{
    HMENU m = CreatePopupMenu();
    HWND hwnd = t->hwnd;
    int cmd;

    if (IsIconic(hwnd)) AppendMenuW(m, MF_STRING, IDM_RESTORE, L"Restore");
    else AppendMenuW(m, MF_STRING, IDM_MINIMIZE, L"Minimize");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, IDM_CLOSE, L"Close window");
    SetForegroundWindow(bar); /* required for the menu to dismiss properly */
    cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_BOTTOMALIGN | TPM_LEFTALIGN | TPM_RIGHTBUTTON,
                         pt.x, pt.y, 0, bar, NULL);
    DestroyMenu(m);
    if (cmd == IDM_CLOSE) PostMessageW(hwnd, WM_CLOSE, 0, 0);
    else if (cmd == IDM_MINIMIZE) ShowWindow(hwnd, SW_MINIMIZE);
    else if (cmd == IDM_RESTORE) { ShowWindow(hwnd, SW_RESTORE); SetForegroundWindow(hwnd); }
    refresh_tasks();
}

/* ------------------------------------------------------------------ */
/* Taskbar window                                                       */

static int bar_height(void) { return S(ST_TASKBAR_H); }

static void update_clock(void)
{
    WCHAR t[64] = L"", d[64] = L"";
    GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, NULL, NULL, t, ARRAYSIZE(t));
    GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, NULL, NULL, d, ARRAYSIZE(d), NULL);
    if (wcscmp(t, clock_text) || wcscmp(d, date_text))
    {
        lstrcpynW(clock_text, t, ARRAYSIZE(clock_text));
        lstrcpynW(date_text, d, ARRAYSIZE(date_text));
        InvalidateRect(bar, &tray_rc, FALSE);
    }
}

/* Tooltips name the buttons, which only show icons. The text comes from
 * TTN_GETDISPINFOW, so it follows title changes. */
static void update_tips(void)
{
    TTTOOLINFOW ti;
    int i, n;

    if (!tips) return;
    n = (int)SendMessageW(tips, TTM_GETTOOLCOUNT, 0, 0);
    for (i = n - 1; i >= 0; i--)
    {
        memset(&ti, 0, sizeof(ti));
        ti.cbSize = sizeof(ti);
        if (SendMessageW(tips, TTM_ENUMTOOLSW, i, (LPARAM)&ti)) SendMessageW(tips, TTM_DELTOOLW, 0, (LPARAM)&ti);
    }
    for (i = -1; i < ntasks; i++)
    {
        memset(&ti, 0, sizeof(ti));
        ti.cbSize = sizeof(ti);
        ti.uFlags = TTF_SUBCLASS;
        ti.hwnd = bar;
        ti.uId = (UINT_PTR)(i + 1);
        ti.rect = i < 0 ? start_rc : tasks[i].rc;
        ti.lpszText = LPSTR_TEXTCALLBACKW;
        SendMessageW(tips, TTM_ADDTOOLW, 0, (LPARAM)&ti);
    }
}

/* The Setu button and the window buttons form one row, centred on the
 * bar (or at its left edge, if TaskbarAlignment is 1); the clock is at
 * the right. */
static void layout_tasks(void)
{
    RECT rc;
    int h, bw = S(ST_TASK_BUTTON), gap = S(4), top, n, pitch, total, x, i, avail;

    GetClientRect(bar, &rc);
    h = rc.bottom;
    top = (h - bw) / 2;
    SetRect(&tray_rc, rc.right - S(8) - S(92), top, rc.right - S(8), top + bw);
    n = ntasks + 1;
    avail = tray_rc.left - S(16);
    pitch = bw + gap;
    if (n * pitch - gap > avail) pitch = max(S(20), (avail + gap) / n);
    bw = min(bw, pitch - gap);
    total = n * pitch - gap;
    x = bar_left_aligned ? S(8) : (rc.right - total) / 2;
    if (x + total > tray_rc.left - S(8)) x = max(S(8), (int)tray_rc.left - S(8) - total);
    SetRect(&start_rc, x, top, x + bw, top + S(ST_TASK_BUTTON));
    for (i = 0; i < ntasks; i++)
    {
        x += pitch;
        SetRect(&tasks[i].rc, x, top, x + bw, top + S(ST_TASK_BUTTON));
    }
    update_tips();
}

static void paint_bar(struct setu_canvas *cv, const RECT *rc)
{
    COLORREF hover = st_mix(ST->taskbar, ST->surface, 170);
    RECT r;
    int i, isz = S(ST_TASK_ICON), rad = S(ST_RADIUS_CONTROL);

    st_fill(cv, rc, ST->taskbar);
    r = *rc;
    r.bottom = r.top + max(1, S(1));
    st_fill(cv, &r, ST->border);

    /* the Setu button: the bridge mark */
    if (IsWindowVisible(launcher))
    {
        st_round(cv, &start_rc, rad, hover);
        st_frame(cv, &start_rc, rad, 1, ST->border_subtle);
    }
    else if (hot == -1) st_round(cv, &start_rc, rad, hover);
    st_icon_in(cv, ICON_SETU_MARK, &start_rc, isz, ST->logo);

    /* window buttons: icon, and a pill under it (long for the active one) */
    for (i = 0; i < ntasks; i++)
    {
        struct task *t = &tasks[i];
        RECT b = t->rc, pill;
        int pw = t->active ? S(16) : S(6), cx = (b.left + b.right) / 2;

        if (t->active)
        {
            st_round(cv, &b, rad, hover);
            st_frame(cv, &b, rad, 1, ST->border_subtle);
        }
        else if (hot == i) st_round(cv, &b, rad, hover);
        if (t->icon)
            DrawIconEx(cv->dc, cx - isz / 2, (b.top + b.bottom - isz) / 2 - S(1), t->icon, isz, isz, 0, NULL, DI_NORMAL);
        SetRect(&pill, cx - pw / 2, b.bottom - S(ST_PILL_H) - S(1), cx + pw / 2, b.bottom - S(1));
        st_round(cv, &pill, S(ST_PILL_H), t->minimized ? ST->text_secondary : ST->accent);
    }

    /* tray: time over date */
    r = tray_rc;
    r.right -= S(10);
    r.bottom = (tray_rc.top + tray_rc.bottom) / 2 + S(1);
    st_text(cv, clock_text, &r, SF_BODY, ST->text, DT_SINGLELINE | DT_BOTTOM | DT_RIGHT);
    r.top = r.bottom + S(1);
    r.bottom = tray_rc.bottom;
    st_text(cv, date_text, &r, SF_BODY, ST->text, DT_SINGLELINE | DT_TOP | DT_RIGHT);
}

static int bar_hit(POINT pt)
{
    int i;
    if (PtInRect(&start_rc, pt)) return -1;
    for (i = 0; i < ntasks; i++)
        if (PtInRect(&tasks[i].rc, pt)) return i;
    return -100;
}

static void get_primary_monitor(RECT *mon)
{
    POINT origin = { 0, 0 };
    MONITORINFO mi = { sizeof(mi) };
    HMONITOR hm = MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
    if (GetMonitorInfoW(hm, &mi)) *mon = mi.rcMonitor;
    else SetRect(mon, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
}

/* Dock to the bottom of the primary monitor, as an appbar if possible. */
static void dock_bar(void)
{
    RECT mon, wa, want;
    APPBARDATA abd;
    int h;

    dpi = get_window_dpi(bar);
    h = bar_height();
    get_primary_monitor(&mon);
    want = mon;
    want.top = mon.bottom - h;

    if (appbar_ok)
    {
        memset(&abd, 0, sizeof(abd));
        abd.cbSize = sizeof(abd);
        abd.hWnd = bar;
        abd.uEdge = ABE_BOTTOM;
        abd.rc = want;
        SHAppBarMessage(ABM_QUERYPOS, &abd);
        abd.rc.top = abd.rc.bottom - h;
        SHAppBarMessage(ABM_SETPOS, &abd);
        want = abd.rc;
    }
    SetWindowPos(bar, HWND_TOPMOST, want.left, want.top, want.right - want.left,
                 want.bottom - want.top, SWP_NOACTIVATE);

    /* If the appbar did not shrink the work area, reserve the space
     * ourselves (SPI_SETWORKAREA), and put it back on exit. */
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    if (wa.bottom > want.top)
    {
        if (!workarea_set) saved_workarea = wa;
        wa = mon;
        wa.bottom = want.top;
        workarea_set = SystemParametersInfoW(SPI_SETWORKAREA, 0, &wa, SPIF_SENDCHANGE);
    }
    layout_tasks();
    InvalidateRect(bar, NULL, FALSE);
}

static void show_launcher(void);
static void toggle_launcher(void);
static void reload_apps(void);
static void refresh_rows(BOOL keep);

/* ------------------------------------------------------------------ */
/* Refresh: watch the Programs folders and the Uninstall keys           */

#define UNINSTALL_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall"

/* FindFirstChangeNotificationW on both Programs folders (with their
 * subfolders) and RegNotifyChangeKeyValue on the Uninstall keys; any
 * change posts WM_APP_CHANGED to the taskbar, which rescans shortly
 * after (installers write many files in a row). */
static DWORD WINAPI watch_thread(void *arg)
{
    static const struct { HKEY root; REGSAM view; } regs[] = {
        { HKEY_CURRENT_USER, 0 },
        { HKEY_LOCAL_MACHINE, KEY_WOW64_64KEY },
        { HKEY_LOCAL_MACHINE, KEY_WOW64_32KEY },
    };
    const WCHAR *dirs[2] = { user_programs, common_programs };
    HANDLE handles[8];
    HKEY keys[8] = { 0 };
    int nh = 0, i;
    DWORD r;

    handles[nh++] = watch_stop;
    for (i = 0; i < 2; i++)
    {
        HANDLE h;
        if (!dirs[i][0] || (i && !lstrcmpiW(dirs[0], dirs[1]))) continue;
        h = FindFirstChangeNotificationW(dirs[i], TRUE, FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                                         FILE_NOTIFY_CHANGE_LAST_WRITE);
        if (h != INVALID_HANDLE_VALUE) handles[nh++] = h;
    }
    for (i = 0; i < (int)ARRAYSIZE(regs); i++)
    {
        HKEY k;
        HANDLE ev;
        /* if there's no Uninstall key yet, watch its parent */
        if (RegOpenKeyExW(regs[i].root, UNINSTALL_KEY, 0, KEY_NOTIFY | regs[i].view, &k) &&
            RegOpenKeyExW(regs[i].root, L"Software\\Microsoft\\Windows\\CurrentVersion", 0,
                          KEY_NOTIFY | regs[i].view, &k))
            continue;
        ev = CreateEventW(NULL, FALSE, FALSE, NULL);
        if (!ev || RegNotifyChangeKeyValue(k, TRUE, REG_NOTIFY_CHANGE_NAME | REG_NOTIFY_CHANGE_LAST_SET, ev, TRUE))
        {
            if (ev) CloseHandle(ev);
            RegCloseKey(k);
            continue;
        }
        keys[nh] = k;
        handles[nh++] = ev;
    }
    nwatches = nh - 1;

    for (;;)
    {
        r = WaitForMultipleObjects(nh, handles, FALSE, INFINITE);
        if (r == WAIT_FAILED || r == WAIT_OBJECT_0 || r >= WAIT_OBJECT_0 + (DWORD)nh) break;
        i = r - WAIT_OBJECT_0;
        if (keys[i]) RegNotifyChangeKeyValue(keys[i], TRUE, REG_NOTIFY_CHANGE_NAME | REG_NOTIFY_CHANGE_LAST_SET,
                                             handles[i], TRUE);
        else FindNextChangeNotification(handles[i]);
        PostMessageW(bar, WM_APP_CHANGED, 0, 0);
    }
    for (i = 1; i < nh; i++)
    {
        if (keys[i]) { RegCloseKey(keys[i]); CloseHandle(handles[i]); }
        else FindCloseChangeNotification(handles[i]);
    }
    return 0;
}

/* SHChangeNotifyRegister on the Programs folders too: installers that
 * call SHChangeNotify are heard without polling the file system. */
static void register_shnotify(void)
{
    static const int folders[] = { CSIDL_PROGRAMS, CSIDL_COMMON_PROGRAMS };
    SHChangeNotifyEntry entries[2];
    int n = 0, i;

    for (i = 0; i < (int)ARRAYSIZE(folders); i++)
    {
        PIDLIST_ABSOLUTE pidl;
        if (SUCCEEDED(SHGetSpecialFolderLocation(bar, folders[i], &pidl)))
        {
            entries[n].pidl = pidl;
            entries[n++].fRecursive = TRUE;
        }
    }
    if (n)
        shnotify_id = SHChangeNotifyRegister(bar, SHCNRF_ShellLevel | SHCNRF_InterruptLevel | SHCNRF_NewDelivery,
                                             SHCNE_CREATE | SHCNE_DELETE | SHCNE_RENAMEITEM | SHCNE_MKDIR |
                                             SHCNE_RMDIR | SHCNE_UPDATEDIR | SHCNE_UPDATEITEM | SHCNE_ASSOCCHANGED,
                                             WM_APP_SHNOTIFY, n, entries);
    for (i = 0; i < n; i++) CoTaskMemFree((void *)entries[i].pidl);
}

/* The Windows key on its own (pressed and released with no other key in
 * between) opens the menu. A low-level keyboard hook sees it whichever
 * window has the focus. */
static BOOL win_down, win_combo;

static LRESULT CALLBACK kbd_proc(int code, WPARAM wp, LPARAM lp)
{
    if (code == HC_ACTION)
    {
        const KBDLLHOOKSTRUCT *k = (const KBDLLHOOKSTRUCT *)lp;
        BOOL down = wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN;
        if (k->vkCode == VK_LWIN || k->vkCode == VK_RWIN)
        {
            if (down)
            {
                if (!win_down) win_combo = FALSE;
                win_down = TRUE;
            }
            else
            {
                if (win_down && !win_combo) PostMessageW(bar, WM_APP_TOGGLE, 0, 0);
                win_down = FALSE;
            }
        }
        else if (down && win_down) win_combo = TRUE;
    }
    return CallNextHookEx(kbd_hook, code, wp, lp);
}

static LRESULT CALLBACK bar_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == shellhook_msg && shellhook_msg)
    {
        refresh_tasks();
        return 0;
    }
    switch (msg)
    {
    case WM_PAINT:
        st_paint_window(hwnd, paint_bar);
        return 0;
    case WM_NOTIFY:
    {
        NMTTDISPINFOW *di = (NMTTDISPINFOW *)lp;
        if (di->hdr.hwndFrom == tips && di->hdr.code == TTN_GETDISPINFOW)
        {
            int i = (int)di->hdr.idFrom - 1;
            static WCHAR text[256];
            if (i < 0) lstrcpynW(text, L"Start", ARRAYSIZE(text));
            else if (i < ntasks) lstrcpynW(text, tasks[i].title, ARRAYSIZE(text));
            else text[0] = 0;
            di->lpszText = text;
        }
        return 0;
    }
    case WM_SETTINGCHANGE:
        if (lp && !lstrcmpiW((const WCHAR *)lp, L"Setu.Shell"))
        {
            load_settings();
            layout_tasks();
            InvalidateRect(hwnd, NULL, FALSE);
        }
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEMOVE:
    {
        POINT pt = { (short)LOWORD(lp), (short)HIWORD(lp) };
        int h = bar_hit(pt);
        if (!tracking_mouse)
        {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
            tracking_mouse = TrackMouseEvent(&tme);
        }
        if (h != hot) { hot = h; InvalidateRect(hwnd, NULL, FALSE); }
        return 0;
    }
    case WM_MOUSELEAVE:
        tracking_mouse = FALSE;
        hot = -100;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_LBUTTONUP:
    {
        POINT pt = { (short)LOWORD(lp), (short)HIWORD(lp) };
        int h = bar_hit(pt);
        if (h == -1)
        {
            if (IsWindowVisible(launcher)) hide_launcher();
            else if (GetTickCount() - launcher_hidden_at > 250) show_launcher();
        }
        else if (h >= 0) activate_task(&tasks[h]);
        return 0;
    }
    case WM_RBUTTONUP:
    {
        POINT pt = { (short)LOWORD(lp), (short)HIWORD(lp) };
        int h = bar_hit(pt);
        if (h >= 0)
        {
            ClientToScreen(hwnd, &pt);
            task_menu(&tasks[h], pt);
        }
        return 0;
    }
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_TIMER:
        if (wp == TIMER_REFRESH) refresh_tasks();
        else if (wp == TIMER_CLOCK) update_clock();
        else if (wp == TIMER_EXIT) DestroyWindow(hwnd);
        else if (wp == TIMER_RELOAD)
        {
            KillTimer(hwnd, TIMER_RELOAD);
            reload_apps();
        }
        return 0;
    case WM_APP_CHANGED:
        SetTimer(hwnd, TIMER_RELOAD, 300, NULL);
        return 0;
    case WM_APP_SHNOTIFY:
    {
        PIDLIST_ABSOLUTE *pidls;
        LONG event = 0;
        HANDLE lock = SHChangeNotification_Lock((HANDLE)wp, (DWORD)lp, &pidls, &event);
        if (lock) SHChangeNotification_Unlock(lock);
        if (event & SHCNE_ASSOCCHANGED) flush_icon_cache();
        SetTimer(hwnd, TIMER_RELOAD, 300, NULL);
        return 0;
    }
    case WM_APP_TOGGLE:
    case WM_HOTKEY:
        toggle_launcher();
        return 0;
    case WM_APP_APPBAR:
        if (wp == ABN_POSCHANGED) dock_bar();
        return 0;
    case WM_DISPLAYCHANGE:
        dock_bar();
        return 0;
    case WM_DPICHANGED:
        dpi = HIWORD(wp);
        make_fonts();
        flush_icon_cache();
        load_apps();
        refresh_rows(FALSE);
        dock_bar();
        return 0;
    case WM_COPYDATA:
    {
        const COPYDATASTRUCT *cds = (const COPYDATASTRUCT *)lp;
        WCHAR *buf, *argv[32], *p;
        int argc = 0;
        BOOL ok;

        if (!cds || cds->dwData != COPYDATA_MAGIC || cds->cbData < sizeof(WCHAR)) return FALSE;
        buf = malloc(cds->cbData + sizeof(WCHAR));
        if (!buf) return FALSE;
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
        /* Alt+F4 on the shell should not kill the desktop by accident */
        return 0;
    case WM_QUERYENDSESSION:
        return TRUE;
    case WM_ENDSESSION:
        /* Sign out: tidy up and exit with 0, which ends the session */
        if (wp)
        {
            DestroyWindow(hwnd);
            ExitProcess(0);
        }
        return 0;
    case WM_DESTROY:
    {
        APPBARDATA abd = { sizeof(abd) };
        abd.hWnd = hwnd;
        if (appbar_ok) SHAppBarMessage(ABM_REMOVE, &abd);
        if (workarea_set) SystemParametersInfoW(SPI_SETWORKAREA, 0, &saved_workarea, SPIF_SENDCHANGE);
        if (shellhook_ok) DeregisterShellHookWindow(hwnd);
        if (shnotify_id) SHChangeNotifyDeregister(shnotify_id);
        if (hotkey_ok) UnregisterHotKey(hwnd, HOTKEY_MENU);
        if (kbd_hook) UnhookWindowsHookEx(kbd_hook);
        if (watch_stop) SetEvent(watch_stop);
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ------------------------------------------------------------------ */
/* Start menu                                                           */

/* Our own glyphs: a lettered tile for apps without an icon, and a folder
 * tile for groups (and Files). */
static void draw_glyph(struct setu_canvas *cv, int x, int y, int size, const WCHAR *name)
{
    static const COLORREF tones[] = {
        RGB(0x2f, 0x7d, 0x6a), RGB(0x5b, 0x5f, 0xc7), RGB(0xb5, 0x5a, 0x3c),
        RGB(0x1f, 0x6f, 0xb2), RGB(0x8a, 0x6d, 0x1f), RGB(0x9b, 0x4d, 0x87),
    };
    unsigned h = 0;
    const WCHAR *p;
    WCHAR letter[2] = { name[0] ? name[0] : L'?', 0 };
    RECT r = { x, y, x + size, y + size };
    LOGFONTW lf;
    HFONT f;
    HGDIOBJ of;

    for (p = name; *p; p++) h = h * 31 + towlower(*p);
    st_round(cv, &r, max(S(ST_RADIUS_CONTROL), size / 5), tones[h % ARRAYSIZE(tones)]);
    CharUpperBuffW(letter, 1);
    GetObjectW(st_font[SF_CAPTION], sizeof(lf), &lf);
    lf.lfHeight = -size * 11 / 20;
    f = CreateFontIndirectW(&lf);
    of = SelectObject(cv->dc, f);
    SetTextColor(cv->dc, RGB(0xff, 0xff, 0xff));
    DrawTextW(cv->dc, letter, 1, &r, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
    SelectObject(cv->dc, of);
    DeleteObject(f);
}

static void draw_folder(struct setu_canvas *cv, int x, int y, int size)
{
    RECT r = { x, y, x + size, y + size };
    st_round(cv, &r, max(S(ST_RADIUS_CONTROL), size / 5), st_mix(ST->surface, ST->accent, 40));
    st_icon_in(cv, ICON_FOLDER, &r, size * 2 / 3, ST->accent);
}

/* An app's icon, or our glyph when it has none (a folder for Files). */
static void draw_app_icon(struct setu_canvas *cv, const struct app *a, int x, int y, int size)
{
    HICON icon = size == S(24) ? a->icon : app_icon(a, size);
    if (icon) DrawIconEx(cv->dc, x, y, icon, size, size, 0, NULL, DI_NORMAL);
    else if (a->kind == APP_BUILTIN && !wcscmp(a->key, L"Files")) draw_folder(cv, x, y, size);
    else draw_glyph(cv, x, y, size, a->name);
}

/* --- layout (client coordinates) ---
 *
 * The launcher window is the panel plus a margin on every side, where the
 * panel's shadow is drawn over a copy of what was under the window when
 * it opened (Wine's virtual desktop has no translucent windows). Without
 * that copy there's no margin and a rounded window region instead. */

#define PIN_COLS 6
#define PIN_ROWS 4

static int shadow_px;              /* the margin; 0 when there's no shadow */
static HBITMAP backdrop;           /* what was under the window */
static RECT header_btn, user_btn, power_btn, search_rc;
static BOOL kbd_sel;               /* the selection came from the keyboard */
static WCHAR user_name[128];

static void panel_rect(RECT *r)
{
    GetClientRect(launcher, r);
    InflateRect(r, -shadow_px, -shadow_px);
}
static int panel_left(void) { RECT r; panel_rect(&r); return r.left; }
static int panel_top(void) { RECT r; panel_rect(&r); return r.top; }
static int menu_width(void) { RECT r; panel_rect(&r); return r.right - r.left; }
static int tile_cols(void) { return PIN_COLS; }
static int tiles_top(void) { return panel_top() + S(116); }
static int tiles_visible(void) { return !search_text[0] && view == VIEW_PINNED; }
static int list_visible(void) { return search_text[0] || view == VIEW_ALL; }
static int footer_top(void) { RECT r; panel_rect(&r); return r.bottom - S(64); }

static void tile_rect(int i, RECT *r)
{
    int cols = tile_cols(), pitch = (menu_width() - S(48)) / cols;
    int x = panel_left() + S(24) + (i % cols) * pitch, y = tiles_top() + (i / cols) * S(88);
    SetRect(r, x, y, x + pitch, y + S(84));
    if (!tiles_visible() || i / cols >= PIN_ROWS) SetRect(r, -1000, -1000, -1000, -1000);
}

static int list_top(void)
{
    if (!list_visible()) return footer_top();
    return panel_top() + (search_text[0] ? S(76) : S(116));
}

static int list_bottom(void) { return footer_top() - S(4); }

static int row_height(int row)
{
    switch (rows[row].type)
    {
    case ROW_LETTER: return S(32);
    case ROW_SECTION: return S(32);
    case ROW_HINT: return S(64);
    default: return S(40);
    }
}

static int row_y(int row)
{
    int y = list_top() - scroll_px, i;
    for (i = 0; i < row; i++) y += row_height(i);
    return y;
}

static int content_height(void) { return row_y(nrows) - row_y(0); }

static void clamp_scroll(void)
{
    int max_scroll = content_height() - (list_bottom() - list_top());
    if (scroll_px > max_scroll) scroll_px = max_scroll;
    if (scroll_px < 0) scroll_px = 0;
}

static void ensure_visible(int row)
{
    int y, h;
    if (row < 0 || row >= nrows) return;
    y = row_y(row);
    h = row_height(row);
    /* show the letter header above the first app of a letter, too */
    if (row > 0 && rows[row - 1].type != ROW_APP && rows[row - 1].type != ROW_GROUP)
    {
        y -= row_height(row - 1);
        h += row_height(row - 1);
    }
    if (y < list_top()) scroll_px -= list_top() - y;
    else if (y + h > list_bottom()) scroll_px += y + h - list_bottom();
    clamp_scroll();
}

/* --- rows --- */

static void add_row(enum row_type type, int index, int indent, const WCHAR *text)
{
    struct row *r;
    if (nrows >= MAX_ROWS) return;
    r = &rows[nrows++];
    r->type = type;
    r->index = index;
    r->indent = indent;
    r->text[0] = 0;
    if (text) lstrcpynW(r->text, text, ARRAYSIZE(r->text));
}

/* The letter an entry is filed under: its first letter, or "#". */
static void letter_of(const WCHAR *name, WCHAR *out)
{
    out[0] = name[0];
    out[1] = 0;
    if (!IsCharAlphaW(out[0])) out[0] = L'#';
    else CharUpperBuffW(out, 1);
}

static BOOL is_word_start(const WCHAR *s, const WCHAR *p)
{
    return p == s || wcschr(L" -_.(", p[-1]) != NULL;
}

/* How well A matches the search text (lower is better), or -1. */
static int match_score(const struct app *a, const WCHAR *text)
{
    WCHAR name[128], needle[256], folder[128];
    const WCHAR *p;
    int best = -1;

    lstrcpynW(name, a->name, ARRAYSIZE(name));
    lstrcpynW(needle, text, ARRAYSIZE(needle));
    lstrcpynW(folder, a->folder, ARRAYSIZE(folder));
    CharLowerW(name);
    CharLowerW(needle);
    CharLowerW(folder);
    for (p = name; (p = wcsstr(p, needle)); p++)
    {
        int s = p == name ? 0 : is_word_start(name, p) ? 1 : 2;
        if (best < 0 || s < best) best = s;
    }
    if (best < 0 && folder[0] && wcsstr(folder, needle)) best = 3;
    return best;
}

static void build_rows(void)
{
    int i, j, k, n;
    WCHAR last[2] = L"", letter[2];

    nrows = 0;
    if (!search_text[0])
    {
        /* all apps, A to Z: top-level apps and groups merged by name */
        static int order[MAX_GROUPS];
        BOOL others = FALSE;

        for (i = 0; i < ngroups; i++)
        {
            for (j = i; j > 0 && name_cmp(groups[order[j - 1]].name, groups[i].name) > 0; j--) order[j] = order[j - 1];
            order[j] = i;
        }
        for (i = 0, j = 0; i < napps || j < ngroups;)
        {
            const WCHAR *name;
            BOOL take_group;

            while (i < napps && (apps[i].group >= 0 || apps[i].kind == APP_OTHER))
            {
                if (apps[i].kind == APP_OTHER) others = TRUE;
                i++;
            }
            if (i >= napps && j >= ngroups) break;
            take_group = j < ngroups && (i >= napps || name_cmp(groups[order[j]].name, apps[i].name) < 0);
            name = take_group ? groups[order[j]].name : apps[i].name;
            letter_of(name, letter);
            if (wcscmp(letter, last))
            {
                add_row(ROW_LETTER, -1, 0, letter);
                wcscpy(last, letter);
            }
            if (take_group)
            {
                int g = order[j++];
                add_row(ROW_GROUP, g, 0, NULL);
                if (groups[g].expanded)
                    for (k = 0; k < napps; k++)
                        if (apps[k].group == g) add_row(ROW_APP, k, 1, NULL);
            }
            else add_row(ROW_APP, i++, 0, NULL);
        }
        if (others)
        {
            add_row(ROW_SECTION, -1, 0, L"Other apps");
            for (i = 0; i < napps; i++)
                if (apps[i].kind == APP_OTHER) add_row(ROW_APP, i, 0, NULL);
        }
        return;
    }

    /* search: best matches first, A to Z within the same score */
    for (n = 0, k = 0; k <= 3; k++)
        for (i = 0; i < napps; i++)
            if (match_score(&apps[i], search_text) == k)
            {
                if (!n) add_row(ROW_SECTION, -1, 0, L"Best match");
                else if (n == 1) add_row(ROW_SECTION, -1, 0, L"Apps");
                add_row(ROW_APP, i, 0, NULL);
                n++;
            }
    if (!n) add_row(ROW_HINT, -1, 0, NULL);
}

/* --- selection --- */

static BOOL row_selectable(int i)
{
    return i >= 0 && i < nrows && (rows[i].type == ROW_APP || rows[i].type == ROW_GROUP);
}

static struct app *selected_app(void)
{
    if (sel_kind == SEL_TILE && sel_index >= 0 && sel_index < npins) return &apps[pins[sel_index]];
    if (sel_kind == SEL_ROW && row_selectable(sel_index) && rows[sel_index].type == ROW_APP)
        return &apps[rows[sel_index].index];
    return NULL;
}

static struct group *selected_group(void)
{
    if (sel_kind == SEL_ROW && row_selectable(sel_index) && rows[sel_index].type == ROW_GROUP)
        return &groups[rows[sel_index].index];
    return NULL;
}

static void select_item(int kind, int index)
{
    sel_kind = kind;
    sel_index = index;
    if (kind == SEL_ROW) ensure_visible(index);
    if (launcher) InvalidateRect(launcher, NULL, FALSE);
}

static int first_row(int from, int dir)
{
    int i;
    for (i = from; i >= 0 && i < nrows; i += dir)
        if (row_selectable(i)) return i;
    return -1;
}

struct saved_sel { int kind; WCHAR key[MAX_PATH], group[128]; };

static void save_selection(struct saved_sel *s)
{
    struct app *a = selected_app();
    struct group *g = selected_group();
    s->kind = sel_kind;
    lstrcpynW(s->key, a ? a->key : L"", ARRAYSIZE(s->key));
    lstrcpynW(s->group, g ? g->name : L"", ARRAYSIZE(s->group));
}

/* Rebuild the rows, keeping the entry in S selected if it's still there;
 * with a search, the best match is selected (Enter runs it). */
static void rebuild_rows(const struct saved_sel *s)
{
    const WCHAR *key = s ? s->key : L"", *gname = s ? s->group : L"";
    int kind = s ? s->kind : SEL_NONE, i;

    build_rows();
    sel_kind = SEL_NONE;
    sel_index = -1;
    if (key[0] && kind == SEL_TILE)
    {
        for (i = 0; i < npins; i++)
            if (!lstrcmpiW(apps[pins[i]].key, key)) select_item(SEL_TILE, i);
    }
    else if (key[0] || gname[0])
    {
        for (i = 0; i < nrows; i++)
            if ((rows[i].type == ROW_APP && key[0] && !lstrcmpiW(apps[rows[i].index].key, key)) ||
                (rows[i].type == ROW_GROUP && gname[0] && !lstrcmpiW(groups[rows[i].index].name, gname)))
            {
                select_item(SEL_ROW, i);
                break;
            }
    }
    if (sel_kind == SEL_NONE && search_text[0] && (i = first_row(0, 1)) >= 0) select_item(SEL_ROW, i);
    clamp_scroll();
    if (launcher) InvalidateRect(launcher, NULL, FALSE);
}

static void refresh_rows(BOOL keep)
{
    struct saved_sel s;
    save_selection(&s);
    rebuild_rows(keep ? &s : NULL);
}

/* Rescan, e.g. after an installer added or removed shortcuts. */
static void reload_apps(void)
{
    struct saved_sel s;
    save_selection(&s);
    load_settings();
    layout_tasks();
    InvalidateRect(bar, NULL, FALSE);
    load_apps();
    rebuild_rows(&s);
    write_state();
}

static void toggle_group(int g, int expand)
{
    if (g < 0 || g >= ngroups) return;
    groups[g].expanded = expand < 0 ? !groups[g].expanded : expand;
    refresh_rows(TRUE);
    if (groups[g].expanded)
    {
        /* scroll the opened group's apps into view, keeping the group row */
        int i, first = -1, last = -1;
        for (i = 0; i < nrows; i++)
        {
            if (rows[i].type == ROW_GROUP && rows[i].index == g) first = last = i;
            else if (first >= 0 && rows[i].indent) last = i;
            else if (first >= 0) break;
        }
        ensure_visible(last);
        ensure_visible(first);
        InvalidateRect(launcher, NULL, FALSE);
    }
    write_state();
}

static void move_rows(int delta)
{
    int r = sel_kind == SEL_ROW ? sel_index : -1, dir = delta > 0 ? 1 : -1, steps = abs(delta), next;

    if (sel_kind != SEL_ROW)
    {
        if ((r = first_row(0, 1)) >= 0) select_item(SEL_ROW, r);
        return;
    }
    while (steps-- && (next = first_row(r + dir, dir)) >= 0) r = next;
    if (r == sel_index && dir < 0)
    {
        /* above the first row: the Back button, then the search box */
        if (!search_text[0]) select_item(SEL_HEADER, 0);
        else select_item(SEL_NONE, -1);
        return;
    }
    select_item(SEL_ROW, r);
}

/* Down from the search box: the pins (or the first app), and down the
 * grid. The "All apps" / "Back" button sits above the pins and the list. */
static void key_down(void)
{
    int r;
    kbd_sel = TRUE;
    switch (sel_kind)
    {
    case SEL_NONE:
        if (tiles_visible()) select_item(npins ? SEL_TILE : SEL_HEADER, 0);
        else move_rows(1);
        break;
    case SEL_HEADER:
        if (tiles_visible()) { if (npins) select_item(SEL_TILE, 0); }
        else if ((r = first_row(0, 1)) >= 0) select_item(SEL_ROW, r);
        break;
    case SEL_TILE:
        if (sel_index + tile_cols() < npins && sel_index / tile_cols() < PIN_ROWS - 1)
            select_item(SEL_TILE, sel_index + tile_cols());
        break;
    case SEL_ROW:
        move_rows(1);
        break;
    }
}

static void key_up(void)
{
    kbd_sel = TRUE;
    switch (sel_kind)
    {
    case SEL_TILE:
        if (sel_index >= tile_cols()) select_item(SEL_TILE, sel_index - tile_cols());
        else select_item(SEL_HEADER, 0);
        break;
    case SEL_HEADER:
    case SEL_USER:
    case SEL_POWER:
        select_item(SEL_NONE, -1);
        break;
    case SEL_ROW:
        move_rows(-1);
        break;
    }
}

/* Left/Right: move along the pins, or collapse/expand a group. Returns
 * FALSE to let the search box have the key. */
static BOOL key_side(int dir)
{
    kbd_sel = TRUE;
    if (sel_kind == SEL_TILE)
    {
        int i = sel_index + dir;
        if (i >= 0 && i < npins && i / tile_cols() < PIN_ROWS) select_item(SEL_TILE, i);
        return TRUE;
    }
    if (sel_kind == SEL_USER || sel_kind == SEL_POWER)
    {
        select_item(dir > 0 ? SEL_POWER : SEL_USER, 0);
        return TRUE;
    }
    if (sel_kind != SEL_ROW || !row_selectable(sel_index)) return FALSE;
    if (rows[sel_index].type == ROW_GROUP)
    {
        toggle_group(rows[sel_index].index, dir > 0);
        return TRUE;
    }
    if (dir < 0 && rows[sel_index].indent)
    {
        int i;
        for (i = sel_index; i >= 0 && rows[i].type != ROW_GROUP; i--) ;
        if (i >= 0)
        {
            select_item(SEL_ROW, i);
            toggle_group(rows[i].index, FALSE);
        }
        return TRUE;
    }
    return FALSE;
}

/* Tab moves between the search box, the list or pins, and the user and
 * power buttons at the bottom. */
static void key_tab(BOOL back)
{
    int r;
    kbd_sel = TRUE;
    if (!back)
    {
        if (sel_kind == SEL_USER) select_item(SEL_POWER, 0);
        else if (sel_kind == SEL_POWER) select_item(SEL_NONE, -1);
        else if (sel_kind == SEL_NONE && tiles_visible() && npins) select_item(SEL_TILE, 0);
        else if (sel_kind == SEL_NONE && list_visible() && (r = first_row(0, 1)) >= 0) select_item(SEL_ROW, r);
        else select_item(SEL_USER, 0);
    }
    else
    {
        if (sel_kind == SEL_POWER) select_item(SEL_USER, 0);
        else if (sel_kind == SEL_NONE) select_item(SEL_POWER, 0);
        else if (sel_kind == SEL_USER && tiles_visible() && npins) select_item(SEL_TILE, 0);
        else if (sel_kind == SEL_USER && list_visible() && (r = first_row(0, 1)) >= 0) select_item(SEL_ROW, r);
        else select_item(SEL_NONE, -1);
    }
}

/* --- hit testing --- */

static void hit_test(POINT pt, int *kind, int *index)
{
    int i, y;
    RECT r;

    *kind = SEL_NONE;
    *index = -1;
    if (!search_text[0] && PtInRect(&header_btn, pt)) { *kind = SEL_HEADER; *index = 0; return; }
    if (PtInRect(&user_btn, pt)) { *kind = SEL_USER; *index = 0; return; }
    if (PtInRect(&power_btn, pt)) { *kind = SEL_POWER; *index = 0; return; }
    for (i = 0; i < npins; i++)
    {
        tile_rect(i, &r);
        if (PtInRect(&r, pt)) { *kind = SEL_TILE; *index = i; return; }
    }
    panel_rect(&r);
    if (pt.y < list_top() || pt.y >= list_bottom() || pt.x < r.left || pt.x >= r.right) return;
    for (i = 0, y = row_y(0); i < nrows; y += row_height(i), i++)
        if (pt.y >= y && pt.y < y + row_height(i))
        {
            if (row_selectable(i)) { *kind = SEL_ROW; *index = i; }
            return;
        }
}

/* --- painting --- */

static RECT panel;                 /* the panel, in client coordinates */

/* behind the selected entry: the accent selection when it came from the
 * keyboard or the search (Enter starts it), else a hover */
static void paint_selection(struct setu_canvas *cv, const RECT *r)
{
    if (kbd_sel || search_text[0])
    {
        st_round(cv, r, S(ST_RADIUS_CONTROL), st_selection());
        st_frame(cv, r, S(ST_RADIUS_CONTROL), max(1, S(1)), ST->accent);
    }
    else st_round(cv, r, S(ST_RADIUS_CONTROL), ST->surface_hover);
}

static void paint_header_button(struct setu_canvas *cv)
{
    BOOL all = view == VIEW_ALL, sel = sel_kind == SEL_HEADER;
    const WCHAR *label = all ? L"Back" : L"All apps";
    RECT r = header_btn;
    int isz = S(16), tw = st_text_width(cv, label, SF_BODY);
    int x = (r.left + r.right - (tw + S(4) + isz)) / 2, cy = (r.top + r.bottom) / 2;

    st_round(cv, &r, S(ST_RADIUS_CONTROL), sel ? ST->surface_hover : ST->window_bg);
    st_frame(cv, &r, S(ST_RADIUS_CONTROL), 1, ST->border_subtle);
    if (sel && kbd_sel) st_focus_ring(cv, &r, S(ST_RADIUS_CONTROL), dpi);
    if (all)
    {
        st_icon(cv, ICON_CHEVRON_LEFT, x, cy - isz / 2, isz, ST->text);
        SetRect(&r, x + isz + S(4), r.top, r.right, r.bottom);
    }
    else
    {
        st_icon(cv, ICON_CHEVRON_RIGHT, x + tw + S(4), cy - isz / 2, isz, ST->text);
        SetRect(&r, x, r.top, x + tw + S(1), r.bottom);
    }
    st_text(cv, label, &r, SF_BODY, ST->text, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
}

static void paint_row(struct setu_canvas *cv, int i, int y)
{
    const struct row *rw = &rows[i];
    int h = row_height(i), isz = S(24);
    BOOL sel = sel_kind == SEL_ROW && sel_index == i;
    RECT r;

    SetRect(&r, panel.left + S(20) + rw->indent * S(24), y, panel.right - S(20), y + h);
    switch (rw->type)
    {
    case ROW_LETTER:
    {
        RECT line;
        int tw = st_text_width(cv, rw->text, SF_CAPTION);
        r.left += S(12);
        r.top += S(6);
        st_text(cv, rw->text, &r, SF_CAPTION, ST->accent, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
        SetRect(&line, r.left + tw + S(12), (r.top + r.bottom) / 2, r.right - S(12), (r.top + r.bottom) / 2 + max(1, S(1)));
        st_fill(cv, &line, ST->border_subtle);
        break;
    }
    case ROW_SECTION:
        r.left += S(12);
        r.bottom -= S(6);
        st_text(cv, rw->text, &r, SF_CAPTION, ST->text_secondary, DT_SINGLELINE | DT_BOTTOM | DT_LEFT);
        break;
    case ROW_HINT:
    {
        WCHAR msg[400];
        swprintf(msg, ARRAYSIZE(msg), L"No app matches \x201c%ls\x201d. Press Enter to run it as a command.",
                 search_text);
        r.left += S(12);
        r.right -= S(12);
        r.top += S(8);
        st_text(cv, msg, &r, SF_BODY, ST->text_secondary, DT_WORDBREAK | DT_LEFT);
        break;
    }
    case ROW_APP:
    {
        struct app *a = &apps[rw->index];
        if (sel) paint_selection(cv, &r);
        draw_app_icon(cv, a, r.left + S(12), y + (h - isz) / 2, isz);
        r.left += S(12) + isz + S(12);
        r.right -= S(12);
        if (search_text[0] && a->folder[0])
        {
            /* where it lives, on the right */
            st_text(cv, a->folder, &r, SF_BODY, ST->text_secondary, DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_END_ELLIPSIS);
            r.right -= min(S(140), (r.right - r.left) / 3);
        }
        if (a->pinned)
        {
            st_icon(cv, ICON_PIN, r.right - S(14), y + (h - S(14)) / 2, S(14), ST->text_secondary);
            r.right -= S(22);
        }
        st_text(cv, a->name, &r, SF_BODY, ST->text, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
        break;
    }
    case ROW_GROUP:
    {
        struct group *g = &groups[rw->index];
        WCHAR count[16];
        if (sel) paint_selection(cv, &r);
        draw_folder(cv, r.left + S(12), y + (h - isz) / 2, isz);
        st_icon(cv, g->expanded ? ICON_CHEVRON_UP : ICON_CHEVRON_DOWN, r.right - S(28), y + (h - S(16)) / 2, S(16),
                ST->text_secondary);
        r.left += S(12) + isz + S(12);
        r.right -= S(40);
        swprintf(count, ARRAYSIZE(count), L"%d", g->count);
        st_text(cv, count, &r, SF_BODY, ST->text_secondary, DT_SINGLELINE | DT_VCENTER | DT_RIGHT);
        r.right -= S(28);
        st_text(cv, g->name, &r, SF_BODY, ST->text, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
        break;
    }
    }
}

static void paint_launcher(struct setu_canvas *cv, const RECT *rc)
{
    int rad = S(ST_RADIUS_WINDOW), i, y, isz;
    RECT r;

    /* backdrop and shadow, then the panel */
    if (shadow_px && backdrop)
    {
        HDC m = CreateCompatibleDC(cv->dc);
        HGDIOBJ old = SelectObject(m, backdrop);
        BitBlt(cv->dc, 0, 0, rc->right, rc->bottom, m, 0, 0, SRCCOPY);
        SelectObject(m, old);
        DeleteDC(m);
        r = panel;
        OffsetRect(&r, 0, S(2));
        st_shadow(cv, &r, rad, shadow_px, 72);
    }
    else st_fill(cv, rc, ST->surface);
    st_round(cv, &panel, rad, ST->surface);

    /* search box: an edit field (the EDIT control sits inside it) */
    st_round(cv, &search_rc, S(ST_RADIUS_CONTROL), ST->surface);
    st_frame(cv, &search_rc, S(ST_RADIUS_CONTROL), 1, ST->border);
    if (GetFocus() == edit)
    {
        RECT line = { search_rc.left + S(1), search_rc.bottom - S(2), search_rc.right - S(1), search_rc.bottom };
        st_clip(cv, &line);
        st_round(cv, &search_rc, S(ST_RADIUS_CONTROL), ST->accent);
        st_clip(cv, NULL);
    }
    st_icon(cv, ICON_SEARCH, search_rc.left + S(12), (search_rc.top + search_rc.bottom - S(16)) / 2, S(16),
            ST->text_secondary);

    /* header: the view's title and the button that switches views */
    if (!search_text[0])
    {
        SetRect(&r, panel.left + S(32), header_btn.top, header_btn.left - S(8), header_btn.bottom);
        st_text(cv, view == VIEW_ALL ? L"All apps" : L"Pinned", &r, SF_TITLE, ST->text,
                DT_SINGLELINE | DT_VCENTER | DT_LEFT);
        paint_header_button(cv);
    }

    if (tiles_visible())
    {
        if (!npins)
        {
            SetRect(&r, panel.left + S(32), tiles_top(), panel.right - S(32), tiles_top() + S(40));
            st_text(cv, L"Right-click an app in All apps and choose Pin to keep it here.", &r, SF_BODY,
                    ST->text_secondary, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
        }
        isz = S(32);
        for (i = 0; i < npins; i++)
        {
            struct app *a = &apps[pins[i]];
            tile_rect(i, &r);
            if (r.left < 0) continue;
            InflateRect(&r, -S(2), 0);
            if (sel_kind == SEL_TILE && sel_index == i) paint_selection(cv, &r);
            draw_app_icon(cv, a, (r.left + r.right - isz) / 2, r.top + S(14), isz);
            r.top += S(14) + isz + S(8);
            r.bottom = r.top + S(20);
            InflateRect(&r, -S(4), 0);
            st_text(cv, a->name, &r, SF_BODY, ST->text, DT_SINGLELINE | DT_TOP | DT_CENTER | DT_END_ELLIPSIS);
        }
    }

    if (list_visible())
    {
        int top = list_top(), bottom = list_bottom(), content = content_height();
        SetRect(&r, panel.left, top, panel.right, bottom);
        st_clip(cv, &r);
        for (i = 0, y = row_y(0); i < nrows; y += row_height(i), i++)
        {
            if (y + row_height(i) < top || y > bottom) continue;
            paint_row(cv, i, y);
        }
        /* a thin scroll indicator when the list is longer than the view */
        if (content > bottom - top)
        {
            int track = bottom - top - S(8);
            int th = max(S(24), track * (bottom - top) / content);
            int ty = top + S(4) + (track - th) * scroll_px / max(1, content - (bottom - top));
            RECT thumb = { panel.right - S(9), ty, panel.right - S(5), ty + th };
            st_round_alpha(cv, &thumb, S(2), ST->text_secondary, 128);
        }
        st_clip(cv, NULL);
    }

    /* footer: the user, a status line, power */
    {
        int ft = footer_top();
        RECT f = { panel.left, ft, panel.right, panel.bottom };
        const struct app *a = selected_app();
        const struct group *g = selected_group();
        WCHAR text[300];
        UINT fmt = DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS;

        st_round(cv, &f, rad, ST->window_bg);
        f.bottom = ft + rad * 2;
        st_fill(cv, &f, ST->window_bg);
        f.bottom = ft + max(1, S(1));
        st_fill(cv, &f, ST->border_subtle);

        if (sel_kind == SEL_USER) paint_selection(cv, &user_btn);
        st_icon(cv, ICON_CIRCLE_USER_ROUND, user_btn.left + S(10), (user_btn.top + user_btn.bottom - S(24)) / 2, S(24),
                ST->text);
        SetRect(&r, user_btn.left + S(10) + S(24) + S(10), user_btn.top, user_btn.right - S(8), user_btn.bottom);
        st_text(cv, user_name, &r, SF_BODY, ST->text, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);

        if (sel_kind == SEL_POWER) paint_selection(cv, &power_btn);
        st_icon_in(cv, ICON_POWER, &power_btn, S(20), ST->text);

        if (status_text[0]) lstrcpynW(text, status_text, ARRAYSIZE(text));
        else if (a && a->desc[0]) lstrcpynW(text, a->desc, ARRAYSIZE(text));
        else if (a && a->file[0]) { lstrcpynW(text, a->file, ARRAYSIZE(text)); fmt = (fmt & ~DT_END_ELLIPSIS) | DT_PATH_ELLIPSIS; }
        else if (a) lstrcpynW(text, L"Installed program. Right-click to uninstall.", ARRAYSIZE(text));
        else if (g) swprintf(text, ARRAYSIZE(text), L"Folder with %d apps", g->count);
        else text[0] = 0;
        SetRect(&r, user_btn.right + S(16), ft, power_btn.left - S(16), panel.bottom);
        st_text(cv, text, &r, SF_BODY, ST->text_secondary, fmt);
    }

    st_frame(cv, &panel, rad, 1, ST->border);
}

/* --- actions --- */

static void show_view(int v)
{
    view = v;
    scroll_px = 0;
    if (v == VIEW_ALL)
    {
        int r = first_row(0, 1);
        if (kbd_sel && r >= 0) select_item(SEL_ROW, r);
        else select_item(SEL_NONE, -1);
    }
    else select_item(kbd_sel ? SEL_HEADER : SEL_NONE, 0);
    InvalidateRect(launcher, NULL, FALSE);
    write_state();
}

static void open_home(void)
{
    WCHAR home[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PROFILE, NULL, SHGFP_TYPE_CURRENT, home))) open_location(home, NULL);
}

/* Restart and Shut down ask the system (systemd) through Z:, the Linux
 * root; Sign out ends the session (ExitWindowsEx sends WM_ENDSESSION, and
 * the shell then exits with 0, which ends setu-session). */
#define SYSTEMCTL L"Z:\\usr\\bin\\systemctl"

static void power_command(int cmd)
{
    WCHAR line[MAX_PATH + 32];
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;

    if (cmd == IDM_SIGNOUT)
    {
        if (!ExitWindowsEx(EWX_LOGOFF, SHTDN_REASON_FLAG_PLANNED)) set_status(L"Could not sign out");
        return;
    }
    swprintf(line, ARRAYSIZE(line), L"\"%ls\" %ls", SYSTEMCTL, cmd == IDM_RESTART ? L"reboot" : L"poweroff");
    if (CreateProcessW(SYSTEMCTL, line, NULL, NULL, FALSE, DETACHED_PROCESS, NULL, NULL, &si, &pi))
    {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    else set_status(L"Could not %ls", cmd == IDM_RESTART ? L"restart" : L"shut down");
}

static void power_menu(POINT pt)
{
    HMENU m = CreatePopupMenu();
    BOOL sys = GetFileAttributesW(SYSTEMCTL) != INVALID_FILE_ATTRIBUTES;
    int cmd;

    AppendMenuW(m, MF_STRING, IDM_SIGNOUT, L"Sign &out");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING | (sys ? 0 : MF_GRAYED), IDM_RESTART, L"&Restart");
    AppendMenuW(m, MF_STRING | (sys ? 0 : MF_GRAYED), IDM_SHUTDOWN, L"Shut &down");
    in_menu = TRUE;
    write_state();
    cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTALIGN | TPM_BOTTOMALIGN | TPM_RIGHTBUTTON, pt.x, pt.y, 0,
                         launcher, NULL);
    in_menu = FALSE;
    DestroyMenu(m);
    if (IsWindowVisible(launcher)) SetFocus(edit);
    if (cmd) power_command(cmd);
    write_state();
}

static void activate_item(int kind, int index)
{
    WCHAR text[1024] = L"";

    if (kind == SEL_TILE && index >= 0 && index < npins) launch_app(&apps[pins[index]]);
    else if (kind == SEL_ROW && row_selectable(index))
    {
        if (rows[index].type == ROW_APP) launch_app(&apps[rows[index].index]);
        else toggle_group(rows[index].index, -1);
    }
    else if (kind == SEL_HEADER) show_view(view == VIEW_ALL ? VIEW_PINNED : VIEW_ALL);
    else if (kind == SEL_USER)
    {
        hide_launcher();
        open_home();
    }
    else if (kind == SEL_POWER)
    {
        POINT pt = { power_btn.right, power_btn.top };
        ClientToScreen(launcher, &pt);
        power_menu(pt);
    }
    else
    {
        GetWindowTextW(edit, text, ARRAYSIZE(text));
        if (text[0]) run_command(text);
    }
}

static void set_pinned(struct app *a, BOOL pin)
{
    if (!pin_app(a, pin)) set_status(pin ? L"The pinned grid is full" : L"Could not save the pins");
    else set_status(pin ? L"Pinned %ls" : L"Unpinned %ls", a->name);
    refresh_rows(FALSE);
    if (pin) InvalidateRect(launcher, NULL, FALSE);
    write_state();
}

/* The context menu of the selected entry, at PT (screen coordinates). */
static void item_menu(POINT pt)
{
    struct app *a = selected_app();
    struct group *g = selected_group();
    HMENU m;
    int cmd;

    if (!a && !g) return;
    m = CreatePopupMenu();
    if (a)
    {
        BOOL has_location = a->kind != APP_OTHER || a->location[0];
        AppendMenuW(m, MF_STRING | (a->file[0] ? 0 : MF_GRAYED), IDM_OPEN, L"&Open");
        SetMenuDefaultItem(m, IDM_OPEN, FALSE);
        AppendMenuW(m, MF_STRING, a->pinned ? IDM_UNPIN : IDM_PIN, a->pinned ? L"Un&pin" : L"&Pin");
        AppendMenuW(m, MF_SEPARATOR, 0, NULL);
        AppendMenuW(m, MF_STRING | (has_location ? 0 : MF_GRAYED), IDM_LOCATION, L"Open file &location");
        AppendMenuW(m, MF_STRING | (a->uninstall[0] ? 0 : MF_GRAYED), IDM_UNINSTALL, L"&Uninstall");
    }
    else
    {
        AppendMenuW(m, MF_STRING, IDM_EXPAND, g->expanded ? L"&Collapse" : L"&Expand");
        SetMenuDefaultItem(m, IDM_EXPAND, FALSE);
        AppendMenuW(m, MF_SEPARATOR, 0, NULL);
        AppendMenuW(m, MF_STRING, IDM_LOCATION, L"Open file &location");
        AppendMenuW(m, MF_STRING | (g->uninstall[0] ? 0 : MF_GRAYED), IDM_UNINSTALL, L"&Uninstall");
    }
    in_menu = TRUE;
    write_state();
    cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON,
                         pt.x, pt.y, 0, launcher, NULL);
    in_menu = FALSE;
    DestroyMenu(m);
    if (IsWindowVisible(launcher)) SetFocus(edit);

    switch (cmd)
    {
    case IDM_OPEN: launch_app(a); break;
    case IDM_PIN: set_pinned(a, TRUE); break;
    case IDM_UNPIN: set_pinned(a, FALSE); break;
    case IDM_EXPAND: toggle_group((int)(g - groups), -1); break;
    case IDM_LOCATION:
        if (a) open_app_location(a);
        else open_location(g->path, NULL);
        break;
    case IDM_UNINSTALL:
        run_uninstall(a ? a->name : g->name, a ? a->uninstall : g->uninstall);
        break;
    }
    write_state();
}

/* The context menu from the keyboard (the Menu key or Shift+F10), next
 * to the selected entry. */
static void item_menu_at_selection(void)
{
    RECT r;
    POINT pt;
    if (sel_kind == SEL_TILE) tile_rect(sel_index, &r);
    else if (sel_kind == SEL_ROW && row_selectable(sel_index))
        SetRect(&r, panel.left + S(40), row_y(sel_index), panel.left + S(200), row_y(sel_index) + row_height(sel_index));
    else return;
    pt.x = (r.left + r.right) / 2;
    pt.y = r.bottom;
    ClientToScreen(launcher, &pt);
    item_menu(pt);
}

static LRESULT CALLBACK edit_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_KEYDOWN)
    {
        BOOL shift = GetKeyState(VK_SHIFT) < 0;
        switch (wp)
        {
        case VK_DOWN: key_down(); write_state(); return 0;
        case VK_UP: key_up(); write_state(); return 0;
        case VK_NEXT: kbd_sel = TRUE; move_rows(8); write_state(); return 0;
        case VK_PRIOR: kbd_sel = TRUE; move_rows(-8); write_state(); return 0;
        case VK_TAB: key_tab(shift); write_state(); return 0;
        case VK_LEFT: case VK_RIGHT:
            if (key_side(wp == VK_RIGHT ? 1 : -1)) { write_state(); return 0; }
            break;
        case VK_RETURN: activate_item(sel_kind, sel_index); return 0;
        case VK_ESCAPE: hide_launcher(); return 0;
        case VK_APPS: item_menu_at_selection(); return 0;
        case VK_F10: if (shift) { item_menu_at_selection(); return 0; } break;
        }
    }
    if (msg == WM_SYSKEYDOWN && wp == VK_F10 && GetKeyState(VK_SHIFT) < 0)
    {
        item_menu_at_selection();
        return 0;
    }
    if (msg == WM_CHAR && (wp == L'\r' || wp == 27 || wp == L'\t')) return 0; /* no beep */
    if (msg == WM_PAINT)
    {
        /* our own placeholder text while the box is empty */
        LRESULT ret = CallWindowProcW(edit_orig_proc, hwnd, msg, wp, lp);
        if (!GetWindowTextLengthW(hwnd))
        {
            HDC dc = GetDC(hwnd);
            RECT r;
            HGDIOBJ of = SelectObject(dc, st_font[SF_BODY]);
            GetClientRect(hwnd, &r);
            r.left += S(2);
            HideCaret(hwnd);
            SetTextColor(dc, ST->text_secondary);
            SetBkMode(dc, TRANSPARENT);
            DrawTextW(dc, L"Search apps, or type a command", -1, &r, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX);
            SelectObject(dc, of);
            ShowCaret(hwnd);
            ReleaseDC(hwnd, dc);
        }
        return ret;
    }
    return CallWindowProcW(edit_orig_proc, hwnd, msg, wp, lp);
}

/* Place the window above the taskbar (centred with centred buttons) and
 * lay out the panel's parts. */
static void layout_launcher(void)
{
    RECT br, wa, mon, win;
    int w = S(600), h = S(660), m = shadow_px, x, eh;
    HDC dc;
    HGDIOBJ of;
    TEXTMETRICW tm;
    SIZE sz;

    GetWindowRect(bar, &br);
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    get_primary_monitor(&mon);
    if (wa.bottom > br.top) wa.bottom = br.top;
    h = min(h, (int)(wa.bottom - wa.top) - S(24));
    w = min(w, (int)(mon.right - mon.left) - S(24));
    x = bar_left_aligned ? br.left + S(12) : (mon.left + mon.right - w) / 2;
    /* the window: the panel and its margin, but never over the taskbar */
    SetRect(&win, x - m, br.top - S(12) - h - m, x + w + m, min(br.top, br.top - S(12) + m));
    SetWindowPos(launcher, HWND_TOPMOST, win.left, win.top, win.right - win.left, win.bottom - win.top, SWP_NOACTIVATE);
    SetRect(&panel, m, m, m + w, m + h);

    dc = GetDC(launcher);
    of = SelectObject(dc, st_font[SF_BODY]);
    GetTextMetricsW(dc, &tm);
    SetRect(&search_rc, panel.left + S(24), panel.top + S(24), panel.right - S(24), panel.top + S(24) + S(36));
    {
        int tw = 0;
        GetTextExtentPoint32W(dc, L"All apps", 8, &sz);
        tw = sz.cx;
        GetTextExtentPoint32W(dc, L"Back", 4, &sz);
        tw = max(tw, sz.cx);
        SetRect(&header_btn, panel.right - S(24) - (tw + S(16) + S(4) + S(24)), panel.top + S(80),
                panel.right - S(24), panel.top + S(80) + S(ST_CONTROL_H));
    }
    GetTextExtentPoint32W(dc, user_name, (int)wcslen(user_name), &sz);
    SetRect(&user_btn, panel.left + S(16), footer_top() + S(12),
            panel.left + S(16) + min(S(10) + S(24) + S(10) + sz.cx + S(14), w / 2), footer_top() + S(52));
    SetRect(&power_btn, panel.right - S(16) - S(40), footer_top() + S(12), panel.right - S(16), footer_top() + S(52));
    SelectObject(dc, of);
    ReleaseDC(launcher, dc);

    SendMessageW(edit, WM_SETFONT, (WPARAM)st_font[SF_BODY], FALSE);
    eh = tm.tmHeight + S(2);
    SetWindowPos(edit, NULL, search_rc.left + S(40), (search_rc.top + search_rc.bottom - eh) / 2,
                 search_rc.right - search_rc.left - S(40) - S(12), eh, SWP_NOZORDER);
}

/* Copy what's under the (still hidden) window, for the shadow. Without it,
 * the panel is shaped by a rounded window region instead. */
static void prepare_backdrop(void)
{
    RECT wr;
    HDC sdc, m;
    HGDIOBJ old;
    BOOL ok = FALSE;

    shadow_px = S(ST_SHADOW_BLUR);
    layout_launcher();
    GetWindowRect(launcher, &wr);
    if (backdrop) DeleteObject(backdrop);
    backdrop = NULL;
    if ((sdc = GetDC(NULL)))
    {
        m = CreateCompatibleDC(sdc);
        backdrop = CreateCompatibleBitmap(sdc, wr.right - wr.left, wr.bottom - wr.top);
        if (m && backdrop)
        {
            old = SelectObject(m, backdrop);
            ok = BitBlt(m, 0, 0, wr.right - wr.left, wr.bottom - wr.top, sdc, wr.left, wr.top, SRCCOPY);
            SelectObject(m, old);
        }
        if (m) DeleteDC(m);
        ReleaseDC(NULL, sdc);
    }
    if (ok)
    {
        SetWindowRgn(launcher, NULL, FALSE);
        return;
    }
    if (backdrop) DeleteObject(backdrop);
    backdrop = NULL;
    shadow_px = 0;
    layout_launcher();
    SetWindowRgn(launcher, CreateRoundRectRgn(0, 0, panel.right + 1, panel.bottom + 1,
                                              S(ST_RADIUS_WINDOW) * 2, S(ST_RADIUS_WINDOW) * 2), FALSE);
}

static void show_launcher(void)
{
    status_text[0] = 0;
    search_text[0] = 0;
    scroll_px = 0;
    view = VIEW_PINNED;
    kbd_sel = FALSE;
    SetWindowTextW(edit, L"");
    prepare_backdrop();
    refresh_rows(FALSE);
    ShowWindow(launcher, SW_SHOW);
    SetForegroundWindow(launcher);
    SetFocus(edit);
    InvalidateRect(bar, NULL, FALSE);
    write_state();
}

static void hide_launcher(void)
{
    if (!launcher || !IsWindowVisible(launcher)) return;
    ShowWindow(launcher, SW_HIDE);
    launcher_hidden_at = GetTickCount();
    InvalidateRect(bar, NULL, FALSE);
    write_state();
}

static void toggle_launcher(void)
{
    if (IsWindowVisible(launcher)) hide_launcher();
    else show_launcher();
}

static LRESULT CALLBACK launcher_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    static POINT last_mouse = { -1, -1 };

    switch (msg)
    {
    case WM_PAINT:
        st_paint_window(hwnd, paint_launcher);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_CTLCOLOREDIT:
        SetTextColor((HDC)wp, ST->text);
        SetBkColor((HDC)wp, ST->surface);
        return (LRESULT)field_brush;
    case WM_COMMAND:
        if (LOWORD(wp) == ID_EDIT && HIWORD(wp) == EN_CHANGE)
        {
            GetWindowTextW(edit, search_text, ARRAYSIZE(search_text));
            status_text[0] = 0;
            scroll_px = 0;
            refresh_rows(FALSE);
            write_state();
        }
        else if (LOWORD(wp) == ID_EDIT && (HIWORD(wp) == EN_SETFOCUS || HIWORD(wp) == EN_KILLFOCUS))
            InvalidateRect(hwnd, &search_rc, FALSE);
        return 0;
    case WM_MOUSEMOVE:
    {
        POINT pt = { (short)LOWORD(lp), (short)HIWORD(lp) };
        int kind, index;
        /* only real moves select: windows appearing under a still pointer
         * get a WM_MOUSEMOVE too */
        if (pt.x == last_mouse.x && pt.y == last_mouse.y) return 0;
        last_mouse = pt;
        hit_test(pt, &kind, &index);
        if (kind != SEL_NONE && (kind != sel_kind || index != sel_index || kbd_sel))
        {
            sel_kind = kind;
            sel_index = index;
            kbd_sel = FALSE;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }
    case WM_LBUTTONUP:
    {
        POINT pt = { (short)LOWORD(lp), (short)HIWORD(lp) };
        int kind, index;
        if (!PtInRect(&panel, pt))
        {
            /* the shadow margin counts as outside the menu */
            hide_launcher();
            return 0;
        }
        hit_test(pt, &kind, &index);
        if (kind != SEL_NONE)
        {
            sel_kind = kind;
            sel_index = index;
            kbd_sel = FALSE;
            activate_item(kind, index);
        }
        else SetFocus(edit);
        return 0;
    }
    case WM_RBUTTONUP:
    {
        POINT pt = { (short)LOWORD(lp), (short)HIWORD(lp) };
        int kind, index;
        hit_test(pt, &kind, &index);
        if (kind != SEL_TILE && kind != SEL_ROW) return 0;
        kbd_sel = FALSE;
        select_item(kind, index);
        UpdateWindow(hwnd);
        ClientToScreen(hwnd, &pt);
        item_menu(pt);
        return 0;
    }
    case WM_MOUSEWHEEL:
        if (!list_visible()) return 0;
        scroll_px -= GET_WHEEL_DELTA_WPARAM(wp) * S(40) * 3 / WHEEL_DELTA;
        clamp_scroll();
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE) { if (!in_menu) hide_launcher(); }
        else SetFocus(edit);
        return 0;
    case WM_CLOSE:
        hide_launcher();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}


/* ------------------------------------------------------------------ */
/* Commands (command line and WM_COPYDATA)                              */

static int exit_after_sec;

BOOL apply_commands(int argc, WCHAR **argv)
{
    BOOL ok = TRUE;
    int i;

    for (i = 0; i < argc; i++)
    {
        const WCHAR *a = argv[i];
        const WCHAR *v = i + 1 < argc ? argv[i + 1] : NULL;

        if (!wcscmp(a, L"--open-launcher")) show_launcher();
        else if (!wcscmp(a, L"--close-launcher")) hide_launcher();
        else if (!wcscmp(a, L"--toggle-launcher")) toggle_launcher();
        else if (!wcscmp(a, L"--quit")) DestroyWindow(bar);
        else if (!wcscmp(a, L"--reload")) reload_apps();
        else if (v && !wcscmp(a, L"--search"))
        {
            if (!IsWindowVisible(launcher)) show_launcher();
            SetWindowTextW(edit, v); /* EN_CHANGE filters */
            SendMessageW(edit, EM_SETSEL, wcslen(v), wcslen(v));
            i++;
        }
        else if (v && (!wcscmp(a, L"--pin") || !wcscmp(a, L"--unpin")))
        {
            struct app *app = find_app(v);
            if (app) set_pinned(app, !wcscmp(a, L"--pin"));
            else { set_status(L"No app called \"%ls\"", v); ok = FALSE; }
            i++;
        }
        else if (v && !wcscmp(a, L"--state-file"))
        {
            GetFullPathNameW(v, ARRAYSIZE(state_path), state_path, NULL);
            write_state();
            i++;
        }
        else if (v && !wcscmp(a, L"--launch"))
        {
            struct app *app = find_app(v);
            if (app) ok = launch_app(app) && ok;
            else { set_status(L"No app called \"%ls\"", v); ok = FALSE; }
            i++;
        }
        else if (v && !wcscmp(a, L"--run")) { ok = run_command(v) && ok; i++; }
        else if (v && !wcscmp(a, L"--exit-after")) { exit_after_sec = _wtoi(v); i++; }
        else { set_status(L"Unknown option %ls", a); ok = FALSE; }
    }
    if (exit_after_sec > 0 && bar) SetTimer(bar, TIMER_EXIT, exit_after_sec * 1000, NULL);
    write_state();
    return ok;
}

/* Hand our arguments to an already-running shell. Returns -1 if none. */
static int forward_to_running_shell(int argc, WCHAR **argv)
{
    HWND other = FindWindowW(BAR_CLASS, NULL);
    COPYDATASTRUCT cds;
    WCHAR *buf;
    size_t len = 0;
    int i;
    DWORD_PTR result = 0;

    if (!other) return -1;
    for (i = 1; i < argc; i++) len += wcslen(argv[i]) + 1;
    buf = calloc(len + 1, sizeof(WCHAR));
    if (!buf) return 1;
    for (i = 1; i < argc; i++)
    {
        if (i > 1) buf[wcslen(buf)] = CMD_SEP;
        wcscat(buf, argv[i]);
    }
    cds.dwData = COPYDATA_MAGIC;
    cds.cbData = (DWORD)(wcslen(buf) * sizeof(WCHAR));
    cds.lpData = buf;
    AllowSetForegroundWindow(ASFW_ANY);
    if (!SendMessageTimeoutW(other, WM_COPYDATA, 0, (LPARAM)&cds, SMTO_ABORTIFHUNG, 15000, &result))
        result = 0;
    free(buf);
    return result ? 0 : 2;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmdline, int show)
{
    WNDCLASSEXW wc;
    WCHAR **argv;
    int argc, fwd;
    MSG msg;

    (void)prev; (void)cmdline; (void)show;
    hinst = inst;
    argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    set_dpi_awareness();

    if (argc > 1 && (fwd = forward_to_running_shell(argc, argv)) >= 0) return fwd;
    if (FindWindowW(BAR_CLASS, NULL)) return 0; /* already running */

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    dpi = get_window_dpi(NULL);
    make_fonts();
    field_brush = CreateSolidBrush(ST->surface);
    load_settings();
    {
        DWORD n = ARRAYSIZE(user_name);
        if (!GetUserNameW(user_name, &n) || !user_name[0]) lstrcpyW(user_name, L"User");
    }

    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpfnWndProc = bar_proc;
    wc.lpszClassName = BAR_CLASS;
    RegisterClassExW(&wc);
    wc.lpfnWndProc = launcher_proc;
    wc.lpszClassName = LAUNCHER_CLASS;
    RegisterClassExW(&wc);

    bar = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, BAR_CLASS, L"Setu taskbar",
                          WS_POPUP | WS_CLIPCHILDREN, 0, 0, 100, bar_height(), NULL, NULL, inst, NULL);
    if (!bar) return 1;
    dpi = get_window_dpi(bar);
    make_fonts();
    {
        INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_WIN95_CLASSES };
        InitCommonControlsEx(&icc);
        tips = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, NULL, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
                               CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, bar, NULL, inst, NULL);
        if (tips) SendMessageW(tips, WM_SETFONT, (WPARAM)st_font[SF_BODY], FALSE);
    }

    launcher = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, LAUNCHER_CLASS, L"Setu launcher",
                               WS_POPUP | WS_CLIPCHILDREN, 0, 0, S(440), S(620), bar, NULL, inst, NULL);
    edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 10, 10,
                           launcher, (HMENU)(INT_PTR)ID_EDIT, inst, NULL);
    edit_orig_proc = (WNDPROC)SetWindowLongPtrW(edit, GWLP_WNDPROC, (LONG_PTR)edit_proc);

    {
        APPBARDATA abd = { sizeof(abd) };
        abd.hWnd = bar;
        abd.uCallbackMessage = WM_APP_APPBAR;
        appbar_ok = (BOOL)SHAppBarMessage(ABM_NEW, &abd);
    }
    {
        /* With a taskbar, minimized windows live on their buttons: ask
         * for them to be moved off screen (ARW_HIDE) for this session. */
        MINIMIZEDMETRICS mm = { sizeof(mm) };
        if (SystemParametersInfoW(SPI_GETMINIMIZEDMETRICS, sizeof(mm), &mm, 0) && !(mm.iArrange & ARW_HIDE))
        {
            mm.iArrange |= ARW_HIDE;
            SystemParametersInfoW(SPI_SETMINIMIZEDMETRICS, sizeof(mm), &mm, 0);
        }
    }
    shellhook_msg = RegisterWindowMessageW(L"SHELLHOOK");
    shellhook_ok = RegisterShellHookWindow(bar);

    load_pins();
    load_apps();
    build_rows();
    watch_stop = CreateEventW(NULL, TRUE, FALSE, NULL);
    CloseHandle(CreateThread(NULL, 0, watch_thread, NULL, 0, NULL));
    register_shnotify();
    /* Ctrl+Esc and the Windows key open the menu */
    hotkey_ok = RegisterHotKey(bar, HOTKEY_MENU, MOD_CONTROL | MOD_NOREPEAT, VK_ESCAPE);
    kbd_hook = SetWindowsHookExW(WH_KEYBOARD_LL, kbd_proc, inst, 0);
    kbd_hook_ok = kbd_hook != NULL;
    update_clock();
    dock_bar();
    ShowWindow(bar, SW_SHOWNOACTIVATE);
    UpdateWindow(bar);

    /* Shell hook messages make polling unnecessary where they work; keep a
     * slow poll anyway for title and icon changes. */
    SetTimer(bar, TIMER_REFRESH, shellhook_ok ? 2000 : 500, NULL);
    SetTimer(bar, TIMER_CLOCK, 1000, NULL);
    refresh_tasks();

    if (argc > 1) apply_commands(argc - 1, argv + 1);
    LocalFree(argv);

    while (GetMessageW(&msg, NULL, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    free_apps();
    CoUninitialize();
    return (int)msg.wParam;
}
