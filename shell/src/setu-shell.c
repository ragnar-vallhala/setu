/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * setu-shell: the Setu desktop shell (Milestone 1).
 *
 * A Win32 program built with mingw-w64 that runs on the Setu personality.
 * It uses only public, documented APIs (user32, gdi32, shell32, ole32,
 * kernel32). Main references (learn.microsoft.com):
 *   - "Using Application Desktop Toolbars" / SHAppBarMessage
 *   - "The Taskbar" > "Managing Taskbar Buttons" (which windows get a button)
 *   - "Shell Links" / IShellLinkW, IPersistFile
 *   - SHGetFolderPathW (CSIDL_PROGRAMS, CSIDL_COMMON_PROGRAMS)
 *   - ShellExecuteExW, SHDefExtractIconW, SHGetFileInfoW
 *   - RegisterShellHookWindow, SetProcessDpiAwarenessContext, GetDpiForWindow
 *   - WM_COPYDATA (used to forward commands to a running shell)
 *
 * Command line (also accepted by a second instance, which forwards them to
 * the running shell and exits):
 *   --open-launcher | --close-launcher | --toggle-launcher
 *   --launch NAME      start the launcher entry called NAME
 *   --run CMD          run CMD as if typed into the launcher's run box
 *   --state-file PATH  write the shell's state (for tests) to PATH
 *   --exit-after SEC   quit after SEC seconds (first instance only)
 *   --quit             quit the running shell
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
#include <objbase.h>
#include <stdio.h>
#include <wchar.h>

#define BAR_CLASS      L"SetuShellTaskbar"
#define LAUNCHER_CLASS L"SetuShellLauncher"
#define COPYDATA_MAGIC 0x55544553 /* "SETU" */
#define CMD_SEP        L'\x1f'

#define WM_APP_APPBAR  (WM_APP + 1)
#define TIMER_REFRESH  1
#define TIMER_CLOCK    2
#define TIMER_EXIT     3
#define ID_EDIT        100
#define IDM_CLOSE      200
#define IDM_MINIMIZE   201
#define IDM_RESTORE    202

/* Palette: an original, warm-on-slate scheme. */
#define C_BAR      RGB(0x1b, 0x20, 0x26)
#define C_EDGE     RGB(0x36, 0x40, 0x4c)
#define C_HOVER    RGB(0x2a, 0x32, 0x3c)
#define C_ACTIVE   RGB(0x34, 0x3e, 0x4b)
#define C_TEXT     RGB(0xe9, 0xed, 0xf1)
#define C_MUTED    RGB(0x95, 0xa2, 0xb0)
#define C_ACCENT   RGB(0xf2, 0xa9, 0x3b)
#define C_PANEL    RGB(0x21, 0x27, 0x2e)
#define C_FIELD    RGB(0x15, 0x19, 0x1e)
#define C_SELECT   RGB(0x3a, 0x33, 0x27)

/* ------------------------------------------------------------------ */
/* State                                                                */

enum app_kind { APP_BUILTIN, APP_SHORTCUT };

struct app
{
    enum app_kind kind;
    WCHAR name[128];
    WCHAR file[MAX_PATH];   /* what ShellExecuteExW opens */
    WCHAR args[1024];
    WCHAR dir[MAX_PATH];
    WCHAR lnk[MAX_PATH];    /* the .lnk itself (fallback target) */
    HICON icon;
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

#define MAX_APPS  512
#define MAX_TASKS 64
#define MAX_ROWS  (MAX_APPS + 4)

static struct app apps[MAX_APPS];
static int napps;
static struct task tasks[MAX_TASKS];
static int ntasks;

/* launcher rows: >= 0 app index, -1 "Built-in" header, -2 "Start Menu" header */
static int rows[MAX_ROWS];
static int nrows;
static int sel_row = -1;
static int scroll_px;

static HINSTANCE hinst;
static HWND bar, launcher, edit;
static WNDPROC edit_orig_proc;
static UINT dpi = 96;
static HFONT font_ui, font_bold, font_small, font_title;
static HBRUSH field_brush;
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
    NONCLIENTMETRICSW ncm = { sizeof(ncm) };
    LOGFONTW lf;

