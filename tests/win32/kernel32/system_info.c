/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: system information shape: GetSystemInfo / GetNativeSystemInfo,
 * GlobalMemoryStatusEx / GlobalMemoryStatus, processor counts from several
 * APIs, GetLogicalProcessorInformation(Ex) buffer protocol, IsWow64Process(2),
 * GetLargePageMinimum and a few processor features every x64 CPU has.
 *
 * Machine-dependent numbers (memory sizes, CPU counts) are never printed:
 * only the relationships between them that Windows guarantees.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-getsysteminfo
 *       https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-getnativesysteminfo
 *       https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/ns-sysinfoapi-system_info
 *       https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-globalmemorystatusex
 *       https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-globalmemorystatus
 *       https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-getlogicalprocessorinformation
 *       https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-getlogicalprocessorinformationex
 *       https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-getactiveprocessorcount
 *       https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-getactiveprocessorgroupcount
 *       https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getcurrentprocessornumber
 *       https://learn.microsoft.com/en-us/windows/win32/api/wow64apiset/nf-wow64apiset-iswow64process
 *       https://learn.microsoft.com/en-us/windows/win32/api/wow64apiset/nf-wow64apiset-iswow64process2
 *       https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-getlargepageminimum
 *       https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-isprocessorfeaturepresent
 *       https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-getphysicallyinstalledsystemmemory
 */
#include "oracle.h"

static int popcount64(ULONG_PTR v)
{
    int n = 0;
    for (; v; v &= v - 1) n++;
    return n;
}

typedef BOOL(WINAPI *iswow64_2_fn)(HANDLE, USHORT *, USHORT *);
typedef BOOL(WINAPI *installed_fn)(ULONGLONG *);

