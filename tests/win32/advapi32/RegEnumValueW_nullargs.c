/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: RegEnumValueW with optional arguments left out: NULL type,
 * NULL data with and without a size, data without a size, a non-NULL
 * reserved pointer, NULL name buffer or name size, and handles without
 * KEY_QUERY_VALUE or invalid.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regenumvaluew
 */
#include "oracle.h"

#define BASE ORACLE_REG_BASE L"\\advapi32_RegEnumValueW_nullargs"

static HKEY root;

static void e_case(const char *cname, HKEY key, int use_name, int use_namelen, int use_reserved,
                   int use_type, int use_data, int use_datalen)
{
    wchar_t name[32];
    unsigned char data[32];
    memset(name, 0xCC, sizeof name);
    memset(data, 0xCC, sizeof data);
    DWORD nl = 32, dl = 32, t = 0xAAAAAAAAu, res = 0;
    LONG st;
    ORACLE_CALL(st, RegEnumValueW(key, 0, use_name ? name : NULL, use_namelen ? &nl : NULL,
                                  use_reserved ? &res : NULL, use_type ? &t : NULL, use_data ? data : NULL,
                                  use_datalen ? &dl : NULL));
    oracle_case(cname);
    o_status("ret", st);
    o_lasterr();
    if (use_namelen) o_u32("name_len", nl);
    if (use_name && !st) o_wstr("name", name);
    if (use_type) o_u32("type", t);
    if (use_datalen) o_u32("data_len", dl);
    if (use_data) o_bytes("data", data, 10);
    oracle_case_end();
}

int main(void)
{
    oracle_begin("advapi32/RegEnumValueW_nullargs");
    root = oracle_reg_root();
    RegSetValueExW(root, L"Val", 0, REG_SZ, (const BYTE *)L"abcd", 10);

    /*                               name nlen res type data dlen */
    e_case("all", root,               1, 1, 0, 1, 1, 1);
    e_case("no_type", root,           1, 1, 0, 0, 1, 1);
    e_case("no_data_no_len", root,    1, 1, 0, 1, 0, 0);
    e_case("no_data_with_len", root,  1, 1, 0, 1, 0, 1);
    e_case("data_without_len", root,  1, 1, 0, 1, 1, 0);
    e_case("reserved_nonnull", root,  1, 1, 1, 1, 1, 1);
    e_case("null_name_buffer", root,  0, 1, 0, 1, 1, 1);
    e_case("null_name_len", root,     1, 0, 0, 1, 1, 1);
    {
        HKEY k = NULL;
        RegOpenKeyExW(HKEY_CURRENT_USER, BASE, 0, KEY_SET_VALUE, &k);
        e_case("set_value_only_handle", k, 1, 1, 0, 1, 1, 1);
        RegCloseKey(k);
        RegOpenKeyExW(HKEY_CURRENT_USER, BASE, 0, KEY_ENUMERATE_SUB_KEYS, &k);
        e_case("enumerate_sub_keys_only_handle", k, 1, 1, 0, 1, 1, 1);
        RegCloseKey(k);
        RegOpenKeyExW(HKEY_CURRENT_USER, BASE, 0, KEY_QUERY_VALUE, &k);
        e_case("query_value_only_handle", k, 1, 1, 0, 1, 1, 1);
        RegCloseKey(k);
    }
    e_case("invalid_handle", (HKEY)(ULONG_PTR)0x1234, 1, 1, 0, 1, 1, 1);
    e_case("null_handle", NULL, 1, 1, 0, 1, 1, 1);

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