    if (font_ui) DeleteObject(font_ui);
    if (font_bold) DeleteObject(font_bold);
    if (font_small) DeleteObject(font_small);
    if (font_title) DeleteObject(font_title);

    if (!SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
    {
        memset(&ncm.lfMessageFont, 0, sizeof(ncm.lfMessageFont));
        ncm.lfMessageFont.lfCharSet = DEFAULT_CHARSET;
    }
    lf = ncm.lfMessageFont;
    lf.lfQuality = CLEARTYPE_QUALITY;
    lf.lfWeight = FW_NORMAL;
    lf.lfHeight = -MulDiv(10, dpi, 72);
    font_ui = CreateFontIndirectW(&lf);
    lf.lfHeight = -MulDiv(8, dpi, 72);
    font_small = CreateFontIndirectW(&lf);
    lf.lfWeight = FW_BOLD;
    lf.lfHeight = -MulDiv(10, dpi, 72);
    font_bold = CreateFontIndirectW(&lf);
    lf.lfHeight = -MulDiv(15, dpi, 72);
    font_title = CreateFontIndirectW(&lf);
}

static void fill_rect(HDC dc, const RECT *r, COLORREF c)
{
    HBRUSH b = CreateSolidBrush(c);
    FillRect(dc, r, b);
    DeleteObject(b);
}

static void fill_round(HDC dc, const RECT *r, int radius, COLORREF c)
{
    HBRUSH b = CreateSolidBrush(c);
    HPEN p = CreatePen(PS_SOLID, 1, c);
    HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, p);
    RoundRect(dc, r->left, r->top, r->right, r->bottom, radius, radius);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(b);
    DeleteObject(p);
}

static void draw_text(HDC dc, const WCHAR *s, RECT *r, HFONT f, COLORREF c, UINT fmt)
{
    HGDIOBJ of = SelectObject(dc, f);
    SetTextColor(dc, c);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, s, -1, r, fmt | DT_NOPREFIX);
    SelectObject(dc, of);
}

/* The Setu mark: a bridge (deck over an arch), drawn with GDI. */
static void draw_mark(HDC dc, int x, int y, int size, COLORREF c)
{
    int w = size, t = max(2, size / 9);
    HPEN pen = CreatePen(PS_SOLID, t, c);
    HGDIOBJ op = SelectObject(dc, pen), ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
    int deck = y + size * 3 / 10, base = y + size * 9 / 10;
    int r = w / 2 - t;

    MoveToEx(dc, x, deck, NULL);
    LineTo(dc, x + w, deck);
    Arc(dc, x + w / 2 - r, base - r, x + w / 2 + r, base + r,
        x + w / 2 + r, base, x + w / 2 - r, base);
    /* hangers between deck and arch */
    MoveToEx(dc, x + w / 2, deck, NULL);
    LineTo(dc, x + w / 2, base - r);
    MoveToEx(dc, x + t, deck, NULL);
    LineTo(dc, x + t, base);
    MoveToEx(dc, x + w - t, deck, NULL);
    LineTo(dc, x + w - t, base);
    SelectObject(dc, op);
    SelectObject(dc, ob);
    DeleteObject(pen);
}

static BOOL ends_with_i(const WCHAR *s, const WCHAR *suffix)
{
    size_t a = wcslen(s), b = wcslen(suffix);
    return a >= b && !lstrcmpiW(s + a - b, suffix);
}

