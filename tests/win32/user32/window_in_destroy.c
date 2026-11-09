/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: user32's IsWindowInDestroy export (imported by the Windows
 * App SDK windowing DLLs that Firefox ships, through
 * api-ms-win-rtcore-ntuser-private-l1-1-0): what it returns for a live
 * window, during WM_DESTROY and WM_NCDESTROY of the window and of its
 * parent, for a child destroyed with its parent, after destruction, for
 * NULL and a bogus handle, and for another thread's window; last error.
 *
 * It is undocumented, so it is taken with GetProcAddress and called as
 * BOOL (WINAPI *)(HWND), the shape every caller in the import tables uses;
 * only the boolean result and the last error are printed.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/winmsg/wm-destroy
 *       https://learn.microsoft.com/en-us/windows/win32/winmsg/wm-ncdestroy
 *       https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-destroywindow
 */
#include "oracle.h"

typedef BOOL (WINAPI *in_destroy_fn)(HWND);
static in_destroy_fn pIsWindowInDestroy;

static HWND parent, child;
static int r_destroy = -1, r_ncdestroy = -1, r_child_destroy = -1, r_child_ncdestroy = -1;
static int r_parent_in_child_destroy = -1, r_child_in_parent_destroy = -1, r_create = -1;

static int call(HWND h) { return pIsWindowInDestroy(h) ? 1 : 0; }

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        r_create = call(hwnd);
        break;
    case WM_DESTROY:
        if (hwnd == parent) {
            r_destroy = call(hwnd);
            if (child) r_child_in_parent_destroy = call(child);
        } else {
            r_child_destroy = call(hwnd);
            r_parent_in_child_destroy = call(parent);
        }
        break;
    case WM_NCDESTROY:
        if (hwnd == parent) r_ncdestroy = call(hwnd);
        else r_child_ncdestroy = call(hwnd);
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static HWND other_hwnd;
static HANDLE other_ready, other_done;
static DWORD WINAPI other_thread(void *arg)
{
    MSG m;
    (void)arg;
    other_hwnd = CreateWindowExW(0, L"SetuInDestroy", L"other", WS_OVERLAPPEDWINDOW,
                                 0, 0, 100, 100, NULL, NULL, GetModuleHandleW(NULL), NULL);
    SetEvent(other_ready);
    while (WaitForSingleObject(other_done, 0) == WAIT_TIMEOUT) {
        while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&m);
        Sleep(5);
    }
    DestroyWindow(other_hwnd);
    return 0;
}

int main(void)
{
    oracle_begin("user32/window_in_destroy");
    WNDCLASSW wc = {0};
    HWND h;
    int r;

    pIsWindowInDestroy = (in_destroy_fn)(void (*)(void))GetProcAddress(GetModuleHandleW(L"user32.dll"), "IsWindowInDestroy");
    oracle_case("export_present");
    o_bool("present", pIsWindowInDestroy != NULL);
    o_bool("apiset_resolves", GetProcAddress(LoadLibraryW(L"api-ms-win-rtcore-ntuser-private-l1-1-0.dll"), "IsWindowInDestroy") != NULL);
    oracle_case_end();
    if (!pIsWindowInDestroy) return oracle_end();

    wc.lpfnWndProc = wndproc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"SetuInDestroy";
    RegisterClassW(&wc);

    parent = CreateWindowExW(0, L"SetuInDestroy", L"parent", WS_OVERLAPPEDWINDOW,
                             0, 0, 200, 200, NULL, NULL, wc.hInstance, NULL);
    child = CreateWindowExW(0, L"SetuInDestroy", L"child", WS_CHILD | WS_VISIBLE,
                            0, 0, 50, 50, parent, NULL, wc.hInstance, NULL);

    ORACLE_CALL(r, call(parent));
    oracle_case("live_window");
    o_bool("ret", r);
    o_lasterr();
    o_bool("during_wm_create", r_create == 1);
    oracle_case_end();

    DestroyWindow(parent);
    oracle_case("during_destroy");
    o_i64("wm_destroy", r_destroy);
    o_i64("wm_ncdestroy", r_ncdestroy);
    o_i64("child_wm_destroy", r_child_destroy);
    o_i64("child_wm_ncdestroy", r_child_ncdestroy);
    o_i64("child_in_parent_destroy", r_child_in_parent_destroy);
    o_i64("parent_in_child_destroy", r_parent_in_child_destroy);
    oracle_case_end();

    ORACLE_CALL(r, call(parent));
    oracle_case("after_destroy");
    o_bool("ret", r);
    o_lasterr();
    oracle_case_end();

    ORACLE_CALL(r, call(NULL));
    oracle_case("null_hwnd");
    o_bool("ret", r);
    o_lasterr();
    oracle_case_end();

    ORACLE_CALL(r, call((HWND)(ULONG_PTR)0x12345678));
    oracle_case("bogus_hwnd");
    o_bool("ret", r);
    o_lasterr();
    oracle_case_end();

    /* a child destroyed on its own */
    parent = CreateWindowExW(0, L"SetuInDestroy", L"parent", WS_OVERLAPPEDWINDOW,
                             0, 0, 200, 200, NULL, NULL, wc.hInstance, NULL);
    h = CreateWindowExW(0, L"SetuInDestroy", L"child", WS_CHILD | WS_VISIBLE,
                        0, 0, 50, 50, parent, NULL, wc.hInstance, NULL);
    child = NULL;
    r_child_destroy = r_child_ncdestroy = r_parent_in_child_destroy = -1;
    DestroyWindow(h);
    oracle_case("child_alone");
    o_i64("child_wm_destroy", r_child_destroy);
    o_i64("child_wm_ncdestroy", r_child_ncdestroy);
    o_i64("parent_meanwhile", r_parent_in_child_destroy);
    o_bool("parent_after", call(parent));
    oracle_case_end();
    DestroyWindow(parent);

    /* another thread's window */
    other_ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    other_done = CreateEventW(NULL, TRUE, FALSE, NULL);
    {
        HANDLE t = CreateThread(NULL, 0, other_thread, NULL, 0, NULL);
        WaitForSingleObject(other_ready, INFINITE);
        ORACLE_CALL(r, call(other_hwnd));
        oracle_case("other_thread_window");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        SetEvent(other_done);
        WaitForSingleObject(t, 10000);
        CloseHandle(t);
    }

    return oracle_end();
}
