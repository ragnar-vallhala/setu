/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: RegDeleteKeyValueW (and RegDeleteKeyValueA): NULL and ""
 * subkeys, existing and missing subkeys and values, the default value,
 * case-insensitive names, a parent opened read-only or with only
 * KEY_SET_VALUE, and invalid handles. The key itself must stay.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regdeletekeyvaluew
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regdeletekeyvaluea
 */
#include "oracle.h"

#define BASE ORACLE_REG_BASE L"\\advapi32_RegDeleteKeyValueW"

static HKEY root;

static void put(const wchar_t *sub, const wchar_t *name)
{
    DWORD dw = 1;
    RegSetKeyValueW(root, sub, name, REG_DWORD, &dw, 4);
}

static void d_case(const char *cname, HKEY key, const wchar_t *sub, const wchar_t *value,
                   const wchar_t *check_sub, const wchar_t *check_value)
{
    LONG st;
    ORACLE_CALL(st, RegDeleteKeyValueW(key, sub, value));
    oracle_case(cname);
    o_status("ret", st);
    o_lasterr();
    HKEY k = NULL;
    LONG o = RegOpenKeyExW(root, check_sub, 0, KEY_READ, &k);
    o_status("key_still_opens", o);
    if (!o) {
        o_status("value_query", RegQueryValueExW(k, check_value, NULL, NULL, NULL, NULL));
        RegCloseKey(k);
    }
    oracle_case_end();
}

static void a_case(const char *cname, const char *sub, const char *value, const wchar_t *check_sub,
                   const wchar_t *check_value)
{
    LONG st;
    ORACLE_CALL(st, RegDeleteKeyValueA(root, sub, value));
    oracle_case(cname);
    o_status("ret", st);
    o_lasterr();
    HKEY k = NULL;
    LONG o = RegOpenKeyExW(root, check_sub, 0, KEY_READ, &k);
    o_status("key_still_opens", o);
    if (!o) {
        o_status("value_query", RegQueryValueExW(k, check_value, NULL, NULL, NULL, NULL));
        RegCloseKey(k);
    }
    oracle_case_end();
}

int main(void)
{
    oracle_begin("advapi32/RegDeleteKeyValueW");
    root = oracle_reg_root();
    put(L"s", L"V");
    put(L"s", L"Other");
    put(L"s", NULL);
    put(L"s\\t", L"V");
    put(NULL, L"Own");
    put(NULL, L"Own2");

    d_case("existing", root, L"s", L"V", L"s", L"V");
    d_case("again_missing_value", root, L"s", L"V", L"s", L"V");
    d_case("other_case", root, L"S", L"OTHER", L"s", L"Other");
    d_case("missing_subkey", root, L"nope", L"V", L"s", L"V");
    d_case("default_null", root, L"s", NULL, L"s", NULL);
    d_case("default_empty_already_gone", root, L"s", L"", L"s", NULL);
    d_case("nested", root, L"s\\t", L"V", L"s\\t", L"V");
    d_case("null_subkey_own", root, NULL, L"Own", NULL, L"Own");
    d_case("empty_subkey_own", root, L"", L"Own2", NULL, L"Own2");
    put(L"s", L"Ro");
    {
        HKEY ro = NULL;
        RegOpenKeyExW(HKEY_CURRENT_USER, BASE, 0, KEY_READ, &ro);
        d_case("readonly_parent", ro, L"s", L"Ro", L"s", L"Ro");
        RegCloseKey(ro);
        HKEY so = NULL;
        RegOpenKeyExW(HKEY_CURRENT_USER, BASE, 0, KEY_SET_VALUE, &so);
        d_case("set_value_only_parent", so, L"s", L"Ro", L"s", L"Ro");
        RegCloseKey(so);
    }
    d_case("invalid_handle", (HKEY)(ULONG_PTR)0x1234, L"s", L"V", L"s", L"V");
    put(L"s", L"AVal");
    put(NULL, L"AOwn");
    a_case("ansi_existing", "s", "AVal", L"s", L"AVal");
    a_case("ansi_missing", "s", "AVal", L"s", L"AVal");
    a_case("ansi_null_subkey", NULL, "AOwn", NULL, L"AOwn");
    a_case("ansi_missing_subkey", "nope", "AOwn", NULL, L"AOwn");

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
