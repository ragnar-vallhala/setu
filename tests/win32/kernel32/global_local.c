/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: the Global* and Local* memory families: fixed versus moveable
 * blocks, lock counts and GlobalFlags/LocalFlags, GlobalHandle/LocalHandle,
 * GMEM_ZEROINIT, discardable and discarded blocks, GlobalReAlloc with
 * GMEM_MODIFY, and the error codes of unlocking and freeing.
 *
 * Handles and pointers are only compared with each other, never printed.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-globalalloc
 *       https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-globallock
 *       https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-globalunlock
 *       https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-globalflags
 *       https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-globalhandle
 *       https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-globalsize
 *       https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-globalrealloc
 *       https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-globalfree
 *       https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-globaldiscard
 *       https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-localalloc
 *       https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-localflags
 *       https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-localunlock
 *       https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-localrealloc
 */
#include "oracle.h"

static void handle_case(const char *name, HGLOBAL h)
{
    oracle_case(name);
    o_bool("ret_null", h == NULL);
    o_lasterr();
    oracle_case_end();
}

#define FLAGS_CASE(name, expr)    \
    do {                          \
        UINT f_;                  \
        ORACLE_CALL(f_, (expr));  \
        oracle_case(name);        \
        o_hex32("flags", f_);     \
        o_lasterr();              \
        oracle_case_end();        \
    } while (0)

#define BOOL_CASE(name, expr)     \
    do {                          \
        BOOL ok_;                 \
        ORACLE_CALL(ok_, (expr)); \
        oracle_case(name);        \
        o_bool("ret", ok_);       \
        o_lasterr();              \
        oracle_case_end();        \
    } while (0)

#define SIZE_CASE(name, expr)     \
    do {                          \
        SIZE_T s_;                \
        ORACLE_CALL(s_, (expr));  \
        oracle_case(name);        \
        o_u64("size", s_);        \
        o_lasterr();              \
        oracle_case_end();        \
    } while (0)

