/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * setu-xproc: two-process checks of the personality behaviour the shell
 * relies on. Each check prints "ok: ..." or "FAIL: ..."; the exit code is
 * the number of failures. Run it next to a running setu-shell.exe (the
 * work area checks expect the taskbar to have reserved its space).
 *
 *   setu-xproc workarea    this process sees the work area another process
 *                          (the shell) reserved; maximize respects it;
 *                          minimize honours ARW_HIDE
 *   setu-xproc shellhook   RegisterShellHookWindow gets HSHELL_WINDOWCREATED,
 *                          HSHELL_WINDOWACTIVATED and HSHELL_WINDOWDESTROYED
 *                          for a window of a child process
 *   setu-xproc icons       class and WM_GETICON icons of a child process's
 *                          window are usable here (GetIconInfo, DrawIconEx)
 *
 * Docs:
 *   SystemParametersInfo (SPI_SETWORKAREA / SPI_GETWORKAREA),
 *   "Using Application Desktop Toolbars" (ABM_SETPOS reserves the space),
 *   MINIMIZEDMETRICS (ARW_HIDE), RegisterShellHookWindow,
 *   "User Objects" (icons are public: any process can use the handle),
 *   GetClassLongPtrW (GCLP_HICON, GCLP_HICONSM), WM_GETICON.
 */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <stdio.h>
#include <wchar.h>

static int failures;

#define CHECK(cond, ...) do { \
    if (cond) { wprintf(L"ok: "); wprintf(__VA_ARGS__); wprintf(L"\n"); } \
    else { wprintf(L"FAIL: "); wprintf(__VA_ARGS__); wprintf(L"\n"); failures++; } \
    fflush(stdout); } while (0)

static LRESULT CALLBACK plain_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void pump(DWORD ms)
{
    DWORD end = GetTickCount() + ms;
    MSG msg;
    do
    {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        MsgWaitForMultipleObjects(0, NULL, FALSE, 20, QS_ALLINPUT);
    } while ((LONG)(end - GetTickCount()) > 0);
}

static HWND make_window(const WCHAR *cls, const WCHAR *title, HICON icon, HICON icon_sm)
{
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = plain_proc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = cls;
    wc.hIcon = icon;
    wc.hIconSm = icon_sm;
    RegisterClassExW(&wc);
    return CreateWindowExW(0, cls, title, WS_OVERLAPPEDWINDOW, 100, 100, 400, 300,
                           NULL, NULL, wc.hInstance, NULL);
}

