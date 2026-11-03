/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: private heaps: HeapCreate options and limits, HeapAlloc /
 * HeapReAlloc / HeapSize flags and errors, HeapValidate, HeapWalk entry
 * shapes, HeapLock, HeapCompact, Heap{Query,Set}Information and
 * GetProcessHeaps.
 *
 * Pointers and addresses are never printed: blocks are compared, sizes are
 * the requested sizes, and HeapWalk results are reduced to counts and flags.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/heapapi/nf-heapapi-heapcreate
 *       https://learn.microsoft.com/en-us/windows/win32/api/heapapi/nf-heapapi-heapalloc
 *       https://learn.microsoft.com/en-us/windows/win32/api/heapapi/nf-heapapi-heaprealloc
 *       https://learn.microsoft.com/en-us/windows/win32/api/heapapi/nf-heapapi-heapsize
 *       https://learn.microsoft.com/en-us/windows/win32/api/heapapi/nf-heapapi-heapvalidate
 *       https://learn.microsoft.com/en-us/windows/win32/api/heapapi/nf-heapapi-heapwalk
 *       https://learn.microsoft.com/en-us/windows/win32/api/minwinbase/ns-minwinbase-process_heap_entry
 *       https://learn.microsoft.com/en-us/windows/win32/api/heapapi/nf-heapapi-heaplock
 *       https://learn.microsoft.com/en-us/windows/win32/api/heapapi/nf-heapapi-heapcompact
 *       https://learn.microsoft.com/en-us/windows/win32/api/heapapi/nf-heapapi-heapqueryinformation
 *       https://learn.microsoft.com/en-us/windows/win32/api/heapapi/nf-heapapi-heapsetinformation
 *       https://learn.microsoft.com/en-us/windows/win32/api/heapapi/nf-heapapi-getprocessheaps
 *       https://learn.microsoft.com/en-us/windows/win32/api/heapapi/nf-heapapi-heapdestroy
 */
#include "oracle.h"

#define BOOL_CASE(name, expr)     \
    do {                          \
        BOOL ok_;                 \
        ORACLE_CALL(ok_, (expr)); \
        oracle_case(name);        \
        o_bool("ret", ok_);       \
        o_lasterr();              \
        oracle_case_end();        \
    } while (0)

static void alloc_case(const char *name, void *p)
{
    oracle_case(name);
    o_bool("ret_null", p == NULL);
    o_lasterr();
    oracle_case_end();
}

static const char *prot_name(DWORD p)
{
    switch (p) {
    case PAGE_READWRITE: return "PAGE_READWRITE";
    case PAGE_EXECUTE_READWRITE: return "PAGE_EXECUTE_READWRITE";
    case PAGE_READONLY: return "PAGE_READONLY";
    default: return "other";
    }
}

/* Walks a heap and reduces it to counts. */
static void walk_case(const char *name, HANDLE heap, SIZE_T want1, SIZE_T want2, SIZE_T want3)
{
    PROCESS_HEAP_ENTRY e;
    int regions = 0, busy = 0, uncommitted = 0, free_blocks = 0, first_is_region = -1, other = 0;
    int found1 = 0, found2 = 0, found3 = 0, region_index_zero = 1, busy_has_moveable = 0;
    BOOL ok;
    DWORD end_err = 0;
    memset(&e, 0, sizeof e);
    e.lpData = NULL;
    int guard = 0;
    for (;;) {
        SetLastError(ORACLE_SENTINEL);
        ok = HeapWalk(heap, &e);
        if (!ok) {
            end_err = GetLastError();
            break;
        }
        if (++guard > 100000) break;
        if (first_is_region < 0) first_is_region = !!(e.wFlags & PROCESS_HEAP_REGION);
        if (e.wFlags & PROCESS_HEAP_REGION) {
            regions++;
            if (e.iRegionIndex != 0) region_index_zero = 0;
        } else if (e.wFlags & PROCESS_HEAP_UNCOMMITTED_RANGE) {
            uncommitted++;
        } else if (e.wFlags & PROCESS_HEAP_ENTRY_BUSY) {
            busy++;
            if (e.wFlags & PROCESS_HEAP_ENTRY_MOVEABLE) busy_has_moveable = 1;
            if (e.cbData == want1) found1++;
            if (e.cbData == want2) found2++;
            if (e.cbData == want3) found3++;
        } else if (e.wFlags == 0) {
            free_blocks++;
        } else {
            other++;
        }
    }
    oracle_case(name);
    o_bool("first_is_region", first_is_region == 1);
    o_bool("has_region", regions > 0);
    o_bool("single_region_index", region_index_zero);
    o_i64("busy", busy);
    o_i64("found_size1", found1);
    o_i64("found_size2", found2);
    o_i64("found_size3", found3);
    o_bool("has_free_block", free_blocks > 0);
    o_bool("busy_moveable", busy_has_moveable);
    o_i64("other_flags", other);
    if (end_err == ORACLE_SENTINEL) o_str("end_error", "unchanged");
    else o_u32("end_error", end_err);
    oracle_case_end();
}

