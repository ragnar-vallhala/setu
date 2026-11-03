/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: FormatMessageW / FormatMessageA flag handling not covered by
 * kernel32/string_format: va_list arguments (64-bit and mixed printf types),
 * FORMAT_MESSAGE_ALLOCATE_BUFFER size rules, FORMAT_MESSAGE_FROM_HMODULE
 * (the exe, which has no message table, and ntdll), FROM_SYSTEM with
 * arguments and with unknown ids or languages, line widths with %n, tabs and
 * hard breaks, and the ANSI variant.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-formatmessagew
 *       https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-formatmessagea
 *       https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-erref/596a1078-e883-4972-9bbc-49e60bebca55
 *       https://learn.microsoft.com/en-us/windows/win32/debug/system-error-codes--0-499-
 */
#include "oracle.h"
#include <stdarg.h>

static void show(const char *name, DWORD r, const wchar_t *buf)
{
    oracle_case(name);
    o_u32("ret", r);
    o_lasterr();
    if (r && r < 512) o_wstr("out", buf);
    oracle_case_end();
}

/* va_list form. */
static void vfm(const char *name, DWORD flags, const wchar_t *fmt, ...)
{
    wchar_t buf[512];
    DWORD r;
    va_list ap;
    va_start(ap, fmt);
    SetLastError(ORACLE_SENTINEL);
    r = FormatMessageW(FORMAT_MESSAGE_FROM_STRING | flags, fmt, 0, 0, buf, 512, &ap);
    oracle_last_error = GetLastError();
    va_end(ap);
    show(name, r, buf);
}

/* Argument-array form with up to 6 values. */
static void afm(const char *name, DWORD flags, const wchar_t *fmt, DWORD size, DWORD_PTR a1, DWORD_PTR a2, DWORD_PTR a3)
{
    wchar_t buf[512];
    DWORD_PTR args[8] = { a1, a2, a3, (DWORD_PTR)L"_", (DWORD_PTR)L"_", (DWORD_PTR)L"_", (DWORD_PTR)L"_", (DWORD_PTR)L"_" };
    DWORD r;
    wmemset(buf, L'#', 512);
    ORACLE_CALL(r, FormatMessageW(FORMAT_MESSAGE_FROM_STRING | FORMAT_MESSAGE_ARGUMENT_ARRAY | flags, fmt, 0, 0,
                                  buf, size, (va_list *)args));
    show(name, r, buf);
}

static void sys(const char *name, DWORD flags, DWORD id, DWORD lang, DWORD_PTR *args)
{
    wchar_t buf[512];
    DWORD r;
    ORACLE_CALL(r, FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | flags, NULL, id, lang, buf, 512, (va_list *)args));
    show(name, r, buf);
}

