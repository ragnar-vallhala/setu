/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: FindFirstFileExW info levels (Standard, Basic), search ops
 * (NameMatch, LimitToDirectories, LimitToDevices), additional flags
 * (CASE_SENSITIVE, LARGE_FETCH, ON_DISK_ENTRIES_ONLY) and parameter
 * validation. Short (8.3) names are machine-specific when generated, so only
 * their shape is printed.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-findfirstfileexw
 *       https://learn.microsoft.com/en-us/windows/win32/api/minwinbase/ne-minwinbase-findex_info_levels
 *       https://learn.microsoft.com/en-us/windows/win32/api/minwinbase/ne-minwinbase-findex_search_ops
 */
#include "oracle.h"

#ifndef FIND_FIRST_EX_CASE_SENSITIVE
#define FIND_FIRST_EX_CASE_SENSITIVE 1
#endif
#ifndef FIND_FIRST_EX_LARGE_FETCH
#define FIND_FIRST_EX_LARGE_FETCH 2
#endif
#ifndef FIND_FIRST_EX_ON_DISK_ENTRIES_ONLY
#define FIND_FIRST_EX_ON_DISK_ENTRIES_ONLY 4
#endif

static void make_file(const wchar_t *path, DWORD size)
{
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    char b[64];
    DWORD w;
    memset(b, 'a', sizeof b);
    WriteFile(h, b, size, &w, NULL);
    CloseHandle(h);
}

static int is_83(const wchar_t *s)
{
    size_t n = wcslen(s);
    const wchar_t *dot = wcschr(s, L'.');
    if (!n || n > 12) return 0;
    if (dot && (dot - s > 8 || wcslen(dot + 1) > 3)) return 0;
    if (!dot && n > 8) return 0;
    for (const wchar_t *p = s; *p; p++)
        if (iswlower(*p)) return 0;
    return 1;
}

static int wcmp(const void *a, const void *b) { return wcscmp((const wchar_t *)a, (const wchar_t *)b); }

/* Enumerates fully: sorted names (with "/" suffix for directories) and a
 * summary of the first entry's fields. */
static void find_case(const char *name, const wchar_t *pattern, FINDEX_INFO_LEVELS level, FINDEX_SEARCH_OPS op,
                      DWORD flags, int sorted)
{
    WIN32_FIND_DATAW fd;
    HANDLE h;
    static wchar_t names[64][MAX_PATH + 1];
    int n = 0, alt_nonempty = 0, alt_all_83 = 1;
    memset(&fd, 0xcc, sizeof fd);
    ORACLE_CALL(h, FindFirstFileExW(pattern, level, &fd, op, NULL, flags));
    oracle_case(name);
    o_handle("ret", h);
    o_lasterr();
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (n < 64) {
                swprintf(names[n], MAX_PATH + 1, L"%ls%ls", fd.cFileName,
                         (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? L"/" : L"");
                n++;
            }
            if (fd.cAlternateFileName[0]) {
                alt_nonempty++;
                if (!is_83(fd.cAlternateFileName)) alt_all_83 = 0;
            }
        } while (FindNextFileW(h, &fd));
        DWORD e = GetLastError();
        o_u32("end_error", e);
        FindClose(h);
        if (sorted) qsort(names, n, sizeof names[0], wcmp);
        char out[4096] = "";
        for (int i = 0; i < n; i++) {
            if (i) strncat(out, ",", sizeof out - strlen(out) - 1);
            strncat(out, oracle_utf8(names[i]), sizeof out - strlen(out) - 1);
        }
        o_str("entries", out);
        o_u32("alt_names", alt_nonempty);
        o_bool("alt_names_are_8_3", alt_all_83);
    }
    oracle_case_end();
}

