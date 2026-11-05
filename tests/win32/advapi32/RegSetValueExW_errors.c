/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: RegSetValueExW error paths: a nonzero Reserved argument,
 * invalid, NULL and closed handles, a handle to a deleted key, handles
 * without KEY_SET_VALUE, NULL data with a nonzero size for several types,
 * and KEY_SET_VALUE-only handles (which must work).
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regsetvalueexw
 *       https://learn.microsoft.com/en-us/windows/win32/sysinfo/registry-key-security-and-access-rights
 */
#include "oracle.h"

#define BASE ORACLE_REG_BASE L"\\advapi32_RegSetValueExW_errors"

static HKEY root;

static void set_case(const char *cname, HKEY key, const wchar_t *value, DWORD reserved, DWORD type,
                     const void *data, DWORD size)
{
    LONG st;
    ORACLE_CALL(st, RegSetValueExW(key, value, reserved, type, data, size));
    oracle_case(cname);
    o_status("ret", st);
    o_lasterr();
    DWORD t = 0, sz = 0;
    LONG q = value ? RegQueryValueExW(root, value, NULL, &t, NULL, &sz) : -1;
    o_status("exists_ret", q);
    if (!q) { o_u32("exists_type", t); o_u32("exists_size", sz); }
    oracle_case_end();
}

static HKEY open_sub(const wchar_t *sub, REGSAM sam)
{
    HKEY k = NULL;
    RegOpenKeyExW(root, sub, 0, sam, &k);
    return k;
}

int main(void)
{
    oracle_begin("advapi32/RegSetValueExW_errors");
    root = oracle_reg_root();
    DWORD dw = 7;
    HKEY sub = NULL;
    RegCreateKeyExW(root, L"sub", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &sub, NULL);
    RegCloseKey(sub);

    set_case("reserved_nonzero", root, L"Res", 1, REG_DWORD, &dw, 4);
    set_case("reserved_high", root, L"ResHigh", 0x80000000u, REG_DWORD, &dw, 4);
    set_case("invalid_handle", (HKEY)(ULONG_PTR)0x1234, L"Inv", 0, REG_DWORD, &dw, 4);
    set_case("null_handle", NULL, L"Null", 0, REG_DWORD, &dw, 4);
    set_case("minus_one_handle", (HKEY)INVALID_HANDLE_VALUE, L"M1", 0, REG_DWORD, &dw, 4);
    {
        HKEY k = open_sub(L"sub", KEY_ALL_ACCESS);
        RegCloseKey(k);
        set_case("closed_handle", k, L"Closed", 0, REG_DWORD, &dw, 4);
    }
    {
        HKEY k = open_sub(NULL, KEY_QUERY_VALUE);
        set_case("query_only_handle", k, L"QOnly", 0, REG_DWORD, &dw, 4);
        RegCloseKey(k);
        k = open_sub(NULL, KEY_SET_VALUE);
        set_case("set_only_handle", k, L"SOnly", 0, REG_DWORD, &dw, 4);
        RegCloseKey(k);
        k = open_sub(NULL, KEY_CREATE_SUB_KEY);
        set_case("create_sub_key_only_handle", k, L"COnly", 0, REG_DWORD, &dw, 4);
        RegCloseKey(k);
        k = open_sub(NULL, KEY_WRITE);
        set_case("key_write_handle", k, L"WOnly", 0, REG_DWORD, &dw, 4);
        RegCloseKey(k);
        k = open_sub(NULL, KEY_ALL_ACCESS | KEY_WOW64_32KEY);
        set_case("wow64_32_flag_handle", k, L"W32", 0, REG_DWORD, &dw, 4);
        RegCloseKey(k);
    }
    {
        HKEY k = open_sub(L"sub", KEY_ALL_ACCESS);
        RegDeleteKeyW(root, L"sub");
        LONG st;
        ORACLE_CALL(st, RegSetValueExW(k, L"Gone", 0, REG_DWORD, (const BYTE *)&dw, 4));
        oracle_case("deleted_key_handle");
        o_status("ret", st);
        o_lasterr();
        DWORD t = 0, sz = 0;
        o_status("query_ret", RegQueryValueExW(k, L"Gone", NULL, &t, NULL, &sz));
        oracle_case_end();
        RegCloseKey(k);
    }
    set_case("null_data_binary_4", root, L"NdBin", 0, REG_BINARY, NULL, 4);
    set_case("null_data_dword_4", root, L"NdDw", 0, REG_DWORD, NULL, 4);
    set_case("null_data_sz_2", root, L"NdSz", 0, REG_SZ, NULL, 2);
    set_case("null_data_multi_4", root, L"NdMulti", 0, REG_MULTI_SZ, NULL, 4);
    set_case("null_data_none_0", root, L"NdNone0", 0, REG_NONE, NULL, 0);
    {
        HKEY k = NULL;
        RegOpenKeyExW(HKEY_CURRENT_USER, BASE, 0, KEY_READ, &k);
        set_case("key_read_handle", k, L"RdOnly", 0, REG_DWORD, &dw, 4);
        RegCloseKey(k);
    }
    {
        /* A handle opened with KEY_SET_VALUE plus a valid handle path. */
        HKEY k = NULL;
        LONG o = RegOpenKeyExW(HKEY_CURRENT_USER, BASE, 0, KEY_SET_VALUE, &k);
        oracle_case("open_set_value_only");
        o_status("open_ret", o);
        DWORD q = 0, sz = 4;
        o_status("query_via_set_only", RegQueryValueExW(k, L"SOnly", NULL, NULL, (BYTE *)&q, &sz));
        oracle_case_end();
        RegCloseKey(k);
    }
    /* Last: these may fault inside the API. */
    set_case("null_data_huge", root, L"NdHuge", 0, REG_BINARY, NULL, 0x7FFFFFFF);
    set_case("data_huge_size", root, L"Huge", 0, REG_BINARY, &dw, 0xFFFFFFFFu);

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
