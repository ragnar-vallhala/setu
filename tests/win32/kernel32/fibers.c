/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: fibers and fiber local storage: converting threads, creating
 * and switching fibers, fiber data, the order fibers run in, FLS values per
 * fiber, FLS callbacks on DeleteFiber/FlsFree, and the error codes for
 * converting twice or freeing bad indexes.
 *
 * Fibers append letters to a log, which is printed; pointers never are.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/procthread/fibers
 *       https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-convertthreadtofiber
 *       https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-createfiberex
 *       https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-switchtofiber
 *       https://learn.microsoft.com/en-us/windows/win32/api/fibersapi/nf-fibersapi-flsalloc
 *       https://learn.microsoft.com/en-us/windows/win32/api/fibersapi/nf-fibersapi-isthreadafiber
 */
#include "oracle.h"

static char log_buf[128];
static void add(const char *s) { strncat(log_buf, s, sizeof log_buf - strlen(log_buf) - 1); }

static void *main_fiber;
static void *fiber_a, *fiber_b;
static DWORD fls_idx;
static int a_data_ok, a_current_ok, b_saw_fls;

static void WINAPI fiber_a_proc(void *param)
{
    a_data_ok = param == (void *)0x1111 && GetFiberData() == (void *)0x1111;
    a_current_ok = GetCurrentFiber() == fiber_a;
    add("a1");
    FlsSetValue(fls_idx, (void *)0xa);
    SwitchToFiber(fiber_b);
    add("a2");
    SwitchToFiber(main_fiber);
    add("never");
}

static void WINAPI fiber_b_proc(void *param)
{
    (void)param;
    add("b1");
    b_saw_fls = (int)(ULONG_PTR)FlsGetValue(fls_idx);
    FlsSetValue(fls_idx, (void *)0xb);
    SwitchToFiber(fiber_a);
    add("never");
}

static int cb_calls;
static ULONG_PTR cb_last;
static void WINAPI fls_cb(void *data)
{
    cb_calls++;
    cb_last = (ULONG_PTR)data;
}

static void WINAPI fiber_set_fls(void *param)
{
    FlsSetValue((DWORD)(ULONG_PTR)param, (void *)0x77);
    SwitchToFiber(main_fiber);
}

static void WINAPI fiber_float(void *param)
{
    volatile double d = 1.5;
    *(int *)param = (int)(d * 4);
    SwitchToFiber(main_fiber);
}

