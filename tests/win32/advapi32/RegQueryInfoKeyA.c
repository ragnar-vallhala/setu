/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: RegQueryInfoKeyA: class buffer handling in ANSI (sizes, too
 * small, NULL buffer), and whether the max subkey/class/value-name lengths
 * are reported in ANSI bytes or Unicode characters for names whose cp1252
 * form has a different length (none here differ, but unmappable letters do).
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regqueryinfokeya
 */
#include "oracle.h"

static HKEY root;

static void info_case(const char *name, HKEY k, DWORD class_cap, int class_buf)
{
    char cls[64];
    DWORD cn = class_cap, nsub = 0xAAAA, maxsub = 0xAAAA, maxcls = 0xAAAA, nval = 0xAAAA, maxval = 0xAAAA, maxdata = 0xAAAA;
    memset(cls, '#', 63); cls[63] = 0;
    LONG st;
    ORACLE_CALL(st, RegQueryInfoKeyA(k, class_buf ? cls : NULL, class_cap != 0xFFFFFFFFu ? &cn : NULL, NULL, &nsub,
                                     &maxsub, &maxcls, &nval, &maxval, &maxdata, NULL, NULL));
    oracle_case(name);
    o_status("ret", st);
    o_lasterr();
    if (class_cap != 0xFFFFFFFFu) o_u32("class_len", cn);
    if (class_buf) {
        cls[20] = 0;
        o_bytes("class_bytes", cls, strlen(cls));
    }
    o_u32("subkeys", nsub);
    o_u32("max_subkey_len", maxsub);
    o_u32("max_class_len", maxcls);
    o_u32("values", nval);
    o_u32("max_value_name_len", maxval);
    o_u32("max_value_data", maxdata);
    oracle_case_end();
}

int main(void)
{
    oracle_begin("advapi32/RegQueryInfoKeyA");
    root = oracle_reg_root();
    HKEY k, g;

    RegCreateKeyExA(root, "plain", 0, "Klasse", 0, KEY_ALL_ACCESS, NULL, &k, NULL);
    info_case("class_cap_64", k, 64, 1);
    info_case("class_cap_7_exact_plus_nul", k, 7, 1);
    info_case("class_cap_6_no_nul_room", k, 6, 1);
    info_case("class_cap_1", k, 1, 1);
    info_case("class_cap_0", k, 0, 1);
    info_case("class_null_buffer_cap_64", k, 64, 0);
    info_case("class_null_buffer_null_len", k, 0xFFFFFFFFu, 0);
    RegCloseKey(k);

    RegCreateKeyExW(root, L"g", 0, L"κλμν", 0, KEY_ALL_ACCESS, NULL, &g, NULL);
    RegCreateKeyExW(g, L"αβγδε", 0, L"χψω", 0, KEY_ALL_ACCESS, NULL, &k, NULL);
    RegCloseKey(k);
    RegCreateKeyExW(g, L"café", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL);
    RegCloseKey(k);
    RegSetValueExW(g, L"στυφχψ", 0, REG_SZ, (const BYTE *)L"ωω", 6);
    info_case("unicode_names_a", g, 64, 1);
    info_case("unicode_class_cap_4_no_nul_room", g, 4, 1);
    info_case("unicode_class_cap_5", g, 5, 1);
    {
        wchar_t wc[16];
        DWORD wn = 16, maxsub = 0, maxcls = 0, maxval = 0, maxdata = 0;
        RegQueryInfoKeyW(g, wc, &wn, NULL, NULL, &maxsub, &maxcls, NULL, &maxval, &maxdata, NULL, NULL);
        oracle_case("unicode_names_w");
        o_u32("class_len", wn);
        o_u32("max_subkey_len", maxsub);
        o_u32("max_class_len", maxcls);
        o_u32("max_value_name_len", maxval);
        o_u32("max_value_data", maxdata);
        oracle_case_end();
    }
    RegCloseKey(g);

    info_case("invalid_handle", (HKEY)(ULONG_PTR)0x1234, 64, 1);
    {
        HKEY q;
        RegOpenKeyExA(root, "plain", 0, KEY_ENUMERATE_SUB_KEYS, &q);
        info_case("enumerate_only_handle", q, 64, 1);
        RegCloseKey(q);
    }
    {
        DWORD res = 0;
        LONG st;
        ORACLE_CALL(st, RegQueryInfoKeyA(root, NULL, NULL, &res, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL));
        oracle_case("reserved_non_null");
        o_status("ret", st);
        o_lasterr();
        oracle_case_end();
    }

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
