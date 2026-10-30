/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: FindFirstFileW / FindNextFileW enumeration, including the
 * *order* of results, "." and "..", wildcard matching, and error codes.
 *
 * Enumeration order is observable behaviour that apps sometimes rely on, so
 * this test records it unsorted on purpose.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-findfirstfilew
 *       https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-findnextfilew
 */
#include "oracle.h"

static void enum_case(const char *name, const wchar_t *pattern)
{
    WIN32_FIND_DATAW fd;
    HANDLE f;
    char list[4096] = "";
    ORACLE_CALL(f, FindFirstFileW(pattern, &fd));
    oracle_case(name);
    o_handle("ret", f);
    o_lasterr();
    if (f != INVALID_HANDLE_VALUE) {
        BOOL more;
        int n = 0;
        do {
            if (n++) strncat(list, ",", sizeof list - strlen(list) - 1);
            strncat(list, oracle_utf8(fd.cFileName), sizeof list - strlen(list) - 1);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) strncat(list, "/", sizeof list - strlen(list) - 1);
            ORACLE_CALL(more, FindNextFileW(f, &fd));
        } while (more);
        o_str("entries_in_order", list);
        o_u32("count", n);
        /* error that ended the enumeration (expected: ERROR_NO_MORE_FILES) */
        if (oracle_last_error == ORACLE_SENTINEL) o_str("end_error", "unchanged");
        else o_u32("end_error", oracle_last_error);
        FindClose(f);
    }
    oracle_case_end();
}

static void touch(const wchar_t *p)
{
    CloseHandle(CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL));
}

int main(void)
{
    oracle_begin("kernel32/FindFirstFileW");

    /* Deliberately created in non-alphabetical order, mixed case. */
    CreateDirectoryW(L"e", NULL);
    touch(L"e\\zeta.txt");
    touch(L"e\\Alpha.txt");
    touch(L"e\\beta.log");
    touch(L"e\\_under.txt");
    touch(L"e\\10.txt");
    touch(L"e\\9.txt");
    touch(L"e\\Gamma.TXT");
    CreateDirectoryW(L"e\\Delta", NULL);
    CreateDirectoryW(L"empty", NULL);

    enum_case("all_entries_order", L"e\\*");
    enum_case("star_dot_star", L"e\\*.*");
    enum_case("txt_case_insensitive_ext", L"e\\*.txt");
    enum_case("question_mark", L"e\\?.txt");
    enum_case("prefix_wildcard", L"e\\*a*");
    enum_case("empty_dir", L"empty\\*");
    enum_case("no_match", L"e\\*.none");
    enum_case("missing_dir", L"nodir\\*");
    enum_case("exact_name", L"e\\beta.log");
    enum_case("dir_itself", L"e");
    enum_case("trailing_backslash", L"e\\");

    return oracle_end();
}
