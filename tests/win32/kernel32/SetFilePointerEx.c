/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: SetFilePointer / SetFilePointerEx, SetEndOfFile, GetFileSizeEx,
 * GetFileSize and reads/writes around the end of file.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-setfilepointerex
 *       https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-setfilepointer
 *       https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-setendoffile
 *       https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getfilesizeex
 *       https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getfilesize
 */
#include "oracle.h"

static HANDLE h;

static void seek_case(const char *name, long long dist, DWORD method)
{
    LARGE_INTEGER d, out;
    BOOL ok;
    d.QuadPart = dist;
    out.QuadPart = -12345;
    ORACLE_CALL(ok, SetFilePointerEx(h, d, &out, method));
    oracle_case(name);
    o_bool("ret", ok);
    o_lasterr();
    o_i64("new_pos", out.QuadPart);
    oracle_case_end();
}

static void size_case(const char *name)
{
    LARGE_INTEGER sz;
    BOOL ok;
    ORACLE_CALL(ok, GetFileSizeEx(h, &sz));
    oracle_case(name);
    o_bool("ret", ok);
    o_lasterr();
    o_i64("size", sz.QuadPart);
    oracle_case_end();
}

int main(void)
{
    oracle_begin("kernel32/SetFilePointerEx");

    h = CreateFileW(L"f.bin", GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    DWORD n;
    WriteFile(h, "0123456789", 10, &n, NULL);

    size_case("size_after_write");
    seek_case("seek_begin_0", 0, FILE_BEGIN);
    seek_case("seek_current_3", 3, FILE_CURRENT);
    seek_case("seek_end_minus_2", -2, FILE_END);
    seek_case("seek_end_0", 0, FILE_END);
    seek_case("seek_negative_from_begin", -1, FILE_BEGIN);
    seek_case("seek_negative_past_start_current", -100, FILE_CURRENT);
    seek_case("seek_past_eof", 100, FILE_BEGIN);
    size_case("size_unchanged_by_seek");
    seek_case("seek_bad_method", 0, 7);
    seek_case("seek_huge", 0x7fffffffffffffffLL, FILE_BEGIN);

    {   /* Read at a position beyond EOF: success, 0 bytes. */
        char buf[8];
        BOOL ok;
        LARGE_INTEGER d = {{100, 0}};
        SetFilePointerEx(h, d, NULL, FILE_BEGIN);
        n = 77;
        ORACLE_CALL(ok, ReadFile(h, buf, sizeof buf, &n, NULL));
        oracle_case("read_beyond_eof");
        o_bool("ret", ok);
        o_lasterr();
        o_u32("read", n);
        oracle_case_end();
    }

    {   /* Write beyond EOF: the gap is zero-filled. */
        LARGE_INTEGER d = {{14, 0}};
        BOOL ok;
        SetFilePointerEx(h, d, NULL, FILE_BEGIN);
        ORACLE_CALL(ok, WriteFile(h, "Z", 1, &n, NULL));
        oracle_case("write_beyond_eof");
        o_bool("ret", ok);
        o_lasterr();
        o_file_state("file", L"f.bin");
        oracle_case_end();
    }

    {   /* SetEndOfFile truncates and extends. */
        LARGE_INTEGER d = {{4, 0}};
        BOOL ok;
        SetFilePointerEx(h, d, NULL, FILE_BEGIN);
        ORACLE_CALL(ok, SetEndOfFile(h));
        oracle_case("set_end_truncate");
        o_bool("ret", ok);
        o_lasterr();
        o_file_state("file", L"f.bin");
        oracle_case_end();
        seek_case("pos_after_truncate", 0, FILE_CURRENT);

        d.QuadPart = 8;
        SetFilePointerEx(h, d, NULL, FILE_BEGIN);
        ORACLE_CALL(ok, SetEndOfFile(h));
        oracle_case("set_end_extend");
        o_bool("ret", ok);
        o_lasterr();
        o_file_state("file", L"f.bin");
        oracle_case_end();

        d.QuadPart = 2;
        SetFilePointerEx(h, d, NULL, FILE_BEGIN);
        ORACLE_CALL(ok, SetEndOfFile(h));
        seek_case("pos_kept_after_truncate_below", 0, FILE_CURRENT);
    }

    {   /* Legacy SetFilePointer: INVALID_SET_FILE_POINTER and last error. */
        DWORD r;
        LONG hi = 0;
        ORACLE_CALL(r, SetFilePointer(h, 5, NULL, FILE_BEGIN));
        oracle_case("legacy_seek_ok");
        o_u32("ret", r);
        o_lasterr();
        oracle_case_end();

        ORACLE_CALL(r, SetFilePointer(h, -10, NULL, FILE_BEGIN));
        oracle_case("legacy_seek_negative");
        o_hex32("ret", r);
        o_lasterr();
        oracle_case_end();

        hi = 1;
        ORACLE_CALL(r, SetFilePointer(h, 0, &hi, FILE_BEGIN));
        oracle_case("legacy_seek_high_part");
        o_hex32("ret", r);
        o_i64("high", hi);
        o_lasterr();
        oracle_case_end();

        hi = -1;
        ORACLE_CALL(r, SetFilePointer(h, -1, &hi, FILE_BEGIN));
        oracle_case("legacy_seek_minus_one_64");
        o_hex32("ret", r);
        o_i64("high", hi);
        o_lasterr();
        oracle_case_end();

        DWORD high = 77;
        ORACLE_CALL(r, GetFileSize(h, &high));
        oracle_case("legacy_get_size");
        o_u32("ret", r);
        o_u32("high", high);
        o_lasterr();
        oracle_case_end();
    }
    CloseHandle(h);

    {   /* Read-only handle: SetEndOfFile fails. */
        BOOL ok;
        h = CreateFileW(L"f.bin", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        ORACLE_CALL(ok, SetEndOfFile(h));
        oracle_case("set_end_readonly_handle");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        CloseHandle(h);
    }

    {   /* Directory handle. */
        LARGE_INTEGER sz;
        BOOL ok;
        CreateDirectoryW(L"dir", NULL);
        h = CreateFileW(L"dir", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                        FILE_FLAG_BACKUP_SEMANTICS, NULL);
        ORACLE_CALL(ok, GetFileSizeEx(h, &sz));
        oracle_case("size_of_directory");
        o_bool("ret", ok);
        o_lasterr();
        if (ok) o_i64("size", sz.QuadPart);
        oracle_case_end();
        seek_case("seek_directory", 5, FILE_BEGIN);
        CloseHandle(h);
    }

    {   /* Append-only handle (FILE_APPEND_DATA): writes go to the end. */
        BOOL ok;
        h = CreateFileW(L"f.bin", FILE_APPEND_DATA, 0, NULL, OPEN_EXISTING, 0, NULL);
        LARGE_INTEGER d = {{0, 0}};
        SetFilePointerEx(h, d, NULL, FILE_BEGIN);
        ORACLE_CALL(ok, WriteFile(h, "AP", 2, &n, NULL));
        oracle_case("append_only_write");
        o_bool("ret", ok);
        o_lasterr();
        CloseHandle(h);
        o_file_state("file", L"f.bin");
        oracle_case_end();
    }

    {   /* Invalid handle. */
        LARGE_INTEGER sz;
        BOOL ok;
        ORACLE_CALL(ok, GetFileSizeEx(INVALID_HANDLE_VALUE, &sz));
        oracle_case("size_invalid_handle");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
    }

    return oracle_end();
}
