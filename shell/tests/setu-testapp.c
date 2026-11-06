/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * setu-testapp: a tiny windowed program for the start menu smoke test
 * (tools/startmenu-smoke.sh). Shortcuts and Uninstall entries point at it.
 *
 *   setu-testapp [--title TITLE]   show a window called TITLE (default
 *                                  "Setu Test App") until it is closed
 *
 * It has no icon resource on purpose, so the menu shows its own glyph.
 */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <shellapi.h>

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_PAINT)
    {
        PAINTSTRUCT ps;
        RECT rc;
        WCHAR title[256];
        HDC dc = BeginPaint(hwnd, &ps);
        GetClientRect(hwnd, &rc);
        GetWindowTextW(hwnd, title, 256);
        DrawTextW(dc, title, -1, &rc, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
        EndPaint(hwnd, &ps);
        return 0;
    }
    if (msg == WM_DESTROY)
    {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmdline, int show)
{
    const WCHAR *title = L"Setu Test App";
    WNDCLASSW wc = { 0 };
    WCHAR **argv;
    int argc, i;
    MSG msg;
    HWND hwnd;

    (void)prev; (void)cmdline;
    argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (i = 1; i + 1 < argc; i++)
        if (!wcscmp(argv[i], L"--title")) title = argv[++i];

    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"SetuTestApp";
    RegisterClassW(&wc);
    hwnd = CreateWindowExW(0, wc.lpszClassName, title, WS_OVERLAPPEDWINDOW, 700, 120, 360, 160,
                           NULL, NULL, inst, NULL);
    ShowWindow(hwnd, show);
    while (GetMessageW(&msg, NULL, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    LocalFree(argv);
    return 0;
}
