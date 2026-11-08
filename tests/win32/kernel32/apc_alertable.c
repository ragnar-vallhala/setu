/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: user-mode APCs and alertable waits: return codes of
 * WaitForSingleObjectEx, WaitForMultipleObjectsEx, SignalObjectAndWait and
 * SleepEx when APCs are pending, APCs queued from inside an APC, APCs on
 * another thread, access checks, and APCs queued to threads that exit.
 * (threads.c covers SleepEx basics and FIFO order.)
 *
 * APCs append a letter to a log; the log is printed. Cross-thread cases
 * synchronise with events and joins only.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/sync/asynchronous-procedure-calls
 *       https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-queueuserapc
 *       https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-waitforsingleobjectex
 *       https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-waitformultipleobjectsex
 *       https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-signalobjectandwait
 *       https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-sleepex
 *       https://learn.microsoft.com/en-us/windows/win32/procthread/thread-security-and-access-rights
 */
#include "oracle.h"

static char log_buf[64];
static int log_n;
static DWORD apc_tid;
static void add(char c)
{
    if (log_n < 63) log_buf[log_n++] = c;
    log_buf[log_n] = 0;
}
static void reset(void)
{
    log_n = 0;
    log_buf[0] = 0;
}

static void CALLBACK apc_letter(ULONG_PTR p)
{
    apc_tid = GetCurrentThreadId();
    add((char)p);
}

/* Queues two more APCs while running. */
static void CALLBACK apc_requeue(ULONG_PTR p)
{
    add((char)p);
    QueueUserAPC(apc_letter, GetCurrentThread(), 'x');
    QueueUserAPC(apc_letter, GetCurrentThread(), 'y');
}

/* Waits alertably from inside an APC: nested APC delivery. */
static DWORD nested_ret;
static void CALLBACK apc_nested_wait(ULONG_PTR p)
{
    add((char)p);
    QueueUserAPC(apc_letter, GetCurrentThread(), 'n');
    nested_ret = SleepEx(0, TRUE);
    add('/');
}

static const char *wait_name(DWORD r)
{
    switch (r) {
    case WAIT_OBJECT_0: return "WAIT_OBJECT_0";
    case WAIT_OBJECT_0 + 1: return "WAIT_OBJECT_0+1";
    case WAIT_ABANDONED_0: return "WAIT_ABANDONED_0";
    case WAIT_IO_COMPLETION: return "WAIT_IO_COMPLETION";
    case WAIT_TIMEOUT: return "WAIT_TIMEOUT";
    case WAIT_FAILED: return "WAIT_FAILED";
    default: return "other";
    }
}

static void wait_case(const char *name, DWORD r)
{
    oracle_case(name);
    o_str("ret", wait_name(r));
    o_lasterr();
    o_str("log", log_buf);
    oracle_case_end();
    reset();
}

/* Worker that waits alertably until told to stop. */
static HANDLE w_ready, w_stop;
static char w_log[32];
static DWORD w_rets[8];
static int w_nrets;
static DWORD WINAPI alertable_worker(void *arg)
{
    (void)arg;
    SetEvent(w_ready);
    for (;;) {
        DWORD r = WaitForSingleObjectEx(w_stop, INFINITE, TRUE);
        if (w_nrets < 8) w_rets[w_nrets++] = r;
        if (r == WAIT_OBJECT_0) break;
    }
    strcpy(w_log, log_buf);
    return 0;
}

static DWORD WINAPI exit_at_once(void *arg)
{
    (void)arg;
    return 0;
}

static DWORD WINAPI exit_without_alertable(void *arg)
{
    WaitForSingleObject((HANDLE)arg, INFINITE); /* not alertable */
    return 0;
}

