/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: user32's undocumented CoreMessaging completion-port exports,
 * which the Windows App SDK DLLs that Firefox ships (CoreMessagingXP) call
 * on Windows 11: InitThreadCoreMessagingIocp2 (ordinal 2669),
 * DrainThreadCoreMessagingCompletions2 (2670) and their older forms
 * (2612, 2613). Ordinals and names come from Wine's user32.spec; the
 * win32u counterparts (NtUser*) are named in Wine's win32u.spec and in
 * phnt's ntuser.h (MIT; accepted source 2026-11-09).
 *
 * There is no documentation, so this is black-box only: the exports are
 * taken with GetProcAddress and called with chosen arguments, each call in
 * a child process of its own (so a crash is an outcome, not a lost run).
 * The child reports return values classified (zero, a handle of some
 * object type, or another value), the last error, and what changes for
 * the thread afterwards. No handle values or addresses are printed.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/fileio/i-o-completion-ports
 *       https://learn.microsoft.com/en-us/windows/win32/api/winternl/nf-winternl-ntqueryobject
 *       https://github.com/winsiderss/phnt/blob/master/ntuser.h
 */
#include "oracle.h"
#include <winternl.h>

typedef ULONG_PTR (WINAPI *fn4)(ULONG_PTR, ULONG_PTR, ULONG_PTR, ULONG_PTR);

static HMODULE user32, win32u;
static wchar_t self[MAX_PATH];

/* ---- child side --------------------------------------------------------- */

static char out[4096];
static size_t outlen;
static void put(const char *fmt, ...)
{
    va_list ap;
    DWORD done;
    va_start(ap, fmt);
    outlen = vsnprintf(out, sizeof(out), fmt, ap);
    va_end(ap);
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), out, (DWORD)outlen, &done, NULL);
}

typedef NTSTATUS (WINAPI *query_object_fn)(HANDLE, OBJECT_INFORMATION_CLASS, void *, ULONG, ULONG *);

/* "0", "handle:<type>", "small:<n>" below 16, "nonhandle" for other values
 * below 0x10000 (no handle values are printed), else "other" */
static void classify(const char *k, ULONG_PTR v)
{
    static query_object_fn pNtQueryObject;
    char buf[1024];
    ULONG len;
    if (!pNtQueryObject)
        pNtQueryObject = (query_object_fn)(void (*)(void))GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryObject");
    if (!v) { put("%s=0 ", k); return; }
    if (v < 0x100000 && !pNtQueryObject((HANDLE)v, (OBJECT_INFORMATION_CLASS)2, buf, sizeof(buf), &len)) {
        PUBLIC_OBJECT_TYPE_INFORMATION *ti = (PUBLIC_OBJECT_TYPE_INFORMATION *)buf;
        put("%s=handle:%ls ", k, ti->TypeName.Buffer);
        return;
    }
    if (v < 16) { put("%s=small:%lu ", k, (unsigned long)v); return; }
    if (v < 0x10000) { put("%s=nonhandle ", k); return; }
    put("%s=other ", k);
}

static fn4 get_ord(int ord) { return (fn4)(void (*)(void))GetProcAddress(user32, MAKEINTRESOURCEA(ord)); }

typedef ULONG_PTR (WINAPI *fn0)(void);

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* Registered once by child_main before any thread starts. A lazy
 * registration raced between the main thread and other_thread, and the
 * loser's CreateWindowExW could fail (ERROR_CANNOT_FIND_WND_CLASS), leaving
 * other_hwnd NULL. */
static void register_class(void)
{
    WNDCLASSW wc = {0};
    wc.lpfnWndProc = wndproc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"SetuCoreMsg";
    RegisterClassW(&wc);
}

static HWND make_window(void)
{
    return CreateWindowExW(0, L"SetuCoreMsg", L"coremsg", WS_OVERLAPPEDWINDOW,
                           0, 0, 100, 100, NULL, NULL, GetModuleHandleW(NULL), NULL);
}