int main(void)
{
    oracle_begin("kernel32/FindFirstFileExW");

    make_file(L"a.txt", 3);
    make_file(L"B.TXT", 5);
    make_file(L"long file name.text", 7);
    make_file(L"c.dat", 0);
    CreateDirectoryW(L"sub", NULL);
    CreateDirectoryW(L"Sub2.dir", NULL);
    make_file(L"sub\\inner.txt", 1);
    SetFileAttributesW(L"c.dat", FILE_ATTRIBUTE_HIDDEN);

    find_case("standard_all", L"*", FindExInfoStandard, FindExSearchNameMatch, 0, 1);
    find_case("basic_all", L"*", FindExInfoBasic, FindExSearchNameMatch, 0, 1);
    find_case("standard_long_name", L"long*", FindExInfoStandard, FindExSearchNameMatch, 0, 1);
    find_case("basic_long_name", L"long*", FindExInfoBasic, FindExSearchNameMatch, 0, 1);
    find_case("standard_short_name_only", L"a.txt", FindExInfoStandard, FindExSearchNameMatch, 0, 1);
    find_case("limit_to_directories", L"*", FindExInfoStandard, FindExSearchLimitToDirectories, 0, 1);
    find_case("limit_to_directories_txt", L"*.txt", FindExInfoBasic, FindExSearchLimitToDirectories, 0, 1);
    find_case("limit_to_devices", L"*", FindExInfoStandard, FindExSearchLimitToDevices, 0, 1);
    find_case("large_fetch", L"*", FindExInfoBasic, FindExSearchNameMatch, FIND_FIRST_EX_LARGE_FETCH, 1);
    find_case("on_disk_entries_only", L"*", FindExInfoBasic, FindExSearchNameMatch,
              FIND_FIRST_EX_ON_DISK_ENTRIES_ONLY, 1);
    find_case("case_insensitive_upper_pattern", L"A.TXT", FindExInfoBasic, FindExSearchNameMatch, 0, 1);
    find_case("case_sensitive_upper_pattern", L"A.TXT", FindExInfoBasic, FindExSearchNameMatch,
              FIND_FIRST_EX_CASE_SENSITIVE, 1);
    find_case("case_sensitive_exact", L"a.txt", FindExInfoBasic, FindExSearchNameMatch,
              FIND_FIRST_EX_CASE_SENSITIVE, 1);
    find_case("case_sensitive_wildcard", L"*.TXT", FindExInfoBasic, FindExSearchNameMatch,
              FIND_FIRST_EX_CASE_SENSITIVE, 1);
    find_case("case_insensitive_wildcard", L"*.TXT", FindExInfoBasic, FindExSearchNameMatch, 0, 1);
    find_case("all_flags", L"*", FindExInfoBasic, FindExSearchNameMatch,
              FIND_FIRST_EX_CASE_SENSITIVE | FIND_FIRST_EX_LARGE_FETCH | FIND_FIRST_EX_ON_DISK_ENTRIES_ONLY, 1);
    find_case("subdir_pattern", L"sub\\*", FindExInfoBasic, FindExSearchNameMatch, 0, 1);
    find_case("subdir_limit_dirs", L"sub\\*", FindExInfoBasic, FindExSearchLimitToDirectories, 0, 1);
    find_case("dir_by_name", L"sub", FindExInfoBasic, FindExSearchLimitToDirectories, 0, 1);
    find_case("file_by_name_limit_dirs", L"a.txt", FindExInfoBasic, FindExSearchLimitToDirectories, 0, 1);
    find_case("question_marks", L"?.txt", FindExInfoBasic, FindExSearchNameMatch, 0, 1);
    find_case("star_dot", L"*.", FindExInfoBasic, FindExSearchNameMatch, 0, 1);
    find_case("no_match", L"zzz*", FindExInfoBasic, FindExSearchNameMatch, 0, 1);
    find_case("missing_dir", L"nodir\\*", FindExInfoBasic, FindExSearchNameMatch, 0, 1);
    find_case("trailing_backslash", L"sub\\", FindExInfoBasic, FindExSearchNameMatch, 0, 1);
    find_case("empty_pattern", L"", FindExInfoBasic, FindExSearchNameMatch, 0, 1);

    /* Parameter validation. */
    find_case("info_level_max", L"*", FindExInfoMaxInfoLevel, FindExSearchNameMatch, 0, 1);
    find_case("info_level_99", L"*", (FINDEX_INFO_LEVELS)99, FindExSearchNameMatch, 0, 1);
    find_case("search_op_max", L"*", FindExInfoBasic, FindExSearchMaxSearchOp, 0, 1);
    find_case("unknown_flag_8", L"*", FindExInfoBasic, FindExSearchNameMatch, 8, 1);
    find_case("unknown_flag_high", L"*", FindExInfoBasic, FindExSearchNameMatch, 0x80000000, 1);
    {
        WIN32_FIND_DATAW fd;
        HANDLE h;
        int dummy = 0;
        ORACLE_CALL(h, FindFirstFileExW(L"*", FindExInfoBasic, &fd, FindExSearchNameMatch, &dummy, 0));
        oracle_case("search_filter_non_null");
        o_handle("ret", h);
        o_lasterr();
        oracle_case_end();
        if (h != INVALID_HANDLE_VALUE) FindClose(h);
        ORACLE_CALL(h, FindFirstFileExW(L"*", FindExInfoBasic, &fd, FindExSearchLimitToDirectories, &dummy, 0));
        oracle_case("search_filter_non_null_limit_dirs");
        o_handle("ret", h);
        o_lasterr();
        oracle_case_end();
        if (h != INVALID_HANDLE_VALUE) FindClose(h);
    }

    /* Field contents of a single match. */
    {
        WIN32_FIND_DATAW fd;
        memset(&fd, 0xcc, sizeof fd);
        HANDLE h = FindFirstFileExW(L"B.txt", FindExInfoBasic, &fd, FindExSearchNameMatch, NULL, 0);
        oracle_case("basic_fields");
        o_wstr("name", fd.cFileName);
        o_hex32("attrs", fd.dwFileAttributes);
        o_u32("size_low", fd.nFileSizeLow);
        o_u32("size_high", fd.nFileSizeHigh);
        o_u32("alt0", fd.cAlternateFileName[0]);
        o_hex32("reserved0", fd.dwReserved0);
        oracle_case_end();
        FindClose(h);
        memset(&fd, 0xcc, sizeof fd);
        h = FindFirstFileExW(L"c.dat", FindExInfoStandard, &fd, FindExSearchNameMatch, NULL, 0);
        oracle_case("standard_fields_hidden");
        o_wstr("name", fd.cFileName);
        o_hex32("attrs", fd.dwFileAttributes);
        o_u32("size_low", fd.nFileSizeLow);
        o_wstr("alt", fd.cAlternateFileName);
        oracle_case_end();
        FindClose(h);
        memset(&fd, 0xcc, sizeof fd);
        h = FindFirstFileExW(L"Sub2.dir", FindExInfoStandard, &fd, FindExSearchNameMatch, NULL, 0);
        oracle_case("standard_fields_dir");
        o_wstr("name", fd.cFileName);
        o_hex32("attrs", fd.dwFileAttributes);
        o_u32("size_low", fd.nFileSizeLow);
        o_wstr("alt", fd.cAlternateFileName);
        oracle_case_end();
        FindClose(h);
    }

    /* Enumeration order (NTFS: by upcased name). */
    find_case("order_all", L"*", FindExInfoBasic, FindExSearchNameMatch, 0, 0);

    return oracle_end();
}
