/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: RegQueryValueExW with buffers that are too small, for each
 * kind of value: the return code, the size and type written back, and
 * which bytes of the caller's buffer are changed (the buffer is filled with
 * 0xCC first).
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regqueryvalueexw
 */
#include "oracle.h"

static HKEY root;

static void q_case(const char *cname, const wchar_t *value, DWORD bufsize)
{
    unsigned char b[64];
    memset(b, 0xCC, sizeof b);
    DWORD t = 0xAAAAAAAAu, sz = bufsize;
    LONG st;
    ORACLE_CALL(st, RegQueryValueExW(root, value, NULL, &t, b, &sz));
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
    oracle_begin("advapi32/RegQueryValueExW_moredata");
    root = oracle_reg_root();

    static const wchar_t hello[] = L"hello";
    static const wchar_t multi[] = L"ab\0cd\0";
    static const unsigned char bin[] = { 1, 2, 3, 4, 5, 6, 7 };
    DWORD dw = 0x11223344;
    ULONGLONG qw = 0x0102030405060708ull;
    RegSetValueExW(root, L"Sz", 0, REG_SZ, (const BYTE *)hello, sizeof hello);
    RegSetValueExW(root, L"SzNoNul", 0, REG_SZ, (const BYTE *)hello, sizeof hello - 2);
    RegSetValueExW(root, L"Exp", 0, REG_EXPAND_SZ, (const BYTE *)hello, sizeof hello);
    RegSetValueExW(root, L"Multi", 0, REG_MULTI_SZ, (const BYTE *)multi, sizeof multi);
    RegSetValueExW(root, L"Bin", 0, REG_BINARY, bin, sizeof bin);
    RegSetValueExW(root, L"Dw", 0, REG_DWORD, (const BYTE *)&dw, 4);
    RegSetValueExW(root, L"Qw", 0, REG_QWORD, (const BYTE *)&qw, 8);
    RegSetValueExW(root, L"Empty", 0, REG_SZ, NULL, 0);
    RegSetValueExW(root, L"None", 0, REG_NONE, bin, 3);

    q_case("sz_exact", L"Sz", 12);
    q_case("sz_minus_1", L"Sz", 11);
    q_case("sz_minus_2_no_room_for_nul", L"Sz", 10);
    q_case("sz_half", L"Sz", 6);
    q_case("sz_1", L"Sz", 1);
    q_case("sz_0", L"Sz", 0);
    q_case("sz_nonul_exact", L"SzNoNul", 10);
    q_case("sz_nonul_plus_1", L"SzNoNul", 11);
    q_case("sz_nonul_plus_2", L"SzNoNul", 12);
    q_case("sz_nonul_minus_1", L"SzNoNul", 9);
    q_case("expand_minus_2", L"Exp", 10);
    q_case("expand_0", L"Exp", 0);
    q_case("multi_exact", L"Multi", 14);
    q_case("multi_minus_2", L"Multi", 12);
    q_case("multi_1", L"Multi", 1);
    q_case("binary_exact", L"Bin", 7);
    q_case("binary_minus_1", L"Bin", 6);
    q_case("binary_0", L"Bin", 0);
    q_case("dword_exact", L"Dw", 4);
    q_case("dword_3", L"Dw", 3);
    q_case("dword_1", L"Dw", 1);
    q_case("dword_0", L"Dw", 0);
    q_case("qword_7", L"Qw", 7);
    q_case("qword_4", L"Qw", 4);
    q_case("empty_sz_0", L"Empty", 0);
    q_case("empty_sz_4", L"Empty", 4);
    q_case("none_2", L"None", 2);
    q_case("missing_0", L"Missing", 0);
    q_case("missing_4", L"Missing", 4);

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