static BOOL contains_i(const WCHAR *hay, const WCHAR *needle)
{
    WCHAR h[256], n[256];
    lstrcpynW(h, hay, ARRAYSIZE(h));
    lstrcpynW(n, needle, ARRAYSIZE(n));
    CharLowerW(h);
    CharLowerW(n);
    return wcsstr(h, n) != NULL;
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
    for (i = 0; i < napps; i++)
        EMIT(L"app=%ls|%ls|%ls\r\n", apps[i].kind == APP_BUILTIN ? L"builtin" : L"shortcut",
            apps[i].name, apps[i].file);
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
/* Apps: built-ins and Start Menu shortcuts                             */

static HICON icon_for(const WCHAR *file, const WCHAR *icon_file, int icon_index)
{
    HICON large = NULL, small = NULL;
    int size = S(20);
    SHFILEINFOW sfi;

    if (icon_file && icon_file[0])
    {
        WCHAR expanded[MAX_PATH];
        ExpandEnvironmentStringsW(icon_file, expanded, ARRAYSIZE(expanded));
        if (SHDefExtractIconW(expanded, icon_index, 0, &large, &small, MAKELONG(size, S(16))) == S_OK)
        {
            if (small) DestroyIcon(small);
            return large;
        }
    }
    /* programs carry their own icon: the first icon resource */
    if ((ends_with_i(file, L".exe") || ends_with_i(file, L".ico")) &&
        SHDefExtractIconW(file, 0, 0, &large, &small, MAKELONG(size, S(16))) == S_OK)
    {
        if (small) DestroyIcon(small);
        return large;
    }
    memset(&sfi, 0, sizeof(sfi));
    if (SHGetFileInfoW(file, 0, &sfi, sizeof(sfi), SHGFI_ICONLOCATION) && sfi.szDisplayName[0] &&
        SHDefExtractIconW(sfi.szDisplayName, sfi.iIcon, 0, &large, &small, MAKELONG(size, S(16))) == S_OK)
    {
        if (small) DestroyIcon(small);
        return large;
    }
    memset(&sfi, 0, sizeof(sfi));
    if (SHGetFileInfoW(file, 0, &sfi, sizeof(sfi), SHGFI_ICON)) return sfi.hIcon;
    return NULL;
}

static struct app *find_app(const WCHAR *name)
{
    int i;
    for (i = 0; i < napps; i++)
        if (!lstrcmpiW(apps[i].name, name)) return &apps[i];
    for (i = 0; i < napps; i++)
        if (!_wcsnicmp(apps[i].name, name, wcslen(name))) return &apps[i];
    return NULL;
}

static void add_builtin(const WCHAR *name, const WCHAR *const *exes, const WCHAR *args)
{
    WCHAR path[MAX_PATH], sysdir[MAX_PATH];
    struct app *a;

    if (napps >= MAX_APPS) return;
    GetSystemDirectoryW(sysdir, ARRAYSIZE(sysdir));
    for (; *exes; exes++)
        if (SearchPathW(sysdir, *exes, NULL, ARRAYSIZE(path), path, NULL) ||
            SearchPathW(NULL, *exes, NULL, ARRAYSIZE(path), path, NULL)) break;
    if (!*exes) return;
    a = &apps[napps++];
    memset(a, 0, sizeof(*a));
    a->kind = APP_BUILTIN;
    lstrcpynW(a->name, name, ARRAYSIZE(a->name));
    lstrcpynW(a->file, path, ARRAYSIZE(a->file));
    if (args) lstrcpynW(a->args, args, ARRAYSIZE(a->args));
    a->icon = icon_for(path, NULL, 0);
}

static void add_shortcut(const WCHAR *lnk_path)
{
    IShellLinkW *sl = NULL;
    IPersistFile *pf = NULL;
    struct app *a;
    WCHAR raw[MAX_PATH], icon_file[MAX_PATH];
    const WCHAR *base;
    int icon_index = 0, i;

    if (napps >= MAX_APPS) return;
    a = &apps[napps];
    memset(a, 0, sizeof(*a));
    a->kind = APP_SHORTCUT;
    lstrcpynW(a->lnk, lnk_path, ARRAYSIZE(a->lnk));

    base = wcsrchr(lnk_path, L'\\');
    lstrcpynW(a->name, base ? base + 1 : lnk_path, ARRAYSIZE(a->name));
    if (ends_with_i(a->name, L".lnk")) a->name[wcslen(a->name) - 4] = 0;

    /* skip duplicates (same name in the per-user and common folders) */
    for (i = 0; i < napps; i++)
        if (!lstrcmpiW(apps[i].name, a->name)) return;

    icon_file[0] = 0;
    if (SUCCEEDED(CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
                                   &IID_IShellLinkW, (void **)&sl)) &&
        SUCCEEDED(IShellLinkW_QueryInterface(sl, &IID_IPersistFile, (void **)&pf)) &&
        SUCCEEDED(IPersistFile_Load(pf, lnk_path, STGM_READ)))
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
        IShellLinkW_GetIconLocation(sl, icon_file, ARRAYSIZE(icon_file), &icon_index);
    }
    if (pf) IPersistFile_Release(pf);
    if (sl) IShellLinkW_Release(sl);

    /* A link without a file-system target (e.g. an advertised shortcut)
     * is opened through the .lnk itself. */
    if (!a->file[0]) lstrcpynW(a->file, lnk_path, ARRAYSIZE(a->file));
    a->icon = icon_for(a->file, icon_file, icon_index);
    if (!a->icon) a->icon = icon_for(lnk_path, NULL, 0);
    napps++;
}

