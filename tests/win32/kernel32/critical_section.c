/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: critical sections: TryEnter, recursion, ownership seen from
 * other threads, spin counts, InitializeCriticalSectionEx flags, and a thread
 * that exits while it owns a critical section.
 *
 * Other threads are always joined with INFINITE before their results are
 * printed, so nothing depends on scheduling. Thread ids are only compared.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/sync/critical-section-objects
 *       https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-initializecriticalsection
 *       https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-initializecriticalsectionandspincount
 *       https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-initializecriticalsectionex
 *       https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-tryentercriticalsection
 *       https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-entercriticalsection
 *       https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-leavecriticalsection
 *       https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-setcriticalsectionspincount
 *       https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-deletecriticalsection
 */
#include "oracle.h"

static CRITICAL_SECTION cs;

/* Runs TryEnterCriticalSection on another thread; leaves again on success. */
static DWORD WINAPI try_other(void *arg)
{
    CRITICAL_SECTION *c = arg;
    if (!TryEnterCriticalSection(c)) return 0;
    LeaveCriticalSection(c);
    return 1;
}

static int try_from_other_thread(CRITICAL_SECTION *c)
{
    DWORD code = 99;
    HANDLE h = CreateThread(NULL, 0, try_other, c, 0, NULL);
    WaitForSingleObject(h, INFINITE);
    GetExitCodeThread(h, &code);
    CloseHandle(h);
    return (int)code;
}

static void state(const char *name, CRITICAL_SECTION *c)
{
    oracle_case(name);
    o_i64("recursion", c->RecursionCount);
    o_bool("owner_is_me", (DWORD)(ULONG_PTR)c->OwningThread == GetCurrentThreadId());
    o_bool("owner_zero", c->OwningThread == 0);
    oracle_case_end();
}

/* A thread that enters a critical section and exits without leaving. */
static DWORD WINAPI enter_and_exit(void *arg)
{
    EnterCriticalSection(arg);
    return 0;
}

/* Blocks in EnterCriticalSection until the main thread leaves. */
static HANDLE ready;
static volatile LONG entered_after_wait;
static DWORD WINAPI enter_blocking(void *arg)
{
    CRITICAL_SECTION *c = arg;
    BOOL got = TryEnterCriticalSection(c);
    SetEvent(ready);
    if (got) return 2;
    EnterCriticalSection(c);
    InterlockedExchange(&entered_after_wait, 1);
    LONG rec = c->RecursionCount;
    LeaveCriticalSection(c);
    return rec == 1 ? 1 : 3;
}

