/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: how byte-range locks (LockFile / LockFileEx) restrict ReadFile
 * and WriteFile: lock boundaries, zero-length I/O, file-pointer I/O, shared
 * locks held by the caller or by another handle, locks beyond end of file,
 * locks taken through a read-only handle, and duplicated handles.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/fileio/locking-and-unlocking-byte-ranges-in-files
 *       https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-lockfileex
 *       https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-lockfile
 *       https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-readfile
 *       https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-writefile
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

static DWORD file_pos(HANDLE h)
{
    return SetFilePointer(h, 0, NULL, FILE_CURRENT);
}

static void read_case(const char *name, HANDLE h, DWORD off, DWORD len)
{
    char buf[64];
    OVERLAPPED ov = {0};
    DWORD got = 99;
    BOOL ok;
    ov.Offset = off;
    ORACLE_CALL(ok, ReadFile(h, buf, len, &got, &ov));
    oracle_case(name);
    o_bool("ret", ok);
    o_lasterr();
    o_u32("read", got);
    oracle_case_end();
}

static void write_case(const char *name, HANDLE h, DWORD off, const char *s)
{
    OVERLAPPED ov = {0};
    DWORD n = 99;
    BOOL ok;
    ov.Offset = off;
    ORACLE_CALL(ok, WriteFile(h, s, (DWORD)strlen(s), &n, &ov));
    oracle_case(name);
    o_bool("ret", ok);
    o_lasterr();
    o_u32("written", n);
    oracle_case_end();
}

/* I/O at the file pointer (no OVERLAPPED): also report where the pointer ends up. */
static void read_ptr_case(const char *name, HANDLE h, DWORD pos, DWORD len)
{
    char buf[64];
    DWORD got = 99;
    BOOL ok;
    SetFilePointer(h, (LONG)pos, NULL, FILE_BEGIN);
    ORACLE_CALL(ok, ReadFile(h, buf, len, &got, NULL));
    oracle_case(name);
    o_bool("ret", ok);
    o_lasterr();
    o_u32("read", got);
    o_u32("pos", file_pos(h));
    oracle_case_end();
}

static void write_ptr_case(const char *name, HANDLE h, DWORD pos, const char *s)
{
    DWORD n = 99;
    BOOL ok;
    SetFilePointer(h, (LONG)pos, NULL, FILE_BEGIN);
    ORACLE_CALL(ok, WriteFile(h, s, (DWORD)strlen(s), &n, NULL));
    oracle_case(name);
    o_bool("ret", ok);
    o_lasterr();
    o_u32("written", n);
    o_u32("pos", file_pos(h));
    oracle_case_end();
}

static BOOL lockex(HANDLE h, DWORD flags, DWORD off, DWORD len)
{
    OVERLAPPED ov = {0};
    ov.Offset = off;
    return LockFileEx(h, flags | LOCKFILE_FAIL_IMMEDIATELY, 0, len, 0, &ov);
}

static BOOL unlockex(HANDLE h, DWORD off, DWORD len)
{
    OVERLAPPED ov = {0};
    ov.Offset = off;
    return UnlockFileEx(h, 0, len, 0, &ov);
}

