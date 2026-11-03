/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: debugging a child process: the debug events it produces
 * (create process, exception breakpoint, output strings, exit), reading the
 * child's OutputDebugString text through ReadProcessMemory, continue
 * status codes, DebugActiveProcessStop, and the no-debugger behaviour of
 * OutputDebugString, IsDebuggerPresent and CheckRemoteDebuggerPresent.
 *
 * DLL load/unload events vary with the system, so only their presence is
 * reported; every other event is printed in order.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/debug/debugging-events
 *       https://learn.microsoft.com/en-us/windows/win32/api/debugapi/nf-debugapi-waitfordebugevent
 *       https://learn.microsoft.com/en-us/windows/win32/api/debugapi/nf-debugapi-continuedebugevent
 *       https://learn.microsoft.com/en-us/windows/win32/api/debugapi/nf-debugapi-outputdebugstringa
 *       https://learn.microsoft.com/en-us/windows/win32/api/debugapi/nf-debugapi-checkremotedebuggerpresent
 *       https://learn.microsoft.com/en-us/windows/win32/api/minwinbase/ns-minwinbase-output_debug_string_info
 */
#include "oracle.h"

static int child(void)
{
    BOOL remote = 2;
    CheckRemoteDebuggerPresent(GetCurrentProcess(), &remote);
    OutputDebugStringA("hello from child");
    OutputDebugStringW(L"wide string");
    if (IsDebuggerPresent()) OutputDebugStringA("debugger present");
    if (remote == TRUE) OutputDebugStringA("remote present");
    RaiseException(0xE0000ABC, 0, 0, NULL); /* the debugger continues it */
    return 0x42;
}

