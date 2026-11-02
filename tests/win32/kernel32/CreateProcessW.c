/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: CreateProcessW: exit codes, command-line parsing in the child,
 * handle inheritance, environment and current directory passing, and the
 * error codes for bad executables.
 *
 * The test re-runs its own .exe as the child, selected by the first argument.
 * The child reports back through its exit code or a file in the current
 * directory, never through stdout.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessw
 *       https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getexitcodeprocess
 *       https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-terminateprocess
 *       https://learn.microsoft.com/en-us/cpp/c-language/parsing-c-command-line-arguments
 *       https://learn.microsoft.com/en-us/windows/win32/procthread/inheritance
 */
#include "oracle.h"

static wchar_t self[MAX_PATH];

/* ---- child side ------------------------------------------------------- */

static int child_main(int argc, wchar_t **argv)
{
    const wchar_t *mode = argv[1];
    if (!wcscmp(mode, L"exit")) return _wtoi(argv[2]);
    if (!wcscmp(mode, L"args")) {
        /* Write argv[2..] to args.txt, one per line, as UTF-8. */
        FILE *f = _wfopen(L"args.txt", L"wb");
        fprintf(f, "argc=%d\n", argc - 2);
        for (int i = 2; i < argc; i++) fprintf(f, "[%s]\n", oracle_utf8(argv[i]));
        fclose(f);
        return 0;
    }
    if (!wcscmp(mode, L"handle")) {
        /* argv[2] is a handle value from the parent: is it usable here? */
        HANDLE h = (HANDLE)(ULONG_PTR)wcstoull(argv[2], NULL, 16);
        DWORD flags;
        if (!GetHandleInformation(h, &flags)) return 100;
        return SetEvent(h) ? 1 : 2;
    }
    if (!wcscmp(mode, L"env")) {
        wchar_t buf[64];
        DWORD r = GetEnvironmentVariableW(L"SETU_CHILD_VAR", buf, 64);
        if (!r) return 200;
        return wcscmp(buf, L"from-parent") ? 201 : 202;
    }
    if (!wcscmp(mode, L"cwd")) {
        /* Return 1 if the current directory ends in \childdir. */
        wchar_t buf[MAX_PATH];
        GetCurrentDirectoryW(MAX_PATH, buf);
        size_t n = wcslen(buf);
        return n >= 9 && !_wcsicmp(buf + n - 9, L"\\childdir") ? 1 : 0;
    }
    if (!wcscmp(mode, L"sleep")) {
        Sleep(60000);
        return 0;
    }
    if (!wcscmp(mode, L"lasterror")) {
        /* A fresh process starts with last error 0. */
        return (int)GetLastError();
    }
    return 99;
}

/* ---- parent side ------------------------------------------------------ */

static BOOL spawn(const wchar_t *app, const wchar_t *args, BOOL inherit, DWORD flags, void *env,
                  const wchar_t *dir, PROCESS_INFORMATION *pi)
{
    wchar_t cmd[1024];
    STARTUPINFOW si = {0};
    si.cb = sizeof si;
    if (args) swprintf(cmd, 1024, L"\"%ls\" %ls", self, args);
    return CreateProcessW(app, args ? cmd : NULL, NULL, NULL, inherit, flags, env, dir, &si, pi);
}

static DWORD run(const wchar_t *args, BOOL inherit, DWORD flags, void *env, const wchar_t *dir, BOOL *ok)
{
    PROCESS_INFORMATION pi;
    DWORD code = 0xffffffff;
    *ok = spawn(NULL, args, inherit, flags, env, dir, &pi);
    if (*ok) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    return code;
}

static void exit_case(const char *name, const wchar_t *args)
{
    BOOL ok;
    DWORD code = run(args, FALSE, 0, NULL, NULL, &ok);
    oracle_case(name);
    o_bool("ret", ok);
    o_hex32("exit_code", code);
    oracle_case_end();
}

static void args_case(const char *name, const wchar_t *args)
{
    wchar_t full[512];
    BOOL ok;
    swprintf(full, 512, L"args %ls", args);
    DeleteFileW(L"args.txt");
    run(full, FALSE, 0, NULL, NULL, &ok);
    oracle_case(name);
    o_bool("ret", ok);
    o_file_state("args", L"args.txt");
    oracle_case_end();
}

