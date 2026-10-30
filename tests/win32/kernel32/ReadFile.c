/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: ReadFile byte counts, EOF behaviour, access checks, invalid handles.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-readfile
 *       https://learn.microsoft.com/en-us/windows/win32/fileio/testing-for-the-end-of-a-file
 */
#include "oracle.h"

static void read_case(const char *name, HANDLE h, DWORD want)
{
    unsigned char buf[64] = {0};
    DWORD got = 0xAAAAAAAAu;
    BOOL ok;
    ORACLE_CALL(ok, ReadFile(h, buf, want, &got, NULL));
    oracle_case(name);
    o_bool("ret", ok);
    o_lasterr();
    o_u32("bytes_read", got);
    if (ok && got <= sizeof buf) o_bytes("data", buf, got);
    oracle_case_end();
}

int main(void)
{
    oracle_begin("kernel32/ReadFile");

    HANDLE w = CreateFileW(L"r.txt", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    DWORD n;
    WriteFile(w, "0123456789", 10, &n, NULL);
    CloseHandle(w);

    HANDLE h = CreateFileW(L"r.txt", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    read_case("read_4", h, 4);
    read_case("read_rest_ask_more", h, 32);
    read_case("read_at_eof", h, 8);
    read_case("read_zero_bytes", h, 0);

    {   /* Seek past EOF, then read. */
        LARGE_INTEGER d = {.QuadPart = 100};
        SetFilePointerEx(h, d, NULL, FILE_BEGIN);
        read_case("read_past_eof", h, 4);
    }
    CloseHandle(h);

    HANDLE wo = CreateFileW(L"r.txt", GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    read_case("read_write_only_handle", wo, 4);
    CloseHandle(wo);

    read_case("read_invalid_handle_value", INVALID_HANDLE_VALUE, 4);
    read_case("read_null_handle", NULL, 4);

    {   /* A handle value that was closed. */
        HANDLE c = CreateFileW(L"r.txt", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        CloseHandle(c);
        read_case("read_closed_handle", c, 4);
    }

    {   /* Reading a directory handle. */
        CreateDirectoryW(L"d", NULL);
        HANDLE dh = CreateFileW(L"d", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                                FILE_FLAG_BACKUP_SEMANTICS, NULL);
        read_case("read_directory_handle", dh, 4);
        CloseHandle(dh);
    }

    {   /* Empty file. */
        HANDLE e = CreateFileW(L"empty.txt", GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        read_case("read_empty_file", e, 4);
        CloseHandle(e);
    }

    return oracle_end();
}
