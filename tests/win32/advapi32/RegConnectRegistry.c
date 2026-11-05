/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: RegConnectRegistryW/A with a NULL or empty machine name (the
 * local registry): which predefined keys are allowed, whether the returned
 * handle is a new handle or the predefined value, that it reaches the same
 * keys (a marker value under HKCU\Software\SetuOracle), closing it, and
 * invalid predefined keys. Only the local machine is used; no network.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regconnectregistryw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regconnectregistrya
 */
#include "oracle.h"

static void connect_case(const char *name, const wchar_t *machine, HKEY pre)
{
    HKEY k = NULL;
    LONG st;
    ORACLE_CALL(st, RegConnectRegistryW(machine, pre, &k));
    oracle_case(name);
    o_status("ret", st);
    o_lasterr();
    if (!st) {
        o_bool("same_as_predefined", k == pre);
        o_bool("non_null", k != NULL);
        HKEY s;
        LONG o = RegOpenKeyExW(k, L"Software", 0, KEY_READ, &s);
        if (o) o = RegOpenKeyExW(k, L"SOFTWARE", 0, KEY_READ, &s);
        o_status("open_software", o);
        if (!o) RegCloseKey(s);
        if (pre == HKEY_CURRENT_USER) {
            DWORD got = 0, sz = sizeof got;
            LONG m = RegOpenKeyExW(k, L"Software\\SetuOracle\\advapi32_RegConnectRegistry", 0, KEY_READ, &s);
            if (!m) {
                m = RegQueryValueExW(s, L"marker", NULL, NULL, (BYTE *)&got, &sz);
                RegCloseKey(s);
            }
            o_status("marker", m);
            o_bool("marker_ok", got == 77);
        }
        if (k != pre) o_status("close", RegCloseKey(k));
    }
    oracle_case_end();
}

int main(void)
{
    oracle_begin("advapi32/RegConnectRegistry");
    HKEY root = oracle_reg_root();
    DWORD v = 77;
    RegSetValueExW(root, L"marker", 0, REG_DWORD, (const BYTE *)&v, sizeof v);

    connect_case("null_hklm", NULL, HKEY_LOCAL_MACHINE);
    connect_case("null_hku", NULL, HKEY_USERS);
    connect_case("null_hkcu", NULL, HKEY_CURRENT_USER);
    connect_case("null_hkcr", NULL, HKEY_CLASSES_ROOT);
    connect_case("null_hkcc", NULL, HKEY_CURRENT_CONFIG);
    connect_case("empty_hklm", L"", HKEY_LOCAL_MACHINE);
    connect_case("empty_hkcu", L"", HKEY_CURRENT_USER);
    connect_case("null_normal_key", NULL, root);
    connect_case("null_invalid_key", NULL, (HKEY)(ULONG_PTR)0x1234);
    connect_case("null_undefined_predefined", NULL, (HKEY)(LONG_PTR)(LONG)0x80000010);
    {
        HKEY k = NULL;
        LONG st;
        ORACLE_CALL(st, RegConnectRegistryA(NULL, HKEY_LOCAL_MACHINE, &k));
        oracle_case("a_null_hklm");
        o_status("ret", st);
        o_lasterr();
        o_bool("same_as_predefined", k == HKEY_LOCAL_MACHINE);
        oracle_case_end();
        if (!st && k != HKEY_LOCAL_MACHINE) RegCloseKey(k);
        k = NULL;
        ORACLE_CALL(st, RegConnectRegistryA("", HKEY_USERS, &k));
        oracle_case("a_empty_hku");
        o_status("ret", st);
        o_lasterr();
        oracle_case_end();
        if (!st && k != HKEY_USERS) RegCloseKey(k);
    }

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
