/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * setu-probe: small helper for the shell smoke test.
 *
 *   setu-probe mklnk LNK TARGET [ARGS]   create a shell link (IShellLinkW,
 *                                        IPersistFile::Save)
 *   setu-probe workarea                  print SPI_GETWORKAREA and the
 *                                        primary monitor's rcWork, as seen
 *                                        from this (separate) process
 *   setu-probe appbar-pos                print ABM_GETTASKBARPOS
 *   setu-probe click X Y                 click at screen X,Y (SendInput)
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

static int mklnk(const WCHAR *lnk, const WCHAR *target, const WCHAR *args)
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
    if (args) IShellLinkW_SetArguments(sl, args);
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
        ret = mklnk(argv[2], argv[3], argc > 4 ? argv[4] : NULL);
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
    else if (argc >= 4 && !wcscmp(argv[1], L"click"))
    {
        /* a real left click at screen pixel (x, y), through SendInput */
        INPUT in[3];
        int x = _wtoi(argv[2]), y = _wtoi(argv[3]);
        int w = GetSystemMetrics(SM_CXSCREEN), h = GetSystemMetrics(SM_CYSCREEN);
        UINT n;

        memset(in, 0, sizeof(in));
        in[0].type = in[1].type = in[2].type = INPUT_MOUSE;
        in[0].mi.dx = MulDiv(x, 65535, w - 1);
        in[0].mi.dy = MulDiv(y, 65535, h - 1);
        in[0].mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
        in[1].mi = in[0].mi;
        in[1].mi.dwFlags |= MOUSEEVENTF_LEFTDOWN;
        in[2].mi = in[0].mi;
        in[2].mi.dwFlags |= MOUSEEVENTF_LEFTUP;
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
        fwprintf(stderr, L"usage: setu-probe mklnk LNK TARGET [ARGS] | workarea | appbar-pos | click X Y | setfg TITLE | windows [all]\n");
    fflush(stdout);
    CoUninitialize();
    return ret;
}
