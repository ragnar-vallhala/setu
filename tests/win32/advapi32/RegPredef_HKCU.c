/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: HKEY_CURRENT_USER is HKEY_USERS\<user SID> (the SID comes from
 * the process token and is never printed): a value written through one path
 * is seen through the other. Also HKCU\Software\Classes vs
 * HKU\<SID>_Classes, HKEY_CURRENT_USER_LOCAL_SETTINGS, and NULL/empty
 * subkeys on HKCU.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/sysinfo/predefined-keys
 *       https://learn.microsoft.com/en-us/windows/win32/sysinfo/hkey-current-user
 *       https://learn.microsoft.com/en-us/windows/win32/sysinfo/hkey-classes-root-key
 *       https://learn.microsoft.com/en-us/windows/win32/api/sddl/nf-sddl-convertsidtostringsidw
 */
#include "oracle.h"
#include <sddl.h>

#ifndef HKEY_CURRENT_USER_LOCAL_SETTINGS
#define HKEY_CURRENT_USER_LOCAL_SETTINGS ((HKEY)(ULONG_PTR)((LONG)0x80000007))
#endif

static wchar_t sid[256];

static void get_sid(void)
{
    HANDLE tok;
    BYTE buf[256];
    DWORD len = 0;
    wchar_t *s = NULL;
    OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok);
    GetTokenInformation(tok, TokenUser, buf, sizeof buf, &len);
    CloseHandle(tok);
    ConvertSidToStringSidW(((TOKEN_USER *)buf)->User.Sid, &s);
    wcscpy(sid, s ? s : L"");
    LocalFree(s);
}

static LONG read_dword(HKEY base, const wchar_t *path, const wchar_t *name, DWORD *out)
{
    HKEY k;
    LONG st = RegOpenKeyExW(base, path, 0, KEY_READ, &k);
    if (!st) {
        DWORD sz = sizeof *out;
        st = RegQueryValueExW(k, name, NULL, NULL, (BYTE *)out, &sz);
        RegCloseKey(k);
    }
    return st;
}

