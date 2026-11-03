/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: how a child process ends and the exit codes its parent sees,
 * for the process (GetExitCodeProcess) and for its main thread
 * (GetExitCodeThread on PROCESS_INFORMATION.hThread).
 *
 * Covers returning from main, the CRT's exit()/_exit()/abort(), ExitProcess,
 * TerminateProcess on itself, ExitThread on the main thread followed by
 * ExitProcess or exit() in another thread, ExitProcess from a secondary
 * thread, atexit handlers, and unhandled faults that seh_exceptions does not
 * cover.
 *
 * Not covered: a process whose last thread of its own calls ExitThread. On
 * Windows 10 the loader's idle worker threads keep such a process alive for
 * a time that depends on timing, so its result is not deterministic.
 *
 * The test re-runs its own .exe as the child; the child's standard handles
 * are redirected to NUL so CRT messages never reach the harness.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-exitprocess
 *       https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-exitthread
 *       https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-terminateprocess
 *       https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getexitcodeprocess
 *       https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getexitcodethread
 *       https://learn.microsoft.com/en-us/windows/win32/procthread/terminating-a-process
 *       https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/abort
 *       https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/exit-exit-exit
 */
#include "oracle.h"

static wchar_t self[MAX_PATH];

/* ---- child side ------------------------------------------------------- */

static DWORD WINAPI exitprocess_later(void *arg)
{
    Sleep(200);
    ExitProcess((UINT)(ULONG_PTR)arg);
}

static DWORD WINAPI crt_exit_later(void *arg)
{
    Sleep(200);
    exit((int)(ULONG_PTR)arg);
}

static DWORD WINAPI spin_forever(void *arg)
{
    (void)arg;
    Sleep(INFINITE);
    return 0;
}

static void at_exit_handler(void) { ExitProcess(21); }

static volatile int zero;

static int child_main(const wchar_t *mode)
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (!wcscmp(mode, L"return")) return 7;
    if (!wcscmp(mode, L"exit")) exit(8);
    if (!wcscmp(mode, L"_exit")) _exit(9);
    if (!wcscmp(mode, L"exitprocess")) ExitProcess(10);
    if (!wcscmp(mode, L"terminate_self")) TerminateProcess(GetCurrentProcess(), 11);
    if (!wcscmp(mode, L"exitthread_then_other_exitprocess")) {
        CloseHandle(CreateThread(NULL, 0, exitprocess_later, (void *)14, 0, NULL));
        ExitThread(13);
    }
    if (!wcscmp(mode, L"exitthread_then_other_crt_exit")) {
        CloseHandle(CreateThread(NULL, 0, crt_exit_later, (void *)16, 0, NULL));
        ExitThread(15);
    }
    if (!wcscmp(mode, L"other_thread_exitprocess")) {
        CloseHandle(CreateThread(NULL, 0, exitprocess_later, (void *)17, 0, NULL));
        Sleep(INFINITE);
    }
    if (!wcscmp(mode, L"return_with_thread_running")) {
        CloseHandle(CreateThread(NULL, 0, spin_forever, NULL, 0, NULL));
        Sleep(50);
        return 18;
    }
    if (!wcscmp(mode, L"exitprocess_huge")) ExitProcess(0x80000001u);
    if (!wcscmp(mode, L"return_negative")) return -2;
    if (!wcscmp(mode, L"atexit_exitprocess")) {
        atexit(at_exit_handler);
        return 20;
    }
    if (!wcscmp(mode, L"abort")) abort();
    if (!wcscmp(mode, L"divide_by_zero")) return 1 / zero;
    if (!wcscmp(mode, L"raise_custom")) RaiseException(0xE0001234u, 0, 0, NULL);
    if (!wcscmp(mode, L"raise_noncontinuable")) RaiseException(0x20000001u, EXCEPTION_NONCONTINUABLE, 0, NULL);
    if (!wcscmp(mode, L"av_in_thread")) {
        HANDLE t = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)(ULONG_PTR)0x10, NULL, 0, NULL);
        WaitForSingleObject(t, INFINITE);
        return 22;
    }
    if (!wcscmp(mode, L"fastfail")) __fastfail(7);
    return 99;
}

/* ---- parent side ------------------------------------------------------ */

static void exit_case(const char *name, const wchar_t *mode)
{
    wchar_t cmd[512];
    STARTUPINFOW si = {0};
    PROCESS_INFORMATION pi;
    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                             OPEN_EXISTING, 0, NULL);
    SetHandleInformation(nul, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = si.hStdOutput = si.hStdError = nul;
    swprintf(cmd, 512, L"\"%ls\" %ls", self, mode);
    BOOL ok = CreateProcessW(NULL, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi);
    CloseHandle(nul);
    oracle_case(name);
    o_bool("created", ok);
    if (ok) {
        DWORD w = WaitForSingleObject(pi.hProcess, 15000);
        o_bool("exited", w == WAIT_OBJECT_0);
        if (w != WAIT_OBJECT_0) TerminateProcess(pi.hProcess, 1);
        DWORD tw = WaitForSingleObject(pi.hThread, 5000);
        o_bool("thread_signaled", tw == WAIT_OBJECT_0);
        DWORD code = 0, tcode = 0;
        GetExitCodeProcess(pi.hProcess, &code);
        GetExitCodeThread(pi.hThread, &tcode);
        o_hex32("process_exit_code", code);
        o_hex32("main_thread_exit_code", tcode);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    oracle_case_end();
}

/* The CRT's wide argv (msvcrt). */
typedef struct { int newmode; } oracle_startupinfo;
extern int __cdecl __wgetmainargs(int *, wchar_t ***, wchar_t ***, int, oracle_startupinfo *);

int main(void)
{
    int argc = 0;
    wchar_t **argv = NULL, **envp = NULL;
    oracle_startupinfo sinfo = {0};
    __wgetmainargs(&argc, &argv, &envp, 0, &sinfo);
    if (argc > 1) return child_main(argv[1]);
    oracle_begin("kernel32/child_exit_codes");
    GetModuleFileNameW(NULL, self, MAX_PATH);

    exit_case("return_from_main", L"return");
    exit_case("crt_exit", L"exit");
    exit_case("crt__exit", L"_exit");
    exit_case("exitprocess", L"exitprocess");
    exit_case("terminate_self", L"terminate_self");
    exit_case("exitthread_then_other_exitprocess", L"exitthread_then_other_exitprocess");
    exit_case("exitthread_then_other_crt_exit", L"exitthread_then_other_crt_exit");
    exit_case("other_thread_exitprocess", L"other_thread_exitprocess");
    exit_case("return_with_thread_running", L"return_with_thread_running");
    exit_case("exitprocess_high_bit", L"exitprocess_huge");
    exit_case("return_negative", L"return_negative");
    exit_case("atexit_handler_exitprocess", L"atexit_exitprocess");
    exit_case("crt_abort", L"abort");
    exit_case("divide_by_zero", L"divide_by_zero");
    exit_case("raise_custom_code", L"raise_custom");
    exit_case("raise_noncontinuable", L"raise_noncontinuable");
    exit_case("access_violation_in_thread", L"av_in_thread");
    exit_case("fastfail", L"fastfail");

    return oracle_end();
}