/* one GetQueuedCompletionStatus(port, 0 ms) */
static void dequeue(const char *tag, HANDLE port)
{
    DWORD bytes = 0xdead;
    ULONG_PTR key = 0xdead;
    OVERLAPPED *ov = (OVERLAPPED *)0xdead;
    BOOL ok;
    SetLastError(ORACLE_SENTINEL);
    ok = GetQueuedCompletionStatus(port, &bytes, &key, &ov, 0);
    put("%s=%d,err=%lu,bytes=%lx,key=%s,ov=%s ", tag, ok, ok ? 0 : GetLastError(), bytes,
        key == 0xdead ? "untouched" : key < 0x10000 ? "small" : "other",
        ov == (OVERLAPPED *)0xdead ? "untouched" : !ov ? "null" : "other");
    if (key && key != 0xdead && key < 0x10000) put("keyval=%lu ", (unsigned long)key);
}

/* what the thread looks like after an init call that returned r */
static void after_init(HWND h, ULONG_PTR r)
{
    fn0 getq = (fn0)(void (*)(void))GetProcAddress(win32u, "NtUserGetQueueIocp");
    MSG m;
    HANDLE port = (HANDLE)r;
    if (getq) {
        ULONG_PTR q = getq();
        classify("queueiocp", q);
        put("queueiocp_is_ret=%d ", q == r);
    }
    if (!r || r >= 0x100000) return;
    dequeue("gqcs_idle", port);
    PostMessageW(h, WM_USER + 1, 1, 2);
    dequeue("gqcs_post", port);
    dequeue("gqcs_post_again", port);
    put("peek=%d ", PeekMessageW(&m, h, WM_USER + 1, WM_USER + 1, PM_REMOVE));
    dequeue("gqcs_after_peek", port);
    PostThreadMessageW(GetCurrentThreadId(), WM_USER + 2, 0, 0);
    dequeue("gqcs_threadmsg", port);
    PeekMessageW(&m, NULL, 0, 0, PM_REMOVE);
    SendMessageW(h, WM_USER + 3, 0, 0);
    dequeue("gqcs_send", port);
    SetTimer(h, 1, 1, NULL);
    Sleep(50);
    dequeue("gqcs_timer", port);
    KillTimer(h, 1);
}

static HWND other_hwnd;
static DWORD WINAPI other_thread(void *ev)
{
    MSG m;
    other_hwnd = make_window();
    SetEvent(ev);
    while (GetMessageW(&m, NULL, 0, 0)) DispatchMessageW(&m);
    return 0;
}

/* posts to h from another thread after a delay */
static HWND poster_hwnd;
static UINT poster_kind;
static DWORD WINAPI poster(void *arg)
{
    (void)arg;
    Sleep(200);
    if (poster_kind == 0) PostMessageW(poster_hwnd, WM_USER + 5, 0, 0);
    else if (poster_kind == 1) SendMessageTimeoutW(poster_hwnd, WM_USER + 6, 0, 0, SMTO_NORMAL, 500, NULL);
    else if (poster_kind == 2) PostThreadMessageW((DWORD)(ULONG_PTR)arg, WM_USER + 7, 0, 0);
    return 0;
}

static void out_word(const char *tag, ULONG_PTR v)
{
    if (v == (ULONG_PTR)0x5a5a5a5a5a5a5a5aull) put("%s=preset ", tag);
    else if ((v >> 32) == 0x5a5a5a5a) put("%s=hi_preset,lo=%lx ", tag, (unsigned long)(DWORD)v);
    else if (v < 0x10000) put("%s=%lu ", tag, (unsigned long)v);
    else put("%s=other ", tag);
}

/* init2(h, &v) then a blocking GetQueuedCompletionStatus on the result
 * while another thread posts or sends to h */
