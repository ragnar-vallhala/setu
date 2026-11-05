/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: reg.exe with non-ASCII key names, value names and data:
 * "reg add" from the command line, "reg query" output written to a pipe
 * (characters the console code page lacks), "/f" searches with non-ASCII
 * patterns and case folding, and export/import of the same key. The output
 * bytes are printed decoded through the OEM code page by the harness helpers.
 *
 * Docs: https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/reg-add
 *       https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/reg-query
 */
#include "regtool.h"

#define K L"HKCU\\Software\\SetuOracle\\reg_unicode"
#define SUB L"Software\\SetuOracle\\reg_unicode"

int main(void)
{
    oracle_begin("reg/unicode");
    HKEY root = oracle_reg_root(), k;

    /* Set directly, so the data does not depend on command-line conversion. */
    RegCreateKeyExW(root, L"caf\x00e9", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL);
    RegSetValueExW(k, L"\x00c9t\x00e9", 0, REG_SZ, (const BYTE *)L"\x00fc" L"ber", 10);
    RegSetValueExW(k, L"cjk", 0, REG_SZ, (const BYTE *)L"\x4e2d\x6587", 6);
    RegSetValueExW(k, L"greek", 0, REG_SZ, (const BYTE *)L"\x03a9\x03c9", 6);
    RegSetValueExW(k, L"emoji", 0, REG_SZ, (const BYTE *)L"\xd83d\xde00", 6);
    RegCloseKey(k);

    rt_case("query_latin1", L"reg.exe query " K L"\\caf\x00e9 /v \x00c9t\x00e9");
    rt_case("query_cjk", L"reg.exe query " K L"\\caf\x00e9 /v cjk");
    rt_case("query_greek", L"reg.exe query " K L"\\caf\x00e9 /v greek");
    rt_case("query_emoji", L"reg.exe query " K L"\\caf\x00e9 /v emoji");
    rt_case("query_key_folded_case", L"reg.exe query " K L"\\CAF\x00c9 /v cjk");
    rt_case("find_data_folded", L"reg.exe query " K L" /s /f \x00dc" L"BER /d");
    rt_case("find_data_case_sensitive", L"reg.exe query " K L" /s /f \x00dc" L"BER /d /c");
    rt_case("find_value_name", L"reg.exe query " K L" /s /f \x00e9t\x00e9 /v");

    oracle_case("add_from_command_line");
    rt_fields(L"reg.exe add " K L"\\na\x00efve /v \x00f1 /d \x00e5\x03bb /f");
    o_regtree("tree", HKEY_CURRENT_USER, SUB L"\\na\x00efve");
    oracle_case_end();
    rt_case("delete_value", L"reg.exe delete " K L"\\na\x00efve /v \x00d1 /f");

    DeleteFileW(L"u.reg");
    oracle_case("export");
    rt_fields(L"reg.exe export " K L"\\caf\x00e9 u.reg /y");
    o_text_file("file", L"u.reg");
    oracle_case_end();
    RegDeleteTreeW(root, L"caf\x00e9");
    RegDeleteKeyW(root, L"caf\x00e9");
    oracle_case("import_after_delete");
    rt_fields(L"reg.exe import u.reg");
    o_regtree("tree", HKEY_CURRENT_USER, SUB L"\\caf\x00e9");
    oracle_case_end();
    DeleteFileW(L"u.reg");

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
