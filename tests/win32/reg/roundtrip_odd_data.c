/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: round trips of values whose size does not fit their type
 * through "reg export" + "reg import", "reg save" + "reg restore" and
 * "reg copy". Each step prints reg.exe's exit code and output and the key
 * tree afterwards, so the cases show which data each path keeps or changes.
 * The hive file written by reg save is only read back by reg restore.
 *
 * Docs: https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/reg-export
 *       https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/reg-save
 *       https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/reg-restore
 *       https://learn.microsoft.com/en-us/windows/win32/sysinfo/registry-value-types
 */
#include "odd_data.h"

#define K L"HKCU\\Software\\SetuOracle\\reg_roundtrip_odd_data"
#define SUB L"Software\\SetuOracle\\reg_roundtrip_odd_data"

static void step(const char *name, const wchar_t *cmd, const wchar_t *tree)
{
    oracle_case(name);
    rt_fields(cmd);
    if (tree) {
        wchar_t sub[256];
        swprintf(sub, 256, L"%ls\\%ls", SUB, tree);
        o_regtree("tree", HKEY_CURRENT_USER, sub);
    }
    oracle_case_end();
}

int main(void)
{
    oracle_begin("reg/roundtrip_odd_data");
    HKEY root = oracle_reg_root(), d, s;
    RegCreateKeyExW(root, L"d", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &d, NULL);
    RegCreateKeyExW(root, L"scratch", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &s, NULL);
    RegCloseKey(s);
    od_set_all(d);
    RegCloseKey(d);

    oracle_case("original");
    o_regtree("tree", HKEY_CURRENT_USER, SUB L"\\d");
    oracle_case_end();

    DeleteFileW(L"odd.reg");
    DeleteFileW(L"odd.hiv");
    step("export", L"reg.exe export " K L"\\d odd.reg /y", NULL);
    step("save", L"reg.exe save " K L"\\d odd.hiv /y", NULL);
    step("copy", L"reg.exe copy " K L"\\d " K L"\\copied /s /f", L"copied");
    step("restore_into_scratch", L"reg.exe restore " K L"\\scratch odd.hiv", L"scratch");
    RegDeleteTreeW(root, L"d");
    RegDeleteKeyW(root, L"d");
    step("import_after_delete", L"reg.exe import odd.reg", L"d");

    DeleteFileW(L"odd.reg");
    DeleteFileW(L"odd.hiv");
    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
