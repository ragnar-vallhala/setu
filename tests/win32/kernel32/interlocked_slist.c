/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: interlocked singly linked lists (SList): LIFO order, depth,
 * return values of push/pop/flush, InterlockedPushListSListEx, the first
 * entry, and a four-thread push/pop stress whose totals are deterministic.
 * Also the kernel32/ntdll exported interlocked helpers that are real
 * functions on x64 (looked up with GetProcAddress, so the compiler cannot
 * replace them with intrinsics).
 *
 * Entries are identified by the tag stored next to them; pointers are only
 * compared, never printed.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/sync/interlocked-singly-linked-lists
 *       https://learn.microsoft.com/en-us/windows/win32/api/interlockedapi/nf-interlockedapi-initializeslisthead
 *       https://learn.microsoft.com/en-us/windows/win32/api/interlockedapi/nf-interlockedapi-interlockedpushentryslist
 *       https://learn.microsoft.com/en-us/windows/win32/api/interlockedapi/nf-interlockedapi-interlockedpopentryslist
 *       https://learn.microsoft.com/en-us/windows/win32/api/interlockedapi/nf-interlockedapi-interlockedflushslist
 *       https://learn.microsoft.com/en-us/windows/win32/api/interlockedapi/nf-interlockedapi-querydepthslist
 *       https://learn.microsoft.com/en-us/windows/win32/api/interlockedapi/nf-interlockedapi-interlockedpushlistslistex
 *       https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-rtlfirstentryslist
 */
#include "oracle.h"

typedef struct DECLSPEC_ALIGN(16) item {
    SLIST_ENTRY entry;
    int tag;
} item;

static SLIST_HEADER head;
static item items[16];

static int tag_of(PSLIST_ENTRY e)
{
    if (!e) return -1;
    return CONTAINING_RECORD(e, item, entry)->tag;
}

static void list_case(const char *name, PSLIST_ENTRY first)
{
    char buf[256] = "";
    int n = 0;
    for (PSLIST_ENTRY e = first; e && n < 40; e = e->Next, n++) {
        char t[16];
        snprintf(t, sizeof t, "%s%d", n ? "," : "", tag_of(e));
        strcat(buf, t);
    }
    oracle_case(name);
    o_str("chain", buf);
    o_u32("depth", QueryDepthSList(&head));
    oracle_case_end();
}

/* ---- stress ---- */
#define PER_THREAD 2000
static SLIST_HEADER stress;
static item *pool;
static volatile LONG popped_count;
static DWORD WINAPI stress_thread(void *arg)
{
    item *mine = pool + (ULONG_PTR)arg * PER_THREAD;
    for (int i = 0; i < PER_THREAD; i++) {
        InterlockedPushEntrySList(&stress, &mine[i].entry);
        if (i % 3 == 0) {
            PSLIST_ENTRY e = InterlockedPopEntrySList(&stress);
            if (e) {
                InterlockedIncrement(&popped_count);
                CONTAINING_RECORD(e, item, entry)->tag = -1;
            }
        }
    }
    return 0;
}

typedef PSLIST_ENTRY(WINAPI *push_list_ex_fn)(PSLIST_HEADER, PSLIST_ENTRY, PSLIST_ENTRY, ULONG);
typedef PSLIST_ENTRY(WINAPI *first_entry_fn)(const SLIST_HEADER *);

