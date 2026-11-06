/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * setu-probe: small helper for the shell smoke test.
 *
 *   setu-probe mklnk LNK TARGET [ARGS [ICON,INDEX]]
 *                                        create a shell link (IShellLinkW,
 *                                        IPersistFile::Save)
 *   setu-probe workarea                  print SPI_GETWORKAREA and the
 *                                        primary monitor's rcWork, as seen
 *                                        from this (separate) process
 *   setu-probe appbar-pos                print ABM_GETTASKBARPOS
 *   setu-probe click X Y                 click at screen X,Y (SendInput)
 *   setu-probe rclick X Y                right-click at screen X,Y
 *   setu-probe key SPEC...               press keys (SendInput): "lwin",
 *                                        "ctrl+esc", "down", "enter", "p"...
 *   setu-probe type TEXT                 type TEXT as key presses
 *   setu-probe setfg TITLE               SetForegroundWindow on that window
 *   setu-probe windows [all]             list top-level windows, top of the
 *                                        z-order first, one per line:
 *                                        window=HWND|class|title|L,T,R,B|state
 *                                        ("all" includes hidden ones)
 *
 * Exit code 0 on success.
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

/* A key name for "key": a letter or digit, or one of these. */
static WORD key_vk(const WCHAR *name)
{
    static const struct { const WCHAR *name; WORD vk; } keys[] = {
        { L"lwin", VK_LWIN }, { L"esc", VK_ESCAPE }, { L"enter", VK_RETURN }, { L"down", VK_DOWN },
        { L"up", VK_UP }, { L"left", VK_LEFT }, { L"right", VK_RIGHT }, { L"apps", VK_APPS },
        { L"tab", VK_TAB }, { L"f10", VK_F10 }, { L"ctrl", VK_CONTROL }, { L"shift", VK_SHIFT },
        { L"back", VK_BACK },
    };
    int i;
    for (i = 0; i < (int)ARRAYSIZE(keys); i++)
        if (!lstrcmpiW(keys[i].name, name)) return keys[i].vk;
    if (name[0] && !name[1]) return LOBYTE(VkKeyScanW(name[0]));
    return 0;
}

static UINT send_key(WORD vk, BOOL up)
{
    INPUT in;
    memset(&in, 0, sizeof(in));
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.wScan = (WORD)MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    if (up) in.ki.dwFlags |= KEYEVENTF_KEYUP;
    if (vk == VK_LWIN || vk == VK_APPS || (vk >= VK_PRIOR && vk <= VK_DOWN)) in.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    return SendInput(1, &in, sizeof(in));
}

/* key SPEC: press and release, e.g. "lwin", "ctrl+esc", "shift+f10", "p" */
static int press_keys(const WCHAR *spec)
{
    WCHAR buf[64], *parts[4], *p, *ctx = NULL;
    WORD vks[4];
    int n = 0, i;
    UINT sent = 0;

    lstrcpynW(buf, spec, ARRAYSIZE(buf));
    for (p = wcstok_s(buf, L"+", &ctx); p && n < 4; p = wcstok_s(NULL, L"+", &ctx)) parts[n++] = p;
    for (i = 0; i < n; i++)
        if (!(vks[i] = key_vk(parts[i]))) { fwprintf(stderr, L"unknown key %ls\n", parts[i]); return 1; }
    for (i = 0; i < n; i++) { sent += send_key(vks[i], FALSE); Sleep(30); }
    for (i = n - 1; i >= 0; i--) { sent += send_key(vks[i], TRUE); Sleep(30); }
    Sleep(100);
    wprintf(L"key %ls: %u events\n", spec, sent);
    return sent != (UINT)(2 * n);
}

/* type TEXT: letters, digits and spaces, as key presses */
static int type_text(const WCHAR *text)
{
    UINT sent = 0, want = 0;
    for (; *text; text++)
    {
        SHORT vs = VkKeyScanW(*text);
        BOOL shift = (vs >> 8) & 1;
        if (vs == -1) continue;
        if (shift) { sent += send_key(VK_SHIFT, FALSE); want++; }
        sent += send_key(LOBYTE(vs), FALSE);
        sent += send_key(LOBYTE(vs), TRUE);
        want += 2;
        if (shift) { sent += send_key(VK_SHIFT, TRUE); want++; }
        Sleep(20);
    }
    Sleep(100);
    wprintf(L"type: %u events\n", sent);
    return sent != want;
}

static int mklnk(const WCHAR *lnk, const WCHAR *target, const WCHAR *args, const WCHAR *icon)
{
    IShellLinkW *sl;
    IPersistFile *pf;
    HRESULT hr;
    WCHAR dir[MAX_PATH], *slash;

    lstrcpynW(dir, lnk, MAX_PATH);
    if ((slash = wcsrchr(dir, L'\\')))
    {
        *slash = 0;
        SHCreateDirectoryExW(NULL, dir, NULL);
    }
    hr = CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkW, (void **)&sl);
    if (FAILED(hr)) { wprintf(L"CoCreateInstance failed %08lx\n", hr); return 1; }
    IShellLinkW_SetPath(sl, target);
    if (args && args[0]) IShellLinkW_SetArguments(sl, args);
    if (icon && icon[0])
    {
        /* "FILE,INDEX" */
        WCHAR file[MAX_PATH], *comma;
        lstrcpynW(file, icon, MAX_PATH);
        comma = wcsrchr(file, L',');
        if (comma) *comma = 0;
        IShellLinkW_SetIconLocation(sl, file, comma ? _wtoi(comma + 1) : 0);
    }
    hr = IShellLinkW_QueryInterface(sl, &IID_IPersistFile, (void **)&pf);
    if (SUCCEEDED(hr))
    {
        hr = IPersistFile_Save(pf, lnk, TRUE);
        IPersistFile_Release(pf);
    }
    IShellLinkW_Release(sl);
    wprintf(L"mklnk %ls -> %ls: %08lx\n", lnk, target, hr);
    return FAILED(hr);
}

