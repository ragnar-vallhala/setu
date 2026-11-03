/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: thread local storage limits and edge cases: how many TLS
 * and FLS indexes a process can allocate, which index values come back,
 * TlsGetValue clearing the last error, values on other threads, freeing
 * indexes twice or out of range, and reusing a freed index (the value is
 * cleared on every thread).
 *
 * Index counts are printed relative to the first index this test gets, so
 * indexes taken by the C runtime before main do not matter.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/procthread/thread-local-storage
 *       https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-tlsalloc
 *       https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-tlsgetvalue
 *       https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-tlsfree
 *       https://learn.microsoft.com/en-us/windows/win32/api/fibersapi/nf-fibersapi-flsalloc
 */
#include "oracle.h"

static DWORD shared_idx;
static void *other_seen;
static DWORD other_err;
static DWORD WINAPI read_other(void *arg)
{
    (void)arg;
    SetLastError(0x1234);
    other_seen = TlsGetValue(shared_idx);
    other_err = GetLastError();
    TlsSetValue(shared_idx, (void *)0x777);
    return 0;
}

static DWORD idx[5000];

int main(void)
{
    oracle_begin("kernel32/tls_limits");
    DWORD i, n = 0;
    void *p;
    BOOL ok;

    /* ---- exhaust TLS ---- */
    for (;;) {
        DWORD x = TlsAlloc();
        if (x == TLS_OUT_OF_INDEXES || n >= 2000) break;
        idx[n++] = x;
    }
    DWORD err = GetLastError();
    {
        DWORD max = 0, first64 = 0, over64 = 0;
        for (i = 0; i < n; i++) {
            if (idx[i] > max) max = idx[i];
            if (idx[i] < 64) first64++;
            else over64++;
        }
        oracle_case("tls_exhaust");
        o_u32("max_index", max);
        /* System DLLs may already hold an expansion slot or two. */
        o_bool("over_64_at_least_1020", over64 >= 1020 && over64 <= 1024);
        o_bool("first_below_64", n && idx[0] < 64);
        o_u32("error", err);
        oracle_case_end();
        oracle_case("tls_order");
        int increasing = 1;
        for (i = 1; i < n; i++) increasing &= idx[i] > idx[i - 1];
        o_bool("increasing", increasing);
        oracle_case_end();
    }
    {
        /* The highest index works like any other. */
        DWORD hi = idx[n - 1];
        ORACLE_CALL(ok, TlsSetValue(hi, (void *)0x55));
        oracle_case("tls_set_highest");
        o_bool("ret", ok);
        o_lasterr();
        o_bool("get", TlsGetValue(hi) == (void *)0x55);
        oracle_case_end();
        /* An expansion-slot index read from a thread that never set it. */
        shared_idx = hi;
        HANDLE th = CreateThread(NULL, 0, read_other, NULL, 0, NULL);
        WaitForSingleObject(th, INFINITE);
        CloseHandle(th);
        oracle_case("tls_highest_other_thread");
        o_bool("null", other_seen == NULL);
        o_u32("error", other_err);
        oracle_case_end();
    }
    {
        /* Free one in the middle, then the next alloc reuses it. */
        DWORD mid = idx[n / 2];
        TlsSetValue(mid, (void *)0x66);
        ORACLE_CALL(ok, TlsFree(mid));
        oracle_case("tls_free_mid");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        DWORD again = TlsAlloc();
        oracle_case("tls_realloc_reuses");
        o_bool("same_index", again == mid);
        o_bool("value_cleared", TlsGetValue(again) == NULL);
        oracle_case_end();
    }
    for (i = 0; i < n; i++) TlsFree(idx[i]);

    ORACLE_CALL(ok, TlsFree(idx[0]));
    oracle_case("tls_free_twice");
    o_bool("ret", ok);
    o_lasterr();
    oracle_case_end();
    ORACLE_CALL(ok, TlsFree(1088));
    oracle_case("tls_free_1088");
    o_bool("ret", ok);
    o_lasterr();
    oracle_case_end();
    ORACLE_CALL(ok, TlsFree(TLS_OUT_OF_INDEXES));
    oracle_case("tls_free_out_of_indexes");
    o_bool("ret", ok);
    o_lasterr();
    oracle_case_end();
    ORACLE_CALL(p, TlsGetValue(1088));
    oracle_case("tls_get_1088");
    o_bool("null", p == NULL);
    o_lasterr();
    oracle_case_end();
    ORACLE_CALL(p, TlsGetValue(1087));
    oracle_case("tls_get_1087_unallocated");
    o_bool("null", p == NULL);
    o_lasterr();
    oracle_case_end();
    ORACLE_CALL(ok, TlsSetValue(1088, (void *)1));
    oracle_case("tls_set_1088");
    o_bool("ret", ok);
    o_lasterr();
    oracle_case_end();
    ORACLE_CALL(ok, TlsSetValue(63, (void *)1));
    oracle_case("tls_set_63_unallocated");
    o_bool("ret", ok);
    o_lasterr();
    oracle_case_end();

    /* ---- TlsGetValue clears the last error ---- */
    {
        DWORD x = TlsAlloc();
        TlsSetValue(x, (void *)0x10);
        SetLastError(0x4321);
        p = TlsGetValue(x);
        oracle_case("tls_get_clears_error");
        o_bool("value", p == (void *)0x10);
        o_u32("error", GetLastError());
        oracle_case_end();
        TlsSetValue(x, NULL);
        SetLastError(0x4321);
        p = TlsGetValue(x);
        oracle_case("tls_get_null_value_clears_error");
        o_bool("null", p == NULL);
        o_u32("error", GetLastError());
        oracle_case_end();
        TlsFree(x);
    }

    /* ---- exhaust FLS ---- */
    {
        DWORD m = 0, x, first = FLS_OUT_OF_INDEXES, last = 0;
        for (;;) {
            x = FlsAlloc(NULL);
            if (x == FLS_OUT_OF_INDEXES || m >= 5000) break;
            if (first == FLS_OUT_OF_INDEXES) first = x;
            last = x;
            idx[m++] = x;
        }
        err = GetLastError();
        oracle_case("fls_exhaust");
        o_u32("last_index", last);
        o_u32("error", err);
        oracle_case_end();
        ORACLE_CALL(ok, FlsSetValue(last, (void *)0x9));
        oracle_case("fls_set_last");
        o_bool("ret", ok);
        o_lasterr();
        o_bool("get", FlsGetValue(last) == (void *)0x9);
        oracle_case_end();
        for (i = 0; i < m; i++) FlsFree(idx[i]);
        ORACLE_CALL(ok, FlsFree(last));
        oracle_case("fls_free_twice");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(p, FlsGetValue(last + 1));
        oracle_case("fls_get_past_last");
        o_bool("null", p == NULL);
        o_lasterr();
        oracle_case_end();
        (void)first;
    }

    return oracle_end();
}
