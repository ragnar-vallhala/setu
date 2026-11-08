/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: exceptions seen by vectored handlers and by the process:
 * RaiseException flags, codes and argument limits, continuing from a
 * handler, vectored handler and continue-handler order, the exception
 * records of hardware faults (access violation, divide by zero, breakpoint,
 * illegal instruction), unhandled exception filters, and the exit codes of
 * child processes that die from unhandled exceptions.
 *
 * Faults that cannot be continued are raised in a child process (this .exe
 * with a mode argument); the parent prints the child's exit code.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/errhandlingapi/nf-errhandlingapi-raiseexception
 *       https://learn.microsoft.com/en-us/windows/win32/api/errhandlingapi/nf-errhandlingapi-addvectoredexceptionhandler
 *       https://learn.microsoft.com/en-us/windows/win32/api/errhandlingapi/nf-errhandlingapi-addvectoredcontinuehandler
 *       https://learn.microsoft.com/en-us/windows/win32/api/errhandlingapi/nf-errhandlingapi-setunhandledexceptionfilter
 *       https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-exception_record
 *       https://learn.microsoft.com/en-us/windows/win32/debug/getexceptioncode
 */
#include "oracle.h"

static char log_buf[64];
static void add(char c)
{
    size_t n = strlen(log_buf);
    if (n < sizeof log_buf - 1) {
        log_buf[n] = c;
        log_buf[n + 1] = 0;
    }
}

static EXCEPTION_RECORD last_rec;
static LONG first_result = EXCEPTION_CONTINUE_EXECUTION;

static LONG CALLBACK veh_first(EXCEPTION_POINTERS *ep)
{
    add('1');
    last_rec = *ep->ExceptionRecord;
    return first_result;
}
static LONG CALLBACK veh_second(EXCEPTION_POINTERS *ep)
{
    (void)ep;
    add('2');
    return EXCEPTION_CONTINUE_EXECUTION;
}
static LONG CALLBACK vch(EXCEPTION_POINTERS *ep)
{
    (void)ep;
    add('c');
    return EXCEPTION_CONTINUE_SEARCH;
}

/* Resumes the hardware-fault cases at a label after the faulting
 * instruction; the address is stored by the asm just before the fault. */
static void *resume_at;
static LONG CALLBACK veh_skip(EXCEPTION_POINTERS *ep)
{
    last_rec = *ep->ExceptionRecord;
    ep->ContextRecord->Rip = (DWORD64)resume_at;
    return EXCEPTION_CONTINUE_EXECUTION;
}

static int noncont_calls;
static LONG CALLBACK veh_continue_once(EXCEPTION_POINTERS *ep)
{
    (void)ep;
    return noncont_calls++ ? EXCEPTION_CONTINUE_SEARCH : EXCEPTION_CONTINUE_EXECUTION;
}

static void rec_case(const char *name)
{
    oracle_case(name);
    o_hex32("code", last_rec.ExceptionCode);
    o_hex32("flags", last_rec.ExceptionFlags);
    o_u32("nparams", last_rec.NumberParameters);
    for (DWORD i = 0; i < last_rec.NumberParameters && i < 3; i++) {
        char k[8];
        snprintf(k, sizeof k, "p%lu", i);
        o_u64(k, last_rec.ExceptionInformation[i]);
    }
    o_bool("nested_null", last_rec.ExceptionRecord == NULL);
    oracle_case_end();
}

static LONG WINAPI filter_exit(EXCEPTION_POINTERS *ep)
{
    ExitProcess(0x5000 | (ep->ExceptionRecord->ExceptionCode & 0xff));
}

static LONG WINAPI filter_continue_search(EXCEPTION_POINTERS *ep)
{
    (void)ep;
    return EXCEPTION_CONTINUE_SEARCH;
}

static LONG WINAPI filter_execute_handler(EXCEPTION_POINTERS *ep)
{
    (void)ep;
    return EXCEPTION_EXECUTE_HANDLER;
}

