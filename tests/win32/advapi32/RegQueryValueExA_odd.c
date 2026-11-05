/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: RegQueryValueExA reading Unicode string values whose byte
 * count is odd (set with RegSetValueExW), so the last byte is half a
 * character; size-only queries, full reads and small buffers.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regqueryvalueexa
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regsetvalueexw
 */
#include "oracle.h"

static HKEY root;

static void q_case(const char *cname, DWORD type, const void *data, DWORD size, DWORD bufsize)
{
    RegSetValueExW(root, L"V", 0, type, data, size);
    DWORD t = 0, need = 0;
    LONG st;
    ORACLE_CALL(st, RegQueryValueExA(root, "V", NULL, &t, NULL, &need));
    oracle_case(cname);
    o_status("sizeonly_ret", st);
    o_lasterr();
    o_u32("sizeonly_size", need);
    unsigned char b[64];
    memset(b, 0xCC, sizeof b);
    DWORD sz = bufsize;
    LONG q = RegQueryValueExA(root, "V", NULL, &t, b, &sz);
    o_status("ret", q);
    o_u32("type", t);
    o_u32("size", sz);
    o_bytes("buffer", b, bufsize + 2 < sizeof b ? bufsize + 2 : sizeof b);
    RegDeleteValueW(root, L"V");
    oracle_case_end();
}

int main(void)
{
    oracle_begin("advapi32/RegQueryValueExA_odd");
    root = oracle_reg_root();

    static const wchar_t hello[] = L"hello";
    static const wchar_t hi_nul[] = L"hi\0x";
    static const wchar_t e_acute[] = L"\x00e9\x00e9";

    q_case("sz_1_byte_big_buf", REG_SZ, hello, 1, 32);
    q_case("sz_3_bytes_big_buf", REG_SZ, hello, 3, 32);
    q_case("sz_11_bytes_big_buf", REG_SZ, hello, 11, 32);
    q_case("sz_13_bytes_past_nul", REG_SZ, L"hello\0", 13, 32);
    q_case("sz_7_bytes_after_nul", REG_SZ, hi_nul, 7, 32);
    q_case("sz_3_bytes_buf_1", REG_SZ, hello, 3, 1);
    q_case("sz_3_bytes_buf_2", REG_SZ, hello, 3, 2);
    q_case("sz_11_bytes_buf_5", REG_SZ, hello, 11, 5);
    q_case("sz_11_bytes_buf_6", REG_SZ, hello, 11, 6);
    q_case("sz_3_bytes_high_byte", REG_SZ, e_acute, 3, 32);
    q_case("expand_3_bytes", REG_EXPAND_SZ, hello, 3, 32);
    q_case("expand_11_bytes", REG_EXPAND_SZ, hello, 11, 32);
    q_case("multi_5_bytes", REG_MULTI_SZ, L"a\0b\0", 5, 32);
    q_case("multi_9_bytes", REG_MULTI_SZ, L"a\0b\0", 9, 32);
    q_case("link_3_bytes", REG_LINK, hello, 3, 32);

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
