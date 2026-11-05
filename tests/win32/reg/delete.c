/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: "reg delete" of values (/v, /ve, /va) and whole keys with /f,
 * missing keys and values, conflicting switches, and the registry state after
 * each step. Prompts are never answered: every destructive case uses /f, and
 * stdin is at end-of-file.
 *
 * Docs: https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/reg-delete
 */
#include "regtool.h"

#define K L"HKCU\\Software\\SetuOracle\\reg_delete"
#define SUB L"Software\\SetuOracle\\reg_delete"

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

static void del_case(const char *name, const wchar_t *cmd)
{
    rt_run(cmd);
    oracle_case(name);
    o_u32("exit", rt_exit);
    o_str("out", rt_out);
    o_str("err", rt_err);
    o_regtree("tree", HKEY_CURRENT_USER, SUB);
    oracle_case_end();
}

int main(void)
{
    oracle_begin("reg/delete");
    HKEY root = oracle_reg_root();
    HKEY a = mk(root, L"a");
    HKEY b = mk(root, L"a\\b");
    HKEY c = mk(root, L"c");
    sz(root, NULL, L"root default");
    sz(root, L"v1", L"one");
    sz(root, L"v2", L"two");
    sz(root, L"V3", L"three");
    sz(a, L"av", L"x");
    sz(a, L"aw", L"y");
    sz(a, NULL, L"a default");
    sz(b, L"bv", L"z");
    sz(c, L"cv", L"w");

    del_case("v_existing", L"reg.exe delete " K L" /v v1 /f");
    del_case("v_missing", L"reg.exe delete " K L" /v v1 /f");
    del_case("v_other_case", L"reg.exe delete " K L" /v v3 /f");
    del_case("ve_existing", L"reg.exe delete " K L" /ve /f");
    del_case("ve_missing", L"reg.exe delete " K L" /ve /f");
    del_case("va_keeps_subkeys", L"reg.exe delete " K L"\\a /va /f");
    del_case("va_empty_key", L"reg.exe delete " K L"\\a /va /f");
    del_case("v_and_ve", L"reg.exe delete " K L" /v v2 /ve /f");
    del_case("v_and_va", L"reg.exe delete " K L" /v v2 /va /f");
    del_case("v_no_name", L"reg.exe delete " K L" /v");
    del_case("key_missing", L"reg.exe delete " K L"\\nosuch /f");
    del_case("value_in_missing_key", L"reg.exe delete " K L"\\nosuch /v x /f");
    del_case("key_with_subkeys", L"reg.exe delete " K L"\\a /f");
    del_case("key_other_case", L"reg.exe delete " K L"\\C /f");
    del_case("bad_root", L"reg.exe delete HKXX\\Software\\x /f");
    del_case("unknown_switch", L"reg.exe delete " K L" /v v2 /q /f");
    del_case("f_twice", L"reg.exe delete " K L" /v v2 /f /f");
    del_case("v_last_value", L"reg.exe delete " K L" /v v2 /f");

    RegCloseKey(c);
    RegCloseKey(b);
    RegCloseKey(a);
    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
