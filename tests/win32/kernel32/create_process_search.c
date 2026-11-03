/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: how CreateProcessW finds the executable.
 *
 * With lpApplicationName NULL, the first token of the command line names the
 * module: ".exe" is appended when there is no extension, an unquoted path
 * with spaces is tried piece by piece ("c:\program files\sub dir\x.exe" ->
 * "c:\program.exe", "c:\program files\sub.exe", ...), and the current
 * directory and PATH are searched. With lpApplicationName set, the name is
 * used as given: no search, no ".exe" appended. This test copies its own
 * .exe under several names in the working directory and checks which copy
 * runs and which command line the child receives.
 *
 * The child writes who.txt: its module name and GetCommandLineW(), with the
 * working directory replaced by "<cwd>".
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessw
 *       https://learn.microsoft.com/en-us/windows/win32/api/processenv/nf-processenv-searchpathw
 */
#include "oracle.h"

static wchar_t self[MAX_PATH], cwd[MAX_PATH];

static const char *tok(const wchar_t *s)
{
    static wchar_t buf[1024];
    size_t n = wcslen(cwd), j = 0;
    for (const wchar_t *p = s; *p && j < 1000;) {
        if (n && !_wcsnicmp(p, cwd, n)) {
            wcscpy(buf + j, L"<cwd>");
            j += 5;
            p += n;
        } else {
            buf[j++] = *p++;
        }
    }
    buf[j] = 0;
    return oracle_utf8(buf);
}

/* ---- child side ------------------------------------------------------- */

static int child_main(void)
{
    wchar_t mod[MAX_PATH];
    GetCurrentDirectoryW(MAX_PATH, cwd);
    GetModuleFileNameW(NULL, mod, MAX_PATH);
    FILE *f = _wfopen(L"who.txt", L"wb");
    fprintf(f, "module=%s\n", tok(mod));
    fprintf(f, "cmdline=%s\n", tok(GetCommandLineW()));
    fclose(f);
    return 0;
}

/* ---- parent side ------------------------------------------------------ */