static void scan_programs(const WCHAR *dir, int depth)
{
    WCHAR pattern[MAX_PATH], path[MAX_PATH];
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
            scan_programs(path, depth + 1);
        else if (ends_with_i(fd.cFileName, L".lnk"))
            add_shortcut(path);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

static int __cdecl cmp_apps(const void *a, const void *b)
{
    const struct app *x = a, *y = b;
    if (x->kind != y->kind) return x->kind - y->kind;
    if (x->kind == APP_BUILTIN) return 0;
    return CompareStringEx(LOCALE_NAME_USER_DEFAULT, NORM_IGNORECASE, x->name, -1, y->name, -1, NULL, NULL, 0) - 2;
}

static void free_apps(void)
{
    int i;
    for (i = 0; i < napps; i++)
        if (apps[i].icon) DestroyIcon(apps[i].icon);
    napps = 0;
}

static void load_apps(void)
{
    static const WCHAR *const notepad[] = { L"notepad.exe", NULL };
    static const WCHAR *const cmd[] = { L"cmd.exe", NULL };
    static const WCHAR *const files[] = { L"explorer.exe", L"winefile.exe", NULL };
    static const WCHAR *const regedit[] = { L"regedit.exe", NULL };
    static const int folders[] = { CSIDL_PROGRAMS, CSIDL_COMMON_PROGRAMS };
    WCHAR dir[MAX_PATH];
    int i, nbuiltin;

    free_apps();
    add_builtin(L"Notepad", notepad, NULL);
    add_builtin(L"Command Prompt", cmd, NULL);
    add_builtin(L"Files", files, NULL);
    add_builtin(L"Registry Editor", regedit, NULL);
    nbuiltin = napps;

    for (i = 0; i < (int)ARRAYSIZE(folders); i++)
        if (SUCCEEDED(SHGetFolderPathW(NULL, folders[i], NULL, SHGFP_TYPE_CURRENT, dir)))
            scan_programs(dir, 0);

    qsort(apps + nbuiltin, napps - nbuiltin, sizeof(apps[0]), cmp_apps);
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
    BOOL ok = shell_execute(a->file, a->args, a->dir);
    DWORD err = ok ? 0 : GetLastError();

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

static struct { WCHAR path[MAX_PATH]; HICON icon; } exe_icons[MAX_TASKS];
static int nexe_icons;

static HICON exe_icon(HWND hwnd)
{
    WCHAR path[MAX_PATH];
    DWORD pid = 0, len = ARRAYSIZE(path);
    HANDLE proc;
    HICON icon;
    int i;

    GetWindowThreadProcessId(hwnd, &pid);
    if (!(proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid))) return NULL;
    if (!QueryFullProcessImageNameW(proc, 0, path, &len)) path[0] = 0;
    CloseHandle(proc);
    if (!path[0]) return NULL;
    for (i = 0; i < nexe_icons; i++)
        if (!lstrcmpiW(exe_icons[i].path, path)) return exe_icons[i].icon;
    if (!(icon = icon_for(path, NULL, 0))) return NULL;
    if (nexe_icons == MAX_TASKS) return icon; /* cache full: leak one, rare */
    lstrcpynW(exe_icons[nexe_icons].path, path, MAX_PATH);
    exe_icons[nexe_icons++].icon = icon;
    return icon;
}

static HICON window_icon(HWND hwnd)
{
    static const WPARAM kinds[] = { ICON_SMALL2, ICON_SMALL, ICON_BIG };
    DWORD_PTR r;
    HICON icon;
    int i;

    for (i = 0; i < (int)ARRAYSIZE(kinds); i++)
    {
        r = 0;
        if (SendMessageTimeoutW(hwnd, WM_GETICON, kinds[i], 0, SMTO_ABORTIFHUNG, 50, &r) &&
            icon_usable((HICON)r))
            return (HICON)r;
    }
    if (icon_usable(icon = (HICON)GetClassLongPtrW(hwnd, GCLP_HICONSM))) return icon;
    if (icon_usable(icon = (HICON)GetClassLongPtrW(hwnd, GCLP_HICON))) return icon;
    if ((icon = exe_icon(hwnd))) return icon;
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

static int bar_height(void) { return S(44); }

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

static void layout_tasks(void)
{
    RECT rc;
    int h, x, avail, w, i;

    GetClientRect(bar, &rc);
    h = rc.bottom;
    SetRect(&start_rc, S(6), S(5), S(6) + S(96), h - S(5));
    SetRect(&tray_rc, rc.right - S(112), S(5), rc.right - S(6), h - S(5));
    x = start_rc.right + S(10);
    avail = tray_rc.left - S(10) - x;
    w = ntasks ? min(S(220), avail / ntasks - S(4)) : 0;
    if (w < S(36)) w = S(36);
    for (i = 0; i < ntasks; i++)
    {
        SetRect(&tasks[i].rc, x, S(5), x + w, h - S(5));
        x += w + S(4);
    }
}

static void paint_bar(HDC dc, const RECT *rc)
{
    RECT r;
    int i;

    fill_rect(dc, rc, C_BAR);
    r = *rc;
    r.bottom = r.top + max(1, S(1));
    fill_rect(dc, &r, C_EDGE);

    /* start button */
    if (hot == -1 || IsWindowVisible(launcher))
        fill_round(dc, &start_rc, S(10), IsWindowVisible(launcher) ? C_ACTIVE : C_HOVER);
    {
        int m = S(20);
        int y = (start_rc.top + start_rc.bottom - m) / 2;
        draw_mark(dc, start_rc.left + S(12), y, m, C_ACCENT);
        r = start_rc;
        r.left += S(12) + m + S(10);
        draw_text(dc, L"Setu", &r, font_bold, C_TEXT, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
    }

    /* separator */
    r.left = start_rc.right + S(4);
    r.right = r.left + max(1, S(1));
    r.top = S(12);
    r.bottom = rc->bottom - S(12);
    fill_rect(dc, &r, C_EDGE);

    /* task buttons */
    for (i = 0; i < ntasks; i++)
    {
        struct task *t = &tasks[i];
        RECT b = t->rc, pill;
        int isz = S(16);

        if (t->active) fill_round(dc, &b, S(10), C_ACTIVE);
        else if (hot == i) fill_round(dc, &b, S(10), C_HOVER);

        /* indicator pill on the top edge: long and warm when active */
        pill.top = b.top;
        pill.bottom = b.top + S(3);
        if (t->active) { pill.left = b.left + S(12); pill.right = b.right - S(12); }
        else { int c = (b.left + b.right) / 2; pill.left = c - S(6); pill.right = c + S(6); }
        fill_round(dc, &pill, S(3), t->active ? C_ACCENT : (t->minimized ? C_EDGE : C_MUTED));

        if (t->icon)
            DrawIconEx(dc, b.left + S(10), (b.top + b.bottom - isz) / 2, t->icon, isz, isz, 0, NULL, DI_NORMAL);
        r = b;
        r.left += S(10) + isz + S(8);
        r.right -= S(8);
        if (r.right > r.left)
            draw_text(dc, t->title, &r, font_ui, t->minimized ? C_MUTED : C_TEXT,
                      DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
    }

    /* tray: clock and date */
    r = tray_rc;
    r.bottom = (tray_rc.top + tray_rc.bottom) / 2 + S(1);
    draw_text(dc, clock_text, &r, font_bold, C_TEXT, DT_SINGLELINE | DT_BOTTOM | DT_RIGHT);
    r.top = r.bottom;
    r.bottom = tray_rc.bottom;
    draw_text(dc, date_text, &r, font_small, C_MUTED, DT_SINGLELINE | DT_TOP | DT_RIGHT);
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
    {
        PAINTSTRUCT ps;
        RECT rc;
        HDC dc = BeginPaint(hwnd, &ps), mem;
        HBITMAP bmp;
        HGDIOBJ old;

        GetClientRect(hwnd, &rc);
        mem = CreateCompatibleDC(dc);
        bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
        old = SelectObject(mem, bmp);
        paint_bar(mem, &rc);
        BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return 0;
    }
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
        free_apps();
        load_apps();
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
    case WM_DESTROY:
    {
        APPBARDATA abd = { sizeof(abd) };
        abd.hWnd = hwnd;
        if (appbar_ok) SHAppBarMessage(ABM_REMOVE, &abd);
        if (workarea_set) SystemParametersInfoW(SPI_SETWORKAREA, 0, &saved_workarea, SPIF_SENDCHANGE);
        if (shellhook_ok) DeregisterShellHookWindow(hwnd);
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ------------------------------------------------------------------ */
/* Launcher                                                             */

static int row_height(int row) { return rows[row] < 0 ? S(30) : S(38); }
static int list_top(void) { return S(112); }
static int list_bottom(void) { RECT rc; GetClientRect(launcher, &rc); return rc.bottom - S(40); }

static void filter_rows(void)
{
    WCHAR text[256] = L"";
    int i, kind;

    if (edit) GetWindowTextW(edit, text, ARRAYSIZE(text));
    nrows = 0;
    for (kind = APP_BUILTIN; kind <= APP_SHORTCUT; kind++)
    {
        int header = nrows, any = 0;
        if (!text[0]) rows[nrows++] = kind == APP_BUILTIN ? -1 : -2;
        for (i = 0; i < napps; i++)
            if ((int)apps[i].kind == kind && (!text[0] || contains_i(apps[i].name, text)))
            {
                rows[nrows++] = i;
                any = 1;
            }
        if (!any && !text[0]) nrows = header; /* drop empty section */
    }
    sel_row = -1;
    for (i = 0; i < nrows; i++)
        if (rows[i] >= 0) { sel_row = text[0] ? i : -1; break; }
    scroll_px = 0;
    if (launcher) InvalidateRect(launcher, NULL, FALSE);
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
    if (row < 0) return;
    y = row_y(row);
    h = row_height(row);
    if (y < list_top()) scroll_px -= list_top() - y;
    else if (y + h > list_bottom()) scroll_px += y + h - list_bottom();
    clamp_scroll();
}

static void move_selection(int delta)
{
    int r = sel_row;
    do
    {
        r += delta;
        if (r < 0 || r >= nrows) return;
    } while (rows[r] < 0);
    sel_row = r;
    ensure_visible(r);
    InvalidateRect(launcher, NULL, FALSE);
}

static int launcher_hit(POINT pt)
{
    int i, y;
    if (pt.y < list_top() || pt.y >= list_bottom()) return -1;
    for (i = 0, y = row_y(0); i < nrows; y += row_height(i), i++)
        if (pt.y >= y && pt.y < y + row_height(i)) return rows[i] >= 0 ? i : -1;
    return -1;
}

static void paint_launcher(HDC dc, const RECT *rc)
{
    RECT r, field;
    HRGN clip;
    int i, y;

    fill_rect(dc, rc, C_PANEL);
    /* a hairline border so the panel reads against any background */
    {
        HPEN p = CreatePen(PS_SOLID, 1, C_EDGE);
        HGDIOBJ op = SelectObject(dc, p), ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, 0, 0, rc->right, rc->bottom);
        SelectObject(dc, op);
        SelectObject(dc, ob);
        DeleteObject(p);
    }

    /* header */
    draw_mark(dc, S(20), S(20), S(26), C_ACCENT);
    SetRect(&r, S(58), S(14), rc->right - S(16), S(40));
    draw_text(dc, L"Setu", &r, font_title, C_TEXT, DT_SINGLELINE | DT_BOTTOM | DT_LEFT);
    SetRect(&r, S(58), S(40), rc->right - S(16), S(58));
    draw_text(dc, L"Type to filter, or enter a command to run", &r, font_small, C_MUTED,
              DT_SINGLELINE | DT_TOP | DT_LEFT | DT_END_ELLIPSIS);

    /* run / search field (the EDIT control sits inside it) */
    SetRect(&field, S(16), S(66), rc->right - S(16), S(98));
    fill_round(dc, &field, S(10), C_FIELD);

    /* list */
    clip = CreateRectRgn(0, list_top(), rc->right, list_bottom());
    SelectClipRgn(dc, clip);
    for (i = 0, y = row_y(0); i < nrows; y += row_height(i), i++)
    {
        int h = row_height(i);
        if (y + h < list_top() || y > list_bottom()) continue;
        SetRect(&r, S(12), y, rc->right - S(12), y + h);
        if (rows[i] < 0)
        {
            r.left += S(10);
            draw_text(dc, rows[i] == -1 ? L"BUILT-IN" : L"START MENU", &r, font_small, C_ACCENT,
                      DT_SINGLELINE | DT_BOTTOM | DT_LEFT);
            continue;
        }
        if (i == sel_row) fill_round(dc, &r, S(10), C_SELECT);
        {
            struct app *a = &apps[rows[i]];
            int isz = S(20);
            if (a->icon)
                DrawIconEx(dc, r.left + S(10), y + (h - isz) / 2, a->icon, isz, isz, 0, NULL, DI_NORMAL);
            r.left += S(10) + isz + S(12);
            draw_text(dc, a->name, &r, font_ui, C_TEXT, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
        }
    }
    if (!nrows)
    {
        SetRect(&r, S(28), list_top() + S(8), rc->right - S(16), list_top() + S(60));
        draw_text(dc, L"No matching app. Press Enter to run it as a command.", &r, font_ui, C_MUTED,
                  DT_WORDBREAK | DT_LEFT);
    }
    SelectClipRgn(dc, NULL);
    DeleteObject(clip);

    /* footer: status, or the selected entry's target */
    SetRect(&r, 0, list_bottom(), rc->right, list_bottom() + max(1, S(1)));
    fill_rect(dc, &r, C_EDGE);
    SetRect(&r, S(20), list_bottom(), rc->right - S(20), rc->bottom);
    if (status_text[0])
        draw_text(dc, status_text, &r, font_small, C_MUTED, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
    else if (sel_row >= 0 && rows[sel_row] >= 0)
        draw_text(dc, apps[rows[sel_row]].file, &r, font_small, C_MUTED,
                  DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_PATH_ELLIPSIS);
    else
        draw_text(dc, L"Enter  open      Esc  close", &r, font_small, C_MUTED,
                  DT_SINGLELINE | DT_VCENTER | DT_LEFT);
}

static void activate_selection(void)
{
    WCHAR text[1024] = L"";
    GetWindowTextW(edit, text, ARRAYSIZE(text));
    if (sel_row >= 0 && rows[sel_row] >= 0) launch_app(&apps[rows[sel_row]]);
    else if (text[0]) run_command(text);
}

static LRESULT CALLBACK edit_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_KEYDOWN)
    {
        switch (wp)
        {
        case VK_DOWN: move_selection(1); return 0;
        case VK_UP: move_selection(-1); return 0;
        case VK_RETURN: activate_selection(); return 0;
        case VK_ESCAPE: hide_launcher(); return 0;
        }
    }
    if (msg == WM_CHAR && (wp == L'\r' || wp == 27)) return 0; /* no beep */
    return CallWindowProcW(edit_orig_proc, hwnd, msg, wp, lp);
}

static void layout_launcher(void)
{
    RECT br, rc;
    int w = S(400), h = S(540);

    GetWindowRect(bar, &br);
    SetWindowPos(launcher, HWND_TOPMOST, br.left + S(6), br.top - S(8) - h, w, h, SWP_NOACTIVATE);
    GetClientRect(launcher, &rc);
    SetWindowPos(edit, NULL, S(28), S(66) + (S(32) - S(20)) / 2, rc.right - S(56), S(20), SWP_NOZORDER);
    SendMessageW(edit, WM_SETFONT, (WPARAM)font_ui, TRUE);
}

static void show_launcher(void)
{
    status_text[0] = 0;
    SetWindowTextW(edit, L"");
    filter_rows();
    layout_launcher();
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

static LRESULT CALLBACK launcher_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        RECT rc;
        HDC dc = BeginPaint(hwnd, &ps), mem;
        HBITMAP bmp;
        HGDIOBJ old;

        GetClientRect(hwnd, &rc);
        mem = CreateCompatibleDC(dc);
        bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
        old = SelectObject(mem, bmp);
        paint_launcher(mem, &rc);
        BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_CTLCOLOREDIT:
        SetTextColor((HDC)wp, C_TEXT);
        SetBkColor((HDC)wp, C_FIELD);
        return (LRESULT)field_brush;
    case WM_COMMAND:
        if (LOWORD(wp) == ID_EDIT && HIWORD(wp) == EN_CHANGE)
        {
            status_text[0] = 0;
            filter_rows();
        }
        return 0;
    case WM_MOUSEMOVE:
    {
        POINT pt = { (short)LOWORD(lp), (short)HIWORD(lp) };
        int r = launcher_hit(pt);
        if (r >= 0 && r != sel_row) { sel_row = r; InvalidateRect(hwnd, NULL, FALSE); }
        return 0;
    }
    case WM_LBUTTONUP:
    {
        POINT pt = { (short)LOWORD(lp), (short)HIWORD(lp) };
        int r = launcher_hit(pt);
        if (r >= 0) { sel_row = r; launch_app(&apps[rows[r]]); }
        else SetFocus(edit);
        return 0;
    }
    case WM_MOUSEWHEEL:
        scroll_px -= GET_WHEEL_DELTA_WPARAM(wp) * S(38) * 3 / WHEEL_DELTA;
        clamp_scroll();
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE) hide_launcher();
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
        else if (!wcscmp(a, L"--toggle-launcher"))
            IsWindowVisible(launcher) ? hide_launcher() : show_launcher();
        else if (!wcscmp(a, L"--quit")) DestroyWindow(bar);
        else if (!wcscmp(a, L"--reload")) { load_apps(); filter_rows(); write_state(); }
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
    field_brush = CreateSolidBrush(C_FIELD);

    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpfnWndProc = bar_proc;
    wc.lpszClassName = BAR_CLASS;
    RegisterClassExW(&wc);
    wc.lpfnWndProc = launcher_proc;
    wc.lpszClassName = LAUNCHER_CLASS;
    wc.style = CS_DROPSHADOW;
    RegisterClassExW(&wc);

    bar = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, BAR_CLASS, L"Setu taskbar",
                          WS_POPUP | WS_CLIPCHILDREN, 0, 0, 100, bar_height(), NULL, NULL, inst, NULL);
    if (!bar) return 1;
    dpi = get_window_dpi(bar);
    make_fonts();

    launcher = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, LAUNCHER_CLASS, L"Setu launcher",
                               WS_POPUP | WS_CLIPCHILDREN, 0, 0, S(400), S(540), bar, NULL, inst, NULL);
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

    load_apps();
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
