/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: the root predefined keys HKEY_LOCAL_MACHINE, HKEY_USERS,
 * HKEY_CURRENT_CONFIG (and HKCU/HKCR for comparison): opening them with
 * NULL/empty subkeys, creating a volatile key directly at the root (and
 * removing it again), well-known subkeys (booleans only), and the alias
 * HKCC = HKLM\SYSTEM\CurrentControlSet\Hardware Profiles\Current.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/sysinfo/predefined-keys
 *       https://learn.microsoft.com/en-us/windows/win32/sysinfo/hkey-current-config
 *       https://learn.microsoft.com/en-us/windows/win32/sysinfo/registry-hives
 */
#include "oracle.h"

static void root_case(const char *name, HKEY pre)
{
    HKEY k = NULL, c = NULL;
    oracle_case(name);
    LONG st = RegOpenKeyExW(pre, NULL, 0, KEY_READ, &k);
    o_status("open_null", st);
    o_bool("open_null_same_handle", k == pre);
    if (!st && k != pre) RegCloseKey(k);
    k = NULL;
    st = RegOpenKeyExW(pre, L"", 0, KEY_READ, &k);
    o_status("open_empty", st);
    o_bool("open_empty_same_handle", k == pre);
    if (!st && k != pre) RegCloseKey(k);
    DWORD nsub = 0, nval = 0;
    st = RegQueryInfoKeyW(pre, NULL, NULL, NULL, &nsub, NULL, NULL, &nval, NULL, NULL, NULL, NULL);
    o_status("query_info", st);
    o_bool("has_subkeys", nsub > 0);
    DWORD disp = 0;
    st = RegCreateKeyExW(pre, L"SetuOracle_RegPredef_roots_tmp", 0, NULL, REG_OPTION_VOLATILE, KEY_ALL_ACCESS, NULL, &c,
                         &disp);
    o_status("create_volatile_at_root", st);
    if (!st) {
        o_u32("disposition", disp);
        RegCloseKey(c);
        o_status("delete_at_root", RegDeleteKeyW(pre, L"SetuOracle_RegPredef_roots_tmp"));
    }
    st = RegCreateKeyExW(pre, L"SetuOracle_RegPredef_roots_nv", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &c, &disp);
    o_status("create_nonvolatile_at_root", st);
    if (!st) {
        RegCloseKey(c);
        o_status("delete_nonvolatile_at_root", RegDeleteKeyW(pre, L"SetuOracle_RegPredef_roots_nv"));
    }
    DWORD v = 1;
    st = RegSetValueExW(pre, L"SetuOracle_RegPredef_roots_val", 0, REG_DWORD, (const BYTE *)&v, sizeof v);
    o_status("set_value_at_root", st);
    if (!st) o_status("delete_value_at_root", RegDeleteValueW(pre, L"SetuOracle_RegPredef_roots_val"));
    oracle_case_end();
}

static void exists_case(const char *name, HKEY pre, const wchar_t *sub)
{
    HKEY k;
    LONG st = RegOpenKeyExW(pre, sub, 0, KEY_READ, &k);
    oracle_case(name);
    o_status("open", st);
    oracle_case_end();
    if (!st) RegCloseKey(k);
}

int main(void)
{
    oracle_begin("advapi32/RegPredef_roots");

    root_case("hklm", HKEY_LOCAL_MACHINE);
    root_case("hku", HKEY_USERS);
    root_case("hkcc", HKEY_CURRENT_CONFIG);
    root_case("hkcu", HKEY_CURRENT_USER);

    exists_case("hklm_software", HKEY_LOCAL_MACHINE, L"SOFTWARE");
    exists_case("hklm_system", HKEY_LOCAL_MACHINE, L"SYSTEM");
    exists_case("hklm_system_ccs", HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet");
    exists_case("hklm_system_ccs_control", HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control");
    exists_case("hklm_system_ccs_services", HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services");
    exists_case("hklm_hardware", HKEY_LOCAL_MACHINE, L"HARDWARE");
    exists_case("hklm_software_microsoft_windows_cv", HKEY_LOCAL_MACHINE,
                L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion");
    exists_case("hklm_software_microsoft_windows_nt_cv", HKEY_LOCAL_MACHINE,
                L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion");
    exists_case("hklm_software_classes", HKEY_LOCAL_MACHINE, L"SOFTWARE\\Classes");
    exists_case("hklm_software_wow6432node", HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node");
    exists_case("hklm_hw_profiles_current", HKEY_LOCAL_MACHINE,
                L"SYSTEM\\CurrentControlSet\\Hardware Profiles\\Current");
    exists_case("hku_default", HKEY_USERS, L".DEFAULT");
    exists_case("hku_localsystem", HKEY_USERS, L"S-1-5-18");
    exists_case("hkcu_software", HKEY_CURRENT_USER, L"Software");
    exists_case("hkcu_software_microsoft", HKEY_CURRENT_USER, L"Software\\Microsoft");
    exists_case("hkcu_environment", HKEY_CURRENT_USER, L"Environment");
    exists_case("hkcu_control_panel", HKEY_CURRENT_USER, L"Control Panel");
    exists_case("hkcc_software", HKEY_CURRENT_CONFIG, L"Software");
    exists_case("hkcc_system", HKEY_CURRENT_CONFIG, L"System");

    {   /* HKCC is an alias of Hardware Profiles\Current. */
        HKEY k;
        DWORD v = 42, got = 0, sz = sizeof got;
        LONG st = RegCreateKeyExW(HKEY_CURRENT_CONFIG, L"Software\\SetuOracle_RegPredef_roots", 0, NULL,
                                  REG_OPTION_VOLATILE, KEY_ALL_ACCESS, NULL, &k, NULL);
        if (!st) {
            RegSetValueExW(k, L"m", 0, REG_DWORD, (const BYTE *)&v, sizeof v);
            RegCloseKey(k);
        }
        LONG r = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                               L"SYSTEM\\CurrentControlSet\\Hardware Profiles\\Current\\Software\\SetuOracle_RegPredef_roots",
                               0, KEY_READ, &k);
        if (!r) {
            r = RegQueryValueExW(k, L"m", NULL, NULL, (BYTE *)&got, &sz);
            RegCloseKey(k);
        }
        oracle_case("hkcc_alias_of_hw_profile_current");
        o_status("create", st);
        o_status("read_via_hklm", r);
        o_bool("same_value", got == v);
        oracle_case_end();
        RegDeleteKeyW(HKEY_CURRENT_CONFIG, L"Software\\SetuOracle_RegPredef_roots");
    }

    return oracle_end();
}