static PROCESS_INFORMATION spawn_self(const WCHAR *args)
{
    WCHAR exe[MAX_PATH], cmd[MAX_PATH * 2];
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = { 0 };
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    _snwprintf(cmd, ARRAYSIZE(cmd), L"\"%ls\" %ls", exe, args);
    if (!CreateProcessW(exe, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
        wprintf(L"CreateProcess failed: %lu\n", GetLastError());
    return pi;
}

static HWND wait_window(const WCHAR *title, DWORD ms)
{
    DWORD end = GetTickCount() + ms;
    HWND hwnd;
    while (!(hwnd = FindWindowW(NULL, title)) && (LONG)(end - GetTickCount()) > 0) pump(50);
    return hwnd;
}

/* ---- workarea ------------------------------------------------------------ */

static void check_workarea(void)
{
    POINT origin = { 0, 0 };
    MONITORINFO mi = { sizeof(mi) };
    RECT wa, rc, client;
    WINDOWPLACEMENT wp = { sizeof(wp) };
    MINIMIZEDMETRICS mm = { sizeof(mm) };
    HWND hwnd;

    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    GetMonitorInfoW(MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY), &mi);
    wprintf(L"monitor=%ld,%ld,%ld,%ld work=%ld,%ld,%ld,%ld\n", mi.rcMonitor.left, mi.rcMonitor.top,
            mi.rcMonitor.right, mi.rcMonitor.bottom, wa.left, wa.top, wa.right, wa.bottom);
    CHECK(EqualRect(&wa, &mi.rcWork), L"SPI_GETWORKAREA equals the primary monitor's rcWork");
    CHECK(wa.bottom < mi.rcMonitor.bottom, L"work area excludes the taskbar reserved by another process (bottom %ld < %ld)",
          wa.bottom, mi.rcMonitor.bottom);

    hwnd = make_window(L"SetuXprocMax", L"setu-xproc maximize", NULL, NULL);
    ShowWindow(hwnd, SW_MAXIMIZE);
    pump(300);
    GetWindowRect(hwnd, &rc);
    GetClientRect(hwnd, &client);
    MapWindowPoints(hwnd, NULL, (POINT *)&client, 2);
    wprintf(L"maximized window=%ld,%ld,%ld,%ld client=%ld,%ld,%ld,%ld\n", rc.left, rc.top, rc.right,
            rc.bottom, client.left, client.top, client.right, client.bottom);
    CHECK(client.bottom <= wa.bottom && client.top >= wa.top, L"maximized window's client area is inside the work area");
    CHECK(rc.bottom - wa.bottom <= 16, L"maximized window does not cover the taskbar (window bottom %ld, work bottom %ld)",
          rc.bottom, wa.bottom);

    SystemParametersInfoW(SPI_GETMINIMIZEDMETRICS, sizeof(mm), &mm, 0);
    ShowWindow(hwnd, SW_MINIMIZE);
    pump(300);
    GetWindowRect(hwnd, &rc);
    GetWindowPlacement(hwnd, &wp);
    wprintf(L"iArrange=%#x minimized window=%ld,%ld,%ld,%ld ptMinPosition=%ld,%ld\n", mm.iArrange,
            rc.left, rc.top, rc.right, rc.bottom, wp.ptMinPosition.x, wp.ptMinPosition.y);
    if (mm.iArrange & ARW_HIDE)
        CHECK(!IntersectRect(&client, &rc, &mi.rcMonitor), L"ARW_HIDE: the minimized window is off screen");
    DestroyWindow(hwnd);
}

/* ---- shellhook ----------------------------------------------------------- */

static UINT shellhook_msg;
static HWND hook_seen[3]; /* created, activated, destroyed */
static HWND child_hwnd;

static LRESULT CALLBACK hook_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == shellhook_msg && shellhook_msg)
    {
        HWND target = (HWND)lp;
        WCHAR title[64] = L"";
        GetWindowTextW(target, title, 64);
        wprintf(L"  SHELLHOOK code %#lx hwnd %p \"%ls\"\n", (unsigned long)wp, target, title);
        /* the messages can arrive before wait_window() returns, so match the
         * created window by title and the later ones by that handle */
        if (wp == HSHELL_WINDOWCREATED && !wcscmp(title, L"setu-xproc child")) hook_seen[0] = target;
        if ((wp & 0x7fff) == HSHELL_WINDOWACTIVATED && target && target == hook_seen[0]) hook_seen[1] = target;
        if (wp == HSHELL_WINDOWDESTROYED && target && target == hook_seen[0]) hook_seen[2] = target;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void check_shellhook(void)
{
    WNDCLASSW wc = { 0 };
    PROCESS_INFORMATION pi;
    HWND hook, fg_seen = NULL;
    DWORD end;
    BOOL r;

    wc.lpfnWndProc = hook_proc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"SetuXprocHook";
    RegisterClassW(&wc);
    hook = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"setu-xproc hook", WS_POPUP, 0, 0, 1, 1,
                           NULL, NULL, wc.hInstance, NULL);
    shellhook_msg = RegisterWindowMessageW(L"SHELLHOOK");
    r = RegisterShellHookWindow(hook);
    CHECK(r, L"RegisterShellHookWindow");

    pi = spawn_self(L"child-window");
    child_hwnd = wait_window(L"setu-xproc child", 10000);
    CHECK(child_hwnd != NULL, L"child window appeared");
    end = GetTickCount() + 10000;
    while (WaitForSingleObject(pi.hProcess, 0) == WAIT_TIMEOUT && (LONG)(end - GetTickCount()) > 0)
    {
        if (!fg_seen && child_hwnd && GetForegroundWindow() == child_hwnd) fg_seen = child_hwnd;
        pump(20);
    }
    pump(500);
    CHECK(hook_seen[0] == child_hwnd && child_hwnd, L"HSHELL_WINDOWCREATED for the child's window");
    if (fg_seen) CHECK(hook_seen[1] == child_hwnd, L"HSHELL_WINDOWACTIVATED for the child's window");
    else wprintf(L"skip: the child window never became the foreground window\n");
    CHECK(hook_seen[2] == child_hwnd && child_hwnd, L"HSHELL_WINDOWDESTROYED for the child's window");
    CHECK(DeregisterShellHookWindow(hook), L"DeregisterShellHookWindow");
    DestroyWindow(hook);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
}

static int child_window(void)
{
    HWND hwnd = make_window(L"SetuXprocChild", L"setu-xproc child", NULL, NULL);
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
    pump(1500);
    DestroyWindow(hwnd);
    pump(200);
    return 0;
}

/* ---- icons --------------------------------------------------------------- */

