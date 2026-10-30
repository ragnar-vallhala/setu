/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: GetFileAttributesW / SetFileAttributesW on files, directories,
 * missing paths, and DOS attribute round-trips.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getfileattributesw
 *       https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-setfileattributesw
 *       https://learn.microsoft.com/en-us/windows/win32/fileio/file-attribute-constants
 */
#include "oracle.h"

static void get_case(const char *name, const wchar_t *path)
{
    DWORD a;
    ORACLE_CALL(a, GetFileAttributesW(path));
    oracle_case(name);
    o_hex32("ret", a);
    o_lasterr();
    oracle_case_end();
}

static void set_then_get(const char *name, const wchar_t *path, DWORD attrs)
{
    BOOL ok;
    ORACLE_CALL(ok, SetFileAttributesW(path, attrs));
    DWORD err = oracle_last_error;
    oracle_case(name);
    o_bool("set_ret", ok);
    oracle_last_error = err;
    o_lasterr();
    o_hex32("get", GetFileAttributesW(path));
    oracle_case_end();
}

int main(void)
{
    oracle_begin("kernel32/GetFileAttributesW");

    HANDLE h = CreateFileW(L"f.txt", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    CloseHandle(h);
    CreateDirectoryW(L"sub", NULL);

    get_case("plain_file", L"f.txt");
    get_case("directory", L"sub");
    get_case("directory_trailing_backslash", L"sub\\");
    get_case("file_trailing_backslash", L"f.txt\\");
    get_case("missing_file", L"missing.txt");
    get_case("missing_parent", L"nodir\\missing.txt");
    get_case("current_dir_dot", L".");
    get_case("empty_string", L"");
    get_case("wildcard", L"*.txt");

    set_then_get("set_readonly", L"f.txt", FILE_ATTRIBUTE_READONLY);
    set_then_get("set_hidden_system", L"f.txt", FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
    set_then_get("set_normal", L"f.txt", FILE_ATTRIBUTE_NORMAL);
    set_then_get("set_zero", L"f.txt", 0);
    set_then_get("set_directory_bit_on_file", L"f.txt", FILE_ATTRIBUTE_DIRECTORY);
    set_then_get("set_hidden_on_dir", L"sub", FILE_ATTRIBUTE_HIDDEN);
    set_then_get("set_on_missing", L"missing.txt", FILE_ATTRIBUTE_HIDDEN);

    {   /* Attributes passed at creation time. */
        HANDLE c = CreateFileW(L"hid.txt", GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_HIDDEN, NULL);
        CloseHandle(c);
        get_case("created_hidden", L"hid.txt");
    }

    return oracle_end();
}
