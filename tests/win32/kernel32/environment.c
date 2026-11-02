/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: Get/SetEnvironmentVariableW, ExpandEnvironmentStringsW and
 * GetEnvironmentStringsW, using only variables the test sets itself.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/processenv/nf-processenv-getenvironmentvariablew
 *       https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-setenvironmentvariable
 *       https://learn.microsoft.com/en-us/windows/win32/api/processenv/nf-processenv-expandenvironmentstringsw
 *       https://learn.microsoft.com/en-us/windows/win32/api/processenv/nf-processenv-getenvironmentstringsw
 *       https://learn.microsoft.com/en-us/windows/win32/procthread/environment-variables
 */
#include "oracle.h"
#include <winternl.h>

static void get_case(const char *name, const wchar_t *var, DWORD size)
{
    wchar_t buf[64];
    DWORD r;
    wmemset(buf, L'#', 64);
    buf[63] = 0;
    ORACLE_CALL(r, GetEnvironmentVariableW(var, size ? buf : NULL, size));
    oracle_case(name);
    o_u32("ret", r);
    o_lasterr();
    if (size && r < size) o_wstr("value", buf);
    if (size && r >= size) {
        o_bool("buf_untouched", buf[0] == L'#');
        o_bytes("buf_head", buf, 8 * sizeof(wchar_t)); /* what a too-small call wrote */
    }
    oracle_case_end();
}

static void set_case(const char *name, const wchar_t *var, const wchar_t *val)
{
    BOOL ok;
    ORACLE_CALL(ok, SetEnvironmentVariableW(var, val));
    oracle_case(name);
    o_bool("ret", ok);
    o_lasterr();
    oracle_case_end();
}

static void expand_case(const char *name, const wchar_t *in, DWORD size)
{
    wchar_t buf[256];
    DWORD r;
    wmemset(buf, L'#', 256);
    buf[255] = 0;
    ORACLE_CALL(r, ExpandEnvironmentStringsW(in, size ? buf : NULL, size));
    oracle_case(name);
    o_u32("ret", r);
    o_lasterr();
    if (size && r && r <= size) o_wstr("out", buf);
    oracle_case_end();
}

