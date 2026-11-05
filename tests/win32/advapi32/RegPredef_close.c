/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: RegCloseKey on predefined key handles (HKCU, HKLM, HKCR,
 * HKU, HKCC, HKEY_PERFORMANCE_DATA) and whether they keep working
 * afterwards, closing twice, and RegOpenKeyExW with an empty subkey on a
 * predefined key (a new handle that can be closed independently).
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regclosekey
 *       https://learn.microsoft.com/en-us/windows/win32/sysinfo/predefined-keys
 */
#include "oracle.h"

static void close_case(const char *name, HKEY pre)
{
    LONG st, st2;
    ORACLE_CALL(st, RegCloseKey(pre));
    DWORD l1 = oracle_last_error;
    ORACLE_CALL(st2, RegCloseKey(pre));
    oracle_case(name);
    o_status("close", st);
    o_str("close_last_error", l1 == ORACLE_SENTINEL ? "unchanged" : "changed");
    o_status("close_again", st2);
    DWORD nsub = 0;
    LONG q = RegQueryInfoKeyW(pre, NULL, NULL, NULL, &nsub, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
    o_status("query_info_after", q);
    HKEY k = NULL;
    LONG o = RegOpenKeyExW(pre, L"Software", 0, KEY_READ, &k);
    o_status("open_software_after", o);
    if (!o) RegCloseKey(k);
    oracle_case_end();
}

int main(void)
{
    oracle_begin("advapi32/RegPredef_close");

    close_case("hkcu", HKEY_CURRENT_USER);
    close_case("hklm", HKEY_LOCAL_MACHINE);
    close_case("hkcr", HKEY_CLASSES_ROOT);
    close_case("hku", HKEY_USERS);
    close_case("hkcc", HKEY_CURRENT_CONFIG);
    {
        LONG st;
        ORACLE_CALL(st, RegCloseKey(HKEY_PERFORMANCE_DATA));
        oracle_case("hkpd");
        o_status("close", st);
        o_lasterr();
        oracle_case_end();
    }
    {
        HKEY k = NULL;
        LONG st = RegOpenKeyExW(HKEY_CURRENT_USER, L"", 0, KEY_READ, &k);
        int same = k == HKEY_CURRENT_USER;
        LONG c = RegCloseKey(k);
        DWORD nsub = 0;
        LONG q = RegQueryInfoKeyW(HKEY_CURRENT_USER, NULL, NULL, NULL, &nsub, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        oracle_case("open_empty_then_close");
        o_status("open", st);
        o_bool("same_handle", same);
        o_status("close", c);
        o_status("hkcu_query_after", q);
        oracle_case_end();
    }
    {
        HKEY k = NULL;
        LONG st = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE", 0, KEY_READ, &k);
        LONG c1 = RegCloseKey(k);
        LONG c2 = RegCloseKey(k);
        oracle_case("normal_key_close_twice");
        o_status("open", st);
        o_status("close1", c1);
        o_status("close2", c2);
        oracle_case_end();
    }

    return oracle_end();
}