int main(void)
{
    oracle_begin("advapi32/RegPredef_HKCU");
    HKEY root = oracle_reg_root();
    get_sid();
    wchar_t p[512];
    DWORD v = 1234, got = 0;
    RegSetValueExW(root, L"marker", 0, REG_DWORD, (const BYTE *)&v, sizeof v);

    swprintf(p, 512, L"%ls\\Software\\SetuOracle\\advapi32_RegPredef_HKCU", sid);
    LONG st = read_dword(HKEY_USERS, p, L"marker", &got);
    oracle_case("hkcu_value_visible_in_hku_sid");
    o_bool("have_sid", sid[0] == L'S');
    o_status("ret", st);
    o_bool("same_value", got == v);
    oracle_case_end();

    {   /* Write via HKU\<SID>, read via HKCU. */
        HKEY k;
        DWORD w = 99;
        st = RegOpenKeyExW(HKEY_USERS, p, 0, KEY_SET_VALUE, &k);
        if (!st) {
            st = RegSetValueExW(k, L"from_hku", 0, REG_DWORD, (const BYTE *)&w, sizeof w);
            RegCloseKey(k);
        }
        got = 0;
        LONG r = RegQueryValueExW(root, L"from_hku", NULL, NULL, (BYTE *)&got, &(DWORD){4});
        oracle_case("hku_sid_write_visible_in_hkcu");
        o_status("write", st);
        o_status("read", r);
        o_bool("same_value", got == w);
        oracle_case_end();
    }
    {   /* HKCU\Software\Classes is HKU\<SID>_Classes. */
        HKEY k;
        DWORD w = 7;
        st = RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\SetuOracle.advapi32_RegPredef_HKCU", 0, NULL,
                             REG_OPTION_VOLATILE, KEY_ALL_ACCESS, NULL, &k, NULL);
        if (!st) {
            RegSetValueExW(k, L"m", 0, REG_DWORD, (const BYTE *)&w, sizeof w);
            RegCloseKey(k);
        }
        swprintf(p, 512, L"%ls_Classes\\SetuOracle.advapi32_RegPredef_HKCU", sid);
        got = 0;
        LONG r = read_dword(HKEY_USERS, p, L"m", &got);
        oracle_case("hkcu_classes_is_hku_sid_classes");
        o_status("create", st);
        o_status("read", r);
        o_bool("same_value", got == w);
        oracle_case_end();
        RegDeleteKeyW(HKEY_CURRENT_USER, L"Software\\Classes\\SetuOracle.advapi32_RegPredef_HKCU");
    }
    {   /* HKEY_CURRENT_USER_LOCAL_SETTINGS. */
        HKEY k = NULL;
        st = RegOpenKeyExW(HKEY_CURRENT_USER_LOCAL_SETTINGS, NULL, 0, KEY_READ, &k);
        oracle_case("hkcu_local_settings_open");
        o_status("ret", st);
        if (!st) {
            DWORD nsub = 0;
            o_status("query_info", RegQueryInfoKeyW(k, NULL, NULL, NULL, &nsub, NULL, NULL, NULL, NULL, NULL, NULL, NULL));
            if (k != HKEY_CURRENT_USER_LOCAL_SETTINGS) RegCloseKey(k);
        }
        oracle_case_end();
        HKEY c = NULL;
        st = RegCreateKeyExW(HKEY_CURRENT_USER_LOCAL_SETTINGS, L"Software\\SetuOracle_RegPredef_HKCU", 0, NULL,
                             REG_OPTION_VOLATILE, KEY_ALL_ACCESS, NULL, &c, NULL);
        oracle_case("hkcu_local_settings_create");
        o_status("ret", st);
        if (!st) {
            RegCloseKey(c);
            swprintf(p, 512, L"%ls_Classes\\Local Settings\\Software\\SetuOracle_RegPredef_HKCU", sid);
            HKEY o;
            LONG r = RegOpenKeyExW(HKEY_USERS, p, 0, KEY_READ, &o);
            o_status("visible_in_hku_sid_classes_local_settings", r);
            if (!r) RegCloseKey(o);
            RegDeleteKeyW(HKEY_CURRENT_USER_LOCAL_SETTINGS, L"Software\\SetuOracle_RegPredef_HKCU");
        }
        oracle_case_end();
    }
    {   /* NULL and empty subkeys on HKCU. */
        HKEY k = NULL;
        st = RegOpenKeyExW(HKEY_CURRENT_USER, NULL, 0, KEY_READ, &k);
        oracle_case("open_hkcu_null");
        o_status("ret", st);
        o_bool("same_handle", k == HKEY_CURRENT_USER);
        oracle_case_end();
        if (!st && k != HKEY_CURRENT_USER) RegCloseKey(k);
        k = NULL;
        st = RegOpenKeyExW(HKEY_CURRENT_USER, L"", 0, KEY_READ, &k);
        oracle_case("open_hkcu_empty");
        o_status("ret", st);
        o_bool("same_handle", k == HKEY_CURRENT_USER);
        oracle_case_end();
        if (!st && k != HKEY_CURRENT_USER) RegCloseKey(k);
        k = NULL;
        DWORD disp = 0;
        st = RegCreateKeyExW(HKEY_CURRENT_USER, L"", 0, NULL, 0, KEY_READ, NULL, &k, &disp);
        oracle_case("create_hkcu_empty");
        o_status("ret", st);
        o_u32("disposition", disp);
        o_bool("same_handle", k == HKEY_CURRENT_USER);
        oracle_case_end();
        if (!st && k != HKEY_CURRENT_USER) RegCloseKey(k);
        k = NULL;
        st = RegOpenKeyExW(HKEY_CURRENT_USER, L"software", 0, KEY_READ, &k);
        oracle_case("open_hkcu_software_lowercase");
        o_status("ret", st);
        oracle_case_end();
        if (!st) RegCloseKey(k);
    }

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
