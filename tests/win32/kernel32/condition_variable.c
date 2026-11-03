/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: condition variables with critical sections and SRW locks:
 * timeouts and their error code, lock state after a timeout, wakes without
 * waiters, and Wake / WakeAll hand-offs.
 *
 * A waiter publishes "I am waiting" while it holds the lock, and only lets go
 * of the lock inside SleepConditionVariable*. So once the main thread holds
 * the lock and sees the flag, the waiter is asleep. Waiters loop on a token
 * count, so spurious wake-ups cannot change any printed value.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/sync/condition-variables
 *       https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-initializeconditionvariable
 *       https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-sleepconditionvariablecs
 *       https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-sleepconditionvariablesrw
 *       https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-wakeconditionvariable
 *       https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-wakeallconditionvariable
 */
#include "oracle.h"

static CRITICAL_SECTION cs;
static SRWLOCK srw = SRWLOCK_INIT;
static CONDITION_VARIABLE cv = CONDITION_VARIABLE_INIT;

static DWORD WINAPI try_srw_other(void *arg)
{
    BOOLEAN r;
    if (arg) {
        r = TryAcquireSRWLockShared(&srw);
        if (r) ReleaseSRWLockShared(&srw);
    } else {
        r = TryAcquireSRWLockExclusive(&srw);
        if (r) ReleaseSRWLockExclusive(&srw);
    }
    return r;
}

static int srw_other(int shared)
{
    DWORD code = 99;
    HANDLE h = CreateThread(NULL, 0, try_srw_other, (void *)(ULONG_PTR)shared, 0, NULL);
    WaitForSingleObject(h, INFINITE);
    GetExitCodeThread(h, &code);
    CloseHandle(h);
    return (int)code;
}

static DWORD WINAPI try_cs_other(void *arg)
{
    (void)arg;
    if (!TryEnterCriticalSection(&cs)) return 0;
    LeaveCriticalSection(&cs);
    return 1;
}

static int cs_other(void)
{
    DWORD code = 99;
    HANDLE h = CreateThread(NULL, 0, try_cs_other, NULL, 0, NULL);
    WaitForSingleObject(h, INFINITE);
    GetExitCodeThread(h, &code);
    CloseHandle(h);
    return (int)code;
}

static void timeout_case(const char *name, BOOL ok)
{
    oracle_case(name);
    o_bool("ret", ok);
    o_lasterr();
    oracle_case_end();
}

/* ---- waiters ---------------------------------------------------------- */

static volatile LONG waiting, tokens, woken;
static DWORD last_err_seen[8];
static BOOL last_ret_seen[8];

static DWORD WINAPI cs_waiter(void *arg)
{
    int idx = (int)(ULONG_PTR)arg;
    EnterCriticalSection(&cs);
    waiting++;
    while (tokens == 0) {
        SetLastError(ORACLE_SENTINEL);
        last_ret_seen[idx] = SleepConditionVariableCS(&cv, &cs, INFINITE);
        last_err_seen[idx] = GetLastError();
    }
    tokens--;
    woken++;
    BOOL owns = (DWORD)(ULONG_PTR)cs.OwningThread == GetCurrentThreadId();
    LONG rec = cs.RecursionCount;
    waiting--;
    LeaveCriticalSection(&cs);
    return owns && rec == 1 ? 1 : 0;
}

static DWORD WINAPI srw_waiter(void *arg)
{
    int idx = (int)(ULONG_PTR)arg;
    AcquireSRWLockExclusive(&srw);
    waiting++;
    while (tokens == 0) {
        SetLastError(ORACLE_SENTINEL);
        last_ret_seen[idx] = SleepConditionVariableSRW(&cv, &srw, INFINITE, 0);
        last_err_seen[idx] = GetLastError();
    }
    tokens--;
    woken++;
    /* We must hold the lock exclusively again. */
    BOOLEAN again = TryAcquireSRWLockShared(&srw);
    if (again) ReleaseSRWLockShared(&srw);
    waiting--;
    ReleaseSRWLockExclusive(&srw);
    return again ? 0 : 1;
}

static void wait_for_cs_waiters(int n)
{
    for (;;) {
        EnterCriticalSection(&cs);
        if (waiting >= n) return; /* returns holding cs */
        LeaveCriticalSection(&cs);
        SwitchToThread();
    }
}

