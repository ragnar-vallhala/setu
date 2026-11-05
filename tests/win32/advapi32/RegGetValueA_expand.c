/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: RegGetValueA on REG_EXPAND_SZ values: expansion, the sizes
 * reported by size-only queries, buffers between the ANSI and the Unicode
 * sizes, RRF_NOEXPAND, and undefined variables.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-reggetvaluea
 *       https://learn.microsoft.com/en-us/windows/win32/api/processenv/nf-processenv-expandenvironmentstringsa
 */
#include "oracle.h"

static HKEY root;

static void g_case(const char *cname, const char *value, DWORD flags, int with_data, DWORD bufsize)
{
    unsigned char b[128];
    memset(b, 0xCC, sizeof b);
    DWORD t = 0xAAAAAAAAu, sz = bufsize;
    LONG st;
    ORACLE_CALL(st, RegGetValueA(root, NULL, value, flags, &t, with_data ? b : NULL, &sz));
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
    oracle_begin("advapi32/RegGetValueA_expand");
    root = oracle_reg_root();
    SetEnvironmentVariableW(L"SETU_ORACLE_LONG", L"0123456789abcdefghij"); /* 20 chars */
    SetEnvironmentVariableW(L"SETU_ORACLE_S", L"s");

    set(L"Grow", L"<%SETU_ORACLE_LONG%>");   /* 21 chars raw, 22 expanded */
    set(L"Shrink", L"<%SETU_ORACLE_S%>");     /* 17 chars raw, 3 expanded */
    set(L"Plain", L"plain");

    g_case("grow_size_only", "Grow", RRF_RT_REG_SZ, 0, 0);
    g_case("grow_big", "Grow", RRF_RT_REG_SZ, 1, 128);
    g_case("grow_exact_ansi", "Grow", RRF_RT_REG_SZ, 1, 23);
    g_case("grow_ansi_minus_1", "Grow", RRF_RT_REG_SZ, 1, 22);
    g_case("grow_small", "Grow", RRF_RT_REG_SZ, 1, 8);
    g_case("shrink_size_only", "Shrink", RRF_RT_REG_SZ, 0, 0);
    g_case("shrink_big", "Shrink", RRF_RT_REG_SZ, 1, 128);
    g_case("shrink_exact_ansi", "Shrink", RRF_RT_REG_SZ, 1, 4);
    g_case("shrink_3", "Shrink", RRF_RT_REG_SZ, 1, 3);
    g_case("plain_size_only", "Plain", RRF_RT_REG_SZ, 0, 0);
    g_case("plain_exact", "Plain", RRF_RT_REG_SZ, 1, 6);
    g_case("noexpand_size_only", "Grow", RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND, 0, 0);
    g_case("noexpand_big", "Grow", RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND, 1, 128);
    g_case("noexpand_exact", "Grow", RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND, 1, 22);
    g_case("expand_filter_without_noexpand", "Grow", RRF_RT_REG_EXPAND_SZ, 1, 128);
    g_case("any_big", "Shrink", RRF_RT_ANY, 1, 128);

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
