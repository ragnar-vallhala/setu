/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: "regedit /e file key" export text (Registry Editor Version 5.00
 * format) for every value type, escaping, line wrapping of long hex data and
 * nested keys; and an export -> delete -> "regedit /s" import round trip. Only
 * keys under the test's own key are exported, and only existing ones (a failed
 * export would show a message box).
 *
 * Docs: https://learn.microsoft.com/en-us/troubleshoot/windows-server/performance/add-modify-delete-registry-subkeys-values
 */
#include "../reg/regtool.h"

#define K L"HKEY_CURRENT_USER\\Software\\SetuOracle\\regedit_export"
#define SUB L"Software\\SetuOracle\\regedit_export"

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

static void export_case(const char *name, const wchar_t *cmd, const wchar_t *file)
{
    rt_run(cmd);
    oracle_case(name);
    o_u32("exit", rt_exit);
    o_str("out", rt_out);
    o_str("err", rt_err);
    o_text_file("file", file);
    oracle_case_end();
}

int main(void)
{
    oracle_begin("regedit/export");
    HKEY root = oracle_reg_root();
    HKEY d = mk(root, L"data");
    HKEY s = mk(root, L"data\\sub");
    HKEY s2 = mk(root, L"data\\sub\\leaf");
    HKEY e = mk(root, L"emptykey");
    DWORD dw = 0x89abcdef;
    ULONGLONG qw = 0xa1b2c3d4e5f60718ULL;
    BYTE big[90];
    for (int i = 0; i < 90; i++) big[i] = (BYTE)(i * 3);
    sz(d, NULL, L"dflt");
    sz(d, L"sz", L"tab\there \"quoted\" back\\slash");
    sz(d, L"name with \"quotes\"", L"v");
    sz(d, L"empty", L"");
    RegSetValueExW(d, L"dw", 0, REG_DWORD, (const BYTE *)&dw, 4);
    RegSetValueExW(d, L"qw", 0, REG_QWORD, (const BYTE *)&qw, 8);
    RegSetValueExW(d, L"bin_long", 0, REG_BINARY, big, sizeof big);
    RegSetValueExW(d, L"multi", 0, REG_MULTI_SZ, (const BYTE *)L"first\0second\0", 28);
    RegSetValueExW(d, L"expand", 0, REG_EXPAND_SZ, (const BYTE *)L"%WINDIR%\\a", 22);
    RegSetValueExW(d, L"none", 0, REG_NONE, NULL, 0);
    RegSetValueExW(d, L"type_99", 0, 99, big, 4);
    RegSetValueExW(d, L"a_very_long_value_name_that_pushes_the_hex_data_further_right", 0,
                   REG_BINARY, big, 40);
    sz(s, L"in_sub", L"1");
    sz(s2, L"in_leaf", L"2");

    export_case("export_data", L"regedit.exe /e data.reg " K L"\\data", L"data.reg");
    export_case("export_leaf", L"regedit.exe /e leaf.reg " K L"\\data\\sub\\leaf", L"leaf.reg");
    export_case("export_empty_key", L"regedit.exe /e empty.reg " K L"\\emptykey", L"empty.reg");
    export_case("export_short_root", L"regedit.exe /e short.reg HKCU\\Software\\SetuOracle\\regedit_export\\data\\sub", L"short.reg");
    export_case("export_overwrites", L"regedit.exe /e data.reg " K L"\\emptykey", L"data.reg");

    {   /* Round trip: export, delete, import, compare trees. */
        oracle_case("before");
        o_regtree("tree", HKEY_CURRENT_USER, SUB L"\\data");
        oracle_case_end();
        rt_run(L"regedit.exe /e rt.reg " K L"\\data");
        RegCloseKey(s2);
        RegCloseKey(s);
        RegCloseKey(d);
        RegDeleteTreeW(root, L"data");
        RegDeleteKeyW(root, L"data");
        rt_run(L"regedit.exe /s rt.reg");
        oracle_case("after_round_trip");
        o_u32("exit", rt_exit);
        o_regtree("tree", HKEY_CURRENT_USER, SUB L"\\data");
        oracle_case_end();
    }

    RegCloseKey(e);
    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
