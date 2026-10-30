/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: RegQueryValueExW size queries, ERROR_MORE_DATA, missing values,
 * the default value, and RegGetValueW type restriction flags.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regqueryvalueexw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-reggetvaluew
 */
#include "oracle.h"

static HKEY root;

static void query_case(const char *name, const wchar_t *value, int want_type, int want_data, DWORD bufsize)
{
    unsigned char buf[256];
    memset(buf, 0xCC, sizeof buf);
    DWORD type = 0xAAAAAAAAu, size = bufsize;
    LONG st;
    ORACLE_CALL(st, RegQueryValueExW(root, value, NULL, want_type ? &type : NULL,
                                     want_data ? buf : NULL, &size));
    oracle_case(name);
    o_status("ret", st);
    o_lasterr();
    if (want_type) o_u32("type", type);
    o_u32("size", size);
    if (want_data && bufsize) o_bytes("buffer", buf, bufsize < 32 ? bufsize : 32);
    oracle_case_end();
}

static void getvalue_case(const char *name, const wchar_t *value, DWORD flags)
{
    unsigned char buf[64];
    memset(buf, 0xCC, sizeof buf);
    DWORD type = 0xAAAAAAAAu, size = sizeof buf;
    LONG st;
    ORACLE_CALL(st, RegGetValueW(root, NULL, value, flags, &type, buf, &size));
    oracle_case(name);
    o_status("ret", st);
    o_lasterr();
    o_u32("type", type);
    o_u32("size", size);
    if (!st) o_bytes("data", buf, size);
    oracle_case_end();
}

int main(void)
{
    oracle_begin("advapi32/RegQueryValueExW");
    root = oracle_reg_root();

    const wchar_t hello[] = L"hello";
    DWORD dw = 42;
    RegSetValueExW(root, L"Str", 0, REG_SZ, (const BYTE *)hello, sizeof hello);
    RegSetValueExW(root, L"NoNul", 0, REG_SZ, (const BYTE *)hello, sizeof hello - sizeof(wchar_t));
    RegSetValueExW(root, L"Num", 0, REG_DWORD, (const BYTE *)&dw, sizeof dw);
    RegSetValueExW(root, L"Exp", 0, REG_EXPAND_SZ, (const BYTE *)L"%SETU_ORACLE_X%\\y",
                   sizeof L"%SETU_ORACLE_X%\\y");
    SetEnvironmentVariableW(L"SETU_ORACLE_X", L"abc");

    query_case("size_only", L"Str", 1, 0, 0);
    query_case("exact_buffer", L"Str", 1, 1, sizeof hello);
    query_case("buffer_too_small", L"Str", 1, 1, 4);
    query_case("buffer_zero", L"Str", 1, 1, 0);
    query_case("type_only_no_size_change", L"Num", 1, 0, 99);
    query_case("dword_big_buffer", L"Num", 1, 1, 16);
    query_case("value_name_other_case", L"STR", 1, 1, 64);
    query_case("missing_value", L"Missing", 1, 1, 64);
    query_case("default_value_unset", NULL, 1, 1, 64);
    query_case("default_value_empty_name", L"", 1, 1, 64);
    query_case("string_without_nul_raw", L"NoNul", 1, 1, 64);

    getvalue_case("getvalue_dword_as_dword", L"Num", RRF_RT_REG_DWORD);
    getvalue_case("getvalue_sz_as_dword", L"Str", RRF_RT_REG_DWORD);
    getvalue_case("getvalue_sz_any", L"Str", RRF_RT_ANY);
    getvalue_case("getvalue_sz_adds_missing_nul", L"NoNul", RRF_RT_REG_SZ);
    getvalue_case("getvalue_expand_sz_expanded", L"Exp", RRF_RT_REG_SZ);
    getvalue_case("getvalue_expand_sz_noexpand", L"Exp", RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND);
    getvalue_case("getvalue_missing", L"Missing", RRF_RT_ANY);
    getvalue_case("getvalue_no_type_flags", L"Num", 0);

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
