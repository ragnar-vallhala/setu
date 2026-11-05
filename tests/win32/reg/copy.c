/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: "reg copy" with and without /s, into new and existing
 * destination keys (/f), onto itself or into its own subtree, from missing
 * keys, and the resulting registry trees.
 *
 * Docs: https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/reg-copy
 */
#include "regtool.h"

#define K L"HKCU\\Software\\SetuOracle\\reg_copy"
#define SUB L"Software\\SetuOracle\\reg_copy"

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

static void copy_case(const char *name, const wchar_t *cmd, const wchar_t *dest)
{
    wchar_t p[256];
    rt_run(cmd);
    oracle_case(name);
    o_u32("exit", rt_exit);
    o_str("out", rt_out);
    o_str("err", rt_err);
    swprintf(p, 256, L"%ls\\%ls", SUB, dest);
    o_regtree("dest", HKEY_CURRENT_USER, p);
    oracle_case_end();
}

int main(void)
{
    oracle_begin("reg/copy");
    HKEY root = oracle_reg_root();
    HKEY src = mk(root, L"src");
    HKEY s1 = mk(root, L"src\\child");
    HKEY s2 = mk(root, L"src\\child\\grand");
    HKEY ex = mk(root, L"existing");
    DWORD dw = 42;
    BYTE bin[] = {1, 2, 3};
    sz(src, NULL, L"src default");
    sz(src, L"name", L"value");
    RegSetValueExW(src, L"num", 0, REG_DWORD, (const BYTE *)&dw, 4);
    RegSetValueExW(src, L"bin", 0, REG_BINARY, bin, 3);
    RegSetValueExW(src, L"multi", 0, REG_MULTI_SZ, (const BYTE *)L"a\0b\0", 10);
    sz(s1, L"childv", L"c");
    sz(s2, L"grandv", L"g");
    sz(ex, L"name", L"old");
    sz(ex, L"keep", L"kept");

    copy_case("copy_values_only", L"reg.exe copy " K L"\\src " K L"\\d1 /f", L"d1");
    copy_case("copy_recursive", L"reg.exe copy " K L"\\src " K L"\\d2 /s /f", L"d2");
    copy_case("copy_into_existing_f", L"reg.exe copy " K L"\\src " K L"\\existing /f", L"existing");
    copy_case("copy_new_dest_no_f", L"reg.exe copy " K L"\\src\\child\\grand " K L"\\d3", L"d3");
    copy_case("copy_onto_itself", L"reg.exe copy " K L"\\src " K L"\\src /f", L"src");
    copy_case("copy_missing_source", L"reg.exe copy " K L"\\nosuch " K L"\\d4 /f", L"d4");
    copy_case("copy_bad_dest_root", L"reg.exe copy " K L"\\src HKXX\\x /f", L"d5");
    copy_case("copy_missing_dest_arg", L"reg.exe copy " K L"\\src", L"d6");
    copy_case("copy_s_twice", L"reg.exe copy " K L"\\src " K L"\\d7 /s /s /f", L"d7");
    copy_case("copy_unknown_switch", L"reg.exe copy " K L"\\src " K L"\\d8 /q /f", L"d8");
    copy_case("copy_dest_other_case", L"reg.exe copy " K L"\\src\\child " K L"\\D1 /f", L"d1");

    RegCloseKey(ex);
    RegCloseKey(s2);
    RegCloseKey(s1);
    RegCloseKey(src);
    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