int main(void)
{
    oracle_begin("kernel32/interlocked_slist");
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    push_list_ex_fn push_list_ex = (push_list_ex_fn)(void *)GetProcAddress(k32, "InterlockedPushListSListEx");
    first_entry_fn first_entry = (first_entry_fn)(void *)GetProcAddress(ntdll, "RtlFirstEntrySList");
    for (int i = 0; i < 16; i++) items[i].tag = i;

    oracle_case("exports");
    o_bool("InterlockedPushListSListEx_k32", push_list_ex != NULL);
    o_bool("RtlFirstEntrySList_ntdll", first_entry != NULL);
    o_bool("InterlockedPushListSList_k32", GetProcAddress(k32, "InterlockedPushListSList") != NULL);
    o_bool("InterlockedPushListSListEx_ntdll", GetProcAddress(ntdll, "RtlInterlockedPushListSListEx") != NULL);
    o_bool("InterlockedFlushSList_k32", GetProcAddress(k32, "InterlockedFlushSList") != NULL);
    o_bool("InterlockedIncrement_k32", GetProcAddress(k32, "InterlockedIncrement") != NULL);
    o_bool("InterlockedCompareExchange64_k32", GetProcAddress(k32, "InterlockedCompareExchange64") != NULL);
    o_u32("header_size", sizeof(SLIST_HEADER));
    oracle_case_end();

    memset(&head, 0xcc, sizeof head);
    SetLastError(ORACLE_SENTINEL);
    InitializeSListHead(&head);
    oracle_last_error = GetLastError();
    oracle_case("init");
    o_u32("depth", QueryDepthSList(&head));
    o_bool("header_zero", head.Alignment == 0 && head.Region == 0);
    o_lasterr();
    oracle_case_end();

    PSLIST_ENTRY r;
    ORACLE_CALL(r, InterlockedPopEntrySList(&head));
    oracle_case("pop_empty");
    o_i64("got", tag_of(r));
    o_lasterr();
    oracle_case_end();

    ORACLE_CALL(r, InterlockedPushEntrySList(&head, &items[1].entry));
    oracle_case("push_first_returns_null");
    o_i64("prev", tag_of(r));
    o_lasterr();
    o_u32("depth", QueryDepthSList(&head));
    oracle_case_end();
    r = InterlockedPushEntrySList(&head, &items[2].entry);
    oracle_case("push_second_returns_previous_first");
    o_i64("prev", tag_of(r));
    o_u32("depth", QueryDepthSList(&head));
    oracle_case_end();
    InterlockedPushEntrySList(&head, &items[3].entry);
    if (first_entry) {
        oracle_case("first_entry");
        o_i64("first", tag_of(first_entry(&head)));
        oracle_case_end();
    }
    list_case("chain_after_three_pushes", first_entry ? first_entry(&head) : NULL);

    r = InterlockedPopEntrySList(&head);
    oracle_case("pop_lifo");
    o_i64("got", tag_of(r));
    o_u32("depth", QueryDepthSList(&head));
    oracle_case_end();

    ORACLE_CALL(r, InterlockedFlushSList(&head));
    list_case("flush_returns_chain", r);
    oracle_case("flush_empties");
    o_u32("depth", QueryDepthSList(&head));
    o_lasterr();
    oracle_case_end();
    ORACLE_CALL(r, InterlockedFlushSList(&head));
    oracle_case("flush_empty");
    o_i64("got", tag_of(r));
    o_lasterr();
    oracle_case_end();

    /* Same entry pushed after being popped. */
    InterlockedPushEntrySList(&head, &items[5].entry);
    r = InterlockedPopEntrySList(&head);
    InterlockedPushEntrySList(&head, r);
    list_case("repush_popped", first_entry ? first_entry(&head) : NULL);
    InterlockedFlushSList(&head);

    /* ---- push a pre-linked list ---- */
    if (push_list_ex) {
        items[10].entry.Next = &items[11].entry;
        items[11].entry.Next = &items[12].entry;
        items[12].entry.Next = NULL;
        InterlockedPushEntrySList(&head, &items[4].entry);
        ORACLE_CALL(r, push_list_ex(&head, &items[10].entry, &items[12].entry, 3));
        oracle_case("push_list_ex_returns_previous_first");
        o_i64("prev", tag_of(r));
        o_lasterr();
        oracle_case_end();
        list_case("chain_after_push_list", first_entry ? first_entry(&head) : NULL);

        InterlockedFlushSList(&head);
        items[13].entry.Next = &items[14].entry;
        items[14].entry.Next = NULL;
        r = push_list_ex(&head, &items[13].entry, &items[14].entry, 5);
        oracle_case("push_list_ex_count_is_trusted");
        o_i64("prev", tag_of(r));
        o_u32("depth", QueryDepthSList(&head));
        oracle_case_end();
        InterlockedFlushSList(&head);
    }

    /* ---- depth after many pushes ---- */
    {
        item *many = _aligned_malloc(sizeof(item) * 70000, 16);
        InitializeSListHead(&head);
        for (int i = 0; i < 70000; i++) InterlockedPushEntrySList(&head, &many[i].entry);
        oracle_case("depth_after_70000_pushes");
        o_u32("depth", QueryDepthSList(&head));
        oracle_case_end();
        int n = 0;
        while (InterlockedPopEntrySList(&head)) n++;
        oracle_case("pop_all_70000");
        o_i64("popped", n);
        o_u32("depth", QueryDepthSList(&head));
        oracle_case_end();
        _aligned_free(many);
    }

    /* ---- four threads push and pop concurrently ---- */
    {
        HANDLE th[4];
        pool = _aligned_malloc(sizeof(item) * 4 * PER_THREAD, 16);
        for (int i = 0; i < 4 * PER_THREAD; i++) pool[i].tag = i;
        InitializeSListHead(&stress);
        for (int i = 0; i < 4; i++) th[i] = CreateThread(NULL, 0, stress_thread, (void *)(ULONG_PTR)i, 0, NULL);
        WaitForMultipleObjects(4, th, TRUE, INFINITE);
        for (int i = 0; i < 4; i++) CloseHandle(th[i]);
        int left = 0, dup = 0;
        PSLIST_ENTRY e;
        while ((e = InterlockedPopEntrySList(&stress))) {
            item *it = CONTAINING_RECORD(e, item, entry);
            if (it->tag == -1 || it->tag == -2) dup++;
            it->tag = -2;
            left++;
        }
        int marked = 0;
        for (int i = 0; i < 4 * PER_THREAD; i++) marked += pool[i].tag < 0;
        oracle_case("stress_four_threads");
        o_i64("popped_during", popped_count);
        o_i64("left_after", left);
        o_i64("total", popped_count + left);
        o_i64("duplicates", dup);
        o_i64("every_item_seen_once", marked);
        oracle_case_end();
        _aligned_free(pool);
    }

    return oracle_end();
}
