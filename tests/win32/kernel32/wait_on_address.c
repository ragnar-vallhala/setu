/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: WaitOnAddress / WakeByAddressSingle / WakeByAddressAll:
 * returning at once when the value already differs, timeouts and their
 * error code, every allowed and some invalid sizes, unaligned addresses,
 * and waking waiter threads (joined, with the usual re-check loop, so
 * spurious wakes do not change the output).
 *
 * The functions live in kernelbase (api-ms-win-core-synch-l1-2-0) and are
 * resolved at run time.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-waitonaddress
 *       https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-wakebyaddresssingle
 *       https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-wakebyaddressall
 */
#include "oracle.h"

typedef BOOL(WINAPI *woa_fn)(volatile void *, void *, SIZE_T, DWORD);
typedef void(WINAPI *wba_fn)(void *);
static woa_fn woa;
static wba_fn wake_single, wake_all;

static void woa_case(const char *name, volatile void *addr, void *cmp, SIZE_T size, DWORD timeout)
{
    BOOL ok;
    ORACLE_CALL(ok, woa(addr, cmp, size, timeout));
    oracle_case(name);
    o_bool("ret", ok);
    o_lasterr();
    oracle_case_end();
}

static volatile LONG flag;
static volatile LONG woken;
static volatile LONG waiting;
static DWORD WINAPI waiter(void *arg)
{
    (void)arg;
    LONG zero = 0;
    InterlockedIncrement(&waiting);
    while (flag == 0) woa(&flag, &zero, sizeof flag, 10000);
    InterlockedIncrement(&woken);
    return 0;
}

int main(void)
{
    oracle_begin("kernel32/wait_on_address");
    HMODULE kb = GetModuleHandleW(L"kernelbase.dll");
    woa = (woa_fn)(void *)GetProcAddress(kb, "WaitOnAddress");
    wake_single = (wba_fn)(void *)GetProcAddress(kb, "WakeByAddressSingle");
    wake_all = (wba_fn)(void *)GetProcAddress(kb, "WakeByAddressAll");
    oracle_case("exports");
    o_bool("wait", woa != NULL);
    o_bool("wake_single", wake_single != NULL);
    o_bool("wake_all", wake_all != NULL);
    oracle_case_end();
    if (!woa || !wake_single || !wake_all) return oracle_end();

    {
        volatile LONG v = 5;
        LONG other = 6, same = 5;
        woa_case("differs_returns_at_once", &v, &other, 4, INFINITE);
        woa_case("same_timeout_0", &v, &same, 4, 0);
        woa_case("same_timeout_10", &v, &same, 4, 10);
    }
    {
        volatile BYTE b = 1;
        BYTE b2 = 1, b3 = 2;
        woa_case("size_1_same", &b, &b2, 1, 0);
        woa_case("size_1_differs", &b, &b3, 1, 0);
        volatile SHORT s = 1;
        SHORT s2 = 1;
        woa_case("size_2_same", &s, &s2, 2, 0);
        volatile LONGLONG q = 0x100000000ll;
        LONGLONG q2 = 0x100000000ll, q3 = 0;
        woa_case("size_8_same", &q, &q2, 8, 0);
        woa_case("size_8_high_half_differs", &q, &q3, 8, 0);
        volatile LONG v = 0;
        LONG z = 0;
        woa_case("size_0", &v, &z, 0, 0);
        woa_case("size_3", &v, &z, 3, 0);
        woa_case("size_16", &q, &q2, 16, 0);
        /* Only the low byte is compared for size 1. */
        volatile LONG w = 0x1201;
        LONG w2 = 0x3401;
        woa_case("size_1_compares_low_byte_only", &w, &w2, 1, 0);
    }
    {
        char buf[16] = {0};
        LONG z = 0;
        woa_case("unaligned_4", (volatile void *)(buf + 1), &z, 4, 0);
        woa_case("unaligned_2", (volatile void *)(buf + 1), &z, 2, 0);
        LONGLONG zz = 0;
        woa_case("unaligned_8_by_4", (volatile void *)(buf + 4), &zz, 8, 0);
    }

    {
        volatile LONG nobody = 0;
        wake_single((void *)&nobody);
        wake_all((void *)&nobody);
        oracle_case("wake_without_waiters");
        o_bool("survived", TRUE);
        oracle_case_end();
    }

    /* ---- wake waiters ---- */
    {
        HANDLE th[3];
        flag = 0;
        woken = 0;
        for (int i = 0; i < 3; i++) th[i] = CreateThread(NULL, 0, waiter, NULL, 0, NULL);
        while (waiting < 3) SwitchToThread();
        InterlockedExchange(&flag, 1);
        wake_all((void *)&flag);
        DWORD w = WaitForMultipleObjects(3, th, TRUE, 20000);
        oracle_case("wake_all_three");
        o_bool("joined", w <= WAIT_OBJECT_0 + 2);
        o_i64("woken", woken);
        oracle_case_end();
        for (int i = 0; i < 3; i++) CloseHandle(th[i]);

        flag = 0;
        woken = 0;
        waiting = 0;
        th[0] = CreateThread(NULL, 0, waiter, NULL, 0, NULL);
        while (waiting < 1) SwitchToThread();
        InterlockedExchange(&flag, 1);
        wake_single((void *)&flag);
        w = WaitForSingleObject(th[0], 20000);
        oracle_case("wake_single_one");
        o_bool("joined", w == WAIT_OBJECT_0);
        o_i64("woken", woken);
        oracle_case_end();
        CloseHandle(th[0]);
    }

    return oracle_end();
}
