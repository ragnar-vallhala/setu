/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: standard handles of a child process.
 *
 * Build tools, shells and IDEs start children with STARTF_USESTDHANDLES and
 * pipes or files, then read the output. This test checks what the child sees
 * (GetStdHandle, GetFileType, GetStartupInfoW) and what arrives at the other
 * end, for pipes, files and NUL; for handles that are not inheritable or
 * not inherited; for NULL and INVALID_HANDLE_VALUE; for SetStdHandle in the
 * parent without STARTF_USESTDHANDLES; and for end-of-pipe conditions.
 *
 * The test re-runs its own .exe as the child. The child writes a small
 * report file (report.txt) in the current directory.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/ns-processthreadsapi-startupinfow
 *       https://learn.microsoft.com/en-us/windows/win32/procthread/creating-a-child-process-with-redirected-input-and-output
 *       https://learn.microsoft.com/en-us/windows/console/getstdhandle
 *       https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getfiletype
 *       https://learn.microsoft.com/en-us/windows/win32/api/namedpipeapi/nf-namedpipeapi-createpipe
 */
#include "oracle.h"

static wchar_t self[MAX_PATH];

/* ---- child side ------------------------------------------------------- */

static void report(const char *text)
{
    HANDLE f = CreateFileW(L"report.txt", FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, 0, NULL);
    DWORD n;
    WriteFile(f, text, (DWORD)strlen(text), &n, NULL);
    CloseHandle(f);
}

static const char *kind(HANDLE h)
{
    if (h == NULL) return "null";
    if (h == INVALID_HANDLE_VALUE) return "invalid";
    SetLastError(ORACLE_SENTINEL);
    DWORD t = GetFileType(h);
    DWORD e = GetLastError();
    static char b[4][48];
    static int i;
    char *s = b[i++ & 3];
    if (t == FILE_TYPE_UNKNOWN) snprintf(s, 48, "unknown/err%lu", e == ORACLE_SENTINEL ? 0ul : (unsigned long)e);
    else snprintf(s, 48, "type%lu", (unsigned long)t);
    return s;
}

static int child_main(const wchar_t *mode)
{
    char line[256];
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE), out = GetStdHandle(STD_OUTPUT_HANDLE),
           err = GetStdHandle(STD_ERROR_HANDLE);
    STARTUPINFOW si;
    GetStartupInfoW(&si);
    snprintf(line, sizeof line, "in=%s out=%s err=%s usestd=%d same_as_si=%d%d%d\n", kind(in), kind(out), kind(err),
             (si.dwFlags & STARTF_USESTDHANDLES) != 0, si.hStdInput == in, si.hStdOutput == out,
             si.hStdError == err);
    report(line);

    if (!wcscmp(mode, L"echo")) {
        /* Copy stdin to stdout with a prefix, then a line to stderr. */
        char buf[256];
        DWORD n = 0, w;
        BOOL ok = ReadFile(in, buf, sizeof buf, &n, NULL);
        snprintf(line, sizeof line, "read ok=%d n=%lu err=%lu\n", ok, (unsigned long)n,
                 ok ? 0ul : (unsigned long)GetLastError());
        report(line);
        WriteFile(out, "out:", 4, &w, NULL);
        if (n) WriteFile(out, buf, n, &w, NULL);
        BOOL ok2 = WriteFile(err, "err!", 4, &w, NULL);
        snprintf(line, sizeof line, "write_err ok=%d\n", ok2);
        report(line);
        /* A second read after the parent closed its end. */
        ok = ReadFile(in, buf, sizeof buf, &n, NULL);
        snprintf(line, sizeof line, "read2 ok=%d n=%lu err=%lu\n", ok, (unsigned long)n,
                 ok ? 0ul : (unsigned long)GetLastError());
        report(line);
        return 0;
    }
    if (!wcscmp(mode, L"write")) {
        DWORD w = 0;
        SetLastError(0);
        BOOL ok = WriteFile(out, "hello", 5, &w, NULL);
        snprintf(line, sizeof line, "write ok=%d n=%lu err=%lu\n", ok, (unsigned long)w,
                 ok ? 0ul : (unsigned long)GetLastError());
        report(line);
        return 0;
    }
    if (!wcscmp(mode, L"crt")) {
        /* The C runtime's stdio on top of the redirected handles. */
        printf("printf-line\n");
        fflush(stdout);
        fprintf(stderr, "stderr-line\n");
        fflush(stderr);
        char buf[64] = "";
        if (!fgets(buf, sizeof buf, stdin)) strcpy(buf, "(eof)\n");
        snprintf(line, sizeof line, "fgets=%s", buf);
        report(line);
        return 0;
    }
    if (!wcscmp(mode, L"write_after_reader_gone")) {
        DWORD w = 0;
        Sleep(300);
        BOOL ok = WriteFile(out, "x", 1, &w, NULL);
        snprintf(line, sizeof line, "write ok=%d err=%lu\n", ok, ok ? 0ul : (unsigned long)GetLastError());
        report(line);
        return 0;
    }
    return 0;
}