static void blocked_wake(fn4 init2, fn4 drain2, UINT kind)
{
    HWND h = make_window();
    ULONG_PTR v = (ULONG_PTR)0x5a5a5a5a5a5a5a5aull, r;
    DWORD bytes = 0xdead, t0;
    ULONG_PTR key = 0xdead;
    OVERLAPPED *ov = (OVERLAPPED *)0xdead;
    BOOL ok;
    HANDLE th;
    MSG m;
    r = init2((ULONG_PTR)h, (ULONG_PTR)&v, 0, 0);
    classify("init", r);
    out_word("v", v);
    poster_hwnd = h;
    poster_kind = kind;
    th = CreateThread(NULL, 0, poster, (void *)(ULONG_PTR)GetCurrentThreadId(), 0, NULL);
    t0 = GetTickCount();
    SetLastError(ORACLE_SENTINEL);
    ok = GetQueuedCompletionStatus((HANDLE)r, &bytes, &key, &ov, 1500);
    put("gqcs=%d,err=%lu,woke_early=%d,bytes=%lx,key=%s,ov=%s ", ok, ok ? 0 : GetLastError(),
        GetTickCount() - t0 < 1000, bytes,
        key == 0xdead ? "untouched" : key < 0x10000 ? "small" : "other",
        ov == (OVERLAPPED *)0xdead ? "untouched" : !ov ? "null" : "other");
    if (key != 0xdead && key < 0x10000) put("keyval=%lu ", (unsigned long)key);
    if (ok || ov) put("bytesval=%lu ", bytes);
    if (drain2) {
        ULONG_PTR d = (ULONG_PTR)0x5a5a5a5a5a5a5a5aull;
        SetLastError(ORACLE_SENTINEL);
        r = drain2((ULONG_PTR)h, (ULONG_PTR)&d, 0, 0);
        classify("drain", r);
        put("drain_err=%lu ", GetLastError() == ORACLE_SENTINEL ? 0 : GetLastError());
        out_word("d", d);
    }
    WaitForSingleObject(th, 3000);
    put("peek=%d ", PeekMessageW(&m, NULL, 0, 0, PM_REMOVE));
}

/* a second thread: its own window, its queue port and init2 */
static fn4 t_init2;
static ULONG_PTR t_port, t_ret, t_getq_nogui, t_getq_nogui2;
static char t_cls[256];
static DWORD t_err;
static DWORD WINAPI second_thread(void *arg)
{
    fn0 getq = (fn0)(void (*)(void))GetProcAddress(win32u, "NtUserGetQueueIocp");
    DWORD v = 0x5a5a5a5a;
    HWND h;
    (void)arg;
    t_getq_nogui = getq();
    t_getq_nogui2 = getq();
    h = make_window();
    t_port = getq();
    SetLastError(ORACLE_SENTINEL);
    t_ret = t_init2((ULONG_PTR)h, (ULONG_PTR)&v, 0, 0);
    t_err = GetLastError();
    /* classified here, while the thread's queue exists */
    t_cls[0] = 0;
    {
        char b[1024];
        ULONG len;
        typedef NTSTATUS (WINAPI *qo)(HANDLE, OBJECT_INFORMATION_CLASS, void *, ULONG, ULONG *);
        qo q = (qo)(void (*)(void))GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryObject");
        if (!q((HANDLE)t_port, (OBJECT_INFORMATION_CLASS)2, b, sizeof(b), &len))
            snprintf(t_cls, sizeof(t_cls), "%ls", ((PUBLIC_OBJECT_TYPE_INFORMATION *)b)->TypeName.Buffer);
        else
            snprintf(t_cls, sizeof(t_cls), "invalid");
    }
    DestroyWindow(h);
    return 0;
}

