/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: the thread message queue: SendMessageW/PostMessageW to the
 * same thread, posted vs sent ordering, PostThreadMessageW, PeekMessageW
 * message-range and window filters (including hWnd = -1), PM_NOREMOVE,
 * WM_QUIT handling (PostQuitMessage vs a posted WM_QUIT, GetMessageW's
 * return value), RegisterWindowMessageW, SetTimer with a 0 elapse and
 * KillTimer, GetQueueStatus bits, MsgWaitForMultipleObjects with a 0
 * timeout, SendMessageTimeoutW / SendNotifyMessageW on the same thread,
 * and a message sent from another thread being delivered inside PeekMessage
 * before posted messages.
 *
 * All windows are message-only (HWND_MESSAGE). Timing is never recorded:
 * waits are bounded loops that only report whether the message arrived.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendmessagew
 *       https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-postmessagew
 *       https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-postthreadmessagew
 *       https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-peekmessagew
 *       https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getmessagew
 *       https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-postquitmessage
 *       https://learn.microsoft.com/en-us/windows/win32/winmsg/wm-quit
 *       https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerwindowmessagew
 *       https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-settimer
 *       https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-killtimer
 *       https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getqueuestatus
 *       https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-msgwaitformultipleobjects
 *       https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendmessagetimeoutw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendnotifymessagew
 *       https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-insendmessage
 *       https://learn.microsoft.com/en-us/windows/win32/winmsg/about-messages-and-message-queues
 */
#include "oracle.h"

#define DECL(f) static __typeof__(f) *p##f
#define LOAD(m, f) (*(FARPROC *)&p##f = GetProcAddress(m, #f))

DECL(RegisterClassExW);
DECL(CreateWindowExW);
DECL(DestroyWindow);
DECL(DefWindowProcW);
DECL(SendMessageW);
DECL(PostMessageW);
DECL(PostThreadMessageW);
DECL(PeekMessageW);
DECL(GetMessageW);
DECL(DispatchMessageW);
DECL(PostQuitMessage);
DECL(RegisterWindowMessageW);
DECL(RegisterClipboardFormatW);
DECL(SetTimer);
DECL(KillTimer);
DECL(GetQueueStatus);
DECL(MsgWaitForMultipleObjects);
DECL(SendMessageTimeoutW);
DECL(SendNotifyMessageW);
DECL(InSendMessage);

static HINSTANCE self;
static HWND wa, wb;
static char log_[2048];
#define BAD_HWND ((HWND)(ULONG_PTR)0x0bad0bad)
#define MSG_BASE (WM_USER + 100)

static void logf_(const char *fmt, ...)
{
    char e[96];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(e, sizeof e, fmt, ap);
    va_end(ap);
    if (log_[0]) strncat(log_, ",", sizeof log_ - strlen(log_) - 1);
    strncat(log_, e, sizeof log_ - strlen(log_) - 1);
}

static const char *wname(HWND h) { return h == wa ? "a" : h == wb ? "b" : h ? "other" : "null"; }

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m >= MSG_BASE && m < MSG_BASE + 100) {
        logf_("%s:%u:%llu:%s", wname(h), m - MSG_BASE, (unsigned long long)w,
              pInSendMessage() ? "sent" : "direct");
        return (LRESULT)w * 2 + 1;
    }
    if (m == WM_TIMER) {
        logf_("%s:timer:%llu", wname(h), (unsigned long long)w);
        return 0;
    }
    return pDefWindowProcW(h, m, w, l);
}

