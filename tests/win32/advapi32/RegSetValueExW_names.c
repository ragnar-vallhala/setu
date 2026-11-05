/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: RegSetValueExW value names: NULL and "" (the default value),
 * case preservation and case-insensitive matching, unusual characters
 * (backslash, spaces, non-ASCII), and the documented 16,383-character
 * name limit (names of 16383, 16384, 32767 and 32768 characters).
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regsetvalueexw
 *       https://learn.microsoft.com/en-us/windows/win32/sysinfo/registry-element-size-limits
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regenumvaluew
 */
#include "oracle.h"

static HKEY root;
static wchar_t big[40000];

/* All value names in enumeration order, joined with '|'. */
static void o_names(const char *k)
{
    char out[1024];
    size_t n = 0;
    out[0] = 0;
    for (DWORD i = 0;; i++) {
        DWORD len = 40000;
        if (RegEnumValueW(root, i, big, &len, NULL, NULL, NULL, NULL)) break;
        const char *u = len > 64 ? "<long>" : oracle_utf8(big);
        n += snprintf(out + n, sizeof out - n, "%s%s", i ? "|" : "", u);
        if (n >= sizeof out) break;
    }
    o_str(k, out);
}

static void set_case(const char *cname, const wchar_t *value, DWORD dw)
{
    LONG st;
    ORACLE_CALL(st, RegSetValueExW(root, value, 0, REG_DWORD, (const BYTE *)&dw, 4));
    oracle_case(cname);
    o_status("ret", st);
    o_lasterr();
    DWORD got = 0, sz = 4;
    LONG q = RegQueryValueExW(root, value, NULL, NULL, (BYTE *)&got, &sz);
    o_status("q_ret", q);
    if (!q) o_u32("q_value", got);
    oracle_case_end();
}

static void long_case(const char *cname, DWORD len)
{
    for (DWORD i = 0; i < len; i++) big[i] = L'a' + (wchar_t)(i % 26);
    big[len] = 0;
    DWORD dw = len;
    LONG st;
    ORACLE_CALL(st, RegSetValueExW(root, big, 0, REG_DWORD, (const BYTE *)&dw, 4));
    oracle_case(cname);
    o_status("ret", st);
    o_lasterr();
    DWORD got = 0, sz = 4;
    LONG q = RegQueryValueExW(root, big, NULL, NULL, (BYTE *)&got, &sz);
    o_status("q_ret", q);
    if (!q) o_u32("q_value", got);
    DWORD maxname = 0, nvalues = 0;
    RegQueryInfoKeyW(root, NULL, NULL, NULL, NULL, NULL, NULL, &nvalues, &maxname, NULL, NULL, NULL);
    o_u32("info_values", nvalues);
    o_u32("info_max_name", maxname);
    /* The enumerated name length, with a buffer that is big enough. */
    DWORD found = 0;
    for (DWORD i = 0;; i++) {
        DWORD l = 40000;
        if (RegEnumValueW(root, i, big, &l, NULL, NULL, NULL, NULL)) break;
        if (l > found) found = l;
    }
    o_u32("enum_longest", found);
    if (!st) {
        for (DWORD i = 0; i < len; i++) big[i] = L'a' + (wchar_t)(i % 26);
        big[len] = 0;
        o_status("delete_ret", RegDeleteValueW(root, big));
    }
    oracle_case_end();
}

int main(void)
{
    oracle_begin("advapi32/RegSetValueExW_names");
    root = oracle_reg_root();

    set_case("default_null_name", NULL, 1);
    set_case("default_empty_name", L"", 2);
    oracle_case("default_null_and_empty_same");
    {
        DWORD a = 0, b = 0, s1 = 4, s2 = 4;
        o_status("null_ret", RegQueryValueExW(root, NULL, NULL, NULL, (BYTE *)&a, &s1));
        o_status("empty_ret", RegQueryValueExW(root, L"", NULL, NULL, (BYTE *)&b, &s2));
        o_u32("null_value", a);
        o_u32("empty_value", b);
    }
    o_names("names");
    oracle_case_end();

    set_case("mixed_case", L"MixedCase", 3);
    set_case("upper_case_overwrites", L"MIXEDCASE", 4);
    oracle_case("case_preserved_after_overwrite");
    o_names("names");
    oracle_case_end();
    set_case("lower_case_query", L"mixedcase", 5);

    set_case("backslash", L"a\\b", 6);
    set_case("leading_trailing_space", L" sp ", 7);
    set_case("only_space", L" ", 8);
    set_case("non_ascii", L"\x00e9t\x00e9", 9);
    set_case("non_ascii_upper", L"\x00c9T\x00c9", 10);
    set_case("cjk", L"\x4e2d\x6587", 11);
    set_case("tab_and_newline", L"t\tn\n", 12);
    set_case("percent", L"%PATH%", 13);
    oracle_case("all_names");
    o_names("names");
    oracle_case_end();

    long_case("name_255", 255);
    long_case("name_256", 256);
    long_case("name_16383", 16383);
    long_case("name_16384", 16384);
    long_case("name_32767", 32767);
    long_case("name_32768", 32768);

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
