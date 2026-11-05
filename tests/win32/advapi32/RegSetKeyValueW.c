/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: RegSetKeyValueW: NULL and "" subkeys, an existing and a
 * missing subkey (is it created?), nested missing subkeys, the default
 * value, string data sizes, NULL data, invalid handles and a parent
 * opened read-only.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regsetkeyvaluew
 */
#include "oracle.h"

#define BASE ORACLE_REG_BASE L"\\advapi32_RegSetKeyValueW"

static HKEY root;

static void k_case(const char *cname, HKEY key, const wchar_t *sub, const wchar_t *value, DWORD type,
                   const void *data, DWORD size)
{
    LONG st;
    ORACLE_CALL(st, RegSetKeyValueW(key, sub, value, type, data, size));
    oracle_case(cname);
    o_status("ret", st);
    o_lasterr();
    HKEY k = NULL;
    LONG o = RegOpenKeyExW(root, sub, 0, KEY_READ, &k);
    o_status("subkey_open", o);
    if (!o) {
        unsigned char b[64];
        DWORD t = 0, sz = sizeof b;
        LONG q = RegQueryValueExW(k, value, NULL, &t, b, &sz);
        o_status("q_ret", q);
        if (!q) { o_u32("q_type", t); o_u32("q_size", sz); o_bytes("q_data", b, sz); }
        RegCloseKey(k);
    }
    oracle_case_end();
}

int main(void)
{
    oracle_begin("advapi32/RegSetKeyValueW");
    root = oracle_reg_root();
    HKEY s = NULL;
    RegCreateKeyExW(root, L"exists", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &s, NULL);
    RegCloseKey(s);
    DWORD dw = 0x1234;
    static const wchar_t hi[] = L"hi";

    k_case("null_subkey", root, NULL, L"V", REG_DWORD, &dw, 4);
    k_case("empty_subkey", root, L"", L"V2", REG_DWORD, &dw, 4);
    k_case("existing_subkey", root, L"exists", L"V", REG_SZ, hi, sizeof hi);
    k_case("missing_subkey_created", root, L"newkey", L"V", REG_DWORD, &dw, 4);
    k_case("nested_missing_subkeys", root, L"n1\\n2\\n3", L"V", REG_DWORD, &dw, 4);
    k_case("default_value_null", root, L"exists", NULL, REG_SZ, hi, sizeof hi);
    k_case("default_value_empty", root, L"exists", L"", REG_SZ, L"x", 4);
    k_case("sz_without_nul", root, L"exists", L"NoNul", REG_SZ, hi, 4);
    k_case("sz_odd", root, L"exists", L"Odd", REG_SZ, hi, 3);
    k_case("null_data_zero", root, L"exists", L"Empty", REG_BINARY, NULL, 0);
    k_case("null_data_nonzero", root, L"exists", L"NullData", REG_BINARY, NULL, 4);
    k_case("trailing_backslash", root, L"exists\\", L"Tb", REG_DWORD, &dw, 4);
    k_case("invalid_handle", (HKEY)(ULONG_PTR)0x1234, L"exists", L"Inv", REG_DWORD, &dw, 4);
    {
        HKEY ro = NULL;
        RegOpenKeyExW(HKEY_CURRENT_USER, BASE, 0, KEY_READ, &ro);
        k_case("readonly_parent_existing_subkey", ro, L"exists", L"Ro", REG_DWORD, &dw, 4);
        k_case("readonly_parent_new_subkey", ro, L"rokey", L"Ro", REG_DWORD, &dw, 4);
        k_case("readonly_parent_own_value", ro, NULL, L"Ro", REG_DWORD, &dw, 4);
        RegCloseKey(ro);
    }
    {
        HKEY so = NULL;
        RegOpenKeyExW(HKEY_CURRENT_USER, BASE, 0, KEY_SET_VALUE, &so);
        k_case("set_value_only_parent_own", so, NULL, L"So", REG_DWORD, &dw, 4);
        k_case("set_value_only_parent_subkey", so, L"exists", L"So", REG_DWORD, &dw, 4);
        RegCloseKey(so);
    }
    oracle_case("new_subkey_is_non_volatile_with_no_class");
    {
        HKEY k = NULL;
        RegOpenKeyExW(root, L"newkey", 0, KEY_READ, &k);
        wchar_t cls[16];
        DWORD clen = 16, nsub = 9, nval = 9;
        LONG r = RegQueryInfoKeyW(k, cls, &clen, NULL, &nsub, NULL, NULL, &nval, NULL, NULL, NULL, NULL);
        o_status("info_ret", r);
        o_u32("class_len", clen);
        o_u32("subkeys", nsub);
        o_u32("values", nval);
        RegCloseKey(k);
    }
    oracle_case_end();

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
