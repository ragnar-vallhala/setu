/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: clocks as relationships: QueryPerformanceCounter /
 * QueryPerformanceFrequency, GetTickCount / GetTickCount64, interrupt time,
 * Sleep and SleepEx return values, and that the clocks agree with each other
 * over a Sleep.
 *
 * No absolute time value is printed. Durations only appear as wide-margin
 * comparisons (a 200 ms sleep took at least 150 ms and less than 5 s), so
 * scheduler noise cannot change the output.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/profileapi/nf-profileapi-queryperformancecounter
 *       https://learn.microsoft.com/en-us/windows/win32/api/profileapi/nf-profileapi-queryperformancefrequency
 *       https://learn.microsoft.com/en-us/windows/win32/sysinfo/acquiring-high-resolution-time-stamps
 *       https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-gettickcount
 *       https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-gettickcount64
 *       https://learn.microsoft.com/en-us/windows/win32/api/realtimeapiset/nf-realtimeapiset-queryunbiasedinterrupttime
 *       https://learn.microsoft.com/en-us/windows/win32/api/realtimeapiset/nf-realtimeapiset-queryinterrupttime
 *       https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-sleep
 *       https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-sleepex
 *       https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-switchtothread
 *       https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-getsystemtimeadjustment
 */
#include "oracle.h"

typedef void(WINAPI *qit_fn)(ULONGLONG *);

static LONGLONG qpc(void)
{
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return c.QuadPart;
}

static void CALLBACK apc(ULONG_PTR p) { (void)p; }

