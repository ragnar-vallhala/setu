/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: the thread pool API: work objects submitted several times,
 * simple callbacks, wait objects (signalled, timed out, re-armed), timers,
 * cleanup groups, callback-return actions (event, semaphore, mutex,
 * critical section), the legacy QueueUserWorkItem and
 * RegisterWaitForSingleObject, and private pools with thread limits.
 *
 * Every callback is joined with WaitForThreadpool*Callbacks or an event
 * before results are printed.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/procthread/thread-pool-api
 *       https://learn.microsoft.com/en-us/windows/win32/api/threadpoolapiset/nf-threadpoolapiset-createthreadpoolwork
 *       https://learn.microsoft.com/en-us/windows/win32/api/threadpoolapiset/nf-threadpoolapiset-setthreadpoolwait
 *       https://learn.microsoft.com/en-us/windows/win32/api/threadpoolapiset/nf-threadpoolapiset-setthreadpooltimer
 *       https://learn.microsoft.com/en-us/windows/win32/api/threadpoolapiset/nf-threadpoolapiset-closethreadpoolcleanupgroupmembers
 *       https://learn.microsoft.com/en-us/windows/win32/api/threadpoolapiset/nf-threadpoolapiset-seteventwhencallbackreturns
 *       https://learn.microsoft.com/en-us/windows/win32/api/threadpoollegacyapiset/nf-threadpoollegacyapiset-queueuserworkitem
 *       https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-registerwaitforsingleobject
 */
#include "oracle.h"

static volatile LONG work_calls;
static void *work_ctx_seen;
static void CALLBACK work_cb(PTP_CALLBACK_INSTANCE inst, void *ctx, PTP_WORK work)
{
    (void)inst;
    (void)work;
    work_ctx_seen = ctx;
    InterlockedIncrement(&work_calls);
}

static HANDLE simple_ev;
static void CALLBACK simple_cb(PTP_CALLBACK_INSTANCE inst, void *ctx)
{
    (void)inst;
    work_ctx_seen = ctx;
    SetEvent(simple_ev);
}

static volatile LONG wait_calls;
static TP_WAIT_RESULT wait_results[4];
static HANDLE wait_done;
static void CALLBACK wait_cb(PTP_CALLBACK_INSTANCE inst, void *ctx, PTP_WAIT wait, TP_WAIT_RESULT res)
{
    (void)inst;
    (void)ctx;
    (void)wait;
    LONG n = InterlockedIncrement(&wait_calls);
    if (n <= 4) wait_results[n - 1] = res;
    SetEvent(wait_done);
}

static volatile LONG timer_calls;
static HANDLE timer_ev;
static void CALLBACK timer_cb(PTP_CALLBACK_INSTANCE inst, void *ctx, PTP_TIMER timer)
{
    (void)inst;
    (void)ctx;
    (void)timer;
    if (InterlockedIncrement(&timer_calls) == 3) SetEvent(timer_ev);
}

/* Callback-return actions. */
static HANDLE act_event, act_sem, act_mutex;
static CRITICAL_SECTION act_cs;
static void CALLBACK actions_cb(PTP_CALLBACK_INSTANCE inst, void *ctx, PTP_WORK work)
{
    (void)ctx;
    (void)work;
    SetEventWhenCallbackReturns(inst, act_event);
    ReleaseSemaphoreWhenCallbackReturns(inst, act_sem, 2);
    WaitForSingleObject(act_mutex, INFINITE);
    ReleaseMutexWhenCallbackReturns(inst, act_mutex);
    EnterCriticalSection(&act_cs);
    LeaveCriticalSectionWhenCallbackReturns(inst, &act_cs);
}

static volatile LONG may_run_long_ret = -1;
static void CALLBACK may_run_long_cb(PTP_CALLBACK_INSTANCE inst, void *ctx, PTP_WORK work)
{
    (void)ctx;
    (void)work;
    may_run_long_ret = CallbackMayRunLong(inst);
    DisassociateCurrentThreadFromCallback(inst);
}

static volatile LONG legacy_calls;
static HANDLE legacy_ev;
static DWORD WINAPI legacy_cb(void *ctx)
{
    InterlockedIncrement(&legacy_calls);
    if (ctx == (void *)0x77) SetEvent(legacy_ev);
    return 0;
}

