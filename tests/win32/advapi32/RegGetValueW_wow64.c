/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: RegGetValueW's RRF_SUBKEY_WOW6464KEY and
 * RRF_SUBKEY_WOW6432KEY flags. HKLM\SOFTWARE is redirected for 32-bit
 * views, so a key there is created once in each view with a different
 * value; HKCU\Software is shared, so both views see the same key. Also
 * the flags without a subkey, and both flags together.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-reggetvaluew
 *       https://learn.microsoft.com/en-us/windows/win32/winprog64/registry-redirector
 *       https://learn.microsoft.com/en-us/windows/win32/winprog64/shared-registry-keys
 */
#include "oracle.h"

#define LM_BASE L"SOFTWARE\\SetuOracle"
#define LM_KEY LM_BASE L"\\advapi32_RegGetValueW_wow64"

static void g_case(const char *cname, HKEY key, const wchar_t *sub, DWORD flags)
{
    DWORD v = 0xAAAAAAAAu, t = 0, sz = 4;
    LONG st;
    ORACLE_CALL(st, RegGetValueW(key, sub, L"V", RRF_RT_DWORD | flags, &t, &v, &sz));
    oracle_case(cname);
    o_status("ret", st);
    o_lasterr();
    o_hex32("value", v);
    oracle_case_end();
}

static void lm_cleanup(void)
{
    static const REGSAM views[2] = { KEY_WOW64_64KEY, KEY_WOW64_32KEY };
    for (int i = 0; i < 2; i++) {
        RegDeleteKeyExW(HKEY_LOCAL_MACHINE, LM_KEY, views[i], 0);
        /* Remove SetuOracle too if it is empty now. */
        RegDeleteKeyExW(HKEY_LOCAL_MACHINE, LM_BASE, views[i], 0);
    }
}

static LONG lm_create(REGSAM view, DWORD v)
{
    HKEY k = NULL;
    LONG r = RegCreateKeyExW(HKEY_LOCAL_MACHINE, LM_KEY, 0, NULL, 0, KEY_ALL_ACCESS | view, NULL, &k, NULL);
    if (!r) {
        RegSetValueExW(k, L"V", 0, REG_DWORD, (const BYTE *)&v, 4);
        RegCloseKey(k);
    }
    return r;
}

int main(void)
{
    oracle_begin("advapi32/RegGetValueW_wow64");
    HKEY root = oracle_reg_root();
    DWORD cu = 0xC0;
    RegSetValueExW(root, L"V", 0, REG_DWORD, (const BYTE *)&cu, 4);

    lm_cleanup();
    oracle_case("setup");
    o_status("create64", lm_create(KEY_WOW64_64KEY, 0x64));
    o_status("create32", lm_create(KEY_WOW64_32KEY, 0x32));
    oracle_case_end();

    g_case("hklm_default_view", HKEY_LOCAL_MACHINE, LM_KEY, 0);
    g_case("hklm_64", HKEY_LOCAL_MACHINE, LM_KEY, RRF_SUBKEY_WOW6464KEY);
    g_case("hklm_32", HKEY_LOCAL_MACHINE, LM_KEY, RRF_SUBKEY_WOW6432KEY);
    g_case("hklm_both", HKEY_LOCAL_MACHINE, LM_KEY, RRF_SUBKEY_WOW6464KEY | RRF_SUBKEY_WOW6432KEY);
    g_case("hkcu_64", HKEY_CURRENT_USER, ORACLE_REG_BASE L"\\advapi32_RegGetValueW_wow64", RRF_SUBKEY_WOW6464KEY);
    g_case("hkcu_32", HKEY_CURRENT_USER, ORACLE_REG_BASE L"\\advapi32_RegGetValueW_wow64", RRF_SUBKEY_WOW6432KEY);
    g_case("no_subkey_32", root, NULL, RRF_SUBKEY_WOW6432KEY);
    g_case("no_subkey_64", root, NULL, RRF_SUBKEY_WOW6464KEY);
    {
        HKEY k = NULL;
        RegOpenKeyExW(HKEY_LOCAL_MACHINE, LM_BASE, 0, KEY_READ | KEY_WOW64_32KEY, &k);
        g_case("from_32_parent_32", k, L"advapi32_RegGetValueW_wow64", RRF_SUBKEY_WOW6432KEY);
        g_case("from_32_parent_64", k, L"advapi32_RegGetValueW_wow64", RRF_SUBKEY_WOW6464KEY);
        g_case("from_32_parent_default", k, L"advapi32_RegGetValueW_wow64", 0);
        RegCloseKey(k);
    }

    lm_cleanup();
    oracle_case("cleanup");
    {
        HKEY k = NULL;
        o_status("open64_after", RegOpenKeyExW(HKEY_LOCAL_MACHINE, LM_KEY, 0, KEY_READ | KEY_WOW64_64KEY, &k));
        if (k) RegCloseKey(k);
        k = NULL;
        o_status("open32_after", RegOpenKeyExW(HKEY_LOCAL_MACHINE, LM_KEY, 0, KEY_READ | KEY_WOW64_32KEY, &k));
        if (k) RegCloseKey(k);
    }
    oracle_case_end();

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
