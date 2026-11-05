/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: RegDeleteKeyExW and the registry views from a 64-bit process.
 * Keys are created under HKLM\SOFTWARE\SetuOracle\<test> in the 64-bit view
 * and in the 32-bit view (KEY_WOW64_32KEY, which redirects to WOW6432Node),
 * then deleted with each samDesired view flag to see which one is affected.
 * Also checks HKCU\Software, which is shared (not redirected).
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regdeletekeyexw
 *       https://learn.microsoft.com/en-us/windows/win32/winprog64/accessing-an-alternate-registry-view
 *       https://learn.microsoft.com/en-us/windows/win32/winprog64/registry-redirector
 *       https://learn.microsoft.com/en-us/windows/win32/winprog64/shared-registry-keys
 */
#include "oracle.h"

#define BASE64 L"SOFTWARE\\SetuOracle\\advapi32_RegDeleteKeyExW"
#define BASE32 L"SOFTWARE\\WOW6432Node\\SetuOracle\\advapi32_RegDeleteKeyExW"

static void wipe(void)
{
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, BASE64);
    RegDeleteKeyW(HKEY_LOCAL_MACHINE, BASE64);
    RegDeleteKeyW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\SetuOracle");
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, BASE32);
    RegDeleteKeyW(HKEY_LOCAL_MACHINE, BASE32);
    RegDeleteKeyW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\SetuOracle");
}

static void mk(REGSAM view, const wchar_t *sub)
{
    wchar_t path[256];
    HKEY k;
    swprintf(path, 256, L"%ls\\%ls", BASE64, sub);
    if (!RegCreateKeyExW(HKEY_LOCAL_MACHINE, path, 0, NULL, 0, KEY_ALL_ACCESS | view, NULL, &k, NULL)) RegCloseKey(k);
}

/* Reports whether <sub> exists in the 64-bit and 32-bit views (opened by
 * the plain path in each view, and by the explicit WOW6432Node path). */
static void views(const char *field, const wchar_t *sub)
{
    wchar_t path[256];
    char key[64];
    HKEY k;
    LONG s64, s32, snode;
    swprintf(path, 256, L"%ls\\%ls", BASE64, sub);
    s64 = RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_READ | KEY_WOW64_64KEY, &k);
    if (!s64) RegCloseKey(k);
    s32 = RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_READ | KEY_WOW64_32KEY, &k);
    if (!s32) RegCloseKey(k);
    swprintf(path, 256, L"%ls\\%ls", BASE32, sub);
    snode = RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_READ | KEY_WOW64_64KEY, &k);
    if (!snode) RegCloseKey(k);
    snprintf(key, sizeof key, "%s.view64", field);
    o_status(key, s64);
    snprintf(key, sizeof key, "%s.view32", field);
    o_status(key, s32);
    snprintf(key, sizeof key, "%s.wow6432node_path", field);
    o_status(key, snode);
}

static void del_case(const char *name, const wchar_t *sub, REGSAM sam, DWORD reserved)
{
    wchar_t path[256];
    LONG st;
    swprintf(path, 256, L"%ls\\%ls", BASE64, sub);
    ORACLE_CALL(st, RegDeleteKeyExW(HKEY_LOCAL_MACHINE, path, sam, reserved));
    oracle_case(name);
    o_status("ret", st);
    o_lasterr();
    views("after", sub);
    oracle_case_end();
}

int main(void)
{
    oracle_begin("advapi32/RegDeleteKeyExW");
    wipe();

    mk(KEY_WOW64_64KEY, L"k64");
    mk(KEY_WOW64_32KEY, L"k32");
    mk(KEY_WOW64_64KEY, L"both");
    mk(KEY_WOW64_32KEY, L"both");
    mk(0, L"plain");
    oracle_case("created");
    views("k64", L"k64");
    views("k32", L"k32");
    views("both", L"both");
    views("plain", L"plain");
    oracle_case_end();

    del_case("delete_k64_with_32key_flag", L"k64", KEY_WOW64_32KEY, 0);
    del_case("delete_k64_with_64key_flag", L"k64", KEY_WOW64_64KEY, 0);
    del_case("delete_k32_with_no_flag", L"k32", 0, 0);
    del_case("delete_k32_with_32key_flag", L"k32", KEY_WOW64_32KEY, 0);
    del_case("delete_both_with_32key_flag", L"both", KEY_WOW64_32KEY, 0);
    del_case("delete_both_with_64key_flag", L"both", KEY_WOW64_64KEY, 0);
    del_case("delete_plain_with_both_flags", L"plain", KEY_WOW64_32KEY | KEY_WOW64_64KEY, 0);
    del_case("delete_plain_with_no_flag", L"plain", 0, 0);
    mk(0, L"r");
    del_case("delete_reserved_nonzero", L"r", 0, 5);
    del_case("delete_with_access_bits", L"r", KEY_ALL_ACCESS, 0);
    del_case("delete_missing", L"r", 0, 0);

    {   /* HKCU\Software is shared: both flags reach the same key. */
        HKEY root = oracle_reg_root(), k;
        RegCreateKeyExW(root, L"s", 0, NULL, 0, KEY_ALL_ACCESS | KEY_WOW64_32KEY, NULL, &k, NULL);
        RegCloseKey(k);
        LONG o64 = RegOpenKeyExW(root, L"s", 0, KEY_READ | KEY_WOW64_64KEY, &k);
        if (!o64) RegCloseKey(k);
        LONG st;
        ORACLE_CALL(st, RegDeleteKeyExW(root, L"s", KEY_WOW64_64KEY, 0));
        oracle_case("hkcu_created_32_deleted_64");
        o_status("open64_before", o64);
        o_status("ret", st);
        oracle_case_end();
        RegCloseKey(root);
        oracle_reg_cleanup();
    }

    wipe();
    return oracle_end();
}
