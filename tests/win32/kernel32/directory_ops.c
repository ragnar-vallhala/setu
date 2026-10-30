/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: CreateDirectoryW, RemoveDirectoryW, DeleteFileW, MoveFileExW
 * success paths and error codes.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createdirectoryw
 *       https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-removedirectoryw
 *       https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-deletefilew
 *       https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-movefileexw
 */
#include "oracle.h"

#define BOOL_CASE(name, expr)          \
    do {                               \
        BOOL ok_;                      \
        ORACLE_CALL(ok_, (expr));      \
        oracle_case(name);             \
        o_bool("ret", ok_);            \
        o_lasterr();                   \
        o_dir_listing("dir_sorted", L".", 1); \
        oracle_case_end();             \
    } while (0)

static void touch(const wchar_t *p, const char *data)
{
    HANDLE h = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    DWORD n;
    WriteFile(h, data, (DWORD)strlen(data), &n, NULL);
    CloseHandle(h);
}

int main(void)
{
    oracle_begin("kernel32/directory_ops");

    BOOL_CASE("mkdir_new", CreateDirectoryW(L"d1", NULL));
    BOOL_CASE("mkdir_exists", CreateDirectoryW(L"d1", NULL));
    BOOL_CASE("mkdir_exists_other_case", CreateDirectoryW(L"D1", NULL));
    BOOL_CASE("mkdir_missing_parent", CreateDirectoryW(L"nope\\d2", NULL));
    touch(L"file", "x");
    BOOL_CASE("mkdir_over_file", CreateDirectoryW(L"file", NULL));
    BOOL_CASE("mkdir_empty_name", CreateDirectoryW(L"", NULL));

    touch(L"d1\\child", "y");
    BOOL_CASE("rmdir_not_empty", RemoveDirectoryW(L"d1"));
    BOOL_CASE("rmdir_on_file", RemoveDirectoryW(L"file"));
    BOOL_CASE("rmdir_missing", RemoveDirectoryW(L"missing"));

    BOOL_CASE("delete_on_directory", DeleteFileW(L"d1"));
    BOOL_CASE("delete_missing", DeleteFileW(L"missing"));
    BOOL_CASE("delete_missing_parent", DeleteFileW(L"nope\\x"));
    BOOL_CASE("delete_child", DeleteFileW(L"d1\\child"));
    BOOL_CASE("rmdir_now_empty", RemoveDirectoryW(L"d1"));

    touch(L"ro", "r");
    SetFileAttributesW(L"ro", FILE_ATTRIBUTE_READONLY);
    BOOL_CASE("delete_readonly", DeleteFileW(L"ro"));
    SetFileAttributesW(L"ro", FILE_ATTRIBUTE_NORMAL);

    {   /* Deleting a file that is open without FILE_SHARE_DELETE. */
        HANDLE h = CreateFileW(L"file", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        BOOL_CASE("delete_open_no_share_delete", DeleteFileW(L"file"));
        CloseHandle(h);
    }

    touch(L"src", "s");
    touch(L"dst", "d");
    BOOL_CASE("move_onto_existing_no_flag", MoveFileExW(L"src", L"dst", 0));
    BOOL_CASE("move_onto_existing_replace", MoveFileExW(L"src", L"dst", MOVEFILE_REPLACE_EXISTING));
    oracle_case("move_replace_result");
    o_file_state("dst", L"dst");
    oracle_case_end();
    BOOL_CASE("move_missing_source", MoveFileExW(L"src", L"dst2", 0));

    return oracle_end();
}
