/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: "reg import" of .reg files: version 5.00 (UTF-16LE with BOM)
 * and REGEDIT4 (ANSI) headers, value syntax for every type (strings, dword:,
 * hex:, hex(n):), line continuations, comments, deletion syntax ([-key] and
 * "v"=-), escapes, malformed lines and error cases. Each case prints the exit
 * code, output and the resulting key tree.
 *
 * Docs: https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/reg-import
 *       https://learn.microsoft.com/en-us/troubleshoot/windows-server/performance/add-modify-delete-registry-subkeys-values
 */
#include "regtool.h"

#define KB "HKEY_CURRENT_USER\\Software\\SetuOracle\\reg_import"
#define WKB L"HKEY_CURRENT_USER\\Software\\SetuOracle\\reg_import"
#define SUB L"Software\\SetuOracle\\reg_import"
#define V5 L"Windows Registry Editor Version 5.00\r\n\r\n"

static void tree_case(const char *name, const wchar_t *cmd)
{
    rt_run(cmd);
    oracle_case(name);
    o_u32("exit", rt_exit);
    o_str("out", rt_out);
    o_str("err", rt_err);
    o_regtree("tree", HKEY_CURRENT_USER, SUB);
    oracle_case_end();
}

static void imp5(const char *name, const wchar_t *body)
{
    static wchar_t text[8192];
    swprintf(text, 8192, L"%ls%ls", V5, body);
    rt_write_utf16(L"in.reg", text);
    tree_case(name, L"reg.exe import in.reg");
}

static void imp4(const char *name, const char *body)
{
    static char text[8192];
    snprintf(text, sizeof text, "REGEDIT4\r\n\r\n%s", body);
    rt_write_ansi(L"in.reg", text);
    tree_case(name, L"reg.exe import in.reg");
}

int main(void)
{
    oracle_begin("reg/import");
    HKEY root = oracle_reg_root();

    imp5("v5_strings",
         L"[" WKB L"\\s]\r\n"
         L"\"plain\"=\"text\"\r\n"
         L"\"quote\"=\"a\\\"b\"\r\n"
         L"\"back\"=\"C:\\\\dir\"\r\n"
         L"\"empty\"=\"\"\r\n"
         L"@=\"default\"\r\n"
         L"\"uni\"=\"caf\x00e9\"\r\n\r\n");
    imp5("v5_numbers",
         L"[" WKB L"\\n]\r\n"
         L"\"dw\"=dword:0000abcd\r\n"
         L"\"dw_upper\"=dword:FFFFFFFF\r\n"
         L"\"dw_short\"=dword:12\r\n"
         L"\"dw_long\"=dword:123456789\r\n"
         L"\"dw_bad\"=dword:xyz\r\n"
         L"\"dw_space\"=dword: 1\r\n\r\n");
    imp5("v5_hex_types",
         L"[" WKB L"\\h]\r\n"
         L"\"bin\"=hex:de,ad,be,ef\r\n"
         L"\"bin_empty\"=hex:\r\n"
         L"\"none\"=hex(0):01,02\r\n"
         L"\"expand\"=hex(2):25,00,54,00,4d,00,50,00,25,00,00,00\r\n"
         L"\"multi\"=hex(7):61,00,00,00,62,00,00,00,00,00\r\n"
         L"\"qword\"=hex(b):08,07,06,05,04,03,02,01\r\n"
         L"\"custom\"=hex(1234):aa,bb\r\n"
         L"\"dwhex\"=hex(4):01,00,00,00\r\n"
         L"\"bad_hex\"=hex:zz\r\n"
         L"\"single_digit\"=hex:1,2\r\n\r\n");
    imp5("v5_continuation",
         L"[" WKB L"\\c]\r\n"
         L"\"long\"=hex:01,02,\\\r\n"
         L"  03,04,\\\r\n"
         L"  05\r\n"
         L"\"after\"=\"ok\"\r\n\r\n");
    imp5("v5_comments_and_blank",
         L"; a comment\r\n"
         L"[" WKB L"\\k]\r\n"
         L"; another\r\n"
         L"\"a\"=\"1\"\r\n"
         L"   \r\n"
         L"  \"indented\"=\"2\"\r\n"
         L"#hash\r\n\r\n");
    imp5("v5_delete_value",
         L"[" WKB L"\\s]\r\n"
         L"\"plain\"=-\r\n"
         L"@=-\r\n"
         L"\"never_existed\"=-\r\n\r\n");
    imp5("v5_delete_key",
         L"[-" WKB L"\\n]\r\n"
         L"[-" WKB L"\\never]\r\n\r\n");
    imp5("v5_malformed_lines",
         L"[" WKB L"\\m]\r\n"
         L"\"ok1\"=\"1\"\r\n"
         L"novalue\r\n"
         L"\"unterminated=\"x\"\r\n"
         L"\"bad_type\"=foo:1\r\n"
         L"\"ok2\"=\"2\"\r\n\r\n");
    imp5("v5_key_without_bracket_end",
         L"[" WKB L"\\nobrace\r\n"
         L"\"x\"=\"1\"\r\n\r\n");
    imp5("v5_values_before_any_key",
         L"\"orphan\"=\"1\"\r\n"
         L"[" WKB L"\\after]\r\n"
         L"\"y\"=\"2\"\r\n\r\n");
    imp5("v5_short_root_name",
         L"[HKCU\\Software\\SetuOracle\\reg_import\\short]\r\n"
         L"\"x\"=\"1\"\r\n\r\n");
    imp5("v5_key_trailing_backslash",
         L"[" WKB L"\\trail\\]\r\n"
         L"\"x\"=\"1\"\r\n\r\n");
    imp5("v5_unknown_escape",
         L"[" WKB L"\\e]\r\n"
         L"\"esc\"=\"a\\qb\"\r\n"
         L"\"nl\"=\"a\\nb\"\r\n\r\n");

    imp4("regedit4_strings",
         "[" KB "\\r4]\r\n"
         "\"s\"=\"ansi\"\r\n"
         "\"dw\"=dword:00000010\r\n"
         "\"bin\"=hex:01,02\r\n"
         "\"multi\"=hex(7):61,00,62,00,00\r\n"
         "@=\"r4 default\"\r\n\r\n");
    imp4("regedit4_delete",
         "[-" KB "\\r4]\r\n\r\n");

    {
        rt_write_ansi(L"in.reg", "Not a registry file\r\n[" KB "\\x]\r\n\"a\"=\"1\"\r\n");
        tree_case("bad_header", L"reg.exe import in.reg");
        rt_write_utf16(L"in.reg", L"Windows Registry Editor Version 4.00\r\n\r\n[" WKB L"\\x]\r\n\"a\"=\"1\"\r\n");
        tree_case("bad_version", L"reg.exe import in.reg");
        rt_write_ansi(L"in.reg", "Windows Registry Editor Version 5.00\r\n\r\n[" KB "\\ansi5]\r\n\"a\"=\"1\"\r\n");
        tree_case("v5_header_in_ansi_file", L"reg.exe import in.reg");
        rt_write_ansi(L"in.reg", "REGEDIT4\n[" KB "\\lf]\n\"a\"=\"1\"\n");
        tree_case("regedit4_lf_line_ends", L"reg.exe import in.reg");
        rt_write_ansi(L"in.reg", "");
        tree_case("empty_file", L"reg.exe import in.reg");
        tree_case("missing_file", L"reg.exe import nosuch.reg");
        tree_case("no_file_arg", L"reg.exe import");
        tree_case("extra_arg", L"reg.exe import in.reg extra");
    }

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
