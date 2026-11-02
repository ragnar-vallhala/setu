/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: VirtualAlloc / VirtualFree / VirtualProtect / VirtualQuery and
 * the process heap (HeapAlloc, HeapReAlloc, HeapSize, HeapFree). Addresses
 * are never printed, only sizes, states and protections relative to the
 * allocation.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualalloc
 *       https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualfree
 *       https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualprotect
 *       https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualquery
 *       https://learn.microsoft.com/en-us/windows/win32/memory/memory-protection-constants
 *       https://learn.microsoft.com/en-us/windows/win32/api/heapapi/nf-heapapi-heapalloc
 *       https://learn.microsoft.com/en-us/windows/win32/api/heapapi/nf-heapapi-heaprealloc
 *       https://learn.microsoft.com/en-us/windows/win32/api/heapapi/nf-heapapi-heapsize
 *       https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-getsysteminfo
 */
#include "oracle.h"

#define PAGE 4096

static char *base; /* the reservation under test */

static void query_case(const char *name, void *addr)
{
    MEMORY_BASIC_INFORMATION mbi;
    SIZE_T r;
    memset(&mbi, 0xcc, sizeof mbi);
    ORACLE_CALL(r, VirtualQuery(addr, &mbi, sizeof mbi));
    oracle_case(name);
    o_bool("ret_is_size", r == sizeof mbi);
    o_lasterr();
    if (r) {
        o_i64("base_offset", (char *)mbi.BaseAddress - base);
        o_i64("alloc_base_offset", mbi.AllocationBase ? (char *)mbi.AllocationBase - base : -1);
        o_hex32("alloc_protect", mbi.AllocationProtect);
        /* A free region's size depends on its neighbours: not deterministic. */
        if (mbi.State != MEM_FREE) o_u64("region_size", mbi.RegionSize);
        o_hex32("state", mbi.State);
        o_hex32("protect", mbi.Protect);
        o_hex32("type", mbi.Type);
    }
    oracle_case_end();
}

static void protect_case(const char *name, void *addr, SIZE_T size, DWORD prot)
{
    DWORD old = 0xdead;
    BOOL ok;
    ORACLE_CALL(ok, VirtualProtect(addr, size, prot, &old));
    oracle_case(name);
    o_bool("ret", ok);
    o_lasterr();
    o_hex32("old", old);
    oracle_case_end();
}

static void free_case(const char *name, void *addr, SIZE_T size, DWORD type)
{
    BOOL ok;
    ORACLE_CALL(ok, VirtualFree(addr, size, type));
    oracle_case(name);
    o_bool("ret", ok);
    o_lasterr();
    oracle_case_end();
}

static void alloc_case(const char *name, void *addr, SIZE_T size, DWORD type, DWORD prot, void **out)
{
    void *p;
    ORACLE_CALL(p, VirtualAlloc(addr, size, type, prot));
    oracle_case(name);
    o_bool("ret_null", p == NULL);
    o_lasterr();
    if (p && addr) o_i64("offset_from_request", (char *)p - (char *)addr);
    oracle_case_end();
    if (out) *out = p;
}

