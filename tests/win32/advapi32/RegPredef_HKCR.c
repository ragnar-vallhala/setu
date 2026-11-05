/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: reading through HKEY_CLASSES_ROOT, the merged view of
 * HKLM\Software\Classes and HKCU\Software\Classes. Test keys are created
 * directly in one store or both, then read through HKCR: which store's
 * values win, whether subkeys of both stores are enumerated, what
 * RegQueryInfoKeyW counts, and what a handle opened through HKCR sees after
 * the HKCU copy is removed.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/sysinfo/hkey-classes-root-key
 *       https://learn.microsoft.com/en-us/windows/win32/sysinfo/merged-view-of-hkey-classes-root
 *       https://learn.microsoft.com/en-us/windows/win32/sysinfo/predefined-keys
 */
#include "oracle.h"

#define NAME L"SetuOracle.advapi32_RegPredef_HKCR"
#define LM L"SOFTWARE\\Classes\\" NAME
#define CU L"Software\\Classes\\" NAME

static void wipe(void)
{
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, LM);
    RegDeleteKeyW(HKEY_LOCAL_MACHINE, LM);
    RegDeleteTreeW(HKEY_CURRENT_USER, CU);
    RegDeleteKeyW(HKEY_CURRENT_USER, CU);
}

static void mk(HKEY base, const wchar_t *path, const wchar_t *vname, DWORD v)
{
    HKEY k;
    if (!RegCreateKeyExW(base, path, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL)) {
        if (vname) RegSetValueExW(k, vname, 0, REG_DWORD, (const BYTE *)&v, sizeof v);
        RegCloseKey(k);
    }
}

static void read_case(const char *name, const wchar_t *sub)
{
    HKEY k = NULL;
    LONG st = RegOpenKeyExW(HKEY_CLASSES_ROOT, sub, 0, KEY_READ, &k);
    oracle_case(name);
    o_status("open", st);
    if (!st) {
        DWORD v = 0, sz = sizeof v;
        LONG q = RegQueryValueExW(k, L"who", NULL, NULL, (BYTE *)&v, &sz);
        o_status("who_ret", q);
        if (!q) o_u32("who", v);
        sz = sizeof v;
        q = RegQueryValueExW(k, L"lm_only", NULL, NULL, (BYTE *)&v, &sz);
        o_status("lm_only_ret", q);
        sz = sizeof v;
        q = RegQueryValueExW(k, L"cu_only", NULL, NULL, (BYTE *)&v, &sz);
        o_status("cu_only_ret", q);
        DWORD nsub = 0, nval = 0;
        RegQueryInfoKeyW(k, NULL, NULL, NULL, &nsub, NULL, NULL, &nval, NULL, NULL, NULL, NULL);
        o_u32("info_subkeys", nsub);
        o_u32("info_values", nval);
        char list[256] = "";
        for (DWORD i = 0;; i++) {
            wchar_t nm[64];
            DWORD n = 64;
            if (RegEnumKeyExW(k, i, nm, &n, NULL, NULL, NULL, NULL)) break;
            if (i) strcat(list, ",");
            strcat(list, oracle_utf8(nm));
        }
        o_str("enum_subkeys", list);
        list[0] = 0;
        for (DWORD i = 0;; i++) {
            wchar_t nm[64];
            DWORD n = 64;
            if (RegEnumValueW(k, i, nm, &n, NULL, NULL, NULL, NULL)) break;
            if (i) strcat(list, ",");
            strcat(list, oracle_utf8(nm));
        }
        o_str("enum_values", list);
        RegCloseKey(k);
    }
    oracle_case_end();
}

int main(void)
{
    oracle_begin("advapi32/RegPredef_HKCR");
    wipe();

    mk(HKEY_LOCAL_MACHINE, LM L"\\lmonly", L"who", 1);
    mk(HKEY_CURRENT_USER, CU L"\\cuonly", L"who", 2);
    mk(HKEY_LOCAL_MACHINE, LM L"\\both", L"who", 1);
    mk(HKEY_LOCAL_MACHINE, LM L"\\both", L"lm_only", 1);
    mk(HKEY_LOCAL_MACHINE, LM L"\\both\\lmsub", NULL, 0);
    mk(HKEY_LOCAL_MACHINE, LM L"\\both\\shared", L"who", 1);
    mk(HKEY_CURRENT_USER, CU L"\\both", L"who", 2);
    mk(HKEY_CURRENT_USER, CU L"\\both", L"cu_only", 2);
    mk(HKEY_CURRENT_USER, CU L"\\both\\cusub", NULL, 0);
    mk(HKEY_CURRENT_USER, CU L"\\both\\shared", L"who", 2);

    read_case("top", NAME);
    read_case("lm_only_key", NAME L"\\lmonly");
    read_case("cu_only_key", NAME L"\\cuonly");
    read_case("both_key", NAME L"\\both");
    read_case("both_key_other_case", NAME L"\\BOTH");
    read_case("both_shared_subkey", NAME L"\\both\\shared");
    read_case("both_lm_subkey", NAME L"\\both\\lmsub");
    read_case("both_cu_subkey", NAME L"\\both\\cusub");
    read_case("missing", NAME L"\\nope");

    {   /* Relative open from a handle opened through HKCR. */
        HKEY h = NULL, k = NULL;
        RegOpenKeyExW(HKEY_CLASSES_ROOT, NAME, 0, KEY_READ, &h);
        LONG a = RegOpenKeyExW(h, L"lmonly", 0, KEY_READ, &k);
        if (!a) RegCloseKey(k);
        LONG b = RegOpenKeyExW(h, L"cuonly", 0, KEY_READ, &k);
        if (!b) RegCloseKey(k);
        oracle_case("relative_open_through_hkcr_handle");
        o_status("lmonly", a);
        o_status("cuonly", b);
        oracle_case_end();
        RegCloseKey(h);
    }
    {   /* The top-level HKCR enumeration contains the name once. */
        int found = 0;
        for (DWORD i = 0; i < 200000; i++) {
            wchar_t nm[300];
            DWORD n = 300;
            if (RegEnumKeyExW(HKEY_CLASSES_ROOT, i, nm, &n, NULL, NULL, NULL, NULL)) break;
            if (!_wcsicmp(nm, NAME)) found++;
        }
        oracle_case("hkcr_root_enum_contains_once");
        o_u32("found", found);
        oracle_case_end();
    }
    {   /* A handle opened through HKCR on a key in both stores, then the HKCU copy is deleted. */
        HKEY h = NULL;
        RegOpenKeyExW(HKEY_CLASSES_ROOT, NAME L"\\both\\shared", 0, KEY_READ, &h);
        RegDeleteKeyW(HKEY_CURRENT_USER, CU L"\\both\\shared");
        DWORD v = 0, sz = sizeof v;
        LONG q = RegQueryValueExW(h, L"who", NULL, NULL, (BYTE *)&v, &sz);
        oracle_case("handle_after_cu_copy_deleted");
        o_status("query", q);
        if (!q) o_u32("who", v);
        oracle_case_end();
        RegCloseKey(h);
        read_case("reopen_after_cu_copy_deleted", NAME L"\\both\\shared");
    }

    wipe();
    return oracle_end();
}
