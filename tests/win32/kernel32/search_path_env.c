/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: SearchPathW with lpPath NULL (the default search order: the
 * system and Windows directories, the current directory, then PATH), the
 * process-wide safe search mode (SetSearchPathMode), the PATH variable's
 * syntax (relative, quoted, empty and trailing-separator entries), and
 * NeedCurrentDirectoryForExePathW.
 *
 * cwd_temp_search covers SearchPathW within the current directory; this test
 * covers which of several candidates in different directories wins.
 *
 * Absolute paths are printed with prefixes replaced by "<cwd>", "<sysdir>"
 * or "<windir>".
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/processenv/nf-processenv-searchpathw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-setsearchpathmode
 *       https://learn.microsoft.com/en-us/windows/win32/api/processenv/nf-processenv-needcurrentdirectoryforexepathw
 */
#include "oracle.h"

static wchar_t cwd[MAX_PATH], sysdir[MAX_PATH], windir[MAX_PATH];

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
    else if (has_prefix_ci(s, windir)) swprintf(out, 2 * MAX_PATH, L"<windir>%ls", s + wcslen(windir));
    else if (s[0] && s[1] == L':') swprintf(out, 2 * MAX_PATH, L"<drive>%ls", s + 1);
    else swprintf(out, 2 * MAX_PATH, L"%ls", s);
    return oracle_utf8(out);
}

static void search_case(const char *name, const wchar_t *path, const wchar_t *file, const wchar_t *ext)
{
    wchar_t buf[MAX_PATH];
    wchar_t *part = NULL;
    DWORD r;
    ORACLE_CALL(r, SearchPathW(path, file, ext, MAX_PATH, buf, &part));
    oracle_case(name);
    o_bool("found", r != 0);
    o_lasterr();
    if (r && r < MAX_PATH) {
        o_str("path", tokenise(buf));
        o_wstr("file_part", part);
    }
    oracle_case_end();
}

static void set_path(const wchar_t *fmt)
{
    wchar_t v[1024];
    /* fmt uses %1$ls for the cwd. */
    swprintf(v, 1024, fmt, cwd, cwd, cwd);
    SetEnvironmentVariableW(L"PATH", v);
}

