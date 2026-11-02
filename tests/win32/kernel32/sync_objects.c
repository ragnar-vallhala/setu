/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: events, mutexes, semaphores and waitable timers: creation,
 * named-object collisions, signalling and zero-timeout waits.
 *
 * Only zero-timeout waits (or waits on objects already signalled) are used,
 * so results never depend on scheduling.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-createeventw
 *       https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-openeventw
 *       https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-createmutexw
 *       https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-releasemutex
 *       https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-createsemaphorew
 *       https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-releasesemaphore
 *       https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-waitforsingleobject
 *       https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-waitformultipleobjects
 *       https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-createwaitabletimerw
 *       https://learn.microsoft.com/en-us/windows/win32/sync/object-names
 */
#include "oracle.h"

#define NAME(s) L"SetuOracle_sync_" s

#define BOOL_CASE(name, expr)     \
    do {                          \
        BOOL ok_;                 \
        ORACLE_CALL(ok_, (expr)); \
        oracle_case(name);        \
        o_bool("ret", ok_);       \
        o_lasterr();              \
        oracle_case_end();        \
    } while (0)

#define HANDLE_CASE(name, var, expr) \
    do {                             \
        ORACLE_CALL(var, (expr));    \
        oracle_case(name);           \
        o_handle("ret", var);        \
        o_lasterr();                 \
        oracle_case_end();           \
    } while (0)

static void wait_str(DWORD r)
{
    char t[32];
    if (r == WAIT_TIMEOUT) o_str("wait", "WAIT_TIMEOUT");
    else if (r == WAIT_FAILED) o_str("wait", "WAIT_FAILED");
    else if (r >= WAIT_ABANDONED_0 && r < WAIT_ABANDONED_0 + 64) {
        snprintf(t, sizeof t, "WAIT_ABANDONED_%lu", (unsigned long)(r - WAIT_ABANDONED_0));
        o_str("wait", t);
    } else {
        snprintf(t, sizeof t, "WAIT_OBJECT_%lu", (unsigned long)r);
        o_str("wait", t);
    }
}

static void wait_case(const char *name, HANDLE h)
{
    DWORD r;
    ORACLE_CALL(r, WaitForSingleObject(h, 0));
    oracle_case(name);
    wait_str(r);
    o_lasterr();
    oracle_case_end();
}

static void waitm_case(const char *name, DWORD n, const HANDLE *hs, BOOL all)
{
    DWORD r;
    ORACLE_CALL(r, WaitForMultipleObjects(n, hs, all, 0));
    oracle_case(name);
    wait_str(r);
    o_lasterr();
    oracle_case_end();
}

static DWORD WINAPI take_mutex_and_exit(void *arg)
{
    WaitForSingleObject((HANDLE)arg, INFINITE);
    return 0; /* exits while owning it: the mutex becomes abandoned */
}