static void drain(void)
{
    MSG msg;
    while (pPeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        ;
    pGetQueueStatus(QS_ALLINPUT); /* clear the "new" bits too */
}

/* Remove (PM_REMOVE) everything matching the filter, log it, dispatch it. */
static void pump(HWND filter, UINT lo, UINT hi)
{
    MSG msg;
    int guard = 0;
    while (guard++ < 64 && pPeekMessageW(&msg, filter, lo, hi, PM_REMOVE)) {
        if (msg.message >= MSG_BASE && msg.message < MSG_BASE + 100)
            logf_("peek:%s:%u:%llu", wname(msg.hwnd), msg.message - MSG_BASE, (unsigned long long)msg.wParam);
        else if (msg.message == WM_QUIT)
            logf_("peek:quit:%llu", (unsigned long long)msg.wParam);
        else if (msg.message == WM_TIMER)
            logf_("peek:%s:timer", wname(msg.hwnd));
        else
            continue; /* unrelated system messages are not logged */
        pDispatchMessageW(&msg);
    }
}

static void log_case(const char *name)
{
    oracle_case(name);
    o_str("log", log_);
    oracle_case_end();
    log_[0] = 0;
}

static void post(HWND h, UINT n, WPARAM w) { pPostMessageW(h, MSG_BASE + n, w, 0); }

static void qs_case(const char *name, UINT flags)
{
    DWORD r;
    ORACLE_CALL(r, pGetQueueStatus(flags));
    oracle_case(name);
    o_hex32("ret", r);
    oracle_case_end();
}

static DWORD WINAPI sender_thread(LPVOID p)
{
    (void)p;
    pSendMessageW(wa, MSG_BASE + 50, 7, 0);
    return 0;
}

int main(void)
{
    oracle_begin("user32/messages");
    HMODULE u32;
    ORACLE_CALL(u32, LoadLibraryW(L"user32.dll"));
    oracle_case("load_user32");
    o_handle("ret", u32);
    oracle_case_end();
    if (!u32) return oracle_end();
    LOAD(u32, RegisterClassExW);
    LOAD(u32, CreateWindowExW);
    LOAD(u32, DestroyWindow);
    LOAD(u32, DefWindowProcW);
    LOAD(u32, SendMessageW);
    LOAD(u32, PostMessageW);
    LOAD(u32, PostThreadMessageW);
    LOAD(u32, PeekMessageW);
    LOAD(u32, GetMessageW);
    LOAD(u32, DispatchMessageW);
    LOAD(u32, PostQuitMessage);
    LOAD(u32, RegisterWindowMessageW);
    LOAD(u32, RegisterClipboardFormatW);
    LOAD(u32, SetTimer);
    LOAD(u32, KillTimer);
    LOAD(u32, GetQueueStatus);
    LOAD(u32, MsgWaitForMultipleObjects);
    LOAD(u32, SendMessageTimeoutW);
    LOAD(u32, SendNotifyMessageW);
    LOAD(u32, InSendMessage);
    self = GetModuleHandleW(NULL);

    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = wndproc;
    wc.hInstance = self;
    wc.lpszClassName = L"SetuOracleMsg";
    pRegisterClassExW(&wc);
    wa = pCreateWindowExW(0, L"SetuOracleMsg", L"a", 0, 0, 0, 1, 1, HWND_MESSAGE, NULL, self, NULL);
    wb = pCreateWindowExW(0, L"SetuOracleMsg", L"b", 0, 0, 0, 1, 1, HWND_MESSAGE, NULL, self, NULL);
    oracle_case("setup");
    o_bool("windows", wa && wb);
    oracle_case_end();
    drain();

    /* ---- SendMessage on the same thread ---- */
    {
        LRESULT r;
        ORACLE_CALL(r, pSendMessageW(wa, MSG_BASE + 1, 20, 0));
        oracle_case("send_same_thread");
        o_i64("ret", r);
        o_lasterr();
        o_str("log", log_);
        oracle_case_end();
        log_[0] = 0;
        ORACLE_CALL(r, pSendMessageW(BAD_HWND, MSG_BASE + 1, 1, 0));
        oracle_case("send_bad_hwnd");
        o_i64("ret", r);
        o_lasterr();
        oracle_case_end();
        BOOL b;
        ORACLE_CALL(b, pPostMessageW(BAD_HWND, MSG_BASE + 1, 1, 0));
        oracle_case("post_bad_hwnd");
        o_bool("ret", b);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(b, pPostMessageW(wa, WM_SETTEXT, 0, (LPARAM)L"x"));
        oracle_case("post_wm_settext_sync_only");
        o_bool("ret", b);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(b, pPostMessageW(wa, WM_GETTEXT, 0, 0));
        oracle_case("post_wm_gettext_sync_only");
        o_bool("ret", b);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(b, pPostThreadMessageW(GetCurrentThreadId(), WM_COPYDATA, 0, 0));
        oracle_case("postthread_wm_copydata_sync_only");
        o_bool("ret", b);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(b, pPostThreadMessageW(0x7ffffff0, MSG_BASE, 0, 0));
        oracle_case("postthread_bad_tid");
        o_bool("ret", b);
        o_lasterr();
        oracle_case_end();
        drain();
        ULONG_PTR res = 0xdead;
        ORACLE_CALL(r, pSendMessageTimeoutW(wa, MSG_BASE + 2, 5, 0, SMTO_NORMAL, 1000, &res));
        oracle_case("sendmessagetimeout_same_thread");
        o_i64("ret", r);
        o_u64("result", res);
        o_lasterr();
        o_str("log", log_);
        oracle_case_end();
        log_[0] = 0;
        ORACLE_CALL(b, pSendNotifyMessageW(wa, MSG_BASE + 3, 6, 0));
        oracle_case("sendnotify_same_thread");
        o_bool("ret", b);
        o_lasterr();
        o_str("log_before_pump", log_);
        oracle_case_end();
        log_[0] = 0;
        drain();
    }

    /* ---- ordering of posted vs sent ---- */
    post(wa, 10, 1);
    post(wb, 10, 2);
    post(wa, 10, 3);
    pSendMessageW(wa, MSG_BASE + 11, 4, 0);
    pPostThreadMessageW(GetCurrentThreadId(), MSG_BASE + 12, 5, 0);
    pump(NULL, 0, 0);
    log_case("posted_fifo_sent_immediate");

    /* PeekMessage filters */
    post(wa, 20, 1);
    post(wb, 21, 2);
    post(wa, 22, 3);
    pPostThreadMessageW(GetCurrentThreadId(), MSG_BASE + 23, 4, 0);
    pump(NULL, MSG_BASE + 22, MSG_BASE + 22);
    log_case("filter_range_single");
    pump(wb, 0, 0);
    log_case("filter_hwnd_b");
    pump((HWND)-1, 0, 0);
    log_case("filter_hwnd_minus1_thread_only");
    pump(NULL, MSG_BASE + 30, MSG_BASE + 10);
    log_case("filter_range_inverted");
    pump(NULL, 0, 0);
    log_case("filter_rest");

    post(wa, 24, 1);
    {
        MSG msg;
        BOOL r;
        memset(&msg, 0, sizeof msg);
        ORACLE_CALL(r, pPeekMessageW(&msg, NULL, 0, 0, PM_NOREMOVE));
        oracle_case("peek_noremove");
        o_bool("ret", r);
        o_u32("msg", msg.message - MSG_BASE);
        o_str("hwnd", wname(msg.hwnd));
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, pPeekMessageW(&msg, BAD_HWND, 0, 0, PM_NOREMOVE));
        oracle_case("peek_bad_hwnd");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        pump(NULL, 0, 0);
        log_case("peek_noremove_left_it");
        ORACLE_CALL(r, pPeekMessageW(&msg, NULL, 0, 0, PM_REMOVE));
        oracle_case("peek_empty");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
    }

    /* ---- WM_QUIT ---- */
    drain();
    pPostQuitMessage(5);
    post(wa, 30, 1);
    pPostThreadMessageW(GetCurrentThreadId(), MSG_BASE + 31, 2, 0);
    pump(NULL, 0, 0);
    log_case("postquit_after_other_messages");
    {
        MSG msg;
        BOOL r;
        ORACLE_CALL(r, pPeekMessageW(&msg, NULL, 0, 0, PM_REMOVE));
        oracle_case("postquit_consumed_once");
        o_bool("ret", r);
        oracle_case_end();
    }
    pPostQuitMessage(6);
    pPostQuitMessage(7);
    pump(NULL, 0, 0);
    log_case("postquit_twice_last_wins");

    pPostMessageW(wa, WM_QUIT, 8, 0);
    post(wa, 32, 1);
    pump(NULL, 0, 0);
    log_case("posted_wm_quit_in_order");

    pPostQuitMessage(9);
    pump(NULL, MSG_BASE, MSG_BASE + 99);
    log_case("postquit_filtered_out");
    pump(wa, 0, 0);
    log_case("postquit_hwnd_filter");
    pump(NULL, 0, 0);
    log_case("postquit_unfiltered");

    {
        MSG msg;
        BOOL r;
        pPostQuitMessage(11);
        memset(&msg, 0, sizeof msg);
        ORACLE_CALL(r, pGetMessageW(&msg, NULL, 0, 0));
        oracle_case("getmessage_quit");
        o_i64("ret", r);
        o_u32("message", msg.message);
        o_u64("wparam", msg.wParam);
        o_str("hwnd", wname(msg.hwnd));
        oracle_case_end();
        post(wa, 33, 12);
        ORACLE_CALL(r, pGetMessageW(&msg, NULL, 0, 0));
        oracle_case("getmessage_normal");
        o_i64("ret", r);
        o_u32("message", msg.message - MSG_BASE);
        o_str("hwnd", wname(msg.hwnd));
        oracle_case_end();
    }

    /* ---- RegisterWindowMessageW ---- */
    {
        UINT m1, m2, m3, m4, m5, m6;
        ORACLE_CALL(m1, pRegisterWindowMessageW(L"SetuOracleMessage"));
        oracle_case("registerwindowmessage_first");
        o_bool("in_range", m1 >= 0xC000 && m1 <= 0xFFFF);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(m2, pRegisterWindowMessageW(L"SetuOracleMessage"));
        m3 = pRegisterWindowMessageW(L"SETUORACLEMESSAGE");
        m4 = pRegisterWindowMessageW(L"SetuOracleMessage2");
        m6 = pRegisterClipboardFormatW(L"SetuOracleMessage");
        oracle_case("registerwindowmessage_repeat");
        o_bool("same_string_same_id", m1 == m2);
        o_bool("case_insensitive", m1 == m3);
        o_bool("different_string_differs", m1 != m4);
        o_bool("clipboard_format_same_id", m1 == m6);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(m5, pRegisterWindowMessageW(L""));
        oracle_case("registerwindowmessage_empty");
        o_u32("ret", m5);
        o_lasterr();
        oracle_case_end();
        wchar_t lng[300];
        for (int i = 0; i < 299; i++) lng[i] = L'm';
        lng[299] = 0;
        ORACLE_CALL(m5, pRegisterWindowMessageW(lng));
        oracle_case("registerwindowmessage_299_chars");
        o_bool("nonzero", m5 != 0);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(m5, pRegisterWindowMessageW(L"#123"));
        oracle_case("registerwindowmessage_hash_number");
        o_bool("in_range", m5 >= 0xC000 && m5 <= 0xFFFF);
        o_u32("ret_if_small", m5 < 0xC000 ? m5 : 0);
        o_lasterr();
        oracle_case_end();
    }

    /* ---- GetQueueStatus / MsgWaitForMultipleObjects ---- */
    drain();
    qs_case("queuestatus_empty", QS_ALLINPUT);
    post(wa, 40, 1);
    qs_case("queuestatus_after_post", QS_ALLINPUT);
    qs_case("queuestatus_after_post_again", QS_ALLINPUT);
    qs_case("queuestatus_postmessage_only", QS_POSTMESSAGE);
    qs_case("queuestatus_timer_only", QS_TIMER);
    {
        DWORD r;
        post(wa, 41, 2);
        ORACLE_CALL(r, pMsgWaitForMultipleObjects(0, NULL, FALSE, 0, QS_POSTMESSAGE));
        oracle_case("msgwait_new_post");
        o_u32("ret", r);
        oracle_case_end();
        pGetQueueStatus(QS_ALLINPUT);
        ORACLE_CALL(r, pMsgWaitForMultipleObjects(0, NULL, FALSE, 0, QS_POSTMESSAGE));
        oracle_case("msgwait_old_post");
        o_u32("ret", r);
        oracle_case_end();
        HANDLE ev = CreateEventW(NULL, TRUE, TRUE, NULL);
        ORACLE_CALL(r, pMsgWaitForMultipleObjects(1, &ev, FALSE, 0, QS_ALLINPUT));
        oracle_case("msgwait_signaled_handle_first");
        o_u32("ret", r);
        oracle_case_end();
        CloseHandle(ev);
    }
    pump(NULL, 0, 0);
    log_case("queuestatus_pump");
    qs_case("queuestatus_after_pump", QS_ALLINPUT);

    /* ---- timers ---- */
    {
        UINT_PTR id;
        ORACLE_CALL(id, pSetTimer(wa, 1, 0, NULL));
        oracle_case("settimer_elapse_0");
        o_u64("ret", id);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(id, pSetTimer(wa, 1, 0, NULL));
        oracle_case("settimer_replace_same_id");
        o_u64("ret", id);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(id, pSetTimer(BAD_HWND, 1, 0, NULL));
        oracle_case("settimer_bad_hwnd");
        o_u64("ret", id);
        o_lasterr();
        oracle_case_end();
        /* wait (bounded) for the timer; a post made meanwhile comes first */
        MSG msg;
        int got = 0;
        for (int i = 0; i < 300 && !got; i++) {
            if (pPeekMessageW(&msg, wa, WM_TIMER, WM_TIMER, PM_NOREMOVE)) got = 1;
            else Sleep(10);
        }
        qs_case("queuestatus_timer_due", QS_TIMER);
        post(wa, 42, 1);
        pump(NULL, 0, 0);
        oracle_case("timer_fired");
        o_bool("got", got);
        oracle_case_end();
        /* The log holds the post, then one timer (timers never queue twice). */
        log_case("timer_after_post");
        BOOL r;
        ORACLE_CALL(r, pKillTimer(wa, 1));
        oracle_case("killtimer");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, pKillTimer(wa, 1));
        oracle_case("killtimer_twice");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, pKillTimer(wa, 999));
        oracle_case("killtimer_unknown_id");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, pKillTimer(BAD_HWND, 1));
        oracle_case("killtimer_bad_hwnd");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(id, pSetTimer(NULL, 0, 0, NULL));
        oracle_case("settimer_thread_timer");
        o_bool("ret_nonzero", id != 0);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, pKillTimer(NULL, id));
        oracle_case("killtimer_thread_timer");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(id, pSetTimer(wa, 0, 0, NULL));
        oracle_case("settimer_window_id_0");
        o_u64("ret", id);
        o_lasterr();
        oracle_case_end();
        pKillTimer(wa, 0);
        drain();
        log_[0] = 0;

        /* Probe: what a PM_NOREMOVE peek does to a due timer. */
        pSetTimer(wa, 2, 0, NULL);
        got = 0;
        for (int i = 0; i < 300 && !got; i++) {
            if (pPeekMessageW(&msg, wa, WM_TIMER, WM_TIMER, PM_NOREMOVE)) got = 1;
            else Sleep(10);
        }
        oracle_case("timer_probe_peeked");
        o_bool("got", got);
        o_hex32("qs_post", pGetQueueStatus(QS_POSTMESSAGE));
        o_hex32("qs_timer", pGetQueueStatus(QS_TIMER));
        o_hex32("qs_all", pGetQueueStatus(QS_ALLINPUT));
        oracle_case_end();
        pKillTimer(wa, 2);
        pump(NULL, 0, 0);
        log_case("timer_probe_killed_after_peek");

        /* Probe: a due timer that was never peeked, then a post. */
        pSetTimer(wa, 3, 0, NULL);
        Sleep(200);
        oracle_case("timer_probe_unpeeked");
        o_hex32("qs_timer", pGetQueueStatus(QS_TIMER));
        o_hex32("qs_post", pGetQueueStatus(QS_POSTMESSAGE));
        oracle_case_end();
        post(wa, 43, 1);
        pump(NULL, 0, 0);
        pKillTimer(wa, 3);
        log_case("timer_probe_unpeeked_then_post");
        drain();
        log_[0] = 0;
    }

    /* ---- message sent from another thread is delivered inside PeekMessage ---- */
    {
        post(wa, 60, 1);
        HANDLE th = CreateThread(NULL, 0, sender_thread, NULL, 0, NULL);
        int pending = 0;
        for (int i = 0; i < 500 && !pending; i++) {
            if (HIWORD(pGetQueueStatus(QS_SENDMESSAGE)) & QS_SENDMESSAGE) pending = 1;
            else Sleep(10);
        }
        MSG msg;
        BOOL r = pPeekMessageW(&msg, NULL, 0, 0, PM_REMOVE);
        if (r && msg.message >= MSG_BASE && msg.message < MSG_BASE + 100)
            logf_("peek:%s:%u", wname(msg.hwnd), msg.message - MSG_BASE);
        oracle_case("cross_thread_send_before_post");
        o_bool("send_was_pending", pending);
        o_bool("peek_ret", r);
        o_str("log", log_);
        oracle_case_end();
        log_[0] = 0;
        WaitForSingleObject(th, 10000);
        CloseHandle(th);
        drain();
    }

    /* ---- messages to a destroyed window ---- */
    pDestroyWindow(wb);
    {
        BOOL r;
        ORACLE_CALL(r, pPostMessageW(wb, MSG_BASE, 0, 0));
        oracle_case("post_destroyed_window");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        post(wa, 70, 1);
        HWND old = wb;
        wb = NULL;
        pDestroyWindow(wa);
        MSG msg;
        ORACLE_CALL(r, pPeekMessageW(&msg, NULL, 0, 0, PM_REMOVE));
        oracle_case("posted_to_window_destroyed_before_peek");
        o_bool("ret", r);
        o_bool("is_test_message", r && msg.message >= MSG_BASE && msg.message < MSG_BASE + 100);
        oracle_case_end();
        (void)old;
    }

    return oracle_end();
}