static void run_case(const char *name, const wchar_t *app, const wchar_t *cmdline)
{
    STARTUPINFOW si = {0};
    PROCESS_INFORMATION pi;
    wchar_t cmd[1024];
    BOOL ok;
    si.cb = sizeof si;
    if (cmdline) wcscpy(cmd, cmdline);
    DeleteFileW(L"who.txt");
    ORACLE_CALL(ok, CreateProcessW(app, cmdline ? cmd : NULL, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi));
    oracle_case(name);
    o_bool("ret", ok);
    /* After a success the last error depends on Windows' own caches (it
     * differs between runs), so it is printed for failures only. */
    if (!ok) o_lasterr();
    if (ok) {
        if (WaitForSingleObject(pi.hProcess, 20000) != WAIT_OBJECT_0) TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        char text[1024] = "";
        HANDLE h = CreateFileW(L"who.txt", GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
        DWORD n = 0;
        if (h != INVALID_HANDLE_VALUE) {
            ReadFile(h, text, sizeof text - 1, &n, NULL);
            text[n] = 0;
            CloseHandle(h);
        }
        o_str("who", text);
    }
    oracle_case_end();
}

static void copy_self(const wchar_t *to)
{
    if (!CopyFileW(self, to, FALSE)) fprintf(stderr, "copy to %ls failed: %lu\n", to, GetLastError());
}

int main(void)
{
    if (GetEnvironmentVariableW(L"SETU_CPS_CHILD", NULL, 0)) return child_main();

    oracle_begin("kernel32/create_process_search");
    GetModuleFileNameW(NULL, self, MAX_PATH);
    GetCurrentDirectoryW(MAX_PATH, cwd);
    SetEnvironmentVariableW(L"SETU_CPS_CHILD", L"1");

    CreateDirectoryW(L"sub", NULL);
    CreateDirectoryW(L"my dir", NULL);
    CreateDirectoryW(L"pdir", NULL);
    CreateDirectoryW(L"dir.exe", NULL);
    copy_self(L"child.exe");
    copy_self(L"child.bin");
    copy_self(L"noext");
    copy_self(L"sub\\subchild.exe");
    copy_self(L"my dir\\spaced.exe");
    copy_self(L"pdir\\pathchild.exe");
    copy_self(L"two words.exe");
    copy_self(L"longchildname.exe");

    wchar_t c[1024];

    /* ---- lpApplicationName NULL: the command line names the module ---- */
    run_case("cmd_name_no_ext", NULL, L"child arg1");
    run_case("cmd_name_with_ext", NULL, L"child.exe arg1");
    run_case("cmd_name_upper_case", NULL, L"CHILD.EXE");
    /* Is the module name kept as typed, or expanded to the long name? */
    run_case("cmd_short_name", NULL, L"LONGCH~1.EXE arg1");
    run_case("cmd_short_name_no_ext", NULL, L"longch~1 arg1");
    run_case("cmd_other_ext", NULL, L"child.bin arg1");
    run_case("cmd_no_ext_file_exists", NULL, L"noext arg1");
    run_case("cmd_trailing_dot", NULL, L"child. arg1");
    run_case("cmd_relative_subdir_no_ext", NULL, L"sub\\subchild arg1");
    run_case("cmd_relative_dot_subdir", NULL, L".\\sub\\subchild.exe");
    run_case("cmd_forward_slash", NULL, L"sub/subchild.exe arg1");
    run_case("cmd_quoted", NULL, L"\"child.exe\" arg1");
    run_case("cmd_quoted_no_ext", NULL, L"\"child\" arg1");
    run_case("cmd_leading_spaces", NULL, L"   child.exe arg1");
    run_case("cmd_tab_separator", NULL, L"child.exe\targ1");
    run_case("cmd_missing", NULL, L"nosuchchild arg1");
    run_case("cmd_missing_quoted_path", NULL, L"\"nodir\\child.exe\"");
    run_case("cmd_directory_with_exe_name", NULL, L"dir.exe");
    /* Unquoted spaces: "my dir\spaced.exe" (relative) and absolute. */
    run_case("cmd_unquoted_space_relative", NULL, L"my dir\\spaced.exe arg1");
    swprintf(c, 1024, L"%ls\\my dir\\spaced.exe arg1", cwd);
    run_case("cmd_unquoted_space_absolute", NULL, c);
    swprintf(c, 1024, L"\"%ls\\my dir\\spaced.exe\" arg1", cwd);
    run_case("cmd_quoted_space_absolute", NULL, c);
    /* "two words.exe" exists; unquoted "two words" first tries "two.exe". */
    run_case("cmd_unquoted_two_words_no_ext", NULL, L"two words arg1");
    run_case("cmd_unquoted_two_words_ext", NULL, L"two words.exe arg1");
    /* Now a "my.exe" exists too: the shorter candidate wins. */
    copy_self(L"my.exe");
    run_case("cmd_unquoted_space_shadowed", NULL, L"my dir\\spaced.exe arg1");
    run_case("cmd_quoted_space_not_shadowed", NULL, L"\"my dir\\spaced.exe\" arg1");
    DeleteFileW(L"my.exe");

    /* ---- PATH search ---- */
    {
        static wchar_t oldpath[32768];
        DWORD had = GetEnvironmentVariableW(L"PATH", oldpath, 32768);
        swprintf(c, 1024, L"%ls\\pdir", cwd);
        SetEnvironmentVariableW(L"PATH", c);
        run_case("path_search_no_ext", NULL, L"pathchild arg1");
        run_case("path_search_with_ext", NULL, L"pathchild.exe arg1");
        run_case("path_search_app_name_not_searched", L"pathchild.exe", L"pathchild.exe arg1");
        run_case("path_search_relative_with_dir_not_searched", NULL, L"x\\pathchild.exe");
        SetEnvironmentVariableW(L"PATH", L"pdir");
        run_case("path_relative_entry", NULL, L"pathchild arg1");
        swprintf(c, 1024, L"\"%ls\\pdir\"", cwd);
        SetEnvironmentVariableW(L"PATH", c);
        run_case("path_quoted_entry", NULL, L"pathchild arg1");
        swprintf(c, 1024, L";;%ls\\nodir;%ls\\pdir\\;", cwd, cwd);
        SetEnvironmentVariableW(L"PATH", c);
        run_case("path_empty_and_trailing_sep_entries", NULL, L"pathchild arg1");
        /* Same name in the current directory and in PATH. */
        copy_self(L"pathchild.exe");
        swprintf(c, 1024, L"%ls\\pdir", cwd);
        SetEnvironmentVariableW(L"PATH", c);
        run_case("cwd_before_path", NULL, L"pathchild arg1");
        DeleteFileW(L"pathchild.exe");
        SetEnvironmentVariableW(L"PATH", had ? oldpath : NULL);
    }

    /* ---- lpApplicationName set ---- */
    run_case("app_relative", L"child.exe", L"anything arg1");
    run_case("app_upper_case", L"CHILD.EXE", L"x");
    run_case("app_short_name", L"LONGCH~1.EXE", L"x");
    run_case("app_no_ext_not_appended", L"child", L"child arg1");
    run_case("app_other_ext", L"child.bin", L"x arg1");
    run_case("app_no_ext_file", L"noext", L"x arg1");
    run_case("app_unquoted_space", L"my dir\\spaced.exe", L"x arg1");
    run_case("app_quoted", L"\"child.exe\"", L"x arg1");
    run_case("app_subdir", L"sub\\subchild.exe", L"x");
    run_case("app_missing", L"nosuchchild.exe", L"x");
    run_case("app_cmdline_null", L"child.exe", NULL);
    run_case("app_cmdline_null_space", L"my dir\\spaced.exe", NULL);
    run_case("app_cmdline_empty", L"child.exe", L"");
    run_case("app_directory", L"sub", L"x");

    SetEnvironmentVariableW(L"SETU_CPS_CHILD", NULL);
    return oracle_end();
}