int main(void)
{
    oracle_begin("kernel32/sync_objects");

    HANDLE e1, e2, m1, m2, s1, s2, t1, x;

    /* ---- events ---- */
    HANDLE_CASE("event_create_named", e1, CreateEventW(NULL, TRUE, FALSE, NAME("ev")));
    HANDLE_CASE("event_create_same_name", e2, CreateEventW(NULL, FALSE, TRUE, NAME("ev")));
    wait_case("event_second_create_ignores_initial_state", e2);
    BOOL_CASE("event_set", SetEvent(e1));
    wait_case("event_manual_signalled", e2);
    wait_case("event_manual_stays_signalled", e1);
    BOOL_CASE("event_reset", ResetEvent(e1));
    wait_case("event_after_reset", e1);
    BOOL_CASE("event_pulse_no_waiters", PulseEvent(e1));
    wait_case("event_after_pulse", e1);
    CloseHandle(e2);
    HANDLE_CASE("event_open_existing", e2, OpenEventW(EVENT_MODIFY_STATE, FALSE, NAME("ev")));
    wait_case("event_wait_without_synchronize", e2);
    CloseHandle(e2);
    HANDLE_CASE("event_open_missing", e2, OpenEventW(EVENT_ALL_ACCESS, FALSE, NAME("missing")));
    HANDLE_CASE("event_open_null_name", e2, OpenEventW(EVENT_ALL_ACCESS, FALSE, NULL));
    HANDLE_CASE("event_open_name_case", e2, OpenEventW(EVENT_ALL_ACCESS, FALSE, L"SETUORACLE_SYNC_EV"));
    if (e2) CloseHandle(e2);
    HANDLE_CASE("event_name_with_backslash", e2, CreateEventW(NULL, TRUE, FALSE, L"SetuOracle\\bad"));
    if (e2) CloseHandle(e2);
    HANDLE_CASE("event_local_prefix", e2, CreateEventW(NULL, TRUE, FALSE, L"Local\\" NAME("ev")));
    if (e2) CloseHandle(e2);
    HANDLE_CASE("event_empty_name", e2, CreateEventW(NULL, TRUE, FALSE, L""));
    if (e2) CloseHandle(e2);

    {   /* Auto-reset event: one successful wait resets it. */
        HANDLE a = CreateEventW(NULL, FALSE, TRUE, NULL);
        wait_case("auto_event_first_wait", a);
        wait_case("auto_event_second_wait", a);
        CloseHandle(a);
    }

    /* Same name, different object type. */
    HANDLE_CASE("mutex_over_event_name", x, CreateMutexW(NULL, FALSE, NAME("ev")));
    HANDLE_CASE("semaphore_over_event_name", x, CreateSemaphoreW(NULL, 0, 1, NAME("ev")));
    HANDLE_CASE("open_mutex_on_event_name", x, OpenMutexW(MUTEX_ALL_ACCESS, FALSE, NAME("ev")));
    {
        HANDLE fm;
        HANDLE_CASE("file_mapping_over_event_name", fm,
                    CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 4096, NAME("ev")));
        if (fm) CloseHandle(fm);
    }
    CloseHandle(e1);
    HANDLE_CASE("event_open_after_last_close", e2, OpenEventW(EVENT_ALL_ACCESS, FALSE, NAME("ev")));

    /* ---- mutexes ---- */
    HANDLE_CASE("mutex_create_owned", m1, CreateMutexW(NULL, TRUE, NAME("mx")));
    wait_case("mutex_recursive_wait", m1);
    BOOL_CASE("mutex_release_1", ReleaseMutex(m1));
    BOOL_CASE("mutex_release_2", ReleaseMutex(m1));
    BOOL_CASE("mutex_release_not_owner", ReleaseMutex(m1));
    HANDLE_CASE("mutex_create_existing_owned", m2, CreateMutexW(NULL, TRUE, NAME("mx")));
    BOOL_CASE("mutex_release_after_existing_owned", ReleaseMutex(m2));
    BOOL_CASE("mutex_release_after_existing_owned_again", ReleaseMutex(m2));
    CloseHandle(m2);
    {
        HANDLE th = CreateThread(NULL, 0, take_mutex_and_exit, m1, 0, NULL);
        WaitForSingleObject(th, INFINITE);
        CloseHandle(th);
        wait_case("mutex_abandoned", m1);
        wait_case("mutex_after_abandoned_wait", m1);
        BOOL_CASE("mutex_release_after_abandoned", ReleaseMutex(m1));
        BOOL_CASE("mutex_release_after_abandoned_again", ReleaseMutex(m1));
    }
    BOOL_CASE("release_mutex_on_event", ReleaseMutex(e2 ? e2 : INVALID_HANDLE_VALUE));
    CloseHandle(m1);

    /* ---- semaphores ---- */
    HANDLE_CASE("semaphore_create", s1, CreateSemaphoreW(NULL, 1, 3, NAME("sem")));
    HANDLE_CASE("semaphore_create_initial_gt_max", s2, CreateSemaphoreW(NULL, 4, 3, NULL));
    HANDLE_CASE("semaphore_create_max_zero", s2, CreateSemaphoreW(NULL, 0, 0, NULL));
    HANDLE_CASE("semaphore_create_negative", s2, CreateSemaphoreW(NULL, -1, 3, NULL));
    {
        LONG prev = -77;
        BOOL ok;
        ORACLE_CALL(ok, ReleaseSemaphore(s1, 2, &prev));
        oracle_case("semaphore_release_to_max");
        o_bool("ret", ok);
        o_lasterr();
        o_i64("prev", prev);
        oracle_case_end();
        prev = -77;
        ORACLE_CALL(ok, ReleaseSemaphore(s1, 1, &prev));
        oracle_case("semaphore_release_past_max");
        o_bool("ret", ok);
        o_lasterr();
        o_i64("prev", prev);
        oracle_case_end();
        prev = -77;
        ORACLE_CALL(ok, ReleaseSemaphore(s1, 0, &prev));
        oracle_case("semaphore_release_zero");
        o_bool("ret", ok);
        o_lasterr();
        o_i64("prev", prev);
        oracle_case_end();
        wait_case("semaphore_wait_1", s1);
        wait_case("semaphore_wait_2", s1);
        wait_case("semaphore_wait_3", s1);
        wait_case("semaphore_wait_4", s1);
        prev = -77;
        ORACLE_CALL(ok, ReleaseSemaphore(s1, 1, &prev));
        oracle_case("semaphore_release_after_drain");
        o_bool("ret", ok);
        o_lasterr();
        o_i64("prev", prev);
        oracle_case_end();
    }

    /* ---- WaitForMultipleObjects ---- */
    {
        HANDLE ea = CreateEventW(NULL, TRUE, FALSE, NULL);
        HANDLE eb = CreateEventW(NULL, TRUE, TRUE, NULL);
        HANDLE ec = CreateEventW(NULL, TRUE, TRUE, NULL);
        HANDLE hs[3] = {ea, eb, ec};
        waitm_case("wfmo_any_first_signalled_index", 3, hs, FALSE);
        waitm_case("wfmo_all_not_all_signalled", 3, hs, TRUE);
        SetEvent(ea);
        waitm_case("wfmo_all_signalled", 3, hs, TRUE);
        waitm_case("wfmo_any_lowest_index", 3, hs, FALSE);
        HANDLE dup[2] = {ea, ea};
        waitm_case("wfmo_duplicate_handle_all", 2, dup, TRUE);
        waitm_case("wfmo_duplicate_handle_any", 2, dup, FALSE);
        waitm_case("wfmo_zero_count", 0, hs, FALSE);
        HANDLE many[MAXIMUM_WAIT_OBJECTS + 1];
        for (int i = 0; i <= MAXIMUM_WAIT_OBJECTS; i++) many[i] = eb;
        waitm_case("wfmo_too_many", MAXIMUM_WAIT_OBJECTS + 1, many, FALSE);
        HANDLE bad[2] = {ea, NULL};
        waitm_case("wfmo_null_member", 2, bad, FALSE);
        CloseHandle(ea);
        CloseHandle(eb);
        CloseHandle(ec);
    }
    wait_case("wait_null_handle", NULL);
    wait_case("wait_pseudo_thread", GetCurrentThread());

    /* ---- waitable timers ---- */
    HANDLE_CASE("timer_create", t1, CreateWaitableTimerW(NULL, TRUE, NULL));
    wait_case("timer_initially_unsignalled", t1);
    {
        LARGE_INTEGER due;
        due.QuadPart = -1; /* 100 ns relative: already due when we wait below */
        BOOL_CASE("timer_set_relative", SetWaitableTimer(t1, &due, 0, NULL, NULL, FALSE));
        DWORD r = WaitForSingleObject(t1, 5000);
        oracle_case("timer_fires");
        wait_str(r);
        oracle_case_end();
        wait_case("timer_manual_stays_signalled", t1);
        BOOL_CASE("timer_cancel", CancelWaitableTimer(t1));
        wait_case("timer_after_cancel_still_signalled", t1);
        due.QuadPart = -100000000LL; /* 10 s */
        SetWaitableTimer(t1, &due, 0, NULL, NULL, FALSE);
        wait_case("timer_reset_by_set", t1);
        CancelWaitableTimer(t1);
    }
    CloseHandle(t1);
    CloseHandle(s1);

    return oracle_end();
}
