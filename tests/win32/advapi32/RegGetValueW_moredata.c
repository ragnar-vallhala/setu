/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: RegGetValueW with small buffers for each kind of value:
 * return code, reported size and type, and which bytes of the buffer
 * change (filled with 0xCC first); also size-only queries with NULL data,
 * and a zero-size buffer.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-reggetvaluew
 */
#include "oracle.h"

static HKEY root;

static void g_case(const char *cname, const wchar_t *value, DWORD flags, int with_data, DWORD bufsize)
{
    unsigned char b[64];
    memset(b, 0xCC, sizeof b);
    DWORD t = 0xAAAAAAAAu, sz = bufsize;
    LONG st;
    ORACLE_CALL(st, RegGetValueW(root, NULL, value, flags, &t, with_data ? b : NULL, &sz));
    oracle_case(cname);
    o_status("ret", st);
    o_lasterr();
    o_u32("type", t);
    o_u32("size", sz);
    if (with_data) o_bytes("buffer", b, bufsize + 4 < sizeof b ? bufsize + 4 : sizeof b);
    oracle_case_end();
}

int main(void)
{
    oracle_begin("advapi32/RegGetValueW_moredata");
    root = oracle_reg_root();

    static const wchar_t hello[] = L"hello";
    static const wchar_t multi[] = L"ab\0cd\0";
    static const unsigned char bin[] = { 1, 2, 3, 4, 5, 6, 7 };
    DWORD dw = 0x11223344;
    ULONGLONG qw = 0x0102030405060708ull;
    RegSetValueExW(root, L"Sz", 0, REG_SZ, (const BYTE *)hello, sizeof hello);
    RegSetValueExW(root, L"SzNoNul", 0, REG_SZ, (const BYTE *)hello, sizeof hello - 2);
    RegSetValueExW(root, L"Multi", 0, REG_MULTI_SZ, (const BYTE *)multi, sizeof multi);
    RegSetValueExW(root, L"Bin", 0, REG_BINARY, bin, sizeof bin);
    RegSetValueExW(root, L"Dw", 0, REG_DWORD, (const BYTE *)&dw, 4);
    RegSetValueExW(root, L"Qw", 0, REG_QWORD, (const BYTE *)&qw, 8);

    g_case("sz_size_only", L"Sz", RRF_RT_REG_SZ, 0, 0);
    g_case("sz_size_only_nonzero_in", L"Sz", RRF_RT_REG_SZ, 0, 100);
    g_case("sz_exact", L"Sz", RRF_RT_REG_SZ, 1, 12);
    g_case("sz_minus_1", L"Sz", RRF_RT_REG_SZ, 1, 11);
    g_case("sz_minus_2", L"Sz", RRF_RT_REG_SZ, 1, 10);
    g_case("sz_2", L"Sz", RRF_RT_REG_SZ, 1, 2);
    g_case("sz_0", L"Sz", RRF_RT_REG_SZ, 1, 0);
    g_case("sz_nonul_size_only", L"SzNoNul", RRF_RT_REG_SZ, 0, 0);
    g_case("sz_nonul_10", L"SzNoNul", RRF_RT_REG_SZ, 1, 10);
    g_case("sz_nonul_11", L"SzNoNul", RRF_RT_REG_SZ, 1, 11);
    g_case("sz_nonul_12", L"SzNoNul", RRF_RT_REG_SZ, 1, 12);
    g_case("multi_exact", L"Multi", RRF_RT_REG_MULTI_SZ, 1, 14);
    g_case("multi_minus_2", L"Multi", RRF_RT_REG_MULTI_SZ, 1, 12);
    g_case("binary_minus_1", L"Bin", RRF_RT_REG_BINARY, 1, 6);
    g_case("binary_size_only", L"Bin", RRF_RT_REG_BINARY, 0, 0);
    g_case("dword_3", L"Dw", RRF_RT_DWORD, 1, 3);
    g_case("dword_0", L"Dw", RRF_RT_DWORD, 1, 0);
    g_case("dword_8", L"Dw", RRF_RT_DWORD, 1, 8);
    g_case("dword_size_only", L"Dw", RRF_RT_DWORD, 0, 0);
    g_case("qword_7", L"Qw", RRF_RT_QWORD, 1, 7);
    g_case("qword_size_only", L"Qw", RRF_RT_QWORD, 0, 0);
    g_case("mismatch_size_only", L"Dw", RRF_RT_REG_SZ, 0, 0);
    g_case("mismatch_small_buffer", L"Sz", RRF_RT_DWORD, 1, 2);
    g_case("missing_size_only", L"Missing", RRF_RT_ANY, 0, 0);

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
