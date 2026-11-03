/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: Toolhelp32 snapshots of the test's own process:
 * CreateToolhelp32Snapshot with the process, thread and module flags,
 * Process32FirstW/NextW, Thread32First/Next, Module32FirstW/NextW,
 * Toolhelp32ReadProcessMemory, dwSize validation and end-of-list errors.
 *
 * Only relationships are printed (this process is found, its thread count
 * matches the thread snapshot, the first module is the exe, module bases
 * match GetModuleHandle), never PIDs, TIDs or addresses.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/tlhelp32/nf-tlhelp32-createtoolhelp32snapshot
 *       https://learn.microsoft.com/en-us/windows/win32/api/tlhelp32/nf-tlhelp32-process32firstw
 *       https://learn.microsoft.com/en-us/windows/win32/api/tlhelp32/nf-tlhelp32-thread32first
 *       https://learn.microsoft.com/en-us/windows/win32/api/tlhelp32/nf-tlhelp32-module32firstw
 *       https://learn.microsoft.com/en-us/windows/win32/api/tlhelp32/nf-tlhelp32-toolhelp32readprocessmemory
 *       https://learn.microsoft.com/en-us/windows/win32/toolhelp/taking-a-snapshot-and-viewing-processes
 */
#include "oracle.h"
#include <tlhelp32.h>

static HANDLE ready, stop;

static DWORD WINAPI worker(void *arg)
{
    (void)arg;
    SetEvent(ready);
    WaitForSingleObject(stop, INFINITE);
    return 0;
}

