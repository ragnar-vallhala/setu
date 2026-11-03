/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: one-time initialisation: InitOnceExecuteOnce (callback runs
 * once, failure lets it run again, context values), InitOnceBeginInitialize /
 * InitOnceComplete in synchronous and asynchronous mode, INIT_ONCE_CHECK_ONLY,
 * INIT_ONCE_INIT_FAILED and invalid flag combinations.
 *
 * Contexts are compared with the values the test put in, never printed as
 * pointers. Single-threaded except one case that joins its threads.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/sync/one-time-initialization
 *       https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-initonceexecuteonce
 *       https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-initoncebegininitialize
 *       https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-initoncecomplete
 *       https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-initonceinitialize
 *       https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nc-synchapi-pinit_once_fn
 */
#include "oracle.h"

static int calls;
static BOOL cb_result;
static ULONG_PTR cb_context;
static DWORD cb_set_error;
static ULONG_PTR cb_param_seen;

static BOOL CALLBACK init_fn(INIT_ONCE *once, void *param, void **ctx)
{
    (void)once;
    calls++;
    cb_param_seen = (ULONG_PTR)param;
    if (ctx) *ctx = (void *)cb_context;
    if (cb_set_error) SetLastError(cb_set_error);
    return cb_result;
}

static void exec_case(const char *name, INIT_ONCE *once, int with_ctx)
{
    void *ctx = (void *)0x5555;
    BOOL ok;
    calls = 0;
    ORACLE_CALL(ok, InitOnceExecuteOnce(once, init_fn, (void *)0x77, with_ctx ? &ctx : NULL));
    oracle_case(name);
    o_bool("ret", ok);
    o_lasterr();
    o_i64("callback_calls", calls);
    if (calls) o_bool("param_passed", cb_param_seen == 0x77);
    if (with_ctx) {
        o_bool("ctx_untouched", ctx == (void *)0x5555);
        o_bool("ctx_is_0x1230", ctx == (void *)0x1230);
        o_bool("ctx_null", ctx == NULL);
        o_bool("ctx_is_0x1233", ctx == (void *)0x1233);
    }
    oracle_case_end();
}

static void begin_case(const char *name, INIT_ONCE *once, DWORD flags)
{
    BOOL ok, pending = 0x55;
    void *ctx = (void *)0x5555;
    ORACLE_CALL(ok, InitOnceBeginInitialize(once, flags, &pending, &ctx));
    oracle_case(name);
    o_bool("ret", ok);
    o_lasterr();
    o_i64("pending", pending);
    o_bool("ctx_untouched", ctx == (void *)0x5555);
    o_bool("ctx_is_0x1230", ctx == (void *)0x1230);
    o_bool("ctx_null", ctx == NULL);
    oracle_case_end();
}

static void complete_case(const char *name, INIT_ONCE *once, DWORD flags, ULONG_PTR ctx)
{
    BOOL ok;
    ORACLE_CALL(ok, InitOnceComplete(once, flags, (void *)ctx));
    oracle_case(name);
    o_bool("ret", ok);
    o_lasterr();
    oracle_case_end();
}

/* Concurrent ExecuteOnce from several threads: the callback runs once. */
static INIT_ONCE shared_once = INIT_ONCE_STATIC_INIT;
static volatile LONG shared_calls;
static HANDLE go;
static BOOL CALLBACK slow_init(INIT_ONCE *once, void *param, void **ctx)
{
    (void)once;
    (void)param;
    InterlockedIncrement(&shared_calls);
    *ctx = (void *)0x4440;
    return TRUE;
}
static DWORD WINAPI exec_thread(void *arg)
{
    (void)arg;
    void *ctx = NULL;
    WaitForSingleObject(go, INFINITE);
    BOOL ok = InitOnceExecuteOnce(&shared_once, slow_init, NULL, &ctx);
    return ok && ctx == (void *)0x4440;
}

static DWORD run_child(const char *mode)
{
    char self[MAX_PATH], cmd[MAX_PATH + 64];
    STARTUPINFOA si = {.cb = sizeof si};
    PROCESS_INFORMATION pi;
    DWORD code = 0xffffffff;
    GetModuleFileNameA(NULL, self, MAX_PATH);
    snprintf(cmd, sizeof cmd, "\"%s\" %s", self, mode);
    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) return 0xfffffffe;
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return code;
}

