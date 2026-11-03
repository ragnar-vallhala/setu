/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: ReadProcessMemory / WriteProcessMemory on the current process
 * and on a suspended child: byte counts, partial copies across a page that
 * is not readable, writes to read-only and executable pages, NULL and
 * unmapped addresses, and handles without the needed access rights.
 *
 * The child is this .exe started suspended; it shares the image layout, so
 * the address of a global in the parent is the address of the same global
 * in the child (ASLR relocates each image once per boot). Addresses are
 * never printed.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-readprocessmemory
 *       https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-writeprocessmemory
 *       https://learn.microsoft.com/en-us/windows/win32/procthread/process-security-and-access-rights
 *       https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualallocex
 *       https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualprotectex
 *       https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualqueryex
 */
#include "oracle.h"

static char marker[32] = "setu-process-memory-marker";
static const char ro_data[16] = "read-only-data!";

static void rw_case(const char *name, BOOL ok, SIZE_T done)
{
    oracle_case(name);
    o_bool("ret", ok);
    o_lasterr();
    o_u64("bytes", done);
    oracle_case_end();
}

static void read_case(const char *name, HANDLE p, const void *addr, SIZE_T n)
{
    char buf[0x3000];
    SIZE_T done = 0x5555;
    BOOL ok;
    memset(buf, 0, sizeof buf);
    ORACLE_CALL(ok, ReadProcessMemory(p, addr, buf, n, &done));
    rw_case(name, ok, done);
}

static void write_case(const char *name, HANDLE p, void *addr, const void *src, SIZE_T n)
{
    SIZE_T done = 0x5555;
    BOOL ok;
    ORACLE_CALL(ok, WriteProcessMemory(p, addr, src, n, &done));
    rw_case(name, ok, done);
}

