/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: RegEnumValueA buffer sizes, name and data separately: name
 * buffers too small or exact, data buffers too small for the converted
 * ANSI string or between the ANSI and Unicode sizes; what the size
 * arguments return and what is written (buffers filled with 0xCC first).
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regenumvaluea
 */
#include "oracle.h"

static HKEY root;

static void e_case(const char *cname, DWORD index, DWORD namelen, int with_data, DWORD datalen)
{
    char name[32];
    unsigned char data[32];
    memset(name, 0xCC, sizeof name);
    memset(data, 0xCC, sizeof data);
    DWORD nl = namelen, dl = datalen, t = 0xAAAAAAAAu;
    LONG st;
    ORACLE_CALL(st, RegEnumValueA(root, index, name, &nl, NULL, &t, with_data ? data : NULL, &dl));
    oracle_case(cname);
    o_status("ret", st);
    o_lasterr();
    o_u32("name_len", nl);
    o_bytes("name_buf", name, namelen + 1 < sizeof name ? namelen + 1 : sizeof name);
    o_u32("type", t);
    o_u32("data_len", dl);
    if (with_data) o_bytes("data_buf", data, datalen + 2 < sizeof data ? datalen + 2 : sizeof data);
    oracle_case_end();
}

int main(void)
{
    oracle_begin("advapi32/RegEnumValueA_buffers");
    root = oracle_reg_root();
    /* index 0: "Hello" REG_SZ "abcde" (W 12 bytes, A 6); index 1: "Bin" 6 bytes */
    RegSetValueExW(root, L"Hello", 0, REG_SZ, (const BYTE *)L"abcde", 12);
    RegSetValueExW(root, L"Bin", 0, REG_BINARY, (const BYTE *)"\1\2\3\4\5\6", 6);

    e_case("big", 0, 32, 1, 32);
    e_case("name_exact_with_nul", 0, 6, 1, 32);
    e_case("name_exact_without_nul", 0, 5, 1, 32);
    e_case("name_1", 0, 1, 1, 32);
    e_case("name_0", 0, 0, 1, 32);
    e_case("data_ansi_exact", 0, 32, 1, 6);
    e_case("data_ansi_minus_1", 0, 32, 1, 5);
    e_case("data_between", 0, 32, 1, 9);
    e_case("data_unicode_exact", 0, 32, 1, 12);
    e_case("data_1", 0, 32, 1, 1);
    e_case("data_0", 0, 32, 1, 0);
    e_case("data_size_only", 0, 32, 0, 0);
    e_case("both_small", 0, 2, 1, 2);
    e_case("binary_minus_1", 1, 32, 1, 5);
    e_case("binary_name_small", 1, 2, 1, 32);

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
