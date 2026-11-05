/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: "reg add" for string and number types: how /d is parsed for
 * REG_SZ, REG_EXPAND_SZ, REG_DWORD (both endians) and REG_QWORD, and the
 * resulting registry data.
 * Each case prints reg.exe's exit code and output plus the stored value.
 *
 * Docs: https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/reg-add
 */
#include "regtool.h"

#define K L"HKCU\\Software\\SetuOracle\\reg_add"
#define SUB L"Software\\SetuOracle\\reg_add"

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
    oracle_begin("reg/add");
    HKEY root = oracle_reg_root();

    add_case("sz_default_type", L"reg.exe add " K L" /v s1 /d hello /f", L"s1");
    add_case("sz_explicit", L"reg.exe add " K L" /v s2 /t REG_SZ /d \"two words\" /f", L"s2");
    add_case("sz_no_data", L"reg.exe add " K L" /v s3 /f", L"s3");
    add_case("sz_empty_data", L"reg.exe add " K L" /v s4 /d \"\" /f", L"s4");
    add_case("sz_type_lower_case", L"reg.exe add " K L" /v s5 /t reg_sz /d x /f", L"s5");
    add_case("expand_sz", L"reg.exe add " K L" /v e1 /t REG_EXPAND_SZ /d %SystemRoot%\\x /f", L"e1");
    add_case("dword_decimal", L"reg.exe add " K L" /v d1 /t REG_DWORD /d 123 /f", L"d1");
    add_case("dword_hex", L"reg.exe add " K L" /v d2 /t REG_DWORD /d 0xff /f", L"d2");
    add_case("dword_hex_upper", L"reg.exe add " K L" /v d3 /t REG_DWORD /d 0XAB /f", L"d3");
    add_case("dword_max", L"reg.exe add " K L" /v d4 /t REG_DWORD /d 4294967295 /f", L"d4");
    add_case("dword_overflow", L"reg.exe add " K L" /v d5 /t REG_DWORD /d 4294967296 /f", L"d5");
    add_case("dword_negative", L"reg.exe add " K L" /v d6 /t REG_DWORD /d -1 /f", L"d6");
    add_case("dword_not_number", L"reg.exe add " K L" /v d7 /t REG_DWORD /d abc /f", L"d7");
    add_case("dword_no_data", L"reg.exe add " K L" /v d8 /t REG_DWORD /f", L"d8");
    add_case("dword_octal_like", L"reg.exe add " K L" /v d9 /t REG_DWORD /d 010 /f", L"d9");
    add_case("dword_trailing_junk", L"reg.exe add " K L" /v d10 /t REG_DWORD /d 12x /f", L"d10");
    add_case("dword_little_endian", L"reg.exe add " K L" /v d11 /t REG_DWORD_LITTLE_ENDIAN /d 5 /f", L"d11");
    add_case("dword_big_endian", L"reg.exe add " K L" /v d12 /t REG_DWORD_BIG_ENDIAN /d 5 /f", L"d12");
    add_case("qword_decimal", L"reg.exe add " K L" /v q1 /t REG_QWORD /d 1234567890123 /f", L"q1");
    add_case("qword_hex", L"reg.exe add " K L" /v q2 /t REG_QWORD /d 0xffffffffffffffff /f", L"q2");
    add_case("qword_overflow", L"reg.exe add " K L" /v q3 /t REG_QWORD /d 0x10000000000000000 /f", L"q3");

    oracle_case("final_tree");
    o_regtree("tree", HKEY_CURRENT_USER, SUB);
    oracle_case_end();

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
