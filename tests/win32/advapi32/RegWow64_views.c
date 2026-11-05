/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: the 32-bit registry view from a 64-bit process. Keys created
 * with KEY_WOW64_32KEY under HKLM\SOFTWARE land under
 * HKLM\SOFTWARE\WOW6432Node; HKLM\SYSTEM and HKCU\Software are shared;
 * HKLM\SOFTWARE\Classes\CLSID is redirected while other Classes subkeys are
 * shared. Every probe uses test-owned key names only.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/winprog64/registry-redirector
 *       https://learn.microsoft.com/en-us/windows/win32/winprog64/shared-registry-keys
 *       https://learn.microsoft.com/en-us/windows/win32/winprog64/accessing-an-alternate-registry-view
 */
#include "oracle.h"

#define T L"SetuOracle_RegWow64_views"
#define CLSID_T L"{5e70a11e-0000-4000-8000-5e7500000001}"

static LONG open_st(HKEY base, const wchar_t *path, REGSAM view)
{
    HKEY k;
    LONG st = RegOpenKeyExW(base, path, 0, KEY_READ | view, &k);
    if (!st) RegCloseKey(k);
    return st;
}

static LONG create32(HKEY base, const wchar_t *path, DWORD tag)
{
    HKEY k;
    LONG st = RegCreateKeyExW(base, path, 0, NULL, 0, KEY_ALL_ACCESS | KEY_WOW64_32KEY, NULL, &k, NULL);
    if (!st) {
        RegSetValueExW(k, L"tag", 0, REG_DWORD, (const BYTE *)&tag, sizeof tag);
        RegCloseKey(k);
    }
    return st;
}

static void del_both(HKEY base, const wchar_t *path)
{
    RegDeleteTreeW(base, path);
    RegDeleteKeyExW(base, path, KEY_WOW64_32KEY, 0);
    RegDeleteKeyExW(base, path, KEY_WOW64_64KEY, 0);
}

static void where_case(const char *name, HKEY base, const wchar_t *create_path, const wchar_t *plain64,
                       const wchar_t *node_path)
{
    LONG c = create32(base, create_path, 32);
    oracle_case(name);
    o_status("create_32", c);
    o_status("open_32view", open_st(base, create_path, KEY_WOW64_32KEY));
    o_status("open_64view", open_st(base, plain64, KEY_WOW64_64KEY));
    o_status("open_default_view", open_st(base, plain64, 0));
    o_status("open_wow6432node_path_64view", open_st(base, node_path, KEY_WOW64_64KEY));
    oracle_case_end();
}

