/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: LockFile / LockFileEx / UnlockFile byte-range locks, and how
 * they affect ReadFile/WriteFile through another handle.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-lockfile
 *       https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-lockfileex
 *       https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-unlockfile
 *       https://learn.microsoft.com/en-us/windows/win32/fileio/locking-and-unlocking-byte-ranges-in-files
 */
#include "oracle.h"

static HANDLE a, b;

#define BOOL_CASE(name, expr)     \
    do {                          \
        BOOL ok_;                 \
        ORACLE_CALL(ok_, (expr)); \
        oracle_case(name);        \
        o_bool("ret", ok_);       \
        o_lasterr();              \
        oracle_case_end();        \
    } while (0)

static BOOL read_at(HANDLE h, DWORD off, DWORD len, DWORD *got)
{
    char buf[64];
    OVERLAPPED ov = {0};
    ov.Offset = off;
    *got = 99;
    return ReadFile(h, buf, len, got, &ov);
}

static BOOL write_at(HANDLE h, DWORD off, const char *s)
{
    DWORD n;
    OVERLAPPED ov = {0};
    ov.Offset = off;
    return WriteFile(h, s, (DWORD)strlen(s), &n, &ov);
}

static void read_case(const char *name, HANDLE h, DWORD off, DWORD len)
{
    DWORD got;
    BOOL ok;
    ORACLE_CALL(ok, read_at(h, off, len, &got));
    oracle_case(name);
    o_bool("ret", ok);
    o_lasterr();
    o_u32("read", got);
    oracle_case_end();
}

static BOOL lockex(HANDLE h, DWORD flags, DWORD off, DWORD len)
{
    OVERLAPPED ov = {0};
    ov.Offset = off;
    return LockFileEx(h, flags, 0, len, 0, &ov);
}

static BOOL unlockex(HANDLE h, DWORD off, DWORD len)
{
    OVERLAPPED ov = {0};
    ov.Offset = off;
    return UnlockFileEx(h, 0, len, 0, &ov);
}

int main(void)
{
    oracle_begin("kernel32/LockFileEx");

    a = CreateFileW(L"f", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                    CREATE_ALWAYS, 0, NULL);
    b = CreateFileW(L"f", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                    OPEN_EXISTING, 0, NULL);
    write_at(a, 0, "0123456789abcdefghij");

    BOOL_CASE("lock_a_0_10", LockFile(a, 0, 0, 10, 0));
    BOOL_CASE("lock_b_overlap", LockFile(b, 5, 0, 10, 0));
    BOOL_CASE("lock_a_same_range_again", LockFile(a, 0, 0, 10, 0));
    BOOL_CASE("lock_b_adjacent", LockFile(b, 10, 0, 5, 0));
    read_case("read_b_in_locked", b, 2, 4);
    read_case("read_b_straddling", b, 8, 4);
    read_case("read_a_own_lock", a, 2, 4);
    BOOL_CASE("write_b_in_locked", write_at(b, 2, "XX"));
    BOOL_CASE("write_a_own_lock", write_at(a, 2, "aa"));
    read_case("read_a_in_b_lock", a, 11, 2);

    BOOL_CASE("unlock_a_partial_range", UnlockFile(a, 0, 0, 5, 0));
    BOOL_CASE("unlock_a_not_locked", UnlockFile(a, 30, 0, 5, 0));
    BOOL_CASE("unlock_b_others_lock", UnlockFile(b, 0, 0, 10, 0));
    BOOL_CASE("unlock_a_exact", UnlockFile(a, 0, 0, 10, 0));
    BOOL_CASE("unlock_a_second_lock", UnlockFile(a, 0, 0, 10, 0));
    BOOL_CASE("unlock_a_third_time", UnlockFile(a, 0, 0, 10, 0));
    read_case("read_b_after_unlock", b, 2, 4);

    /* Shared locks. */
    BOOL_CASE("shared_a", lockex(a, LOCKFILE_FAIL_IMMEDIATELY, 20, 10));
    BOOL_CASE("shared_b_same_range", lockex(b, LOCKFILE_FAIL_IMMEDIATELY, 20, 10));
    BOOL_CASE("exclusive_b_over_shared",
              lockex(b, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 25, 2));
    read_case("read_a_in_shared", a, 20, 0);
    BOOL_CASE("write_a_in_own_shared", write_at(a, 21, "S"));
    BOOL_CASE("unlockex_a", unlockex(a, 20, 10));
    BOOL_CASE("unlockex_b", unlockex(b, 20, 10));

    /* Lock beyond EOF and zero-length locks. */
    BOOL_CASE("lock_beyond_eof", LockFile(a, 1000, 0, 10, 0));
    BOOL_CASE("lock_zero_length", LockFile(b, 1000, 0, 0, 0));
    BOOL_CASE("lock_zero_length_again", LockFile(b, 1000, 0, 0, 0));
    BOOL_CASE("unlock_zero_length", UnlockFile(b, 1000, 0, 0, 0));
    BOOL_CASE("lock_whole_range", LockFile(b, 0, 0, 0xffffffff, 0xffffffff));

    /* Locks go away when the handle closes. */
    CloseHandle(a);
    BOOL_CASE("lock_after_owner_closed", LockFile(b, 1000, 0, 10, 0));
    CloseHandle(b);

    {   /* Read-only handle with GENERIC_READ can lock. */
        HANDLE r = CreateFileW(L"f", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                               OPEN_EXISTING, 0, NULL);
        BOOL_CASE("lock_on_read_handle", LockFile(r, 0, 0, 1, 0));
        CloseHandle(r);
    }
    BOOL_CASE("lock_invalid_handle", LockFile(INVALID_HANDLE_VALUE, 0, 0, 1, 0));

    return oracle_end();
}