static int child(const char *mode)
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (!strcmp(mode, "av")) *(volatile int *)16 = 1;
    if (!strcmp(mode, "raise")) RaiseException(0xE0001234, 0, 0, NULL);
    if (!strcmp(mode, "noncont")) {
        AddVectoredExceptionHandler(1, veh_continue_once); /* tries to continue once */
        RaiseException(0xE0000001, EXCEPTION_NONCONTINUABLE, 0, NULL);
    }
    if (!strcmp(mode, "filter_exit")) {
        SetUnhandledExceptionFilter(filter_exit);
        RaiseException(0xE0000042, 0, 0, NULL);
    }
    if (!strcmp(mode, "filter_search")) {
        SetUnhandledExceptionFilter(filter_continue_search);
        RaiseException(0xE0000043, 0, 0, NULL);
    }
    if (!strcmp(mode, "filter_handler")) {
        SetUnhandledExceptionFilter(filter_execute_handler);
        RaiseException(0xE0000044, 0, 0, NULL);
    }
    if (!strcmp(mode, "stack_overflow")) {
        volatile char big[1 << 16];
        big[0] = 1;
        return child(mode) + big[0];
    }
    if (!strcmp(mode, "int3")) __asm__ volatile("int3");
    if (!strcmp(mode, "ud2")) __asm__ volatile("ud2");
    return 0x77;
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
    WaitForSingleObject(pi.hProcess, 30000);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return code;
}

static void child_case(const char *name, const char *mode)
{
    DWORD code = run_child(mode);
    oracle_case(name);
    o_hex32("exit", code);
    oracle_case_end();
}

