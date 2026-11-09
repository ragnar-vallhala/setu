/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: WNF (Windows Notification Facility) state names, which the
 * Windows App SDK's FrameworkUdk.dll (shipped with Firefox) imports:
 * NtCreateWnfStateName / NtDeleteWnfStateName for a temporary,
 * process-scoped name, NtUpdateWnfStateData / NtQueryWnfStateData (change
 * stamps, sizes, small buffers, stamp checks), NtQueryWnfStateNameInformation,
 * and RtlSubscribeWnfStateChangeNotification /
 * RtlUnsubscribeWnfStateChangeNotification (which changes reach the
 * callback, and what it is given).
 *
 * Neither MinGW-w64 nor Wine declare these functions. The prototypes follow
 * the public, MIT-licensed phnt headers (ntexapi.h, ntrtl.h); everything
 * printed here is black-box behaviour. State name values are opaque and are
 * never printed.
 *
 * Docs: https://github.com/winsiderss/phnt/blob/master/ntexapi.h
 *       https://github.com/winsiderss/phnt/blob/master/ntrtl.h
 */
#include "oracle.h"

typedef ULONG64 WNF_NAME;
typedef ULONG WNF_STAMP;

typedef LONG (NTAPI *create_fn)(WNF_NAME *, ULONG, ULONG, BOOLEAN, const GUID *, ULONG, PSECURITY_DESCRIPTOR);
typedef LONG (NTAPI *delete_fn)(const WNF_NAME *);
typedef LONG (NTAPI *update_fn)(const WNF_NAME *, const void *, ULONG, const GUID *, const void *, WNF_STAMP, ULONG);
typedef LONG (NTAPI *query_fn)(const WNF_NAME *, const GUID *, const void *, WNF_STAMP *, void *, ULONG *);
typedef LONG (NTAPI *info_fn)(const WNF_NAME *, ULONG, const void *, void *, ULONG);
typedef LONG (NTAPI *user_cb)(WNF_NAME, WNF_STAMP, GUID *, void *, const void *, ULONG);
typedef LONG (NTAPI *subscribe_fn)(void **, WNF_NAME, WNF_STAMP, user_cb, void *, const GUID *, ULONG, ULONG);
typedef LONG (NTAPI *unsubscribe_fn)(void *);

static create_fn pCreate;
static delete_fn pDelete;
static update_fn pUpdate;
static query_fn pQuery;
static info_fn pInfo;
static subscribe_fn pSubscribe;
static unsubscribe_fn pUnsubscribe;

#define LIFETIME_TEMPORARY 3
#define SCOPE_PROCESS      3

#define MAX_CALLS 16
static struct call {
    int name_ok, ctx_ok, type_null;
    WNF_STAMP stamp;
    ULONG len;
    char data[32];
    DWORD thread;
} calls[MAX_CALLS];
static volatile LONG ncalls;
static WNF_NAME cb_name;
static HANDLE cb_event;
static int cb_ctx;

static LONG NTAPI callback(WNF_NAME name, WNF_STAMP stamp, GUID *type, void *ctx, const void *buf, ULONG len)
{
    LONG n = ncalls;
    if (n < MAX_CALLS) {
        calls[n].name_ok = name == cb_name;
        calls[n].ctx_ok = ctx == &cb_ctx;
        calls[n].type_null = type == NULL;
        calls[n].stamp = stamp;
        calls[n].len = len;
        memset(calls[n].data, 0, sizeof(calls[n].data));
        if (buf && len) memcpy(calls[n].data, buf, len < 31 ? len : 31);
        calls[n].thread = GetCurrentThreadId();
        InterlockedIncrement(&ncalls);
    }
    SetEvent(cb_event);
    return 0;
}

/* wait for the callback count to reach N, or for a quiet period */
static void wait_calls(LONG n, DWORD ms)
{
    DWORD start = GetTickCount();
    while (ncalls < n && GetTickCount() - start < ms) WaitForSingleObject(cb_event, 50);
    if (ncalls >= n) Sleep(200); /* anything extra arrives now */
}

