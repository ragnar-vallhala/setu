/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: RegEnumValueW while the key changes: deleting the value just
 * enumerated (the classic delete-while-enumerating loop), deleting at index
 * 0 repeatedly, adding values during enumeration, changing a value's data
 * between calls, and enumerating through a second handle to the same key.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regenumvaluew
 */
#include "oracle.h"

static HKEY root;

static void fill(DWORD n)
{
    for (DWORD i = 0; i < n; i++) {
        wchar_t name[8];
        swprintf(name, 8, L"v%u", (unsigned)i);
        RegSetValueExW(root, name, 0, REG_DWORD, (const BYTE *)&i, 4);
    }
}

static DWORD count(void)
{
    DWORD n = 0;
    RegQueryInfoKeyW(root, NULL, NULL, NULL, NULL, NULL, NULL, &n, NULL, NULL, NULL, NULL);
    return n;
}

int main(void)
{
    oracle_begin("advapi32/RegEnumValueW_modify");
    root = oracle_reg_root();
    wchar_t name[16];
    char seen[512];
    size_t n;

    fill(6);
    oracle_case("delete_current_increasing_index");
    n = 0;
    seen[0] = 0;
    for (DWORD i = 0; i < 20; i++) {
        DWORD len = 16;
        if (RegEnumValueW(root, i, name, &len, NULL, NULL, NULL, NULL)) break;
        n += snprintf(seen + n, sizeof seen - n, "%s%s", i ? "," : "", oracle_utf8(name));
        RegDeleteValueW(root, name);
    }
    o_str("seen", seen);
    o_u32("left", count());
    oracle_case_end();
    for (DWORD i = 0; i < 6; i++) { swprintf(name, 16, L"v%u", (unsigned)i); RegDeleteValueW(root, name); }

    fill(6);
    oracle_case("delete_at_index_0");
    n = 0;
    seen[0] = 0;
    for (DWORD i = 0; i < 20; i++) {
        DWORD len = 16;
        if (RegEnumValueW(root, 0, name, &len, NULL, NULL, NULL, NULL)) break;
        n += snprintf(seen + n, sizeof seen - n, "%s%s", i ? "," : "", oracle_utf8(name));
        RegDeleteValueW(root, name);
    }
    o_str("seen", seen);
    o_u32("left", count());
    oracle_case_end();

    fill(3);
    oracle_case("add_during_enumeration");
    n = 0;
    seen[0] = 0;
    for (DWORD i = 0; i < 20; i++) {
        DWORD len = 16;
        if (RegEnumValueW(root, i, name, &len, NULL, NULL, NULL, NULL)) break;
        n += snprintf(seen + n, sizeof seen - n, "%s%s", i ? "," : "", oracle_utf8(name));
        if (i < 2) {
            wchar_t add[16];
            swprintf(add, 16, L"added%u", (unsigned)i);
            RegSetValueExW(root, add, 0, REG_DWORD, (const BYTE *)&i, 4);
        }
    }
    o_str("seen", seen);
    o_u32("count", count());
    oracle_case_end();

    oracle_case("change_data_between_calls");
    {
        DWORD v = 0, dl = 4, len = 16;
        RegEnumValueW(root, 1, name, &len, NULL, NULL, (BYTE *)&v, &dl);
        o_wstr("name_before", name);
        o_u32("data_before", v);
        RegSetValueExW(root, name, 0, REG_SZ, (const BYTE *)L"str", 8);
        DWORD t = 0;
        unsigned char b[16];
        dl = sizeof b;
        len = 16;
        LONG r = RegEnumValueW(root, 1, name, &len, NULL, &t, b, &dl);
        o_status("ret_after", r);
        o_wstr("name_after", name);
        o_u32("type_after", t);
        o_u32("size_after", dl);
    }
    oracle_case_end();

    oracle_case("second_handle_sees_changes");
    {
        HKEY h2 = NULL;
        RegOpenKeyExW(HKEY_CURRENT_USER, ORACLE_REG_BASE L"\\advapi32_RegEnumValueW_modify", 0, KEY_ALL_ACCESS, &h2);
        RegSetValueExW(h2, L"via2", 0, REG_DWORD, (const BYTE *)"\1\0\0\0", 4);
        n = 0;
        seen[0] = 0;
        for (DWORD i = 0; i < 20; i++) {
            DWORD len = 16;
            if (RegEnumValueW(root, i, name, &len, NULL, NULL, NULL, NULL)) break;
            n += snprintf(seen + n, sizeof seen - n, "%s%s", i ? "," : "", oracle_utf8(name));
        }
        o_str("seen", seen);
        RegCloseKey(h2);
    }
    oracle_case_end();

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