int main(void)
{
    oracle_begin("kernel32/heap_api");
    BOOL ok;
    HANDLE heap;
    void *p, *q;

    /* ---- process heap ---- */
    {
        HANDLE heaps[64];
        DWORD n;
        ORACLE_CALL(n, GetProcessHeaps(0, NULL));
        oracle_case("get_process_heaps_count_only");
        o_bool("nonzero", n > 0);
        o_lasterr();
        oracle_case_end();
        DWORD n2 = GetProcessHeaps(64, heaps);
        int has_default = 0;
        for (DWORD i = 0; i < n2 && i < 64; i++) has_default |= heaps[i] == GetProcessHeap();
        oracle_case("get_process_heaps_lists_default");
        o_bool("same_count", n == n2);
        o_bool("first_is_default", n2 > 0 && heaps[0] == GetProcessHeap());
        o_bool("has_default", has_default);
        oracle_case_end();

        HANDLE h2 = HeapCreate(0, 0, 0);
        DWORD n3 = GetProcessHeaps(64, heaps);
        int has_new = 0;
        for (DWORD i = 0; i < n3 && i < 64; i++) has_new |= heaps[i] == h2;
        oracle_case("get_process_heaps_after_create");
        o_i64("delta", (long long)n3 - (long long)n2);
        o_bool("has_new", has_new);
        oracle_case_end();
        HeapDestroy(h2);
        DWORD n4 = GetProcessHeaps(64, heaps);
        oracle_case("get_process_heaps_after_destroy");
        o_i64("delta", (long long)n4 - (long long)n2);
        oracle_case_end();

        DWORD n5;
        ORACLE_CALL(n5, GetProcessHeaps(1, heaps));
        oracle_case("get_process_heaps_small_buffer");
        o_bool("returns_total", n5 == n2);
        o_lasterr();
        oracle_case_end();
    }

    /* ---- HeapCreate variants ---- */
    ORACLE_CALL(heap, HeapCreate(0, 0x20000, 0x10000));
    oracle_case("create_initial_gt_max");
    o_bool("ret_null", heap == NULL);
    o_lasterr();
    oracle_case_end();
    if (heap) HeapDestroy(heap);

    ORACLE_CALL(heap, HeapCreate(0x80000000, 0, 0));
    oracle_case("create_unknown_flag");
    o_bool("ret_null", heap == NULL);
    o_lasterr();
    oracle_case_end();
    if (heap) HeapDestroy(heap);

    ORACLE_CALL(heap, HeapCreate(HEAP_CREATE_ENABLE_EXECUTE, 0, 0));
    oracle_case("create_executable");
    o_bool("ret_null", heap == NULL);
    o_lasterr();
    oracle_case_end();
    if (heap) {
        MEMORY_BASIC_INFORMATION mbi;
        p = HeapAlloc(heap, 0, 64);
        VirtualQuery(p, &mbi, sizeof mbi);
        oracle_case("executable_heap_block_protect");
        o_str("protect", prot_name(mbi.Protect));
        oracle_case_end();
        HeapDestroy(heap);
    }
    {
        heap = HeapCreate(0, 0, 0);
        MEMORY_BASIC_INFORMATION mbi;
        p = HeapAlloc(heap, 0, 64);
        VirtualQuery(p, &mbi, sizeof mbi);
        oracle_case("plain_heap_block_protect");
        o_str("protect", prot_name(mbi.Protect));
        o_bool("private", mbi.Type == MEM_PRIVATE);
        oracle_case_end();
        HeapDestroy(heap);
    }

    /* ---- growable heap ---- */
    heap = HeapCreate(0, 0, 0);
    ORACLE_CALL(p, HeapAlloc(heap, 0, 4 * 1024 * 1024));
    alloc_case("growable_alloc_4mb", p);
    oracle_case("growable_4mb_size");
    o_bool("size_exact", p && HeapSize(heap, 0, p) == 4 * 1024 * 1024);
    oracle_case_end();
    {
        MEMORY_BASIC_INFORMATION mbi;
        VirtualQuery(p, &mbi, sizeof mbi);
        oracle_case("large_block_own_allocation");
        o_bool("committed", mbi.State == MEM_COMMIT);
        o_bool("region_covers_block", mbi.RegionSize >= 4 * 1024 * 1024);
        oracle_case_end();
    }
    BOOL_CASE("free_large", HeapFree(heap, 0, p));

    /* ---- HeapReAlloc ---- */
    p = HeapAlloc(heap, 0, 100);
    memset(p, 0x5a, 100);
    ORACLE_CALL(q, HeapReAlloc(heap, HEAP_REALLOC_IN_PLACE_ONLY, p, 50));
    oracle_case("realloc_in_place_shrink");
    o_bool("same_pointer", q == p);
    o_lasterr();
    o_u64("size", q ? HeapSize(heap, 0, q) : 0);
    oracle_case_end();
    ORACLE_CALL(q, HeapReAlloc(heap, HEAP_REALLOC_IN_PLACE_ONLY, p, 1024 * 1024));
    oracle_case("realloc_in_place_grow_huge");
    o_bool("ret_null", q == NULL);
    o_lasterr();
    o_u64("size_unchanged", HeapSize(heap, 0, p));
    oracle_case_end();
    ORACLE_CALL(q, HeapReAlloc(heap, HEAP_ZERO_MEMORY, p, 300));
    oracle_case("realloc_grow_zero_memory");
    o_bool("ret_null", q == NULL);
    o_lasterr();
    if (q) {
        unsigned char *b = q;
        int keep = 1, zero = 1;
        for (int i = 0; i < 50; i++) keep &= b[i] == 0x5a;
        for (int i = 100; i < 300; i++) zero &= b[i] == 0;
        o_bool("keeps_first_50", keep);
        o_bool("zero_from_100", zero);
        o_u64("size", HeapSize(heap, 0, q));
    }
    oracle_case_end();
    p = q;
    ORACLE_CALL(q, HeapReAlloc(heap, 0, p, 0));
    oracle_case("realloc_to_zero");
    o_bool("ret_null", q == NULL);
    o_lasterr();
    if (q) o_u64("size", HeapSize(heap, 0, q));
    oracle_case_end();
    if (q) p = q;
    HeapFree(heap, 0, p);

    /* ---- HeapSize / HeapValidate ---- */
    p = HeapAlloc(heap, 0, 37);
    {
        SIZE_T s;
        ORACLE_CALL(s, HeapSize(heap, 0, p));
        oracle_case("size_37");
        o_u64("size", s);
        o_lasterr();
        oracle_case_end();
    }
    BOOL_CASE("validate_heap", HeapValidate(heap, 0, NULL));
    BOOL_CASE("validate_block", HeapValidate(heap, 0, p));
    BOOL_CASE("validate_interior_pointer", HeapValidate(heap, 0, (char *)p + 8));
    BOOL_CASE("validate_process_heap", HeapValidate(GetProcessHeap(), 0, NULL));
    {
        static char not_heap[64];
        BOOL_CASE("validate_static_buffer", HeapValidate(heap, 0, not_heap + 16));
    }
    HeapFree(heap, 0, p);

    /* ---- HeapLock / HeapUnlock / HeapCompact ---- */
    BOOL_CASE("lock", HeapLock(heap));
    BOOL_CASE("lock_recursive", HeapLock(heap));
    BOOL_CASE("unlock", HeapUnlock(heap));
    BOOL_CASE("unlock_second", HeapUnlock(heap));
    {
        SIZE_T c;
        ORACLE_CALL(c, HeapCompact(heap, 0));
        oracle_case("compact");
        o_bool("nonzero", c != 0);
        o_lasterr();
        oracle_case_end();
    }
    HeapDestroy(heap);

    /* ---- HEAP_NO_SERIALIZE heap ---- */
    heap = HeapCreate(HEAP_NO_SERIALIZE, 0, 0);
    BOOL_CASE("no_serialize_lock", HeapLock(heap));
    BOOL_CASE("no_serialize_unlock", HeapUnlock(heap));
    {
        ULONG v = 2;
        BOOL_CASE("no_serialize_set_lfh", HeapSetInformation(heap, HeapCompatibilityInformation, &v, sizeof v));
    }
    HeapDestroy(heap);

    /* ---- HeapWalk ---- */
    heap = HeapCreate(0, 0, 0);
    {
        void *a = HeapAlloc(heap, 0, 100);
        void *b = HeapAlloc(heap, 0, 200);
        void *c = HeapAlloc(heap, 0, 300);
        walk_case("walk_three_blocks", heap, 100, 200, 300);
        HeapFree(heap, 0, b);
        walk_case("walk_after_free_middle", heap, 100, 200, 300);
        HeapFree(heap, 0, a);
        HeapFree(heap, 0, c);
        walk_case("walk_empty_heap", heap, 100, 200, 300);
    }
    HeapDestroy(heap);

    /* ---- HeapQueryInformation / HeapSetInformation ---- */
    {
        ULONG v = 0x55;
        SIZE_T ret = 0x55;
        heap = HeapCreate(0, 0, 0);
        ORACLE_CALL(ok, HeapQueryInformation(heap, HeapCompatibilityInformation, &v, sizeof v, &ret));
        oracle_case("query_compat_new_heap");
        o_bool("ret", ok);
        o_lasterr();
        o_u32("value", v);
        o_u64("ret_len", ret);
        oracle_case_end();
        ORACLE_CALL(ok, HeapQueryInformation(heap, HeapCompatibilityInformation, &v, 2, &ret));
        oracle_case("query_compat_small_buffer");
        o_bool("ret", ok);
        o_lasterr();
        o_u64("ret_len", ret);
        oracle_case_end();
        v = 2;
        BOOL_CASE("set_lfh", HeapSetInformation(heap, HeapCompatibilityInformation, &v, sizeof v));
        v = 0x55;
        HeapQueryInformation(heap, HeapCompatibilityInformation, &v, sizeof v, NULL);
        oracle_case("query_after_set_lfh");
        o_u32("value", v);
        oracle_case_end();
        v = 1;
        BOOL_CASE("set_lal", HeapSetInformation(heap, HeapCompatibilityInformation, &v, sizeof v));
        v = 7;
        BOOL_CASE("set_compat_7", HeapSetInformation(heap, HeapCompatibilityInformation, &v, sizeof v));
        BOOL_CASE("set_compat_small_buffer", HeapSetInformation(heap, HeapCompatibilityInformation, &v, 2));
        BOOL_CASE("set_termination_on_corruption",
                  HeapSetInformation(NULL, HeapEnableTerminationOnCorruption, NULL, 0));
        BOOL_CASE("set_unknown_class", HeapSetInformation(heap, (HEAP_INFORMATION_CLASS)99, &v, sizeof v));
        ORACLE_CALL(ok, HeapQueryInformation(heap, (HEAP_INFORMATION_CLASS)99, &v, sizeof v, &ret));
        oracle_case("query_unknown_class");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        HeapDestroy(heap);
    }

    /* ---- fixed-size heap limits ---- */
    heap = HeapCreate(0, 0x10000, 0x10000);
    ORACLE_CALL(p, HeapAlloc(heap, 0, 0x8000));
    alloc_case("fixed_alloc_half", p);
    ORACLE_CALL(q, HeapAlloc(heap, 0, 0x8000));
    alloc_case("fixed_alloc_second_half", q);
    ORACLE_CALL(q, HeapReAlloc(heap, 0, p, 0x20000));
    oracle_case("fixed_realloc_past_max");
    o_bool("ret_null", q == NULL);
    o_lasterr();
    o_u64("size_unchanged", HeapSize(heap, 0, p));
    oracle_case_end();
    BOOL_CASE("destroy_fixed", HeapDestroy(heap));

    /* Last: if the implementation accepted this, the process heap would be gone. */
    BOOL_CASE("destroy_process_heap", HeapDestroy(GetProcessHeap()));
    p = HeapAlloc(GetProcessHeap(), 0, 16);
    alloc_case("process_heap_still_works", p);

    return oracle_end();
}
