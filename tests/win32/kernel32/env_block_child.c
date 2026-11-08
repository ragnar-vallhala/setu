/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: the environment block a child process receives.
 *
 * With lpEnvironment NULL the child gets a copy of the parent's current
 * environment (including changes and deletions made with
 * SetEnvironmentVariableW). With an explicit block, Unicode or ANSI, the
 * child sees exactly that block: this test checks the order of entries,
 * duplicates differing in case, entries without '=', entries starting with
 * '=', the empty block, and what lookups in the child return.
 *
 * The child writes what it sees to env.txt. For blocks the test builds itself
 * it prints every entry; for inherited environments only SETU_* entries, so
 * no machine data is printed.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/procthread/environment-variables
 *       https://learn.microsoft.com/en-us/windows/win32/procthread/changing-environment-variables
 *       https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessw
 *       https://learn.microsoft.com/en-us/windows/win32/api/processenv/nf-processenv-getenvironmentstringsw
 */
#include "oracle.h"

static wchar_t self[MAX_PATH];

/* ---- child side ------------------------------------------------------- */

/* Writes s, with the current directory (should it appear) as "<cwd>". */
static void out(FILE *f, const wchar_t *s)
{
    wchar_t cwd[MAX_PATH];
    DWORD n = GetCurrentDirectoryW(MAX_PATH, cwd);
    for (const wchar_t *p = s; *p;) {
        if (n && !_wcsnicmp(p, cwd, n)) {
            fprintf(f, "<cwd>");
            p += n;
            continue;
        }
        wchar_t one[2] = { *p++, 0 };
        fprintf(f, "%s", oracle_utf8(one));
    }
}

static int child_main(const wchar_t *mode)
{
    FILE *f = _wfopen(L"env.txt", L"wb");
    int all = !wcscmp(mode, L"all");
    wchar_t *block = GetEnvironmentStringsW();
    int n = 0;
    for (wchar_t *p = block; *p; p += wcslen(p) + 1) {
        n++;
        if (all || !_wcsnicmp(p, L"SETU_", 5)) {
            out(f, p);
            fprintf(f, "\n");
        }
    }
    if (all) fprintf(f, "count=%d\n", n);
    FreeEnvironmentStringsW(block);
    /* Lookups. */
    static const wchar_t *names[] = { L"SETU_A", L"setu_a", L"SETU_DUP", L"SETU_B", L"SETU_NOEQ", L"=X:", L"SETU_EQ",
                                      L"SETU_HIDE", L"SETU_BIG", L"SETU_EMPTY" };
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        static wchar_t buf[40000];
        SetLastError(0);
        DWORD r = GetEnvironmentVariableW(names[i], buf, 40000);
        DWORD e = GetLastError();
        out(f, L"get ");
        out(f, names[i]);
        if (r && r < 40000 && wcslen(buf) > 40) fprintf(f, " -> len %lu\n", (unsigned long)wcslen(buf));
        else if (r) {
            out(f, L" -> [");
            out(f, buf);
            out(f, L"]\n");
        } else fprintf(f, " -> missing err=%lu\n", (unsigned long)e);
    }
    {
        wchar_t buf[128];
        ExpandEnvironmentStringsW(L"%SETU_A%|%SETU_B%|%SETU_MISSING%", buf, 128);
        out(f, L"expand ");
        out(f, buf);
        out(f, L"\n");
    }
    fclose(f);
    return 0;
}

/* ---- parent side ------------------------------------------------------ */

