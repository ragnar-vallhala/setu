/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: RegQueryValueExW through different handles: invalid, NULL and
 * closed handles, a handle to a key deleted after it was opened, handles
 * opened without KEY_QUERY_VALUE, and values of the default value of a
 * subkey reached through its parent (not possible: names are not paths).
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regqueryvalueexw
 *       https://learn.microsoft.com/en-us/windows/win32/sysinfo/registry-key-security-and-access-rights
 */
#include "oracle.h"

static HKEY root;

static void q_case(const char *cname, HKEY key, const wchar_t *value)
{
    unsigned char b[16];
    memset(b, 0xCC, sizeof b);
    DWORD t = 0xAAAAAAAAu, sz = sizeof b;
    LONG st;
    ORACLE_CALL(st, RegQueryValueExW(key, value, NULL, &t, b, &sz));
    oracle_case(cname);
    o_status("ret", st);
    o_lasterr();
    o_u32("type", t);
    o_u32("size", sz);
    if (!st) o_bytes("data", b, sz);
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
    oracle_begin("advapi32/RegQueryValueExW_handles");
    root = oracle_reg_root();
    DWORD dw = 9;
    HKEY sub = NULL;
    RegCreateKeyExW(root, L"sub", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &sub, NULL);
    RegSetValueExW(sub, L"V", 0, REG_DWORD, (const BYTE *)&dw, 4);
    RegSetValueExW(sub, NULL, 0, REG_SZ, (const BYTE *)L"def", 8);
    RegCloseKey(sub);

    q_case("invalid_handle", (HKEY)(ULONG_PTR)0x1234, L"V");
    q_case("null_handle", NULL, L"V");
    {
        HKEY k = open_sub(L"sub", KEY_READ);
        RegCloseKey(k);
        q_case("closed_handle", k, L"V");
    }
    {
        HKEY k = open_sub(L"sub", KEY_QUERY_VALUE);
        q_case("query_value_only", k, L"V");
        RegCloseKey(k);
        k = open_sub(L"sub", KEY_SET_VALUE);
        q_case("set_value_only", k, L"V");
        RegCloseKey(k);
        k = open_sub(L"sub", KEY_ENUMERATE_SUB_KEYS);
        q_case("enumerate_only", k, L"V");
        RegCloseKey(k);
        k = open_sub(L"sub", KEY_NOTIFY);
        q_case("notify_only", k, L"V");
        RegCloseKey(k);
        k = open_sub(L"sub", KEY_WRITE);
        q_case("key_write", k, L"V");
        RegCloseKey(k);
        k = open_sub(L"sub", READ_CONTROL);
        q_case("read_control_only", k, L"V");
        RegCloseKey(k);
        k = open_sub(L"sub", MAXIMUM_ALLOWED);
        q_case("maximum_allowed", k, L"V");
        RegCloseKey(k);
        k = open_sub(L"sub", GENERIC_READ);
        q_case("generic_read", k, L"V");
        RegCloseKey(k);
    }
    q_case("value_path_through_parent", root, L"sub\\V");
    q_case("default_of_subkey_by_name", root, L"sub");
    {
        HKEY k = open_sub(L"sub", KEY_READ);
        RegDeleteValueW(k, L"nothing");
        HKEY d = open_sub(L"sub", KEY_ALL_ACCESS);
        RegDeleteValueW(d, L"V");
        RegDeleteValueW(d, NULL);
        RegCloseKey(d);
        RegDeleteKeyW(root, L"sub");
        q_case("deleted_key", k, L"V");
        q_case("deleted_key_default", k, NULL);
        RegCloseKey(k);
    }

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
