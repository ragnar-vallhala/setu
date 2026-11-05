/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: RegOverridePredefKey: after mapping a predefined key to a test
 * key, operations on the predefined handle go to the test key; NULL restores
 * the default. Also overriding with a key that is then closed or deleted,
 * overriding a predefined key with another predefined key, and keys not
 * allowed (HKEY_PERFORMANCE_DATA, a non-predefined handle).
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regoverridepredefkey
 */
#include "oracle.h"

static HKEY root;

static LONG probe(HKEY base, const wchar_t *sub)
{
    HKEY k;
    LONG st = RegOpenKeyExW(base, sub, 0, KEY_READ, &k);
    if (!st) RegCloseKey(k);
    return st;
}

int main(void)
{
    oracle_begin("advapi32/RegOverridePredefKey");
    root = oracle_reg_root();
    HKEY target, k;
    RegCreateKeyExW(root, L"target", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &target, NULL);
    RegCreateKeyExW(target, L"only_in_target", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL);
    RegCloseKey(k);

    LONG st;
    ORACLE_CALL(st, RegOverridePredefKey(HKEY_LOCAL_MACHINE, target));
    oracle_case("override_hklm");
    o_status("ret", st);
    o_lasterr();
    o_status("hklm_sees_target_child", probe(HKEY_LOCAL_MACHINE, L"only_in_target"));
    o_status("hklm_software_hidden", probe(HKEY_LOCAL_MACHINE, L"SOFTWARE"));
    oracle_case_end();
    {
        DWORD v = 5;
        LONG s = RegSetValueExW(HKEY_LOCAL_MACHINE, L"via_override", 0, REG_DWORD, (const BYTE *)&v, sizeof v);
        LONG q = RegQueryValueExW(target, L"via_override", NULL, NULL, NULL, NULL);
        oracle_case("write_through_override");
        o_status("set", s);
        o_status("visible_in_target", q);
        oracle_case_end();
    }
    {   /* Closing our handle to the target: the override keeps its own reference? */
        RegCloseKey(target);
        target = NULL;
        oracle_case("after_closing_target_handle");
        o_status("hklm_sees_target_child", probe(HKEY_LOCAL_MACHINE, L"only_in_target"));
        oracle_case_end();
    }
    ORACLE_CALL(st, RegOverridePredefKey(HKEY_LOCAL_MACHINE, NULL));
    oracle_case("restore_hklm");
    o_status("ret", st);
    o_status("hklm_software_back", probe(HKEY_LOCAL_MACHINE, L"SOFTWARE"));
    o_status("target_child_gone", probe(HKEY_LOCAL_MACHINE, L"only_in_target"));
    oracle_case_end();
    ORACLE_CALL(st, RegOverridePredefKey(HKEY_LOCAL_MACHINE, NULL));
    oracle_case("restore_hklm_twice");
    o_status("ret", st);
    oracle_case_end();

    {   /* Override HKCR with HKCU (a predefined key as the new key). */
        ORACLE_CALL(st, RegOverridePredefKey(HKEY_CLASSES_ROOT, HKEY_CURRENT_USER));
        oracle_case("override_hkcr_with_hkcu");
        o_status("ret", st);
        o_status("hkcr_sees_software", probe(HKEY_CLASSES_ROOT, L"Software\\SetuOracle\\advapi32_RegOverridePredefKey"));
        oracle_case_end();
        RegOverridePredefKey(HKEY_CLASSES_ROOT, NULL);
    }
    {   /* Override with a key that is then deleted. */
        HKEY t2;
        RegCreateKeyExW(root, L"t2", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &t2, NULL);
        RegOverridePredefKey(HKEY_USERS, t2);
        RegCloseKey(t2);
        RegDeleteKeyW(root, L"t2");
        DWORD n = 0;
        LONG q = RegQueryInfoKeyW(HKEY_USERS, NULL, NULL, NULL, &n, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        oracle_case("override_target_deleted");
        o_status("query_info", q);
        oracle_case_end();
        RegOverridePredefKey(HKEY_USERS, NULL);
        oracle_case("override_target_deleted_restored");
        o_status("hku_default", probe(HKEY_USERS, L".DEFAULT"));
        oracle_case_end();
    }
    {
        HKEY t3;
        RegCreateKeyExW(root, L"t3", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &t3, NULL);
        ORACLE_CALL(st, RegOverridePredefKey(HKEY_PERFORMANCE_DATA, t3));
        oracle_case("override_performance_data");
        o_status("ret", st);
        o_lasterr();
        oracle_case_end();
        RegOverridePredefKey(HKEY_PERFORMANCE_DATA, NULL);
        ORACLE_CALL(st, RegOverridePredefKey(t3, root));
        oracle_case("override_non_predefined");
        o_status("ret", st);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(st, RegOverridePredefKey(HKEY_CURRENT_CONFIG, (HKEY)(ULONG_PTR)0x1234));
        oracle_case("override_with_invalid_handle");
        o_status("ret", st);
        o_lasterr();
        oracle_case_end();
        RegOverridePredefKey(HKEY_CURRENT_CONFIG, NULL);
        ORACLE_CALL(st, RegOverridePredefKey((HKEY)(LONG_PTR)(LONG)0x80000010, t3));
        oracle_case("override_undefined_predefined");
        o_status("ret", st);
        o_lasterr();
        oracle_case_end();
        RegCloseKey(t3);
    }

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
