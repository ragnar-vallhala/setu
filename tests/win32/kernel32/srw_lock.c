/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: slim reader/writer locks: TryAcquire* return values in every
 * state, from the owning thread and from other threads, and the hand-off
 * from a blocked exclusive waiter.
 *
 * Other threads are always joined with INFINITE before results are printed.
 * The lock word is only checked for zero / non-zero, never printed.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/sync/slim-reader-writer--srw--locks
 *       https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-initializesrwlock
 *       https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-tryacquiresrwlockexclusive
 *       https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-tryacquiresrwlockshared
 *       https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-acquiresrwlockexclusive
 *       https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-acquiresrwlockshared
 *       https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-releasesrwlockexclusive
 *       https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-releasesrwlockshared
 */
#include "oracle.h"

static SRWLOCK lock = SRWLOCK_INIT;

/* Other-thread probes. arg: 0 = try exclusive, 1 = try shared. Returns the
 * BOOLEAN value, and releases what it got. */
static DWORD WINAPI try_other(void *arg)
{
    BOOLEAN r;
    if (arg) {
        r = TryAcquireSRWLockShared(&lock);
        if (r) ReleaseSRWLockShared(&lock);
    } else {
        r = TryAcquireSRWLockExclusive(&lock);
        if (r) ReleaseSRWLockExclusive(&lock);
    }
    return r;
}

static int other(int shared)
{
    DWORD code = 99;
    HANDLE h = CreateThread(NULL, 0, try_other, (void *)(ULONG_PTR)shared, 0, NULL);
    WaitForSingleObject(h, INFINITE);
    GetExitCodeThread(h, &code);
    CloseHandle(h);
    return (int)code;
}

#define TRY_CASE(name, expr)                \
    do {                                    \
        BOOLEAN r_;                         \
        ORACLE_CALL(r_, (expr));            \
        oracle_case(name);                  \
        o_u32("ret", r_);                   \
        o_lasterr();                        \
        oracle_case_end();                  \
    } while (0)

static void others(const char *name)
{
    oracle_case(name);
    o_i64("other_exclusive", other(0));
    o_i64("other_shared", other(1));
    o_bool("word_nonzero", lock.Ptr != NULL);
    oracle_case_end();
}

/* Blocking hand-off: thread waits in AcquireSRWLockExclusive. */
static HANDLE ready;
static volatile LONG got_it;
static DWORD WINAPI acquire_blocking(void *arg)
{
    (void)arg;
    SetEvent(ready);
    AcquireSRWLockExclusive(&lock);
    InterlockedExchange(&got_it, 1);
    ReleaseSRWLockExclusive(&lock);
    return 7;
}

/* Several threads take the lock shared at once: each acquires shared, signals,
 * and waits for the release event before letting go. */
static HANDLE shared_go;
static volatile LONG shared_inside;
static DWORD WINAPI shared_holder(void *arg)
{
    HANDLE in = arg;
    AcquireSRWLockShared(&lock);
    InterlockedIncrement(&shared_inside);
    SetEvent(in);
    WaitForSingleObject(shared_go, INFINITE);
    ReleaseSRWLockShared(&lock);
    return 0;
}

