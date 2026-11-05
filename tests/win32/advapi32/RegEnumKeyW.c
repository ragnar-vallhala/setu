/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: the legacy RegEnumKeyW: names, buffer sizes in characters,
 * zero and NULL buffers, past-the-end indexes, invalid handles, handles
 * without KEY_ENUMERATE_SUB_KEYS, and enumerating a few predefined keys
 * (only booleans about well-known names, never the list of keys).
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regenumkeyw
 *       https://learn.microsoft.com/en-us/windows/win32/sysinfo/predefined-keys
 */
#include "oracle.h"

static void enum_case(const char *name, HKEY k, DWORD index, wchar_t *buf, DWORD cap)
{
    if (buf) { wmemset(buf, L'#', 63); buf[63] = 0; }
    LONG st;
    ORACLE_CALL(st, RegEnumKeyW(k, index, buf, cap));
    oracle_case(name);
    o_status("ret", st);
    o_lasterr();
    if (buf) {
        buf[20] = 0;
        o_wstr("buf", buf);
    }
    oracle_case_end();
}

/* Does an enumeration of `k` include `want` (case-insensitive)? */
static void contains_case(const char *name, HKEY k, const wchar_t *want)
{
    int found = 0;
    DWORD i;
    LONG st = 0;
    for (i = 0; i < 100000; i++) {
        wchar_t nm[300];
        st = RegEnumKeyW(k, i, nm, 300);
        if (st) break;
        if (!_wcsicmp(nm, want)) found = 1;
    }
    oracle_case(name);
    o_bool("found", found);
    o_bool("nonempty", i > 0);
    o_status("final_ret", st);
    oracle_case_end();
}

int main(void)
{
    oracle_begin("advapi32/RegEnumKeyW");
    HKEY root = oracle_reg_root(), k;
    wchar_t buf[64];
    RegCreateKeyExW(root, L"Echo", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL);
    RegCloseKey(k);
    RegCreateKeyExW(root, L"bravo", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL);
    RegCloseKey(k);

    enum_case("index0", root, 0, buf, 64);
    enum_case("index1", root, 1, buf, 64);
    enum_case("index2_end", root, 2, buf, 64);
    enum_case("cap_5_no_nul_room", root, 0, buf, 5);
    enum_case("cap_6_exact", root, 0, buf, 6);
    enum_case("cap_0", root, 0, buf, 0);
    enum_case("invalid_handle", (HKEY)(ULONG_PTR)0x1234, 0, buf, 64);
    {
        HKEY q;
        RegOpenKeyExW(root, NULL, 0, KEY_QUERY_VALUE, &q);
        enum_case("query_value_only_handle", q, 0, buf, 64);
        RegCloseKey(q);
    }
    {
        HKEY e;
        RegOpenKeyExW(root, NULL, 0, KEY_ENUMERATE_SUB_KEYS, &e);
        enum_case("enumerate_only_handle", e, 0, buf, 64);
        RegCloseKey(e);
    }

    contains_case("hkcu_has_software", HKEY_CURRENT_USER, L"Software");
    contains_case("hklm_has_software", HKEY_LOCAL_MACHINE, L"SOFTWARE");
    contains_case("hklm_has_system", HKEY_LOCAL_MACHINE, L"SYSTEM");
    contains_case("hku_has_default", HKEY_USERS, L".DEFAULT");
    contains_case("hkcc_has_software", HKEY_CURRENT_CONFIG, L"Software");
    contains_case("hkcr_has_dot_txt", HKEY_CLASSES_ROOT, L".txt");
    contains_case("hkcr_has_clsid", HKEY_CLASSES_ROOT, L"CLSID");

    enum_case("null_buffer", root, 0, NULL, 64); /* may fault: last */

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