static volatile LONG rw_calls;
static BOOLEAN rw_timed_out[4];
static HANDLE rw_done;
static void CALLBACK rw_cb(void *ctx, BOOLEAN timed_out)
{
    (void)ctx;
    LONG n = InterlockedIncrement(&rw_calls);
    if (n <= 4) rw_timed_out[n - 1] = timed_out;
    SetEvent(rw_done);
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
    oracle_begin("kernel32/thread_pool");
    BOOL ok;

    /* ---- work ---- */
    {
        PTP_WORK w;
        ORACLE_CALL(w, CreateThreadpoolWork(work_cb, (void *)0x1234, NULL));
        oracle_case("create_work");
        o_bool("ret_null", w == NULL);
        o_lasterr();
        oracle_case_end();
        for (int i = 0; i < 5; i++) SubmitThreadpoolWork(w);
        WaitForThreadpoolWorkCallbacks(w, FALSE);
        oracle_case("work_submitted_5_times");
        o_i64("calls", work_calls);
        o_bool("ctx", work_ctx_seen == (void *)0x1234);
        oracle_case_end();
        WaitForThreadpoolWorkCallbacks(w, TRUE);
        CloseThreadpoolWork(w);

        /* A NULL callback crashes on Windows; not tested. */
    }

    simple_ev = CreateEventW(NULL, FALSE, FALSE, NULL);
    ORACLE_CALL(ok, TrySubmitThreadpoolCallback(simple_cb, (void *)0x55, NULL));
    bool_case("try_submit_simple", ok);
    oracle_case("simple_callback_ran");
    o_bool("signalled", WaitForSingleObject(simple_ev, 10000) == WAIT_OBJECT_0);
    o_bool("ctx", work_ctx_seen == (void *)0x55);
    oracle_case_end();

    /* ---- wait objects ---- */
    {
        HANDLE ev = CreateEventW(NULL, FALSE, FALSE, NULL);
        wait_done = CreateEventW(NULL, FALSE, FALSE, NULL);
        PTP_WAIT w = CreateThreadpoolWait(wait_cb, NULL, NULL);
        SetThreadpoolWait(w, ev, NULL);
        SetEvent(ev);
        WaitForSingleObject(wait_done, 10000);
        WaitForThreadpoolWaitCallbacks(w, FALSE);
        oracle_case("wait_signalled");
        o_i64("calls", wait_calls);
        o_hex32("result", wait_results[0]);
        oracle_case_end();
        /* The wait is one-shot: a second signal does nothing until re-armed. */
        SetEvent(ev);
        FILETIME ft;
        LARGE_INTEGER li;
        li.QuadPart = -10 * 1000 * 20; /* 20 ms relative */
        ft.dwLowDateTime = li.LowPart;
        ft.dwHighDateTime = (DWORD)li.HighPart;
        HANDLE never = CreateEventW(NULL, TRUE, FALSE, NULL);
        SetThreadpoolWait(w, never, &ft);
        WaitForSingleObject(wait_done, 10000);
        WaitForThreadpoolWaitCallbacks(w, FALSE);
        oracle_case("wait_timeout_after_rearm");
        o_i64("calls", wait_calls);
        o_hex32("result", wait_results[1]);
        o_bool("event_still_set", WaitForSingleObject(ev, 0) == WAIT_OBJECT_0);
        oracle_case_end();
        SetThreadpoolWait(w, NULL, NULL);
        WaitForThreadpoolWaitCallbacks(w, TRUE);
        CloseThreadpoolWait(w);
        CloseHandle(never);
        CloseHandle(ev);
    }

    /* ---- timers ---- */
    {
        timer_ev = CreateEventW(NULL, TRUE, FALSE, NULL);
        PTP_TIMER t = CreateThreadpoolTimer(timer_cb, NULL, NULL);
        oracle_case("timer_not_set_initially");
        o_bool("set", IsThreadpoolTimerSet(t));
        oracle_case_end();
        FILETIME due = {0, 0};
        SetThreadpoolTimer(t, &due, 5, 0);
        oracle_case("timer_set_periodic");
        o_bool("set", IsThreadpoolTimerSet(t));
        oracle_case_end();
        DWORD w = WaitForSingleObject(timer_ev, 10000);
        SetThreadpoolTimer(t, NULL, 0, 0);
        WaitForThreadpoolTimerCallbacks(t, TRUE);
        LONG after = timer_calls;
        oracle_case("timer_cancelled");
        o_bool("reached_3", w == WAIT_OBJECT_0);
        o_bool("set", IsThreadpoolTimerSet(t));
        o_bool("stable", after == timer_calls);
        oracle_case_end();
        CloseThreadpoolTimer(t);
    }

    /* ---- callback-return actions ---- */
    {
        act_event = CreateEventW(NULL, TRUE, FALSE, NULL);
        act_sem = CreateSemaphoreW(NULL, 0, 10, NULL);
        act_mutex = CreateMutexW(NULL, FALSE, NULL);
        InitializeCriticalSection(&act_cs);
        PTP_WORK w = CreateThreadpoolWork(actions_cb, NULL, NULL);
        SubmitThreadpoolWork(w);
        WaitForThreadpoolWorkCallbacks(w, FALSE);
        LONG prev = -1;
        ReleaseSemaphore(act_sem, 1, &prev);
        oracle_case("callback_return_actions");
        o_bool("event_set", WaitForSingleObject(act_event, 0) == WAIT_OBJECT_0);
        o_i64("sem_count", prev);
        o_bool("mutex_free", WaitForSingleObject(act_mutex, 0) == WAIT_OBJECT_0);
        o_bool("cs_free", TryEnterCriticalSection(&act_cs));
        oracle_case_end();
        ReleaseMutex(act_mutex);
        LeaveCriticalSection(&act_cs);
        CloseThreadpoolWork(w);

        w = CreateThreadpoolWork(may_run_long_cb, NULL, NULL);
        SubmitThreadpoolWork(w);
        WaitForThreadpoolWorkCallbacks(w, FALSE);
        oracle_case("callback_may_run_long");
        o_i64("ret", may_run_long_ret);
        oracle_case_end();
        CloseThreadpoolWork(w);
    }

    /* ---- cleanup group ---- */
    {
        TP_CALLBACK_ENVIRON env;
        InitializeThreadpoolEnvironment(&env);
        PTP_CLEANUP_GROUP g;
        ORACLE_CALL(g, CreateThreadpoolCleanupGroup());
        oracle_case("create_cleanup_group");
        o_bool("ret_null", g == NULL);
        o_lasterr();
        oracle_case_end();
        SetThreadpoolCallbackCleanupGroup(&env, g, NULL);
        work_calls = 0;
        PTP_WORK w1 = CreateThreadpoolWork(work_cb, NULL, &env);
        PTP_WORK w2 = CreateThreadpoolWork(work_cb, NULL, &env);
        SubmitThreadpoolWork(w1);
        SubmitThreadpoolWork(w2);
        CloseThreadpoolCleanupGroupMembers(g, FALSE, NULL);
        oracle_case("cleanup_group_waits_members");
        o_i64("calls", work_calls);
        oracle_case_end();
        CloseThreadpoolCleanupGroup(g);
        DestroyThreadpoolEnvironment(&env);
    }

    /* ---- private pool ---- */
    {
        PTP_POOL pool;
        ORACLE_CALL(pool, CreateThreadpool(NULL));
        oracle_case("create_pool");
        o_bool("ret_null", pool == NULL);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(ok, SetThreadpoolThreadMinimum(pool, 2));
        bool_case("pool_min_2", ok);
        SetThreadpoolThreadMaximum(pool, 1);
        ORACLE_CALL(ok, SetThreadpoolThreadMinimum(pool, 1));
        bool_case("pool_min_1_after_max_1", ok);
        TP_CALLBACK_ENVIRON env;
        InitializeThreadpoolEnvironment(&env);
        SetThreadpoolCallbackPool(&env, pool);
        work_calls = 0;
        PTP_WORK w = CreateThreadpoolWork(work_cb, (void *)0x9, &env);
        for (int i = 0; i < 3; i++) SubmitThreadpoolWork(w);
        WaitForThreadpoolWorkCallbacks(w, FALSE);
        oracle_case("private_pool_work");
        o_i64("calls", work_calls);
        oracle_case_end();
        CloseThreadpoolWork(w);
        DestroyThreadpoolEnvironment(&env);
        CloseThreadpool(pool);
    }

    /* ---- legacy APIs ---- */
    {
        legacy_ev = CreateEventW(NULL, TRUE, FALSE, NULL);
        ORACLE_CALL(ok, QueueUserWorkItem(legacy_cb, (void *)0x77, WT_EXECUTEDEFAULT));
        bool_case("queue_user_work_item", ok);
        oracle_case("queue_user_work_item_ran");
        o_bool("signalled", WaitForSingleObject(legacy_ev, 10000) == WAIT_OBJECT_0);
        oracle_case_end();

        HANDLE ev = CreateEventW(NULL, FALSE, FALSE, NULL);
        rw_done = CreateEventW(NULL, FALSE, FALSE, NULL);
        HANDLE wh = NULL;
        ORACLE_CALL(ok, RegisterWaitForSingleObject(&wh, ev, rw_cb, NULL, INFINITE, WT_EXECUTEONLYONCE));
        bool_case("register_wait_once", ok);
        SetEvent(ev);
        WaitForSingleObject(rw_done, 10000);
        ORACLE_CALL(ok, UnregisterWaitEx(wh, INVALID_HANDLE_VALUE));
        bool_case("unregister_wait_blocking", ok);
        oracle_case("register_wait_fired");
        o_i64("calls", rw_calls);
        o_bool("timed_out", rw_timed_out[0]);
        oracle_case_end();

        ORACLE_CALL(ok, RegisterWaitForSingleObject(&wh, ev, rw_cb, NULL, 0, WT_EXECUTEONLYONCE));
        bool_case("register_wait_timeout_0", ok);
        WaitForSingleObject(rw_done, 10000);
        UnregisterWaitEx(wh, INVALID_HANDLE_VALUE);
        oracle_case("register_wait_timed_out");
        o_i64("calls", rw_calls);
        o_bool("timed_out", rw_timed_out[1]);
        oracle_case_end();

        ORACLE_CALL(ok, RegisterWaitForSingleObject(&wh, NULL, rw_cb, NULL, INFINITE, 0));
        bool_case("register_wait_null_handle", ok);
        if (ok) UnregisterWaitEx(wh, INVALID_HANDLE_VALUE);
        ORACLE_CALL(ok, UnregisterWaitEx(NULL, INVALID_HANDLE_VALUE));
        bool_case("unregister_null", ok);
        CloseHandle(ev);
    }

    return oracle_end();
}