int main(void)
{
    oracle_begin("kernel32/srw_lock");

    {
        SRWLOCK l;
        memset(&l, 0xcc, sizeof l);
        SetLastError(ORACLE_SENTINEL);
        InitializeSRWLock(&l);
        oracle_last_error = GetLastError();
        oracle_case("initialize_zeroes");
        o_bool("word_zero", l.Ptr == NULL);
        o_lasterr();
        oracle_case_end();
        oracle_case("srwlock_init_is_zero");
        SRWLOCK z = SRWLOCK_INIT;
        o_bool("word_zero", z.Ptr == NULL);
        o_u32("size", sizeof(SRWLOCK));
        oracle_case_end();
    }

    others("free_lock");

    TRY_CASE("try_exclusive_free", TryAcquireSRWLockExclusive(&lock));
    others("exclusive_held");
    TRY_CASE("try_exclusive_again_same_thread", TryAcquireSRWLockExclusive(&lock));
    TRY_CASE("try_shared_while_exclusive_same_thread", TryAcquireSRWLockShared(&lock));
    SetLastError(ORACLE_SENTINEL);
    ReleaseSRWLockExclusive(&lock);
    oracle_last_error = GetLastError();
    oracle_case("release_exclusive");
    o_lasterr();
    o_bool("word_zero", lock.Ptr == NULL);
    oracle_case_end();
    others("after_release_exclusive");

    TRY_CASE("try_shared_free", TryAcquireSRWLockShared(&lock));
    others("shared_held_once");
    TRY_CASE("try_shared_second", TryAcquireSRWLockShared(&lock));
    TRY_CASE("try_exclusive_while_shared", TryAcquireSRWLockExclusive(&lock));
    ReleaseSRWLockShared(&lock);
    others("shared_held_after_one_release");
    ReleaseSRWLockShared(&lock);
    others("after_release_all_shared");
    oracle_case("word_zero_after_shared_release");
    o_bool("word_zero", lock.Ptr == NULL);
    oracle_case_end();

    /* Blocking acquire (non-try) paths. */
    AcquireSRWLockExclusive(&lock);
    others("acquire_exclusive_held");
    ReleaseSRWLockExclusive(&lock);
    AcquireSRWLockShared(&lock);
    AcquireSRWLockShared(&lock);
    others("acquire_shared_twice_held");
    ReleaseSRWLockShared(&lock);
    ReleaseSRWLockShared(&lock);
    others("acquire_shared_released");

    /* ---- exclusive waiter is handed the lock ---- */
    {
        ready = CreateEventW(NULL, TRUE, FALSE, NULL);
        AcquireSRWLockExclusive(&lock);
        HANDLE h = CreateThread(NULL, 0, acquire_blocking, NULL, 0, NULL);
        WaitForSingleObject(ready, INFINITE);
        LONG before = got_it;
        ReleaseSRWLockExclusive(&lock);
        WaitForSingleObject(h, INFINITE);
        DWORD code = 0;
        GetExitCodeThread(h, &code);
        oracle_case("exclusive_waiter_handoff");
        o_bool("not_before_release", before == 0);
        o_bool("got_after_release", got_it == 1);
        o_u32("thread_exit", code);
        oracle_case_end();
        CloseHandle(h);
        CloseHandle(ready);
        others("after_handoff");
    }

    /* ---- three concurrent shared owners ---- */
    {
        HANDLE ins[3], th[3];
        shared_go = CreateEventW(NULL, TRUE, FALSE, NULL);
        for (int i = 0; i < 3; i++) {
            ins[i] = CreateEventW(NULL, TRUE, FALSE, NULL);
            th[i] = CreateThread(NULL, 0, shared_holder, ins[i], 0, NULL);
        }
        WaitForMultipleObjects(3, ins, TRUE, INFINITE);
        oracle_case("three_shared_owners");
        o_i64("inside", shared_inside);
        oracle_case_end();
        TRY_CASE("try_exclusive_with_three_readers", TryAcquireSRWLockExclusive(&lock));
        TRY_CASE("try_shared_with_three_readers", TryAcquireSRWLockShared(&lock));
        ReleaseSRWLockShared(&lock);
        SetEvent(shared_go);
        WaitForMultipleObjects(3, th, TRUE, INFINITE);
        for (int i = 0; i < 3; i++) {
            CloseHandle(th[i]);
            CloseHandle(ins[i]);
        }
        CloseHandle(shared_go);
        others("after_three_readers");
        TRY_CASE("try_exclusive_after_readers", TryAcquireSRWLockExclusive(&lock));
        ReleaseSRWLockExclusive(&lock);
    }

    /* ---- many uncontended cycles leave the word zero ---- */
    {
        for (int i = 0; i < 10000; i++) {
            AcquireSRWLockExclusive(&lock);
            ReleaseSRWLockExclusive(&lock);
            AcquireSRWLockShared(&lock);
            ReleaseSRWLockShared(&lock);
        }
        oracle_case("cycles_leave_word_zero");
        o_bool("word_zero", lock.Ptr == NULL);
        oracle_case_end();
    }

    return oracle_end();
}
