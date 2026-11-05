/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: "reg add" for REG_BINARY, REG_NONE and unknown types, /ve,
 * keys without values, /f overwrite, value names and syntax errors, and the
 * resulting registry data.
 * Each case prints reg.exe's exit code and output plus the stored value.
 *
 * Docs: https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/reg-add
 */
#include "regtool.h"

#define K L"HKCU\\Software\\SetuOracle\\reg_add_keys"
#define SUB L"Software\\SetuOracle\\reg_add_keys"

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
    oracle_begin("reg/add_keys");
    HKEY root = oracle_reg_root();

    add_case("binary", L"reg.exe add " K L" /v b1 /t REG_BINARY /d deadBEEF00 /f", L"b1");
    add_case("binary_odd_digits", L"reg.exe add " K L" /v b2 /t REG_BINARY /d abc /f", L"b2");
    add_case("binary_bad_digit", L"reg.exe add " K L" /v b3 /t REG_BINARY /d 12zz /f", L"b3");
    add_case("binary_no_data", L"reg.exe add " K L" /v b4 /t REG_BINARY /f", L"b4");
    add_case("binary_0x_prefix", L"reg.exe add " K L" /v b5 /t REG_BINARY /d 0x1234 /f", L"b5");
    add_case("none_with_data", L"reg.exe add " K L" /v n1 /t REG_NONE /d text /f", L"n1");
    add_case("none_no_data", L"reg.exe add " K L" /v n2 /t REG_NONE /f", L"n2");
    add_case("unknown_type", L"reg.exe add " K L" /v u1 /t REG_FOO /d x /f", L"u1");
    add_case("link_type", L"reg.exe add " K L" /v u2 /t REG_LINK /d x /f", L"u2");
    add_case("default_value", L"reg.exe add " K L" /ve /d dflt /f", L"");
    add_case("default_value_dword", L"reg.exe add " K L"\\dk /ve /t REG_DWORD /d 1 /f", L"");
    add_case("v_and_ve", L"reg.exe add " K L" /v x /ve /d y /f", L"x");
    add_case("v_missing_name", L"reg.exe add " K L" /v", L"");
    add_case("d_twice", L"reg.exe add " K L" /v x2 /d a /d b /f", L"x2");
    add_case("t_twice", L"reg.exe add " K L" /v x3 /t REG_SZ /t REG_SZ /d a /f", L"x3");
    add_case("key_only", L"reg.exe add " K L"\\newkey /f", L"");
    add_case("key_nested", L"reg.exe add " K L"\\n1\\n2\\n3 /f", L"");
    add_case("key_existing_no_f", L"reg.exe add " K L"\\newkey", L"");
    add_case("new_value_without_f", L"reg.exe add " K L" /v fresh /d 1", L"fresh");
    add_case("overwrite_with_f", L"reg.exe add " K L" /v s1 /t REG_DWORD /d 7 /f", L"s1");
    add_case("value_name_with_backslash", L"reg.exe add " K L" /v a\\b /d x /f", L"a\\b");
    add_case("value_name_empty_quoted", L"reg.exe add " K L"\\ek /v \"\" /d empty_name /f", L"");
    add_case("bad_root", L"reg.exe add HKXX\\Software\\x /f", L"");
    add_case("unknown_switch", L"reg.exe add " K L" /v z /q /f", L"z");

    oracle_case("final_tree");
    o_regtree("tree", HKEY_CURRENT_USER, SUB);
    oracle_case_end();

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
