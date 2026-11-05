/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: "reg add" escaping in /d: REG_MULTI_SZ with the default "\0"
 * separator and with /s, empty items, doubled separators, backslashes and
 * quotes passed through the command line, and /s misuse.
 *
 * Docs: https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/reg-add
 *       https://learn.microsoft.com/en-us/cpp/c-language/parsing-c-command-line-arguments
 */
#include "regtool.h"

#define K L"HKCU\\Software\\SetuOracle\\reg_add_multi"
#define SUB L"Software\\SetuOracle\\reg_add_multi"

static void add_case(const char *name, const wchar_t *cmd, const wchar_t *value)
{
    rt_run(cmd);
    oracle_case(name);
    o_u32("exit", rt_exit);
    o_str("out", rt_out);
    o_str("err", rt_err);
    o_regval("value", HKEY_CURRENT_USER, SUB, value);
    oracle_case_end();
}

int main(void)
{
    oracle_begin("reg/add_multi");
    HKEY root = oracle_reg_root();

    add_case("multi_default_sep", L"reg.exe add " K L" /v m1 /t REG_MULTI_SZ /d a\\0b\\0c /f", L"m1");
    add_case("multi_single", L"reg.exe add " K L" /v m2 /t REG_MULTI_SZ /d single /f", L"m2");
    add_case("multi_empty", L"reg.exe add " K L" /v m3 /t REG_MULTI_SZ /d \"\" /f", L"m3");
    add_case("multi_no_data", L"reg.exe add " K L" /v m4 /t REG_MULTI_SZ /f", L"m4");
    add_case("multi_trailing_sep", L"reg.exe add " K L" /v m5 /t REG_MULTI_SZ /d a\\0b\\0 /f", L"m5");
    add_case("multi_leading_sep", L"reg.exe add " K L" /v m6 /t REG_MULTI_SZ /d \\0a /f", L"m6");
    add_case("multi_double_sep", L"reg.exe add " K L" /v m7 /t REG_MULTI_SZ /d a\\0\\0b /f", L"m7");
    add_case("multi_custom_sep", L"reg.exe add " K L" /v m8 /t REG_MULTI_SZ /s # /d x#y#z /f", L"m8");
    add_case("multi_custom_sep_double", L"reg.exe add " K L" /v m9 /t REG_MULTI_SZ /s # /d x##y /f", L"m9");
    add_case("multi_custom_sep_trailing", L"reg.exe add " K L" /v m10 /t REG_MULTI_SZ /s # /d x#y# /f", L"m10");
    add_case("multi_sep_two_chars", L"reg.exe add " K L" /v m11 /t REG_MULTI_SZ /s ## /d x##y /f", L"m11");
    add_case("multi_sep_empty", L"reg.exe add " K L" /v m12 /t REG_MULTI_SZ /s \"\" /d xy /f", L"m12");
    add_case("multi_sep_missing", L"reg.exe add " K L" /v m13 /t REG_MULTI_SZ /d xy /s", L"m13");
    add_case("multi_sep_backslash0_with_s", L"reg.exe add " K L" /v m14 /t REG_MULTI_SZ /s # /d a\\0b#c /f", L"m14");
    add_case("multi_sep_twice", L"reg.exe add " K L" /v m15 /t REG_MULTI_SZ /s # /s # /d a#b /f", L"m15");
    add_case("s_with_sz", L"reg.exe add " K L" /v m16 /t REG_SZ /s # /d a#b /f", L"m16");
    add_case("multi_backslash_other", L"reg.exe add " K L" /v m17 /t REG_MULTI_SZ /d a\\nb\\\\0c /f", L"m17");
    add_case("sz_backslash0_literal", L"reg.exe add " K L" /v s1 /d a\\0b /f", L"s1");
    add_case("sz_trailing_backslash", L"reg.exe add " K L" /v s2 /d C:\\dir\\ /f", L"s2");
    add_case("sz_quoted_trailing_backslashes", L"reg.exe add " K L" /v s3 /d \"C:\\dir\\\\\" /f", L"s3");
    add_case("sz_embedded_quote", L"reg.exe add " K L" /v s4 /d \"say \\\"hi\\\"\" /f", L"s4");
    add_case("sz_percent", L"reg.exe add " K L" /v s5 /d 100%% /f", L"s5");
    add_case("sz_unicode", L"reg.exe add " K L" /v s6 /d caf\x00e9 /f", L"s6");
    add_case("expand_backslash0", L"reg.exe add " K L" /v s7 /t REG_EXPAND_SZ /d a\\0b /f", L"s7");

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
