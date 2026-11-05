/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: "regedit /s file.reg" imports, headless: version 5.00 UTF-16LE
 * and REGEDIT4 ANSI files, deletion syntax ([-key], "v"=-), hex(n): types,
 * line continuations, comments, several files in one command and bad files.
 * Every run uses /s, so regedit never opens a window or a message box.
 *
 * Docs: https://learn.microsoft.com/en-us/troubleshoot/windows-server/performance/add-modify-delete-registry-subkeys-values
 *       https://learn.microsoft.com/en-us/windows/win32/sysinfo/registry
 */
#include "../reg/regtool.h"

#define KB "HKEY_CURRENT_USER\\Software\\SetuOracle\\regedit_import"
#define WKB L"HKEY_CURRENT_USER\\Software\\SetuOracle\\regedit_import"
#define SUB L"Software\\SetuOracle\\regedit_import"
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
    tree_case(name, L"regedit.exe /s in.reg");
}

static void imp4(const char *name, const char *body)
{
    static char text[8192];
    snprintf(text, sizeof text, "REGEDIT4\r\n\r\n%s", body);
    rt_write_ansi(L"in.reg", text);
    tree_case(name, L"regedit.exe /s in.reg");
}

int main(void)
{
    oracle_begin("regedit/import");
    HKEY root = oracle_reg_root();

    imp5("v5_types",
         L"[" WKB L"\\t]\r\n"
         L"@=\"default\"\r\n"
         L"\"sz\"=\"a \\\"q\\\" \\\\ b\"\r\n"
         L"\"dw\"=dword:00001234\r\n"
         L"\"bin\"=hex:00,ff,10\r\n"
         L"\"none\"=hex(0):\r\n"
         L"\"expand\"=hex(2):41,00,00,00\r\n"
         L"\"multi\"=hex(7):61,00,00,00,62,00,00,00,00,00\r\n"
         L"\"qword\"=hex(b):01,00,00,00,00,00,00,00\r\n"
         L"\"custom\"=hex(100):01\r\n"
         L"\"uni\"=\"\x4e2d\x6587\"\r\n\r\n");
    imp5("v5_continuation_and_comments",
         L"; comment line\r\n"
         L"[" WKB L"\\c]\r\n"
         L"\"long\"=hex:01,02,03,\\\r\n"
         L"  04,05,\\\r\n"
         L"  ; not a comment inside a continuation?\r\n"
         L"  06\r\n"
         L"\"next\"=\"n\"\r\n"
         L"\r\n"
         L"[" WKB L"\\c\\sub]\r\n"
         L"\"x\"=dword:1\r\n\r\n");
    imp5("v5_delete_value", L"[" WKB L"\\t]\r\n\"sz\"=-\r\n@=-\r\n\"nosuch\"=-\r\n\r\n");
    imp5("v5_delete_key", L"[-" WKB L"\\c]\r\n\r\n");
    imp5("v5_delete_missing_key", L"[-" WKB L"\\never\\there]\r\n\r\n");
    imp5("v5_key_name_spaces",
         L"[" WKB L"\\with space]\r\n\"a\"=\"1\"\r\n"
         L"[ " WKB L"\\leading_space]\r\n\"b\"=\"2\"\r\n\r\n");
    imp5("v5_malformed",
         L"[" WKB L"\\m]\r\n"
         L"\"ok\"=\"1\"\r\n"
         L"\"bad_dword\"=dword:1234567890\r\n"
         L"\"bad_hex\"=hex:1g\r\n"
         L"garbage line\r\n"
         L"\"ok2\"=\"2\"\r\n\r\n");
    imp4("regedit4_types",
         "[" KB "\\r4]\r\n"
         "\"sz\"=\"ansi text\"\r\n"
         "\"dw\"=dword:000000ff\r\n"
         "\"expand\"=hex(2):41,42,00\r\n"
         "\"multi\"=hex(7):61,00,62,00,00\r\n"
         "@=\"d\"\r\n\r\n");
    imp4("regedit4_delete", "[" KB "\\r4]\r\n\"sz\"=-\r\n\r\n[-" KB "\\t]\r\n\r\n");

    {
        rt_write_ansi(L"one.reg", "REGEDIT4\r\n\r\n[" KB "\\multi1]\r\n\"a\"=\"1\"\r\n");
        rt_write_ansi(L"two.reg", "REGEDIT4\r\n\r\n[" KB "\\multi2]\r\n\"b\"=\"2\"\r\n");
        tree_case("two_files", L"regedit.exe /s one.reg two.reg");
        rt_write_ansi(L"in.reg", "Junk header\r\n\r\n[" KB "\\junk]\r\n\"a\"=\"1\"\r\n");
        tree_case("bad_header", L"regedit.exe /s in.reg");
        tree_case("missing_file", L"regedit.exe /s nosuch.reg");
        rt_write_ansi(L"in.reg", "REGEDIT4\r\n\r\n[" KB "\\dash]\r\n\"a\"=\"1\"\r\n");
        tree_case("dash_s_switch", L"regedit.exe -s in.reg");
        rt_write_ansi(L"in.reg", "REGEDIT4\r\n\r\n[" KB "\\upper]\r\n\"a\"=\"1\"\r\n");
        tree_case("upper_s_switch", L"regedit.exe /S in.reg");
    }

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
