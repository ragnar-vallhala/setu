/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: RegGetValueW's lpSubKey argument: NULL, "", a direct and a
 * nested subkey, a missing subkey, a trailing and a leading backslash,
 * different case, the default value of a subkey (NULL and "" value names),
 * and a key handle without KEY_QUERY_VALUE whose subkey is readable.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-reggetvaluew
 */
#include "oracle.h"

static HKEY root;

static void g_case(const char *cname, HKEY key, const wchar_t *sub, const wchar_t *value)
{
    unsigned char b[32];
    memset(b, 0xCC, sizeof b);
    DWORD t = 0xAAAAAAAAu, sz = sizeof b;
    LONG st;
    ORACLE_CALL(st, RegGetValueW(key, sub, value, RRF_RT_ANY, &t, b, &sz));
    oracle_case(cname);
    o_status("ret", st);
    o_lasterr();
    o_u32("type", t);
    o_u32("size", sz);
    if (!st) o_bytes("data", b, sz);
    oracle_case_end();
}

static void setdw(HKEY k, const wchar_t *sub, const wchar_t *name, DWORD v)
{
    HKEY s = NULL;
    RegCreateKeyExW(k, sub, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &s, NULL);
    RegSetValueExW(s, name, 0, REG_DWORD, (const BYTE *)&v, 4);
    RegCloseKey(s);
}

int main(void)
{
    oracle_begin("advapi32/RegGetValueW_subkey");
    root = oracle_reg_root();
    setdw(root, L"a", L"V", 1);
    setdw(root, L"a\\b", L"V", 2);
    setdw(root, L"a", NULL, 3);
    {
        DWORD v = 4;
        RegSetValueExW(root, L"V", 0, REG_DWORD, (const BYTE *)&v, 4);
    }

    g_case("null_subkey", root, NULL, L"V");
    g_case("empty_subkey", root, L"", L"V");
    g_case("direct_subkey", root, L"a", L"V");
    g_case("nested_subkey", root, L"a\\b", L"V");
    g_case("missing_subkey", root, L"nope", L"V");
    g_case("missing_value_in_subkey", root, L"a", L"Nope");
    g_case("subkey_other_case", root, L"A\\B", L"V");
    g_case("trailing_backslash", root, L"a\\", L"V");
    g_case("leading_backslash", root, L"\\a", L"V");
    g_case("double_backslash", root, L"a\\\\b", L"V");
    g_case("default_null_name", root, L"a", NULL);
    g_case("default_empty_name", root, L"a", L"");
    g_case("default_unset_in_nested", root, L"a\\b", NULL);
    g_case("from_hkcu_full_path", HKEY_CURRENT_USER, ORACLE_REG_BASE L"\\advapi32_RegGetValueW_subkey\\a", L"V");
    g_case("invalid_handle", (HKEY)(ULONG_PTR)0x1234, L"a", L"V");
    {
        HKEY k = NULL;
        RegOpenKeyExW(HKEY_CURRENT_USER, ORACLE_REG_BASE L"\\advapi32_RegGetValueW_subkey", 0,
                      KEY_ENUMERATE_SUB_KEYS, &k);
        g_case("parent_without_query_value_subkey", k, L"a", L"V");
        g_case("parent_without_query_value_own", k, NULL, L"V");
        RegCloseKey(k);
    }

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