int main(void)
{
    oracle_begin("kernel32/global_local");
    HGLOBAL g, g2;
    void *p, *p2;

    /* ---- GMEM_FIXED ---- */
    ORACLE_CALL(g, GlobalAlloc(GMEM_FIXED, 10));
    handle_case("fixed_alloc", g);
    oracle_case("fixed_handle_is_pointer");
    o_bool("handle_eq_pointer", GlobalHandle(g) == g);
    o_bool("lock_eq_handle", GlobalLock(g) == g);
    oracle_case_end();
    FLAGS_CASE("fixed_flags_after_lock", GlobalFlags(g));
    BOOL_CASE("fixed_unlock", GlobalUnlock(g));
    BOOL_CASE("fixed_unlock_again", GlobalUnlock(g));
    SIZE_CASE("fixed_size", GlobalSize(g));
    ORACLE_CALL(g2, GlobalFree(g));
    handle_case("fixed_free", g2);

    /* ---- GMEM_MOVEABLE ---- */
    ORACLE_CALL(g, GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, 40));
    handle_case("moveable_alloc", g);
    FLAGS_CASE("moveable_flags_new", GlobalFlags(g));
    ORACLE_CALL(p, GlobalLock(g));
    oracle_case("moveable_lock");
    o_bool("ret_null", p == NULL);
    o_bool("pointer_ne_handle", p != g);
    o_lasterr();
    {
        int zero = 1;
        for (int i = 0; i < 40; i++) zero &= ((unsigned char *)p)[i] == 0;
        o_bool("zeroinit", zero);
    }
    oracle_case_end();
    FLAGS_CASE("moveable_flags_locked_once", GlobalFlags(g));
    p2 = GlobalLock(g);
    oracle_case("moveable_lock_twice_same_pointer");
    o_bool("same", p2 == p);
    oracle_case_end();
    FLAGS_CASE("moveable_flags_locked_twice", GlobalFlags(g));
    oracle_case("moveable_global_handle_from_pointer");
    o_bool("handle_matches", GlobalHandle(p) == g);
    oracle_case_end();
    SIZE_CASE("moveable_size", GlobalSize(g));
    BOOL_CASE("moveable_unlock_1", GlobalUnlock(g));
    BOOL_CASE("moveable_unlock_2", GlobalUnlock(g));
    BOOL_CASE("moveable_unlock_not_locked", GlobalUnlock(g));
    FLAGS_CASE("moveable_flags_after_unlocks", GlobalFlags(g));

    /* ---- GlobalReAlloc ---- */
    ORACLE_CALL(g2, GlobalReAlloc(g, 4000, GMEM_MOVEABLE | GMEM_ZEROINIT));
    oracle_case("moveable_realloc_grow");
    o_bool("ret_null", g2 == NULL);
    o_bool("same_handle", g2 == g);
    o_lasterr();
    o_u64("size", GlobalSize(g2));
    oracle_case_end();
    if (g2) g = g2;
    ORACLE_CALL(g2, GlobalReAlloc(g, 0, GMEM_MODIFY | GMEM_DISCARDABLE));
    oracle_case("modify_discardable");
    o_bool("ret_null", g2 == NULL);
    o_bool("same_handle", g2 == g);
    o_lasterr();
    oracle_case_end();
    FLAGS_CASE("flags_after_modify_discardable", GlobalFlags(g));
    ORACLE_CALL(g2, GlobalDiscard(g));
    oracle_case("discard");
    o_bool("ret_null", g2 == NULL);
    o_bool("same_handle", g2 == g);
    o_lasterr();
    oracle_case_end();
    FLAGS_CASE("flags_after_discard", GlobalFlags(g));
    SIZE_CASE("size_after_discard", GlobalSize(g));
    ORACLE_CALL(p, GlobalLock(g));
    oracle_case("lock_discarded");
    o_bool("ret_null", p == NULL);
    o_lasterr();
    oracle_case_end();
    ORACLE_CALL(g2, GlobalReAlloc(g, 64, GMEM_MOVEABLE));
    oracle_case("realloc_discarded");
    o_bool("ret_null", g2 == NULL);
    o_bool("same_handle", g2 == g);
    o_lasterr();
    oracle_case_end();
    if (g2) g = g2;
    FLAGS_CASE("flags_after_realloc_discarded", GlobalFlags(g));
    SIZE_CASE("size_after_realloc_discarded", GlobalSize(g));
    GlobalFree(g);

    /* ---- zero-size moveable ---- */
    ORACLE_CALL(g, GlobalAlloc(GMEM_MOVEABLE, 0));
    handle_case("moveable_zero_size", g);
    FLAGS_CASE("moveable_zero_flags", GlobalFlags(g));
    SIZE_CASE("moveable_zero_size_query", GlobalSize(g));
    ORACLE_CALL(p, GlobalLock(g));
    oracle_case("moveable_zero_lock");
    o_bool("ret_null", p == NULL);
    o_lasterr();
    oracle_case_end();
    ORACLE_CALL(g2, GlobalFree(g));
    handle_case("moveable_zero_free", g2);

    /* ---- fixed zero-size ---- */
    ORACLE_CALL(g, GlobalAlloc(GMEM_FIXED, 0));
    handle_case("fixed_zero_size", g);
    SIZE_CASE("fixed_zero_size_query", GlobalSize(g));
    GlobalFree(g);

    /* ---- free while locked ---- */
    g = GlobalAlloc(GMEM_MOVEABLE, 16);
    GlobalLock(g);
    ORACLE_CALL(g2, GlobalFree(g));
    oracle_case("free_locked_moveable");
    o_bool("ret_null", g2 == NULL);
    o_bool("returns_handle", g2 == g);
    o_lasterr();
    oracle_case_end();

    /* ---- fixed realloc ---- */
    /* Shrinking a fixed block in place (flags 0) succeeds or fails with
     * error 8 depending on the heap's state, so it is not tested. */
    g = GlobalAlloc(GMEM_FIXED, 16);
    ORACLE_CALL(g2, GlobalReAlloc(g, 1 << 20, GMEM_MOVEABLE));
    oracle_case("fixed_realloc_grow_moveable_flag");
    o_bool("ret_null", g2 == NULL);
    o_lasterr();
    o_u64("size", g2 ? GlobalSize(g2) : 0);
    o_bool("still_fixed", g2 && GlobalHandle(g2) == g2);
    oracle_case_end();
    if (g2) g = g2;
    ORACLE_CALL(g2, GlobalReAlloc(g, 0, GMEM_MODIFY | GMEM_MOVEABLE));
    oracle_case("fixed_modify_to_moveable");
    o_bool("ret_null", g2 == NULL);
    o_bool("same", g2 == g);
    o_lasterr();
    oracle_case_end();
    if (g2) g = g2;
    FLAGS_CASE("flags_after_modify_to_moveable", GlobalFlags(g));
    GlobalFree(g);

    /* ---- Local* family ---- */
    HLOCAL l, l2;
    ORACLE_CALL(l, LocalAlloc(LMEM_MOVEABLE | LMEM_ZEROINIT, 24));
    handle_case("local_moveable_alloc", l);
    FLAGS_CASE("local_flags_new", LocalFlags(l));
    p = LocalLock(l);
    p2 = LocalLock(l);
    oracle_case("local_lock_twice");
    o_bool("same_pointer", p == p2);
    o_bool("pointer_ne_handle", p != l);
    o_bool("local_handle_matches", LocalHandle(p) == l);
    oracle_case_end();
    FLAGS_CASE("local_flags_locked_twice", LocalFlags(l));
    SIZE_CASE("local_size", LocalSize(l));
    BOOL_CASE("local_unlock_1", LocalUnlock(l));
    BOOL_CASE("local_unlock_2", LocalUnlock(l));
    BOOL_CASE("local_unlock_not_locked", LocalUnlock(l));
    ORACLE_CALL(l2, LocalReAlloc(l, 100, LMEM_MOVEABLE));
    oracle_case("local_realloc_grow");
    o_bool("ret_null", l2 == NULL);
    o_bool("same_handle", l2 == l);
    o_lasterr();
    o_u64("size", l2 ? LocalSize(l2) : 0);
    oracle_case_end();
    if (l2) l = l2;
    ORACLE_CALL(l2, LocalFree(l));
    handle_case("local_free", l2);

    ORACLE_CALL(l, LocalAlloc(LMEM_FIXED, 12));
    handle_case("local_fixed_alloc", l);
    FLAGS_CASE("local_fixed_flags", LocalFlags(l));
    BOOL_CASE("local_fixed_unlock", LocalUnlock(l));
    oracle_case("local_fixed_handle_is_pointer");
    o_bool("handle_eq_pointer", LocalHandle(l) == l);
    oracle_case_end();
    LocalFree(l);

    ORACLE_CALL(l, LocalAlloc(LMEM_MOVEABLE | LMEM_DISCARDABLE, 8));
    handle_case("local_discardable_alloc", l);
    FLAGS_CASE("local_discardable_flags", LocalFlags(l));
    ORACLE_CALL(l2, LocalDiscard(l));
    oracle_case("local_discard");
    o_bool("ret_null", l2 == NULL);
    o_bool("same_handle", l2 == l);
    o_lasterr();
    oracle_case_end();
    FLAGS_CASE("local_flags_after_discard", LocalFlags(l));
    LocalFree(l);

    /* GlobalHandle on a plain process-heap pointer. */
    {
        void *hp = HeapAlloc(GetProcessHeap(), 0, 32);
        HGLOBAL hg;
        ORACLE_CALL(hg, GlobalHandle(hp));
        oracle_case("global_handle_heap_pointer");
        o_bool("returns_pointer", hg == hp);
        o_bool("ret_null", hg == NULL);
        o_lasterr();
        oracle_case_end();
        SIZE_CASE("global_size_heap_pointer", GlobalSize(hp));
        HeapFree(GetProcessHeap(), 0, hp);
    }

    /* NULL handles. */
    ORACLE_CALL(p, GlobalLock(NULL));
    oracle_case("lock_null");
    o_bool("ret_null", p == NULL);
    o_lasterr();
    oracle_case_end();
    BOOL_CASE("unlock_null", GlobalUnlock(NULL));
    SIZE_CASE("size_null", GlobalSize(NULL));
    FLAGS_CASE("flags_null", GlobalFlags(NULL));
    ORACLE_CALL(g2, GlobalFree(NULL));
    handle_case("free_null", g2);
    ORACLE_CALL(l2, LocalFree(NULL));
    handle_case("local_free_null", l2);

    /* Freed moveable handle reused: last, it may touch stale state. */
    g = GlobalAlloc(GMEM_MOVEABLE, 16);
    GlobalFree(g);
    FLAGS_CASE("flags_freed_handle", GlobalFlags(g));
    SIZE_CASE("size_freed_handle", GlobalSize(g));
    ORACLE_CALL(g2, GlobalFree(g));
    oracle_case("free_freed_handle");
    o_bool("ret_null", g2 == NULL);
    o_bool("returns_handle", g2 == g);
    o_lasterr();
    oracle_case_end();

    return oracle_end();
}
