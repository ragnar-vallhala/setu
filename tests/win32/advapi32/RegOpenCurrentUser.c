/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: RegOpenCurrentUser and RegOpenUserClassesRoot: the returned
 * key is the same key as HKEY_CURRENT_USER (a marker value written through
 * HKCU is visible), access masks, closing the returned handle, and errors.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regopencurrentuser
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regopenuserclassesroot
 */
#include "oracle.h"

static LONG marker_visible(HKEY base, DWORD *got)
{
    HKEY k;
    LONG st = RegOpenKeyExW(base, L"Software\\SetuOracle\\advapi32_RegOpenCurrentUser", 0, KEY_READ, &k);
    if (!st) {
        DWORD sz = sizeof *got;
        st = RegQueryValueExW(k, L"marker", NULL, NULL, (BYTE *)got, &sz);
        RegCloseKey(k);
    }
    return st;
}

static void ocu_case(const char *name, REGSAM sam)
{
    HKEY k = NULL;
    LONG st;
    ORACLE_CALL(st, RegOpenCurrentUser(sam, &k));
    oracle_case(name);
    o_status("ret", st);
    o_lasterr();
    if (!st) {
        o_bool("is_predefined_hkcu", k == HKEY_CURRENT_USER);
        DWORD got = 0;
        LONG m = marker_visible(k, &got);
        o_status("marker", m);
        o_bool("marker_value_ok", got == 4321);
        DWORD v = 1;
        o_status("set_value", RegSetValueExW(k, L"SetuOracle_RegOpenCurrentUser_probe", 0, REG_DWORD, (const BYTE *)&v,
                                             sizeof v));
        RegDeleteValueW(HKEY_CURRENT_USER, L"SetuOracle_RegOpenCurrentUser_probe");
        o_status("close", RegCloseKey(k));
    }
    oracle_case_end();
}

int main(void)
{
    oracle_begin("advapi32/RegOpenCurrentUser");
    HKEY root = oracle_reg_root();
    DWORD v = 4321;
    RegSetValueExW(root, L"marker", 0, REG_DWORD, (const BYTE *)&v, sizeof v);

    ocu_case("read", KEY_READ);
    ocu_case("all_access", KEY_ALL_ACCESS);
    ocu_case("query_value_only", KEY_QUERY_VALUE);
    ocu_case("zero_access", 0);
    ocu_case("maximum_allowed", MAXIMUM_ALLOWED);

    {   /* HKCU still works after closing the returned handle. */
        DWORD got = 0;
        oracle_case("hkcu_after_close");
        o_status("marker", marker_visible(HKEY_CURRENT_USER, &got));
        oracle_case_end();
    }
    {
        HANDLE tok = NULL;
        HKEY k = NULL;
        OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_IMPERSONATE | TOKEN_DUPLICATE, &tok);
        LONG st;
        ORACLE_CALL(st, RegOpenUserClassesRoot(tok, 0, KEY_READ, &k));
        oracle_case("user_classes_root");
        o_status("ret", st);
        o_lasterr();
        if (!st) {
            o_bool("is_predefined_hkcr", k == HKEY_CLASSES_ROOT);
            HKEY c;
            LONG w = RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\SetuOracle.advapi32_RegOpenCurrentUser", 0, NULL,
                                     REG_OPTION_VOLATILE, KEY_ALL_ACCESS, NULL, &c, NULL);
            if (!w) RegCloseKey(c);
            LONG r = RegOpenKeyExW(k, L"SetuOracle.advapi32_RegOpenCurrentUser", 0, KEY_READ, &c);
            if (!r) RegCloseKey(c);
            o_status("sees_hkcu_classes_key", r);
            RegDeleteKeyW(HKEY_CURRENT_USER, L"Software\\Classes\\SetuOracle.advapi32_RegOpenCurrentUser");
            o_status("close", RegCloseKey(k));
        }
        oracle_case_end();
        k = NULL;
        ORACLE_CALL(st, RegOpenUserClassesRoot(tok, 1, KEY_READ, &k));
        oracle_case("user_classes_root_options_1");
        o_status("ret", st);
        o_lasterr();
        oracle_case_end();
        if (!st) RegCloseKey(k);
        k = NULL;
        ORACLE_CALL(st, RegOpenUserClassesRoot(NULL, 0, KEY_READ, &k));
        oracle_case("user_classes_root_null_token");
        o_status("ret", st);
        o_lasterr();
        oracle_case_end();
        if (!st) RegCloseKey(k);
        k = NULL;
        ORACLE_CALL(st, RegOpenUserClassesRoot((HANDLE)(ULONG_PTR)0x1234, 0, KEY_READ, &k));
        oracle_case("user_classes_root_bad_token");
        o_status("ret", st);
        o_lasterr();
        oracle_case_end();
        if (!st) RegCloseKey(k);
        CloseHandle(tok);
    }

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