int main(void)
{
    DWORD n;
    oracle_begin("kernel32/LockFile_io");

    a = CreateFileW(L"f", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                    CREATE_ALWAYS, 0, NULL);
    b = CreateFileW(L"f", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                    OPEN_EXISTING, 0, NULL);
    WriteFile(a, "0123456789abcdefghijklmnopqrstuvwxyzABCD", 40, &n, NULL);

    /* Exclusive lock held by a on [4, 8). */
    BOOL_CASE("excl_a_4_8", lockex(a, LOCKFILE_EXCLUSIVE_LOCK, 4, 4));
    read_case("read_b_ending_at_lock", b, 0, 4);
    read_case("read_b_starting_after_lock", b, 8, 4);
    read_case("read_b_last_byte", b, 7, 4);
    read_case("read_b_first_byte", b, 1, 4);
    read_case("read_b_zero_len_in_lock", b, 5, 0);
    read_case("read_b_covering_lock", b, 0, 20);
    write_case("write_b_ending_at_lock", b, 2, "..");
    write_case("write_b_last_byte", b, 7, "..");
    read_ptr_case("read_ptr_b_in_lock", b, 5, 2);
    write_ptr_case("write_ptr_b_in_lock", b, 5, "..");
    read_ptr_case("read_ptr_b_outside_lock", b, 10, 2);
    write_ptr_case("write_ptr_a_in_own_lock", a, 5, "x");
    {
        HANDLE d = NULL;
        DuplicateHandle(GetCurrentProcess(), a, GetCurrentProcess(), &d, 0, FALSE,
                        DUPLICATE_SAME_ACCESS);
        read_case("read_dup_of_a_in_lock", d, 4, 2);
        write_case("write_dup_of_a_in_lock", d, 4, "y");
        CloseHandle(d);
    }
    BOOL_CASE("unlock_a_4_8", unlockex(a, 4, 4));
    read_case("read_b_after_unlock", b, 4, 4);

    /* Shared locks on [20, 30). */
    BOOL_CASE("shared_a_20_30", lockex(a, 0, 20, 10));
    read_case("read_b_in_a_shared", b, 22, 4);
    write_case("write_b_in_a_shared", b, 22, "..");
    read_case("read_a_in_own_shared", a, 22, 4);
    write_case("write_a_in_own_shared", a, 22, "..");
    write_case("write_a_straddling_own_shared", a, 18, "....");
    BOOL_CASE("shared_b_20_30", lockex(b, 0, 20, 10));
    write_case("write_a_in_both_shared", a, 25, "..");
    BOOL_CASE("unlock_a_20_30", unlockex(a, 20, 10));
    write_case("write_b_in_own_shared_only", b, 25, "..");
    write_case("write_a_in_b_shared_only", a, 25, "..");
    read_case("read_a_in_b_shared_only", a, 25, 2);
    BOOL_CASE("unlock_b_20_30", unlockex(b, 20, 10));
    write_case("write_a_after_shared_gone", a, 25, "..");

    /* A shared lock on top of the caller's own exclusive lock. */
    BOOL_CASE("excl_a_30_34", lockex(a, LOCKFILE_EXCLUSIVE_LOCK, 30, 4));
    BOOL_CASE("shared_a_over_own_excl", lockex(a, 0, 30, 4));
    write_case("write_a_excl_and_shared", a, 31, ".");
    read_case("read_a_excl_and_shared", a, 31, 2);
    read_case("read_b_excl_and_shared", b, 31, 2);
    BOOL_CASE("unlock_a_30_34_first", unlockex(a, 30, 4));
    read_case("read_b_after_first_unlock", b, 31, 2);
    write_case("write_a_after_first_unlock", a, 31, ".");
    BOOL_CASE("unlock_a_30_34_second", unlockex(a, 30, 4));
    read_case("read_b_after_second_unlock", b, 31, 2);

    /* Lock beyond end of file (file is 40 bytes). */
    BOOL_CASE("excl_a_100_110", lockex(a, LOCKFILE_EXCLUSIVE_LOCK, 100, 10));
    read_case("read_b_beyond_eof_in_lock", b, 100, 4);
    read_case("read_b_beyond_eof_before_lock", b, 50, 4);
    read_case("read_b_from_eof_into_lock", b, 38, 70);
    write_case("write_b_beyond_eof_in_lock", b, 102, "..");
    write_case("write_b_extending_into_lock", b, 98, "....");
    read_case("read_a_beyond_eof_own_lock", a, 100, 4);
    oracle_case("size_after_lock_writes");
    o_u32("size", GetFileSize(a, NULL));
    oracle_case_end();
    BOOL_CASE("unlock_a_100_110", unlockex(a, 100, 10));

    /* Exclusive lock taken through a read-only handle. */
    {
        HANDLE r = CreateFileW(L"f", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                               OPEN_EXISTING, 0, NULL);
        BOOL_CASE("excl_readonly_10_14", lockex(r, LOCKFILE_EXCLUSIVE_LOCK, 10, 4));
        read_case("read_b_in_readonly_excl", b, 11, 2);
        write_case("write_b_in_readonly_excl", b, 11, "..");
        read_case("read_r_in_own_excl", r, 11, 2);
        CloseHandle(r);
        read_case("read_b_after_readonly_closed", b, 11, 2);
    }

    CloseHandle(a);
    CloseHandle(b);
    return oracle_end();
}
