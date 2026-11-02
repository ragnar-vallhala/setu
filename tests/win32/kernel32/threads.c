/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: threads: CreateThread, exit codes, suspend counts, priorities,
 * thread ids, APCs, TLS and FLS slots, and thread descriptions.
 *
 * Every thread is either created suspended and inspected before it runs, or
 * waited for with INFINITE after it was released, so results never depend on
 * scheduling. Thread ids are only compared, never printed.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createthread
 *       https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getexitcodethread
 *       https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-exitthread
 *       https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-terminatethread
 *       https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-suspendthread
 *       https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-resumethread
 *       https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getthreadpriority
 *       https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-setthreadpriority
 *       https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getthreadpriorityboost
 *       https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getthreadid
 *       https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-openthread
 *       https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-queueuserapc
 *       https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-sleepex
 *       https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-tlsalloc
 *       https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-tlsgetvalue
 *       https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-tlsfree
 *       https://learn.microsoft.com/en-us/windows/win32/api/fibersapi/nf-fibersapi-flsalloc
 *       https://learn.microsoft.com/en-us/windows/win32/api/fibersapi/nf-fibersapi-flsfree
 *       https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-setthreaddescription
 *       https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getthreaddescription
 *       https://learn.microsoft.com/en-us/windows/win32/procthread/thread-local-storage
 */
#include "oracle.h"

#define BOOL_CASE(name, expr)     \
    do {                          \
        BOOL ok_;                 \
        ORACLE_CALL(ok_, (expr)); \
        oracle_case(name);        \
        o_bool("ret", ok_);       \
        o_lasterr();              \
        oracle_case_end();        \
    } while (0)

#define DWORD_CASE(name, expr)    \
    do {                          \
        DWORD r_;                 \
        ORACLE_CALL(r_, (expr));  \
        oracle_case(name);        \
        o_i64("ret", (int)r_);    \
        o_lasterr();              \
        oracle_case_end();        \
    } while (0)

static void exit_code_case(const char *name, HANDLE h)
{
    DWORD code = 0xAAAAAAAA;
    BOOL ok;
    ORACLE_CALL(ok, GetExitCodeThread(h, &code));
    oracle_case(name);
    o_bool("ret", ok);
    o_lasterr();
    if (ok) o_u32("code", code);
    oracle_case_end();
}

static void wait0_case(const char *name, HANDLE h)
{
    DWORD r;
    ORACLE_CALL(r, WaitForSingleObject(h, 0));
    oracle_case(name);
    o_str("wait", r == WAIT_OBJECT_0 ? "WAIT_OBJECT_0" : r == WAIT_TIMEOUT ? "WAIT_TIMEOUT"
                  : r == WAIT_FAILED ? "WAIT_FAILED" : "other");
    o_lasterr();
    oracle_case_end();
}

static DWORD WINAPI ret_arg(void *arg) { return (DWORD)(ULONG_PTR)arg; }
static DWORD WINAPI call_exit_thread(void *arg) { ExitThread((DWORD)(ULONG_PTR)arg); }

/* APC ordering: each event appends a letter. */
static char order[16];
static int norder;
static void add_order(char c) { if (norder < 15) order[norder++] = c; }
static void CALLBACK apc_fn(ULONG_PTR p) { add_order((char)p); }
static DWORD WINAPI start_records(void *arg) { (void)arg; add_order('S'); return 0; }

/* TLS seen from another thread. */
static DWORD tls_index;
static DWORD WINAPI read_tls(void *arg)
{
    (void)arg;
    SetLastError(ORACLE_SENTINEL);
    void *v = TlsGetValue(tls_index);
    return v == NULL ? (GetLastError() == 0 ? 1 : 2) : 3;
}

/* FLS callbacks: count and record the value. */
static int fls_calls;
static ULONG_PTR fls_last;
static void WINAPI fls_cb(void *p) { fls_calls++; fls_last = (ULONG_PTR)p; }
static DWORD fls_index;
static DWORD WINAPI set_fls(void *arg) { FlsSetValue(fls_index, arg); return 0; }

