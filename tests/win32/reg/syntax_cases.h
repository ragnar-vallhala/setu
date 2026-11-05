/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * syntax_cases.h: .reg value-syntax edge cases, shared by reg/import_syntax
 * and regedit/import_syntax. The including file defines SC_SUB (its key under
 * HKCU, as a wide string), SC_KEY (the same key as a wide
 * HKEY_CURRENT_USER\... path) and SC_CMD (the import command for "in.reg").
 *
 * Every case writes a version 5.00 UTF-16LE file whose values go to its own
 * subkey, imports it and prints the exit code, output and that subkey's tree.
 */
#ifndef SETU_SYNTAX_CASES_H
#define SETU_SYNTAX_CASES_H

#include "regtool.h"

static void sc_case(const char *name, const wchar_t *sub, const wchar_t *lines)
{
    static wchar_t text[8192], tree[256];
    swprintf(text, 8192, L"Windows Registry Editor Version 5.00\r\n\r\n[" SC_KEY L"\\%ls]\r\n%ls\r\n", sub, lines);
    rt_write_utf16(L"in.reg", text);
    rt_run(SC_CMD);
    oracle_case(name);
    o_u32("exit", rt_exit);
    o_str("out", rt_out);
    o_str("err", rt_err);
    swprintf(tree, 256, L"%ls\\%ls", SC_SUB, sub);
    o_regtree("tree", HKEY_CURRENT_USER, tree);
    oracle_case_end();
}

static void sc_run_all(void)
{
    sc_case("hex_upper_case", L"hexu", L"\"a\"=HEX:0A,0B\r\n\"b\"=hex:0a,0B");
    sc_case("hex_spaces", L"hexsp", L"\"a\"=hex: 01, 02 ,03\r\n\"b\"=hex :01");
    sc_case("hex_trailing_comma", L"hextc", L"\"a\"=hex:01,02,");
    sc_case("hex_empty", L"hexe", L"\"a\"=hex:\r\n\"b\"=hex(2):\r\n\"c\"=hex(0):");
    sc_case("hex_single_digit", L"hex1", L"\"a\"=hex:1,2,3");
    sc_case("hex_three_digits", L"hex3", L"\"a\"=hex:123");
    sc_case("hex_type_upper", L"hext", L"\"a\"=hex(B):01,00,00,00,00,00,00,00\r\n\"b\"=hex(7FFFFFFF):01");
    sc_case("hex_type_too_big", L"hexbig", L"\"a\"=hex(100000000):01\r\n\"b\"=hex(-1):01");
    sc_case("hex_qword_short", L"qws", L"\"a\"=hex(b):01,02");
    sc_case("continuation_after_comma", L"cont", L"\"a\"=hex:01,\\\r\n  02,\\\r\n  03");
    sc_case("continuation_without_comma", L"contnc", L"\"a\"=hex:01\\\r\n  02");
    sc_case("continuation_comment_between", L"contc", L"\"a\"=hex:01,\\\r\n; comment\r\n  02");
    sc_case("continuation_at_eof", L"conteof", L"\"a\"=hex:01,\\");
    sc_case("dword_upper", L"dwu", L"\"a\"=DWORD:0000000A\r\n\"b\"=dword:0000000a");
    sc_case("dword_short_digits", L"dws", L"\"a\"=dword:a\r\n\"b\"=dword:");
    sc_case("dword_nine_digits", L"dw9", L"\"a\"=dword:000000001\r\n\"b\"=dword:ffffffff");
    sc_case("dword_spaces", L"dwsp", L"\"a\"=dword: 00000001\r\n\"b\"= dword:00000002\r\n\"c\" =dword:00000003");
    sc_case("string_spaces_around_equals", L"ssp", L"\"a\" = \"x\"\r\n\"b\"=\"y\" \r\n\"c\"=\"z\" ; tail");
    sc_case("string_unterminated", L"sun", L"\"a\"=\"open\r\n\"b\"=\"ok\"");
    sc_case("string_embedded_quote_unescaped", L"seq", L"\"a\"=\"x\"y\"");
    sc_case("name_with_equals", L"neq", L"\"a=b\"=\"v\"\r\n\"c\\\"d\"=\"w\"");
    sc_case("default_value_forms", L"def", L"@=\"one\"\r\n@ = \"two\"");
    sc_case("default_value_quoted_at", L"defq", L"\"@\"=\"literal at\"");
    sc_case("delete_default_value", L"deld", L"@=\"x\"\r\n@=-");
    sc_case("delete_missing_value", L"delm", L"\"nosuch\"=-\r\n\"a\"=\"kept\"");
    sc_case("delete_with_spaces", L"dels", L"\"a\"=\"x\"\r\n\"a\" = -");
    sc_case("comment_forms", L"com", L"; one\r\n\"a\"=\"1\"\r\n  ; indented\r\n# hash\r\n\"b\"=\"2\"");
    sc_case("value_indented", L"ind", L"  \"a\"=\"1\"\r\n\t\"b\"=\"2\"");
    sc_case("key_line_trailing_text", L"ktt", L"\"a\"=\"1\"\r\n[" SC_KEY L"\\ktt\\sub] trailing\r\n\"b\"=\"2\"");
    sc_case("delete_missing_key", L"dmk", L"\"a\"=\"1\"\r\n[-" SC_KEY L"\\nosuchkey]\r\n\"b\"=\"2\"");
}

#endif /* SETU_SYNTAX_CASES_H */