static void fail_case(const char *name, const wchar_t *app, const wchar_t *cmdline)
{
    wchar_t cmd[512];
    STARTUPINFOW si = {0};
    PROCESS_INFORMATION pi;
    BOOL ok;
    si.cb = sizeof si;
    if (cmdline) wcscpy(cmd, cmdline);
    ORACLE_CALL(ok, CreateProcessW(app, cmdline ? cmd : NULL, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi));
    oracle_case(name);
    o_bool("ret", ok);
    o_lasterr();
    oracle_case_end();
    if (ok) {
        TerminateProcess(pi.hProcess, 0);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
}

static void write_bytes(const wchar_t *name, const void *data, DWORD size)
{
    DWORD n;
    HANDLE h = CreateFileW(name, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    WriteFile(h, data, size, &n, NULL);
    CloseHandle(h);
}

/* The CRT's wide argv (msvcrt): the child's view of its command line. */
typedef struct { int newmode; } oracle_startupinfo;
extern int __cdecl __wgetmainargs(int *, wchar_t ***, wchar_t ***, int, oracle_startupinfo *);

int main(void)
{
    int argc = 0;
    wchar_t **argv = NULL, **envp = NULL;
    oracle_startupinfo si = {0};
    __wgetmainargs(&argc, &argv, &envp, 0, &si);
    if (argc >= 2) return child_main(argc, argv);

    oracle_begin("kernel32/CreateProcessW");
    GetModuleFileNameW(NULL, self, MAX_PATH);

    exit_case("exit_zero", L"exit 0");
    exit_case("exit_42", L"exit 42");
    exit_case("exit_negative", L"exit -1");
    exit_case("exit_259_still_active_value", L"exit 259");
    exit_case("child_starts_with_last_error_zero", L"lasterror");

    args_case("args_simple", L"a b c");
    args_case("args_quoted_space", L"\"a b\" c");
    args_case("args_escaped_quote", L"a\\\"b");
    args_case("args_backslashes_before_quote", L"\"a\\\\\" b");
    args_case("args_backslashes_not_before_quote", L"a\\\\b");
    args_case("args_odd_backslashes_quote", L"a\\\\\\\"b");
    args_case("args_double_quote_inside", L"\"a\"\"b\"");
    args_case("args_empty_quoted", L"\"\" x");
    args_case("args_tabs", L"a\tb");
    args_case("args_unterminated_quote", L"\"a b");
    args_case("args_unicode", L"\u00e9t\u00e9 \u65e5\u672c");

    {   /* Handle inheritance. */
        SECURITY_ATTRIBUTES sa = {sizeof sa, NULL, TRUE};
        HANDLE inh = CreateEventW(&sa, TRUE, FALSE, NULL);
        HANDLE noinh = CreateEventW(NULL, TRUE, FALSE, NULL);
        wchar_t args[64];
        BOOL ok;
        DWORD code;

        swprintf(args, 64, L"handle %llx", (unsigned long long)(ULONG_PTR)inh);
        code = run(args, TRUE, 0, NULL, NULL, &ok);
        oracle_case("inherit_inheritable_handle");
        o_bool("ret", ok);
        o_u32("exit_code", code);
        o_bool("parent_sees_set", WaitForSingleObject(inh, 0) == WAIT_OBJECT_0);
        oracle_case_end();

        ResetEvent(inh);
        code = run(args, FALSE, 0, NULL, NULL, &ok);
        oracle_case("inherit_disabled");
        o_bool("ret", ok);
        o_bool("child_could_use_handle", code == 1);
        o_bool("parent_sees_set", WaitForSingleObject(inh, 0) == WAIT_OBJECT_0);
        oracle_case_end();

        swprintf(args, 64, L"handle %llx", (unsigned long long)(ULONG_PTR)noinh);
        code = run(args, TRUE, 0, NULL, NULL, &ok);
        oracle_case("inherit_non_inheritable_handle");
        o_bool("ret", ok);
        o_bool("child_could_use_handle", code == 1);
        o_bool("parent_sees_set", WaitForSingleObject(noinh, 0) == WAIT_OBJECT_0);
        oracle_case_end();
        CloseHandle(inh);
        CloseHandle(noinh);
    }

    {   /* Environment: inherited by default, replaced by lpEnvironment. */
        BOOL ok;
        DWORD code;
        SetEnvironmentVariableW(L"SETU_CHILD_VAR", L"from-parent");
        code = run(L"env", FALSE, 0, NULL, NULL, &ok);
        oracle_case("env_inherited");
        o_bool("ret", ok);
        o_u32("exit_code", code);
        oracle_case_end();

        static wchar_t blk[] = L"SETU_OTHER=1\0SystemRoot=C:\\Windows\0";
        code = run(L"env", FALSE, CREATE_UNICODE_ENVIRONMENT, blk, NULL, &ok);
        oracle_case("env_replaced");
        o_bool("ret", ok);
        o_u32("exit_code", code);
        oracle_case_end();

        SetEnvironmentVariableW(L"SETU_CHILD_VAR", NULL);
    }

    {   /* Current directory. */
        BOOL ok;
        DWORD code;
        CreateDirectoryW(L"childdir", NULL);
        code = run(L"cwd", FALSE, 0, NULL, L"childdir", &ok);
        oracle_case("cwd_set");
        o_bool("ret", ok);
        o_u32("exit_code", code);
        oracle_case_end();
        PROCESS_INFORMATION pi;
        ORACLE_CALL(ok, spawn(NULL, L"cwd", FALSE, 0, NULL, L"no_such_dir", &pi));
        oracle_case("cwd_missing");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        if (ok) { CloseHandle(pi.hProcess); CloseHandle(pi.hThread); }
    }

    {   /* TerminateProcess and exit codes of a running process. */
        PROCESS_INFORMATION pi;
        DWORD code = 0;
        BOOL ok = spawn(NULL, L"sleep", FALSE, 0, NULL, NULL, &pi);
        GetExitCodeProcess(pi.hProcess, &code);
        oracle_case("running_exit_code");
        o_bool("spawned", ok);
        o_u32("exit_code", code);
        oracle_case_end();
        ORACLE_CALL(ok, TerminateProcess(pi.hProcess, 7));
        WaitForSingleObject(pi.hProcess, INFINITE);
        GetExitCodeProcess(pi.hProcess, &code);
        oracle_case("terminate");
        o_bool("ret", ok);
        o_lasterr();
        o_u32("exit_code", code);
        oracle_case_end();
        ORACLE_CALL(ok, TerminateProcess(pi.hProcess, 8));
        oracle_case("terminate_twice");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    {   /* TerminateProcess on a process that exited by itself. */
        PROCESS_INFORMATION pi;
        DWORD code = 0;
        BOOL ok = spawn(NULL, L"exit 3", FALSE, 0, NULL, NULL, &pi);
        WaitForSingleObject(pi.hProcess, INFINITE);
        ORACLE_CALL(ok, TerminateProcess(pi.hProcess, 9));
        GetExitCodeProcess(pi.hProcess, &code);
        oracle_case("terminate_exited");
        o_bool("ret", ok);
        o_lasterr();
        o_u32("exit_code", code);
        oracle_case_end();
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    {   /* TerminateProcess on a process that never ran (suspended). */
        PROCESS_INFORMATION pi;
        DWORD code = 0;
        BOOL ok = spawn(NULL, L"exit 5", FALSE, CREATE_SUSPENDED, NULL, NULL, &pi);
        ORACLE_CALL(ok, TerminateProcess(pi.hProcess, 9));
        WaitForSingleObject(pi.hProcess, INFINITE);
        GetExitCodeProcess(pi.hProcess, &code);
        oracle_case("terminate_suspended");
        o_bool("ret", ok);
        o_lasterr();
        o_u32("exit_code", code);
        oracle_case_end();
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    {   /* Suspended start. */
        PROCESS_INFORMATION pi;
        DWORD code = 0, prev;
        BOOL ok = spawn(NULL, L"exit 5", FALSE, CREATE_SUSPENDED, NULL, NULL, &pi);
        oracle_case("suspended");
        o_bool("ret", ok);
        o_bool("not_finished", WaitForSingleObject(pi.hProcess, 200) == WAIT_TIMEOUT);
        prev = ResumeThread(pi.hThread);
        o_u32("resume_prev_count", prev);
        WaitForSingleObject(pi.hProcess, INFINITE);
        GetExitCodeProcess(pi.hProcess, &code);
        o_u32("exit_code", code);
        oracle_case_end();
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    /* Error cases. */
    {
        HANDLE h = CreateFileW(L"text.exe", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        DWORD n;
        WriteFile(h, "this is not a program", 21, &n, NULL);
        CloseHandle(h);
        h = CreateFileW(L"empty.exe", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        CloseHandle(h);
        h = CreateFileW(L"mz_only.exe", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        WriteFile(h, "MZ", 2, &n, NULL);
        CloseHandle(h);
    }
    {   /* More file shapes (probes for the bad-executable error codes). */
        unsigned char img[0x200];
        write_bytes(L"one_byte.exe", "X", 1);
        write_bytes(L"text.txt", "this is not a program", 21);
        write_bytes(L"text_noext", "this is not a program", 21);
        write_bytes(L"text.com", "this is not a program", 21);
        write_bytes(L"text.pif", "this is not a program", 21);
        write_bytes(L"text.scr", "this is not a program", 21);
        write_bytes(L"text.dll", "this is not a program", 21);
        write_bytes(L"mz_text.exe", "MZ is not a program..", 21);
        /* A full 64-byte DOS header whose e_lfanew points at itself. */
        memset(img, 0, sizeof img);
        img[0] = 'M'; img[1] = 'Z';
        write_bytes(L"mz_header.exe", img, 64);
        write_bytes(L"mz_header.txt", img, 64);
        write_bytes(L"mz_header_noext", img, 64);
        /* DOS header with e_lfanew past the end of the file. */
        img[0x3c] = 0x00; img[0x3d] = 0x10;
        write_bytes(L"mz_lfanew_eof.exe", img, 64);
        /* A 512-byte DOS program: header plus zeros, no new header. */
        img[0x3c] = 0; img[0x3d] = 0;
        write_bytes(L"mz_dos_zeros.exe", img, sizeof img);
        /* DOS header + a Win16 NE header. */
        memset(img, 0, sizeof img);
        img[0] = 'M'; img[1] = 'Z'; img[0x3c] = 0x40;
        img[0x40] = 'N'; img[0x41] = 'E'; img[0x40 + 0x36] = 2;
        write_bytes(L"mz_ne_win16.exe", img, sizeof img);
        /* DOS header + PE header with an unknown machine. */
        memset(img, 0, sizeof img);
        img[0] = 'M'; img[1] = 'Z'; img[0x3c] = 0x40;
        img[0x40] = 'P'; img[0x41] = 'E';
        img[0x44] = 0x34; img[0x45] = 0x12;          /* Machine 0x1234 */
        img[0x54] = 0xf0;                            /* SizeOfOptionalHeader */
        img[0x58] = 0x0b; img[0x59] = 0x02;          /* PE32+ magic */
        write_bytes(L"mz_pe_bad_machine.exe", img, sizeof img);
        CreateDirectoryW(L"dir.exe", NULL);
    }
    fail_case("missing_exe_app_name", L"no_such_program.exe", NULL);
    fail_case("missing_exe_cmdline", NULL, L"no_such_program.exe arg");
    fail_case("missing_exe_in_missing_dir", NULL, L"nodir\\no_such_program.exe");
    fail_case("text_file_as_exe", L"text.exe", NULL);
    fail_case("empty_file_as_exe", L"empty.exe", NULL);
    fail_case("mz_only_exe", L"mz_only.exe", NULL);
    fail_case("directory_as_exe", L"childdir", NULL);
    fail_case("empty_cmdline", NULL, L"");
    fail_case("one_byte_exe", L"one_byte.exe", NULL);
    fail_case("text_file_txt_ext", L"text.txt", NULL);
    fail_case("text_file_no_ext", L"text_noext", NULL);
    fail_case("text_file_com_ext", L"text.com", NULL);
    fail_case("text_file_via_cmdline", NULL, L"text.exe");
    fail_case("mz_text_exe", L"mz_text.exe", NULL);
    fail_case("mz_header_only_exe", L"mz_header.exe", NULL);
    fail_case("mz_header_txt_ext", L"mz_header.txt", NULL);
    fail_case("mz_header_no_ext", L"mz_header_noext", NULL);
    fail_case("text_file_pif_ext", L"text.pif", NULL);
    fail_case("text_file_scr_ext", L"text.scr", NULL);
    fail_case("text_file_dll_ext", L"text.dll", NULL);
    fail_case("mz_lfanew_past_eof_exe", L"mz_lfanew_eof.exe", NULL);
    fail_case("mz_dos_program_exe", L"mz_dos_zeros.exe", NULL);
    fail_case("mz_ne_win16_exe", L"mz_ne_win16.exe", NULL);
    fail_case("mz_pe_bad_machine_exe", L"mz_pe_bad_machine.exe", NULL);
    fail_case("directory_with_exe_ext", L"dir.exe", NULL);
    fail_case("directory_via_cmdline", NULL, L"dir.exe");
    /* Last on purpose: if this crashes, every earlier case is still recorded. */
    fail_case("both_null", NULL, NULL);

    return oracle_end();
}