int main(void)
{
    oracle_begin("kernel32/toolhelp");

    DWORD me = GetCurrentProcessId(), my_tid = GetCurrentThreadId();
    ready = CreateEventW(NULL, FALSE, FALSE, NULL);
    stop = CreateEventW(NULL, TRUE, FALSE, NULL);
    HANDLE th[2];
    DWORD tids[2];
    for (int i = 0; i < 2; i++) {
        th[i] = CreateThread(NULL, 0, worker, NULL, 0, &tids[i]);
        WaitForSingleObject(ready, 10000);
    }

    HANDLE snap;
    ORACLE_CALL(snap, CreateToolhelp32Snapshot(TH32CS_SNAPALL, 0));
    oracle_case("snapshot_all");
    o_handle("ret", snap);
    o_lasterr();
    oracle_case_end();

    /* Processes. */
    {
        PROCESSENTRY32W pe;
        BOOL r;
        int n = 0, found = 0, has_pid0 = 0, has_pid4 = 0;
        DWORD my_threads = 0;
        wchar_t my_exe[MAX_PATH] = L"", pid0_name[MAX_PATH] = L"", pid4_name[MAX_PATH] = L"";
        pe.dwSize = sizeof pe;
        ORACLE_CALL(r, Process32FirstW(snap, &pe));
        oracle_case("process_first");
        o_bool("ret", r);
        o_lasterr();
        o_u32("dwSize_kept", pe.dwSize == sizeof pe);
        oracle_case_end();
        while (r) {
            n++;
            if (pe.th32ProcessID == me) {
                found++;
                my_threads = pe.cntThreads;
                wcscpy(my_exe, pe.szExeFile);
            }
            if (pe.th32ProcessID == 0) { has_pid0 = 1; wcscpy(pid0_name, pe.szExeFile); }
            if (pe.th32ProcessID == 4) { has_pid4 = 1; wcscpy(pid4_name, pe.szExeFile); }
            pe.dwSize = sizeof pe;
            SetLastError(ORACLE_SENTINEL);
            r = Process32NextW(snap, &pe);
        }
        oracle_last_error = GetLastError();
        oracle_case("process_walk");
        o_bool("several", n > 2);
        o_i64("self_found", found);
        {
            /* The exe's file name depends on how the harness names it. */
            wchar_t path[MAX_PATH], *base;
            GetModuleFileNameW(NULL, path, MAX_PATH);
            base = wcsrchr(path, L'\\') ? wcsrchr(path, L'\\') + 1 : path;
            o_bool("self_exe_is_file_name", !_wcsicmp(my_exe, base));
        }
        o_bool("has_pid0", has_pid0);
        o_wstr("pid0_name", pid0_name);
        o_bool("has_pid4", has_pid4);
        o_wstr("pid4_name", pid4_name);
        o_lasterr();
        oracle_case_end();

        /* Thread counts agree. */
        THREADENTRY32 te;
        DWORD mine = 0;
        int saw_main = 0, saw_workers = 0, owner_ok = 1;
        te.dwSize = sizeof te;
        r = Thread32First(snap, &te);
        while (r) {
            if (te.th32OwnerProcessID == me) {
                mine++;
                if (te.th32ThreadID == my_tid) saw_main = 1;
                if (te.th32ThreadID == tids[0] || te.th32ThreadID == tids[1]) saw_workers++;
            } else if (te.th32ThreadID == my_tid) owner_ok = 0;
            te.dwSize = sizeof te;
            r = Thread32Next(snap, &te);
        }
        oracle_case("thread_walk");
        o_bool("count_matches_process_entry", mine == my_threads);
        o_bool("at_least_three", mine >= 3);
        o_bool("saw_main", saw_main);
        o_i64("saw_workers", saw_workers);
        o_bool("owner_ok", owner_ok);
        oracle_case_end();

        r = Process32NextW(snap, &pe);
        oracle_case("process_next_after_end");
        o_bool("ret", r);
        oracle_case_end();

        pe.dwSize = sizeof pe - 4;
        ORACLE_CALL(r, Process32FirstW(snap, &pe));
        oracle_case("process_first_bad_size");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        pe.dwSize = sizeof pe + 64;
        ORACLE_CALL(r, Process32FirstW(snap, &pe));
        oracle_case("process_first_bigger_size");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
    }

    /* Modules. */
    {
        MODULEENTRY32W me32;
        BOOL r;
        HMODULE exe = GetModuleHandleW(NULL);
        HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
        HMODULE nt = GetModuleHandleW(L"ntdll.dll");
        me32.dwSize = sizeof me32;
        ORACLE_CALL(r, Module32FirstW(snap, &me32));
        oracle_case("module_first");
        o_bool("ret", r);
        o_lasterr();
        if (r) {
            IMAGE_NT_HEADERS *nh = (IMAGE_NT_HEADERS *)((BYTE *)exe + ((IMAGE_DOS_HEADER *)exe)->e_lfanew);
            {
                wchar_t path[MAX_PATH], *base;
                GetModuleFileNameW(NULL, path, MAX_PATH);
                base = wcsrchr(path, L'\\') ? wcsrchr(path, L'\\') + 1 : path;
                o_bool("module_is_file_name", !_wcsicmp(me32.szModule, base));
            }
            o_bool("is_exe", me32.hModule == exe);
            o_bool("base_is_handle", me32.modBaseAddr == (BYTE *)me32.hModule);
            o_bool("size_is_image_size", me32.modBaseSize == nh->OptionalHeader.SizeOfImage);
            o_bool("pid_is_self", me32.th32ProcessID == me);
            o_u32("module_id", me32.th32ModuleID);
            o_u32("glbl_cnt", me32.GlblcntUsage);
            o_u32("proc_cnt", me32.ProccntUsage);
            wchar_t path[MAX_PATH];
            GetModuleFileNameW(NULL, path, MAX_PATH);
            o_bool("path_matches", !wcscmp(path, me32.szExePath));
        }
        oracle_case_end();
        int n = 0, k32_ok = 0, nt_ok = 0, all_match = 1;
        while (r) {
            n++;
            if (!_wcsicmp(me32.szModule, L"kernel32.dll")) k32_ok = me32.hModule == k32;
            if (!_wcsicmp(me32.szModule, L"ntdll.dll")) nt_ok = me32.hModule == nt;
            if (GetModuleHandleW(me32.szExePath) != me32.hModule) all_match = 0;
            me32.dwSize = sizeof me32;
            SetLastError(ORACLE_SENTINEL);
            r = Module32NextW(snap, &me32);
        }
        oracle_last_error = GetLastError();
        oracle_case("module_walk");
        o_bool("several", n >= 3);
        o_bool("kernel32_ok", k32_ok);
        o_bool("ntdll_ok", nt_ok);
        o_bool("all_match_getmodulehandle", all_match);
        o_lasterr();
        oracle_case_end();
        me32.dwSize = 8;
        ORACLE_CALL(r, Module32FirstW(snap, &me32));
        oracle_case("module_first_bad_size");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
    }
    CloseHandle(snap);

    /* Flag-specific snapshots. */
    {
        HANDLE s;
        PROCESSENTRY32W pe;
        MODULEENTRY32W m;
        THREADENTRY32 t;
        memset(&pe, 0, sizeof pe);
        memset(&m, 0, sizeof m);
        memset(&t, 0, sizeof t);
        pe.dwSize = sizeof pe;
        m.dwSize = sizeof m;
        t.dwSize = sizeof t;
        BOOL r1, r2, r3;
        s = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, 0);
        r1 = Process32FirstW(s, &pe);
        DWORD e1 = GetLastError();
        r2 = Module32FirstW(s, &m);
        r3 = Thread32First(s, &t);
        DWORD e3 = GetLastError();
        oracle_case("modules_only_snapshot");
        o_bool("process_first", r1);
        o_u32("process_error", e1);
        o_bool("module_first", r2);
        o_bool("thread_first", r3);
        o_u32("thread_error", e3);
        oracle_case_end();
        CloseHandle(s);

        s = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE32, 0);
        m.dwSize = sizeof m;
        SetLastError(ORACLE_SENTINEL);
        r2 = Module32FirstW(s, &m);
        oracle_last_error = GetLastError();
        oracle_case("module32_only_in_64bit_process");
        o_handle("snap", s);
        o_bool("module_first", r2);
        o_lasterr();
        oracle_case_end();
        if (s != INVALID_HANDLE_VALUE) CloseHandle(s);

        s = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, me);
        m.dwSize = sizeof m;
        r2 = Module32FirstW(s, &m);
        oracle_case("modules_by_explicit_pid");
        o_handle("snap", s);
        o_bool("first_is_exe", r2 && m.hModule == GetModuleHandleW(NULL));
        oracle_case_end();
        if (s != INVALID_HANDLE_VALUE) CloseHandle(s);

        ORACLE_CALL(s, CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, 0xfffffff0));
        oracle_case("modules_bogus_pid");
        o_handle("snap", s);
        o_lasterr();
        oracle_case_end();
        if (s != INVALID_HANDLE_VALUE) CloseHandle(s);

        ORACLE_CALL(s, CreateToolhelp32Snapshot(0, 0));
        oracle_case("no_flags");
        o_handle("snap", s);
        o_lasterr();
        if (s != INVALID_HANDLE_VALUE) {
            pe.dwSize = sizeof pe;
            o_bool("process_first", Process32FirstW(s, &pe));
            CloseHandle(s);
        }
        oracle_case_end();
    }

    /* Toolhelp32ReadProcessMemory on our own memory. */
    {
        static const char secret[] = "toolhelp-read";
        char out[32] = { 0 };
        SIZE_T got = 0;
        BOOL r;
        ORACLE_CALL(r, Toolhelp32ReadProcessMemory(0, secret, out, sizeof secret, &got));
        oracle_case("read_own_memory_pid0");
        o_bool("ret", r);
        o_lasterr();
        o_u64("got", got);
        o_str("data", out);
        oracle_case_end();
        memset(out, 0, sizeof out);
        ORACLE_CALL(r, Toolhelp32ReadProcessMemory(me, secret, out, sizeof secret, &got));
        oracle_case("read_own_memory_pid_self");
        o_bool("ret", r);
        o_lasterr();
        o_str("data", out);
        oracle_case_end();
        ORACLE_CALL(r, Toolhelp32ReadProcessMemory(me, (void *)(ULONG_PTR)0x10, out, 4, &got));
        oracle_case("read_unmapped");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
    }

    SetEvent(stop);
    WaitForMultipleObjects(2, th, TRUE, 10000);
    return oracle_end();
}
