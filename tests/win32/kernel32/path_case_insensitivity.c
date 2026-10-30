/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: Win32 path case-insensitivity and case preservation.
 * Names are matched case-insensitively, but the case used at creation is kept.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/fileio/naming-a-file
 *       https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-findfirstfilew
 *       https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getlongpathnamew
 */
#include "oracle.h"

static void open_case(const char *name, const wchar_t *path, DWORD disposition)
{
    HANDLE h;
    ORACLE_CALL(h, CreateFileW(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, disposition, 0, NULL));
    oracle_case(name);
    o_handle("ret", h);
    o_lasterr();
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    o_dir_listing("dir_sorted", L".", 1);
    oracle_case_end();
}

static void find_case(const char *name, const wchar_t *pattern)
{
    WIN32_FIND_DATAW fd;
    HANDLE f;
    ORACLE_CALL(f, FindFirstFileW(pattern, &fd));
    oracle_case(name);
    o_handle("ret", f);
    o_lasterr();
    if (f != INVALID_HANDLE_VALUE) {
        o_wstr("found_name", fd.cFileName);
        FindClose(f);
    }
    oracle_case_end();
}

int main(void)
{
    oracle_begin("kernel32/path_case_insensitivity");

    HANDLE h = CreateFileW(L"MixedCase.TxT", GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL);
    CloseHandle(h);
    CreateDirectoryW(L"SubDir", NULL);
    h = CreateFileW(L"SubDir\\Inner.dat", GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL);
    CloseHandle(h);

    open_case("open_lowercase", L"mixedcase.txt", OPEN_EXISTING);
    open_case("open_uppercase", L"MIXEDCASE.TXT", OPEN_EXISTING);
    open_case("create_new_other_case_exists", L"MIXEDCASE.txt", CREATE_NEW);
    open_case("create_always_other_case_keeps_name", L"mixedCASE.txt", CREATE_ALWAYS);
    open_case("open_dir_component_other_case", L"subdir\\INNER.DAT", OPEN_EXISTING);

    find_case("find_exact_lowercase_query", L"mixedcase.txt");
    find_case("find_upper_dir_query", L"SUBDIR\\inner.dat");
    find_case("find_wildcard_upper", L"MIXED*");

    {
        DWORD a;
        ORACLE_CALL(a, GetFileAttributesW(L"SUBDIR"));
        oracle_case("attrs_dir_other_case");
        o_hex32("ret", a);
        o_lasterr();
        oracle_case_end();
    }
    {   /* GetLongPathNameW returns the on-disk case of each component. */
        wchar_t out[MAX_PATH] = L"";
        DWORD n;
        ORACLE_CALL(n, GetLongPathNameW(L"subdir\\inner.dat", out, MAX_PATH));
        oracle_case("long_path_name_restores_case");
        o_u32("ret", n);
        o_lasterr();
        o_wstr("path", out);
        oracle_case_end();
    }
    {   /* Rename that changes only case. */
        BOOL ok;
        ORACLE_CALL(ok, MoveFileW(L"MixedCase.TxT", L"MIXEDCASE.TXT"));
        oracle_case("rename_case_only");
        o_bool("ret", ok);
        o_lasterr();
        o_dir_listing("dir_sorted", L".", 1);
        oracle_case_end();
    }
    {
        BOOL ok;
        ORACLE_CALL(ok, DeleteFileW(L"mixedcase.txt"));
        oracle_case("delete_other_case");
        o_bool("ret", ok);
        o_lasterr();
        o_dir_listing("dir_sorted", L".", 1);
        oracle_case_end();
    }

    return oracle_end();
}