static HICON solid_icon(int size, COLORREF rgb)
{
    BITMAPINFO bi = { { sizeof(BITMAPINFOHEADER), size, -size, 1, 32, BI_RGB } };
    ICONINFO ii = { TRUE };
    DWORD *bits, px = 0xff000000 | (GetRValue(rgb) << 16) | (GetGValue(rgb) << 8) | GetBValue(rgb);
    HBITMAP color = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, (void **)&bits, NULL, 0);
    HICON icon;
    int i;

    for (i = 0; i < size * size; i++) bits[i] = px;
    ii.hbmColor = color;
    ii.hbmMask = CreateBitmap(size, size, 1, 1, NULL);
    icon = CreateIconIndirect(&ii);
    DeleteObject(ii.hbmMask);
    DeleteObject(color);
    return icon;
}

/* draw the icon at size x size and return the centre pixel as 0xRRGGBB */
static DWORD draw_center(HICON icon, int size)
{
    BITMAPINFO bi = { { sizeof(BITMAPINFOHEADER), size, -size, 1, 32, BI_RGB } };
    HDC dc = CreateCompatibleDC(NULL);
    DWORD *bits, px;
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, (void **)&bits, NULL, 0);
    HGDIOBJ old = SelectObject(dc, bmp);
    memset(bits, 0, size * size * 4);
    if (!DrawIconEx(dc, 0, 0, icon, size, size, 0, NULL, DI_NORMAL)) px = 0xdeadbeef;
    else px = bits[(size / 2) * size + size / 2] & 0xffffff;
    SelectObject(dc, old);
    DeleteObject(bmp);
    DeleteDC(dc);
    return px;
}

static void check_icon(const WCHAR *what, HICON icon, int size, DWORD want)
{
    ICONINFO ii = { 0 };
    BITMAP bm = { 0 };
    BOOL info;
    DWORD px;

    CHECK(icon != NULL, L"%ls: handle %p", what, icon);
    if (!icon) return;
    info = GetIconInfo(icon, &ii);
    if (info) GetObjectW(ii.hbmColor ? ii.hbmColor : ii.hbmMask, sizeof(bm), &bm);
    CHECK(info && bm.bmWidth == size, L"%ls: GetIconInfo (%d, %ldx%ld)", what, info, bm.bmWidth, bm.bmHeight);
    if (ii.hbmColor) DeleteObject(ii.hbmColor);
    if (ii.hbmMask) DeleteObject(ii.hbmMask);
    px = draw_center(icon, size);
    CHECK(px == want, L"%ls: DrawIconEx draws %06lx (want %06lx)", what, px, want);
}

static void check_icons(void)
{
    PROCESS_INFORMATION pi = spawn_self(L"child-icon");
    HWND hwnd = wait_window(L"setu-xproc icons", 10000);
    DWORD_PTR r = 0;

    CHECK(hwnd != NULL, L"child window with icons appeared");
    if (hwnd)
    {
        check_icon(L"GCLP_HICON", (HICON)GetClassLongPtrW(hwnd, GCLP_HICON), 32, 0xff0000);
        check_icon(L"GCLP_HICONSM", (HICON)GetClassLongPtrW(hwnd, GCLP_HICONSM), 16, 0x00ff00);
        SendMessageTimeoutW(hwnd, WM_GETICON, ICON_BIG, 0, SMTO_ABORTIFHUNG, 2000, &r);
        check_icon(L"WM_GETICON ICON_BIG", (HICON)r, 32, 0x0000ff);
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
    }
    WaitForSingleObject(pi.hProcess, 10000);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
}

static int child_icon(void)
{
    HWND hwnd = make_window(L"SetuXprocIcons", L"setu-xproc icons (starting)", solid_icon(32, RGB(255, 0, 0)),
                            solid_icon(16, RGB(0, 255, 0)));
    MSG msg;
    SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)solid_icon(32, RGB(0, 0, 255)));
    /* the parent looks the window up by this title: set it once the icons are in place */
    SetWindowTextW(hwnd, L"setu-xproc icons");
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    SetTimer(hwnd, 1, 20000, NULL); /* give up eventually */
    while (GetMessageW(&msg, NULL, 0, 0) > 0)
    {
        if (msg.message == WM_TIMER) DestroyWindow(hwnd);
        DispatchMessageW(&msg);
    }
    return 0;
}

int wmain(int argc, WCHAR **argv)
{
    SetProcessDPIAware();
    if (argc < 2) goto usage;
    if (!wcscmp(argv[1], L"workarea")) check_workarea();
    else if (!wcscmp(argv[1], L"shellhook")) check_shellhook();
    else if (!wcscmp(argv[1], L"icons")) check_icons();
    else if (!wcscmp(argv[1], L"child-window")) return child_window();
    else if (!wcscmp(argv[1], L"child-icon")) return child_icon();
    else goto usage;
    wprintf(L"%ls: %d failure(s)\n", argv[1], failures);
    return failures;
usage:
    fwprintf(stderr, L"usage: setu-xproc workarea | shellhook | icons\n");
    return 2;
}
