/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: process objects: OpenProcess access checks and errors,
 * GetExitCodeProcess, GetProcessTimes shape, GetProcessId, priority classes,
 * GetProcessVersion, QueryFullProcessImageNameW, affinity, handle counts,
 * and what each call does with a handle that lacks the access it needs.
 *
 * The test re-runs its own .exe as the child ("exit N"). PIDs, times and
 * paths are never printed: only comparisons and suffixes.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-openprocess
 *       https://learn.microsoft.com/en-us/windows/win32/procthread/process-security-and-access-rights
 *       https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getexitcodeprocess
 *       https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getprocesstimes
 *       https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getprocessid
 *       https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getprocessidofthread
 *       https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getpriorityclass
 *       https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-setpriorityclass
 *       https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getprocessversion
 *       https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-queryfullprocessimagenamew
 *       https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-getprocessaffinitymask
 *       https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getprocesshandlecount
 *       https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-terminateprocess
 *       https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-getprocessworkingsetsize
 */
#include "oracle.h"

static char self[MAX_PATH];

static BOOL spawn(const char *args, DWORD flags, PROCESS_INFORMATION *pi)
{
    char cmd[MAX_PATH + 64];
    STARTUPINFOA si = {.cb = sizeof si};
    snprintf(cmd, sizeof cmd, "\"%s\" %s", self, args);
    return CreateProcessA(NULL, cmd, NULL, NULL, FALSE, flags, NULL, NULL, &si, pi);
}

#define BOOL_CASE(name, expr)     \
    do {                          \
        BOOL ok_;                 \
        ORACLE_CALL(ok_, (expr)); \
        oracle_case(name);        \
        o_bool("ret", ok_);       \
        o_lasterr();              \
        oracle_case_end();        \
    } while (0)

static void open_case(const char *name, DWORD access, DWORD pid)
{
    HANDLE h;
    ORACLE_CALL(h, OpenProcess(access, FALSE, pid));
    oracle_case(name);
    o_handle("ret", h);
    o_lasterr();
    oracle_case_end();
    if (h) CloseHandle(h);
}

static void exit_code_case(const char *name, HANDLE h)
{
    DWORD code = 0xaaaaaaaa;
    BOOL ok;
    ORACLE_CALL(ok, GetExitCodeProcess(h, &code));
    oracle_case(name);
    o_bool("ret", ok);
    o_lasterr();
    o_u32("code", code);
    oracle_case_end();
}

/* The harness names the .exe differently on each target, so compare with
 * the base name GetModuleFileNameW reports. */
static int ends_with_base(const wchar_t *name, DWORD len)
{
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(NULL, self, MAX_PATH);
    const wchar_t *base = wcsrchr(self, L'\\');
    size_t bl = base ? wcslen(base) : 0;
    return base && len > bl && !_wcsicmp(name + len - bl, base);
}

static ULONGLONG ft(FILETIME f) { return ((ULONGLONG)f.dwHighDateTime << 32) | f.dwLowDateTime; }

