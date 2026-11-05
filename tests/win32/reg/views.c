/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: reg.exe registry views: /reg:32 and /reg:64 for add, query,
 * delete, copy and export under HKLM\SOFTWARE\SetuOracle\reg_views, which the
 * 32-bit view redirects (WOW6432Node), and under HKCU\Software, which is shared.
 *
 * Docs: https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/reg-add
 *       https://learn.microsoft.com/en-us/windows/win32/winprog64/registry-redirector
 *       https://learn.microsoft.com/en-us/windows/win32/winprog64/shared-registry-keys
 */
#include "regtool.h"

#define LM L"HKLM\\SOFTWARE\\SetuOracle\\reg_views"
#define CU L"HKCU\\Software\\SetuOracle\\reg_views"

static void lm_cleanup(void)
{
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\SetuOracle\\reg_views");
    RegDeleteKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\SetuOracle\\reg_views", KEY_WOW64_64KEY, 0);
    HKEY k;
    if (!RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\SetuOracle", 0, KEY_ALL_ACCESS | KEY_WOW64_32KEY, &k)) {
        RegDeleteTreeW(k, L"reg_views");
        RegDeleteKeyExW(k, L"reg_views", KEY_WOW64_32KEY, 0);
        RegCloseKey(k);
    }
    RegDeleteKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\SetuOracle", KEY_WOW64_32KEY, 0);
    RegDeleteKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\SetuOracle", KEY_WOW64_64KEY, 0);
}

static void view_state(const char *name)
{
    oracle_case(name);
    o_regval("v64", HKEY_LOCAL_MACHINE, L"SOFTWARE\\SetuOracle\\reg_views", L"which");
    {
        HKEY k;
        BYTE data[64];
        DWORD size = sizeof data, type;
        LONG st = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\SetuOracle\\reg_views", 0, KEY_READ | KEY_WOW64_32KEY, &k);
        if (!st) {
            st = RegQueryValueExW(k, L"which", NULL, &type, data, &size);
            RegCloseKey(k);
        }
        o_status("v32.ret", st);
        if (!st) o_bytes("v32.data", data, size);
    }
    oracle_case_end();
}

int main(void)
{
    oracle_begin("reg/views");
    HKEY root = oracle_reg_root();
    lm_cleanup();

    rt_case("add_32", L"reg.exe add " LM L" /v which /d thirtytwo /reg:32 /f");
    view_state("after_add_32");
    rt_case("add_64", L"reg.exe add " LM L" /v which /d sixtyfour /reg:64 /f");
    view_state("after_add_64");
    rt_case("query_32", L"reg.exe query " LM L" /v which /reg:32");
    rt_case("query_64", L"reg.exe query " LM L" /v which /reg:64");
    rt_case("query_default_view", L"reg.exe query " LM L" /v which");
    rt_case("export_32", L"reg.exe export " LM L" v32.reg /reg:32 /y");
    oracle_case("export_32_file");
    o_text_file("file", L"v32.reg");
    oracle_case_end();
    rt_case("copy_32_to_hkcu", L"reg.exe copy " LM L" " CU L"\\from32 /reg:32 /f");
    rt_case("query_copied", L"reg.exe query " CU L"\\from32");
    rt_case("delete_value_32", L"reg.exe delete " LM L" /v which /reg:32 /f");
    view_state("after_delete_32");
    rt_case("hkcu_shared_add_32", L"reg.exe add " CU L" /v shared /d x /reg:32 /f");
    rt_case("hkcu_shared_query_64", L"reg.exe query " CU L" /v shared /reg:64");
    rt_case("delete_key_32", L"reg.exe delete " LM L" /reg:32 /f");
    rt_case("delete_key_64", L"reg.exe delete " LM L" /reg:64 /f");
    view_state("after_delete_keys");

    lm_cleanup();
    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