static int child_main(const wchar_t *what)
{
    fn4 init2 = get_ord(2669), drain2 = get_ord(2670), init1 = get_ord(2612), drain1 = get_ord(2613);
    ULONG_PTR r;
    DWORD err;

    register_class();

#define CALL(f, a, b, c, d) do { SetLastError(ORACLE_SENTINEL); r = (f)((ULONG_PTR)(a), (ULONG_PTR)(b), (ULONG_PTR)(c), (ULONG_PTR)(d)); err = GetLastError(); } while (0)
#define REPORT(tag) do { classify(tag, r); if (err == ORACLE_SENTINEL) put("err=unchanged "); else put("err=%lu ", err); } while (0)

    if (!wcsncmp(what, L"args2_", 6) || !wcsncmp(what, L"args1_", 6)) {
        /* argument shapes, one token per argument: 0, 1, h (own window),
         * p (pointer to 4 ULONG_PTRs preset to 0xdead), port (a new
         * completion port), qport (the queue's port) */
        HWND h = make_window();
        ULONG_PTR a[4] = {0}, buf[4][4];
        const wchar_t *t = what + 6;
        int i, j, np = 0;
        fn0 getq = (fn0)(void (*)(void))GetProcAddress(win32u, "NtUserGetQueueIocp");
        for (i = 0; i < 4 && *t; i++) {
            if (!wcsncmp(t, L"qport", 5)) { a[i] = getq(); t += 5; }
            else if (!wcsncmp(t, L"port", 4)) { a[i] = (ULONG_PTR)CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0); t += 4; }
            else if (*t == L'h') { a[i] = (ULONG_PTR)h; t++; }
            else if (*t == L'p') { for (j = 0; j < 4; j++) buf[np][j] = 0xdead; a[i] = (ULONG_PTR)buf[np++]; t++; }
            else if (*t == L'1') { a[i] = 1; t++; }
            else t++;
            if (*t == L'_') t++;
        }
        if (what[4] == L'2') CALL(init2, a[0], a[1], a[2], a[3]);
        else CALL(init1, a[0], a[1], a[2], a[3]);
        REPORT("ret");
        for (i = 0; i < np; i++) {
            put("p%d=", i);
            for (j = 0; j < 4; j++) {
                if (buf[i][j] == 0xdead) put("dead,");
                else classify("", buf[i][j]);
            }
        }
        if (getq) put("qport_is_ret=%d ", getq() == r);
        after_init(h, r);
    } else if (!wcscmp(what, L"init2_preset")) {
        HWND h = make_window();
        ULONG_PTR v[2] = { (ULONG_PTR)0x5a5a5a5a5a5a5a5aull, (ULONG_PTR)0x5a5a5a5a5a5a5a5aull };
        CALL(init2, h, v, 0, 0);
        REPORT("ret");
        out_word("v0", v[0]);
        out_word("v1", v[1]);
        v[0] = v[1] = (ULONG_PTR)0x5a5a5a5a5a5a5a5aull;
        CALL(init2, h, v, 0, 0);
        REPORT("again");
        out_word("v0", v[0]);
    } else if (!wcscmp(what, L"init2_pending_post")) {
        HWND h = make_window();
        ULONG_PTR v = (ULONG_PTR)0x5a5a5a5a5a5a5a5aull;
        MSG m;
        while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&m);
        PostMessageW(h, WM_USER + 1, 0, 0);
        CALL(init2, h, &v, 0, 0);
        REPORT("ret");
        out_word("v", v);
        after_init(h, r);
    } else if (!wcscmp(what, L"init2_bad_ptr")) {
        HWND h = make_window();
        CALL(init2, h, 0x10, 0, 0);
        REPORT("ret");
    } else if (!wcscmp(what, L"init2_other_thread_hwnd_p")) {
        HANDLE ev = CreateEventW(NULL, FALSE, FALSE, NULL);
        HANDLE t = CreateThread(NULL, 0, other_thread, ev, 0, NULL);
        ULONG_PTR v = (ULONG_PTR)0x5a5a5a5a5a5a5a5aull;
        WaitForSingleObject(ev, INFINITE);
        CALL(init2, other_hwnd, &v, 0, 0);
        REPORT("ret");
        out_word("v", v);
        PostMessageW(other_hwnd, WM_QUIT, 0, 0);
        WaitForSingleObject(t, 2000);
    } else if (!wcscmp(what, L"init2_message_only_p")) {
        HWND h = CreateWindowExW(0, L"STATIC", L"x", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, NULL, NULL);
        ULONG_PTR v = (ULONG_PTR)0x5a5a5a5a5a5a5a5aull;
        CALL(init2, h, &v, 0, 0);
        REPORT("ret");
        out_word("v", v);
    } else if (!wcscmp(what, L"init2_desktop_p")) {
        ULONG_PTR v = (ULONG_PTR)0x5a5a5a5a5a5a5a5aull;
        CALL(init2, GetDesktopWindow(), &v, 0, 0);
        REPORT("ret");
        out_word("v", v);
    } else if (!wcscmp(what, L"drain2_after_init_p")) {
        HWND h = make_window();
        ULONG_PTR v = (ULONG_PTR)0x5a5a5a5a5a5a5a5aull, d = (ULONG_PTR)0x5a5a5a5a5a5a5a5aull;
        CALL(init2, h, &v, 0, 0);
        REPORT("init");
        CALL(drain2, h, 0, 0, 0);
        REPORT("drain_null");
        CALL(drain2, h, &d, 0, 0);
        REPORT("drain_p");
        out_word("d", d);
        CALL(drain2, 0, &d, 0, 0);
        REPORT("drain_nullhwnd");
        CALL(drain1, 0, 0, 0, 0);
        REPORT("drain1");
    } else if (!wcscmp(what, L"wake_post")) {
        blocked_wake(init2, drain2, 0);
    } else if (!wcscmp(what, L"wake_send")) {
        blocked_wake(init2, drain2, 1);
    } else if (!wcscmp(what, L"wake_threadmsg")) {
        blocked_wake(init2, drain2, 2);
    } else if (!wcscmp(what, L"two_threads")) {
        fn0 getq = (fn0)(void (*)(void))GetProcAddress(win32u, "NtUserGetQueueIocp");
        HWND h = make_window();
        DWORD v = 0x5a5a5a5a;
        HANDLE t;
        ULONG_PTR mine;
        CALL(init2, h, &v, 0, 0);
        REPORT("ret");
        mine = getq();
        put("getq_stable=%d ", mine == getq());
        t_init2 = init2;
        t = CreateThread(NULL, 0, second_thread, NULL, 0, NULL);
        WaitForSingleObject(t, 5000);
        put("t_nogui_nonzero=%d ", t_getq_nogui != 0);
        put("t_nogui_stable=%d ", t_getq_nogui == t_getq_nogui2);
        put("t_nogui_is_later=%d ", t_getq_nogui == t_port);
        put("t_port=%s ", t_cls);
        classify("t_port_after_exit", t_port);
        put("t_port_same_as_main=%d ", t_port == mine);
        classify("t_ret", t_ret);
        put("t_ret_is_t_port=%d t_err=%lu ", t_ret == t_port, t_err == ORACLE_SENTINEL ? 0 : t_err);
        put("main_after=%d ", getq() == mine);
    } else if (!wcscmp(what, L"close_port")) {
        fn0 getq = (fn0)(void (*)(void))GetProcAddress(win32u, "NtUserGetQueueIocp");
        HWND h = make_window();
        DWORD v = 0x5a5a5a5a;
        ULONG_PTR first;
        CALL(init2, h, &v, 0, 0);
        REPORT("ret");
        first = r;
        put("close=%d ", CloseHandle((HANDLE)r));
        r = getq();
        classify("getq_after_close", r);
        put("same_value=%d ", r == first);
    } else if (!wcscmp(what, L"init1_then_init2")) {
        HWND h = make_window();
        DWORD v = 0x5a5a5a5a;
        CALL(drain1, 0, 0, 0, 0);
        REPORT("drain1_before");
        CALL(init1, h, 0, 0, 0);
        REPORT("init1");
        CALL(drain1, 0, 0, 0, 0);
        REPORT("drain1");
        CALL(drain2, h, 0, 0, 0);
        REPORT("drain2");
        CALL(init1, h, 0, 0, 0);
        REPORT("init1_again");
        CALL(init2, h, &v, 0, 0);
        REPORT("init2");
        put("v=%lx ", v);
    } else if (!wcscmp(what, L"init2_then_init1")) {
        HWND h = make_window();
        DWORD v = 0x5a5a5a5a;
        CALL(init2, h, &v, 0, 0);
        REPORT("init2");
        CALL(init1, h, 0, 0, 0);
        REPORT("init1");
    } else if (!wcscmp(what, L"init1_other_thread_hwnd")) {
        HANDLE ev = CreateEventW(NULL, FALSE, FALSE, NULL);
        HANDLE t = CreateThread(NULL, 0, other_thread, ev, 0, NULL);
        WaitForSingleObject(ev, INFINITE);
        CALL(init1, other_hwnd, 0, 0, 0);
        REPORT("ret");
        CALL(init1, GetDesktopWindow(), 0, 0, 0);
        REPORT("desktop");
        PostMessageW(other_hwnd, WM_QUIT, 0, 0);
        WaitForSingleObject(t, 2000);
    } else if (!wcscmp(what, L"drain2_other_thread_hwnd")) {
        HANDLE ev = CreateEventW(NULL, FALSE, FALSE, NULL);
        HANDLE t = CreateThread(NULL, 0, other_thread, ev, 0, NULL);
        HWND h = make_window();
        DWORD v = 0x5a5a5a5a;
        WaitForSingleObject(ev, INFINITE);
        CALL(init2, h, &v, 0, 0);
        REPORT("init2");
        CALL(drain2, other_hwnd, 0, 0, 0);
        REPORT("drain2_other");
        CALL(drain2, GetDesktopWindow(), 0, 0, 0);
        REPORT("drain2_desktop");
        DestroyWindow(h);
        CALL(drain2, h, 0, 0, 0);
        REPORT("drain2_destroyed");
        CALL(drain1, 0, 0, 0, 0);
        REPORT("drain1_after_destroy");
        PostMessageW(other_hwnd, WM_QUIT, 0, 0);
        WaitForSingleObject(t, 2000);
    } else if (!wcscmp(what, L"close_port_error")) {
        fn0 getq = (fn0)(void (*)(void))GetProcAddress(win32u, "NtUserGetQueueIocp");
        HWND h = make_window();
        BOOL ok;
        DWORD flags = 0xdead;
        r = getq();
        SetLastError(ORACLE_SENTINEL);
        ok = CloseHandle((HANDLE)r);
        put("close=%d err=%lu ", ok, GetLastError());
        put("info=%d ", GetHandleInformation((HANDLE)r, &flags));
        DestroyWindow(h);
    } else if (!wcscmp(what, L"rebind_after_destroy")) {
        fn0 getq = (fn0)(void (*)(void))GetProcAddress(win32u, "NtUserGetQueueIocp");
        HWND h = make_window(), h2 = make_window(), h3;
        DWORD v = 0x5a5a5a5a;
        ULONG_PTR port;
        CALL(init2, h, &v, 0, 0);
        REPORT("init2");
        port = r;
        CALL(drain2, h2, 0, 0, 0);
        REPORT("drain2_sibling");
        DestroyWindow(h2);
        CALL(drain1, 0, 0, 0, 0);
        REPORT("drain1_after_sibling_destroyed");
        DestroyWindow(h);
        CALL(drain1, 0, 0, 0, 0);
        REPORT("drain1_after_destroy");
        h3 = make_window();
        v = 0x5a5a5a5a;
        CALL(init2, h3, &v, 0, 0);
        REPORT("init2_again");
        put("v=%lx same_port=%d getq_same=%d ", v, r == port, getq() == port);
        CALL(drain2, h3, 0, 0, 0);
        REPORT("drain2_new");
    } else if (!wcscmp(what, L"child_window_init")) {
        HWND h = make_window();
        HWND c = CreateWindowExW(0, L"SetuCoreMsg", L"c", WS_CHILD, 0, 0, 10, 10, h, NULL, NULL, NULL);
        DWORD v = 0x5a5a5a5a;
        CALL(init2, c, &v, 0, 0);
        REPORT("init2_child");
        CALL(drain2, h, 0, 0, 0);
        REPORT("drain2_parent");
        DestroyWindow(c);
        CALL(drain1, 0, 0, 0, 0);
        REPORT("drain1_after_child_destroyed");
    } else if (!wcscmp(what, L"init2_zero")) {
        CALL(init2, 0, 0, 0, 0);
        REPORT("ret");
    } else if (!wcscmp(what, L"init2_zero_gui")) {
        HWND h = CreateWindowExW(0, L"STATIC", L"x", 0, 0, 0, 10, 10, NULL, NULL, NULL, NULL);
        CALL(init2, 0, 0, 0, 0);
        REPORT("ret");
        DestroyWindow(h);
    } else if (!wcscmp(what, L"init2_twice")) {
        CALL(init2, 0, 0, 0, 0);
        REPORT("ret1");
        CALL(init2, 0, 0, 0, 0);
        REPORT("ret2");
    } else if (!wcscmp(what, L"init2_own_hwnd")) {
        HWND h = make_window();
        CALL(init2, h, 0, 0, 0);
        REPORT("ret");
        after_init(h, r);
    } else if (!wcscmp(what, L"init2_own_hwnd_twice")) {
        HWND h = make_window();
        ULONG_PTR first;
        CALL(init2, h, 0, 0, 0);
        REPORT("ret1");
        first = r;
        CALL(init2, h, 0, 0, 0);
        REPORT("ret2");
        put("same=%d ", first == r);
    } else if (!wcscmp(what, L"init2_destroyed_hwnd")) {
        HWND h = make_window();
        DestroyWindow(h);
        CALL(init2, h, 0, 0, 0);
        REPORT("ret");
    } else if (!wcscmp(what, L"init2_other_thread_hwnd")) {
        HANDLE ev = CreateEventW(NULL, FALSE, FALSE, NULL);
        HANDLE t = CreateThread(NULL, 0, other_thread, ev, 0, NULL);
        WaitForSingleObject(ev, INFINITE);
        CALL(init2, other_hwnd, 0, 0, 0);
        REPORT("ret");
        PostMessageW(other_hwnd, WM_QUIT, 0, 0);
        WaitForSingleObject(t, 2000);
    } else if (!wcscmp(what, L"init2_desktop_hwnd")) {
        CALL(init2, GetDesktopWindow(), 0, 0, 0);
        REPORT("ret");
    } else if (!wcscmp(what, L"init2_message_only")) {
        HWND h = CreateWindowExW(0, L"STATIC", L"x", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, NULL, NULL);
        CALL(init2, h, 0, 0, 0);
        REPORT("ret");
    } else if (!wcscmp(what, L"drain2_after_init")) {
        HWND h = make_window();
        CALL(init2, h, 0, 0, 0);
        REPORT("init");
        CALL(drain2, h, 0, 0, 0);
        REPORT("drain");
        CALL(drain1, 0, 0, 0, 0);
        REPORT("drain1");
    } else if (!wcscmp(what, L"init1_own_hwnd")) {
        HWND h = make_window();
        CALL(init1, h, 0, 0, 0);
        REPORT("ret");
        after_init(h, r);
    } else if (!wcscmp(what, L"queueiocp_fresh")) {
        fn0 getq = (fn0)(void (*)(void))GetProcAddress(win32u, "NtUserGetQueueIocp");
        HWND h = make_window();
        SetLastError(ORACLE_SENTINEL);
        r = getq();
        err = GetLastError();
        REPORT("ret");
        DestroyWindow(h);
    } else if (!wcscmp(what, L"drain2_zero")) {
        CALL(drain2, 0, 0, 0, 0);
        REPORT("ret");
    } else if (!wcscmp(what, L"init1_zero")) {
        CALL(init1, 0, 0, 0, 0);
        REPORT("ret");
    } else if (!wcscmp(what, L"drain1_zero")) {
        CALL(drain1, 0, 0, 0, 0);
        REPORT("ret");
    }
    put("end");
    return 0;
}