static void priority_case(const char *name, HANDLE h, int prio)
{
    BOOL ok;
    ORACLE_CALL(ok, SetThreadPriority(h, prio));
    oracle_case(name);
    o_bool("ret", ok);
    o_lasterr();
    o_i64("now", GetThreadPriority(h));
    oracle_case_end();
}

int main(void)
{
    oracle_begin("kernel32/threads");

    HANDLE h;
    DWORD tid = 0;

    /* ---- create suspended, suspend counts ---- */
    ORACLE_CALL(h, CreateThread(NULL, 0, ret_arg, (void *)42, CREATE_SUSPENDED, &tid));
    oracle_case("create_suspended");
    o_handle("ret", h);
    o_lasterr();
    o_bool("tid_nonzero", tid != 0);
    o_bool("tid_matches_GetThreadId", GetThreadId(h) == tid);
    o_bool("tid_not_current", tid != GetCurrentThreadId());
    oracle_case_end();
    exit_code_case("suspended_exit_code", h);
    wait0_case("suspended_not_signalled", h);
    DWORD_CASE("suspend_1", SuspendThread(h));
    DWORD_CASE("suspend_2", SuspendThread(h));
    DWORD_CASE("resume_3", ResumeThread(h));
    DWORD_CASE("resume_2", ResumeThread(h));
    {
        oracle_case("priority_of_suspended");
        o_i64("prio", GetThreadPriority(h));
        oracle_case_end();
    }
    priority_case("set_priority_suspended_lowest", h, THREAD_PRIORITY_LOWEST);
    DWORD_CASE("resume_1", ResumeThread(h));
    WaitForSingleObject(h, INFINITE);
    wait0_case("exited_signalled", h);
    exit_code_case("exit_code_return_value", h);
    DWORD_CASE("resume_after_exit", ResumeThread(h));
    DWORD_CASE("suspend_after_exit", SuspendThread(h));
    priority_case("set_priority_after_exit", h, THREAD_PRIORITY_NORMAL);
    BOOL_CASE("terminate_after_exit", TerminateThread(h, 5));
    exit_code_case("exit_code_after_terminate_exited", h);
    CloseHandle(h);

    /* ---- ExitThread, TerminateThread, STILL_ACTIVE ---- */
    h = CreateThread(NULL, 0, call_exit_thread, (void *)7, 0, NULL);
    WaitForSingleObject(h, INFINITE);
    exit_code_case("exit_code_ExitThread", h);
    CloseHandle(h);

    h = CreateThread(NULL, 0, ret_arg, (void *)STILL_ACTIVE, 0, NULL);
    WaitForSingleObject(h, INFINITE);
    exit_code_case("exit_code_returns_still_active", h);
    wait0_case("still_active_thread_signalled", h);
    CloseHandle(h);

    h = CreateThread(NULL, 0, ret_arg, (void *)1, CREATE_SUSPENDED, NULL);
    BOOL_CASE("terminate_suspended", TerminateThread(h, 99));
    WaitForSingleObject(h, INFINITE);
    exit_code_case("exit_code_terminated", h);
    DWORD_CASE("resume_terminated", ResumeThread(h));
    CloseHandle(h);

    h = CreateThread(NULL, 0, ret_arg, (void *)0, 0x80000000u, NULL);
    oracle_case("create_bad_flags");
    o_handle("ret", h);
    oracle_case_end();
    if (h) { WaitForSingleObject(h, INFINITE); CloseHandle(h); }

    {   /* STACK_SIZE_PARAM_IS_A_RESERVATION with a modest size */
        ORACLE_CALL(h, CreateThread(NULL, 1 << 20, ret_arg, (void *)3, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL));
        oracle_case("create_stack_reservation");
        o_handle("ret", h);
        o_lasterr();
        oracle_case_end();
        if (h) { WaitForSingleObject(h, INFINITE); exit_code_case("stack_reservation_exit_code", h); CloseHandle(h); }
    }

    /* ---- invalid handles ---- */
    exit_code_case("exit_code_null_handle", NULL);
    exit_code_case("exit_code_invalid_handle", (HANDLE)(ULONG_PTR)0x1234);
    DWORD_CASE("resume_null", ResumeThread(NULL));
    DWORD_CASE("suspend_null", SuspendThread(NULL));
    DWORD_CASE("resume_current_thread", ResumeThread(GetCurrentThread()));
    {
        HANDLE ev = CreateEventW(NULL, TRUE, FALSE, NULL);
        DWORD_CASE("resume_event_handle", ResumeThread(ev));
        exit_code_case("exit_code_event_handle", ev);
        DWORD_CASE("get_priority_event_handle", (DWORD)GetThreadPriority(ev));
        DWORD_CASE("get_thread_id_event_handle", GetThreadId(ev));
        CloseHandle(ev);
    }
    DWORD_CASE("get_priority_null", (DWORD)GetThreadPriority(NULL));
    DWORD_CASE("get_thread_id_null", GetThreadId(NULL));

    /* ---- pseudo handle, ids ---- */
    oracle_case("current_thread_pseudo");
    o_bool("is_minus_2", GetCurrentThread() == (HANDLE)(LONG_PTR)-2);
    o_bool("id_matches", GetThreadId(GetCurrentThread()) == GetCurrentThreadId());
    o_bool("process_id_of_thread", GetProcessIdOfThread(GetCurrentThread()) == GetCurrentProcessId());
    oracle_case_end();
    exit_code_case("exit_code_current_thread", GetCurrentThread());

    ORACLE_CALL(h, OpenThread(THREAD_QUERY_INFORMATION, FALSE, GetCurrentThreadId()));
    oracle_case("open_current_thread");
    o_handle("ret", h);
    o_lasterr();
    o_bool("id_matches", GetThreadId(h) == GetCurrentThreadId());
    oracle_case_end();
    if (h) {
        priority_case("set_priority_without_access", h, THREAD_PRIORITY_NORMAL);
        DWORD_CASE("suspend_without_access", SuspendThread(h));
        CloseHandle(h);
    }
    ORACLE_CALL(h, OpenThread(THREAD_ALL_ACCESS, FALSE, 0));
    oracle_case("open_thread_id_zero");
    o_handle("ret", h);
    o_lasterr();
    oracle_case_end();
    ORACLE_CALL(h, OpenThread(THREAD_ALL_ACCESS, FALSE, 0xFFFFFFF0u));
    oracle_case("open_thread_bogus_id");
    o_handle("ret", h);
    o_lasterr();
    oracle_case_end();

    /* ---- priorities on the current thread ---- */
    {
        oracle_case("priority_default");
        o_i64("prio", GetThreadPriority(GetCurrentThread()));
        oracle_case_end();
        HANDLE cur = GetCurrentThread();
        priority_case("set_priority_above_normal", cur, THREAD_PRIORITY_ABOVE_NORMAL);
        priority_case("set_priority_highest", cur, THREAD_PRIORITY_HIGHEST);
        priority_case("set_priority_idle", cur, THREAD_PRIORITY_IDLE);
        priority_case("set_priority_below_normal", cur, THREAD_PRIORITY_BELOW_NORMAL);
        priority_case("set_priority_3", cur, 3);
        priority_case("set_priority_minus_3", cur, -3);
        priority_case("set_priority_6", cur, 6);
        priority_case("set_priority_14", cur, 14);
        priority_case("set_priority_16", cur, 16);
        priority_case("set_priority_minus_7", cur, -7);
        priority_case("set_priority_bg_begin", cur, THREAD_MODE_BACKGROUND_BEGIN);
        priority_case("set_priority_bg_begin_again", cur, THREAD_MODE_BACKGROUND_BEGIN);
        priority_case("set_priority_bg_end", cur, THREAD_MODE_BACKGROUND_END);
        priority_case("set_priority_bg_end_again", cur, THREAD_MODE_BACKGROUND_END);
        priority_case("set_priority_normal", cur, THREAD_PRIORITY_NORMAL);

        BOOL dis = 7;
        BOOL ok;
        ORACLE_CALL(ok, GetThreadPriorityBoost(cur, &dis));
        oracle_case("priority_boost_default");
        o_bool("ret", ok);
        o_lasterr();
        o_i64("disabled", dis);
        oracle_case_end();
        BOOL_CASE("priority_boost_disable", SetThreadPriorityBoost(cur, TRUE));
        dis = 7;
        GetThreadPriorityBoost(cur, &dis);
        oracle_case("priority_boost_after_disable");
        o_i64("disabled", dis);
        oracle_case_end();
        SetThreadPriorityBoost(cur, FALSE);
    }

    /* ---- APCs ---- */
    {
        DWORD r;
        norder = 0;
        memset(order, 0, sizeof order);
        BOOL_CASE("apc_queue_current", QueueUserAPC(apc_fn, GetCurrentThread(), 'A') != 0);
        ORACLE_CALL(r, SleepEx(0, FALSE));
        oracle_case("apc_sleep_not_alertable");
        o_hex32("ret", r);
        o_lasterr();
        o_str("order", order);
        oracle_case_end();
        ORACLE_CALL(r, SleepEx(0, TRUE));
        oracle_case("apc_sleep_alertable");
        o_hex32("ret", r);
        o_lasterr();
        o_str("order", order);
        oracle_case_end();
        ORACLE_CALL(r, SleepEx(0, TRUE));
        oracle_case("apc_sleep_alertable_empty");
        o_hex32("ret", r);
        o_lasterr();
        oracle_case_end();

        /* Two APCs: one alertable wait runs both, in FIFO order. */
        norder = 0;
        memset(order, 0, sizeof order);
        QueueUserAPC(apc_fn, GetCurrentThread(), '1');
        QueueUserAPC(apc_fn, GetCurrentThread(), '2');
        HANDLE ev = CreateEventW(NULL, TRUE, TRUE, NULL);
        ORACLE_CALL(r, WaitForSingleObjectEx(ev, 0, TRUE));
        oracle_case("apc_two_fifo_wait_signalled_object");
        o_hex32("ret", r);
        o_str("order", order);
        oracle_case_end();
        CloseHandle(ev);
        SleepEx(0, TRUE); /* drain anything still queued */
        oracle_case("apc_two_fifo_after_drain");
        o_str("order", order);
        oracle_case_end();

        /* APC queued to a thread created suspended runs before its start routine. */
        norder = 0;
        memset(order, 0, sizeof order);
        h = CreateThread(NULL, 0, start_records, NULL, CREATE_SUSPENDED, NULL);
        QueueUserAPC(apc_fn, h, 'A');
        ResumeThread(h);
        WaitForSingleObject(h, INFINITE);
        oracle_case("apc_suspended_thread_order");
        o_str("order", order);
        oracle_case_end();
        DWORD_CASE("apc_queue_exited_thread", QueueUserAPC(apc_fn, h, 'X'));
        CloseHandle(h);
        DWORD_CASE("apc_queue_null_handle", QueueUserAPC(apc_fn, NULL, 'X'));
    }

    /* ---- TLS ---- */
    {
        DWORD idx;
        void *v;
        BOOL ok;
        ORACLE_CALL(idx, TlsAlloc());
        oracle_case("tls_alloc");
        o_bool("ok", idx != TLS_OUT_OF_INDEXES);
        o_lasterr();
        oracle_case_end();
        tls_index = idx;
        ORACLE_CALL(v, TlsGetValue(idx));
        oracle_case("tls_get_fresh");
        o_bool("null", v == NULL);
        o_lasterr();
        oracle_case_end();
        BOOL_CASE("tls_set", TlsSetValue(idx, (void *)0x1234));
        ORACLE_CALL(v, TlsGetValue(idx));
        oracle_case("tls_get_after_set");
        o_hex32("value", (DWORD)(ULONG_PTR)v);
        o_lasterr();
        oracle_case_end();
        h = CreateThread(NULL, 0, read_tls, NULL, 0, NULL);
        WaitForSingleObject(h, INFINITE);
        exit_code_case("tls_other_thread_sees_null", h);
        CloseHandle(h);
        BOOL_CASE("tls_free", TlsFree(idx));
        BOOL_CASE("tls_free_again", TlsFree(idx));
        ORACLE_CALL(v, TlsGetValue(idx));
        oracle_case("tls_get_after_free");
        o_bool("null", v == NULL);
        o_lasterr();
        oracle_case_end();

        ORACLE_CALL(v, TlsGetValue(5000));
        oracle_case("tls_get_index_5000");
        o_bool("null", v == NULL);
        o_lasterr();
        oracle_case_end();
        BOOL_CASE("tls_set_index_5000", TlsSetValue(5000, (void *)1));
        BOOL_CASE("tls_free_index_5000", TlsFree(5000));
        BOOL_CASE("tls_free_out_of_indexes", TlsFree(TLS_OUT_OF_INDEXES));

        /* 100 more slots: beyond the 64 inline slots, into the expansion slots. */
        DWORD many[100];
        int nok = 0, roundtrip = 0, fresh_null = 0;
        for (int i = 0; i < 100; i++) {
            many[i] = TlsAlloc();
            if (many[i] != TLS_OUT_OF_INDEXES) nok++;
        }
        for (int i = 0; i < 100; i++)
            if (many[i] != TLS_OUT_OF_INDEXES && TlsGetValue(many[i]) == NULL) fresh_null++;
        for (int i = 0; i < 100; i++)
            if (many[i] != TLS_OUT_OF_INDEXES) TlsSetValue(many[i], (void *)(ULONG_PTR)(i + 1));
        for (int i = 0; i < 100; i++)
            if (many[i] != TLS_OUT_OF_INDEXES && TlsGetValue(many[i]) == (void *)(ULONG_PTR)(i + 1)) roundtrip++;
        int beyond64 = 0;
        for (int i = 0; i < 100; i++)
            if (many[i] != TLS_OUT_OF_INDEXES && many[i] >= TLS_MINIMUM_AVAILABLE) beyond64 = 1;
        oracle_case("tls_alloc_100");
        o_u32("allocated", nok);
        o_u32("fresh_null", fresh_null);
        o_u32("roundtrip", roundtrip);
        o_bool("uses_expansion_slots", beyond64);
        oracle_case_end();
        /* Free one and reallocate: a freed slot's value is cleared. */
        DWORD reuse = many[70];
        TlsFree(reuse);
        DWORD again = TlsAlloc();
        oracle_case("tls_realloc_after_free");
        o_bool("value_cleared", again != TLS_OUT_OF_INDEXES && TlsGetValue(again) == NULL);
        oracle_case_end();
        for (int i = 0; i < 100; i++)
            if (i != 70 && many[i] != TLS_OUT_OF_INDEXES) TlsFree(many[i]);
        if (again != TLS_OUT_OF_INDEXES) TlsFree(again);
        (void)ok;
    }

    /* ---- FLS ---- */
    {
        DWORD idx;
        void *v;
        ORACLE_CALL(idx, FlsAlloc(fls_cb));
        oracle_case("fls_alloc");
        o_bool("ok", idx != FLS_OUT_OF_INDEXES);
        o_lasterr();
        oracle_case_end();
        fls_index = idx;
        ORACLE_CALL(v, FlsGetValue(idx));
        oracle_case("fls_get_fresh");
        o_bool("null", v == NULL);
        o_lasterr();
        oracle_case_end();
        BOOL_CASE("fls_set", FlsSetValue(idx, (void *)0x55));
        ORACLE_CALL(v, FlsGetValue(idx));
        oracle_case("fls_get_after_set");
        o_hex32("value", (DWORD)(ULONG_PTR)v);
        o_lasterr();
        oracle_case_end();

        /* A thread sets a value and exits: the callback runs with that value. */
        fls_calls = 0;
        fls_last = 0;
        h = CreateThread(NULL, 0, set_fls, (void *)0x77, 0, NULL);
        WaitForSingleObject(h, INFINITE);
        CloseHandle(h);
        oracle_case("fls_callback_on_thread_exit");
        o_i64("calls", fls_calls);
        o_hex32("value", (DWORD)fls_last);
        oracle_case_end();

        /* A thread that exits with a NULL value: no callback. */
        fls_calls = 0;
        h = CreateThread(NULL, 0, set_fls, NULL, 0, NULL);
        WaitForSingleObject(h, INFINITE);
        CloseHandle(h);
        oracle_case("fls_no_callback_for_null");
        o_i64("calls", fls_calls);
        oracle_case_end();

        /* Overwriting a value does not call the callback. */
        fls_calls = 0;
        FlsSetValue(idx, (void *)0x66);
        oracle_case("fls_overwrite_no_callback");
        o_i64("calls", fls_calls);
        oracle_case_end();

        fls_calls = 0;
        fls_last = 0;
        BOOL_CASE("fls_free", FlsFree(idx));
        oracle_case("fls_free_runs_callback");
        o_i64("calls", fls_calls);
        o_hex32("value", (DWORD)fls_last);
        oracle_case_end();
        BOOL_CASE("fls_free_again", FlsFree(idx));
        ORACLE_CALL(v, FlsGetValue(idx));
        oracle_case("fls_get_after_free");
        o_bool("null", v == NULL);
        o_lasterr();
        oracle_case_end();
        BOOL_CASE("fls_set_after_free", FlsSetValue(idx, (void *)1));
        ORACLE_CALL(v, FlsGetValue(5000));
        oracle_case("fls_get_index_5000");
        o_bool("null", v == NULL);
        o_lasterr();
        oracle_case_end();
        BOOL_CASE("fls_set_index_5000", FlsSetValue(5000, (void *)1));
        BOOL_CASE("fls_free_index_5000", FlsFree(5000));
        BOOL_CASE("fls_free_out_of_indexes", FlsFree(FLS_OUT_OF_INDEXES));

        idx = FlsAlloc(NULL);
        oracle_case("fls_alloc_null_callback");
        o_bool("ok", idx != FLS_OUT_OF_INDEXES);
        oracle_case_end();
        if (idx != FLS_OUT_OF_INDEXES) FlsFree(idx);
    }

    /* ---- thread descriptions (Windows 10 1607+) ---- */
    {
        HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
        HRESULT (WINAPI *pSet)(HANDLE, PCWSTR) =
            (HRESULT (WINAPI *)(HANDLE, PCWSTR))(void *)GetProcAddress(k32, "SetThreadDescription");
        HRESULT (WINAPI *pGet)(HANDLE, PWSTR *) =
            (HRESULT (WINAPI *)(HANDLE, PWSTR *))(void *)GetProcAddress(k32, "GetThreadDescription");
        oracle_case("description_apis_present");
        o_bool("set", pSet != NULL);
        o_bool("get", pGet != NULL);
        oracle_case_end();
        if (pSet && pGet) {
            HRESULT hr;
            PWSTR d = NULL;
            h = CreateThread(NULL, 0, ret_arg, NULL, CREATE_SUSPENDED, NULL);
            ORACLE_CALL(hr, pGet(h, &d));
            oracle_case("description_default");
            o_hex32("hr", hr);
            o_lasterr();
            o_wstr("desc", SUCCEEDED(hr) ? d : NULL);
            oracle_case_end();
            if (SUCCEEDED(hr)) LocalFree(d);
            ORACLE_CALL(hr, pSet(h, L"setu worker"));
            oracle_case("description_set");
            o_hex32("hr", hr);
            o_lasterr();
            oracle_case_end();
            d = NULL;
            hr = pGet(h, &d);
            oracle_case("description_get_after_set");
            o_hex32("hr", hr);
            o_wstr("desc", SUCCEEDED(hr) ? d : NULL);
            oracle_case_end();
            if (SUCCEEDED(hr)) LocalFree(d);
            ORACLE_CALL(hr, pSet(h, L""));
            d = NULL;
            pGet(h, &d);
            oracle_case("description_set_empty");
            o_hex32("hr", hr);
            o_wstr("desc", d);
            oracle_case_end();
            if (d) LocalFree(d);
            ORACLE_CALL(hr, pSet(NULL, L"x"));
            oracle_case("description_set_null_handle");
            o_hex32("hr", hr);
            o_lasterr();
            oracle_case_end();
            TerminateThread(h, 0);
            WaitForSingleObject(h, INFINITE);
            CloseHandle(h);
        }
    }

    return oracle_end();
}
