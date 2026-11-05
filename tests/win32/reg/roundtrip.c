/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: round trips through reg.exe: export then import gives back the
 * same tree; save then restore into a scratch key gives back the same values
 * (the saved hive file itself is never inspected); save/restore and
 * load/unload error cases.
 *
 * Docs: https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/reg-export
 *       https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/reg-import
 *       https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/reg-save
 *       https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/reg-restore
 *       https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/reg-load
 *       https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/reg-unload
 */
#include "regtool.h"

#define K L"HKCU\\Software\\SetuOracle\\reg_roundtrip"
#define SUB L"Software\\SetuOracle\\reg_roundtrip"

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

static void step(const char *name, const wchar_t *cmd, const wchar_t *tree)
{
    rt_run(cmd);
    oracle_case(name);
    o_u32("exit", rt_exit);
    o_str("out", rt_out);
    o_str("err", rt_err);
    if (tree) {
        wchar_t p[256];
        swprintf(p, 256, L"%ls\\%ls", SUB, tree);
        o_regtree("tree", HKEY_CURRENT_USER, p);
    }
    oracle_case_end();
}

int main(void)
{
    oracle_begin("reg/roundtrip");
    DeleteFileW(L"rt.hiv");   /* from an earlier run: "reg save" would prompt */
    rt_run(L"reg.exe unload HKU\\SetuOracleRoundtrip");
    HKEY root = oracle_reg_root();
    HKEY d = mk(root, L"data");
    HKEY d1 = mk(root, L"data\\one");
    HKEY d2 = mk(root, L"data\\one\\two");
    HKEY scratch = mk(root, L"scratch");
    DWORD dw = 0xfeedface;
    ULONGLONG qw = 12345678901234ULL;
    BYTE bin[40];
    for (int i = 0; i < 40; i++) bin[i] = (BYTE)(255 - i);
    sz(d, NULL, L"default");
    sz(d, L"sz", L"quote \" back \\ end");
    RegSetValueExW(d, L"dw", 0, REG_DWORD, (const BYTE *)&dw, 4);
    RegSetValueExW(d, L"qw", 0, REG_QWORD, (const BYTE *)&qw, 8);
    RegSetValueExW(d, L"bin", 0, REG_BINARY, bin, sizeof bin);
    RegSetValueExW(d, L"multi", 0, REG_MULTI_SZ, (const BYTE *)L"x\0yy\0\0", 14);
    RegSetValueExW(d, L"exp", 0, REG_EXPAND_SZ, (const BYTE *)L"%PATH%;x", 18);
    RegSetValueExW(d, L"none", 0, REG_NONE, bin, 4);
    sz(d1, L"v1", L"one");
    sz(d2, L"v2", L"two");
    RegCloseKey(d2);
    RegCloseKey(d1);
    RegCloseKey(d);

    oracle_case("original");
    o_regtree("tree", HKEY_CURRENT_USER, SUB L"\\data");
    oracle_case_end();

    step("export", L"reg.exe export " K L"\\data rt.reg /y", NULL);
    RegDeleteTreeW(root, L"data");
    RegDeleteKeyW(root, L"data");
    step("import_after_delete", L"reg.exe import rt.reg", L"data");

    step("save", L"reg.exe save " K L"\\data rt.hiv", NULL);
    step("save_existing_y", L"reg.exe save " K L"\\data rt.hiv /y", NULL);
    step("restore_into_scratch", L"reg.exe restore " K L"\\scratch rt.hiv", L"scratch");
    step("restore_missing_file", L"reg.exe restore " K L"\\scratch nosuch.hiv", NULL);
    step("restore_missing_key", L"reg.exe restore " K L"\\nosuch rt.hiv", NULL);
    step("save_missing_key", L"reg.exe save " K L"\\nosuch x.hiv", NULL);
    step("save_no_file_arg", L"reg.exe save " K L"\\data", NULL);
    step("load_hku", L"reg.exe load HKU\\SetuOracleRoundtrip rt.hiv", NULL);
    oracle_case("loaded_tree");
    o_regtree("tree", HKEY_USERS, L"SetuOracleRoundtrip");
    oracle_case_end();
    step("load_again", L"reg.exe load HKU\\SetuOracleRoundtrip rt.hiv", NULL);
    step("unload_hku", L"reg.exe unload HKU\\SetuOracleRoundtrip", NULL);
    oracle_case("after_unload");
    {
        HKEY k;
        LONG st = RegOpenKeyExW(HKEY_USERS, L"SetuOracleRoundtrip", 0, KEY_READ, &k);
        o_status("open", st);
        if (!st) RegCloseKey(k);
    }
    oracle_case_end();
    step("load_not_under_hku_or_hklm", L"reg.exe load " K L"\\loaded rt.hiv", NULL);
    step("load_missing_file", L"reg.exe load HKU\\SetuOracleRoundtrip nosuch.hiv", NULL);
    step("unload_missing", L"reg.exe unload HKU\\SetuOracleRoundtripNone", NULL);
    step("load_no_args", L"reg.exe load", NULL);
    step("unload_no_args", L"reg.exe unload", NULL);

    /* Nothing may stay loaded: the hive file would stay in use. */
    rt_run(L"reg.exe unload " K L"\\loaded");
    rt_run(L"reg.exe unload HKU\\SetuOracleRoundtrip");
    RegCloseKey(scratch);
    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
