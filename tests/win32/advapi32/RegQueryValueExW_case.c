/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: case-insensitive value-name matching in RegQueryValueExW
 * beyond ASCII: Latin-1, Greek, Cyrillic, sharp s, dotted/dotless i,
 * full-width letters, and a value set with one case then overwritten with
 * another (the stored name's case).
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regqueryvalueexw
 *       https://learn.microsoft.com/en-us/windows/win32/sysinfo/structure-of-the-registry
 */
#include "oracle.h"

static HKEY root;

static void pair_case(const char *cname, const wchar_t *set_name, const wchar_t *query_name)
{
    DWORD dw = 1;
    RegSetValueExW(root, set_name, 0, REG_DWORD, (const BYTE *)&dw, 4);
    DWORD got = 0, sz = 4;
    LONG st;
    ORACLE_CALL(st, RegQueryValueExW(root, query_name, NULL, NULL, (BYTE *)&got, &sz));
    oracle_case(cname);
    o_status("ret", st);
    o_lasterr();
    /* Does setting the other spelling create a second value? */
    dw = 2;
    RegSetValueExW(root, query_name, 0, REG_DWORD, (const BYTE *)&dw, 4);
    DWORD n = 0;
    RegQueryInfoKeyW(root, NULL, NULL, NULL, NULL, NULL, NULL, &n, NULL, NULL, NULL, NULL);
    o_u32("values_after_set_other", n);
    wchar_t name[32];
    DWORD len = 32;
    if (!RegEnumValueW(root, 0, name, &len, NULL, NULL, NULL, NULL)) o_wstr("first_name", name);
    RegDeleteValueW(root, set_name);
    RegDeleteValueW(root, query_name);
    oracle_case_end();
}

int main(void)
{
    oracle_begin("advapi32/RegQueryValueExW_case");
    root = oracle_reg_root();

    pair_case("ascii", L"abc", L"ABC");
    pair_case("latin1_e_acute", L"\x00e9", L"\x00c9");
    pair_case("latin1_y_diaeresis", L"\x00ff", L"\x0178");
    pair_case("greek_sigma", L"\x03c3", L"\x03a3");
    pair_case("greek_final_sigma", L"\x03c2", L"\x03a3");
    pair_case("cyrillic", L"\x0436", L"\x0416");
    pair_case("sharp_s_vs_ss", L"\x00df", L"SS");
    pair_case("sharp_s_vs_capital", L"\x00df", L"\x1e9e");
    pair_case("dotless_i", L"\x0131", L"I");
    pair_case("dotted_capital_i", L"\x0130", L"i");
    pair_case("fullwidth", L"\xff41", L"\xff21");
    pair_case("kelvin_sign", L"\x212a", L"k");
    pair_case("micro_vs_mu", L"\x00b5", L"\x039c");
    pair_case("georgian", L"\x10d0", L"\x10a0");
    pair_case("cherokee", L"\xab70", L"\x13a0");
    pair_case("roman_numeral", L"\x2170", L"\x2160");

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