int main(void)
{
    oracle_begin("kernel32/VirtualAlloc");

    {
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        oracle_case("system_info");
        o_u32("page_size", si.dwPageSize);
        o_u32("allocation_granularity", si.dwAllocationGranularity);
        oracle_case_end();
    }

    base = VirtualAlloc(NULL, 16 * PAGE, MEM_RESERVE, PAGE_READWRITE);
    query_case("reserved", base);
    alloc_case("commit_middle", base + 4 * PAGE, 2 * PAGE, MEM_COMMIT, PAGE_READWRITE, NULL);
    query_case("query_before_commit", base);
    query_case("query_committed", base + 4 * PAGE);
    query_case("query_after_commit", base + 6 * PAGE);
    alloc_case("commit_unaligned", base + 8 * PAGE + 100, 10, MEM_COMMIT, PAGE_READONLY, NULL);
    query_case("query_unaligned_commit", base + 8 * PAGE);
    alloc_case("commit_beyond_reservation", base + 15 * PAGE, 2 * PAGE, MEM_COMMIT, PAGE_READWRITE, NULL);
    alloc_case("commit_bad_protect", base + 10 * PAGE, PAGE, MEM_COMMIT, 0x12345, NULL);
    alloc_case("commit_zero_protect", base + 10 * PAGE, PAGE, MEM_COMMIT, 0, NULL);
    alloc_case("recommit_committed", base + 4 * PAGE, PAGE, MEM_COMMIT, PAGE_READONLY, NULL);
    query_case("query_after_recommit", base + 4 * PAGE);
    alloc_case("reserve_inside_reservation", base + 12 * PAGE, PAGE, MEM_RESERVE, PAGE_READWRITE, NULL);

    {   /* Committed pages are zero-filled. */
        oracle_case("zero_filled");
        int zero = 1;
        for (int i = 0; i < 2 * PAGE; i++) if (base[4 * PAGE + i]) zero = 0;
        o_bool("zero", zero);
        oracle_case_end();
    }

    protect_case("protect_readonly", base + 4 * PAGE, PAGE, PAGE_READONLY);
    query_case("query_after_protect", base + 4 * PAGE);
    protect_case("protect_back", base + 4 * PAGE, PAGE, PAGE_READWRITE);
    protect_case("protect_span_two_regions", base + 4 * PAGE, 2 * PAGE, PAGE_EXECUTE_READ);
    protect_case("protect_reserved_only", base, PAGE, PAGE_READWRITE);
    protect_case("protect_guard", base + 5 * PAGE, PAGE, PAGE_READWRITE | PAGE_GUARD);
    query_case("query_guard", base + 5 * PAGE);
    protect_case("protect_noaccess", base + 5 * PAGE, PAGE, PAGE_NOACCESS);
    protect_case("protect_invalid_value", base + 5 * PAGE, PAGE, 0x3);
    protect_case("protect_null_size", base + 4 * PAGE, 0, PAGE_READWRITE);

    free_case("decommit_one", base + 4 * PAGE, PAGE, MEM_DECOMMIT);
    query_case("query_after_decommit", base + 4 * PAGE);
    free_case("decommit_already_decommitted", base + 4 * PAGE, PAGE, MEM_DECOMMIT);
    free_case("release_with_size", base, 16 * PAGE, MEM_RELEASE);
    free_case("release_not_base", base + PAGE, 0, MEM_RELEASE);
    free_case("free_bad_type", base, 0, 0x12345);
    free_case("free_both_flags", base, 0, MEM_RELEASE | MEM_DECOMMIT);
    free_case("release_base", base, 0, MEM_RELEASE);
    query_case("query_after_release", base);
    free_case("release_twice", base, 0, MEM_RELEASE);

    {   /* Commit+reserve in one call, size rounding, top-down. */
        void *p;
        alloc_case("reserve_commit_odd_size", NULL, 5000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE, &p);
        char *keep = base;
        base = p;
        query_case("query_odd_size", p);
        base = keep;
        VirtualFree(p, 0, MEM_RELEASE);
        alloc_case("zero_size", NULL, 0, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE, NULL);
        alloc_case("no_type", NULL, PAGE, 0, PAGE_READWRITE, NULL);
        alloc_case("commit_without_address_or_reserve", NULL, PAGE, MEM_COMMIT, PAGE_READWRITE, &p);
        if (p) VirtualFree(p, 0, MEM_RELEASE);
        alloc_case("reset_on_null", NULL, PAGE, MEM_RESET, PAGE_READWRITE, NULL);
    }

    {   /* Heap. */
        HANDLE heap = GetProcessHeap();
        SIZE_T sz;
        char *p, *q;
        BOOL ok;

        ORACLE_CALL(p, HeapAlloc(heap, HEAP_ZERO_MEMORY, 100));
        oracle_case("heap_alloc_zero");
        o_bool("ret_null", p == NULL);
        o_lasterr();
        int zero = 1;
        for (int i = 0; i < 100; i++) if (p[i]) zero = 0;
        o_bool("zero", zero);
        oracle_case_end();

        ORACLE_CALL(sz, HeapSize(heap, 0, p));
        oracle_case("heap_size_exact");
        o_u64("size", sz);
        o_lasterr();
        oracle_case_end();

        memset(p, 'A', 100);
        ORACLE_CALL(q, HeapReAlloc(heap, HEAP_ZERO_MEMORY, p, 200));
        oracle_case("heap_realloc_grow_zero");
        o_bool("ret_null", q == NULL);
        o_lasterr();
        if (q) {
            int keeps = 1, zero_tail = 1;
            for (int i = 0; i < 100; i++) if (q[i] != 'A') keeps = 0;
            for (int i = 100; i < 200; i++) if (q[i]) zero_tail = 0;
            o_bool("keeps_contents", keeps);
            o_bool("zero_tail", zero_tail);
            o_u64("size", HeapSize(heap, 0, q));
            p = q;
        }
        oracle_case_end();

        ORACLE_CALL(q, HeapReAlloc(heap, HEAP_REALLOC_IN_PLACE_ONLY, p, 50));
        oracle_case("heap_realloc_shrink_in_place");
        o_bool("same_pointer", q == p);
        o_lasterr();
        if (q) o_u64("size", HeapSize(heap, 0, q));
        oracle_case_end();

        ORACLE_CALL(q, HeapAlloc(heap, 0, 0));
        oracle_case("heap_alloc_zero_bytes");
        o_bool("ret_null", q == NULL);
        o_lasterr();
        if (q) o_u64("size", HeapSize(heap, 0, q));
        oracle_case_end();
        HeapFree(heap, 0, q);

        ORACLE_CALL(ok, HeapFree(heap, 0, NULL));
        oracle_case("heap_free_null");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();

        ORACLE_CALL(ok, HeapFree(heap, 0, p));
        oracle_case("heap_free");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();

        HANDLE h2;
        ORACLE_CALL(h2, HeapCreate(0, 0, 8192));
        oracle_case("heap_create_fixed_max");
        o_bool("ret_null", h2 == NULL);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(p, HeapAlloc(h2, 0, 1 << 20));
        oracle_case("heap_alloc_over_fixed_max");
        o_bool("ret_null", p == NULL);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(ok, HeapDestroy(h2));
        oracle_case("heap_destroy");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
    }

    return oracle_end();
}