static void wait_for_srw_waiters(int n)
{
    for (;;) {
        AcquireSRWLockExclusive(&srw);
        if (waiting >= n) return; /* returns holding srw */
        ReleaseSRWLockExclusive(&srw);
        SwitchToThread();
    }
}

static void last_err_case(const char *name, int idx)
{
    oracle_case(name);
    o_bool("sleep_ret", last_ret_seen[idx]);
    if (last_err_seen[idx] == ORACLE_SENTINEL) o_str("sleep_last_error", "unchanged");
    else o_u32("sleep_last_error", last_err_seen[idx]);
    oracle_case_end();
}

int main(void)
{
    oracle_begin("kernel32/condition_variable");
    BOOL ok;
    InitializeCriticalSection(&cs);

    {
        CONDITION_VARIABLE c;
        memset(&c, 0xcc, sizeof c);
        InitializeConditionVariable(&c);
        oracle_case("initialize_zeroes");
        o_bool("word_zero", c.Ptr == NULL);
        o_u32("size", sizeof c);
        oracle_case_end();
    }

    /* ---- timeouts with a critical section ---- */
    EnterCriticalSection(&cs);
    ORACLE_CALL(ok, SleepConditionVariableCS(&cv, &cs, 0));
    timeout_case("cs_timeout_0", ok);
    oracle_case("cs_owned_after_timeout");
    o_bool("owner_is_me", (DWORD)(ULONG_PTR)cs.OwningThread == GetCurrentThreadId());
    o_i64("recursion", cs.RecursionCount);
    o_i64("other_thread_try", cs_other());
    oracle_case_end();
    ORACLE_CALL(ok, SleepConditionVariableCS(&cv, &cs, 20));
    timeout_case("cs_timeout_20ms", ok);
    oracle_case("cs_owned_after_timeout_20ms");
    o_bool("owner_is_me", (DWORD)(ULONG_PTR)cs.OwningThread == GetCurrentThreadId());
    o_i64("recursion", cs.RecursionCount);
    oracle_case_end();

    SetLastError(ORACLE_SENTINEL);
    WakeConditionVariable(&cv);
    WakeAllConditionVariable(&cv);
    oracle_last_error = GetLastError();
    oracle_case("wake_without_waiters");
    o_lasterr();
    oracle_case_end();
    ORACLE_CALL(ok, SleepConditionVariableCS(&cv, &cs, 0));
    timeout_case("cs_wake_is_not_remembered", ok);
    LeaveCriticalSection(&cs);

    /* ---- timeouts with an SRW lock ---- */
    AcquireSRWLockExclusive(&srw);
    ORACLE_CALL(ok, SleepConditionVariableSRW(&cv, &srw, 0, 0));
    timeout_case("srw_exclusive_timeout_0", ok);
    oracle_case("srw_exclusive_held_after_timeout");
    o_i64("other_exclusive", srw_other(0));
    o_i64("other_shared", srw_other(1));
    oracle_case_end();
    ORACLE_CALL(ok, SleepConditionVariableSRW(&cv, &srw, 20, 0));
    timeout_case("srw_exclusive_timeout_20ms", ok);
    ReleaseSRWLockExclusive(&srw);

    AcquireSRWLockShared(&srw);
    ORACLE_CALL(ok, SleepConditionVariableSRW(&cv, &srw, 0, CONDITION_VARIABLE_LOCKMODE_SHARED));
    timeout_case("srw_shared_timeout_0", ok);
    oracle_case("srw_shared_held_after_timeout");
    o_i64("other_exclusive", srw_other(0));
    o_i64("other_shared", srw_other(1));
    oracle_case_end();
    ReleaseSRWLockShared(&srw);

    AcquireSRWLockExclusive(&srw);
    ORACLE_CALL(ok, SleepConditionVariableSRW(&cv, &srw, 0, 2));
    timeout_case("srw_unknown_flag_timeout_0", ok);
    ReleaseSRWLockExclusive(&srw);

    oracle_case("srw_free_after_timeouts");
    o_i64("other_exclusive", srw_other(0));
    oracle_case_end();

    /* ---- Wake hands one token to one CS waiter ---- */
    {
        waiting = tokens = woken = 0;
        HANDLE th = CreateThread(NULL, 0, cs_waiter, (void *)0, 0, NULL);
        wait_for_cs_waiters(1);
        tokens = 1;
        WakeConditionVariable(&cv);
        LeaveCriticalSection(&cs);
        WaitForSingleObject(th, INFINITE);
        DWORD code = 0;
        GetExitCodeThread(th, &code);
        CloseHandle(th);
        oracle_case("cs_wake_one_waiter");
        o_i64("woken", woken);
        o_i64("tokens_left", tokens);
        o_u32("waiter_owned_cs_once", code);
        oracle_case_end();
        last_err_case("cs_wake_sleep_result", 0);
    }

    /* ---- WakeAll releases three CS waiters ---- */
    {
        HANDLE th[3];
        waiting = tokens = woken = 0;
        for (int i = 0; i < 3; i++) th[i] = CreateThread(NULL, 0, cs_waiter, (void *)(ULONG_PTR)(i + 1), 0, NULL);
        wait_for_cs_waiters(3);
        tokens = 3;
        WakeAllConditionVariable(&cv);
        LeaveCriticalSection(&cs);
        WaitForMultipleObjects(3, th, TRUE, INFINITE);
        int all_ok = 1;
        for (int i = 0; i < 3; i++) {
            DWORD code = 0;
            GetExitCodeThread(th[i], &code);
            all_ok &= code == 1;
            CloseHandle(th[i]);
        }
        oracle_case("cs_wake_all_three");
        o_i64("woken", woken);
        o_i64("tokens_left", tokens);
        o_bool("all_owned_cs_once", all_ok);
        oracle_case_end();
    }

    /* ---- Wake one of two waiters, then the other ---- */
    {
        HANDLE th[2];
        waiting = tokens = woken = 0;
        for (int i = 0; i < 2; i++) th[i] = CreateThread(NULL, 0, cs_waiter, (void *)(ULONG_PTR)(i + 4), 0, NULL);
        wait_for_cs_waiters(2);
        tokens = 1;
        WakeConditionVariable(&cv);
        LeaveCriticalSection(&cs);
        DWORD first = WaitForMultipleObjects(2, th, FALSE, INFINITE);
        EnterCriticalSection(&cs);
        oracle_case("cs_wake_one_of_two");
        o_bool("one_thread_done", first == WAIT_OBJECT_0 || first == WAIT_OBJECT_0 + 1);
        o_i64("woken", woken);
        o_i64("still_waiting", waiting);
        oracle_case_end();
        tokens = 1;
        WakeConditionVariable(&cv);
        LeaveCriticalSection(&cs);
        WaitForMultipleObjects(2, th, TRUE, INFINITE);
        oracle_case("cs_wake_second_of_two");
        o_i64("woken", woken);
        o_i64("still_waiting", waiting);
        oracle_case_end();
        CloseHandle(th[0]);
        CloseHandle(th[1]);
    }

    /* ---- SRW waiters ---- */
    {
        HANDLE th[2];
        waiting = tokens = woken = 0;
        for (int i = 0; i < 2; i++) th[i] = CreateThread(NULL, 0, srw_waiter, (void *)(ULONG_PTR)(i + 6), 0, NULL);
        wait_for_srw_waiters(2);
        tokens = 2;
        WakeAllConditionVariable(&cv);
        ReleaseSRWLockExclusive(&srw);
        WaitForMultipleObjects(2, th, TRUE, INFINITE);
        int all_ok = 1;
        for (int i = 0; i < 2; i++) {
            DWORD code = 0;
            GetExitCodeThread(th[i], &code);
            all_ok &= code == 1;
            CloseHandle(th[i]);
        }
        oracle_case("srw_wake_all_two");
        o_i64("woken", woken);
        o_bool("all_held_exclusive_after_wake", all_ok);
        oracle_case_end();
        last_err_case("srw_wake_sleep_result", 6);
        oracle_case("srw_free_after_waiters");
        o_i64("other_exclusive", srw_other(0));
        oracle_case_end();
    }

    DeleteCriticalSection(&cs);
    return oracle_end();
}
