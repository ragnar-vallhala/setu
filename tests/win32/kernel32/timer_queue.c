/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: timer queues: one-shot and periodic timers on private and
 * default queues, callback parameters, DeleteTimerQueueTimer with each kind
 * of completion event, ChangeTimerQueueTimer, DeleteTimerQueueEx, and bad
 * handles.
 *
 * Callbacks signal events; the test waits on those (bounded) instead of
 * sleeping. Periodic timers are deleted with INVALID_HANDLE_VALUE, which
 * waits for running callbacks, so the counts printed after that are stable.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/threadpoollegacyapiset/nf-threadpoollegacyapiset-createtimerqueue
 *       https://learn.microsoft.com/en-us/windows/win32/api/threadpoollegacyapiset/nf-threadpoollegacyapiset-createtimerqueuetimer
 *       https://learn.microsoft.com/en-us/windows/win32/api/threadpoollegacyapiset/nf-threadpoollegacyapiset-deletetimerqueuetimer
 *       https://learn.microsoft.com/en-us/windows/win32/api/threadpoollegacyapiset/nf-threadpoollegacyapiset-changetimerqueuetimer
 *       https://learn.microsoft.com/en-us/windows/win32/api/threadpoollegacyapiset/nf-threadpoollegacyapiset-deletetimerqueueex
 */
#include "oracle.h"

struct ctx {
    HANDLE ev;
    volatile LONG calls;
    LONG stop_after;
    BOOLEAN fired;
    void *param_seen;
    DWORD tid;
};

static void CALLBACK cb(void *param, BOOLEAN fired)
{
    struct ctx *c = param;
    c->param_seen = param;
    c->fired = fired;
    c->tid = GetCurrentThreadId();
    if (InterlockedIncrement(&c->calls) == c->stop_after) SetEvent(c->ev);
}

static void bool_case(const char *name, BOOL ok)
{
    oracle_case(name);
    o_bool("ret", ok);
    o_lasterr();
    oracle_case_end();
}

