/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: writing through HKEY_CLASSES_ROOT (elevated administrator):
 * where new keys and values land (HKLM\Software\Classes or
 * HKCU\Software\Classes) depending on which store already has the key, and
 * which copy RegDeleteKeyW / RegDeleteValueW through HKCR remove.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/sysinfo/hkey-classes-root-key
 *       https://learn.microsoft.com/en-us/windows/win32/sysinfo/merged-view-of-hkey-classes-root
 */
#include "oracle.h"

#define NAME L"SetuOracle.advapi32_RegPredef_HKCR_write"
#define LM L"SOFTWARE\\Classes\\" NAME
#define CU L"Software\\Classes\\" NAME

static void wipe(void)
{
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, LM);
    RegDeleteKeyW(HKEY_LOCAL_MACHINE, LM);
    RegDeleteTreeW(HKEY_CURRENT_USER, CU);
    RegDeleteKeyW(HKEY_CURRENT_USER, CU);
}

static void mk(HKEY base, const wchar_t *path)
{
    HKEY k;
    if (!RegCreateKeyExW(base, path, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL)) RegCloseKey(k);
}

static LONG has_key(HKEY base, const wchar_t *path)
{
    HKEY k;
    LONG st = RegOpenKeyExW(base, path, 0, KEY_READ, &k);
    if (!st) RegCloseKey(k);
    return st;
}

static LONG has_value(HKEY base, const wchar_t *path, const wchar_t *v)
{
    HKEY k;
    LONG st = RegOpenKeyExW(base, path, 0, KEY_READ, &k);
    if (!st) {
        st = RegQueryValueExW(k, v, NULL, NULL, NULL, NULL);
        RegCloseKey(k);
    }
    return st;
}

static void where_key(const wchar_t *rel)
{
    wchar_t p[256];
    swprintf(p, 256, L"%ls%ls", LM, rel);
    o_status("in_hklm", has_key(HKEY_LOCAL_MACHINE, p));
    swprintf(p, 256, L"%ls%ls", CU, rel);
    o_status("in_hkcu", has_key(HKEY_CURRENT_USER, p));
}

static void where_value(const wchar_t *rel, const wchar_t *v)
{
    wchar_t p[256];
    swprintf(p, 256, L"%ls%ls", LM, rel);
    o_status("value_in_hklm", has_value(HKEY_LOCAL_MACHINE, p, v));
    swprintf(p, 256, L"%ls%ls", CU, rel);
    o_status("value_in_hkcu", has_value(HKEY_CURRENT_USER, p, v));
}

static void create_case(const char *name, const wchar_t *rel)
{
    HKEY k = NULL;
    DWORD disp = 0;
    wchar_t p[256];
    swprintf(p, 256, L"%ls%ls", NAME, rel);
    LONG st;
    ORACLE_CALL(st, RegCreateKeyExW(HKEY_CLASSES_ROOT, p, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, &disp));
    oracle_case(name);
    o_status("ret", st);
    o_u32("disposition", disp);
    if (!st) RegCloseKey(k);
    where_key(rel);
    oracle_case_end();
}

static void setval_case(const char *name, const wchar_t *rel, const wchar_t *v)
{
    HKEY k = NULL;
    wchar_t p[256];
    swprintf(p, 256, L"%ls%ls", NAME, rel);
    LONG ost = RegOpenKeyExW(HKEY_CLASSES_ROOT, p, 0, KEY_ALL_ACCESS, &k);
    LONG st = -1;
    DWORD d = 5;
    if (!ost) {
        st = RegSetValueExW(k, v, 0, REG_DWORD, (const BYTE *)&d, sizeof d);
        RegCloseKey(k);
    }
    oracle_case(name);
    o_status("open", ost);
    o_status("ret", st);
    where_value(rel, v);
    oracle_case_end();
}

int main(void)
{
    oracle_begin("advapi32/RegPredef_HKCR_write");
    wipe();
    mk(HKEY_LOCAL_MACHINE, LM L"\\lm");
    mk(HKEY_CURRENT_USER, CU L"\\cu");
    mk(HKEY_LOCAL_MACHINE, LM L"\\both");
    mk(HKEY_CURRENT_USER, CU L"\\both");

    create_case("create_new_top_level_child", L"\\fresh");
    create_case("create_under_lm_only", L"\\lm\\new");
    create_case("create_under_cu_only", L"\\cu\\new");
    create_case("create_under_both", L"\\both\\new");
    create_case("open_existing_lm_with_create", L"\\lm");
    create_case("open_existing_cu_with_create", L"\\cu");

    setval_case("set_value_lm_only", L"\\lm", L"v");
    setval_case("set_value_cu_only", L"\\cu", L"v");
    setval_case("set_value_both", L"\\both", L"v");

    {
        wchar_t p[256];
        swprintf(p, 256, L"%ls\\both\\new", NAME);
        LONG st = RegDeleteKeyW(HKEY_CLASSES_ROOT, p);
        oracle_case("delete_new_under_both");
        o_status("ret", st);
        where_key(L"\\both\\new");
        oracle_case_end();
        mk(HKEY_LOCAL_MACHINE, LM L"\\both\\dup");
        mk(HKEY_CURRENT_USER, CU L"\\both\\dup");
        swprintf(p, 256, L"%ls\\both\\dup", NAME);
        st = RegDeleteKeyW(HKEY_CLASSES_ROOT, p);
        oracle_case("delete_key_in_both_stores");
        o_status("ret", st);
        where_key(L"\\both\\dup");
        oracle_case_end();
        st = RegDeleteKeyW(HKEY_CLASSES_ROOT, p);
        oracle_case("delete_key_in_both_stores_again");
        o_status("ret", st);
        where_key(L"\\both\\dup");
        oracle_case_end();
    }

    wipe();
    return oracle_end();
}