int main(void)
{
    oracle_begin("kernel32/fibers");
    BOOL ok;
    void *p;

    typedef BOOL(WINAPI * itaf_fn)(void);
    itaf_fn is_fiber = (itaf_fn)(void *)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "IsThreadAFiber");

    oracle_case("is_fiber_before");
    o_bool("ret", is_fiber());
    oracle_case_end();

    ORACLE_CALL(ok, ConvertFiberToThread());
    oracle_case("convert_to_thread_when_not_fiber");
    o_bool("ret", ok);
    o_lasterr();
    oracle_case_end();

    ORACLE_CALL(main_fiber, ConvertThreadToFiber((void *)0x5150));
    oracle_case("convert_thread_to_fiber");
    o_bool("ret_null", main_fiber == NULL);
    o_lasterr();
    o_bool("is_current", GetCurrentFiber() == main_fiber);
    o_bool("data", GetFiberData() == (void *)0x5150);
    o_bool("is_fiber", is_fiber());
    oracle_case_end();

    ORACLE_CALL(p, ConvertThreadToFiber(NULL));
    oracle_case("convert_twice");
    o_bool("ret_null", p == NULL);
    o_lasterr();
    oracle_case_end();
    ORACLE_CALL(p, ConvertThreadToFiberEx(NULL, 0));
    oracle_case("convert_ex_twice");
    o_bool("ret_null", p == NULL);
    o_lasterr();
    oracle_case_end();

    /* ---- two fibers ping-pong ---- */
    fls_idx = FlsAlloc(NULL);
    FlsSetValue(fls_idx, (void *)0x5);
    ORACLE_CALL(fiber_a, CreateFiber(0, fiber_a_proc, (void *)0x1111));
    oracle_case("create_fiber");
    o_bool("ret_null", fiber_a == NULL);
    o_lasterr();
    oracle_case_end();
    fiber_b = CreateFiberEx(0x10000, 0x20000, 0, fiber_b_proc, NULL);
    SwitchToFiber(fiber_a);
    oracle_case("ping_pong");
    o_str("log", log_buf);
    o_bool("a_data", a_data_ok);
    o_bool("a_current", a_current_ok);
    o_i64("b_saw_fls", b_saw_fls);
    o_hex32("main_fls", (DWORD)(ULONG_PTR)FlsGetValue(fls_idx));
    o_bool("back_on_main", GetCurrentFiber() == main_fiber);
    oracle_case_end();
    DeleteFiber(fiber_a);
    DeleteFiber(fiber_b);
    FlsFree(fls_idx);

    /* ---- FLS callbacks ---- */
    {
        DWORD idx;
        ORACLE_CALL(idx, FlsAlloc(fls_cb));
        oracle_case("fls_alloc_with_callback");
        o_bool("valid", idx != FLS_OUT_OF_INDEXES);
        o_lasterr();
        oracle_case_end();
        void *f = CreateFiber(0, fiber_set_fls, (void *)(ULONG_PTR)idx);
        SwitchToFiber(f);
        cb_calls = 0;
        DeleteFiber(f);
        oracle_case("delete_fiber_runs_callback");
        o_i64("calls", cb_calls);
        o_hex32("data", (DWORD)cb_last);
        oracle_case_end();

        f = CreateFiber(0, fiber_set_fls, (void *)(ULONG_PTR)idx);
        cb_calls = 0;
        DeleteFiber(f); /* never ran: no value set */
        oracle_case("delete_unstarted_fiber");
        o_i64("calls", cb_calls);
        oracle_case_end();

        FlsSetValue(idx, NULL);
        cb_calls = 0;
        FlsFree(idx);
        oracle_case("fls_free_null_value");
        o_i64("calls", cb_calls);
        oracle_case_end();

        idx = FlsAlloc(fls_cb);
        FlsSetValue(idx, (void *)0x99);
        cb_calls = 0;
        ORACLE_CALL(ok, FlsFree(idx));
        oracle_case("fls_free_runs_callback");
        o_bool("ret", ok);
        o_lasterr();
        o_i64("calls", cb_calls);
        o_hex32("data", (DWORD)cb_last);
        oracle_case_end();

        ORACLE_CALL(ok, FlsFree(idx));
        oracle_case("fls_free_twice");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(p, FlsGetValue(idx));
        oracle_case("fls_get_freed");
        o_bool("ret_null", p == NULL);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(ok, FlsSetValue(idx, (void *)1));
        oracle_case("fls_set_freed");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(p, FlsGetValue(4096));
        oracle_case("fls_get_out_of_range");
        o_bool("ret_null", p == NULL);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(ok, FlsFree(FLS_OUT_OF_INDEXES));
        oracle_case("fls_free_out_of_indexes");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();

        idx = FlsAlloc(NULL);
        ORACLE_CALL(p, FlsGetValue(idx));
        oracle_case("fls_get_fresh_index");
        o_bool("ret_null", p == NULL);
        o_lasterr();
        oracle_case_end();
        FlsFree(idx);
    }

    /* ---- floating-point fiber ---- */
    {
        int out = 0;
        void *f;
        ORACLE_CALL(f, CreateFiberEx(0, 0, FIBER_FLAG_FLOAT_SWITCH, fiber_float, &out));
        oracle_case("create_fiber_float_switch");
        o_bool("ret_null", f == NULL);
        o_lasterr();
        oracle_case_end();
        if (f) {
            SwitchToFiber(f);
            DeleteFiber(f);
        }
        oracle_case("float_fiber_ran");
        o_i64("out", out);
        oracle_case_end();
        ORACLE_CALL(f, CreateFiberEx(0, 0, 0x2, fiber_float, &out));
        oracle_case("create_fiber_unknown_flag");
        o_bool("ret_null", f == NULL);
        o_lasterr();
        oracle_case_end();
        if (f) DeleteFiber(f);
    }

    ORACLE_CALL(ok, ConvertFiberToThread());
    oracle_case("convert_back_to_thread");
    o_bool("ret", ok);
    o_lasterr();
    o_bool("is_fiber_after", is_fiber());
    oracle_case_end();

    ORACLE_CALL(p, ConvertThreadToFiberEx((void *)0x42, FIBER_FLAG_FLOAT_SWITCH));
    oracle_case("convert_ex_float");
    o_bool("ret_null", p == NULL);
    o_lasterr();
    o_bool("data", GetFiberData() == (void *)0x42);
    oracle_case_end();
    ConvertFiberToThread();
    ORACLE_CALL(p, ConvertThreadToFiberEx(NULL, 0x2));
    oracle_case("convert_ex_unknown_flag");
    o_bool("ret_null", p == NULL);
    o_lasterr();
    oracle_case_end();
    if (p) ConvertFiberToThread();

    return oracle_end();
}
