/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: handles across processes: DuplicateHandle into and out of a
 * child, DUPLICATE_CLOSE_SOURCE in another process, the pseudo handles of
 * another process, PROCESS_DUP_HANDLE access, inheritance with bInheritHandles
 * and with PROC_THREAD_ATTRIBUTE_HANDLE_LIST, and the inherit flag the child
 * sees.
 *
 * The child is this .exe. It checks the handle values given on its command
 * line (or in h.txt, for handles duplicated after it started) and reports
 * through its exit code: per handle 4 bits, 1 = valid, 2 = inherit flag set,
 * 4 = it is an event the child could set. Handle values are never printed.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/handleapi/nf-handleapi-duplicatehandle
 *       https://learn.microsoft.com/en-us/windows/win32/procthread/inheritance
 *       https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-updateprocthreadattribute
 *       https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-initializeprocthreadattributelist
 *       https://learn.microsoft.com/en-us/windows/win32/api/handleapi/nf-handleapi-gethandleinformation
 *       https://learn.microsoft.com/en-us/windows/win32/procthread/process-security-and-access-rights
 */
#include "oracle.h"

static char self[MAX_PATH];

typedef BOOL(WINAPI *coh_fn)(HANDLE, HANDLE);

static int check_handle(HANDLE h)
{
    DWORD flags;
    int r = 0;
    if (!GetHandleInformation(h, &flags)) return 0;
    r |= 1;
    if (flags & HANDLE_FLAG_INHERIT) r |= 2;
    if (SetEvent(h)) r |= 4;
    return r;
}

/* A handle value that should not exist in the child can still be valid there
 * by chance (pointing at an unrelated object), so a value counts only when it
 * refers to the same object as the parent's named event. */
static int check_named(const char *ppid, const char *spec)
{
    char name[64];
    unsigned idx = 0;
    unsigned long long v = 0;
    if (sscanf(spec, "%u:%llx", &idx, &v) != 2) return 0xf;
    snprintf(name, sizeof name, "setu_hi_%s_%u", ppid, idx);
    HANDLE named = OpenEventA(SYNCHRONIZE, FALSE, name);
    coh_fn coh = (coh_fn)(void *)GetProcAddress(GetModuleHandleW(L"kernelbase.dll"), "CompareObjectHandles");
    HANDLE h = (HANDLE)(ULONG_PTR)v;
    int same = named && coh && coh(h, named);
    if (named) CloseHandle(named);
    return same ? check_handle(h) : 0;
}

static int child_check(int argc, char **argv)
{
    int code = 0;
    for (int i = 3; i < argc && i < 9; i++) code |= check_named(argv[2], argv[i]) << (4 * (i - 3));
    return code;
}

static int child_file(void)
{
    char buf[64] = "";
    FILE *f = fopen("h.txt", "rb");
    if (!f) return 0xff;
    fgets(buf, sizeof buf, f);
    fclose(f);
    return check_handle((HANDLE)(ULONG_PTR)strtoull(buf, NULL, 16));
}

/* ---- parent helpers ---- */

static BOOL spawn(const char *args, BOOL inherit, DWORD flags, STARTUPINFOA *si_in, PROCESS_INFORMATION *pi)
{
    char cmd[MAX_PATH + 128];
    STARTUPINFOA si = {.cb = sizeof si};
    snprintf(cmd, sizeof cmd, "\"%s\" %s", self, args);
    return CreateProcessA(NULL, cmd, NULL, NULL, inherit, flags, NULL, NULL, si_in ? si_in : &si, pi);
}

static DWORD finish(PROCESS_INFORMATION *pi)
{
    DWORD code = 0xffffffff;
    WaitForSingleObject(pi->hProcess, INFINITE);
    GetExitCodeProcess(pi->hProcess, &code);
    CloseHandle(pi->hProcess);
    CloseHandle(pi->hThread);
    return code;
}

static void code_case(const char *name, BOOL spawned, DWORD code, HANDLE ev1, HANDLE ev2)
{
    oracle_case(name);
    o_bool("spawned", spawned);
    if (spawned) o_hex32("child", code);
    if (ev1) o_bool("ev1_set", WaitForSingleObject(ev1, 0) == WAIT_OBJECT_0);
    if (ev2) o_bool("ev2_set", WaitForSingleObject(ev2, 0) == WAIT_OBJECT_0);
    oracle_case_end();
    if (ev1) ResetEvent(ev1);
    if (ev2) ResetEvent(ev2);
}

