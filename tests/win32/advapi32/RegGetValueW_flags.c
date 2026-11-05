/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: RegGetValueW flag handling: RRF_NOEXPAND with and without
 * RRF_RT_REG_EXPAND_SZ, RRF_RT_REG_EXPAND_SZ without RRF_NOEXPAND,
 * RRF_ZEROONFAILURE on type mismatch, ERROR_MORE_DATA and missing values,
 * flags of zero and undefined high bits. The data buffer is filled with
 * 0xCC first to see what is written.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-reggetvaluew
 */
#include "oracle.h"

static HKEY root;

static void g_case(const char *cname, const wchar_t *value, DWORD flags, DWORD bufsize, int with_type)
{
    unsigned char b[32];
    memset(b, 0xCC, sizeof b);
    DWORD t = 0xAAAAAAAAu, sz = bufsize;
    LONG st;
    ORACLE_CALL(st, RegGetValueW(root, NULL, value, flags, with_type ? &t : NULL, b, &sz));
    oracle_case(cname);
    o_status("ret", st);
    o_lasterr();
    if (with_type) o_u32("type", t);
    o_u32("size", sz);
    o_bytes("buffer", b, bufsize < sizeof b ? bufsize : sizeof b);
    oracle_case_end();
}

int main(void)
{
    oracle_begin("advapi32/RegGetValueW_flags");
    root = oracle_reg_root();
    SetEnvironmentVariableW(L"SETU_ORACLE_V", L"xyz");
    static const wchar_t exp[] = L"%SETU_ORACLE_V%!";
    static const wchar_t sz[] = L"plain";
    DWORD dw = 0x01020304;
    RegSetValueExW(root, L"Exp", 0, REG_EXPAND_SZ, (const BYTE *)exp, sizeof exp);
    RegSetValueExW(root, L"Sz", 0, REG_SZ, (const BYTE *)sz, sizeof sz);
    RegSetValueExW(root, L"Dw", 0, REG_DWORD, (const BYTE *)&dw, 4);

    g_case("expand_with_sz_filter", L"Exp", RRF_RT_REG_SZ, 32, 1);
    g_case("expand_with_expand_filter", L"Exp", RRF_RT_REG_EXPAND_SZ, 32, 1);
    g_case("expand_with_expand_noexpand", L"Exp", RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND, 32, 1);
    g_case("expand_with_sz_noexpand", L"Exp", RRF_RT_REG_SZ | RRF_NOEXPAND, 32, 1);
    g_case("expand_with_any", L"Exp", RRF_RT_ANY, 32, 1);
    g_case("expand_with_any_noexpand", L"Exp", RRF_RT_ANY | RRF_NOEXPAND, 32, 1);
    g_case("expand_sz_and_expand", L"Exp", RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, 32, 1);
    g_case("sz_with_expand_noexpand", L"Sz", RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND, 32, 1);
    g_case("sz_with_noexpand_sz", L"Sz", RRF_RT_REG_SZ | RRF_NOEXPAND, 32, 1);
    g_case("dword_noexpand", L"Dw", RRF_RT_DWORD | RRF_NOEXPAND, 32, 1);

    g_case("zero_on_failure_mismatch", L"Sz", RRF_RT_REG_DWORD | RRF_ZEROONFAILURE, 16, 1);
    g_case("zero_on_failure_moredata", L"Sz", RRF_RT_REG_SZ | RRF_ZEROONFAILURE, 4, 1);
    g_case("zero_on_failure_missing", L"Missing", RRF_RT_ANY | RRF_ZEROONFAILURE, 16, 1);
    g_case("zero_on_failure_success", L"Dw", RRF_RT_ANY | RRF_ZEROONFAILURE, 16, 1);
    g_case("no_zero_mismatch", L"Sz", RRF_RT_REG_DWORD, 16, 1);
    g_case("no_zero_moredata", L"Sz", RRF_RT_REG_SZ, 4, 1);
    g_case("no_zero_missing", L"Missing", RRF_RT_ANY, 16, 1);
    g_case("zero_on_failure_expand_moredata", L"Exp", RRF_RT_REG_SZ | RRF_ZEROONFAILURE, 8, 1);

    g_case("flags_zero", L"Dw", 0, 16, 1);
    g_case("only_noexpand", L"Dw", RRF_NOEXPAND, 16, 1);
    g_case("only_zero_on_failure", L"Dw", RRF_ZEROONFAILURE, 16, 1);
    g_case("high_bit_set", L"Dw", RRF_RT_ANY | 0x80000000u, 16, 1);
    g_case("bit_0x00100000", L"Dw", RRF_RT_ANY | 0x00100000, 16, 1);
    g_case("bit_0x00010000", L"Dw", RRF_RT_ANY | 0x00010000, 16, 1);
    g_case("both_wow64_flags", L"Dw", RRF_RT_ANY | RRF_SUBKEY_WOW6464KEY | RRF_SUBKEY_WOW6432KEY, 16, 1);
    g_case("no_type_pointer", L"Dw", RRF_RT_DWORD, 16, 0);
    g_case("no_type_pointer_mismatch", L"Sz", RRF_RT_DWORD, 16, 0);

    {
        LONG st;
        DWORD t = 0xAAAAAAAAu;
        unsigned char b[8];
        ORACLE_CALL(st, RegGetValueW(root, NULL, L"Dw", RRF_RT_ANY, &t, b, NULL));
        oracle_case("data_without_size");
        o_status("ret", st);
        o_lasterr();
        o_u32("type", t);
        oracle_case_end();
        ORACLE_CALL(st, RegGetValueW(root, NULL, L"Dw", RRF_RT_ANY, &t, NULL, NULL));
        oracle_case("no_data_no_size");
        o_status("ret", st);
        o_lasterr();
        o_u32("type", t);
        oracle_case_end();
    }

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
