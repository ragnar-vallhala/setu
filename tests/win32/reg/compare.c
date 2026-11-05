/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: "reg compare" of two keys: identical and different trees, one
 * value (/v), the default value (/ve), recursion (/s), the output filters
 * /oa /od /os /on, missing keys and syntax errors. Exit codes: 0 identical,
 * 2 different, 1 failure.
 *
 * Docs: https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/reg-compare
 */
#include "regtool.h"

#define K L"HKCU\\Software\\SetuOracle\\reg_compare"

static void sz(HKEY k, const wchar_t *name, const wchar_t *v)
{
    RegSetValueExW(k, name, 0, REG_SZ, (const BYTE *)v, (DWORD)(wcslen(v) + 1) * 2);
}

static HKEY mk(HKEY root, const wchar_t *path)
{
    HKEY k = NULL;
    RegCreateKeyExW(root, path, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL);
    return k;
}

int main(void)
{
    oracle_begin("reg/compare");
    HKEY root = oracle_reg_root();
    HKEY a = mk(root, L"a"), b = mk(root, L"b"), c = mk(root, L"c");
    HKEY as = mk(root, L"a\\sub"), bs = mk(root, L"b\\sub"), cs = mk(root, L"c\\sub");
    HKEY aonly = mk(root, L"a\\only_a"), bonly = mk(root, L"b\\only_b");
    DWORD one = 1, two = 2;

    /* a and c are identical; b differs from a. */
    HKEY same[2] = {a, c}, ssub[2] = {as, cs};
    for (int i = 0; i < 2; i++) {
        sz(same[i], L"same", L"x");
        sz(same[i], L"diff", L"left");
        sz(same[i], NULL, L"dflt");
        RegSetValueExW(same[i], L"num", 0, REG_DWORD, (const BYTE *)&one, 4);
        sz(ssub[i], L"inner", L"i1");
    }
    sz(a, L"left_only", L"l");
    sz(c, L"left_only", L"l");
    sz(b, L"same", L"x");
    sz(b, L"diff", L"right");
    sz(b, NULL, L"other dflt");
    RegSetValueExW(b, L"num", 0, REG_SZ, (const BYTE *)L"1", 4);
    sz(b, L"right_only", L"r");
    sz(bs, L"inner", L"i2");
    sz(bonly, L"x", L"y");
    sz(aonly, L"x", L"y");
    (void)two;

    rt_case("identical", L"reg.exe compare " K L"\\a " K L"\\c");
    rt_case("identical_s", L"reg.exe compare " K L"\\a " K L"\\c /s");
    rt_case("different", L"reg.exe compare " K L"\\a " K L"\\b");
    rt_case("different_s", L"reg.exe compare " K L"\\a " K L"\\b /s");
    rt_case("v_same", L"reg.exe compare " K L"\\a " K L"\\b /v same");
    rt_case("v_diff", L"reg.exe compare " K L"\\a " K L"\\b /v diff");
    rt_case("v_type_diff", L"reg.exe compare " K L"\\a " K L"\\b /v num");
    rt_case("v_missing_both", L"reg.exe compare " K L"\\a " K L"\\b /v nosuch");
    rt_case("v_missing_right", L"reg.exe compare " K L"\\a " K L"\\b /v left_only");
    rt_case("ve", L"reg.exe compare " K L"\\a " K L"\\b /ve");
    rt_case("oa", L"reg.exe compare " K L"\\a " K L"\\b /oa");
    rt_case("od", L"reg.exe compare " K L"\\a " K L"\\b /od");
    rt_case("os", L"reg.exe compare " K L"\\a " K L"\\b /os");
    rt_case("on", L"reg.exe compare " K L"\\a " K L"\\b /on");
    rt_case("os_identical", L"reg.exe compare " K L"\\a " K L"\\c /os");
    rt_case("missing_left", L"reg.exe compare " K L"\\nosuch " K L"\\b");
    rt_case("missing_right", L"reg.exe compare " K L"\\a " K L"\\nosuch");
    rt_case("same_key", L"reg.exe compare " K L"\\a " K L"\\a");
    rt_case("one_arg", L"reg.exe compare " K L"\\a");
    rt_case("oa_and_od", L"reg.exe compare " K L"\\a " K L"\\b /oa /od");
    rt_case("v_and_ve", L"reg.exe compare " K L"\\a " K L"\\b /v x /ve");

    HKEY ks[] = {bonly, aonly, cs, bs, as, c, b, a, root};
    for (unsigned i = 0; i < sizeof ks / sizeof ks[0]; i++) RegCloseKey(ks[i]);
    oracle_reg_cleanup();
    return oracle_end();
}
