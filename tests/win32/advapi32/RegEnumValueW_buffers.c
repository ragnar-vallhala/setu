/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: RegEnumValueW buffer sizes, the name and the data checked
 * separately: name buffers too small by one, exact (with and without room
 * for the NUL) and zero; data buffers too small; both too small at once.
 * What is returned in the size arguments and written into the buffers
 * (filled with 0xCC first).
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regenumvaluew
 */
#include "oracle.h"

static HKEY root;

static void e_case(const char *cname, DWORD index, DWORD namelen, int with_data, DWORD datalen)
{
    wchar_t name[32];
    unsigned char data[32];
    memset(name, 0xCC, sizeof name);
    memset(data, 0xCC, sizeof data);
    DWORD nl = namelen, dl = datalen, t = 0xAAAAAAAAu;
    LONG st;
    ORACLE_CALL(st, RegEnumValueW(root, index, name, &nl, NULL, &t, with_data ? data : NULL, &dl));
    oracle_case(cname);
    o_status("ret", st);
    o_lasterr();
    o_u32("name_len", nl);
    o_bytes("name_buf", name, (namelen + 1) * 2 < sizeof name ? (namelen + 1) * 2 : sizeof name);
    o_u32("type", t);
    o_u32("data_len", dl);
    if (with_data) o_bytes("data_buf", data, datalen + 2 < sizeof data ? datalen + 2 : sizeof data);
    oracle_case_end();
}

int main(void)
{
    oracle_begin("advapi32/RegEnumValueW_buffers");
    root = oracle_reg_root();
    /* index 0: "Hello" REG_SZ "abc"; index 1: "Bin" 6 bytes; index 2: default value */
    RegSetValueExW(root, L"Hello", 0, REG_SZ, (const BYTE *)L"abc", 8);
    RegSetValueExW(root, L"Bin", 0, REG_BINARY, (const BYTE *)"\1\2\3\4\5\6", 6);
    RegSetValueExW(root, NULL, 0, REG_DWORD, (const BYTE *)"\7\7\7\7", 4);

    e_case("name_big_data_big", 0, 32, 1, 32);
    e_case("name_exact_with_nul", 0, 6, 1, 32);
    e_case("name_exact_without_nul", 0, 5, 1, 32);
    e_case("name_4", 0, 4, 1, 32);
    e_case("name_1", 0, 1, 1, 32);
    e_case("name_0", 0, 0, 1, 32);
    e_case("data_exact", 0, 32, 1, 8);
    e_case("data_minus_1", 0, 32, 1, 7);
    e_case("data_minus_2", 0, 32, 1, 6);
    e_case("data_0", 0, 32, 1, 0);
    e_case("both_small", 0, 3, 1, 3);
    e_case("name_small_data_null", 0, 3, 0, 0);
    e_case("data_size_only", 0, 32, 0, 0);
    e_case("binary_data_minus_1", 1, 32, 1, 5);
    e_case("binary_name_exact_without_nul", 1, 3, 1, 32);
    e_case("default_name_1", 2, 1, 1, 32);
    e_case("default_name_0", 2, 0, 1, 32);
    e_case("default_data_3", 2, 32, 1, 3);
    e_case("past_end", 3, 32, 1, 32);
    e_case("past_end_name_0", 3, 0, 1, 32);

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