int main(int argc, char **argv)
{
    (void)argv;
    if (argc > 1) {
        /* Child: never runs (always started suspended and terminated). */
        return 0;
    }
    oracle_begin("kernel32/process_memory");
    HANDLE me = GetCurrentProcess();
    BOOL ok;
    SIZE_T done;

    /* ---- self ---- */
    {
        char buf[32] = {0};
        ORACLE_CALL(ok, ReadProcessMemory(me, marker, buf, sizeof marker, &done));
        oracle_case("self_read");
        o_bool("ret", ok);
        o_lasterr();
        o_u64("bytes", done);
        o_str("data", buf);
        oracle_case_end();
        ORACLE_CALL(ok, ReadProcessMemory(me, marker, buf, 8, NULL));
        oracle_case("self_read_null_count");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        read_case("self_read_zero_bytes", me, marker, 0);
        read_case("self_read_null_address", me, NULL, 4);
        read_case("self_read_null_address_zero_bytes", me, NULL, 0);
        {
            /* The byte count is left undefined here (varies run to run). */
            char kb[4];
            ORACLE_CALL(ok, ReadProcessMemory(me, (void *)0xffff800000000000ull, kb, 4, &done));
            oracle_case("self_read_kernel_address");
            o_bool("ret", ok);
            o_lasterr();
            oracle_case_end();
        }
        write_case("self_write", me, marker, "SETU", 4);
        oracle_case("self_write_visible");
        o_str("data", marker);
        oracle_case_end();
        write_case("self_write_read_only_section", me, (void *)ro_data, "XX", 2);
        oracle_case("self_write_read_only_section_result");
        o_str("data", ro_data);
        oracle_case_end();
    }

    /* ---- across a page boundary into an inaccessible page ---- */
    {
        char *two = VirtualAlloc(NULL, 0x3000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        memset(two, 'a', 0x3000);
        DWORD old;
        VirtualProtect(two + 0x1000, 0x1000, PAGE_NOACCESS, &old);
        read_case("self_read_into_noaccess_page", me, two + 0x1000 - 16, 32);
        read_case("self_read_starting_in_noaccess_page", me, two + 0x1000, 16);
        write_case("self_write_into_noaccess_page", me, two + 0x1000 - 16, "0123456789abcdef0123456789abcdef", 32);
        oracle_case("self_write_into_noaccess_page_effect");
        o_bool("first_part_written", two[0x1000 - 16] == '0');
        oracle_case_end();
        VirtualProtect(two + 0x1000, 0x1000, PAGE_READONLY, &old);
        write_case("self_write_readonly_page", me, two + 0x1000, "zz", 2);
        oracle_case("self_write_readonly_page_effect");
        o_bool("written", two[0x1000] == 'z');
        DWORD prot = 0;
        MEMORY_BASIC_INFORMATION mbi;
        VirtualQuery(two + 0x1000, &mbi, sizeof mbi);
        prot = mbi.Protect;
        o_hex32("protect_after", prot);
        oracle_case_end();
        VirtualProtect(two + 0x1000, 0x1000, PAGE_EXECUTE_READ, &old);
        write_case("self_write_execute_read_page", me, two + 0x1000, "yy", 2);
        VirtualQuery(two + 0x1000, &mbi, sizeof mbi);
        oracle_case("self_write_execute_read_page_effect");
        o_bool("written", two[0x1000] == 'y');
        o_hex32("protect_after", mbi.Protect);
        oracle_case_end();
        VirtualProtect(two + 0x1000, 0x1000, PAGE_GUARD | PAGE_READWRITE, &old);
        read_case("self_read_guard_page", me, two + 0x1000, 4);
        VirtualQuery(two + 0x1000, &mbi, sizeof mbi);
        oracle_case("guard_page_after_read");
        o_hex32("protect_after", mbi.Protect);
        oracle_case_end();
        VirtualFree(two, 0, MEM_RELEASE);
        read_case("self_read_released", me, two, 4);
    }

    /* ---- child ---- */
    {
        char selfpath[MAX_PATH], cmd[MAX_PATH + 16];
        STARTUPINFOA si = {.cb = sizeof si};
        PROCESS_INFORMATION pi;
        GetModuleFileNameA(NULL, selfpath, MAX_PATH);
        snprintf(cmd, sizeof cmd, "\"%s\" child", selfpath);
        CreateProcessA(NULL, cmd, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL, &si, &pi);

        char buf[32] = {0};
        ORACLE_CALL(ok, ReadProcessMemory(pi.hProcess, marker, buf, sizeof marker, &done));
        oracle_case("child_read_global");
        o_bool("ret", ok);
        o_lasterr();
        o_u64("bytes", done);
        o_str("data", buf); /* the child's copy still has the original text */
        oracle_case_end();

        write_case("child_write_global", pi.hProcess, marker, "CHILD", 5);
        memset(buf, 0, sizeof buf);
        ReadProcessMemory(pi.hProcess, marker, buf, sizeof marker, &done);
        oracle_case("child_read_back");
        o_str("data", buf);
        o_str("parent_unchanged", marker);
        oracle_case_end();
        write_case("child_write_read_only_section", pi.hProcess, (void *)ro_data, "QQ", 2);
        memset(buf, 0, sizeof buf);
        ReadProcessMemory(pi.hProcess, ro_data, buf, sizeof ro_data, &done);
        oracle_case("child_read_only_section_after_write");
        o_str("data", buf);
        oracle_case_end();

        void *remote;
        ORACLE_CALL(remote, VirtualAllocEx(pi.hProcess, NULL, 0x2000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        oracle_case("child_virtual_alloc");
        o_bool("ret_null", remote == NULL);
        o_lasterr();
        oracle_case_end();
        write_case("child_write_allocated", pi.hProcess, remote, "remote!", 8);
        DWORD old = 0;
        ORACLE_CALL(ok, VirtualProtectEx(pi.hProcess, (char *)remote + 0x1000, 0x1000, PAGE_NOACCESS, &old));
        oracle_case("child_virtual_protect");
        o_bool("ret", ok);
        o_lasterr();
        o_hex32("old", old);
        oracle_case_end();
        read_case("child_read_into_noaccess", pi.hProcess, (char *)remote + 0x1000 - 8, 16);
        MEMORY_BASIC_INFORMATION mbi;
        SIZE_T q;
        ORACLE_CALL(q, VirtualQueryEx(pi.hProcess, remote, &mbi, sizeof mbi));
        oracle_case("child_virtual_query");
        o_bool("ret_full", q == sizeof mbi);
        o_lasterr();
        o_hex32("state", mbi.State);
        o_hex32("protect", mbi.Protect);
        o_u64("region", mbi.RegionSize);
        oracle_case_end();
        read_case("child_read_unmapped", pi.hProcess, (void *)0x10000, 4);

        /* Limited handles. */
        HANDLE rd = OpenProcess(PROCESS_VM_READ, FALSE, pi.dwProcessId);
        HANDLE wr = OpenProcess(PROCESS_VM_WRITE, FALSE, pi.dwProcessId);
        HANDLE wrop = OpenProcess(PROCESS_VM_WRITE | PROCESS_VM_OPERATION, FALSE, pi.dwProcessId);
        HANDLE ql = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pi.dwProcessId);
        read_case("child_read_vm_read_handle", rd, marker, 8);
        read_case("child_read_vm_write_handle", wr, marker, 8);
        write_case("child_write_vm_read_handle", rd, marker, "R", 1);
        write_case("child_write_vm_write_only_handle", wr, marker, "W", 1);
        write_case("child_write_vm_write_operation_handle", wrop, marker, "O", 1);
        read_case("child_read_query_limited_handle", ql, marker, 8);
        ORACLE_CALL(q, VirtualQueryEx(ql, remote, &mbi, sizeof mbi));
        oracle_case("child_virtual_query_query_limited_handle");
        o_u64("ret", q);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(q, VirtualQueryEx(rd, remote, &mbi, sizeof mbi));
        oracle_case("child_virtual_query_vm_read_handle");
        o_u64("ret", q);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(remote, VirtualAllocEx(wr, NULL, 0x1000, MEM_COMMIT, PAGE_READWRITE));
        oracle_case("child_virtual_alloc_vm_write_handle");
        o_bool("ret_null", remote == NULL);
        o_lasterr();
        oracle_case_end();
        CloseHandle(rd);
        CloseHandle(wr);
        CloseHandle(wrop);
        CloseHandle(ql);

        TerminateProcess(pi.hProcess, 0);
        WaitForSingleObject(pi.hProcess, INFINITE);
        read_case("child_read_after_exit", pi.hProcess, marker, 8);
        write_case("child_write_after_exit", pi.hProcess, marker, "X", 1);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }

    read_case("read_null_handle", NULL, marker, 4);
    {
        HANDLE ev = CreateEventW(NULL, FALSE, FALSE, NULL);
        read_case("read_event_handle", ev, marker, 4);
        CloseHandle(ev);
    }

    return oracle_end();
}