int main(void)
{
    oracle_begin("kernel32/apc_alertable");
    HANDLE set_ev = CreateEventW(NULL, TRUE, TRUE, NULL);
    HANDLE unset_ev = CreateEventW(NULL, TRUE, FALSE, NULL);
    HANDLE self = GetCurrentThread();
    DWORD r;
    BOOL ok;

    reset();
    ORACLE_CALL(ok, QueueUserAPC(apc_letter, self, 'a'));
    oracle_case("queue_to_self");
    o_bool("ret", ok);
    o_lasterr();
    oracle_case_end();

    ORACLE_CALL(r, WaitForSingleObjectEx(unset_ev, 0, FALSE));
    wait_case("wfso_ex_not_alertable_pending_apc", r);
    ORACLE_CALL(r, WaitForSingleObjectEx(unset_ev, 0, TRUE));
    wait_case("wfso_ex_alertable_timeout_0_runs_apc", r);
    ORACLE_CALL(r, WaitForSingleObjectEx(unset_ev, 0, TRUE));
    wait_case("wfso_ex_alertable_nothing_pending", r);

    QueueUserAPC(apc_letter, self, 'b');
    ORACLE_CALL(r, WaitForSingleObjectEx(set_ev, INFINITE, TRUE));
    wait_case("wfso_ex_signalled_object_with_apc", r);
    QueueUserAPC(apc_letter, self, 'c');
    ORACLE_CALL(r, WaitForSingleObjectEx(unset_ev, INFINITE, TRUE));
    wait_case("wfso_ex_infinite_returns_for_apc", r);

    {
        HANDLE two[2] = {unset_ev, set_ev};
        QueueUserAPC(apc_letter, self, 'd');
        ORACLE_CALL(r, WaitForMultipleObjectsEx(2, two, FALSE, INFINITE, TRUE));
        wait_case("wfmo_ex_any_with_apc", r);
        ORACLE_CALL(r, WaitForMultipleObjectsEx(2, two, FALSE, INFINITE, TRUE));
        wait_case("wfmo_ex_any_after_apc", r);
        QueueUserAPC(apc_letter, self, 'e');
        ORACLE_CALL(r, WaitForMultipleObjectsEx(2, two, TRUE, 0, TRUE));
        wait_case("wfmo_ex_all_timeout_0_with_apc", r);
        QueueUserAPC(apc_letter, self, 'f');
        ORACLE_CALL(r, WaitForMultipleObjectsEx(2, two, TRUE, 0, FALSE));
        wait_case("wfmo_ex_all_not_alertable", r);
        ORACLE_CALL(r, SleepEx(0, TRUE));
        wait_case("sleepex_drains_f", r);
    }

    /* ---- SignalObjectAndWait ---- */
    {
        HANDLE sem = CreateSemaphoreW(NULL, 0, 5, NULL);
        QueueUserAPC(apc_letter, self, 'g');
        ORACLE_CALL(r, SignalObjectAndWait(sem, unset_ev, INFINITE, TRUE));
        wait_case("signal_and_wait_alertable_apc", r);
        LONG prev = -1;
        ReleaseSemaphore(sem, 1, &prev);
        oracle_case("signal_and_wait_signalled_before_apc");
        o_i64("sem_count_before_release", prev);
        oracle_case_end();
        ORACLE_CALL(r, SignalObjectAndWait(sem, set_ev, INFINITE, FALSE));
        wait_case("signal_and_wait_set_event", r);
        ORACLE_CALL(r, SignalObjectAndWait(unset_ev, set_ev, 0, FALSE));
        wait_case("signal_and_wait_event_as_signal", r);
        ResetEvent(unset_ev);
        ORACLE_CALL(r, SignalObjectAndWait(self, set_ev, 0, FALSE));
        wait_case("signal_and_wait_thread_as_signal", r);
        HANDLE mx = CreateMutexW(NULL, FALSE, NULL);
        ORACLE_CALL(r, SignalObjectAndWait(mx, set_ev, 0, FALSE));
        wait_case("signal_and_wait_unowned_mutex", r);
        CloseHandle(mx);
        CloseHandle(sem);
    }

    /* ---- APCs queued by an APC run in the same wait ---- */
    QueueUserAPC(apc_requeue, self, 'r');
    ORACLE_CALL(r, SleepEx(0, TRUE));
    wait_case("apc_requeues_two", r);
    ORACLE_CALL(r, SleepEx(0, TRUE));
    wait_case("apc_requeue_leftovers", r);

    QueueUserAPC(apc_nested_wait, self, 'w');
    QueueUserAPC(apc_letter, self, 'z');
    ORACLE_CALL(r, SleepEx(0, TRUE));
    oracle_case("nested_alertable_wait_in_apc");
    o_str("ret", wait_name(r));
    o_str("nested_ret", wait_name(nested_ret));
    o_str("log", log_buf);
    oracle_case_end();
    reset();
    SleepEx(0, TRUE);
    reset();

    /* ---- APC to another thread waiting alertably ---- */
    {
        w_ready = CreateEventW(NULL, TRUE, FALSE, NULL);
        w_stop = CreateEventW(NULL, TRUE, FALSE, NULL);
        HANDLE th = CreateThread(NULL, 0, alertable_worker, NULL, 0, NULL);
        WaitForSingleObject(w_ready, INFINITE);
        apc_tid = 0;
        /* APCs before the stop event: queued in order, run on the worker. */
        QueueUserAPC(apc_letter, th, 'p');
        QueueUserAPC(apc_letter, th, 'q');
        SetEvent(w_stop);
        WaitForSingleObject(th, INFINITE);
        char rets[64] = "";
        int stop_last = w_nrets > 0 && w_rets[w_nrets - 1] == WAIT_OBJECT_0;
        int ios = 0;
        for (int i = 0; i < w_nrets; i++) ios += w_rets[i] == WAIT_IO_COMPLETION;
        snprintf(rets, sizeof rets, "%d", ios);
        oracle_case("other_thread_alertable");
        o_str("log", w_log);
        o_bool("ran_on_worker", apc_tid == GetThreadId(th));
        o_bool("last_wait_was_stop", stop_last);
        oracle_case_end();
        CloseHandle(th);
        reset();
    }

    /* ---- access and exited threads ---- */
    {
        HANDLE th = CreateThread(NULL, 0, exit_at_once, NULL, 0, NULL);
        WaitForSingleObject(th, INFINITE);
        ORACLE_CALL(ok, QueueUserAPC(apc_letter, th, 'X'));
        oracle_case("queue_to_exited_thread");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        CloseHandle(th);

        HANDLE gate = CreateEventW(NULL, TRUE, FALSE, NULL);
        /* Queued before the thread runs: it starts by calling the APC
         * (QueueUserAPC docs). Created suspended so it cannot start first. */
        th = CreateThread(NULL, 0, exit_without_alertable, gate, CREATE_SUSPENDED, NULL);
        ORACLE_CALL(ok, QueueUserAPC(apc_letter, th, 'Y'));
        ResumeThread(th);
        oracle_case("queue_to_never_alertable_thread");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        SetEvent(gate);
        WaitForSingleObject(th, INFINITE);
        oracle_case("never_alertable_apc_discarded");
        o_str("log", log_buf);
        oracle_case_end();
        CloseHandle(th);
        CloseHandle(gate);

        HANDLE lim;
        DuplicateHandle(GetCurrentProcess(), self, GetCurrentProcess(), &lim, THREAD_QUERY_INFORMATION, FALSE, 0);
        ORACLE_CALL(ok, QueueUserAPC(apc_letter, lim, 'L'));
        oracle_case("queue_without_set_context_access");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        CloseHandle(lim);
        DuplicateHandle(GetCurrentProcess(), self, GetCurrentProcess(), &lim, THREAD_SET_CONTEXT, FALSE, 0);
        ORACLE_CALL(ok, QueueUserAPC(apc_letter, lim, 'S'));
        oracle_case("queue_with_set_context_only");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        CloseHandle(lim);
        ORACLE_CALL(r, SleepEx(0, TRUE));
        wait_case("set_context_only_apc_runs", r);

        ORACLE_CALL(ok, QueueUserAPC(apc_letter, NULL, 'N'));
        oracle_case("queue_null_thread");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(ok, QueueUserAPC(apc_letter, set_ev, 'E'));
        oracle_case("queue_to_event_handle");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        HANDLE proc_thread = GetCurrentProcess();
        ORACLE_CALL(ok, QueueUserAPC(apc_letter, proc_thread, 'P'));
        oracle_case("queue_to_process_pseudo_handle");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        SleepEx(0, TRUE);
        reset();
    }

    oracle_case("queue_user_apc2_export");
    o_bool("present", GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "QueueUserAPC2") != NULL);
    oracle_case_end();

    return oracle_end();
}