static void spawn_case(const char *name, const wchar_t *mode, void *env, DWORD flags)
{
    wchar_t cmd[512];
    STARTUPINFOW si = {0};
    PROCESS_INFORMATION pi;
    BOOL ok;
    si.cb = sizeof si;
    swprintf(cmd, 512, L"\"%ls\" %ls", self, mode);
    DeleteFileW(L"env.txt");
    ORACLE_CALL(ok, CreateProcessW(self, cmd, NULL, NULL, FALSE, flags, env, NULL, &si, &pi));
    oracle_case(name);
    o_bool("created", ok);
    o_lasterr();
    if (ok) {
        DWORD code = 0;
        if (WaitForSingleObject(pi.hProcess, 20000) != WAIT_OBJECT_0) TerminateProcess(pi.hProcess, 1);
        GetExitCodeProcess(pi.hProcess, &code);
        o_hex32("exit_code", code);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        char text[4096] = "";
        HANDLE h = CreateFileW(L"env.txt", GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
        DWORD n = 0;
        if (h != INVALID_HANDLE_VALUE) {
            ReadFile(h, text, sizeof text - 1, &n, NULL);
            text[n] = 0;
            CloseHandle(h);
        }
        o_bool("report_exists", h != INVALID_HANDLE_VALUE);
        o_str("child_sees", text);
    }
    oracle_case_end();
}

/* Build a double-NUL-terminated wide block from '|'-separated entries. */
static wchar_t *wblock(const wchar_t *spec, wchar_t *buf)
{
    size_t i = 0;
    for (const wchar_t *p = spec; *p; p++) buf[i++] = *p == L'|' ? 0 : *p;
    buf[i++] = 0;
    buf[i] = 0;
    return buf;
}

static char *ablock(const char *spec, char *buf)
{
    size_t i = 0;
    for (const char *p = spec; *p; p++) buf[i++] = *p == '|' ? 0 : *p;
    buf[i++] = 0;
    buf[i] = 0;
    return buf;
}

int main(void)
{
    typedef struct { int newmode; } startinfo;
    extern int __cdecl __wgetmainargs(int *, wchar_t ***, wchar_t ***, int, startinfo *);
    int argc = 0;
    wchar_t **argv = NULL, **envp = NULL;
    startinfo sinfo = {0};
    __wgetmainargs(&argc, &argv, &envp, 0, &sinfo);
    if (argc > 1) return child_main(argv[1]);

    oracle_begin("kernel32/env_block_child");
    GetModuleFileNameW(NULL, self, MAX_PATH);
    wchar_t wb[1024];
    char ab[1024];
    const DWORD U = CREATE_UNICODE_ENVIRONMENT;

    /* ---- inherited (lpEnvironment NULL) ---- */
    SetEnvironmentVariableW(L"SETU_B", L"b-value");
    SetEnvironmentVariableW(L"SETU_A", L"a-value");
    SetEnvironmentVariableW(L"SETU_HIDE", L"x");
    SetEnvironmentVariableW(L"SETU_EMPTY", L"");
    spawn_case("inherit_after_set", L"setu", NULL, 0);
    SetEnvironmentVariableW(L"SETU_HIDE", NULL);
    spawn_case("inherit_after_delete", L"setu", NULL, 0);
    {
        static wchar_t big[32768];
        for (int i = 0; i < 32766; i++) big[i] = L'a' + i % 26;
        big[32766] = 0;
        BOOL ok;
        ORACLE_CALL(ok, SetEnvironmentVariableW(L"SETU_BIG", big));
        oracle_case("set_big_value");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        spawn_case("inherit_big_value", L"setu", NULL, 0);
        SetEnvironmentVariableW(L"SETU_BIG", NULL);
    }
    spawn_case("inherit_with_unicode_flag", L"setu", NULL, U);

    /* ---- explicit Unicode blocks ---- */
    spawn_case("unicode_sorted", L"all", wblock(L"SETU_A=1|SETU_B=2", wb), U);
    spawn_case("unicode_unsorted", L"all", wblock(L"SETU_B=2|SETU_A=1|SETU_DUP=x", wb), U);
    spawn_case("unicode_case_duplicates", L"all", wblock(L"setu_a=lower|SETU_A=upper", wb), U);
    spawn_case("unicode_exact_duplicates", L"all", wblock(L"SETU_DUP=first|SETU_DUP=second", wb), U);
    spawn_case("unicode_entry_without_equals", L"all", wblock(L"SETU_A=1|SETU_NOEQ|SETU_B=2", wb), U);
    spawn_case("unicode_drive_entry", L"all", wblock(L"=X:=X:\\dir|SETU_A=1", wb), U);
    spawn_case("unicode_value_with_equals", L"all", wblock(L"SETU_EQ=a=b=c", wb), U);
    spawn_case("unicode_empty_value", L"all", wblock(L"SETU_EMPTY=|SETU_A=1", wb), U);
    spawn_case("unicode_non_ascii", L"all", wblock(L"SETU_A=\x00e9\x4e2d|SETU_B=\x00fc", wb), U);
    {
        wchar_t empty[2] = { 0, 0 };
        spawn_case("unicode_empty_block", L"all", empty, U);
    }

    /* ---- ANSI blocks ---- */
    spawn_case("ansi_block", L"all", ablock("SETU_B=2|SETU_A=1", ab), 0);
    spawn_case("ansi_block_high_chars", L"all", ablock("SETU_A=\xe9\xfc", ab), 0);
    {
        char empty[2] = { 0, 0 };
        spawn_case("ansi_empty_block", L"all", empty, 0);
    }
    /* A Unicode block passed without CREATE_UNICODE_ENVIRONMENT: read as ANSI. */
    spawn_case("unicode_block_without_flag", L"all", wblock(L"SETU_A=1|SETU_B=2", wb), 0);

    /* Probe: is the last error cleared by every successful CreateProcess,
     * or only when the environment is inherited? */
    {
        wchar_t cmd[512];
        char cmda[512];
        STARTUPINFOW si = {0};
        STARTUPINFOA sia = {0};
        PROCESS_INFORMATION pi;
        BOOL ok;
        si.cb = sizeof si;
        sia.cb = sizeof sia;
        swprintf(cmd, 512, L"\"%ls\" setu", self);
        ORACLE_CALL(ok, CreateProcessW(self, cmd, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL, &si, &pi));
        oracle_case("probe_suspended_inherit");
        o_bool("created", ok);
        o_lasterr();
        oracle_case_end();
        if (ok) {
            TerminateProcess(pi.hProcess, 0);
            WaitForSingleObject(pi.hProcess, 20000);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
        snprintf(cmda, sizeof cmda, "\"%ls\" setu", self);
        ORACLE_CALL(ok, CreateProcessA(NULL, cmda, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL, &sia, &pi));
        oracle_case("probe_ansi_suspended_inherit");
        o_bool("created", ok);
        o_lasterr();
        oracle_case_end();
        if (ok) {
            TerminateProcess(pi.hProcess, 0);
            WaitForSingleObject(pi.hProcess, 20000);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
        ORACLE_CALL(ok, CreateProcessW(self, cmd, NULL, NULL, FALSE, CREATE_SUSPENDED, ablock("SETU_A=1", ab),
                                       NULL, &si, &pi));
        oracle_case("probe_suspended_ansi_block");
        o_bool("created", ok);
        o_lasterr();
        oracle_case_end();
        if (ok) {
            TerminateProcess(pi.hProcess, 0);
            WaitForSingleObject(pi.hProcess, 20000);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
    }

    return oracle_end();
}