int main(int argc, char **argv)
{
    if (argc > 1) return child(argv[1]);
    oracle_begin("kernel32/seh_exceptions");

    /* ---- RaiseException continued by a vectored handler ---- */
    void *h1 = AddVectoredExceptionHandler(0, veh_first);
    oracle_case("add_handler");
    o_bool("ret_null", h1 == NULL);
    oracle_case_end();
    {
        ULONG_PTR args[3] = {1, 2, 3};
        log_buf[0] = 0;
        RaiseException(0xE0000100, 0, 3, args);
        rec_case("raise_three_args");
        RaiseException(0x12345678, 0, 0, NULL);
        rec_case("raise_code_without_customer_bit");
        RaiseException(0xE0000101, 0xfffffffe, 0, NULL);
        rec_case("raise_unknown_flags");
        RaiseException(0xE0000102, 0, 1, NULL);
        rec_case("raise_args_null_with_count");
        ULONG_PTR many[20];
        for (int i = 0; i < 20; i++) many[i] = 100 + i;
        RaiseException(0xE0000103, 0, 20, many);
        rec_case("raise_twenty_args");
        RaiseException(0xE0000104, 0, EXCEPTION_MAXIMUM_PARAMETERS, many);
        rec_case("raise_max_args");
        oracle_case("handler_log_single");
        o_str("log", log_buf);
        oracle_case_end();

        /* Probe: is EXCEPTION_SOFTWARE_ORIGINATE (0x80) added by
         * RaiseException or by RtlRaiseException, and is the caller's
         * record changed? */
        typedef void(WINAPI * rtl_raise_fn)(EXCEPTION_RECORD *);
        rtl_raise_fn rtl_raise =
            (rtl_raise_fn)(void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlRaiseException");
        if (rtl_raise) {
            EXCEPTION_RECORD rec;
            memset(&rec, 0, sizeof rec);
            rec.ExceptionCode = 0xE0000105;
            rtl_raise(&rec);
            rec_case("probe_rtl_raise_flags_0");
            oracle_case("probe_rtl_raise_caller_record");
            o_hex32("flags", rec.ExceptionFlags);
            oracle_case_end();
            memset(&rec, 0, sizeof rec);
            rec.ExceptionCode = 0xE0000106;
            rec.ExceptionFlags = 0x80;
            rtl_raise(&rec);
            rec_case("probe_rtl_raise_flags_80");
        }
    }

    /* ---- handler order ---- */
    {
        void *h2 = AddVectoredExceptionHandler(0, veh_second); /* appended last */
        void *c1 = AddVectoredContinueHandler(1, vch);
        first_result = EXCEPTION_CONTINUE_SEARCH;
        log_buf[0] = 0;
        RaiseException(0xE0000200, 0, 0, NULL);
        oracle_case("order_first_searches_second_continues");
        o_str("log", log_buf);
        oracle_case_end();
        void *h3 = AddVectoredExceptionHandler(1, veh_second); /* first */
        log_buf[0] = 0;
        RaiseException(0xE0000201, 0, 0, NULL);
        oracle_case("order_handler_added_first");
        o_str("log", log_buf);
        oracle_case_end();
        oracle_case("remove_handlers");
        o_u32("h3", RemoveVectoredExceptionHandler(h3));
        o_u32("h2", RemoveVectoredExceptionHandler(h2));
        o_u32("c1", RemoveVectoredContinueHandler(c1));
        o_u32("c1_again", RemoveVectoredContinueHandler(c1));
        o_u32("h2_as_continue", RemoveVectoredContinueHandler(h2));
        o_u32("null", RemoveVectoredExceptionHandler(NULL));
        oracle_case_end();
        first_result = EXCEPTION_CONTINUE_EXECUTION;
    }
    RemoveVectoredExceptionHandler(h1);

    /* ---- hardware faults, skipped by the handler ---- */
    {
        void *h = AddVectoredExceptionHandler(1, veh_skip);
        __asm__ volatile("leaq 1f(%%rip), %%r11\n\tmovq %%r11, %0\n\t"
                         "movq $16, %%rax\n\tmovl $1, (%%rax)\n1:"
                         : "=m"(resume_at) : : "rax", "r11", "memory");
        rec_case("access_violation_write");
        __asm__ volatile("leaq 1f(%%rip), %%r11\n\tmovq %%r11, %0\n\t"
                         "movq $32, %%rax\n\tmovl (%%rax), %%eax\n1:"
                         : "=m"(resume_at) : : "rax", "r11", "memory");
        rec_case("access_violation_read");
        __asm__ volatile("leaq 1f(%%rip), %%r11\n\tmovq %%r11, %0\n\t"
                         "xorl %%ecx, %%ecx\n\tmovl $1, %%eax\n\txorl %%edx, %%edx\n\tdivl %%ecx\n1:"
                         : "=m"(resume_at) : : "rax", "rcx", "rdx", "r11");
        rec_case("integer_divide_by_zero");
        __asm__ volatile("leaq 1f(%%rip), %%r11\n\tmovq %%r11, %0\n\tint3\n1:"
                         : "=m"(resume_at) : : "r11");
        rec_case("breakpoint");
        __asm__ volatile("leaq 1f(%%rip), %%r11\n\tmovq %%r11, %0\n\tud2\n1:"
                         : "=m"(resume_at) : : "r11");
        rec_case("illegal_instruction");
        RemoveVectoredExceptionHandler(h);
    }

    /* ---- unhandled exceptions in children ---- */
    child_case("child_access_violation", "av");
    child_case("child_raise_unhandled", "raise");
    child_case("child_noncontinuable_continued", "noncont");
    child_case("child_filter_exits", "filter_exit");
    child_case("child_filter_continue_search", "filter_search");
    child_case("child_filter_execute_handler", "filter_handler");
    child_case("child_stack_overflow", "stack_overflow");
    child_case("child_int3", "int3");
    child_case("child_ud2", "ud2");

    {
        LPTOP_LEVEL_EXCEPTION_FILTER prev = SetUnhandledExceptionFilter(filter_exit);
        LPTOP_LEVEL_EXCEPTION_FILTER again = SetUnhandledExceptionFilter(NULL);
        oracle_case("set_unhandled_filter_returns_previous");
        o_bool("prev_null", prev == NULL);
        o_bool("again_is_ours", again == filter_exit);
        oracle_case_end();
    }

    return oracle_end();
}
