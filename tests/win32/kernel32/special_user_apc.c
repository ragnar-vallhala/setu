/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: special user-mode APCs (QueueUserAPC2 with
 * QUEUE_USER_APC_FLAGS_SPECIAL_USER_APC): when they run relative to the
 * target thread's alertable and non-alertable waits and user-mode code,
 * whether they end alertable waits, their order relative to regular APCs,
 * and the APC_CALLBACK_DATA passed with QUEUE_USER_APC_CALLBACK_DATA_CONTEXT.
 *
 * APCs append a letter to a log; the log is printed. Cross-thread cases
 * synchronise with events, polling with a deadline, and joins.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-queueuserapc2
 *       https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/ne-processthreadsapi-queue_user_apc_flags
 *       https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/ns-processthreadsapi-apc_callback_data
 *       https://learn.microsoft.com/en-us/windows/win32/sync/asynchronous-procedure-calls
 */
#include "oracle.h"

#define SPECIAL 0x00000001       /* QUEUE_USER_APC_FLAGS_SPECIAL_USER_APC */
#define CALLBACK_DATA 0x00010000 /* QUEUE_USER_APC_CALLBACK_DATA_CONTEXT */

typedef struct {
    ULONG_PTR Parameter;
    CONTEXT *ContextRecord;
    ULONG_PTR Reserved0;
    ULONG_PTR Reserved1;
} apc_callback_data;

typedef BOOL (WINAPI *queue2_fn)(PAPCFUNC, HANDLE, ULONG_PTR, DWORD);
static queue2_fn pQueueUserAPC2;

static char log_buf[64];
static volatile LONG log_n;
static void add(char c)
{
    LONG i = InterlockedIncrement(&log_n) - 1;
    if (i < 63) { log_buf[i] = c; log_buf[i + 1] = 0; }
}
static void reset(void)
{
    log_n = 0;
    memset(log_buf, 0, sizeof(log_buf));
}

static volatile LONG apc_ran;
static volatile DWORD apc_tid;
static void CALLBACK apc_letter(ULONG_PTR p)
{
    apc_tid = GetCurrentThreadId();
    add((char)p);
    InterlockedExchange(&apc_ran, 1);
}

static apc_callback_data seen_data;
static int seen_ctx_ok;
static void CALLBACK apc_data(ULONG_PTR p)
{
    apc_callback_data *d = (apc_callback_data *)p;
    seen_data = *d;
    seen_ctx_ok = d->ContextRecord != NULL
        && (d->ContextRecord->ContextFlags & CONTEXT_CONTROL) == CONTEXT_CONTROL;
    add('d');
}

static const char *wait_name(DWORD r)
{
    switch (r) {
    case WAIT_OBJECT_0: return "WAIT_OBJECT_0";
    case WAIT_IO_COMPLETION: return "WAIT_IO_COMPLETION";
    case WAIT_TIMEOUT: return "WAIT_TIMEOUT";
    case WAIT_FAILED: return "WAIT_FAILED";
    default: return "other";
    }
}

/* polls for the APC flag for up to MS milliseconds */
static int wait_flag(DWORD ms)
{
    DWORD start = GetTickCount();
    while (!apc_ran) {
        if (GetTickCount() - start > ms) return 0;
        Sleep(5);
    }
    return 1;
}

/* --- workers ------------------------------------------------------------ */
static HANDLE w_ready, w_go;
static volatile DWORD w_ret;
static volatile LONG w_spin_stop;
static char w_log[64];

static DWORD WINAPI wait_worker(void *arg)
{
    BOOL alertable = (BOOL)(ULONG_PTR)arg;
    SetEvent(w_ready);
    w_ret = WaitForSingleObjectEx(w_go, 10000, alertable);
    add('w');
    strcpy(w_log, log_buf);
    return 0;
}

