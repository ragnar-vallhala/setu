/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: reg.exe command-line syntax errors and exit codes: no
 * arguments, help switches (exit code only: help text is not recorded),
 * unknown operations, bad root keys, remote names, duplicated or conflicting
 * switches, and missing keys.
 *
 * Docs: https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/reg
 *       https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/reg-query
 */
#include "regtool.h"

#define K L"HKCU\\Software\\SetuOracle\\reg_syntax"

int main(void)
{
    oracle_begin("reg/syntax");
    HKEY root = oracle_reg_root();
    DWORD v = 1;
    RegSetValueExW(root, L"v", 0, REG_DWORD, (const BYTE *)&v, 4);

    rt_case("no_args", L"reg.exe");
    rt_case_exit("help", L"reg.exe /?");
    rt_case_exit("help_dash_h", L"reg.exe -h");
    rt_case_exit("query_help", L"reg.exe query /?");
    rt_case_exit("add_help", L"reg.exe add /?");
    rt_case_exit("delete_help", L"reg.exe delete /?");
    rt_case_exit("copy_help", L"reg.exe copy /?");
    rt_case_exit("export_help", L"reg.exe export /?");
    rt_case_exit("import_help", L"reg.exe import /?");
    rt_case_exit("compare_help", L"reg.exe compare /?");
    rt_case_exit("save_help", L"reg.exe save /?");
    rt_case_exit("restore_help", L"reg.exe restore /?");
    rt_case("help_with_extra_arg", L"reg.exe query /? extra");
    rt_case("unknown_operation", L"reg.exe frobnicate");
    rt_case("operation_upper_case", L"reg.exe QUERY " K L" /v v");
    rt_case("query_no_key", L"reg.exe query");
    rt_case("query_missing_key", L"reg.exe query " K L"\\nosuch");
    rt_case("query_bad_root", L"reg.exe query HKXX\\Software");
    rt_case("query_root_prefix_only", L"reg.exe query HKCUX\\Software");
    rt_case("query_root_trailing_backslash", L"reg.exe query HKCU\\");
    rt_case("query_double_trailing_backslash", L"reg.exe query " K L"\\\\");
    rt_case("query_v_no_name", L"reg.exe query " K L" /v");
    rt_case("query_v_twice", L"reg.exe query " K L" /v a /v b");
    rt_case("query_v_and_ve", L"reg.exe query " K L" /v v /ve");
    rt_case("query_unknown_switch", L"reg.exe query " K L" /q");
    rt_case("query_reg32_twice", L"reg.exe query " K L" /reg:32 /reg:32");
    rt_case("query_reg32_reg64", L"reg.exe query " K L" /reg:32 /reg:64");
    rt_case("query_reg64", L"reg.exe query " K L" /v v /reg:64");
    rt_case("add_no_key", L"reg.exe add");
    rt_case("delete_no_key", L"reg.exe delete");

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