/* ---- parent side ------------------------------------------------------ */

static HANDLE inheritable(HANDLE h)
{
    SetHandleInformation(h, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    return h;
}

static HANDLE open_file(const wchar_t *name, BOOL inherit)
{
    SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, inherit };
    return CreateFileW(name, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, CREATE_ALWAYS, 0,
                       NULL);
}

static BOOL spawn(const wchar_t *mode, BOOL use_std, HANDLE in, HANDLE out, HANDLE err, BOOL inherit, DWORD flags,
                  PROCESS_INFORMATION *pi)
{
    wchar_t cmd[512];
    STARTUPINFOW si = {0};
    si.cb = sizeof si;
    if (use_std) {
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = in;
        si.hStdOutput = out;
        si.hStdError = err;
    }
    swprintf(cmd, 512, L"\"%ls\" %ls", self, mode);
    DeleteFileW(L"report.txt");
    return CreateProcessW(NULL, cmd, NULL, NULL, inherit, flags, NULL, NULL, &si, pi);
}

static void finish(PROCESS_INFORMATION *pi)
{
    DWORD code = 0;
    if (WaitForSingleObject(pi->hProcess, 20000) != WAIT_OBJECT_0) TerminateProcess(pi->hProcess, 1);
    GetExitCodeProcess(pi->hProcess, &code);
    o_u32("child_exit", code);
    o_file_state("report", L"report.txt");
    CloseHandle(pi->hProcess);
    CloseHandle(pi->hThread);
}

static void read_all(const char *key, HANDLE rd)
{
    char buf[256];
    DWORD total = 0, n;
    while (total < sizeof buf && ReadFile(rd, buf + total, sizeof buf - total, &n, NULL) && n) total += n;
    buf[total < sizeof buf ? total : sizeof buf - 1] = 0;
    o_str(key, buf);
}

static void file_text(const char *key, HANDLE f)
{
    char buf[256];
    DWORD n = 0;
    SetFilePointer(f, 0, NULL, FILE_BEGIN);
    ReadFile(f, buf, sizeof buf - 1, &n, NULL);
    buf[n] = 0;
    o_str(key, buf);
}