static void CALLBACK apc_stop_spin(ULONG_PTR p)
{
    add((char)p);
    InterlockedExchange(&w_spin_stop, 1);
}

static DWORD WINAPI spin_worker(void *arg)
{
    DWORD start = GetTickCount();
    (void)arg;
    SetEvent(w_ready);
    /* pure user-mode code: no system calls until the APC stops it */
    while (!w_spin_stop) {
        if (GetTickCount() - start > 10000) break;
    }
    add('w');
    strcpy(w_log, log_buf);
    return 0;
}

int main(void)
{
    oracle_begin("kernel32/special_user_apc");
    HANDLE self = GetCurrentThread();
    BOOL ok;
    DWORD r;

    pQueueUserAPC2 = (queue2_fn)(void (*)(void))GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "QueueUserAPC2");
    oracle_case("export_present");
    o_bool("present", pQueueUserAPC2 != NULL);
    oracle_case_end();
    if (!pQueueUserAPC2) return oracle_end();

    /* Queued to the calling thread: it is executing a system call, so the
     * APC runs when the call returns, before the next statement. */
    reset();
    apc_ran = 0;
    ORACLE_CALL(ok, pQueueUserAPC2(apc_letter, self, 's', SPECIAL));
    add('|');
    oracle_case("self_runs_when_call_returns");
    o_bool("ret", ok);
    o_lasterr();
    o_str("log", log_buf);
    o_bool("same_thread", apc_tid == GetCurrentThreadId());
    oracle_case_end();

    /* nothing left to deliver: an alertable sleep is not ended */
    r = SleepEx(0, TRUE);
    oracle_case("self_then_alertable_sleep");
    o_str("ret", wait_name(r));
    o_str("log", log_buf);
    oracle_case_end();

    /* a regular APC queued first still waits for an alertable wait */
    reset();
    QueueUserAPC(apc_letter, self, 'r');
    pQueueUserAPC2(apc_letter, self, 's', SPECIAL);
    add('|');
    r = SleepEx(0, TRUE);
    oracle_case("regular_then_special_order");
    o_str("ret", wait_name(r));
    o_str("log", log_buf);
    oracle_case_end();

    /* a regular APC through QueueUserAPC2 with no flags behaves like QueueUserAPC */
    reset();
    ORACLE_CALL(ok, pQueueUserAPC2(apc_letter, self, 'n', 0));
    add('|');
    r = SleepEx(0, TRUE);
    oracle_case("flags_none_is_regular");
    o_bool("ret", ok);
    o_str("wait", wait_name(r));
    o_str("log", log_buf);
    oracle_case_end();

    /* QUEUE_USER_APC_CALLBACK_DATA_CONTEXT: the APC gets an APC_CALLBACK_DATA */
    reset();
    memset(&seen_data, 0xcc, sizeof(seen_data));
    seen_ctx_ok = 0;
    ORACLE_CALL(ok, pQueueUserAPC2(apc_data, self, 0x1234, SPECIAL | CALLBACK_DATA));
    oracle_case("special_callback_data");
    o_bool("ret", ok);
    o_lasterr();
    o_str("log", log_buf);
    o_bool("parameter_is_data", seen_data.Parameter == 0x1234);
    o_bool("context_control", seen_ctx_ok);
    o_bool("reserved0_zero", seen_data.Reserved0 == 0);
    o_bool("reserved1_zero", seen_data.Reserved1 == 0);
    oracle_case_end();

    reset();
    memset(&seen_data, 0xcc, sizeof(seen_data));
    seen_ctx_ok = 0;
    pQueueUserAPC2(apc_data, self, 0x5678, CALLBACK_DATA);
    r = SleepEx(0, TRUE);
    oracle_case("regular_callback_data");
    o_str("wait", wait_name(r));
    o_str("log", log_buf);
    o_bool("parameter_is_data", seen_data.Parameter == 0x5678);
    o_bool("context_control", seen_ctx_ok);
    oracle_case_end();

    /* unknown flag bits */
    ORACLE_CALL(ok, pQueueUserAPC2(apc_letter, self, 'x', 0x4));
    oracle_case("unknown_flag");
    o_bool("ret", ok);
    o_lasterr();
    oracle_case_end();
    SleepEx(0, TRUE);

    w_ready = CreateEventW(NULL, FALSE, FALSE, NULL);
    w_go = CreateEventW(NULL, TRUE, FALSE, NULL);

    /* another thread running user-mode code is interrupted at once */
    {
        HANDLE t;
        reset();
        w_spin_stop = 0;
        t = CreateThread(NULL, 0, spin_worker, NULL, 0, NULL);
        WaitForSingleObject(w_ready, INFINITE);
        Sleep(50);
        ok = pQueueUserAPC2(apc_stop_spin, t, 's', SPECIAL);
        r = WaitForSingleObject(t, 5000);
        oracle_case("other_thread_user_code");
        o_bool("ret", ok);
        o_bool("interrupted", r == WAIT_OBJECT_0 && w_spin_stop);
        o_str("log", w_log);
        oracle_case_end();
        if (r != WAIT_OBJECT_0) { w_spin_stop = 1; WaitForSingleObject(t, INFINITE); }
        CloseHandle(t);
    }

    /* another thread in an alertable wait */
    {
        HANDLE t;
        int during;
        reset();
        ResetEvent(w_go);
        apc_ran = 0;
        w_ret = 0xdead;
        t = CreateThread(NULL, 0, wait_worker, (void *)(ULONG_PTR)TRUE, 0, NULL);
        WaitForSingleObject(w_ready, INFINITE);
        Sleep(100);
        pQueueUserAPC2(apc_letter, t, 's', SPECIAL);
        during = wait_flag(2000);
        Sleep(100);
        add('|');
        SetEvent(w_go);
        WaitForSingleObject(t, 15000);
        oracle_case("other_thread_alertable_wait");
        o_bool("ran_during_wait", during);
        o_str("wait", wait_name(w_ret));
        o_str("log", w_log);
        oracle_case_end();
        CloseHandle(t);
    }

    /* another thread in a non-alertable wait: runs after the wait ends */
    {
        HANDLE t;
        int during;
        reset();
        ResetEvent(w_go);
        apc_ran = 0;
        w_ret = 0xdead;
        t = CreateThread(NULL, 0, wait_worker, (void *)(ULONG_PTR)FALSE, 0, NULL);
        WaitForSingleObject(w_ready, INFINITE);
        Sleep(100);
        pQueueUserAPC2(apc_letter, t, 's', SPECIAL);
        during = wait_flag(500);
        add('|');
        SetEvent(w_go);
        WaitForSingleObject(t, 15000);
        oracle_case("other_thread_nonalertable_wait");
        o_bool("ran_during_wait", during);
        o_str("wait", wait_name(w_ret));
        o_str("log", w_log);
        oracle_case_end();
        CloseHandle(t);
    }

    /* a regular APC to a thread in a non-alertable wait never runs there,
     * and a special one queued after it still does */
    {
        HANDLE t;
        reset();
        ResetEvent(w_go);
        apc_ran = 0;
        t = CreateThread(NULL, 0, wait_worker, (void *)(ULONG_PTR)FALSE, 0, NULL);
        WaitForSingleObject(w_ready, INFINITE);
        Sleep(100);
        QueueUserAPC(apc_letter, t, 'r');
        pQueueUserAPC2(apc_letter, t, 's', SPECIAL);
        Sleep(100);
        add('|');
        SetEvent(w_go);
        WaitForSingleObject(t, 15000);
        oracle_case("other_thread_regular_and_special_nonalertable");
        o_str("wait", wait_name(w_ret));
        o_str("log", w_log);
        oracle_case_end();
        CloseHandle(t);
    }

    return oracle_end();
}
