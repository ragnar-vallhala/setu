/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: RegDisablePredefinedCache and RegDisablePredefinedCacheEx:
 * return values, calling them twice, and that HKEY_CURRENT_USER and the
 * other predefined keys keep working afterwards.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regdisablepredefinedcache
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regdisablepredefinedcacheex
 */
#include "oracle.h"

static void works_case(const char *name)
{
    HKEY k;
    oracle_case(name);
    LONG st = RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\SetuOracle\\advapi32_RegDisablePredefinedCache", 0, KEY_READ, &k);
    o_status("hkcu_open", st);
    if (!st) RegCloseKey(k);
    st = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE", 0, KEY_READ, &k);
    o_status("hklm_open", st);
    if (!st) RegCloseKey(k);
    st = RegOpenKeyExW(HKEY_CLASSES_ROOT, L"CLSID", 0, KEY_READ, &k);
    o_status("hkcr_open", st);
    if (!st) RegCloseKey(k);
    DWORD n = 0;
    o_status("hkcu_query_info", RegQueryInfoKeyW(HKEY_CURRENT_USER, NULL, NULL, NULL, &n, NULL, NULL, NULL, NULL, NULL,
                                                 NULL, NULL));
    oracle_case_end();
}

int main(void)
{
    oracle_begin("advapi32/RegDisablePredefinedCache");
    HKEY root = oracle_reg_root();
    works_case("before");
    LONG st;
    ORACLE_CALL(st, RegDisablePredefinedCache());
    oracle_case("disable");
    o_status("ret", st);
    o_lasterr();
    oracle_case_end();
    works_case("after_disable");
    ORACLE_CALL(st, RegDisablePredefinedCache());
    oracle_case("disable_again");
    o_status("ret", st);
    o_lasterr();
    oracle_case_end();
    ORACLE_CALL(st, RegDisablePredefinedCacheEx());
    oracle_case("disable_ex");
    o_status("ret", st);
    o_lasterr();
    oracle_case_end();
    ORACLE_CALL(st, RegDisablePredefinedCacheEx());
    oracle_case("disable_ex_again");
    o_status("ret", st);
    o_lasterr();
    oracle_case_end();
    works_case("after_disable_ex");
    {   /* Closing HKCU after the cache is disabled. */
        LONG c = RegCloseKey(HKEY_CURRENT_USER);
        oracle_case("close_hkcu_after_disable");
        o_status("close", c);
        oracle_case_end();
        works_case("after_close_hkcu");
    }
    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
