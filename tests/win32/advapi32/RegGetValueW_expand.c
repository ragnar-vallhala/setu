/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: RegGetValueW expanding REG_EXPAND_SZ values: the size
 * reported by size-only queries (before or after expansion), buffers that
 * fit the raw string but not the expansion (and the other way round),
 * exactly fitting buffers, undefined variables, and expansion that shrinks
 * or grows the string.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-reggetvaluew
 *       https://learn.microsoft.com/en-us/windows/win32/api/processenv/nf-processenv-expandenvironmentstringsw
 */
#include "oracle.h"

static HKEY root;

static void g_case(const char *cname, const wchar_t *value, DWORD flags, int with_data, DWORD bufsize)
{
    unsigned char b[256];
    memset(b, 0xCC, sizeof b);
    DWORD t = 0xAAAAAAAAu, sz = bufsize;
    LONG st;
    ORACLE_CALL(st, RegGetValueW(root, NULL, value, flags, &t, with_data ? b : NULL, &sz));
    oracle_case(cname);
    o_status("ret", st);
    o_lasterr();
    o_u32("type", t);
    o_u32("size", sz);
    if (with_data) o_bytes("buffer", b, bufsize + 2 < sizeof b ? bufsize + 2 : sizeof b);
    oracle_case_end();
}

static void set(const wchar_t *name, const wchar_t *s)
{
    RegSetValueExW(root, name, 0, REG_EXPAND_SZ, (const BYTE *)s, (DWORD)((wcslen(s) + 1) * sizeof(wchar_t)));
}

int main(void)
{
    oracle_begin("advapi32/RegGetValueW_expand");
    root = oracle_reg_root();
    SetEnvironmentVariableW(L"SETU_ORACLE_LONG", L"0123456789abcdefghij"); /* 20 chars */
    SetEnvironmentVariableW(L"SETU_ORACLE_S", L"s");
    SetEnvironmentVariableW(L"SETU_ORACLE_EMPTY", NULL);

    set(L"Grow", L"<%SETU_ORACLE_LONG%>");       /* 21 chars raw, 22 expanded */
    set(L"Shrink", L"<%SETU_ORACLE_S%>");         /* 17 chars raw, 3 expanded */
    set(L"Undef", L"<%SETU_ORACLE_EMPTY%>");      /* stays unexpanded */
    set(L"NoVars", L"plain");
    set(L"Pct", L"100%");
    RegSetValueExW(root, L"NoNul", 0, REG_EXPAND_SZ, (const BYTE *)L"<%SETU_ORACLE_S%>", 34);
    RegSetValueExW(root, L"Empty", 0, REG_EXPAND_SZ, NULL, 0);

    const DWORD f = RRF_RT_REG_SZ;
    g_case("grow_size_only", L"Grow", f, 0, 0);
    g_case("grow_big_buffer", L"Grow", f, 1, 256);
    g_case("grow_exact_expanded", L"Grow", f, 1, 46);
    g_case("grow_expanded_minus_2", L"Grow", f, 1, 44);
    g_case("grow_raw_size", L"Grow", f, 1, 44);
    g_case("grow_small", L"Grow", f, 1, 10);
    g_case("grow_zero_with_data", L"Grow", f, 1, 0);
    g_case("shrink_size_only", L"Shrink", f, 0, 0);
    g_case("shrink_big_buffer", L"Shrink", f, 1, 256);
    g_case("shrink_exact_expanded", L"Shrink", f, 1, 8);
    g_case("shrink_between", L"Shrink", f, 1, 20);
    g_case("shrink_small", L"Shrink", f, 1, 4);
    g_case("undef_big_buffer", L"Undef", f, 1, 256);
    g_case("novars_size_only", L"NoVars", f, 0, 0);
    g_case("novars_exact", L"NoVars", f, 1, 12);
    g_case("novars_minus_2", L"NoVars", f, 1, 10);
    g_case("percent_literal", L"Pct", f, 1, 256);
    g_case("no_nul_big_buffer", L"NoNul", f, 1, 256);
    g_case("no_nul_size_only", L"NoNul", f, 0, 0);
    g_case("empty_size_only", L"Empty", f, 0, 0);
    g_case("empty_big_buffer", L"Empty", f, 1, 16);
    g_case("grow_noexpand_size_only", L"Grow", RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND, 0, 0);
    g_case("grow_noexpand_raw_exact", L"Grow", RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND, 1, 44);
    g_case("grow_any_size_only", L"Grow", RRF_RT_ANY, 0, 0);
    g_case("grow_any_big", L"Grow", RRF_RT_ANY, 1, 256);

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