/* ---- parent side -------------------------------------------------------- */

static void spawn_case(const char *name)
{
    wchar_t cmd[MAX_PATH + 64];
    STARTUPINFOW si = {0};
    PROCESS_INFORMATION pi;
    SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};
    HANDLE rd, wr;
    char buf[4096];
    DWORD n, total = 0, code = 0;

    CreatePipe(&rd, &wr, &sa, 0);
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = wr;
    si.hStdError = wr;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    swprintf(cmd, MAX_PATH + 64, L"\"%ls\" %hs", self, name);
    oracle_case(name);
    if (!CreateProcessW(self, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        o_str("spawn", "failed");
        oracle_case_end();
        CloseHandle(rd); CloseHandle(wr);
        return;
    }
    CloseHandle(wr);
    while (total < sizeof(buf) - 1 && ReadFile(rd, buf + total, sizeof(buf) - 1 - total, &n, NULL) && n) total += n;
    buf[total] = 0;
    if (WaitForSingleObject(pi.hProcess, 20000) == WAIT_TIMEOUT) { TerminateProcess(pi.hProcess, 1); o_str("child", "hung"); }
    GetExitCodeProcess(pi.hProcess, &code);
    o_str("out", buf);
    o_hex32("exit", code);
    oracle_case_end();
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread); CloseHandle(rd);
}