static void print_calls(const char *prefix, LONG from)
{
    char k[64];
    LONG i;

    wsprintfA(k, "%s_calls", prefix); o_i64(k, ncalls - from);
    for (i = from; i < ncalls && i < MAX_CALLS; i++) {
        wsprintfA(k, "%s_call%ld_name_ok", prefix, i - from); o_bool(k, calls[i].name_ok);
        wsprintfA(k, "%s_call%ld_ctx_ok", prefix, i - from); o_bool(k, calls[i].ctx_ok);
        wsprintfA(k, "%s_call%ld_type_null", prefix, i - from); o_bool(k, calls[i].type_null);
        wsprintfA(k, "%s_call%ld_stamp", prefix, i - from); o_u32(k, calls[i].stamp);
        wsprintfA(k, "%s_call%ld_len", prefix, i - from); o_u32(k, calls[i].len);
        wsprintfA(k, "%s_call%ld_data", prefix, i - from); o_str(k, calls[i].data);
        wsprintfA(k, "%s_call%ld_other_thread", prefix, i - from); o_bool(k, calls[i].thread != GetCurrentThreadId());
    }
}

static void query(const char *prefix, const WNF_NAME *name, ULONG size)
{
    char buf[64], k[64], text[65];
    WNF_STAMP stamp = 0x7777;
    ULONG len = size;
    LONG st;

    memset(buf, 0xcc, sizeof(buf));
    st = pQuery(name, NULL, NULL, &stamp, size ? buf : NULL, &len);
    wsprintfA(k, "%s_status", prefix); o_status(k, st);
    wsprintfA(k, "%s_stamp", prefix); o_u32(k, stamp);
    wsprintfA(k, "%s_len", prefix); o_u32(k, len);
    if (!st && size && len <= size) {
        memcpy(text, buf, len); text[len] = 0;
        wsprintfA(k, "%s_data", prefix); o_str(k, text);
    }
    wsprintfA(k, "%s_untouched", prefix);
    o_bool(k, (BYTE)buf[0] == 0xcc);
}

static LONG update(const WNF_NAME *name, const char *data, WNF_STAMP match, ULONG check)
{
    return pUpdate(name, data, data ? lstrlenA(data) : 0, NULL, NULL, match, check);
}

static void info(const char *k, const WNF_NAME *name, ULONG cls)
{
    char key[64];
    ULONG v = 0x7777;
    LONG st = pInfo(name, cls, NULL, &v, sizeof(v));
    wsprintfA(key, "%s_status", k); o_status(key, st);
    wsprintfA(key, "%s_value", k); o_u32(key, v);
}