int main(void)
{
    oracle_begin("kernel32/timer_queue");
    HANDLE q, t;
    BOOL ok;
    struct ctx c;

    ORACLE_CALL(q, CreateTimerQueue());
    oracle_case("create_queue");
    o_bool("ret_null", q == NULL);
    o_lasterr();
    oracle_case_end();

    /* ---- one-shot ---- */
    memset(&c, 0, sizeof c);
    c.ev = CreateEventW(NULL, TRUE, FALSE, NULL);
    c.stop_after = 1;
    ORACLE_CALL(ok, CreateTimerQueueTimer(&t, q, cb, &c, 0, 0, 0));
    bool_case("create_one_shot", ok);
    {
        DWORD w = WaitForSingleObject(c.ev, 10000);
        ok = DeleteTimerQueueTimer(q, t, INVALID_HANDLE_VALUE);
        oracle_case("one_shot_fired");
        o_bool("signalled", w == WAIT_OBJECT_0);
        o_i64("calls", c.calls);
        o_bool("param", c.param_seen == &c);
        o_bool("timer_or_wait_fired", c.fired);
        o_bool("other_thread", c.tid != GetCurrentThreadId());
        o_bool("delete_ret", ok);
        oracle_case_end();
    }

    /* ---- periodic, deleted with a blocking wait ---- */
    memset(&c, 0, sizeof c);
    c.ev = CreateEventW(NULL, TRUE, FALSE, NULL);
    c.stop_after = 3;
    ORACLE_CALL(ok, CreateTimerQueueTimer(&t, q, cb, &c, 0, 10, 0));
    bool_case("create_periodic", ok);
    {
        DWORD w = WaitForSingleObject(c.ev, 10000);
        ORACLE_CALL(ok, DeleteTimerQueueTimer(q, t, INVALID_HANDLE_VALUE));
        LONG after = c.calls;
        /* No callback may run once the blocking delete has returned. */
        HANDLE probe = CreateEventW(NULL, TRUE, FALSE, NULL);
        HANDLE t2;
        struct ctx c2 = {probe, 0, 1, 0, 0, 0};
        CreateTimerQueueTimer(&t2, q, cb, &c2, 30, 0, 0);
        WaitForSingleObject(probe, 10000);
        DeleteTimerQueueTimer(q, t2, INVALID_HANDLE_VALUE);
        oracle_case("periodic_delete_blocking");
        o_bool("reached_3", w == WAIT_OBJECT_0);
        o_bool("ret", ok);
        o_lasterr();
        o_bool("no_calls_after_delete", c.calls == after);
        oracle_case_end();
        CloseHandle(probe);
    }

    /* ---- delete with an event ---- */
    {
        memset(&c, 0, sizeof c);
        c.ev = CreateEventW(NULL, TRUE, FALSE, NULL);
        c.stop_after = 1;
        HANDLE done = CreateEventW(NULL, TRUE, FALSE, NULL);
        CreateTimerQueueTimer(&t, q, cb, &c, 0, 0, 0);
        WaitForSingleObject(c.ev, 10000);
        ORACLE_CALL(ok, DeleteTimerQueueTimer(q, t, done));
        DWORD err = GetLastError();
        DWORD w = WaitForSingleObject(done, 10000);
        oracle_case("delete_with_event");
        /* Either completes at once or reports ERROR_IO_PENDING; both then signal. */
        o_bool("ret_or_pending", ok || err == ERROR_IO_PENDING);
        o_bool("event_signalled", w == WAIT_OBJECT_0);
        oracle_case_end();
        CloseHandle(done);
    }

    /* ---- change and long due times ---- */
    {
        memset(&c, 0, sizeof c);
        c.ev = CreateEventW(NULL, TRUE, FALSE, NULL);
        c.stop_after = 1;
        CreateTimerQueueTimer(&t, q, cb, &c, 3600000, 0, 0);
        ORACLE_CALL(ok, ChangeTimerQueueTimer(q, t, 0, 0));
        bool_case("change_to_now", ok);
        DWORD w = WaitForSingleObject(c.ev, 10000);
        oracle_case("changed_timer_fired");
        o_bool("signalled", w == WAIT_OBJECT_0);
        oracle_case_end();
        ORACLE_CALL(ok, ChangeTimerQueueTimer(q, t, 3600000, 0));
        bool_case("change_after_one_shot_fired", ok);
        ORACLE_CALL(ok, DeleteTimerQueueTimer(q, t, INVALID_HANDLE_VALUE));
        bool_case("delete_pending_long", ok);
    }

    /* ---- default queue and flags ---- */
    {
        memset(&c, 0, sizeof c);
        c.ev = CreateEventW(NULL, TRUE, FALSE, NULL);
        c.stop_after = 1;
        ORACLE_CALL(ok, CreateTimerQueueTimer(&t, NULL, cb, &c, 0, 0, WT_EXECUTEINTIMERTHREAD));
        bool_case("default_queue_timer_thread", ok);
        DWORD w = WaitForSingleObject(c.ev, 10000);
        ok = DeleteTimerQueueTimer(NULL, t, INVALID_HANDLE_VALUE);
        oracle_case("default_queue_fired");
        o_bool("signalled", w == WAIT_OBJECT_0);
        o_bool("delete_ret", ok);
        oracle_case_end();

        memset(&c, 0, sizeof c);
        c.ev = CreateEventW(NULL, TRUE, FALSE, NULL);
        c.stop_after = 1;
        ORACLE_CALL(ok, CreateTimerQueueTimer(&t, NULL, cb, &c, 0, 5, WT_EXECUTEONLYONCE));
        bool_case("execute_only_once_with_period", ok);
        WaitForSingleObject(c.ev, 10000);
        HANDLE probe = CreateEventW(NULL, TRUE, FALSE, NULL);
        HANDLE t2;
        struct ctx c2 = {probe, 0, 1, 0, 0, 0};
        CreateTimerQueueTimer(&t2, NULL, cb, &c2, 50, 0, 0);
        WaitForSingleObject(probe, 10000);
        DeleteTimerQueueTimer(NULL, t2, INVALID_HANDLE_VALUE);
        DeleteTimerQueueTimer(NULL, t, INVALID_HANDLE_VALUE);
        /* The period wins over WT_EXECUTEONLYONCE: the timer repeats (the
         * exact count depends on scheduling). */
        oracle_case("execute_only_once_calls");
        o_bool("repeated", c.calls > 1);
        oracle_case_end();
        CloseHandle(probe);
    }

    /* ---- bad parameters ---- */
    /* Timer queue handles are pointers, so passing another kind of handle
     * (or a NULL callback that later fires) crashes; not tested. */
    ORACLE_CALL(ok, DeleteTimerQueueTimer(q, NULL, INVALID_HANDLE_VALUE));
    bool_case("delete_null_timer", ok);
    ORACLE_CALL(ok, ChangeTimerQueueTimer(q, NULL, 0, 0));
    bool_case("change_null_timer", ok);

    /* ---- deleting the queue ---- */
    {
        memset(&c, 0, sizeof c);
        c.ev = CreateEventW(NULL, TRUE, FALSE, NULL);
        CreateTimerQueueTimer(&t, q, cb, &c, 3600000, 0, 0);
        ORACLE_CALL(ok, DeleteTimerQueueEx(q, INVALID_HANDLE_VALUE));
        bool_case("delete_queue_with_pending_timer", ok);
        oracle_case("deleted_queue_timer_never_ran");
        o_i64("calls", c.calls);
        oracle_case_end();
        ORACLE_CALL(ok, DeleteTimerQueueEx(NULL, INVALID_HANDLE_VALUE));
        bool_case("delete_null_queue", ok);
        HANDLE q2 = CreateTimerQueue();
        HANDLE done = CreateEventW(NULL, TRUE, FALSE, NULL);
        ORACLE_CALL(ok, DeleteTimerQueueEx(q2, done));
        DWORD err = GetLastError();
        DWORD w = WaitForSingleObject(done, 10000);
        oracle_case("delete_queue_with_event");
        o_bool("ret", ok);
        o_u32("error", ok ? 0 : err);
        o_bool("event_signalled", w == WAIT_OBJECT_0);
        oracle_case_end();
        HANDLE q3 = CreateTimerQueue();
        ORACLE_CALL(ok, DeleteTimerQueue(q3));
        bool_case("delete_queue_legacy", ok);
    }

    return oracle_end();
}