static void export_case(const char *name, int ord, const char *w32name)
{
    FARPROC u = GetProcAddress(user32, MAKEINTRESOURCEA(ord));
    FARPROC w = win32u ? GetProcAddress(win32u, w32name) : NULL;
    oracle_case(name);
    o_bool("user32_ordinal", u != NULL);
    o_bool("win32u_export", w != NULL);
    o_bool("same_address", u && u == w);
    oracle_case_end();
}

int main(void)
{
    typedef struct { int newmode; } startinfo;
    extern int __cdecl __wgetmainargs(int *, wchar_t ***, wchar_t ***, int, startinfo *);
    int argc = 0;
    wchar_t **argv = NULL, **envp = NULL;
    startinfo sinfo = {0};

    user32 = LoadLibraryW(L"user32.dll");
    win32u = LoadLibraryW(L"win32u.dll");
    __wgetmainargs(&argc, &argv, &envp, 0, &sinfo);
    if (argc > 1) return child_main(argv[1]);

    oracle_begin("user32/coremsg_iocp");
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    GetModuleFileNameW(NULL, self, MAX_PATH);

    export_case("export_init2", 2669, "NtUserInitThreadCoreMessagingIocp2");
    export_case("export_drain2", 2670, "NtUserDrainThreadCoreMessagingCompletions2");
    export_case("export_init1", 2612, "NtUserInitThreadCoreMessagingIocp");
    export_case("export_drain1", 2613, "NtUserDrainThreadCoreMessagingCompletions");
    oracle_case("export_getqueueiocp");
    o_bool("win32u_export", win32u && GetProcAddress(win32u, "NtUserGetQueueIocp") != NULL);
    oracle_case_end();

    spawn_case("init2_zero");
    spawn_case("init2_zero_gui");
    spawn_case("init2_twice");
    spawn_case("init2_own_hwnd");
    spawn_case("init2_own_hwnd_twice");
    spawn_case("init2_destroyed_hwnd");
    spawn_case("init2_other_thread_hwnd");
    spawn_case("init2_desktop_hwnd");
    spawn_case("init2_message_only");
    spawn_case("drain2_after_init");
    spawn_case("init1_own_hwnd");
    spawn_case("queueiocp_fresh");
    spawn_case("args2_h_1");
    spawn_case("args2_h_p");
    spawn_case("args2_h_port");
    spawn_case("args2_h_qport");
    spawn_case("args2_h_0_p");
    spawn_case("args2_h_1_p");
    spawn_case("args2_h_port_p");
    spawn_case("args2_h_p_p");
    spawn_case("args2_h_0_0_p");
    spawn_case("args2_h_p_0_0");
    spawn_case("args1_h_p");
    spawn_case("init2_preset");
    spawn_case("init2_pending_post");
    spawn_case("init2_bad_ptr");
    spawn_case("init2_other_thread_hwnd_p");
    spawn_case("init2_message_only_p");
    spawn_case("init2_desktop_p");
    spawn_case("drain2_after_init_p");
    spawn_case("wake_post");
    spawn_case("wake_send");
    spawn_case("wake_threadmsg");
    spawn_case("two_threads");
    spawn_case("close_port");
    spawn_case("init1_then_init2");
    spawn_case("init2_then_init1");
    spawn_case("init1_other_thread_hwnd");
    spawn_case("drain2_other_thread_hwnd");
    spawn_case("close_port_error");
    spawn_case("rebind_after_destroy");
    spawn_case("child_window_init");
    spawn_case("drain2_zero");
    spawn_case("init1_zero");
    spawn_case("drain1_zero");
    return oracle_end();
}