/* Child mode "echo" with pipes for stdin/stdout and a file for stderr. */
static void echo_case(const char *name, BOOL pipe_inheritable, BOOL inherit)
{
    SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, pipe_inheritable };
    HANDLE in_rd, in_wr, out_rd, out_wr, errf;
    PROCESS_INFORMATION pi;
    CreatePipe(&in_rd, &in_wr, &sa, 0);
    CreatePipe(&out_rd, &out_wr, &sa, 0);
    SetHandleInformation(in_wr, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(out_rd, HANDLE_FLAG_INHERIT, 0);
    errf = open_file(L"err.txt", pipe_inheritable);
    BOOL ok;
    ORACLE_CALL(ok, spawn(L"echo", TRUE, in_rd, out_wr, errf, inherit, 0, &pi));
    oracle_case(name);
    o_bool("created", ok);
    o_lasterr();
    CloseHandle(in_rd);
    CloseHandle(out_wr);
    DWORD n;
    WriteFile(in_wr, "data", 4, &n, NULL);
    CloseHandle(in_wr);
    if (ok) {
        read_all("stdout", out_rd);
        finish(&pi);
        file_text("stderr_file", errf);
    }
    CloseHandle(out_rd);
    CloseHandle(errf);
    oracle_case_end();
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

    oracle_begin("kernel32/std_handle_redirect");
    GetModuleFileNameW(NULL, self, MAX_PATH);
    PROCESS_INFORMATION pi;
    BOOL ok;

    echo_case("pipes_inherited", TRUE, TRUE);
    echo_case("pipes_not_inheritable", FALSE, TRUE);
    echo_case("pipes_inherit_false", TRUE, FALSE);

    {   /* stdout and stderr are the same pipe: both writes arrive in order. */
        SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
        HANDLE in_rd, in_wr, out_rd, out_wr;
        CreatePipe(&in_rd, &in_wr, &sa, 0);
        CreatePipe(&out_rd, &out_wr, &sa, 0);
        SetHandleInformation(in_wr, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(out_rd, HANDLE_FLAG_INHERIT, 0);
        ORACLE_CALL(ok, spawn(L"echo", TRUE, in_rd, out_wr, out_wr, TRUE, 0, &pi));
        oracle_case("stdout_and_stderr_same_pipe");
        o_bool("created", ok);
        CloseHandle(in_rd);
        CloseHandle(out_wr);
        CloseHandle(in_wr); /* no input at all: the child's first read sees EOF */
        if (ok) {
            read_all("combined", out_rd);
            finish(&pi);
        }
        CloseHandle(out_rd);
        oracle_case_end();
    }

    {   /* Files and NUL. */
        HANDLE nul = inheritable(CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                             NULL, OPEN_EXISTING, 0, NULL));
        HANDLE f = open_file(L"out.txt", TRUE);
        ORACLE_CALL(ok, spawn(L"write", TRUE, nul, f, nul, TRUE, 0, &pi));
        oracle_case("stdout_file_stdin_nul");
        o_bool("created", ok);
        if (ok) {
            finish(&pi);
            file_text("out_file", f);
        }
        oracle_case_end();
        CloseHandle(f);
        ORACLE_CALL(ok, spawn(L"write", TRUE, nul, nul, nul, TRUE, 0, &pi));
        oracle_case("all_nul");
        o_bool("created", ok);
        if (ok) finish(&pi);
        oracle_case_end();
        CloseHandle(nul);
    }

    {   /* NULL and INVALID_HANDLE_VALUE std handles. */
        ORACLE_CALL(ok, spawn(L"write", TRUE, NULL, NULL, NULL, TRUE, 0, &pi));
        oracle_case("null_handles");
        o_bool("created", ok);
        if (ok) finish(&pi);
        oracle_case_end();
        ORACLE_CALL(ok, spawn(L"write", TRUE, INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE, TRUE, 0,
                              &pi));
        oracle_case("invalid_handle_values");
        o_bool("created", ok);
        if (ok) finish(&pi);
        oracle_case_end();
    }

    {   /* A file handle that is not inheritable, passed with bInheritHandles TRUE. */
        HANDLE f = open_file(L"noinh.txt", FALSE);
        ORACLE_CALL(ok, spawn(L"write", TRUE, NULL, f, NULL, TRUE, 0, &pi));
        oracle_case("file_not_inheritable");
        o_bool("created", ok);
        if (ok) {
            finish(&pi);
            file_text("out_file", f);
        }
        oracle_case_end();
        CloseHandle(f);
    }

    {   /* No STARTF_USESTDHANDLES: the parent's SetStdHandle values are passed on. */
        /* stdin/stderr point at NUL too, so nothing of the harness's own
         * handles reaches the child. */
        HANDLE old_in = GetStdHandle(STD_INPUT_HANDLE), old_out = GetStdHandle(STD_OUTPUT_HANDLE),
               old_err = GetStdHandle(STD_ERROR_HANDLE);
        HANDLE nul = inheritable(CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                             NULL, OPEN_EXISTING, 0, NULL));
        SetStdHandle(STD_INPUT_HANDLE, nul);
        SetStdHandle(STD_ERROR_HANDLE, nul);
        HANDLE f = open_file(L"setstd.txt", TRUE);
        SetStdHandle(STD_OUTPUT_HANDLE, f);
        ORACLE_CALL(ok, spawn(L"write", FALSE, NULL, NULL, NULL, TRUE, 0, &pi));
        SetStdHandle(STD_OUTPUT_HANDLE, old_out);
        oracle_case("parent_setstdhandle_inherited");
        o_bool("created", ok);
        if (ok) {
            finish(&pi);
            file_text("out_file", f);
        }
        oracle_case_end();
        CloseHandle(f);

        f = open_file(L"setstd2.txt", TRUE);
        SetStdHandle(STD_OUTPUT_HANDLE, f);
        ORACLE_CALL(ok, spawn(L"write", FALSE, NULL, NULL, NULL, FALSE, 0, &pi));
        SetStdHandle(STD_OUTPUT_HANDLE, old_out);
        oracle_case("parent_setstdhandle_inherit_false");
        o_bool("created", ok);
        if (ok) {
            finish(&pi);
            file_text("out_file", f);
        }
        oracle_case_end();
        CloseHandle(f);

        f = open_file(L"setstd3.txt", TRUE);
        SetStdHandle(STD_OUTPUT_HANDLE, f);
        ORACLE_CALL(ok, spawn(L"write", FALSE, NULL, NULL, NULL, TRUE, DETACHED_PROCESS, &pi));
        SetStdHandle(STD_OUTPUT_HANDLE, old_out);
        oracle_case("parent_setstdhandle_detached");
        o_bool("created", ok);
        if (ok) {
            finish(&pi);
            file_text("out_file", f);
        }
        oracle_case_end();
        CloseHandle(f);
        SetStdHandle(STD_INPUT_HANDLE, old_in);
        SetStdHandle(STD_ERROR_HANDLE, old_err);
        CloseHandle(nul);
    }

    {   /* The CRT (printf/fprintf/fgets) over pipes. */
        SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
        HANDLE in_rd, in_wr, out_rd, out_wr, errf;
        CreatePipe(&in_rd, &in_wr, &sa, 0);
        CreatePipe(&out_rd, &out_wr, &sa, 0);
        SetHandleInformation(in_wr, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(out_rd, HANDLE_FLAG_INHERIT, 0);
        errf = open_file(L"crt_err.txt", TRUE);
        ORACLE_CALL(ok, spawn(L"crt", TRUE, in_rd, out_wr, errf, TRUE, 0, &pi));
        oracle_case("crt_stdio_over_pipes");
        o_bool("created", ok);
        CloseHandle(in_rd);
        CloseHandle(out_wr);
        DWORD n;
        WriteFile(in_wr, "typed line\r\nmore\r\n", 18, &n, NULL);
        CloseHandle(in_wr);
        if (ok) {
            read_all("stdout", out_rd);
            finish(&pi);
            file_text("stderr_file", errf);
        }
        CloseHandle(out_rd);
        CloseHandle(errf);
        oracle_case_end();
    }

    {   /* Writing to a pipe whose read end is gone. */
        SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
        HANDLE out_rd, out_wr;
        CreatePipe(&out_rd, &out_wr, &sa, 0);
        SetHandleInformation(out_rd, HANDLE_FLAG_INHERIT, 0);
        ORACLE_CALL(ok, spawn(L"write_after_reader_gone", TRUE, NULL, out_wr, NULL, TRUE, 0, &pi));
        oracle_case("write_to_closed_pipe");
        o_bool("created", ok);
        CloseHandle(out_wr);
        CloseHandle(out_rd);
        if (ok) finish(&pi);
        oracle_case_end();
    }

    return oracle_end();
}
