/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: error codes for invalid inputs across kernel32. This covers
 * NULL and odd arguments that the docs allow, or that commonly occur in real
 * apps, but avoids inputs that are documented to crash (undefined behaviour).
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/handleapi/nf-handleapi-closehandle
 *       https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getfullpathnamew
 *       https://learn.microsoft.com/en-us/windows/win32/api/processenv/nf-processenv-getenvironmentvariablew
 *       https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-setfilepointerex
 *       https://learn.microsoft.com/en-us/windows/win32/debug/system-error-codes
 */
#include "oracle.h"

int main(void)
{
    oracle_begin("kernel32/invalid_params");

    {   /* Pseudo-handle for the current process: closing it is documented as harmless. */
        BOOL ok;
        ORACLE_CALL(ok, CloseHandle(GetCurrentProcess()));
        oracle_case("close_current_process_pseudo");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
    }
    {
        BOOL ok;
        ORACLE_CALL(ok, CloseHandle(NULL));
        oracle_case("close_null");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
    }
    {
        BOOL ok;
        HANDLE h = CreateFileW(L"x", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        CloseHandle(h);
        ORACLE_CALL(ok, CloseHandle(h));
        oracle_case("close_twice");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
    }
    {   /* GetFullPathNameW with a too-small buffer reports the required size. */
        wchar_t buf[2];
        DWORD n;
        ORACLE_CALL(n, GetFullPathNameW(L"abc", 2, buf, NULL));
        DWORD n2 = GetFullPathNameW(L"abc", 0, NULL, NULL);
        oracle_case("fullpath_buffer_too_small");
        /* Absolute paths are machine-specific: report only how much larger the
         * result is than the cwd, which is stable. */
        DWORD cwd = GetCurrentDirectoryW(0, NULL); /* includes the NUL */
        o_i64("ret_minus_cwd_len", (long long)n - (long long)cwd);
        o_i64("size_query_minus_cwd_len", (long long)n2 - (long long)cwd);
        o_lasterr();
        oracle_case_end();
    }
    {
        DWORD n;
        ORACLE_CALL(n, GetFullPathNameW(L"", MAX_PATH, (wchar_t[MAX_PATH]){0}, NULL));
        oracle_case("fullpath_empty");
        o_u32("ret", n);
        o_lasterr();
        oracle_case_end();
    }
    {
        wchar_t buf[16];
        DWORD n;
        ORACLE_CALL(n, GetEnvironmentVariableW(L"SETU_ORACLE_DEFINITELY_UNSET", buf, 16));
        oracle_case("getenv_missing");
        o_u32("ret", n);
        o_lasterr();
        oracle_case_end();
    }
    {
        wchar_t buf[2];
        DWORD n;
        SetEnvironmentVariableW(L"SETU_ORACLE_VAR", L"hello");
        ORACLE_CALL(n, GetEnvironmentVariableW(L"SETU_ORACLE_VAR", buf, 2));
        oracle_case("getenv_buffer_too_small");
        o_u32("ret", n);
        o_lasterr();
        oracle_case_end();
    }
    {
        wchar_t buf[16];
        DWORD n;
        ORACLE_CALL(n, GetEnvironmentVariableW(L"setu_oracle_var", buf, 16));
        oracle_case("getenv_case_insensitive");
        o_u32("ret", n);
        o_lasterr();
        if (n && n < 16) o_wstr("value", buf);
        oracle_case_end();
    }
    {   /* Negative absolute file position. */
        HANDLE h = CreateFileW(L"x", GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        LARGE_INTEGER d = {.QuadPart = -1}, pos = {.QuadPart = 12345};
        BOOL ok;
        ORACLE_CALL(ok, SetFilePointerEx(h, d, &pos, FILE_BEGIN));
        oracle_case("seek_negative");
        o_bool("ret", ok);
        o_lasterr();
        o_i64("new_pos", pos.QuadPart);
        oracle_case_end();
        ORACLE_CALL(ok, SetFilePointerEx(h, d, &pos, 7));
        oracle_case("seek_bad_method");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        CloseHandle(h);
    }
    {   /* WaitForSingleObject on an invalid handle. */
        DWORD r;
        ORACLE_CALL(r, WaitForSingleObject((HANDLE)(ULONG_PTR)0x7ffc, 0));
        oracle_case("wait_bogus_handle");
        o_hex32("ret", r);
        o_lasterr();
        oracle_case_end();
    }
    {   /* SetLastError / GetLastError round-trip, including a large value. */
        SetLastError(0x20001234u);
        DWORD e = GetLastError();
        oracle_case("last_error_roundtrip");
        o_hex32("value", e);
        oracle_case_end();
    }

    return oracle_end();
}