static BOOL CALLBACK list_window(HWND hwnd, LPARAM lp)
{
    WCHAR title[128] = L"", cls[64] = L"";
    RECT rc;
    DWORD pid = 0;

    if (lp && !IsWindowVisible(hwnd)) return TRUE;
    GetWindowTextW(hwnd, title, 128);
    GetClassNameW(hwnd, cls, 64);
    GetWindowRect(hwnd, &rc);
    GetWindowThreadProcessId(hwnd, &pid);
    wprintf(L"window=%p|%ls|%ls|%ld,%ld,%ld,%ld|%ls%ls\n", hwnd, cls, title, rc.left, rc.top, rc.right, rc.bottom,
            IsWindowVisible(hwnd) ? L"visible" : L"hidden", IsIconic(hwnd) ? L",minimized" : L"");
    return TRUE;
}

int wmain(int argc, WCHAR **argv)
{
    int ret = 2;

    SetProcessDPIAware();
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (argc >= 4 && !wcscmp(argv[1], L"mklnk"))
        ret = mklnk(argv[2], argv[3], argc > 4 ? argv[4] : NULL, argc > 5 ? argv[5] : NULL);
    else if (argc >= 3 && !wcscmp(argv[1], L"key"))
    {
        int i;
        for (i = 2, ret = 0; i < argc && !ret; i++) ret = press_keys(argv[i]);
    }
    else if (argc >= 3 && !wcscmp(argv[1], L"type"))
        ret = type_text(argv[2]);
    else if (argc >= 2 && !wcscmp(argv[1], L"workarea"))
    {
        RECT wa;
        POINT origin = { 0, 0 };
        MONITORINFO mi = { sizeof(mi) };
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
        GetMonitorInfoW(MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY), &mi);
        wprintf(L"spi_workarea=%ld,%ld,%ld,%ld\n", wa.left, wa.top, wa.right, wa.bottom);
        wprintf(L"monitor=%ld,%ld,%ld,%ld\n", mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right, mi.rcMonitor.bottom);
        wprintf(L"monitor_work=%ld,%ld,%ld,%ld\n", mi.rcWork.left, mi.rcWork.top, mi.rcWork.right, mi.rcWork.bottom);
        ret = 0;
    }
    else if (argc >= 2 && !wcscmp(argv[1], L"appbar-pos"))
    {
        APPBARDATA abd = { sizeof(abd) };
        UINT_PTR r = SHAppBarMessage(ABM_GETTASKBARPOS, &abd);
        wprintf(L"taskbarpos=%lu edge=%u rc=%ld,%ld,%ld,%ld\n", (unsigned long)r, abd.uEdge,
                abd.rc.left, abd.rc.top, abd.rc.right, abd.rc.bottom);
        ret = 0;
    }
    else if (argc >= 4 && (!wcscmp(argv[1], L"click") || !wcscmp(argv[1], L"rclick")))
    {
        /* a real left (or right) click at screen pixel (x, y), through SendInput */
        INPUT in[3];
        int x = _wtoi(argv[2]), y = _wtoi(argv[3]);
        BOOL right = argv[1][0] == L'r';
        int w = GetSystemMetrics(SM_CXSCREEN), h = GetSystemMetrics(SM_CYSCREEN);
        UINT n;

        memset(in, 0, sizeof(in));
        in[0].type = in[1].type = in[2].type = INPUT_MOUSE;
        in[0].mi.dx = MulDiv(x, 65535, w - 1);
        in[0].mi.dy = MulDiv(y, 65535, h - 1);
        in[0].mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
        in[1].mi = in[0].mi;
        in[1].mi.dwFlags |= right ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_LEFTDOWN;
        in[2].mi = in[0].mi;
        in[2].mi.dwFlags |= right ? MOUSEEVENTF_RIGHTUP : MOUSEEVENTF_LEFTUP;
        n = SendInput(1, &in[0], sizeof(INPUT));
        Sleep(100);
        n += SendInput(2, &in[1], sizeof(INPUT));
        Sleep(200);
        { POINT p; WCHAR cls[64] = L""; GetCursorPos(&p); GetClassNameW(WindowFromPoint(p), cls, 64);
          wprintf(L"click %d,%d: %u events; cursor now %ld,%ld over %ls\n", x, y, n, p.x, p.y, cls); }
        ret = n != 3;
    }
    else if (argc >= 3 && !wcscmp(argv[1], L"setfg"))
    {
        HWND hwnd = FindWindowW(NULL, argv[2]);
        WCHAR title[256] = L"";
        BOOL r = SetForegroundWindow(hwnd);
        DWORD err = GetLastError();
        Sleep(500);
        GetWindowTextW(GetForegroundWindow(), title, 256);
        wprintf(L"setfg %p -> %d (err %lu); foreground now %p \"%ls\"\n", hwnd, r, err,
                GetForegroundWindow(), title);
        ret = !r;
    }
    else if (argc >= 2 && !wcscmp(argv[1], L"windows"))
    {
        /* top-level windows, top of the z-order first; "windows all" includes hidden ones */
        EnumWindows(list_window, argc < 3 || wcscmp(argv[2], L"all"));
        ret = 0;
    }
    else
        fwprintf(stderr, L"usage: setu-probe mklnk LNK TARGET [ARGS [ICON,INDEX]] | workarea | appbar-pos | click X Y | rclick X Y | key SPEC... | type TEXT | setfg TITLE | windows [all]\n");
    fflush(stdout);
    CoUninitialize();
    return ret;
}
