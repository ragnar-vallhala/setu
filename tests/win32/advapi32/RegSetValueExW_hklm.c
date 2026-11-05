/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: values under HKEY_LOCAL_MACHINE\SOFTWARE (the test runs as an
 * elevated administrator): set and read in the 64-bit view, the 32-bit
 * (redirected) view is separate, the default value, a volatile key's
 * values, a read-only handle, and deleting in one view only.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regsetvalueexw
 *       https://learn.microsoft.com/en-us/windows/win32/winprog64/registry-redirector
 *       https://learn.microsoft.com/en-us/windows/win32/sysinfo/predefined-keys
 */
#include "oracle.h"

#define LM_BASE L"SOFTWARE\\SetuOracle"
#define LM_KEY LM_BASE L"\\advapi32_RegSetValueExW_hklm"

static void cleanup(void)
{
    static const REGSAM views[2] = { KEY_WOW64_64KEY, KEY_WOW64_32KEY };
    for (int i = 0; i < 2; i++) {
        RegDeleteKeyExW(HKEY_LOCAL_MACHINE, LM_KEY L"\\vol", views[i], 0);
        RegDeleteKeyExW(HKEY_LOCAL_MACHINE, LM_KEY, views[i], 0);
        RegDeleteKeyExW(HKEY_LOCAL_MACHINE, LM_BASE, views[i], 0);
    }
}

static void q(const char *prefix, HKEY k, const wchar_t *name)
{
    char key[48];
    DWORD v = 0xAAAAAAAAu, sz = 4, t = 0;
    LONG r = RegQueryValueExW(k, name, NULL, &t, (BYTE *)&v, &sz);
    snprintf(key, sizeof key, "%s_ret", prefix); o_status(key, r);
    snprintf(key, sizeof key, "%s_value", prefix); o_hex32(key, v);
}

int main(void)
{
    oracle_begin("advapi32/RegSetValueExW_hklm");
    cleanup();
    HKEY k64 = NULL, k32 = NULL, vol = NULL;
    LONG r64 = RegCreateKeyExW(HKEY_LOCAL_MACHINE, LM_KEY, 0, NULL, 0, KEY_ALL_ACCESS | KEY_WOW64_64KEY, NULL, &k64, NULL);
    LONG r32 = RegCreateKeyExW(HKEY_LOCAL_MACHINE, LM_KEY, 0, NULL, 0, KEY_ALL_ACCESS | KEY_WOW64_32KEY, NULL, &k32, NULL);
    oracle_case("create");
    o_status("create64", r64);
    o_status("create32", r32);
    oracle_case_end();
    if (r64 || r32) { cleanup(); return oracle_end(); }

    DWORD a = 0x64, b = 0x32, c = 0xDEF;
    LONG st;
    ORACLE_CALL(st, RegSetValueExW(k64, L"V", 0, REG_DWORD, (const BYTE *)&a, 4));
    oracle_case("set_64");
    o_status("ret", st);
    o_lasterr();
    q("read64", k64, L"V");
    q("read32", k32, L"V");
    oracle_case_end();

    ORACLE_CALL(st, RegSetValueExW(k32, L"V", 0, REG_DWORD, (const BYTE *)&b, 4));
    oracle_case("set_32_separate");
    o_status("ret", st);
    o_lasterr();
    q("read64", k64, L"V");
    q("read32", k32, L"V");
    oracle_case_end();

    ORACLE_CALL(st, RegSetValueExW(k64, NULL, 0, REG_DWORD, (const BYTE *)&c, 4));
    oracle_case("default_value");
    o_status("ret", st);
    q("read64", k64, NULL);
    q("read32", k32, NULL);
    oracle_case_end();

    {
        LONG rv = RegCreateKeyExW(k64, L"vol", 0, NULL, REG_OPTION_VOLATILE, KEY_ALL_ACCESS, NULL, &vol, NULL);
        ORACLE_CALL(st, RegSetValueExW(vol, L"V", 0, REG_DWORD, (const BYTE *)&a, 4));
        oracle_case("volatile_key_value");
        o_status("create_ret", rv);
        o_status("ret", st);
        q("read", vol, L"V");
        oracle_case_end();
        RegCloseKey(vol);
    }

    {
        HKEY ro = NULL;
        RegOpenKeyExW(HKEY_LOCAL_MACHINE, LM_KEY, 0, KEY_READ | KEY_WOW64_64KEY, &ro);
        ORACLE_CALL(st, RegSetValueExW(ro, L"R", 0, REG_DWORD, (const BYTE *)&a, 4));
        oracle_case("readonly_handle");
        o_status("ret", st);
        o_lasterr();
        oracle_case_end();
        RegCloseKey(ro);
    }

    ORACLE_CALL(st, RegDeleteValueW(k64, L"V"));
    oracle_case("delete_64_keeps_32");
    o_status("ret", st);
    q("read64", k64, L"V");
    q("read32", k32, L"V");
    oracle_case_end();

    RegCloseKey(k64);
    RegCloseKey(k32);
    cleanup();
    oracle_case("cleanup");
    {
        HKEY k = NULL;
        o_status("open_after", RegOpenKeyExW(HKEY_LOCAL_MACHINE, LM_KEY, 0, KEY_READ, &k));
        if (k) RegCloseKey(k);
    }
    oracle_case_end();
    return oracle_end();
}