static void touch(const wchar_t *name)
{
    HANDLE h = CreateFileW(name, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    CloseHandle(h);
}

static void need_case(const char *name, const wchar_t *exe)
{
    BOOL r;
    ORACLE_CALL(r, NeedCurrentDirectoryForExePathW(exe));
    oracle_case(name);
    o_bool("ret", r);
    o_lasterr();
    oracle_case_end();
}

int main(void)
{
    oracle_begin("kernel32/search_path_env");
    GetCurrentDirectoryW(MAX_PATH, cwd);
    GetSystemDirectoryW(sysdir, MAX_PATH);
    GetWindowsDirectoryW(windir, MAX_PATH);
    static wchar_t oldpath[32768];
    DWORD had = GetEnvironmentVariableW(L"PATH", oldpath, 32768);

    CreateDirectoryW(L"p1", NULL);
    CreateDirectoryW(L"p2", NULL);
    CreateDirectoryW(L"p 3", NULL);
    touch(L"p1\\one.txt");
    touch(L"p2\\two.txt");
    touch(L"p2\\both.txt");
    touch(L"p1\\both.txt");
    touch(L"p 3\\three.txt");
    touch(L"p2\\tool.exe");
    touch(L"p1\\tool");
    touch(L"shadow.txt");
    touch(L"p1\\shadow.txt");

    /* ---- PATH entries ---- */
    set_path(L"%ls\\p1;%ls\\p2");
    search_case("path_first_entry", NULL, L"one.txt", NULL);
    search_case("path_second_entry", NULL, L"two.txt", NULL);
    search_case("path_first_entry_wins", NULL, L"both.txt", NULL);
    search_case("path_cwd_before_path", NULL, L"shadow.txt", NULL);
    search_case("path_missing", NULL, L"none.txt", NULL);
    search_case("path_ext_appended", NULL, L"tool", L".exe");
    search_case("path_name_without_ext_exists_first", NULL, L"tool", NULL);
    search_case("path_not_used_with_subdir_name", NULL, L"x\\one.txt", NULL);
    set_path(L"%ls\\p2;%ls\\p1");
    search_case("path_order_reversed", NULL, L"both.txt", NULL);
    set_path(L"p1;p2");
    search_case("path_relative_entries", NULL, L"two.txt", NULL);
    set_path(L"\"%ls\\p 3\"");
    search_case("path_quoted_entry", NULL, L"three.txt", NULL);
    set_path(L"%ls\\p 3");
    search_case("path_unquoted_space_entry", NULL, L"three.txt", NULL);
    set_path(L";;%ls\\nodir;%ls\\p1\\;");
    search_case("path_empty_missing_trailing_sep", NULL, L"one.txt", NULL);
    set_path(L"%ls\\p1/");
    search_case("path_trailing_forward_slash", NULL, L"one.txt", NULL);
    SetEnvironmentVariableW(L"SETU_P1", L"p1");
    set_path(L"%%SETU_P1%%");
    search_case("path_env_reference_not_expanded", NULL, L"one.txt", NULL);
    SetEnvironmentVariableW(L"PATH", NULL);
    search_case("path_unset", NULL, L"one.txt", NULL);
    search_case("path_unset_cwd_still_searched", NULL, L"shadow.txt", NULL);
    /* An explicit lpPath ignores PATH. */
    set_path(L"%ls\\p1");
    search_case("explicit_path_ignores_env", L"p2", L"one.txt", NULL);

    /* ---- current directory versus system directories ---- */
    touch(L"kernel32.dll");
    touch(L"notepad.exe");
    touch(L"setu_only_here.dll");
    SetEnvironmentVariableW(L"PATH", NULL);
    search_case("sys_dll_default_mode", NULL, L"kernel32.dll", NULL);
    search_case("windir_exe_default_mode", NULL, L"notepad.exe", NULL);
    search_case("cwd_only_dll_default_mode", NULL, L"setu_only_here.dll", NULL);
    {
        BOOL ok;
        ORACLE_CALL(ok, SetSearchPathMode(BASE_SEARCH_PATH_ENABLE_SAFE_SEARCHMODE));
        oracle_case("set_safe_mode");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        search_case("sys_dll_safe_mode", NULL, L"kernel32.dll", NULL);
        search_case("cwd_only_dll_safe_mode", NULL, L"setu_only_here.dll", NULL);
        ORACLE_CALL(ok, SetSearchPathMode(BASE_SEARCH_PATH_DISABLE_SAFE_SEARCHMODE));
        oracle_case("set_unsafe_mode");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        search_case("sys_dll_unsafe_mode", NULL, L"kernel32.dll", NULL);
        search_case("windir_exe_unsafe_mode", NULL, L"notepad.exe", NULL);
        ORACLE_CALL(ok, SetSearchPathMode(0));
        oracle_case("set_mode_zero");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(ok, SetSearchPathMode(BASE_SEARCH_PATH_ENABLE_SAFE_SEARCHMODE |
                                          BASE_SEARCH_PATH_DISABLE_SAFE_SEARCHMODE));
        oracle_case("set_mode_both");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(ok, SetSearchPathMode(BASE_SEARCH_PATH_PERMANENT));
        oracle_case("set_mode_permanent_alone");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(ok, SetSearchPathMode(BASE_SEARCH_PATH_ENABLE_SAFE_SEARCHMODE | BASE_SEARCH_PATH_PERMANENT));
        oracle_case("set_safe_mode_permanent");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(ok, SetSearchPathMode(BASE_SEARCH_PATH_DISABLE_SAFE_SEARCHMODE));
        oracle_case("set_unsafe_after_permanent");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        search_case("sys_dll_after_permanent", NULL, L"kernel32.dll", NULL);
    }

    /* ---- NeedCurrentDirectoryForExePathW ---- */
    SetEnvironmentVariableW(L"NoDefaultCurrentDirectoryInExePath", NULL);
    need_case("need_cwd_default", L"tool.exe");
    need_case("need_cwd_default_with_dir", L"sub\\tool.exe");
    SetEnvironmentVariableW(L"NoDefaultCurrentDirectoryInExePath", L"1");
    need_case("need_cwd_var_set", L"tool.exe");
    need_case("need_cwd_var_set_with_dir", L"sub\\tool.exe");
    need_case("need_cwd_var_set_forward_slash", L"sub/tool.exe");
    SetEnvironmentVariableW(L"NoDefaultCurrentDirectoryInExePath", L"");
    need_case("need_cwd_var_empty", L"tool.exe");
    SetEnvironmentVariableW(L"NoDefaultCurrentDirectoryInExePath", NULL);

    SetEnvironmentVariableW(L"PATH", had ? oldpath : NULL);
    return oracle_end();
}
