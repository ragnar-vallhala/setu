/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: RegSetValueExA value names: NULL and "" (the default value),
 * case-insensitive matching, a high-ANSI name, and long names around the
 * 16,383-character limit; names read back with RegEnumValueW.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regsetvalueexa
 *       https://learn.microsoft.com/en-us/windows/win32/sysinfo/registry-element-size-limits
 */
#include "oracle.h"

static HKEY root;
static char big[40000];
static wchar_t wbig[40000];

static void set_case(const char *cname, const char *value, DWORD dw)
{
    LONG st;
    ORACLE_CALL(st, RegSetValueExA(root, value, 0, REG_DWORD, (const BYTE *)&dw, 4));
    oracle_case(cname);
    o_status("ret", st);
    o_lasterr();
    DWORD got = 0, sz = 4;
    LONG q = RegQueryValueExA(root, value, NULL, NULL, (BYTE *)&got, &sz);
    o_status("q_ret", q);
    if (!q) o_u32("q_value", got);
    DWORD n = 0;
    RegQueryInfoKeyW(root, NULL, NULL, NULL, NULL, NULL, NULL, &n, NULL, NULL, NULL, NULL);
    o_u32("values", n);
    oracle_case_end();
}

static void long_case(const char *cname, DWORD len)
{
    for (DWORD i = 0; i < len; i++) big[i] = (char)('a' + i % 26);
    big[len] = 0;
    LONG st;
    ORACLE_CALL(st, RegSetValueExA(root, big, 0, REG_DWORD, (const BYTE *)&len, 4));
    oracle_case(cname);
    o_status("ret", st);
    o_lasterr();
    DWORD got = 0, sz = 4;
    LONG q = RegQueryValueExA(root, big, NULL, NULL, (BYTE *)&got, &sz);
    o_status("q_ret", q);
    if (!q) o_u32("q_value", got);
    DWORD found = 0;
    for (DWORD i = 0;; i++) {
        DWORD l = 40000;
        if (RegEnumValueW(root, i, wbig, &l, NULL, NULL, NULL, NULL)) break;
        if (l > found) found = l;
    }
    o_u32("enum_longest", found);
    if (!st) o_status("delete_ret", RegDeleteValueA(root, big));
    oracle_case_end();
}

int main(void)
{
    oracle_begin("advapi32/RegSetValueExA_names");
    root = oracle_reg_root();

    set_case("default_null", NULL, 1);
    set_case("default_empty", "", 2);
    set_case("mixed_case", "MixedCase", 3);
    set_case("upper_overwrites", "MIXEDCASE", 4);
    set_case("high_ansi", "caf\xe9", 5);
    set_case("high_ansi_upper", "CAF\xc9", 6);
    oracle_case("names_seen_as_unicode");
    {
        char out[512];
        size_t n = 0;
        out[0] = 0;
        for (DWORD i = 0;; i++) {
            DWORD l = 64;
            if (RegEnumValueW(root, i, wbig, &l, NULL, NULL, NULL, NULL)) break;
            n += snprintf(out + n, sizeof out - n, "%s%s", i ? "|" : "", oracle_utf8(wbig));
        }
        o_str("names", out);
    }
    oracle_case_end();

    long_case("name_16383", 16383);
    long_case("name_16384", 16384);
    long_case("name_32767", 32767);

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
