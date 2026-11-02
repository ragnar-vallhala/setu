/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: GetCurrentDirectoryW / SetCurrentDirectoryW (buffer sizing,
 * errors, the case and trailing separators kept), GetTempFileNameW with a
 * fixed unique value, GetTempPathW buffer sizing, and SearchPathW within the
 * current directory.
 *
 * Absolute paths are printed with the current directory replaced by "<cwd>"
 * and the system directory by "<sysdir>"; the drive letter is never printed.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-getcurrentdirectory
 *       https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-setcurrentdirectory
 *       https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-gettempfilenamew
 *       https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-gettemppathw
 *       https://learn.microsoft.com/en-us/windows/win32/api/processenv/nf-processenv-searchpathw
 *       https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-removedirectoryw
 *       https://learn.microsoft.com/en-us/windows/win32/fileio/naming-a-file
 */
#include "oracle.h"

static wchar_t cwd[MAX_PATH], sysdir[MAX_PATH];

static int has_prefix_ci(const wchar_t *s, const wchar_t *p)
{
    size_t n = wcslen(p);
    return n && !_wcsnicmp(s, p, n) && (s[n] == 0 || s[n] == L'\\');
}

static const char *tokenise(const wchar_t *s)
{
    static wchar_t out[2 * MAX_PATH];
    if (has_prefix_ci(s, cwd)) swprintf(out, 2 * MAX_PATH, L"<cwd>%ls", s + wcslen(cwd));
    else if (has_prefix_ci(s, sysdir)) swprintf(out, 2 * MAX_PATH, L"<sysdir>%ls", s + wcslen(sysdir));
    else if (s[0] && s[1] == L':') swprintf(out, 2 * MAX_PATH, L"<drive>%ls", s + 1);
    else swprintf(out, 2 * MAX_PATH, L"%ls", s);
    return oracle_utf8(out);
}

static void setcd_case(const char *name, const wchar_t *dir)
{
    BOOL ok;
    wchar_t now[MAX_PATH];
    ORACLE_CALL(ok, SetCurrentDirectoryW(dir));
    oracle_case(name);
    o_bool("ret", ok);
    o_lasterr();
    GetCurrentDirectoryW(MAX_PATH, now);
    o_str("now", tokenise(now));
    oracle_case_end();
    SetCurrentDirectoryW(cwd);
}

static void gtfn_case(const char *name, const wchar_t *path, const wchar_t *prefix, UINT unique)
{
    wchar_t buf[MAX_PATH + 16];
    UINT r;
    wmemset(buf, L'#', MAX_PATH + 16);
    buf[MAX_PATH + 15] = 0;
    ORACLE_CALL(r, GetTempFileNameW(path, prefix, unique, buf));
    oracle_case(name);
    o_hex32("ret", r);
    o_lasterr();
    if (r) {
        o_str("name", tokenise(buf));
        o_bool("created", GetFileAttributesW(buf) != INVALID_FILE_ATTRIBUTES);
    }
    oracle_case_end();
}

static void search_case(const char *name, const wchar_t *path, const wchar_t *file, const wchar_t *ext, DWORD size)
{
    wchar_t buf[MAX_PATH];
    wchar_t *part = (wchar_t *)1;
    DWORD r;
    wmemset(buf, L'#', MAX_PATH);
    ORACLE_CALL(r, SearchPathW(path, file, ext, size, buf, &part));
    oracle_case(name);
    /* The length includes the machine-specific cwd: print 0 or "nonzero". */
    if (r) o_str("ret", "nonzero");
    else o_u32("ret", r);
    o_lasterr();
    if (r && r < size) {
        o_bool("ret_is_len", r == wcslen(buf));
        o_str("found", tokenise(buf));
        if (part == (wchar_t *)1) o_str("file_part", "untouched");
        else if (part) o_wstr("file_part", part);
        else o_str("file_part", NULL);
    } else if (r) {
        /* Too small: the return value is a size; print it relative to the cwd length. */
        o_i64("ret_minus_cwd_len", (long long)r - (long long)wcslen(cwd));
        o_bool("buffer_untouched", buf[0] == L'#');
        o_bool("buffer_starts_nul", buf[0] == 0);
    }
    oracle_case_end();
}