int main(void)
{
    oracle_begin("kernel32/system_info");
    SYSTEM_INFO si, nsi;
    BOOL ok;
    memset(&si, 0xcc, sizeof si);
    memset(&nsi, 0xcc, sizeof nsi);

    SetLastError(ORACLE_SENTINEL);
    GetSystemInfo(&si);
    oracle_last_error = GetLastError();
    oracle_case("system_info");
    o_lasterr();
    o_u32("arch", si.wProcessorArchitecture);
    o_u32("reserved", si.wReserved);
    o_u32("page_size", si.dwPageSize);
    o_u32("granularity", si.dwAllocationGranularity);
    o_hex32("min_app_low", (DWORD)(ULONG_PTR)si.lpMinimumApplicationAddress);
    o_u64("max_app", (ULONG_PTR)si.lpMaximumApplicationAddress);
    o_u32("processor_type", si.dwProcessorType);
    o_bool("cpus_nonzero", si.dwNumberOfProcessors > 0);
    o_bool("mask_matches_count", popcount64(si.dwActiveProcessorMask) == (int)si.dwNumberOfProcessors);
    o_bool("level_nonzero", si.wProcessorLevel != 0);
    oracle_case_end();

    SetLastError(ORACLE_SENTINEL);
    GetNativeSystemInfo(&nsi);
    oracle_last_error = GetLastError();
    oracle_case("native_system_info_equals_system_info");
    o_lasterr();
    o_bool("equal", !memcmp(&si, &nsi, sizeof si));
    oracle_case_end();

    /* ---- processor counts agree ---- */
    {
        DWORD all = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
        DWORD g0 = GetActiveProcessorCount(0);
        DWORD mx = GetMaximumProcessorCount(ALL_PROCESSOR_GROUPS);
        WORD groups = GetActiveProcessorGroupCount();
        WORD maxgroups = GetMaximumProcessorGroupCount();
        DWORD cur = GetCurrentProcessorNumber();
        oracle_case("processor_counts");
        o_bool("active_all_eq_sysinfo", all == si.dwNumberOfProcessors);
        o_bool("group0_eq_sysinfo", g0 == si.dwNumberOfProcessors);
        o_bool("max_ge_active", mx >= all);
        o_u32("active_groups", groups);
        o_u32("max_groups", maxgroups);
        o_bool("current_in_range", cur < si.dwNumberOfProcessors);
        oracle_case_end();
        DWORD bad;
        ORACLE_CALL(bad, GetActiveProcessorCount(5));
        oracle_case("active_count_bad_group");
        o_u32("ret", bad);
        o_lasterr();
        oracle_case_end();
        DWORD good;
        ORACLE_CALL(good, GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
        oracle_case("active_count_all_last_error");
        o_bool("nonzero", good != 0);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(bad, GetMaximumProcessorCount(5));
        oracle_case("max_count_bad_group");
        o_u32("ret", bad);
        o_lasterr();
        oracle_case_end();
        WORD gc;
        ORACLE_CALL(gc, GetActiveProcessorGroupCount());
        oracle_case("active_group_count_last_error");
        o_u32("ret", gc);
        o_lasterr();
        oracle_case_end();
    }

    /* ---- memory status ---- */
    {
        MEMORYSTATUSEX ms;
        memset(&ms, 0, sizeof ms);
        ms.dwLength = sizeof ms - 1;
        ORACLE_CALL(ok, GlobalMemoryStatusEx(&ms));
        oracle_case("memstatusex_bad_length");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        ms.dwLength = sizeof ms + 8;
        ORACLE_CALL(ok, GlobalMemoryStatusEx(&ms));
        oracle_case("memstatusex_long_length");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();

        ms.dwLength = sizeof ms;
        ORACLE_CALL(ok, GlobalMemoryStatusEx(&ms));
        oracle_case("memstatusex");
        o_bool("ret", ok);
        o_lasterr();
        o_u32("length_after", ms.dwLength);
        o_bool("load_le_100", ms.dwMemoryLoad <= 100);
        o_bool("avail_phys_le_total", ms.ullAvailPhys <= ms.ullTotalPhys);
        o_bool("total_phys_nonzero", ms.ullTotalPhys > 0);
        o_bool("avail_pagefile_le_total", ms.ullAvailPageFile <= ms.ullTotalPageFile);
        o_bool("avail_virtual_le_total", ms.ullAvailVirtual <= ms.ullTotalVirtual);
        o_bool("total_virtual_matches_app_range",
               ms.ullTotalVirtual == (ULONG_PTR)si.lpMaximumApplicationAddress - (ULONG_PTR)si.lpMinimumApplicationAddress + 1);
        o_u64("avail_extended", ms.ullAvailExtendedVirtual);
        oracle_case_end();

        /* Committing 64 MB lowers available virtual space by at least that. */
        MEMORYSTATUSEX before = {.dwLength = sizeof before}, after = {.dwLength = sizeof after};
        GlobalMemoryStatusEx(&before);
        void *big = VirtualAlloc(NULL, 64 << 20, MEM_RESERVE, PAGE_NOACCESS);
        GlobalMemoryStatusEx(&after);
        oracle_case("memstatusex_reserve_lowers_avail_virtual");
        o_bool("lowered_by_at_least_64mb", before.ullAvailVirtual - after.ullAvailVirtual >= (64ull << 20));
        oracle_case_end();
        VirtualFree(big, 0, MEM_RELEASE);

        MEMORYSTATUS old;
        memset(&old, 0xcc, sizeof old);
        old.dwLength = 0;
        SetLastError(ORACLE_SENTINEL);
        GlobalMemoryStatus(&old);
        oracle_last_error = GetLastError();
        oracle_case("memstatus_legacy");
        o_lasterr();
        o_u32("length_after", old.dwLength);
        o_bool("load_le_100", old.dwMemoryLoad <= 100);
        o_bool("avail_phys_le_total", old.dwAvailPhys <= old.dwTotalPhys);
        o_bool("total_virtual_matches_ex", old.dwTotalVirtual == ms.ullTotalVirtual);
        oracle_case_end();
    }

    {
        installed_fn installed = (installed_fn)(void *)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),
                                                                     "GetPhysicallyInstalledSystemMemory");
        oracle_case("physically_installed");
        o_bool("present", installed != NULL);
        if (installed) {
            ULONGLONG kb = 0;
            MEMORYSTATUSEX ms = {.dwLength = sizeof ms};
            GlobalMemoryStatusEx(&ms);
            BOOL r = installed(&kb);
            o_bool("ret", r);
            o_bool("ge_total_phys", kb * 1024 >= ms.ullTotalPhys);
        }
        oracle_case_end();
        if (installed) {
            ORACLE_CALL(ok, installed(NULL));
            oracle_case("physically_installed_null");
            o_bool("ret", ok);
            o_lasterr();
            oracle_case_end();
        }
    }

    /* ---- GetLogicalProcessorInformation ---- */
    {
        DWORD len = 0;
        ORACLE_CALL(ok, GetLogicalProcessorInformation(NULL, &len));
        oracle_case("lpi_query_size");
        o_bool("ret", ok);
        o_lasterr();
        o_bool("len_multiple_of_entry", len % sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION) == 0);
        o_bool("len_nonzero", len > 0);
        oracle_case_end();
        DWORD small = sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION) - 1;
        SYSTEM_LOGICAL_PROCESSOR_INFORMATION one[1];
        ORACLE_CALL(ok, GetLogicalProcessorInformation(one, &small));
        oracle_case("lpi_too_small");
        o_bool("ret", ok);
        o_lasterr();
        o_bool("len_reported_full", small == len);
        oracle_case_end();

        SYSTEM_LOGICAL_PROCESSOR_INFORMATION *buf = malloc(len);
        ORACLE_CALL(ok, GetLogicalProcessorInformation(buf, &len));
        int cores = 0, logical_in_cores = 0, packages = 0, numa = 0, caches = 0, l1 = 0, bad_cache = 0;
        ULONG_PTR core_union = 0;
        int overlap = 0;
        for (DWORD i = 0; ok && i < len / sizeof *buf; i++) {
            switch (buf[i].Relationship) {
            case RelationProcessorCore:
                cores++;
                logical_in_cores += popcount64(buf[i].ProcessorMask);
                if (core_union & buf[i].ProcessorMask) overlap = 1;
                core_union |= buf[i].ProcessorMask;
                break;
            case RelationProcessorPackage: packages++; break;
            case RelationNumaNode: numa++; break;
            case RelationCache:
                caches++;
                if (buf[i].Cache.Level == 1) l1++;
                if (buf[i].Cache.Level < 1 || buf[i].Cache.Level > 4 || !buf[i].Cache.LineSize) bad_cache++;
                break;
            default: break;
            }
        }
        oracle_case("lpi_full");
        o_bool("ret", ok);
        o_lasterr();
        o_bool("has_core", cores > 0);
        o_bool("core_logical_eq_cpus", logical_in_cores == (int)si.dwNumberOfProcessors);
        o_bool("core_masks_disjoint", !overlap);
        o_bool("core_union_eq_active_mask", core_union == si.dwActiveProcessorMask);
        o_bool("has_package", packages > 0);
        o_bool("has_numa", numa > 0);
        o_bool("has_l1", l1 > 0);
        o_i64("bad_cache_entries", bad_cache);
        oracle_case_end();
        free(buf);
    }

    /* ---- GetLogicalProcessorInformationEx ---- */
    {
        DWORD len = 0;
        ORACLE_CALL(ok, GetLogicalProcessorInformationEx(RelationProcessorCore, NULL, &len));
        oracle_case("lpix_core_query_size");
        o_bool("ret", ok);
        o_lasterr();
        o_bool("len_nonzero", len > 0);
        oracle_case_end();
        char *buf = malloc(len);
        ORACLE_CALL(ok, GetLogicalProcessorInformationEx(RelationProcessorCore, (void *)buf, &len));
        int entries = 0, logical = 0, wrong_rel = 0, bad_size = 0;
        for (DWORD off = 0; ok && off < len;) {
            SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *e = (void *)(buf + off);
            if (e->Size == 0) { bad_size = 1; break; }
            entries++;
            if (e->Relationship != RelationProcessorCore) wrong_rel++;
            for (WORD g = 0; g < e->Processor.GroupCount; g++) logical += popcount64(e->Processor.GroupMask[g].Mask);
            off += e->Size;
        }
        oracle_case("lpix_core");
        o_bool("ret", ok);
        o_lasterr();
        o_bool("entries_nonzero", entries > 0);
        o_i64("wrong_relationship", wrong_rel);
        o_bool("bad_size", bad_size);
        o_bool("logical_eq_cpus", logical == (int)si.dwNumberOfProcessors);
        oracle_case_end();
        free(buf);

        len = 0;
        ORACLE_CALL(ok, GetLogicalProcessorInformationEx(RelationGroup, NULL, &len));
        oracle_case("lpix_group_query_size");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        buf = malloc(len);
        ok = GetLogicalProcessorInformationEx(RelationGroup, (void *)buf, &len);
        oracle_case("lpix_group");
        o_bool("ret", ok);
        if (ok) {
            SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *e = (void *)buf;
            o_bool("one_entry", e->Size == len);
            o_u32("active_groups", e->Group.ActiveGroupCount);
            o_bool("group0_active_eq_cpus", e->Group.GroupInfo[0].ActiveProcessorCount == si.dwNumberOfProcessors);
            o_bool("group0_mask_eq_sysinfo", e->Group.GroupInfo[0].ActiveProcessorMask == si.dwActiveProcessorMask);
        }
        oracle_case_end();
        free(buf);

        len = 0;
        ORACLE_CALL(ok, GetLogicalProcessorInformationEx((LOGICAL_PROCESSOR_RELATIONSHIP)77, NULL, &len));
        oracle_case("lpix_bad_relationship");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        for (int rel = 5; rel <= 8; rel++) {
            char nm[64];
            len = 0;
            ORACLE_CALL(ok, GetLogicalProcessorInformationEx((LOGICAL_PROCESSOR_RELATIONSHIP)rel, NULL, &len));
            snprintf(nm, sizeof nm, "probe_lpix_relationship_%d", rel);
            oracle_case(nm);
            o_bool("ret", ok);
            o_lasterr();
            o_bool("len_nonzero", len != 0);
            oracle_case_end();
        }
    }

    /* ---- WOW64 ---- */
    {
        BOOL wow = 0x55;
        ORACLE_CALL(ok, IsWow64Process(GetCurrentProcess(), &wow));
        oracle_case("iswow64");
        o_bool("ret", ok);
        o_lasterr();
        o_i64("wow64", wow);
        oracle_case_end();
        iswow64_2_fn w2 = (iswow64_2_fn)(void *)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "IsWow64Process2");
        oracle_case("iswow64_2");
        o_bool("present", w2 != NULL);
        if (w2) {
            USHORT pm = 0x5555, nm = 0x5555;
            o_bool("ret", w2(GetCurrentProcess(), &pm, &nm));
            o_hex32("process_machine", pm);
            o_hex32("native_machine", nm);
        }
        oracle_case_end();
    }

    /* ---- misc ---- */
    {
        oracle_case("large_page_minimum");
        o_u64("bytes", GetLargePageMinimum());
        oracle_case_end();
        oracle_case("processor_features");
        o_bool("sse2", IsProcessorFeaturePresent(PF_XMMI64_INSTRUCTIONS_AVAILABLE));
        o_bool("sse", IsProcessorFeaturePresent(PF_XMMI_INSTRUCTIONS_AVAILABLE));
        o_bool("rdtsc", IsProcessorFeaturePresent(PF_RDTSC_INSTRUCTION_AVAILABLE));
        o_bool("cmpxchg_double", IsProcessorFeaturePresent(PF_COMPARE_EXCHANGE_DOUBLE));
        o_bool("nx", IsProcessorFeaturePresent(PF_NX_ENABLED));
        o_bool("fp_errata", IsProcessorFeaturePresent(PF_FLOATING_POINT_PRECISION_ERRATA));
        o_bool("feature_63", IsProcessorFeaturePresent(63));
        o_bool("feature_200", IsProcessorFeaturePresent(200));
        oracle_case_end();
    }

    return oracle_end();
}