int main(void)
{
    oracle_begin("kernel32/format_message_flags");

    /* va_list arguments. */
    vfm("va_strings", 0, L"%1-%2-%1", L"a", L"bc");
    vfm("va_int_then_string", 0, L"%1!d! %2", -5, L"x");
    vfm("va_i64", 0, L"%1!I64d! %2!s!", (LONGLONG)-1234567890123LL, L"end");
    vfm("va_i64x", 0, L"%1!I64x!", (ULONGLONG)0x123456789abcULL);
    vfm("va_hex_alt", 0, L"%1!#x! %2!#X!", 255, 255);
    vfm("va_plus_space", 0, L"[%1!+d!][%2! d!]", 7, 7);
    vfm("va_precision_string", 0, L"[%1!.3s!]", L"abcdef");
    vfm("va_width_precision", 0, L"[%1!6.2s!]", L"abcdef");
    vfm("va_zero_pad", 0, L"[%1!05d!]", -42);
    vfm("va_lu", 0, L"%1!lu!", 4000000000u);
    vfm("va_pointer", 0, L"%1!p!", (void *)(ULONG_PTR)0x1234);
    vfm("va_char_wide", 0, L"%1!c!%2!C!", L'W', 'n');
    vfm("va_star_width", 0, L"[%1!*d!]", 5, 42);
    vfm("va_ignore_inserts", FORMAT_MESSAGE_IGNORE_INSERTS, L"%1!d! %2", 1, L"x");
    vfm("va_hs_narrow", 0, L"%1!hs!", "narrow");

    /* Argument arrays with printf types. */
    afm("arr_i64u", 0, L"%1!I64u!", 256, (DWORD_PTR)18446744073709551615ULL, 0, 0);
    afm("arr_mixed", 0, L"%3 %1!x! %2!c!", 256, 0xab, L'z', (DWORD_PTR)L"s");
    afm("arr_lowercase_ignored_language", 0, L"%1", 256, (DWORD_PTR)L"lang", 0, 0);

    /* Widths and breaks. */
    afm("width_10_with_hard_break", 10, L"aaaa bbbb%ncccc dddd eeee", 256, 0, 0, 0);
    afm("width_10_exact_fit", 10, L"aaaa bbbbb cc", 256, 0, 0, 0);
    afm("width_10_tab", 10, L"aa%tbb cc dd ee", 256, 0, 0, 0);
    afm("width_10_crlf_in_text", 10, L"aa\r\nbb", 256, 0, 0, 0);
    afm("width_1", 1, L"ab cd", 256, 0, 0, 0);
    afm("width_10_insert_wraps", 10, L"x %1 y", 256, (DWORD_PTR)L"inserted words here", 0, 0);
    afm("width_mask_hard_break", FORMAT_MESSAGE_MAX_WIDTH_MASK, L"a%nb%rc", 256, 0, 0, 0);
    afm("width_mask_tabs", FORMAT_MESSAGE_MAX_WIDTH_MASK, L"a\tb%tc", 256, 0, 0, 0);
    afm("width_10_trailing_space", 10, L"aaaa ", 256, 0, 0, 0);

    /* Allocated buffers. */
    {
        wchar_t *p = NULL;
        DWORD r;
        ORACLE_CALL(r, FormatMessageW(FORMAT_MESSAGE_FROM_STRING | FORMAT_MESSAGE_ALLOCATE_BUFFER, L"abc", 0, 0,
                                      (LPWSTR)&p, 100, NULL));
        oracle_case("alloc_min_size_100");
        o_u32("ret", r);
        o_lasterr();
        o_bool("allocated", p != NULL);
        if (p) {
            o_wstr("out", p);
            o_bool("size_at_least_min", LocalSize(p) >= 100 * sizeof(wchar_t));
            LocalFree(p);
        }
        oracle_case_end();
        p = NULL;
        ORACLE_CALL(r, FormatMessageW(FORMAT_MESSAGE_FROM_STRING | FORMAT_MESSAGE_ALLOCATE_BUFFER, L"abcdef", 0, 0,
                                      (LPWSTR)&p, 2, NULL));
        oracle_case("alloc_min_smaller_than_text");
        o_u32("ret", r);
        o_lasterr();
        o_bool("allocated", p != NULL);
        if (p) {
            o_wstr("out", p);
            o_bool("size_holds_text", LocalSize(p) >= 7 * sizeof(wchar_t));
            LocalFree(p);
        }
        oracle_case_end();
        p = NULL;
        wchar_t big[400];
        for (int i = 0; i < 399; i++) big[i] = L'a' + i % 26;
        big[399] = 0;
        DWORD_PTR args[1] = { (DWORD_PTR)big };
        ORACLE_CALL(r, FormatMessageW(FORMAT_MESSAGE_FROM_STRING | FORMAT_MESSAGE_ALLOCATE_BUFFER |
                                      FORMAT_MESSAGE_ARGUMENT_ARRAY, L"%1%1%1%1%1%1%1%1%1%1", 0, 0,
                                      (LPWSTR)&p, 0, (va_list *)args));
        oracle_case("alloc_large_output");
        o_u32("ret", r);
        o_lasterr();
        o_bool("allocated", p != NULL);
        if (p) {
            o_u64("strlen", wcslen(p));
            LocalFree(p);
        }
        oracle_case_end();
        p = NULL;
        ORACLE_CALL(r, FormatMessageW(FORMAT_MESSAGE_FROM_STRING | FORMAT_MESSAGE_ALLOCATE_BUFFER, L"x", 0, 0,
                                      (LPWSTR)&p, 70000, NULL));
        oracle_case("alloc_min_over_64k");
        o_u32("ret", r);
        o_lasterr();
        o_bool("allocated", p != NULL);
        if (p) LocalFree(p);
        oracle_case_end();
        wchar_t buf[8];
        ORACLE_CALL(r, FormatMessageW(FORMAT_MESSAGE_FROM_STRING, L"x", 0, 0, buf, 70000, NULL));
        oracle_case("size_over_64k");
        o_u32("ret", r);
        o_lasterr();
        oracle_case_end();
    }

    /* Message tables. */
    {
        wchar_t buf[512];
        DWORD r;
        ORACLE_CALL(r, FormatMessageW(FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, 1, 0,
                                      buf, 512, NULL));
        show("hmodule_exe_no_table", r, buf);
        HMODULE nt = GetModuleHandleW(L"ntdll.dll");
        ORACLE_CALL(r, FormatMessageW(FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_IGNORE_INSERTS, nt, 0xC0000005,
                                      MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), buf, 512, NULL));
        show("hmodule_ntdll_access_violation", r, buf);
        ORACLE_CALL(r, FormatMessageW(FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_IGNORE_INSERTS, nt, 0xC0000034,
                                      MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), buf, 512, NULL));
        show("hmodule_ntdll_object_name_not_found", r, buf);
        ORACLE_CALL(r, FormatMessageW(FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_IGNORE_INSERTS, nt, 0x7654321,
                                      MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), buf, 512, NULL));
        show("hmodule_ntdll_missing_id", r, buf);
        ORACLE_CALL(r, FormatMessageW(FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_FROM_SYSTEM |
                                      FORMAT_MESSAGE_IGNORE_INSERTS, NULL, ERROR_ACCESS_DENIED,
                                      MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), buf, 512, NULL));
        show("hmodule_exe_falls_back_to_system", r, buf);
    }
    {
        DWORD_PTR args[2] = { (DWORD_PTR)L"1A2B", (DWORD_PTR)L"System" };
        sys("system_with_arguments", FORMAT_MESSAGE_ARGUMENT_ARRAY, ERROR_MR_MID_NOT_FOUND,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), args);
        sys("system_ignore_inserts_keeps_markers", FORMAT_MESSAGE_IGNORE_INSERTS, ERROR_MR_MID_NOT_FOUND,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), NULL);
        sys("system_unknown_id", FORMAT_MESSAGE_IGNORE_INSERTS, 0x2fff0, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), NULL);
        sys("system_uninstalled_language", FORMAT_MESSAGE_IGNORE_INSERTS, ERROR_ACCESS_DENIED,
            MAKELANGID(LANG_FRENCH, SUBLANG_FRENCH), NULL);
        sys("system_english_us", FORMAT_MESSAGE_IGNORE_INSERTS, ERROR_ACCESS_DENIED,
            MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), NULL);
        sys("system_max_width_mask", FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_MAX_WIDTH_MASK, ERROR_ACCESS_DENIED,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), NULL);
        sys("system_hresult_win32", FORMAT_MESSAGE_IGNORE_INSERTS, HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND),
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), NULL);
    }

    /* ANSI. */
    {
        char buf[256];
        DWORD_PTR args[2] = { (DWORD_PTR)"ansi", (DWORD_PTR)7 };
        DWORD r;
        ORACLE_CALL(r, FormatMessageA(FORMAT_MESSAGE_FROM_STRING | FORMAT_MESSAGE_ARGUMENT_ARRAY, "[%1] [%2!d!] %%",
                                      0, 0, buf, 256, (va_list *)args));
        oracle_case("ansi_from_string");
        o_u32("ret", r);
        o_lasterr();
        if (r) o_str("out", buf);
        oracle_case_end();
        ORACLE_CALL(r, FormatMessageA(FORMAT_MESSAGE_FROM_STRING, "hello", 0, 0, buf, 3, NULL));
        oracle_case("ansi_buffer_too_small");
        o_u32("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL,
                                      ERROR_FILE_NOT_FOUND, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), buf, 256, NULL));
        oracle_case("ansi_system");
        o_u32("ret", r);
        o_lasterr();
        if (r) o_str("out", buf);
        oracle_case_end();
        ORACLE_CALL(r, FormatMessageA(FORMAT_MESSAGE_FROM_STRING | FORMAT_MESSAGE_ARGUMENT_ARRAY, "%1!ls!",
                                      0, 0, buf, 256, (va_list *)(DWORD_PTR[]){ (DWORD_PTR)L"wide" }));
        oracle_case("ansi_wide_insert");
        o_u32("ret", r);
        o_lasterr();
        if (r) o_str("out", buf);
        oracle_case_end();
    }

    return oracle_end();
}