static void touch(const wchar_t *name)
{
    HANDLE h = CreateFileW(name, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    CloseHandle(h);
}

int main(void)
{
    oracle_begin("kernel32/cwd_temp_search");

    GetCurrentDirectoryW(MAX_PATH, cwd);
    GetSystemDirectoryW(sysdir, MAX_PATH);
    size_t len = wcslen(cwd);
    wchar_t buf[MAX_PATH];
    DWORD r;

    /* ---- GetCurrentDirectoryW sizing ---- */
    ORACLE_CALL(r, GetCurrentDirectoryW(0, NULL));
    oracle_case("getcd_size_query");
    o_bool("ret_is_len_plus_1", r == len + 1);
    o_lasterr();
    oracle_case_end();
    wmemset(buf, L'#', MAX_PATH);
    ORACLE_CALL(r, GetCurrentDirectoryW((DWORD)len, buf));
    oracle_case("getcd_buffer_len");
    o_bool("ret_is_len_plus_1", r == len + 1);
    o_lasterr();
    o_bool("buffer_untouched", buf[0] == L'#');
    oracle_case_end();
    wmemset(buf, L'#', MAX_PATH);
    ORACLE_CALL(r, GetCurrentDirectoryW((DWORD)len + 1, buf));
    oracle_case("getcd_buffer_len_plus_1");
    o_bool("ret_is_len", r == len);
    o_lasterr();
    o_bool("terminated", buf[len] == 0);
    oracle_case_end();
    ORACLE_CALL(r, GetCurrentDirectoryW(1, buf));
    oracle_case("getcd_buffer_1");
    o_bool("ret_is_len_plus_1", r == len + 1);
    o_lasterr();
    oracle_case_end();
    oracle_case("getcd_no_trailing_backslash");
    o_bool("ok", cwd[len - 1] != L'\\');
    oracle_case_end();

    /* ---- SetCurrentDirectoryW ---- */
    CreateDirectoryW(L"sub", NULL);
    CreateDirectoryW(L"sub\\deeper", NULL);
    touch(L"file.txt");
    setcd_case("setcd_subdir", L"sub");
    setcd_case("setcd_subdir_trailing_backslash", L"sub\\");
    setcd_case("setcd_subdir_trailing_slash", L"sub/");
    setcd_case("setcd_subdir_upper_case", L"SUB");
    setcd_case("setcd_subdir_double_backslash", L"sub\\\\deeper");
    setcd_case("setcd_forward_slashes", L"sub/deeper");
    setcd_case("setcd_dot", L".");
    setcd_case("setcd_dotdot_back", L"sub\\deeper\\..\\..");
    setcd_case("setcd_trailing_dot", L"sub.");
    setcd_case("setcd_trailing_space", L"sub ");
    setcd_case("setcd_missing", L"missing");
    setcd_case("setcd_missing_nested", L"missing\\x");
    setcd_case("setcd_file", L"file.txt");
    setcd_case("setcd_file_trailing_backslash", L"file.txt\\");
    setcd_case("setcd_empty", L"");
    setcd_case("setcd_wildcard", L"s*");
    setcd_case("setcd_bad_char", L"sub|x");
    setcd_case("setcd_nul_device", L"NUL");
    {
        wchar_t lng[600];
        wcscpy(lng, L"sub\\");
        for (int i = 0; i < 300; i++) wcscat(lng, L"a");
        setcd_case("setcd_component_too_long", lng);
        wcscpy(lng, L"");
        for (int i = 0; i < 40; i++) wcscat(lng, L"sub\\..\\");
        wcscat(lng, L"sub");
        setcd_case("setcd_long_relative_resolves_short", lng);
        /* Where the length limits start: ".\\" padding that resolves to "sub". */
        static const struct { const char *name; int dots; const wchar_t *tail; } lens[] = {
            { "setcd_len_257", 127, L"sub" }, { "setcd_len_258", 127, L"sub\\" },
            { "setcd_len_259", 128, L"sub" }, { "setcd_len_260", 128, L"sub\\" },
        };
        for (int k = 0; k < 4; k++) {
            wcscpy(lng, L"");
            for (int i = 0; i < lens[k].dots; i++) wcscat(lng, L".\\");
            wcscat(lng, lens[k].tail);
            setcd_case(lens[k].name, lng);
        }
        wcscpy(lng, L"");
        for (int i = 0; i < 255; i++) wcscat(lng, L"a");
        setcd_case("setcd_missing_component_255", lng);
        wcscat(lng, L"a");
        setcd_case("setcd_missing_component_256", lng);
        setcd_case("setcd_nul_device_lower", L"nul");
        setcd_case("setcd_con_device", L"CON");
        setcd_case("setcd_nul_device_in_subdir", L"sub\\NUL");
        /* Relative names resolving to "sub" whose length plus the cwd's
         * (cwd + "\\" + name) is exactly N. */
        static const struct { const char *name; size_t total; } totals[] = {
            { "setcd_total_256", 256 }, { "setcd_total_257", 257 }, { "setcd_total_258", 258 },
            { "setcd_total_259", 259 }, { "setcd_total_260", 260 },
        };
        for (int k = 0; k < 5; k++) {
            size_t want = totals[k].total - wcslen(cwd) - 1;
            wcscpy(lng, L"");
            while (wcslen(lng) + 2 + 3 <= want && want - wcslen(lng) - 3 >= 2) wcscat(lng, L".\\");
            wcscat(lng, wcslen(lng) + 3 == want ? L"sub" : L"sub\\");
            setcd_case(totals[k].name, wcslen(lng) == want ? lng : L"length-mismatch");
        }
    }

    /* The current directory cannot be removed (it is held open). */
    SetCurrentDirectoryW(L"sub\\deeper");
    {
        BOOL ok;
        ORACLE_CALL(ok, RemoveDirectoryW(L"..\\deeper"));
        oracle_case("remove_current_directory");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(ok, MoveFileW(L"..\\deeper", L"..\\moved"));
        oracle_case("rename_current_directory");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        if (ok) MoveFileW(L"..\\moved", L"..\\deeper");
    }
    SetCurrentDirectoryW(cwd);
    {
        BOOL ok;
        ORACLE_CALL(ok, RemoveDirectoryW(L"sub\\deeper"));
        oracle_case("remove_former_current_directory");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
    }

    /* ---- GetTempFileNameW ---- */
    gtfn_case("gtfn_unique_1234", L".", L"set", 0x1234);
    gtfn_case("gtfn_long_prefix_truncated", L".", L"setuoracle", 0xabc);
    gtfn_case("gtfn_unique_high_bits", L".", L"pre", 0x12345678);
    gtfn_case("gtfn_unique_ffff", L".", L"pre", 0xffff);
    gtfn_case("gtfn_empty_prefix", L".", L"", 0x42);
    gtfn_case("gtfn_null_prefix", L".", NULL, 0x42);
    gtfn_case("gtfn_subdir", L"sub", L"t", 0x1);
    gtfn_case("gtfn_subdir_trailing_backslash", L"sub\\", L"t", 0x1);
    gtfn_case("gtfn_missing_dir_fixed_unique", L"missing", L"t", 0x1);
    gtfn_case("gtfn_missing_dir_unique_0", L"missing", L"t", 0);
    gtfn_case("gtfn_empty_path", L"", L"t", 0x7);
    gtfn_case("gtfn_missing_nested_dir", L"missing\\x", L"t", 0x7);
    gtfn_case("gtfn_file_as_dir", L"file.txt", L"t", 0x7);
    gtfn_case("gtfn_bad_char_dir", L"a|b", L"t", 0x7);
    {
        /* uUnique 0: a new file is created; its number is not deterministic. */
        wchar_t out[MAX_PATH];
        UINT u;
        ORACLE_CALL(u, GetTempFileNameW(L".", L"zz", 0, out));
        const wchar_t *base = wcsrchr(out, L'\\');
        base = base ? base + 1 : out;
        oracle_case("gtfn_unique_0_creates");
        o_bool("ret_nonzero", u != 0);
        o_lasterr();
        o_bool("created", GetFileAttributesW(out) != INVALID_FILE_ATTRIBUTES);
        o_bool("starts_with_prefix", !wcsncmp(base, L"zz", 2));
        o_bool("ends_with_tmp", wcslen(base) > 4 && !_wcsicmp(base + wcslen(base) - 4, L".tmp"));
        o_bool("in_cwd", has_prefix_ci(out, cwd));
        wchar_t expect[16];
        swprintf(expect, 16, L"zz%X.tmp", u);
        o_bool("name_matches_ret_hex", !_wcsicmp(base, expect));
        oracle_case_end();
        DeleteFileW(out);
    }
    {   /* With a fixed unique value of an existing file: still returned, not checked. */
        touch(L"pre1.tmp");
        gtfn_case("gtfn_fixed_unique_existing_file", L".", L"pre", 0x1);
    }

    /* ---- GetTempPathW sizing ---- */
    {
        wchar_t t[MAX_PATH + 1];
        DWORD full;
        ORACLE_CALL(full, GetTempPathW(MAX_PATH + 1, t));
        oracle_case("gettemppath_full");
        o_bool("ret_is_len", full == wcslen(t));
        o_bool("trailing_backslash", full && t[full - 1] == L'\\');
        o_lasterr();
        oracle_case_end();
        wmemset(t, L'#', MAX_PATH);
        ORACLE_CALL(r, GetTempPathW(full, t));
        oracle_case("gettemppath_buffer_len");
        o_bool("ret_is_len_plus_1", r == full + 1);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, GetTempPathW(0, NULL));
        oracle_case("gettemppath_size_query");
        o_bool("ret_is_len_plus_1", r == full + 1);
        o_lasterr();
        oracle_case_end();
    }

    /* ---- SearchPathW ---- */
    touch(L"a.txt");
    touch(L"noext");
    touch(L"sub\\b.dat");
    search_case("search_cwd_file", NULL, L"a.txt", NULL, MAX_PATH);
    search_case("search_cwd_file_upper", NULL, L"A.TXT", NULL, MAX_PATH);
    search_case("search_ext_added", NULL, L"a", L".txt", MAX_PATH);
    search_case("search_ext_ignored_when_name_has_one", NULL, L"a.txt", L".dat", MAX_PATH);
    search_case("search_ext_without_dot", NULL, L"a", L"txt", MAX_PATH);
    search_case("search_no_ext_file", NULL, L"noext", NULL, MAX_PATH);
    search_case("search_missing", NULL, L"missing.txt", NULL, MAX_PATH);
    search_case("search_relative_subpath", NULL, L"sub\\b.dat", NULL, MAX_PATH);
    search_case("search_dot_relative_subpath", NULL, L".\\sub\\b.dat", NULL, MAX_PATH);
    search_case("search_explicit_path_dot", L".", L"a.txt", NULL, MAX_PATH);
    search_case("search_explicit_path_sub", L"sub", L"b.dat", NULL, MAX_PATH);
    search_case("search_explicit_path_list", L"missing;sub", L"b.dat", NULL, MAX_PATH);
    search_case("search_explicit_path_not_cwd", L"sub", L"a.txt", NULL, MAX_PATH);
    search_case("search_directory_name", NULL, L"sub", NULL, MAX_PATH);
    search_case("search_buffer_too_small", NULL, L"a.txt", NULL, 4);
    search_case("search_buffer_exact_len", NULL, L"a.txt", NULL, (DWORD)(len + 6));
    search_case("search_buffer_len_plus_1", NULL, L"a.txt", NULL, (DWORD)(len + 7));
    search_case("search_buffer_len_plus_2", NULL, L"a.txt", NULL, (DWORD)(len + 8));
    search_case("search_buffer_len_plus_3", NULL, L"a.txt", NULL, (DWORD)(len + 9));
    search_case("search_buffer_len_plus_4", NULL, L"a.txt", NULL, (DWORD)(len + 10));
    search_case("search_buffer_too_small_subpath", NULL, L"sub\\b.dat", NULL, 4);
    search_case("search_buffer_size_0", NULL, L"a.txt", NULL, 0);
    search_case("search_empty_name", NULL, L"", NULL, MAX_PATH);
    search_case("search_wildcard", NULL, L"a.*", NULL, MAX_PATH);
    search_case("search_system_dll", NULL, L"kernel32.dll", NULL, MAX_PATH);
    search_case("search_system_dll_ext", NULL, L"kernel32", L".dll", MAX_PATH);
    search_case("search_absolute_missing", NULL, L"C:\\setu_oracle_missing_dir\\x.txt", NULL, MAX_PATH);

    return oracle_end();
}