int main(void)
{
    oracle_begin("kernel32/environment");

    set_case("set_new", L"SETU_ORACLE_A", L"hello");
    get_case("get_exact", L"SETU_ORACLE_A", 64);
    get_case("get_other_case", L"setu_oracle_a", 64);
    get_case("get_size_exact_len", L"SETU_ORACLE_A", 5);
    get_case("get_size_len_plus_one", L"SETU_ORACLE_A", 6);
    get_case("probe_get_size_one", L"SETU_ORACLE_A", 1);
    get_case("probe_get_size_three", L"SETU_ORACLE_A", 3);
    {   /* The same through ntdll: buffer for 3 characters. */
        typedef LONG (WINAPI *rtl_query_fn)(void *, UNICODE_STRING *, UNICODE_STRING *);
        rtl_query_fn fn = (rtl_query_fn)(void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"),
                                                               "RtlQueryEnvironmentVariable_U");
        static wchar_t name[] = L"SETU_ORACLE_A";
        wchar_t buf[8];
        wmemset(buf, L'#', 8);
        UNICODE_STRING un = {sizeof name - sizeof(wchar_t), sizeof name, name};
        UNICODE_STRING uv = {0, 3 * sizeof(wchar_t), buf};
        LONG st = fn(NULL, &un, &uv);
        oracle_case("probe_rtl_query_too_small");
        o_hex32("status", (DWORD)st);
        o_u32("length", uv.Length);
        o_bytes("buf_head", buf, 8 * sizeof(wchar_t));
        oracle_case_end();
    }
    get_case("get_null_buffer", L"SETU_ORACLE_A", 0);
    get_case("get_missing", L"SETU_ORACLE_MISSING", 64);
    get_case("get_missing_null_buffer", L"SETU_ORACLE_MISSING", 0);

    set_case("set_empty_value", L"SETU_ORACLE_EMPTY", L"");
    get_case("get_empty_value", L"SETU_ORACLE_EMPTY", 64);
    get_case("get_empty_value_null_buffer", L"SETU_ORACLE_EMPTY", 0);

    set_case("set_overwrite", L"setu_oracle_a", L"world");
    get_case("get_after_case_insensitive_overwrite", L"SETU_ORACLE_A", 64);
    set_case("delete", L"SETU_ORACLE_A", NULL);
    get_case("get_deleted", L"SETU_ORACLE_A", 64);
    set_case("delete_missing", L"SETU_ORACLE_NEVER", NULL);

    set_case("set_name_with_equals", L"SETU=BAD", L"x");
    get_case("get_name_with_equals", L"SETU=BAD", 64);
    set_case("set_empty_name", L"", L"x");
    get_case("get_empty_name", L"", 64);
    set_case("set_leading_equals", L"=SETU_HIDDEN", L"h");
    get_case("get_leading_equals", L"=SETU_HIDDEN", 64);
    set_case("set_value_with_equals", L"SETU_ORACLE_EQ", L"a=b");
    get_case("get_value_with_equals", L"SETU_ORACLE_EQ", 64);
    set_case("set_name_with_space", L"SETU ORACLE", L"sp");
    get_case("get_name_with_space", L"SETU ORACLE", 64);
    get_case("get_name_with_trailing_space", L"SETU_ORACLE_EQ ", 64);

    SetEnvironmentVariableW(L"SETU_X", L"ex");
    SetEnvironmentVariableW(L"SETU_NEST", L"%SETU_X%");
    expand_case("expand_simple", L"a%SETU_X%b", 256);
    expand_case("expand_case_insensitive", L"%setu_x%", 256);
    expand_case("expand_undefined_kept", L"%SETU_UNDEFINED%", 256);
    expand_case("expand_not_recursive", L"%SETU_NEST%", 256);
    expand_case("expand_lone_percent", L"100% sure", 256);
    expand_case("expand_double_percent", L"%%SETU_X%%", 256);
    expand_case("expand_unterminated", L"%SETU_X", 256);
    expand_case("expand_empty_name", L"a%%b", 256);
    expand_case("expand_empty_input", L"", 256);
    expand_case("expand_null_buffer", L"a%SETU_X%b", 0);
    expand_case("expand_buffer_too_small", L"a%SETU_X%b", 3);
    expand_case("expand_buffer_exact_no_nul", L"a%SETU_X%b", 4);
    expand_case("expand_buffer_exact", L"a%SETU_X%b", 5);
    expand_case("expand_empty_variable", L"[%SETU_ORACLE_EMPTY%]", 256);
    expand_case("expand_hidden_variable", L"%=SETU_HIDDEN%", 256);
    /* Probes for the general '%' rule. */
    expand_case("probe_expand_undefined_then_var", L"%SETU_UNDEF%SETU_X%", 256);
    expand_case("probe_expand_two_vars", L"%SETU_X%%SETU_X%", 256);
    expand_case("probe_expand_triple_percent", L"%%%SETU_X%", 256);
    expand_case("probe_expand_var_then_percent", L"%SETU_X%%", 256);
    expand_case("probe_expand_two_percents", L"%%", 256);
    expand_case("probe_expand_one_percent", L"%", 256);
    expand_case("probe_expand_space_name_then_var", L"%a b%SETU_X%", 256);
    expand_case("probe_expand_undefined_twice", L"x%SETU_UNDEF%%SETU_UNDEF%y", 256);
    expand_case("probe_expand_undef_var_undef", L"%U1%SETU_X%U2%", 256);
    expand_case("probe_expand_double_percent_small_buf", L"%%SETU_X%%", 3);
    expand_case("probe_expand_double_percent_exact_buf", L"%%SETU_X%%", 5);
    {   /* The same through ntdll. */
        typedef LONG (WINAPI *rtl_expand_fn)(void *, UNICODE_STRING *, UNICODE_STRING *, ULONG *);
        rtl_expand_fn fn = (rtl_expand_fn)(void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"),
                                                                 "RtlExpandEnvironmentStrings_U");
        static wchar_t src[] = L"%%SETU_X%%";
        wchar_t out[64];
        UNICODE_STRING us = {sizeof src - sizeof(wchar_t), sizeof src, src};
        UNICODE_STRING ud = {0, sizeof out, out};
        ULONG len = 0;
        LONG st = fn(NULL, &us, &ud, &len);
        oracle_case("probe_rtl_expand_double_percent");
        o_hex32("status", (DWORD)st);
        o_u32("len", len);
        if (!st) {
            out[ud.Length / sizeof(wchar_t)] = 0;
            o_wstr("out", out);
        }
        oracle_case_end();
    }

    {   /* The block must contain our variable once, in the exact case we set. */
        SetEnvironmentVariableW(L"Setu_Block", L"v");
        wchar_t *blk = GetEnvironmentStringsW();
        int found = 0, found_other_case = 0, sorted = 1;
        const wchar_t *prev = NULL;
        for (wchar_t *p = blk; *p; p += wcslen(p) + 1) {
            if (!wcscmp(p, L"Setu_Block=v")) found++;
            else if (!_wcsicmp(p, L"Setu_Block=v")) found_other_case++;
            if (p[0] != L'=' && prev && prev[0] != L'=' && _wcsicmp(prev, p) > 0) sorted = 0;
            prev = p;
        }
        oracle_case("block_contains_variable");
        o_u32("found", found);
        o_u32("found_other_case", found_other_case);
        oracle_case_end();
        oracle_case("block_case_insensitively_sorted");
        o_bool("sorted", sorted);
        oracle_case_end();
        BOOL ok;
        ORACLE_CALL(ok, FreeEnvironmentStringsW(blk));
        oracle_case("free_block");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
    }

    {   /* SetEnvironmentStringsW replaces the whole block. */
        static wchar_t block[] = L"SETU_ONLY=1\0Zeta=z\0";
        BOOL ok;
        ORACLE_CALL(ok, SetEnvironmentStringsW(block));
        oracle_case("set_environment_strings");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        get_case("after_replace_new", L"SETU_ONLY", 64);
        get_case("after_replace_old_gone", L"SETU_X", 64);
    }

    return oracle_end();
}
