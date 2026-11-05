/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: "reg export" .reg text (Registry Editor Version 5.00 format)
 * for every value type, escaping of names and string data, hex line wrapping,
 * default values, empty and nested keys, /y overwrite and error cases. The
 * exported files are compared as text: .reg files are a documented text format.
 *
 * Docs: https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/reg-export
 *       https://learn.microsoft.com/en-us/troubleshoot/windows-server/performance/add-modify-delete-registry-subkeys-values
 */
#include "regtool.h"

#define K L"HKCU\\Software\\SetuOracle\\reg_export"

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
    oracle_begin("reg/export");
    HKEY root = oracle_reg_root();
    HKEY t = mk(root, L"types");
    HKEY esc = mk(root, L"escape");
    HKEY n = mk(root, L"nest");
    HKEY n1 = mk(root, L"nest\\b_second");
    HKEY n2 = mk(root, L"nest\\a_first");
    HKEY n3 = mk(root, L"nest\\a_first\\deeper");
    HKEY e = mk(root, L"empty");

    DWORD dw = 0x00abcdef;
    ULONGLONG qw = 0x1122334455667788ULL;
    BYTE big[100];
    for (int i = 0; i < 100; i++) big[i] = (BYTE)(i * 7);
    sz(t, L"str", L"plain");
    sz(t, L"empty_str", L"");
    RegSetValueExW(t, L"dw", 0, REG_DWORD, (const BYTE *)&dw, 4);
    RegSetValueExW(t, L"qw", 0, REG_QWORD, (const BYTE *)&qw, 8);
    RegSetValueExW(t, L"bin", 0, REG_BINARY, big, 5);
    RegSetValueExW(t, L"bin_long", 0, REG_BINARY, big, sizeof big);
    RegSetValueExW(t, L"bin_empty", 0, REG_BINARY, NULL, 0);
    RegSetValueExW(t, L"multi", 0, REG_MULTI_SZ, (const BYTE *)L"ab\0cd\0", 14);
    RegSetValueExW(t, L"expand", 0, REG_EXPAND_SZ, (const BYTE *)L"%TEMP%", 14);
    RegSetValueExW(t, L"none", 0, REG_NONE, big, 3);
    RegSetValueExW(t, L"odd_type", 0, 0x77, big, 2);
    RegSetValueExW(t, L"dw_be", 0, REG_DWORD_BIG_ENDIAN, (const BYTE *)&dw, 4);
    RegSetValueExW(t, L"dw_short", 0, REG_DWORD, (const BYTE *)&dw, 2);
    RegSetValueExW(t, L"sz_no_nul", 0, REG_SZ, (const BYTE *)L"abc", 6);
    sz(t, NULL, L"types default");

    sz(esc, L"quote\"name", L"say \"hi\"");
    sz(esc, L"back\\slash", L"C:\\dir\\file");
    sz(esc, L"eq=name", L"a=b");
    sz(esc, L"newline", L"line1\nline2");
    sz(esc, L"tab", L"a\tb");
    sz(esc, L"unicode", L"caf\x00e9 \x4e2d");

    sz(n, L"v", L"top");
    sz(n2, L"v", L"a");
    sz(n1, L"v", L"b");
    sz(n3, L"v", L"deep");

    export_case("export_types", L"reg.exe export " K L"\\types types.reg", L"types.reg");
    export_case("export_escaping", L"reg.exe export " K L"\\escape esc.reg", L"esc.reg");
    export_case("export_nested", L"reg.exe export " K L"\\nest nest.reg", L"nest.reg");
    export_case("export_empty_key", L"reg.exe export " K L"\\empty empty.reg", L"empty.reg");
    export_case("export_overwrite_y", L"reg.exe export " K L"\\nest\\a_first empty.reg /y", L"empty.reg");
    export_case("export_missing_key", L"reg.exe export " K L"\\nosuch missing.reg", L"missing.reg");
    export_case("export_bad_root", L"reg.exe export HKXX\\x bad.reg", L"bad.reg");
    export_case("export_no_file_arg", L"reg.exe export " K L"\\empty", L"none.reg");
    export_case("export_extra_arg", L"reg.exe export " K L"\\empty x.reg extra", L"x.reg");
    export_case("export_y_twice", L"reg.exe export " K L"\\empty y.reg /y /y", L"y.reg");
    export_case("export_bad_dir", L"reg.exe export " K L"\\empty nodir\\x.reg", L"nodir\\x.reg");
    export_case("export_other_extension", L"reg.exe export " K L"\\empty out.txt", L"out.txt");

    RegCloseKey(e);
    RegCloseKey(n3);
    RegCloseKey(n2);
    RegCloseKey(n1);
    RegCloseKey(n);
    RegCloseKey(esc);
    RegCloseKey(t);
    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
