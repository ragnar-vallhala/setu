/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: "reg query" /s, /f (with /k, /d, /c, /e), /t, /se and /z.
 *
 * Docs: https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/reg-query
 */
#include "regtool.h"

#define K L"HKCU\\Software\\SetuOracle\\reg_query_recursive"

static void sz(HKEY k, const wchar_t *name, const wchar_t *v)
{
    RegSetValueExW(k, name, 0, REG_SZ, (const BYTE *)v, (DWORD)(wcslen(v) + 1) * 2);
}

static void dw(HKEY k, const wchar_t *name, DWORD v)
{
    RegSetValueExW(k, name, 0, REG_DWORD, (const BYTE *)&v, 4);
}

static HKEY mk(HKEY root, const wchar_t *path)
{
    HKEY k = NULL;
    RegCreateKeyExW(root, path, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL);
    return k;
}

int main(void)
{
    oracle_begin("reg/query_recursive");
    HKEY root = oracle_reg_root();
    HKEY a = mk(root, L"alpha");
    HKEY ab = mk(root, L"alpha\\beta");
    HKEY g = mk(root, L"gamma");
    HKEY e = mk(root, L"alpha\\empty");

    sz(root, L"top", L"Apple pie");
    dw(root, L"count", 7);
    sz(a, L"fruit", L"apple");
    sz(a, L"Wine", L"first");
    RegSetValueExW(a, L"list", 0, REG_MULTI_SZ, (const BYTE *)L"x\0apple\0z\0", 22);
    sz(ab, L"Wine", L"second");
    dw(ab, L"num", 0x10);
    sz(ab, NULL, L"beta default");
    sz(g, L"APPLE", L"name match");

    rt_case("s_whole_tree", L"reg.exe query " K L" /s");
    rt_case("s_v_name", L"reg.exe query " K L" /v Wine /s");
    rt_case("s_ve", L"reg.exe query " K L" /ve /s");
    rt_case("f_default_scope", L"reg.exe query " K L" /f apple");
    rt_case("f_with_s", L"reg.exe query " K L" /f apple /s");
    rt_case("f_keys_only", L"reg.exe query " K L" /f beta /k /s");
    rt_case("f_data_only", L"reg.exe query " K L" /f apple /d /s");
    rt_case("f_case_sensitive", L"reg.exe query " K L" /f Apple /c /s");
    rt_case("f_exact", L"reg.exe query " K L" /f apple /e /s");
    rt_case("f_exact_data", L"reg.exe query " K L" /f apple /e /d /s");
    rt_case("f_wildcard", L"reg.exe query " K L" /f * /s");
    rt_case("f_no_match", L"reg.exe query " K L" /f zzzz /s");
    rt_case("f_value_names_only", L"reg.exe query " K L" /f wine /v /s");
    rt_case("t_sz", L"reg.exe query " K L" /t REG_SZ /s");
    rt_case("t_dword_and_multi", L"reg.exe query " K L" /t REG_DWORD,REG_MULTI_SZ /s");
    rt_case("t_bad_type", L"reg.exe query " K L" /t REG_FOO");
    rt_case("se_separator", L"reg.exe query " K L"\\alpha /v list /se #");
    rt_case("z_verbose", L"reg.exe query " K L"\\alpha\\beta /z");
    rt_case("s_twice", L"reg.exe query " K L" /s /s");

    RegCloseKey(e);
    RegCloseKey(g);
    RegCloseKey(ab);
    RegCloseKey(a);
    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
