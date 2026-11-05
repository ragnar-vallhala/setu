/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: RegQueryValueExA with buffers too small for the converted
 * ANSI string (and between the ANSI and the Unicode size): return code,
 * reported size and which buffer bytes change (filled with 0xCC first).
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regqueryvalueexa
 */
#include "oracle.h"

static HKEY root;

static void q_case(const char *cname, const char *value, DWORD bufsize)
{
    unsigned char b[64];
    memset(b, 0xCC, sizeof b);
    DWORD t = 0xAAAAAAAAu, sz = bufsize;
    LONG st;
    ORACLE_CALL(st, RegQueryValueExA(root, value, NULL, &t, b, &sz));
    oracle_case(cname);
    o_status("ret", st);
    o_lasterr();
    o_u32("type", t);
    o_u32("size", sz);
    o_bytes("buffer", b, bufsize + 4 < sizeof b ? bufsize + 4 : sizeof b);
    oracle_case_end();
}

int main(void)
{
    oracle_begin("advapi32/RegQueryValueExA_moredata");
    root = oracle_reg_root();

    static const wchar_t hello[] = L"hello";        /* W 12 bytes, A 6 */
    static const wchar_t multi[] = L"ab\0cd\0";      /* W 14 bytes, A 7 */
    RegSetValueExW(root, L"Sz", 0, REG_SZ, (const BYTE *)hello, sizeof hello);
    RegSetValueExW(root, L"SzNoNul", 0, REG_SZ, (const BYTE *)hello, sizeof hello - 2);
    RegSetValueExW(root, L"Exp", 0, REG_EXPAND_SZ, (const BYTE *)hello, sizeof hello);
    RegSetValueExW(root, L"Multi", 0, REG_MULTI_SZ, (const BYTE *)multi, sizeof multi);
    RegSetValueExW(root, L"Bin", 0, REG_BINARY, (const BYTE *)hello, 5);

    q_case("sz_ansi_exact_6", "Sz", 6);
    q_case("sz_5_no_room_for_nul", "Sz", 5);
    q_case("sz_4", "Sz", 4);
    q_case("sz_1", "Sz", 1);
    q_case("sz_0", "Sz", 0);
    q_case("sz_7", "Sz", 7);
    q_case("sz_11", "Sz", 11);
    q_case("sz_12_unicode_size", "Sz", 12);
    q_case("sz_nonul_5", "SzNoNul", 5);
    q_case("sz_nonul_6", "SzNoNul", 6);
    q_case("sz_nonul_4", "SzNoNul", 4);
    q_case("sz_nonul_10", "SzNoNul", 10);
    q_case("expand_5", "Exp", 5);
    q_case("expand_6", "Exp", 6);
    q_case("multi_7", "Multi", 7);
    q_case("multi_6", "Multi", 6);
    q_case("multi_3", "Multi", 3);
    q_case("binary_4", "Bin", 4);
    q_case("binary_5", "Bin", 5);

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