int main(int argc, char **argv)
{
    if (argc > 2 && !strcmp(argv[1], "exit")) return atoi(argv[2]);
    oracle_begin("kernel32/process_info");
    GetModuleFileNameA(NULL, self, MAX_PATH);
    DWORD me = GetCurrentProcessId();
    BOOL ok;

    /* ---- ids ---- */
    {
        DWORD id;
        ORACLE_CALL(id, GetProcessId(GetCurrentProcess()));
        oracle_case("process_id_pseudo");
        o_bool("matches_current", id == me);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(id, GetProcessIdOfThread(GetCurrentThread()));
        oracle_case("process_id_of_current_thread");
        o_bool("matches_current", id == me);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(id, GetProcessId(NULL));
        oracle_case("process_id_null");
        o_u32("ret", id);
        o_lasterr();
        oracle_case_end();
        HANDLE ev = CreateEventW(NULL, FALSE, FALSE, NULL);
        ORACLE_CALL(id, GetProcessId(ev));
        oracle_case("process_id_event_handle");
        o_u32("ret", id);
        o_lasterr();
        oracle_case_end();
        exit_code_case("exit_code_event_handle", ev);
        CloseHandle(ev);
    }

    /* ---- OpenProcess ---- */
    open_case("open_self_all_access", PROCESS_ALL_ACCESS, me);
    open_case("open_self_zero_access", 0, me);
    open_case("open_self_query_limited", PROCESS_QUERY_LIMITED_INFORMATION, me);
    open_case("open_pid_zero", PROCESS_QUERY_LIMITED_INFORMATION, 0);
    open_case("open_pid_bogus", PROCESS_QUERY_LIMITED_INFORMATION, 0xfffffff0);
    open_case("open_pid_odd", PROCESS_QUERY_LIMITED_INFORMATION, me + 1);
    open_case("open_self_bad_access_bits", 0x00800000 | PROCESS_QUERY_INFORMATION, me);
    {
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, TRUE, me);
        DWORD flags = 0;
        GetHandleInformation(h, &flags);
        oracle_case("open_inheritable");
        o_bool("inherit_flag", (flags & HANDLE_FLAG_INHERIT) != 0);
        oracle_case_end();
        CloseHandle(h);
    }

    /* ---- self ---- */
    exit_code_case("exit_code_self_pseudo", GetCurrentProcess());
    {
        FILETIME c, e, k, u, now;
        memset(&e, 0xcc, sizeof e);
        GetSystemTimeAsFileTime(&now);
        ORACLE_CALL(ok, GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u));
        oracle_case("times_self");
        o_bool("ret", ok);
        o_lasterr();
        o_bool("creation_before_now", ft(c) <= ft(now) + 10000000);
        o_bool("creation_recent", ft(now) - ft(c) < 600ull * 10000000);
        o_bool("exit_zero", ft(e) == 0);
        o_bool("kernel_lt_1h", ft(k) < 3600ull * 10000000);
        o_bool("user_lt_1h", ft(u) < 3600ull * 10000000);
        oracle_case_end();
    }
    {
        DWORD v;
        ORACLE_CALL(v, GetProcessVersion(0));
        oracle_case("process_version_self_zero");
        o_hex32("ret", v);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(v, GetProcessVersion(me));
        oracle_case("process_version_self_pid");
        o_hex32("ret", v);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(v, GetProcessVersion(0xfffffff0));
        oracle_case("process_version_bogus_pid");
        o_hex32("ret", v);
        o_lasterr();
        oracle_case_end();
    }
    {
        DWORD c;
        ORACLE_CALL(c, GetPriorityClass(GetCurrentProcess()));
        oracle_case("priority_class_default");
        o_hex32("class", c);
        o_lasterr();
        oracle_case_end();
        BOOL_CASE("set_priority_below_normal", SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS));
        oracle_case("priority_class_after_set");
        o_hex32("class", GetPriorityClass(GetCurrentProcess()));
        oracle_case_end();
        BOOL_CASE("set_priority_invalid", SetPriorityClass(GetCurrentProcess(), 0x12345));
        BOOL_CASE("set_priority_two_classes", SetPriorityClass(GetCurrentProcess(),
                                                               NORMAL_PRIORITY_CLASS | IDLE_PRIORITY_CLASS));
        oracle_case("priority_class_after_invalid");
        o_hex32("class", GetPriorityClass(GetCurrentProcess()));
        oracle_case_end();
        SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);
    }
    {
        wchar_t name[MAX_PATH];
        DWORD len = MAX_PATH;
        ORACLE_CALL(ok, QueryFullProcessImageNameW(GetCurrentProcess(), 0, name, &len));
        oracle_case("image_name_win32");
        o_bool("ret", ok);
        o_lasterr();
        o_bool("len_matches", len == wcslen(name));
        o_bool("suffix", ends_with_base(name, len));
        o_bool("drive_letter", name[1] == L':' && name[2] == L'\\');
        oracle_case_end();
        len = MAX_PATH;
        ORACLE_CALL(ok, QueryFullProcessImageNameW(GetCurrentProcess(), PROCESS_NAME_NATIVE, name, &len));
        oracle_case("image_name_native");
        o_bool("ret", ok);
        o_lasterr();
        o_bool("len_matches", len == wcslen(name));
        o_bool("device_prefix", !wcsncmp(name, L"\\Device\\", 8));
        o_bool("suffix", ends_with_base(name, len));
        oracle_case_end();
        len = 5;
        ORACLE_CALL(ok, QueryFullProcessImageNameW(GetCurrentProcess(), 0, name, &len));
        oracle_case("image_name_small_buffer");
        o_bool("ret", ok);
        o_lasterr();
        o_u32("len_after", len);
        oracle_case_end();
        len = MAX_PATH;
        ORACLE_CALL(ok, QueryFullProcessImageNameW(GetCurrentProcess(), 2, name, &len));
        oracle_case("image_name_bad_flags");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
    }
    {
        DWORD_PTR pm = 0, sm = 0;
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        ORACLE_CALL(ok, GetProcessAffinityMask(GetCurrentProcess(), &pm, &sm));
        oracle_case("affinity_mask");
        o_bool("ret", ok);
        o_lasterr();
        o_bool("process_eq_system", pm == sm);
        o_bool("system_eq_active_mask", sm == si.dwActiveProcessorMask);
        oracle_case_end();
    }
    {
        DWORD n = 0;
        ORACLE_CALL(ok, GetProcessHandleCount(GetCurrentProcess(), &n));
        oracle_case("handle_count");
        o_bool("ret", ok);
        o_lasterr();
        o_bool("positive", n > 0);
        oracle_case_end();
        HANDLE evs[10];
        for (int i = 0; i < 10; i++) evs[i] = CreateEventW(NULL, FALSE, FALSE, NULL);
        DWORD n2 = 0;
        GetProcessHandleCount(GetCurrentProcess(), &n2);
        for (int i = 0; i < 10; i++) CloseHandle(evs[i]);
        DWORD n3 = 0;
        GetProcessHandleCount(GetCurrentProcess(), &n3);
        oracle_case("handle_count_tracks_handles");
        o_i64("delta_after_10", (long long)n2 - (long long)n);
        o_i64("delta_after_close", (long long)n3 - (long long)n);
        oracle_case_end();
    }
    {
        SIZE_T mn = 0, mx = 0;
        ORACLE_CALL(ok, GetProcessWorkingSetSize(GetCurrentProcess(), &mn, &mx));
        oracle_case("working_set_size");
        o_bool("ret", ok);
        o_lasterr();
        o_bool("min_lt_max", mn < mx);
        o_bool("min_nonzero", mn > 0);
        oracle_case_end();
    }

    /* ---- a suspended child ---- */
    {
        PROCESS_INFORMATION pi;
        spawn("exit 7", CREATE_SUSPENDED, &pi);
        exit_code_case("child_suspended_exit_code", pi.hProcess);
        oracle_case("child_ids");
        o_bool("pid_matches", GetProcessId(pi.hProcess) == pi.dwProcessId);
        o_bool("pid_of_thread", GetProcessIdOfThread(pi.hThread) == pi.dwProcessId);
        o_bool("tid_matches", GetThreadId(pi.hThread) == pi.dwThreadId);
        o_bool("pid_differs", pi.dwProcessId != me);
        oracle_case_end();
        {
            DWORD v;
            ORACLE_CALL(v, GetProcessVersion(pi.dwProcessId));
            oracle_case("process_version_child");
            o_hex32("ret", v);
            o_lasterr();
            oracle_case_end();
        }

        /* Limited handles. */
        HANDLE sync = OpenProcess(SYNCHRONIZE, FALSE, pi.dwProcessId);
        HANDLE lim = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pi.dwProcessId);
        oracle_case("child_open_limited");
        o_handle("sync", sync);
        o_handle("query_limited", lim);
        oracle_case_end();
        exit_code_case("exit_code_sync_only_handle", sync);
        exit_code_case("exit_code_query_limited_handle", lim);
        {
            DWORD id;
            ORACLE_CALL(id, GetProcessId(sync));
            oracle_case("process_id_sync_only_handle");
            o_bool("nonzero", id != 0);
            o_lasterr();
            oracle_case_end();
            FILETIME c, e, k, u;
            ORACLE_CALL(ok, GetProcessTimes(sync, &c, &e, &k, &u));
            oracle_case("times_sync_only_handle");
            o_bool("ret", ok);
            o_lasterr();
            oracle_case_end();
            ORACLE_CALL(ok, GetProcessTimes(lim, &c, &e, &k, &u));
            oracle_case("times_query_limited_handle");
            o_bool("ret", ok);
            o_lasterr();
            oracle_case_end();
            DWORD cls;
            ORACLE_CALL(cls, GetPriorityClass(sync));
            oracle_case("priority_class_sync_only_handle");
            o_hex32("ret", cls);
            o_lasterr();
            oracle_case_end();
            char buf[4];
            SIZE_T got = 77;
            ORACLE_CALL(ok, ReadProcessMemory(lim, (void *)0x10000, buf, 4, &got));
            oracle_case("read_memory_query_limited_handle");
            o_bool("ret", ok);
            o_lasterr();
            o_u64("got", got);
            oracle_case_end();
        }
        BOOL_CASE("terminate_query_limited_handle", TerminateProcess(lim, 3));
        BOOL_CASE("terminate_sync_only_handle", TerminateProcess(sync, 3));
        DWORD w;
        ORACLE_CALL(w, WaitForSingleObject(lim, 0));
        oracle_case("wait_query_limited_handle");
        o_hex32("ret", w);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(w, WaitForSingleObject(sync, 0));
        oracle_case("wait_sync_only_handle_running");
        o_hex32("ret", w);
        o_lasterr();
        oracle_case_end();

        ResumeThread(pi.hThread);
        WaitForSingleObject(pi.hProcess, INFINITE);
        exit_code_case("child_exit_code", pi.hProcess);
        exit_code_case("child_exit_code_query_limited", lim);
        {
            FILETIME c, e, k, u;
            ok = GetProcessTimes(pi.hProcess, &c, &e, &k, &u);
            oracle_case("times_exited_child");
            o_bool("ret", ok);
            o_bool("exit_ge_creation", ft(e) >= ft(c));
            o_bool("exit_nonzero", ft(e) != 0);
            oracle_case_end();
        }
        open_case("open_exited_child_while_handle_held", PROCESS_QUERY_LIMITED_INFORMATION, pi.dwProcessId);
        BOOL_CASE("terminate_exited_child", TerminateProcess(pi.hProcess, 9));
        exit_code_case("child_exit_code_after_terminate", pi.hProcess);
        DWORD pid = pi.dwProcessId;
        CloseHandle(sync);
        CloseHandle(lim);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        open_case("open_exited_child_after_all_closed", PROCESS_QUERY_LIMITED_INFORMATION, pid);
    }

    /* ---- terminated child ---- */
    {
        PROCESS_INFORMATION pi;
        spawn("exit 7", CREATE_SUSPENDED, &pi);
        BOOL_CASE("terminate_suspended_child", TerminateProcess(pi.hProcess, 0xC0000005));
        WaitForSingleObject(pi.hProcess, INFINITE);
        exit_code_case("terminated_child_exit_code", pi.hProcess);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    {
        PROCESS_INFORMATION pi;
        spawn("exit -1", 0, &pi);
        WaitForSingleObject(pi.hProcess, INFINITE);
        exit_code_case("child_exit_minus_1", pi.hProcess);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }

    /* ---- thread handle where a process handle is expected ---- */
    exit_code_case("exit_code_thread_handle", GetCurrentThread());
    {
        HANDLE t = OpenThread(THREAD_ALL_ACCESS, FALSE, GetCurrentThreadId());
        exit_code_case("exit_code_real_thread_handle", t);
        CloseHandle(t);
    }

    return oracle_end();
}