/* Child: exit code 0x100 | flags, or the exception's exit code. */
static int child_lowbits(void)
{
    INIT_ONCE o = INIT_ONCE_STATIC_INIT;
    void *ctx = NULL;
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    cb_result = TRUE;
    cb_context = 0x1233;
    SetLastError(0);
    BOOL ok = InitOnceExecuteOnce(&o, init_fn, NULL, &ctx);
    return 0x100 | (ok ? 1 : 0) | (GetLastError() ? 2 : 0);
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "lowbits")) return child_lowbits();
    oracle_begin("kernel32/init_once");

    {
        INIT_ONCE o;
        memset(&o, 0xcc, sizeof o);
        InitOnceInitialize(&o);
        oracle_case("initialize_zeroes");
        o_bool("word_zero", o.Ptr == NULL);
        o_u32("reserved_bits", INIT_ONCE_CTX_RESERVED_BITS);
        oracle_case_end();
    }

    /* ---- ExecuteOnce ---- */
    {
        INIT_ONCE o = INIT_ONCE_STATIC_INIT;
        cb_result = TRUE;
        cb_context = 0x1230;
        cb_set_error = 0;
        exec_case("exec_first", &o, 1);
        exec_case("exec_second_no_callback", &o, 1);
        exec_case("exec_third_null_ctx", &o, 0);
        begin_case("begin_after_exec", &o, 0);
        begin_case("begin_check_only_after_exec", &o, INIT_ONCE_CHECK_ONLY);
    }
    {
        INIT_ONCE o = INIT_ONCE_STATIC_INIT;
        cb_result = FALSE;
        cb_context = 0x1230;
        cb_set_error = 0;
        exec_case("exec_callback_fails", &o, 1);
        cb_set_error = ERROR_ACCESS_DENIED;
        exec_case("exec_callback_fails_sets_error", &o, 1);
        cb_result = TRUE;
        cb_set_error = 0;
        exec_case("exec_after_failures_runs_again", &o, 1);
        exec_case("exec_after_success", &o, 1);
    }
    {
        /* A callback context with the reserved low bits set: run in a child
         * process, because the implementation may raise an exception. */
        DWORD code = run_child("lowbits");
        oracle_case("exec_ctx_low_bits_child");
        o_hex32("child_exit", code);
        oracle_case_end();
    }
    {
        INIT_ONCE o = INIT_ONCE_STATIC_INIT;
        cb_result = TRUE;
        cb_context = 0;
        exec_case("exec_ctx_null", &o, 1);
        exec_case("exec_ctx_null_again", &o, 1);
    }
    {
        INIT_ONCE o = INIT_ONCE_STATIC_INIT;
        cb_result = TRUE;
        cb_context = 0x1230;
        cb_set_error = ERROR_FILE_NOT_FOUND;
        exec_case("exec_success_callback_sets_error", &o, 1);
        cb_set_error = 0;
    }

    /* ---- synchronous Begin / Complete ---- */
    {
        INIT_ONCE o = INIT_ONCE_STATIC_INIT;
        begin_case("check_only_uninitialised", &o, INIT_ONCE_CHECK_ONLY);
        begin_case("begin_sync_first", &o, 0);
        begin_case("check_only_while_pending", &o, INIT_ONCE_CHECK_ONLY);
        begin_case("begin_async_while_sync_pending", &o, INIT_ONCE_ASYNC);
        complete_case("complete_async_flag_on_sync", &o, INIT_ONCE_ASYNC, 0x1230);
        complete_case("complete_ctx_low_bits", &o, 0, 0x1231);
        complete_case("complete_sync", &o, 0, 0x1230);
        begin_case("begin_after_complete", &o, 0);
        complete_case("complete_twice", &o, 0, 0x1230);
        begin_case("check_only_after_complete", &o, INIT_ONCE_CHECK_ONLY);
    }
    {
        INIT_ONCE o = INIT_ONCE_STATIC_INIT;
        begin_case("begin_sync_for_failure", &o, 0);
        complete_case("complete_init_failed", &o, INIT_ONCE_INIT_FAILED, 0);
        begin_case("begin_after_init_failed", &o, 0);
        complete_case("complete_init_failed_with_ctx", &o, INIT_ONCE_INIT_FAILED, 0x1230);
        begin_case("check_only_after_failed_with_ctx", &o, INIT_ONCE_CHECK_ONLY);
        complete_case("complete_init_failed_not_pending", &o, INIT_ONCE_INIT_FAILED, 0);
    }
    {
        INIT_ONCE o = INIT_ONCE_STATIC_INIT;
        complete_case("complete_not_begun", &o, 0, 0x1230);
        begin_case("begin_after_complete_not_begun", &o, 0);
    }

    /* ---- asynchronous mode ---- */
    {
        INIT_ONCE o = INIT_ONCE_STATIC_INIT;
        begin_case("begin_async_first", &o, INIT_ONCE_ASYNC);
        begin_case("begin_async_second", &o, INIT_ONCE_ASYNC);
        begin_case("begin_sync_while_async_pending", &o, 0);
        begin_case("check_only_while_async_pending", &o, INIT_ONCE_CHECK_ONLY);
        complete_case("complete_sync_flag_on_async", &o, 0, 0x1230);
        complete_case("complete_async", &o, INIT_ONCE_ASYNC, 0x1230);
        complete_case("complete_async_second", &o, INIT_ONCE_ASYNC, 0x1240);
        begin_case("begin_async_after_complete", &o, INIT_ONCE_ASYNC);
        begin_case("check_only_after_async_complete", &o, INIT_ONCE_CHECK_ONLY);
    }
    {
        INIT_ONCE o = INIT_ONCE_STATIC_INIT;
        begin_case("begin_async_for_failure", &o, INIT_ONCE_ASYNC);
        complete_case("complete_async_init_failed", &o, INIT_ONCE_ASYNC | INIT_ONCE_INIT_FAILED, 0);
        begin_case("begin_after_async_failed", &o, INIT_ONCE_ASYNC);
    }

    /* ---- invalid flags: a fresh once each, so nothing can block ---- */
    {
        INIT_ONCE o1 = INIT_ONCE_STATIC_INIT, o2 = INIT_ONCE_STATIC_INIT, o3 = INIT_ONCE_STATIC_INIT;
        begin_case("begin_check_only_and_async", &o1, INIT_ONCE_CHECK_ONLY | INIT_ONCE_ASYNC);
        begin_case("check_only_after_check_only_and_async", &o1, INIT_ONCE_CHECK_ONLY);
        begin_case("begin_unknown_flag", &o2, 0x10);
        begin_case("check_only_after_unknown_flag", &o2, INIT_ONCE_CHECK_ONLY);
        begin_case("begin_init_failed_flag", &o3, INIT_ONCE_INIT_FAILED);
        begin_case("check_only_after_init_failed_flag", &o3, INIT_ONCE_CHECK_ONLY);
    }
    {
        INIT_ONCE o = INIT_ONCE_STATIC_INIT;
        begin_case("begin_sync_for_invalid_complete", &o, 0);
        complete_case("complete_unknown_flag", &o, 0x10, 0x1230);
        begin_case("check_only_after_complete_unknown_flag", &o, INIT_ONCE_CHECK_ONLY);
    }
    {
        INIT_ONCE o = INIT_ONCE_STATIC_INIT;
        begin_case("begin_sync_for_check_only_complete", &o, 0);
        complete_case("complete_check_only_flag", &o, INIT_ONCE_CHECK_ONLY, 0x1230);
        begin_case("check_only_after_complete_check_only_flag", &o, INIT_ONCE_CHECK_ONLY);
    }

    /* ---- four threads race ExecuteOnce ---- */
    {
        HANDLE th[4];
        go = CreateEventW(NULL, TRUE, FALSE, NULL);
        for (int i = 0; i < 4; i++) th[i] = CreateThread(NULL, 0, exec_thread, NULL, 0, NULL);
        SetEvent(go);
        WaitForMultipleObjects(4, th, TRUE, INFINITE);
        int all = 1;
        for (int i = 0; i < 4; i++) {
            DWORD c = 0;
            GetExitCodeThread(th[i], &c);
            all &= c == 1;
            CloseHandle(th[i]);
        }
        oracle_case("exec_four_threads");
        o_i64("callback_calls", shared_calls);
        o_bool("all_saw_ctx", all);
        oracle_case_end();
        CloseHandle(go);
    }

    return oracle_end();
}