int main(void)
{
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    SECURITY_DESCRIPTOR sd;
    WNF_NAME name = 0, name2 = 0, bogus;
    void *sub = NULL, *sub2 = NULL;
    LONG st, base;
    ULONG good_scope = SCOPE_PROCESS;
    int good_scope_set = 0;

    oracle_begin("ntdll/wnf_state");

    pCreate = (create_fn)(void (*)(void))GetProcAddress(ntdll, "NtCreateWnfStateName");
    pDelete = (delete_fn)(void (*)(void))GetProcAddress(ntdll, "NtDeleteWnfStateName");
    pUpdate = (update_fn)(void (*)(void))GetProcAddress(ntdll, "NtUpdateWnfStateData");
    pQuery = (query_fn)(void (*)(void))GetProcAddress(ntdll, "NtQueryWnfStateData");
    pInfo = (info_fn)(void (*)(void))GetProcAddress(ntdll, "NtQueryWnfStateNameInformation");
    pSubscribe = (subscribe_fn)(void (*)(void))GetProcAddress(ntdll, "RtlSubscribeWnfStateChangeNotification");
    pUnsubscribe = (unsubscribe_fn)(void (*)(void))GetProcAddress(ntdll, "RtlUnsubscribeWnfStateChangeNotification");
    oracle_case("exports");
    o_bool("create", pCreate != NULL);
    o_bool("delete", pDelete != NULL);
    o_bool("update", pUpdate != NULL);
    o_bool("query", pQuery != NULL);
    o_bool("info", pInfo != NULL);
    o_bool("subscribe", pSubscribe != NULL);
    o_bool("unsubscribe", pUnsubscribe != NULL);
    oracle_case_end();
    if (!pCreate || !pDelete || !pUpdate || !pQuery || !pInfo || !pSubscribe || !pUnsubscribe)
        return oracle_end();

    cb_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
    SetSecurityDescriptorDacl(&sd, TRUE, NULL, FALSE);

    /* which scopes a temporary name may have, with a NULL DACL and an
     * explicit one */
    oracle_case("create_scopes");
    {
        static BYTE acl_buf[256];
        SECURITY_DESCRIPTOR sd2;
        PACL acl = (PACL)acl_buf;
        PSID world = NULL;
        SID_IDENTIFIER_AUTHORITY wa = { SECURITY_WORLD_SID_AUTHORITY };
        ULONG scope;
        char k[64];

        AllocateAndInitializeSid(&wa, 1, SECURITY_WORLD_RID, 0, 0, 0, 0, 0, 0, 0, &world);
        InitializeAcl(acl, sizeof(acl_buf), ACL_REVISION);
        AddAccessAllowedAce(acl, ACL_REVISION, GENERIC_ALL, world);
        InitializeSecurityDescriptor(&sd2, SECURITY_DESCRIPTOR_REVISION);
        SetSecurityDescriptorDacl(&sd2, TRUE, acl, FALSE);
        for (scope = 0; scope < 6; scope++) {
            bogus = 0;
            st = pCreate(&bogus, LIFETIME_TEMPORARY, scope, FALSE, NULL, 64, &sd);
            wsprintfA(k, "null_dacl_scope%lu", scope); o_status(k, st);
            if (!st) pDelete(&bogus);
            bogus = 0;
            st = pCreate(&bogus, LIFETIME_TEMPORARY, scope, FALSE, NULL, 64, &sd2);
            wsprintfA(k, "world_dacl_scope%lu", scope); o_status(k, st);
            if (!st) {
                pDelete(&bogus);
                if (!good_scope_set) { good_scope = scope; good_scope_set = 1; }
            }
        }
        o_u32("chosen_scope", good_scope);
        sd = sd2;
    }
    oracle_case_end();

    oracle_case("create");
    st = pCreate(&name, LIFETIME_TEMPORARY, good_scope, FALSE, NULL, 64, &sd);
    o_status("status", st);
    o_bool("name_nonzero", name != 0);
    st = pCreate(&name2, LIFETIME_TEMPORARY, good_scope, FALSE, NULL, 64, &sd);
    o_status("second_status", st);
    o_bool("names_differ", name != name2);
    st = pCreate(&bogus, LIFETIME_TEMPORARY, good_scope, FALSE, NULL, 0x10000, &sd);
    o_status("max_size_64k_status", st);
    if (!st) pDelete(&bogus);
    st = pCreate(&bogus, LIFETIME_TEMPORARY, good_scope, FALSE, NULL, 0x1001, &sd);
    o_status("max_size_4097_status", st);
    if (!st) pDelete(&bogus);
    st = pCreate(&bogus, LIFETIME_TEMPORARY, good_scope, FALSE, NULL, 0x1000, &sd);
    o_status("max_size_4096_status", st);
    if (!st) pDelete(&bogus);
    st = pCreate(&bogus, LIFETIME_TEMPORARY, good_scope, FALSE, NULL, 0, &sd);
    o_status("max_size_0_status", st);
    if (!st) pDelete(&bogus);
    oracle_case_end();
    if (!name) return oracle_end();

    oracle_case("fresh_name");
    query("null_buffer", &name, 0);
    query("buffer", &name, 64);
    info("exists", &name, 0);
    info("subscribers", &name, 1);
    oracle_case_end();

    oracle_case("update_and_query");
    o_status("update_status", update(&name, "hello", 0, FALSE));
    query("after_first", &name, 64);
    o_status("update2_status", update(&name, "abc", 0, FALSE));
    query("after_second", &name, 64);
    query("small_buffer", &name, 2);
    query("null_buffer", &name, 0);
    info("exists", &name, 0);
    oracle_case_end();

    oracle_case("update_limits");
    {
        static char big[80];
        memset(big, 'x', 65);
        o_status("too_long_status", update(&name, big, 0, FALSE));
        big[65] = 0; big[64] = 0;
        o_status("max_len_status", update(&name, big, 0, FALSE));
        query("after_max", &name, 0);
    }
    o_status("empty_status", update(&name, NULL, 0, FALSE));
    query("after_empty", &name, 64);
    oracle_case_end();

    oracle_case("stamp_check");
    o_status("wrong_stamp_status", update(&name, "nope", 1, TRUE));
    query("after_wrong", &name, 64);
    o_status("right_stamp_status", update(&name, "yes", 4, TRUE));
    query("after_right", &name, 64);
    o_status("stamp_ignored_status", update(&name, "free", 1, FALSE));
    query("after_ignored", &name, 64);
    oracle_case_end();

    /* name2 was never written to */
    cb_name = name2;
    oracle_case("subscribe_unwritten");
    base = ncalls;
    st = pSubscribe(&sub2, name2, 0, callback, &cb_ctx, NULL, 0, 0);
    o_status("status", st);
    o_bool("handle_nonzero", sub2 != NULL);
    wait_calls(base + 1, 1000);
    print_calls("initial", base);
    info("subscribers", &name2, 1);
    base = ncalls;
    o_status("update_status", update(&name2, "first", 0, FALSE));
    wait_calls(base + 1, 3000);
    print_calls("first", base);
    oracle_case_end();
    if (sub2) {
        oracle_case("unsubscribe_unwritten");
        o_status("status", pUnsubscribe(sub2));
        base = ncalls;
        update(&name2, "second", 0, FALSE);
        wait_calls(base + 1, 500);
        print_calls("after", base);
        info("subscribers", &name2, 1);
        oracle_case_end();
    }

    /* name has stamp 6 now */
    cb_name = name;
    oracle_case("subscribe_stamp_zero");
    base = ncalls;
    st = pSubscribe(&sub, name, 0, callback, &cb_ctx, NULL, 0, 0);
    o_status("status", st);
    wait_calls(base + 1, 3000);
    print_calls("initial", base);
    oracle_case_end();
    if (sub) pUnsubscribe(sub);

    oracle_case("subscribe_current_stamp");
    sub = NULL;
    base = ncalls;
    st = pSubscribe(&sub, name, 6, callback, &cb_ctx, NULL, 0, 0);
    o_status("status", st);
    wait_calls(base + 1, 1000);
    print_calls("initial", base);
    base = ncalls;
    o_status("update_status", update(&name, "news", 0, FALSE));
    wait_calls(base + 1, 3000);
    print_calls("first", base);
    base = ncalls;
    update(&name, "one", 0, FALSE);
    wait_calls(base + 1, 3000);
    update(&name, "two", 0, FALSE);
    wait_calls(base + 2, 3000);
    print_calls("second", base);
    oracle_case_end();

    oracle_case("subscribe_old_stamp");
    sub2 = NULL;
    base = ncalls;
    st = pSubscribe(&sub2, name, 3, callback, &cb_ctx, NULL, 0, 0);
    o_status("status", st);
    wait_calls(base + 1, 3000);
    print_calls("initial", base);
    if (sub2) pUnsubscribe(sub2);
    oracle_case_end();

    if (sub) {
        oracle_case("unsubscribe");
        o_status("status", pUnsubscribe(sub));
        base = ncalls;
        update(&name, "gone", 0, FALSE);
        wait_calls(base + 1, 500);
        print_calls("after", base);
        oracle_case_end();
    }

    oracle_case("delete");
    o_status("status", pDelete(&name));
    query("after_delete", &name, 64);
    o_status("update_after_delete", update(&name, "x", 0, FALSE));
    info("exists_after_delete", &name, 0);
    o_status("second_delete", pDelete(&name));
    o_status("delete_name2", pDelete(&name2));
    oracle_case_end();

    /* a value nobody created: the low bits mark a temporary name in the
     * encoding phnt describes, but the value is not one that was handed out */
    oracle_case("unknown_name");
    bogus = 0x5ee75ee75ee75ee7ull;
    query("query", &bogus, 64);
    o_status("update", update(&bogus, "x", 0, FALSE));
    info("exists", &bogus, 0);
    oracle_case_end();

    return oracle_end();
}
