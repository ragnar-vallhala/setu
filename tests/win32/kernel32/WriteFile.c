/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: WriteFile byte counts, gaps after seeking past EOF, append via
 * FILE_APPEND_DATA, access checks, invalid handles.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-writefile
 *       https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-setfilepointerex
 *       https://learn.microsoft.com/en-us/windows/win32/fileio/file-access-rights-constants
 */
#include "oracle.h"

static void write_case(const char *name, HANDLE h, const char *data, DWORD len, const wchar_t *state)
{
    DWORD wrote = 0xAAAAAAAAu;
    BOOL ok;
    ORACLE_CALL(ok, WriteFile(h, data, len, &wrote, NULL));
    oracle_case(name);
    o_bool("ret", ok);
    o_lasterr();
    o_u32("bytes_written", wrote);
    if (state) {
        FlushFileBuffers(h);
        o_file_state("file", state);
    }
    oracle_case_end();
}

int main(void)
{
    oracle_begin("kernel32/WriteFile");

    HANDLE h = CreateFileW(L"w.txt", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
    write_case("write_hello", h, "hello", 5, L"w.txt");
    write_case("write_zero_bytes", h, "x", 0, L"w.txt");

    {   /* Seek past EOF: the gap reads back as zeros. */
        LARGE_INTEGER d = {.QuadPart = 8};
        SetFilePointerEx(h, d, NULL, FILE_BEGIN);
        write_case("write_after_gap", h, "!", 1, L"w.txt");
    }
    {   /* Overwrite in the middle. */
        LARGE_INTEGER d = {.QuadPart = 1};
        SetFilePointerEx(h, d, NULL, FILE_BEGIN);
        write_case("overwrite_middle", h, "EL", 2, L"w.txt");
    }
    CloseHandle(h);

    HANDLE ro = CreateFileW(L"w.txt", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    write_case("write_read_only_handle", ro, "zz", 2, L"w.txt");
    CloseHandle(ro);

    {   /* FILE_APPEND_DATA without FILE_WRITE_DATA: writes always go to EOF. */
        HANDLE ap = CreateFileW(L"w.txt", FILE_APPEND_DATA | SYNCHRONIZE, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        LARGE_INTEGER d = {.QuadPart = 0};
        SetFilePointerEx(ap, d, NULL, FILE_BEGIN);
        write_case("append_data_only", ap, "++", 2, L"w.txt");
        CloseHandle(ap);
    }

    {   /* Writes to a file with FILE_ATTRIBUTE_READONLY can't even be opened for write. */
        HANDLE r = CreateFileW(L"ro.txt", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_READONLY, NULL);
        write_case("write_new_readonly_attr_file", r, "abc", 3, L"ro.txt");
        CloseHandle(r);
        HANDLE r2;
        ORACLE_CALL(r2, CreateFileW(L"ro.txt", GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL));
        oracle_case("reopen_readonly_attr_for_write");
        o_handle("ret", r2);
        o_lasterr();
        oracle_case_end();
        if (r2 != INVALID_HANDLE_VALUE) CloseHandle(r2);
        SetFileAttributesW(L"ro.txt", FILE_ATTRIBUTE_NORMAL);
    }

    write_case("write_invalid_handle_value", INVALID_HANDLE_VALUE, "a", 1, NULL);
    write_case("write_null_handle", NULL, "a", 1, NULL);

    return oracle_end();
}
