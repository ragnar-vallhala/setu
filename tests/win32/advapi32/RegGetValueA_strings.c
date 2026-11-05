/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: RegGetValueA on Unicode values: string conversion and the
 * reported sizes (size-only, exact and small buffers), termination of
 * unterminated strings, REG_MULTI_SZ, non-string types, type filters and
 * the subkey argument.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-reggetvaluea
 */
#include "oracle.h"

static HKEY root;

static void g_case(const char *cname, const char *sub, const char *value, DWORD flags, int with_data,
                   DWORD bufsize)
{
    unsigned char b[64];
    memset(b, 0xCC, sizeof b);
    DWORD t = 0xAAAAAAAAu, sz = bufsize;
    LONG st;
    ORACLE_CALL(st, RegGetValueA(root, sub, value, flags, &t, with_data ? b : NULL, &sz));
    oracle_case(cname);
    o_status("ret", st);
    o_lasterr();
    o_u32("type", t);
    o_u32("size", sz);
    if (with_data) o_bytes("buffer", b, bufsize + 2 < sizeof b ? bufsize + 2 : sizeof b);
    oracle_case_end();
}

int main(void)
{
    oracle_begin("advapi32/RegGetValueA_strings");
    root = oracle_reg_root();

    static const wchar_t hello[] = L"hello";
    static const wchar_t multi[] = L"ab\0cd\0";
    DWORD dw = 0x11223344;
    RegSetValueExW(root, L"Sz", 0, REG_SZ, (const BYTE *)hello, sizeof hello);
    RegSetValueExW(root, L"SzNoNul", 0, REG_SZ, (const BYTE *)hello, sizeof hello - 2);
    RegSetValueExW(root, L"SzOdd", 0, REG_SZ, (const BYTE *)hello, 7);
    RegSetValueExW(root, L"Multi", 0, REG_MULTI_SZ, (const BYTE *)multi, sizeof multi);
    RegSetValueExW(root, L"MultiNoFinal", 0, REG_MULTI_SZ, (const BYTE *)multi, sizeof multi - 2);
    RegSetValueExW(root, L"Bin", 0, REG_BINARY, (const BYTE *)hello, 6);
    RegSetValueExW(root, L"Dw", 0, REG_DWORD, (const BYTE *)&dw, 4);
    RegSetValueExW(root, L"Empty", 0, REG_SZ, NULL, 0);
    {
        HKEY s = NULL;
        RegCreateKeyExW(root, L"sub", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &s, NULL);
        RegSetValueExW(s, L"Sz", 0, REG_SZ, (const BYTE *)L"sub", 8);
        RegCloseKey(s);
    }

    g_case("sz_size_only", NULL, "Sz", RRF_RT_REG_SZ, 0, 0);
    g_case("sz_big", NULL, "Sz", RRF_RT_REG_SZ, 1, 64);
    g_case("sz_ansi_exact", NULL, "Sz", RRF_RT_REG_SZ, 1, 6);
    g_case("sz_5", NULL, "Sz", RRF_RT_REG_SZ, 1, 5);
    g_case("sz_1", NULL, "Sz", RRF_RT_REG_SZ, 1, 1);
    g_case("sz_unicode_size", NULL, "Sz", RRF_RT_REG_SZ, 1, 12);
    g_case("sz_nonul_size_only", NULL, "SzNoNul", RRF_RT_REG_SZ, 0, 0);
    g_case("sz_nonul_big", NULL, "SzNoNul", RRF_RT_REG_SZ, 1, 64);
    g_case("sz_nonul_5", NULL, "SzNoNul", RRF_RT_REG_SZ, 1, 5);
    g_case("sz_nonul_6", NULL, "SzNoNul", RRF_RT_REG_SZ, 1, 6);
    g_case("sz_odd_big", NULL, "SzOdd", RRF_RT_REG_SZ, 1, 64);
    g_case("sz_odd_size_only", NULL, "SzOdd", RRF_RT_REG_SZ, 0, 0);
    g_case("multi_size_only", NULL, "Multi", RRF_RT_REG_MULTI_SZ, 0, 0);
    g_case("multi_big", NULL, "Multi", RRF_RT_REG_MULTI_SZ, 1, 64);
    g_case("multi_no_final_big", NULL, "MultiNoFinal", RRF_RT_REG_MULTI_SZ, 1, 64);
    g_case("multi_6", NULL, "Multi", RRF_RT_REG_MULTI_SZ, 1, 6);
    g_case("binary_big", NULL, "Bin", RRF_RT_REG_BINARY, 1, 64);
    g_case("binary_size_only", NULL, "Bin", RRF_RT_REG_BINARY, 0, 0);
    g_case("dword_big", NULL, "Dw", RRF_RT_DWORD, 1, 64);
    g_case("dword_as_sz", NULL, "Dw", RRF_RT_REG_SZ, 1, 64);
    g_case("sz_as_dword", NULL, "Sz", RRF_RT_DWORD, 1, 64);
    g_case("empty_big", NULL, "Empty", RRF_RT_REG_SZ, 1, 8);
    g_case("empty_size_only", NULL, "Empty", RRF_RT_REG_SZ, 0, 0);
    g_case("subkey", "sub", "Sz", RRF_RT_REG_SZ, 1, 64);
    g_case("subkey_missing", "nope", "Sz", RRF_RT_REG_SZ, 1, 64);
    g_case("missing", NULL, "Missing", RRF_RT_ANY, 1, 64);
    g_case("zero_on_failure_moredata", NULL, "Sz", RRF_RT_REG_SZ | RRF_ZEROONFAILURE, 1, 4);

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
