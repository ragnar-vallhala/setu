/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: CopyFileW / CopyFileExW / MoveFileW, including attribute
 * propagation and error codes.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-copyfilew
 *       https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-copyfileexw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-movefilew
 */
#include "oracle.h"

static void touch(const wchar_t *p, const char *data)
{
    HANDLE h = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    DWORD n;
    WriteFile(h, data, (DWORD)strlen(data), &n, NULL);
    CloseHandle(h);
}

#define COPY_CASE(name, expr, dst)          \
    do {                                    \
        BOOL ok_;                           \
        ORACLE_CALL(ok_, (expr));           \
        oracle_case(name);                  \
        o_bool("ret", ok_);                 \
        o_lasterr();                        \
        o_file_state("dst", dst);           \
        o_dir_listing("dir_sorted", L".", 1); \
        oracle_case_end();                  \
    } while (0)

int main(void)
{
    oracle_begin("kernel32/CopyFileW");

    touch(L"src", "source data");
    touch(L"other", "old");

    COPY_CASE("copy_new", CopyFileW(L"src", L"dst", TRUE), L"dst");
    COPY_CASE("copy_fail_if_exists", CopyFileW(L"src", L"other", TRUE), L"other");
    COPY_CASE("copy_overwrite", CopyFileW(L"src", L"other", FALSE), L"other");
    COPY_CASE("copy_onto_self", CopyFileW(L"src", L"src", FALSE), L"src");
    COPY_CASE("copy_onto_self_other_case", CopyFileW(L"src", L"SRC", FALSE), L"src");
    COPY_CASE("copy_missing_source", CopyFileW(L"missing", L"x", FALSE), L"x");
    COPY_CASE("copy_missing_dest_dir", CopyFileW(L"src", L"nope\\x", FALSE), L"nope\\x");

    CreateDirectoryW(L"dir", NULL);
    COPY_CASE("copy_directory_source", CopyFileW(L"dir", L"dircopy", FALSE), L"dircopy");
    COPY_CASE("copy_onto_directory", CopyFileW(L"src", L"dir", FALSE), L"dir");
    COPY_CASE("copy_into_directory", CopyFileW(L"src", L"dir\\in", FALSE), L"dir\\in");

    /* Attributes: READONLY and HIDDEN are copied; ARCHIVE is set on the copy. */
    touch(L"ro", "readonly");
    SetFileAttributesW(L"ro", FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN);
    COPY_CASE("copy_readonly_hidden_source", CopyFileW(L"ro", L"ro_copy", FALSE), L"ro_copy");
    COPY_CASE("copy_over_readonly_dest", CopyFileW(L"src", L"ro_copy", FALSE), L"ro_copy");
    SetFileAttributesW(L"ro_copy", FILE_ATTRIBUTE_HIDDEN);
    COPY_CASE("copy_over_hidden_dest", CopyFileW(L"src", L"ro_copy", FALSE), L"ro_copy");
    SetFileAttributesW(L"ro_copy", FILE_ATTRIBUTE_NORMAL);
    SetFileAttributesW(L"ro", FILE_ATTRIBUTE_NORMAL);

    {   /* Source open without FILE_SHARE_READ. */
        HANDLE h = CreateFileW(L"src", GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        COPY_CASE("copy_source_locked", CopyFileW(L"src", L"locked_copy", FALSE), L"locked_copy");
        CloseHandle(h);
    }
    {   /* Destination open without sharing. */
        HANDLE h = CreateFileW(L"other", GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
        COPY_CASE("copy_dest_locked", CopyFileW(L"src", L"other", FALSE), L"other");
        CloseHandle(h);
    }

    COPY_CASE("copyex_fail_if_exists",
              CopyFileExW(L"src", L"other", NULL, NULL, NULL, COPY_FILE_FAIL_IF_EXISTS), L"other");
    COPY_CASE("copyex_new", CopyFileExW(L"src", L"ex_new", NULL, NULL, NULL, 0), L"ex_new");
    {
        BOOL cancel = TRUE;
        COPY_CASE("copyex_pre_cancelled",
                  CopyFileExW(L"src", L"ex_cancel", NULL, NULL, &cancel, 0), L"ex_cancel");
        DeleteFileW(L"ex_cancel"); /* keep later listings independent of this case */
    }

    touch(L"empty", "");
    COPY_CASE("copy_empty_file", CopyFileW(L"empty", L"empty_copy", FALSE), L"empty_copy");

    COPY_CASE("move_new", MoveFileW(L"ex_new", L"moved"), L"moved");
    COPY_CASE("move_onto_existing", MoveFileW(L"moved", L"other"), L"other");
    COPY_CASE("move_directory", MoveFileW(L"dir", L"dir2"), L"dir2\\in");
    COPY_CASE("move_into_own_subdir", MoveFileW(L"dir2", L"dir2\\sub"), L"dir2\\sub");
    COPY_CASE("move_case_only", MoveFileW(L"moved", L"MOVED"), L"MOVED");

    /* Probes: where the pre-set cancel flag is checked, and how far the
     * "into its own subdirectory" error reaches. */
    {
        BOOL cancel = TRUE;
        COPY_CASE("copyex_pre_cancelled_missing_source",
                  CopyFileExW(L"missing", L"ex_cancel", NULL, NULL, &cancel, 0), L"ex_cancel");
        COPY_CASE("copyex_pre_cancelled_existing_dest",
                  CopyFileExW(L"src", L"other", NULL, NULL, &cancel, 0), L"other");
        COPY_CASE("copyex_pre_cancelled_fail_if_exists",
                  CopyFileExW(L"src", L"other", NULL, NULL, &cancel, COPY_FILE_FAIL_IF_EXISTS), L"other");
    }
    CreateDirectoryW(L"dir2\\d", NULL);
    COPY_CASE("move_into_own_nested_subdir", MoveFileW(L"dir2", L"dir2\\d\\sub"), L"dir2\\d\\sub");

    /* Probes: hidden/system destinations. */
    SetFileAttributesW(L"ro", FILE_ATTRIBUTE_HIDDEN);
    SetFileAttributesW(L"ro_copy", FILE_ATTRIBUTE_HIDDEN);
    COPY_CASE("copy_hidden_over_hidden_dest", CopyFileW(L"ro", L"ro_copy", FALSE), L"ro_copy");
    SetFileAttributesW(L"ro_copy", FILE_ATTRIBUTE_SYSTEM);
    COPY_CASE("copy_over_system_dest", CopyFileW(L"src", L"ro_copy", FALSE), L"ro_copy");
    SetFileAttributesW(L"ro_copy", FILE_ATTRIBUTE_NORMAL);
    SetFileAttributesW(L"ro", FILE_ATTRIBUTE_NORMAL);

    return oracle_end();
}
