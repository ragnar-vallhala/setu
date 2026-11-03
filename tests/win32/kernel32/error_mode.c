/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: SetErrorMode / GetErrorMode and SetThreadErrorMode /
 * GetThreadErrorMode: return values, which bits are kept, the sticky
 * SEM_NOALIGNMENTFAULTEXCEPT bit, invalid thread-mode bits, the thread mode
 * of new threads, and inheritance by child processes with and without
 * CREATE_DEFAULT_ERROR_MODE (the child reports its modes as its exit code).
 *
 * oracle_begin() sets SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX |
 * SEM_NOOPENFILEERRORBOX first, so that is the starting process mode.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/errhandlingapi/nf-errhandlingapi-seterrormode
 *       https://learn.microsoft.com/en-us/windows/win32/api/errhandlingapi/nf-errhandlingapi-geterrormode
 *       https://learn.microsoft.com/en-us/windows/win32/api/errhandlingapi/nf-errhandlingapi-setthreaderrormode
 *       https://learn.microsoft.com/en-us/windows/win32/api/errhandlingapi/nf-errhandlingapi-getthreaderrormode
 *       https://learn.microsoft.com/en-us/windows/win32/procthread/process-creation-flags
 */
#include "oracle.h"

static void set_case(const char *name, UINT mode)
{
    UINT prev;
    ORACLE_CALL(prev, SetErrorMode(mode));
    oracle_case(name);
    o_hex32("prev", prev);
    o_lasterr();
    o_hex32("now", GetErrorMode());
    o_hex32("thread", GetThreadErrorMode());
    oracle_case_end();
}

static void tset_case(const char *name, DWORD mode, int with_old)
{
    DWORD old = 0xdeadbeef;
    BOOL r;
    ORACLE_CALL(r, SetThreadErrorMode(mode, with_old ? &old : NULL));
    oracle_case(name);
    o_bool("ret", r);
    o_lasterr();
    if (with_old) o_hex32("old", old);
    o_hex32("thread", GetThreadErrorMode());
    o_hex32("process", GetErrorMode());
    oracle_case_end();
}

static DWORD WINAPI thread_proc(void *arg)
{
    *(DWORD *)arg = GetThreadErrorMode();
    return 0;
}

static void child_case(const char *name, DWORD flags)
{
    wchar_t exe[MAX_PATH], cmd[MAX_PATH + 16];
    STARTUPINFOW si;
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    PROCESS_INFORMATION pi;
    DWORD code = 0xdeadbeef;
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    swprintf(cmd, MAX_PATH + 16, L"\"%ls\" child", exe);
    BOOL ok = CreateProcessW(exe, cmd, NULL, NULL, FALSE, flags, NULL, NULL, &si, &pi);
    if (ok) {
        WaitForSingleObject(pi.hProcess, 10000);
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    oracle_case(name);
    o_bool("created", ok);
    if (ok) {
        o_hex32("child_process_mode", code & 0xffff);
        o_hex32("child_thread_mode", code >> 16);
    }
    oracle_case_end();
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "child"))
        ExitProcess((GetErrorMode() & 0xffff) | (GetThreadErrorMode() << 16));

    oracle_begin("kernel32/error_mode");

    oracle_case("initial");
    o_hex32("process", GetErrorMode());
    o_hex32("thread", GetThreadErrorMode());
    oracle_case_end();

    child_case("child_inherits", 0);
    child_case("child_default_error_mode", CREATE_DEFAULT_ERROR_MODE);

    set_case("set_zero", 0);
    set_case("set_failcritical", SEM_FAILCRITICALERRORS);
    set_case("set_all_documented", SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    set_case("set_unknown_bits", 0x00f0);
    set_case("set_high_bits", 0x10000);
    set_case("set_zero_again", 0);

    tset_case("thread_set_failcritical", SEM_FAILCRITICALERRORS, 1);
    tset_case("thread_set_all_allowed", SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX, 1);
    tset_case("thread_set_null_old", 0, 0);
    tset_case("thread_set_alignment_rejected", SEM_NOALIGNMENTFAULTEXCEPT, 1);
    tset_case("thread_set_unknown_bit_rejected", 0x0100, 1);
    tset_case("thread_set_nogpfault", SEM_NOGPFAULTERRORBOX, 1);

    {
        DWORD seen = 0xdeadbeef;
        SetThreadErrorMode(SEM_FAILCRITICALERRORS, NULL);
        HANDLE t = CreateThread(NULL, 0, thread_proc, &seen, 0, NULL);
        WaitForSingleObject(t, 10000);
        CloseHandle(t);
        oracle_case("new_thread_mode");
        o_hex32("seen", seen);
        o_hex32("creator_thread", GetThreadErrorMode());
        oracle_case_end();
    }

    SetErrorMode(SEM_NOOPENFILEERRORBOX);
    child_case("child_inherits_after_change", 0);

    /* Sticky alignment bit last: it cannot be cleared again. */
    set_case("set_noalignmentfaultexcept", SEM_NOALIGNMENTFAULTEXCEPT);
    set_case("clear_after_alignment", 0);
    child_case("child_after_alignment", 0);

    return oracle_end();
}