int main(void)
{
    oracle_begin("advapi32/RegWow64_views");
    del_both(HKEY_LOCAL_MACHINE, L"SOFTWARE\\" T);
    del_both(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\" T);
    del_both(HKEY_LOCAL_MACHINE, L"SYSTEM\\" T);
    del_both(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Classes\\CLSID\\" CLSID_T);
    del_both(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Classes\\WOW6432Node\\CLSID\\" CLSID_T);
    del_both(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Classes\\" T);
    HKEY root = oracle_reg_root();

    where_case("hklm_software", HKEY_LOCAL_MACHINE, L"SOFTWARE\\" T, L"SOFTWARE\\" T, L"SOFTWARE\\WOW6432Node\\" T);
    where_case("hklm_software_lowercase", HKEY_LOCAL_MACHINE, L"software\\" T L"_lc", L"software\\" T L"_lc",
               L"SOFTWARE\\Wow6432Node\\" T L"_lc");
    where_case("hklm_system_shared", HKEY_LOCAL_MACHINE, L"SYSTEM\\" T, L"SYSTEM\\" T, L"SYSTEM\\WOW6432Node\\" T);
    where_case("hklm_classes_clsid", HKEY_LOCAL_MACHINE, L"SOFTWARE\\Classes\\CLSID\\" CLSID_T,
               L"SOFTWARE\\Classes\\CLSID\\" CLSID_T, L"SOFTWARE\\Classes\\WOW6432Node\\CLSID\\" CLSID_T);
    where_case("hklm_classes_other", HKEY_LOCAL_MACHINE, L"SOFTWARE\\Classes\\" T, L"SOFTWARE\\Classes\\" T,
               L"SOFTWARE\\Classes\\WOW6432Node\\" T);
    where_case("hkcu_software_shared", HKEY_CURRENT_USER, L"Software\\SetuOracle\\advapi32_RegWow64_views\\k",
               L"Software\\SetuOracle\\advapi32_RegWow64_views\\k",
               L"Software\\WOW6432Node\\SetuOracle\\advapi32_RegWow64_views\\k");

    {   /* Same name in both views holds separate values. */
        HKEY k;
        DWORD v = 64, got32 = 0, got64 = 0, sz = 4;
        RegCreateKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\" T, 0, NULL, 0, KEY_ALL_ACCESS | KEY_WOW64_64KEY, NULL, &k, NULL);
        RegSetValueExW(k, L"tag", 0, REG_DWORD, (const BYTE *)&v, sizeof v);
        RegCloseKey(k);
        RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\" T, 0, KEY_READ | KEY_WOW64_32KEY, &k);
        RegQueryValueExW(k, L"tag", NULL, NULL, (BYTE *)&got32, &sz);
        RegCloseKey(k);
        sz = 4;
        RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\" T, 0, KEY_READ | KEY_WOW64_64KEY, &k);
        RegQueryValueExW(k, L"tag", NULL, NULL, (BYTE *)&got64, &sz);
        RegCloseKey(k);
        oracle_case("separate_values_per_view");
        o_u32("tag_32", got32);
        o_u32("tag_64", got64);
        oracle_case_end();
    }
    {   /* Relative opens from a handle opened in the 32-bit view stay in it. */
        HKEY sw32 = NULL, k;
        LONG st = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE", 0, KEY_READ | KEY_WOW64_32KEY, &sw32);
        DWORD got = 0, sz = 4;
        LONG r = RegOpenKeyExW(sw32, T, 0, KEY_READ, &k);
        if (!r) {
            RegQueryValueExW(k, L"tag", NULL, NULL, (BYTE *)&got, &sz);
            RegCloseKey(k);
        }
        LONG r64 = RegOpenKeyExW(sw32, T, 0, KEY_READ | KEY_WOW64_64KEY, &k);
        DWORD got64 = 0;
        sz = 4;
        if (!r64) {
            RegQueryValueExW(k, L"tag", NULL, NULL, (BYTE *)&got64, &sz);
            RegCloseKey(k);
        }
        oracle_case("relative_from_32view_handle");
        o_status("open_software_32", st);
        o_status("open_child", r);
        o_u32("child_tag", got);
        o_status("open_child_64flag", r64);
        o_u32("child_64flag_tag", got64);
        oracle_case_end();
        {   /* Does the 32-bit SOFTWARE handle list WOW6432Node? */
            int found = 0;
            for (DWORD i = 0; i < 100000; i++) {
                wchar_t nm[300];
                DWORD n = 300;
                if (RegEnumKeyExW(sw32, i, nm, &n, NULL, NULL, NULL, NULL)) break;
                if (!_wcsicmp(nm, L"WOW6432Node")) found = 1;
            }
            oracle_case("software_32view_lists_wow6432node");
            o_bool("found", found);
            oracle_case_end();
        }
        RegCloseKey(sw32);
    }
    {   /* Opening WOW6432Node explicitly with the 32-bit flag (no double redirection). */
        oracle_case("explicit_wow6432node_with_32flag");
        o_status("open", open_st(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\" T, KEY_WOW64_32KEY));
        o_status("open_double", open_st(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\WOW6432Node\\" T, KEY_WOW64_32KEY));
        oracle_case_end();
    }

    del_both(HKEY_LOCAL_MACHINE, L"SOFTWARE\\" T);
    del_both(HKEY_LOCAL_MACHINE, L"software\\" T L"_lc");
    del_both(HKEY_LOCAL_MACHINE, L"SYSTEM\\" T);
    del_both(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Classes\\CLSID\\" CLSID_T);
    del_both(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Classes\\" T);
    {
        oracle_case("cleanup_done");
        o_status("software_32", open_st(HKEY_LOCAL_MACHINE, L"SOFTWARE\\" T, KEY_WOW64_32KEY));
        o_status("software_64", open_st(HKEY_LOCAL_MACHINE, L"SOFTWARE\\" T, KEY_WOW64_64KEY));
        o_status("clsid_32", open_st(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Classes\\CLSID\\" CLSID_T, KEY_WOW64_32KEY));
        oracle_case_end();
    }
    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
