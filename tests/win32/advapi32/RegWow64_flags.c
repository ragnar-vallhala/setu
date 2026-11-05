/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: KEY_WOW64_32KEY / KEY_WOW64_64KEY flag handling: both flags
 * at once, flags on RegCreateKeyExW, RegOpenKeyExW, RegDeleteKeyExW and
 * RegCopyTree-style relative use, flags on HKCU (shared), on HKCR\CLSID
 * (redirected to HKCR\WOW6432Node\CLSID), and the reflection functions
 * RegQueryReflectionKey / RegDisableReflectionKey / RegEnableReflectionKey.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/winprog64/accessing-an-alternate-registry-view
 *       https://learn.microsoft.com/en-us/windows/win32/winprog64/registry-reflection
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regqueryreflectionkey
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regdisablereflectionkey
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regenablereflectionkey
 */
#include "oracle.h"

#define T L"SetuOracle_RegWow64_flags"
#define CLSID_T L"{5e70a11e-0000-4000-8000-5e7500000002}"

static LONG open_st(HKEY base, const wchar_t *path, REGSAM view)
{
    HKEY k;
    LONG st = RegOpenKeyExW(base, path, 0, KEY_READ | view, &k);
    if (!st) RegCloseKey(k);
    return st;
}

static void cleanup(void)
{
    RegDeleteKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\" T, KEY_WOW64_32KEY, 0);
    RegDeleteKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\" T, KEY_WOW64_64KEY, 0);
    RegDeleteKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\" T L"_both", KEY_WOW64_32KEY, 0);
    RegDeleteKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\" T L"_both", KEY_WOW64_64KEY, 0);
    RegDeleteKeyExW(HKEY_CLASSES_ROOT, L"CLSID\\" CLSID_T, KEY_WOW64_32KEY, 0);
    RegDeleteKeyExW(HKEY_CLASSES_ROOT, L"CLSID\\" CLSID_T, KEY_WOW64_64KEY, 0);
}

int main(void)
{
    oracle_begin("advapi32/RegWow64_flags");
    cleanup();
    HKEY root = oracle_reg_root();

    {
        HKEY k = NULL;
        DWORD disp = 0;
        LONG st;
        ORACLE_CALL(st, RegCreateKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\" T L"_both", 0, NULL, 0,
                                        KEY_ALL_ACCESS | KEY_WOW64_32KEY | KEY_WOW64_64KEY, NULL, &k, &disp));
        oracle_case("create_both_flags");
        o_status("ret", st);
        o_lasterr();
        if (!st) {
            o_u32("disposition", disp);
            RegCloseKey(k);
        }
        o_status("in_32", open_st(HKEY_LOCAL_MACHINE, L"SOFTWARE\\" T L"_both", KEY_WOW64_32KEY));
        o_status("in_64", open_st(HKEY_LOCAL_MACHINE, L"SOFTWARE\\" T L"_both", KEY_WOW64_64KEY));
        oracle_case_end();
        ORACLE_CALL(st, RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE", 0, KEY_READ | KEY_WOW64_32KEY | KEY_WOW64_64KEY, &k));
        oracle_case("open_both_flags");
        o_status("ret", st);
        o_lasterr();
        oracle_case_end();
        if (!st) RegCloseKey(k);
        ORACLE_CALL(st, RegDeleteKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\" T L"_both", KEY_WOW64_32KEY | KEY_WOW64_64KEY, 0));
        oracle_case("delete_both_flags");
        o_status("ret", st);
        o_lasterr();
        o_status("in_32", open_st(HKEY_LOCAL_MACHINE, L"SOFTWARE\\" T L"_both", KEY_WOW64_32KEY));
        o_status("in_64", open_st(HKEY_LOCAL_MACHINE, L"SOFTWARE\\" T L"_both", KEY_WOW64_64KEY));
        oracle_case_end();
    }
    {   /* HKCR\CLSID with the 32-bit flag. */
        HKEY k = NULL;
        LONG st = RegCreateKeyExW(HKEY_CLASSES_ROOT, L"CLSID\\" CLSID_T, 0, NULL, 0, KEY_ALL_ACCESS | KEY_WOW64_32KEY, NULL,
                                  &k, NULL);
        if (!st) RegCloseKey(k);
        oracle_case("hkcr_clsid_32");
        o_status("create", st);
        o_status("open_32", open_st(HKEY_CLASSES_ROOT, L"CLSID\\" CLSID_T, KEY_WOW64_32KEY));
        o_status("open_64", open_st(HKEY_CLASSES_ROOT, L"CLSID\\" CLSID_T, KEY_WOW64_64KEY));
        o_status("open_hkcr_wow6432node_path", open_st(HKEY_CLASSES_ROOT, L"WOW6432Node\\CLSID\\" CLSID_T, 0));
        o_status("open_hklm_classes_wow6432node_path",
                 open_st(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Classes\\WOW6432Node\\CLSID\\" CLSID_T, 0));
        o_status("open_hklm_wow6432node_classes_path",
                 open_st(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Classes\\CLSID\\" CLSID_T, 0));
        oracle_case_end();
    }
    {   /* The flag on an HKCU test key changes nothing. */
        HKEY k = NULL;
        LONG st = RegCreateKeyExW(root, L"x", 0, NULL, 0, KEY_ALL_ACCESS | KEY_WOW64_32KEY, NULL, &k, NULL);
        if (!st) RegCloseKey(k);
        oracle_case("hkcu_flags_shared");
        o_status("create_32", st);
        o_status("open_64", open_st(root, L"x", KEY_WOW64_64KEY));
        o_status("open_none", open_st(root, L"x", 0));
        oracle_case_end();
    }
    {   /* Reflection functions (reflection is gone since Windows 7). */
        HKEY k = NULL, k32 = NULL;
        RegCreateKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\" T, 0, NULL, 0, KEY_ALL_ACCESS | KEY_WOW64_64KEY, NULL, &k, NULL);
        RegCreateKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\" T, 0, NULL, 0, KEY_ALL_ACCESS | KEY_WOW64_32KEY, NULL, &k32, NULL);
        BOOL disabled = 77;
        LONG st;
        ORACLE_CALL(st, RegQueryReflectionKey(k, &disabled));
        oracle_case("query_reflection");
        o_status("ret", st);
        o_lasterr();
        o_u32("disabled", (DWORD)disabled);
        oracle_case_end();
        ORACLE_CALL(st, RegDisableReflectionKey(k));
        oracle_case("disable_reflection");
        o_status("ret", st);
        o_lasterr();
        disabled = 77;
        o_status("query_ret", RegQueryReflectionKey(k, &disabled));
        o_u32("disabled", (DWORD)disabled);
        oracle_case_end();
        ORACLE_CALL(st, RegEnableReflectionKey(k));
        oracle_case("enable_reflection");
        o_status("ret", st);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(st, RegQueryReflectionKey(k32, &disabled));
        oracle_case("query_reflection_32");
        o_status("ret", st);
        oracle_case_end();
        ORACLE_CALL(st, RegQueryReflectionKey(HKEY_LOCAL_MACHINE, &disabled));
        oracle_case("query_reflection_hklm");
        o_status("ret", st);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(st, RegQueryReflectionKey((HKEY)(ULONG_PTR)0x1234, &disabled));
        oracle_case("query_reflection_invalid");
        o_status("ret", st);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(st, RegDisableReflectionKey(root));
        oracle_case("disable_reflection_hkcu_key");
        o_status("ret", st);
        o_lasterr();
        oracle_case_end();
        RegCloseKey(k);
        RegCloseKey(k32);
    }

    cleanup();
    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