static const char *ev_name(DWORD code)
{
    switch (code) {
    case EXCEPTION_DEBUG_EVENT: return "exception";
    case CREATE_THREAD_DEBUG_EVENT: return "create_thread";
    case CREATE_PROCESS_DEBUG_EVENT: return "create_process";
    case EXIT_THREAD_DEBUG_EVENT: return "exit_thread";
    case EXIT_PROCESS_DEBUG_EVENT: return "exit_process";
    case LOAD_DLL_DEBUG_EVENT: return "load_dll";
    case UNLOAD_DLL_DEBUG_EVENT: return "unload_dll";
    case OUTPUT_DEBUG_STRING_EVENT: return "output_string";
    case RIP_EVENT: return "rip";
    default: return "other";
    }
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "child")) return child();
    oracle_begin("kernel32/debug_output");

    oracle_case("no_debugger");
    o_bool("is_debugger_present", IsDebuggerPresent());
    {
        BOOL remote = 2;
        BOOL ok = CheckRemoteDebuggerPresent(GetCurrentProcess(), &remote);
        o_bool("check_remote_ret", ok);
        o_i64("check_remote", remote);
    }
    oracle_case_end();
    {
        BOOL ok;
        ORACLE_CALL(ok, CheckRemoteDebuggerPresent(GetCurrentProcess(), NULL));
        oracle_case("check_remote_null_out");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        BOOL r = 2;
        ORACLE_CALL(ok, CheckRemoteDebuggerPresent(NULL, &r));
        oracle_case("check_remote_null_process");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
    }
    {
        DWORD sentinel = 0xDEADBEEF;
        SetLastError(sentinel);
        OutputDebugStringA("no debugger attached");
        oracle_case("output_debug_string_no_debugger");
        o_hex32("last_error", GetLastError());
        oracle_case_end();
        SetLastError(sentinel);
        OutputDebugStringW(L"no debugger attached");
        oracle_case("output_debug_string_w_no_debugger");
        o_hex32("last_error", GetLastError());
        oracle_case_end();
        SetLastError(sentinel);
        OutputDebugStringA(NULL);
        oracle_case("output_debug_string_null");
        o_hex32("last_error", GetLastError());
        oracle_case_end();
    }

    /* ---- debug a child ---- */
    char self[MAX_PATH], cmd[MAX_PATH + 16];
    GetModuleFileNameA(NULL, self, MAX_PATH);
    snprintf(cmd, sizeof cmd, "\"%s\" child", self);
    STARTUPINFOA si = {.cb = sizeof si};
    PROCESS_INFORMATION pi;
    BOOL ok = CreateProcessA(NULL, cmd, NULL, NULL, FALSE, DEBUG_ONLY_THIS_PROCESS, NULL, NULL, &si, &pi);
    oracle_case("create_debuggee");
    o_bool("ret", ok);
    oracle_case_end();
    if (!ok) return oracle_end();

    char seq[512] = "";
    int load_dlls = 0, n_events = 0, first_bp = 0, strings = 0;
    DWORD exit_code = 0;
    BOOL wrong_continue_ok = TRUE;
    DWORD wrong_continue_err = 0;
    for (;;) {
        DEBUG_EVENT ev;
        if (!WaitForDebugEvent(&ev, 30000)) {
            strcat(seq, "|timeout");
            break;
        }
        n_events++;
        DWORD cont = DBG_CONTINUE;
        if (ev.dwDebugEventCode == LOAD_DLL_DEBUG_EVENT) {
            load_dlls++;
            if (ev.u.LoadDll.hFile) CloseHandle(ev.u.LoadDll.hFile);
        } else if (ev.dwDebugEventCode == UNLOAD_DLL_DEBUG_EVENT ||
                   ev.dwDebugEventCode == CREATE_THREAD_DEBUG_EVENT ||
                   ev.dwDebugEventCode == EXIT_THREAD_DEBUG_EVENT) {
            /* thread pool threads come and go; not printed */
        } else {
            char item[200];
            snprintf(item, sizeof item, "%s%s", seq[0] ? "|" : "", ev_name(ev.dwDebugEventCode));
            strcat(seq, item);
            if (ev.dwDebugEventCode == CREATE_PROCESS_DEBUG_EVENT) {
                if (ev.u.CreateProcessInfo.hFile) CloseHandle(ev.u.CreateProcessInfo.hFile);
                oracle_case("create_process_event");
                o_bool("pid_matches", ev.dwProcessId == pi.dwProcessId);
                o_bool("tid_matches", ev.dwThreadId == pi.dwThreadId);
                o_bool("has_process_handle", ev.u.CreateProcessInfo.hProcess != NULL);
                o_bool("has_thread_handle", ev.u.CreateProcessInfo.hThread != NULL);
                o_bool("has_start_address", ev.u.CreateProcessInfo.lpStartAddress != NULL);
                o_bool("unicode_flag_is_0_or_1", ev.u.CreateProcessInfo.fUnicode <= 1);
                oracle_case_end();
                /* A wrong thread id is rejected. */
                wrong_continue_ok = ContinueDebugEvent(ev.dwProcessId, ev.dwThreadId + 4000, DBG_CONTINUE);
                wrong_continue_err = GetLastError();
            } else if (ev.dwDebugEventCode == EXCEPTION_DEBUG_EVENT) {
                DWORD code = ev.u.Exception.ExceptionRecord.ExceptionCode;
                snprintf(item, sizeof item, ":%08lx:%s", code, ev.u.Exception.dwFirstChance ? "first" : "second");
                strcat(seq, item);
                if (code == EXCEPTION_BREAKPOINT) first_bp++;
                else cont = DBG_EXCEPTION_NOT_HANDLED;
                if (code == 0xE0000ABC && !ev.u.Exception.dwFirstChance) cont = DBG_CONTINUE;
            } else if (ev.dwDebugEventCode == OUTPUT_DEBUG_STRING_EVENT) {
                OUTPUT_DEBUG_STRING_INFO *o = &ev.u.DebugString;
                char buf[128] = "";
                SIZE_T got = 0;
                SIZE_T want = o->nDebugStringLength < sizeof buf ? o->nDebugStringLength : sizeof buf - 1;
                ReadProcessMemory(pi.hProcess, o->lpDebugStringData, buf, want, &got);
                buf[sizeof buf - 1] = 0;
                snprintf(item, sizeof item, ":u%d:n%u:%s", o->fUnicode, o->nDebugStringLength, buf);
                strcat(seq, item);
                strings++;
            } else if (ev.dwDebugEventCode == EXIT_PROCESS_DEBUG_EVENT) {
                exit_code = ev.u.ExitProcess.dwExitCode;
            }
        }
        ContinueDebugEvent(ev.dwProcessId, ev.dwThreadId, cont);
        if (ev.dwDebugEventCode == EXIT_PROCESS_DEBUG_EVENT) break;
    }
    oracle_case("debug_event_sequence");
    o_str("seq", seq);
    o_bool("some_dll_loads", load_dlls > 0);
    o_i64("breakpoints", first_bp);
    o_i64("strings", strings);
    o_hex32("exit_code", exit_code);
    oracle_case_end();
    oracle_case("continue_wrong_thread");
    o_bool("ret", wrong_continue_ok);
    o_u32("error", wrong_continue_ok ? 0 : wrong_continue_err);
    oracle_case_end();
    WaitForSingleObject(pi.hProcess, 10000);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    {
        DEBUG_EVENT ev;
        ORACLE_CALL(ok, WaitForDebugEvent(&ev, 0));
        oracle_case("wait_debug_event_no_debuggee");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(ok, ContinueDebugEvent(GetCurrentProcessId(), GetCurrentThreadId(), DBG_CONTINUE));
        oracle_case("continue_without_event");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(ok, DebugActiveProcessStop(pi.dwProcessId));
        oracle_case("stop_debugging_exited");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(ok, DebugActiveProcess(GetCurrentProcessId()));
        oracle_case("debug_self");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
    }

    return oracle_end();
}