int main(void)
{
    oracle_begin("kernel32/perf_counter");
    BOOL ok;
    LARGE_INTEGER f, c1, c2;

    f.QuadPart = -1;
    ORACLE_CALL(ok, QueryPerformanceFrequency(&f));
    oracle_case("frequency");
    o_bool("ret", ok);
    o_lasterr();
    o_bool("positive", f.QuadPart > 0);
    o_bool("ge_1mhz", f.QuadPart >= 1000000);
    oracle_case_end();
    {
        LARGE_INTEGER f2;
        QueryPerformanceFrequency(&f2);
        oracle_case("frequency_stable");
        o_bool("same", f2.QuadPart == f.QuadPart);
        oracle_case_end();
    }

    ORACLE_CALL(ok, QueryPerformanceCounter(&c1));
    oracle_case("counter");
    o_bool("ret", ok);
    o_lasterr();
    o_bool("positive", c1.QuadPart > 0);
    oracle_case_end();
    {
        int mono = 1;
        LONGLONG prev = qpc();
        for (int i = 0; i < 100000; i++) {
            LONGLONG now = qpc();
            if (now < prev) mono = 0;
            prev = now;
        }
        oracle_case("counter_monotonic_100000");
        o_bool("monotonic", mono);
        oracle_case_end();
    }

    /* ---- tick counts ---- */
    {
        ULONGLONG t64;
        ORACLE_CALL(t64, GetTickCount64());
        DWORD t32 = GetTickCount();
        oracle_case("tick_count");
        o_lasterr();
        o_bool("t64_positive", t64 > 0);
        o_bool("t32_close_to_low_t64", (DWORD)(t32 - (DWORD)t64) < 1000);
        oracle_case_end();
        int mono = 1;
        ULONGLONG prev = GetTickCount64();
        for (int i = 0; i < 100000; i++) {
            ULONGLONG now = GetTickCount64();
            if (now < prev) mono = 0;
            prev = now;
        }
        oracle_case("tick64_monotonic");
        o_bool("monotonic", mono);
        oracle_case_end();
    }

    /* ---- interrupt time ---- */
    {
        ULONGLONG u1 = 0, u2 = 0;
        ORACLE_CALL(ok, QueryUnbiasedInterruptTime(&u1));
        oracle_case("unbiased_interrupt_time");
        o_bool("ret", ok);
        o_lasterr();
        o_bool("positive", u1 > 0);
        oracle_case_end();
        QueryUnbiasedInterruptTime(&u2);
        oracle_case("unbiased_interrupt_time_monotonic");
        o_bool("monotonic", u2 >= u1);
        oracle_case_end();
        qit_fn qit = (qit_fn)(void *)GetProcAddress(GetModuleHandleW(L"kernelbase.dll"), "QueryInterruptTime");
        qit_fn qitp = (qit_fn)(void *)GetProcAddress(GetModuleHandleW(L"kernelbase.dll"), "QueryInterruptTimePrecise");
        oracle_case("interrupt_time_exports");
        o_bool("QueryInterruptTime", qit != NULL);
        o_bool("QueryInterruptTimePrecise", qitp != NULL);
        o_bool("k32_QueryInterruptTime", GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "QueryInterruptTime") != NULL);
        oracle_case_end();
        if (qit && qitp) {
            ULONGLONG a = 0, b = 0;
            qit(&a);
            qitp(&b);
            ULONGLONG unb = 0;
            QueryUnbiasedInterruptTime(&unb);
            oracle_case("interrupt_time_relations");
            o_bool("precise_ge_coarse_minus_20ms", b + 200000 >= a);
            o_bool("interrupt_ge_unbiased", b + 200000 >= unb);
            oracle_case_end();
        }
    }

    /* ---- Sleep and clocks agree ---- */
    {
        ULONGLONG t0 = GetTickCount64(), u0, u1;
        QueryUnbiasedInterruptTime(&u0);
        QueryPerformanceCounter(&c1);
        SetLastError(ORACLE_SENTINEL);
        Sleep(200);
        oracle_last_error = GetLastError();
        QueryPerformanceCounter(&c2);
        QueryUnbiasedInterruptTime(&u1);
        ULONGLONG t1 = GetTickCount64();
        double qpc_ms = (double)(c2.QuadPart - c1.QuadPart) * 1000.0 / (double)f.QuadPart;
        double tick_ms = (double)(t1 - t0);
        double unb_ms = (double)(u1 - u0) / 10000.0;
        oracle_case("sleep_200");
        o_lasterr();
        o_bool("qpc_ge_150ms", qpc_ms >= 150.0);
        o_bool("qpc_lt_5s", qpc_ms < 5000.0);
        o_bool("tick_agrees_within_50ms", tick_ms > qpc_ms - 50.0 && tick_ms < qpc_ms + 50.0);
        o_bool("unbiased_agrees_within_50ms", unb_ms > qpc_ms - 50.0 && unb_ms < qpc_ms + 50.0);
        oracle_case_end();
    }

    /* ---- Sleep(0) / SleepEx / SwitchToThread ---- */
    {
        SetLastError(ORACLE_SENTINEL);
        Sleep(0);
        oracle_last_error = GetLastError();
        oracle_case("sleep_0");
        o_lasterr();
        oracle_case_end();
        DWORD r;
        ORACLE_CALL(r, SleepEx(0, FALSE));
        oracle_case("sleepex_0_not_alertable");
        o_u32("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, SleepEx(0, TRUE));
        oracle_case("sleepex_0_alertable_no_apc");
        o_u32("ret", r);
        o_lasterr();
        oracle_case_end();
        QueueUserAPC(apc, GetCurrentThread(), 0);
        ORACLE_CALL(r, SleepEx(0, FALSE));
        oracle_case("sleepex_0_not_alertable_apc_queued");
        o_u32("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, SleepEx(0, TRUE));
        oracle_case("sleepex_0_alertable_apc_queued");
        o_u32("ret", r);
        o_lasterr();
        oracle_case_end();
        QueueUserAPC(apc, GetCurrentThread(), 0);
        LONGLONG a = qpc();
        ORACLE_CALL(r, SleepEx(5000, TRUE));
        double ms = (double)(qpc() - a) * 1000.0 / (double)f.QuadPart;
        oracle_case("sleepex_long_alertable_apc_returns_early");
        o_u32("ret", r);
        o_lasterr();
        o_bool("under_1s", ms < 1000.0);
        oracle_case_end();
        ORACLE_CALL(r, SleepEx(1, TRUE));
        oracle_case("sleepex_1_alertable_after_drain");
        o_u32("ret", r);
        o_lasterr();
        oracle_case_end();
        BOOL s;
        ORACLE_CALL(s, SwitchToThread());
        oracle_case("switch_to_thread_last_error");
        o_lasterr();
        oracle_case_end();
        (void)s;
    }

    /* ---- many Sleep(0) take little time ---- */
    {
        LONGLONG a = qpc();
        for (int i = 0; i < 1000; i++) Sleep(0);
        double ms = (double)(qpc() - a) * 1000.0 / (double)f.QuadPart;
        oracle_case("sleep_0_x1000_fast");
        o_bool("under_2s", ms < 2000.0);
        oracle_case_end();
        a = qpc();
        for (int i = 0; i < 20; i++) Sleep(1);
        ms = (double)(qpc() - a) * 1000.0 / (double)f.QuadPart;
        oracle_case("sleep_1_x20_waits");
        o_bool("at_least_10ms", ms >= 10.0);
        o_bool("under_2s", ms < 2000.0);
        oracle_case_end();
    }

    /* ---- time adjustment ---- */
    {
        DWORD adj = 0, inc = 0;
        BOOL disabled = 0x55;
        ORACLE_CALL(ok, GetSystemTimeAdjustment(&adj, &inc, &disabled));
        oracle_case("system_time_adjustment");
        o_bool("ret", ok);
        o_lasterr();
        o_bool("increment_nonzero", inc != 0);
        o_bool("increment_le_1s", inc <= 10000000);
        o_bool("disabled_is_bool", disabled == TRUE || disabled == FALSE);
        oracle_case_end();
    }

    /* ---- precise system time is close to coarse ---- */
    {
        FILETIME a, b;
        GetSystemTimeAsFileTime(&a);
        GetSystemTimePreciseAsFileTime(&b);
        ULONGLONG ua = ((ULONGLONG)a.dwHighDateTime << 32) | a.dwLowDateTime;
        ULONGLONG ub = ((ULONGLONG)b.dwHighDateTime << 32) | b.dwLowDateTime;
        oracle_case("precise_vs_coarse_system_time");
        o_bool("within_1s", ub + 10000000 > ua && ua + 10000000 > ub);
        oracle_case_end();
    }

    return oracle_end();
}