static void dup_case(const char *name, BOOL ok)
{
    oracle_case(name);
    o_bool("ret", ok);
    o_lasterr();
    oracle_case_end();
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "check")) return child_check(argc, argv);
    if (argc > 1 && !strcmp(argv[1], "file")) return child_file();
    if (argc > 1 && !strcmp(argv[1], "wait")) {
        Sleep(INFINITE);
        return 0;
    }
    oracle_begin("kernel32/handle_inheritance");
    GetModuleFileNameA(NULL, self, MAX_PATH);
    SECURITY_ATTRIBUTES inh = {sizeof inh, NULL, TRUE};
    char nm[64];
    DWORD pid = GetCurrentProcessId();
    snprintf(nm, sizeof nm, "setu_hi_%lu_1", pid);
    HANDLE ev_inh = CreateEventA(&inh, TRUE, FALSE, nm);
    snprintf(nm, sizeof nm, "setu_hi_%lu_2", pid);
    HANDLE ev_plain = CreateEventA(NULL, TRUE, FALSE, nm);
    snprintf(nm, sizeof nm, "setu_hi_%lu_3", pid);
    HANDLE ev_inh2 = CreateEventA(&inh, TRUE, FALSE, nm);
    char args[128];
    PROCESS_INFORMATION pi;
    BOOL ok;

    /* ---- plain inheritance ---- */
    snprintf(args, sizeof args, "check %lu 1:%p 2:%p", pid, ev_inh, ev_plain);
    ok = spawn(args, TRUE, 0, NULL, &pi);
    code_case("inherit_true", ok, ok ? finish(&pi) : 0, ev_inh, ev_plain);
    ok = spawn(args, FALSE, 0, NULL, &pi);
    code_case("inherit_false", ok, ok ? finish(&pi) : 0, ev_inh, ev_plain);

    /* ---- handle list attribute ---- */
    {
        SIZE_T size = 0;
        ORACLE_CALL(ok, InitializeProcThreadAttributeList(NULL, 1, 0, &size));
        oracle_case("attr_list_size_query");
        o_bool("ret", ok);
        o_lasterr();
        o_bool("size_nonzero", size > 0);
        oracle_case_end();
        LPPROC_THREAD_ATTRIBUTE_LIST list = HeapAlloc(GetProcessHeap(), 0, size);
        InitializeProcThreadAttributeList(list, 1, 0, &size);
        HANDLE only[1] = {ev_inh2};
        ORACLE_CALL(ok, UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, only, sizeof only, NULL, NULL));
        dup_case("attr_handle_list_update", ok);
        STARTUPINFOEXA six;
        memset(&six, 0, sizeof six);
        six.StartupInfo.cb = sizeof six;
        six.lpAttributeList = list;
        snprintf(args, sizeof args, "check %lu 1:%p 3:%p", pid, ev_inh, ev_inh2);
        ok = spawn(args, TRUE, EXTENDED_STARTUPINFO_PRESENT, &six.StartupInfo, &pi);
        code_case("handle_list_only_listed_inherited", ok, ok ? finish(&pi) : 0, ev_inh, ev_inh2);
        ok = spawn(args, FALSE, EXTENDED_STARTUPINFO_PRESENT, &six.StartupInfo, &pi);
        DWORD err = GetLastError();
        oracle_case("handle_list_with_inherit_false");
        o_bool("spawned", ok);
        if (!ok) o_u32("error", err);
        oracle_case_end();
        if (ok) finish(&pi);
        DeleteProcThreadAttributeList(list);

        /* A non-inheritable handle in the list. */
        InitializeProcThreadAttributeList(list, 1, 0, &size);
        HANDLE bad[1] = {ev_plain};
        UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, bad, sizeof bad, NULL, NULL);
        six.lpAttributeList = list;
        snprintf(args, sizeof args, "check %lu 2:%p", pid, ev_plain);
        ok = spawn(args, TRUE, EXTENDED_STARTUPINFO_PRESENT, &six.StartupInfo, &pi);
        err = GetLastError();
        oracle_case("handle_list_non_inheritable");
        o_bool("spawned", ok);
        if (!ok) o_u32("error", err);
        if (ok) o_hex32("child", finish(&pi));
        oracle_case_end();
        ResetEvent(ev_plain);
        DeleteProcThreadAttributeList(list);

        InitializeProcThreadAttributeList(list, 1, 0, &size);
        ORACLE_CALL(ok, UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, only, 3, NULL, NULL));
        dup_case("attr_handle_list_odd_size", ok);
        ORACLE_CALL(ok, UpdateProcThreadAttribute(list, 0, 0x7777, only, sizeof only, NULL, NULL));
        dup_case("attr_unknown_attribute", ok);
        ORACLE_CALL(ok, UpdateProcThreadAttribute(list, 1, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, only, sizeof only, NULL, NULL));
        dup_case("attr_bad_flags", ok);
        DeleteProcThreadAttributeList(list);
        HeapFree(GetProcessHeap(), 0, list);
    }

    /* ---- duplicate into a running child ---- */
    {
        ok = spawn("file", FALSE, CREATE_SUSPENDED, NULL, &pi);
        HANDLE in_child = NULL;
        ORACLE_CALL(ok, DuplicateHandle(GetCurrentProcess(), ev_plain, pi.hProcess, &in_child, 0, TRUE,
                                        DUPLICATE_SAME_ACCESS));
        dup_case("dup_into_child", ok);
        FILE *f = fopen("h.txt", "wb");
        fprintf(f, "%p\n", in_child);
        fclose(f);
        ResumeThread(pi.hThread);
        code_case("dup_into_child_used", TRUE, finish(&pi), ev_plain, NULL);
        DeleteFileA("h.txt");
    }

    /* ---- duplicate out of a child; close in a child ---- */
    {
        spawn("wait", FALSE, CREATE_SUSPENDED, NULL, &pi);
        HANDLE out = NULL;
        ORACLE_CALL(ok, DuplicateHandle(pi.hProcess, GetCurrentProcess(), GetCurrentProcess(), &out, 0, FALSE,
                                        DUPLICATE_SAME_ACCESS));
        dup_case("dup_child_pseudo_process_handle", ok);
        oracle_case("child_pseudo_handle_is_child");
        o_bool("pid_is_child", ok && GetProcessId(out) == pi.dwProcessId);
        oracle_case_end();
        if (ok) CloseHandle(out);

        HANDLE in_child = NULL;
        DuplicateHandle(GetCurrentProcess(), ev_plain, pi.hProcess, &in_child, 0, FALSE, DUPLICATE_SAME_ACCESS);
        ORACLE_CALL(ok, DuplicateHandle(pi.hProcess, in_child, GetCurrentProcess(), &out, 0, FALSE, DUPLICATE_SAME_ACCESS));
        dup_case("dup_back_from_child", ok);
        oracle_case("dup_back_is_same_event");
        {
            typedef BOOL(WINAPI * coh_fn)(HANDLE, HANDLE);
            coh_fn coh = (coh_fn)(void *)GetProcAddress(GetModuleHandleW(L"kernelbase.dll"), "CompareObjectHandles");
            o_bool("same", ok && coh && coh(out, ev_plain));
        }
        oracle_case_end();
        if (ok) CloseHandle(out);
        ORACLE_CALL(ok, DuplicateHandle(pi.hProcess, in_child, NULL, NULL, 0, FALSE, DUPLICATE_CLOSE_SOURCE));
        dup_case("close_in_child", ok);
        ORACLE_CALL(ok, DuplicateHandle(pi.hProcess, in_child, GetCurrentProcess(), &out, 0, FALSE, DUPLICATE_SAME_ACCESS));
        dup_case("dup_closed_child_handle", ok);
        ORACLE_CALL(ok, DuplicateHandle(pi.hProcess, in_child, NULL, NULL, 0, FALSE, DUPLICATE_CLOSE_SOURCE));
        dup_case("close_in_child_twice", ok);
        ORACLE_CALL(ok, DuplicateHandle(GetCurrentProcess(), ev_plain, pi.hProcess, NULL, 0, FALSE, DUPLICATE_SAME_ACCESS));
        dup_case("dup_into_child_null_target_ptr", ok);

        HANDLE nodup = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE, FALSE, pi.dwProcessId);
        ORACLE_CALL(ok, DuplicateHandle(GetCurrentProcess(), ev_plain, nodup, &out, 0, FALSE, DUPLICATE_SAME_ACCESS));
        dup_case("dup_into_without_dup_access", ok);
        ORACLE_CALL(ok, DuplicateHandle(nodup, GetCurrentProcess(), GetCurrentProcess(), &out, 0, FALSE, DUPLICATE_SAME_ACCESS));
        dup_case("dup_out_of_without_dup_access", ok);
        HANDLE withdup = OpenProcess(PROCESS_DUP_HANDLE, FALSE, pi.dwProcessId);
        ORACLE_CALL(ok, DuplicateHandle(GetCurrentProcess(), ev_plain, withdup, &out, 0, FALSE, DUPLICATE_SAME_ACCESS));
        dup_case("dup_into_with_dup_access_only", ok);
        CloseHandle(withdup);

        /* The child's pseudo thread handle has no meaning outside it. */
        ORACLE_CALL(ok, DuplicateHandle(pi.hProcess, GetCurrentThread(), GetCurrentProcess(), &out, 0, FALSE,
                                        DUPLICATE_SAME_ACCESS));
        dup_case("dup_child_pseudo_thread_handle", ok);
        if (ok) CloseHandle(out);

        TerminateProcess(nodup, 0);
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(nodup);
        ORACLE_CALL(ok, DuplicateHandle(GetCurrentProcess(), ev_plain, pi.hProcess, &out, 0, FALSE, DUPLICATE_SAME_ACCESS));
        dup_case("dup_into_exited_child", ok);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    /* ---- our own pseudo handles given to a child ---- */
    {
        spawn("file", FALSE, CREATE_SUSPENDED, NULL, &pi);
        HANDLE in_child = NULL;
        ORACLE_CALL(ok, DuplicateHandle(GetCurrentProcess(), GetCurrentProcess(), pi.hProcess, &in_child,
                                        SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, 0));
        dup_case("dup_own_pseudo_process_into_child", ok);
        FILE *f = fopen("h.txt", "wb");
        fprintf(f, "%p\n", in_child);
        fclose(f);
        ResumeThread(pi.hThread);
        code_case("child_sees_parent_process_handle", TRUE, finish(&pi), NULL, NULL);
        DeleteFileA("h.txt");
    }

    return oracle_end();
}