int main(void)
{
    oracle_begin("kernel32/critical_section");
    BOOL ok;

    /* ---- fresh critical section ---- */
    memset(&cs, 0xcc, sizeof cs);
    InitializeCriticalSection(&cs);
    state("init_state", &cs);

    ORACLE_CALL(ok, TryEnterCriticalSection(&cs));
    oracle_case("try_enter_free");
    o_bool("ret", ok);
    o_lasterr();
    oracle_case_end();
    state("after_try_enter", &cs);

    ORACLE_CALL(ok, TryEnterCriticalSection(&cs));
    oracle_case("try_enter_recursive");
    o_bool("ret", ok);
    o_lasterr();
    oracle_case_end();
    state("after_try_enter_recursive", &cs);

    EnterCriticalSection(&cs);
    state("after_enter_third", &cs);

    oracle_case("try_enter_other_thread_while_owned");
    o_i64("got", try_from_other_thread(&cs));
    oracle_case_end();

    SetLastError(ORACLE_SENTINEL);
    LeaveCriticalSection(&cs);
    oracle_last_error = GetLastError();
    oracle_case("leave_keeps_last_error");
    o_lasterr();
    oracle_case_end();
    state("after_leave_one", &cs);

    LeaveCriticalSection(&cs);
    state("after_leave_two", &cs);
    oracle_case("try_enter_other_thread_still_owned");
    o_i64("got", try_from_other_thread(&cs));
    oracle_case_end();

    LeaveCriticalSection(&cs);
    state("after_leave_all", &cs);
    oracle_case("try_enter_other_thread_after_leave");
    o_i64("got", try_from_other_thread(&cs));
    oracle_case_end();
    state("after_other_thread_entered_and_left", &cs);

    /* ---- blocking enter from another thread ---- */
    {
        ready = CreateEventW(NULL, TRUE, FALSE, NULL);
        EnterCriticalSection(&cs);
        HANDLE h = CreateThread(NULL, 0, enter_blocking, &cs, 0, NULL);
        WaitForSingleObject(ready, INFINITE);
        LONG before = entered_after_wait;
        LeaveCriticalSection(&cs);
        WaitForSingleObject(h, INFINITE);
        DWORD code = 0;
        GetExitCodeThread(h, &code);
        oracle_case("enter_blocks_until_leave");
        o_bool("not_entered_while_owned", before == 0);
        o_u32("thread_result", code);
        o_bool("entered_after_leave", entered_after_wait == 1);
        oracle_case_end();
        state("after_blocking_handoff", &cs);
        CloseHandle(h);
        CloseHandle(ready);
    }

    /* ---- thread exits while owning the critical section ---- */
    {
        HANDLE h = CreateThread(NULL, 0, enter_and_exit, &cs, 0, NULL);
        WaitForSingleObject(h, INFINITE);
        CloseHandle(h);
        ORACLE_CALL(ok, TryEnterCriticalSection(&cs));
        oracle_case("orphaned_try_enter");
        o_bool("ret", ok);
        o_lasterr();
        o_i64("recursion", cs.RecursionCount);
        o_bool("owner_is_me", (DWORD)(ULONG_PTR)cs.OwningThread == GetCurrentThreadId());
        o_bool("owner_zero", cs.OwningThread == 0);
        oracle_case_end();
        if (ok) LeaveCriticalSection(&cs);
    }
    /* The orphaned section is abandoned; use a new one from here on. */

    /* ---- spin counts ---- */
    {
        CRITICAL_SECTION s;
        ORACLE_CALL(ok, InitializeCriticalSectionAndSpinCount(&s, 4000));
        oracle_case("init_spin_4000");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        DWORD prev;
        ORACLE_CALL(prev, SetCriticalSectionSpinCount(&s, 100));
        oracle_case("set_spin_returns_previous");
        o_bool("prev_is_4000_or_0", prev == 4000 || prev == 0);
        o_bool("prev_is_4000", prev == 4000);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(prev, SetCriticalSectionSpinCount(&s, 0));
        oracle_case("set_spin_again");
        o_bool("prev_is_100", prev == 100);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(ok, TryEnterCriticalSection(&s));
        oracle_case("spin_cs_try_enter");
        o_bool("ret", ok);
        oracle_case_end();
        LeaveCriticalSection(&s);
        DeleteCriticalSection(&s);

        ORACLE_CALL(ok, InitializeCriticalSectionAndSpinCount(&s, 0x80000400));
        oracle_case("init_spin_high_bit");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        if (ok) {
            ORACLE_CALL(prev, SetCriticalSectionSpinCount(&s, 0));
            oracle_case("init_spin_high_bit_stored");
            o_bool("prev_is_0x400", prev == 0x400);
            o_bool("prev_is_0x80000400", prev == 0x80000400);
            o_bool("prev_is_0", prev == 0);
            oracle_case_end();
            DeleteCriticalSection(&s);
        }
    }

    /* ---- InitializeCriticalSectionEx ---- */
    {
        CRITICAL_SECTION s;
        ORACLE_CALL(ok, InitializeCriticalSectionEx(&s, 1000, 0));
        oracle_case("init_ex_no_flags");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        if (ok) {
            DWORD prev = SetCriticalSectionSpinCount(&s, 0);
            oracle_case("init_ex_spin_stored");
            o_bool("prev_is_1000", prev == 1000);
            o_bool("prev_is_0", prev == 0);
            oracle_case_end();
            DeleteCriticalSection(&s);
        }

        ORACLE_CALL(ok, InitializeCriticalSectionEx(&s, 0, CRITICAL_SECTION_NO_DEBUG_INFO));
        oracle_case("init_ex_no_debug_info");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        if (ok) {
            BOOL t = TryEnterCriticalSection(&s);
            oracle_case("init_ex_no_debug_info_usable");
            o_bool("try", t);
            o_i64("recursion", s.RecursionCount);
            oracle_case_end();
            if (t) LeaveCriticalSection(&s);
            DeleteCriticalSection(&s);
        }

        ORACLE_CALL(ok, InitializeCriticalSectionEx(&s, 0, 0x00000001));
        oracle_case("init_ex_low_flag_bit");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        if (ok) DeleteCriticalSection(&s);

        ORACLE_CALL(ok, InitializeCriticalSectionEx(&s, 0x01000000, 0));
        oracle_case("init_ex_spin_with_flag_bits");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        if (ok) DeleteCriticalSection(&s);

        /* Probes: where the spin-count / flag boundaries are. */
        static const struct { const char *name; DWORD spin, flags; } probes[] = {
            {"probe_init_ex_spin_00ffffff", 0x00ffffff, 0},
            {"probe_init_ex_spin_80000000", 0x80000000, 0},
            {"probe_init_ex_spin_10000000", 0x10000000, 0},
            {"probe_init_ex_flag_80000000", 0, 0x80000000},
            {"probe_init_ex_flag_10000000", 0, 0x10000000},
            {"probe_init_ex_flag_00ffffff", 0, 0x00ffffff},
            {"probe_init_ex_flag_02000000", 0, 0x02000000},
            {"probe_init_ex_flag_04000000", 0, 0x04000000},
            {"probe_init_ex_flag_08000000", 0, 0x08000000},
            {"probe_init_ex_flag_20000000", 0, 0x20000000},
            {"probe_init_ex_flag_40000000", 0, 0x40000000},
        };
        for (size_t i = 0; i < sizeof probes / sizeof probes[0]; i++) {
            ORACLE_CALL(ok, InitializeCriticalSectionEx(&s, probes[i].spin, probes[i].flags));
            oracle_case(probes[i].name);
            o_bool("ret", ok);
            o_lasterr();
            if (ok) {
                DWORD prev = SetCriticalSectionSpinCount(&s, 0);
                o_hex32("spin_stored", prev);
                DeleteCriticalSection(&s);
            }
            oracle_case_end();
        }
        ORACLE_CALL(ok, InitializeCriticalSectionAndSpinCount(&s, 0x01000400));
        oracle_case("probe_init_spin_01000400");
        o_bool("ret", ok);
        o_lasterr();
        if (ok) {
            o_hex32("spin_stored", SetCriticalSectionSpinCount(&s, 0));
            DeleteCriticalSection(&s);
        }
        oracle_case_end();
        ORACLE_CALL(ok, InitializeCriticalSectionAndSpinCount(&s, 0xffffffff));
        oracle_case("probe_init_spin_ffffffff");
        o_bool("ret", ok);
        o_lasterr();
        if (ok) {
            o_hex32("spin_stored", SetCriticalSectionSpinCount(&s, 0));
            DeleteCriticalSection(&s);
        }
        oracle_case_end();
        InitializeCriticalSection(&s);
        oracle_case("probe_set_spin_high_bits");
        o_hex32("prev", SetCriticalSectionSpinCount(&s, 0xff000123));
        o_hex32("stored", SetCriticalSectionSpinCount(&s, 0));
        oracle_case_end();
        DeleteCriticalSection(&s);
    }

    /* ---- delete and reinitialise ---- */
    {
        CRITICAL_SECTION s;
        InitializeCriticalSection(&s);
        EnterCriticalSection(&s);
        LeaveCriticalSection(&s);
        SetLastError(ORACLE_SENTINEL);
        DeleteCriticalSection(&s);
        oracle_last_error = GetLastError();
        oracle_case("delete_keeps_last_error");
        o_lasterr();
        oracle_case_end();
        InitializeCriticalSection(&s);
        state("reinit_state", &s);
        ok = TryEnterCriticalSection(&s);
        oracle_case("reinit_try_enter");
        o_bool("ret", ok);
        oracle_case_end();
        LeaveCriticalSection(&s);
        DeleteCriticalSection(&s);
    }

    /* ---- deep recursion ---- */
    {
        CRITICAL_SECTION s;
        InitializeCriticalSection(&s);
        for (int i = 0; i < 1000; i++) EnterCriticalSection(&s);
        state("recursion_1000", &s);
        for (int i = 0; i < 999; i++) LeaveCriticalSection(&s);
        state("recursion_back_to_1", &s);
        LeaveCriticalSection(&s);
        oracle_case("recursion_released_other_thread");
        o_i64("got", try_from_other_thread(&s));
        oracle_case_end();
        DeleteCriticalSection(&s);
    }

    return oracle_end();
}
