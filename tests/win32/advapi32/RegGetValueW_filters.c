/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: RegGetValueW type restriction: every stored type against
 * every RRF_RT_* filter (single flags, RRF_RT_DWORD, RRF_RT_QWORD,
 * RRF_RT_ANY, a mixed mask), with the return code, reported type and size.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-reggetvaluew
 */
#include "oracle.h"

static HKEY root;

struct val { const wchar_t *name; DWORD type; DWORD size; };

int main(void)
{
    oracle_begin("advapi32/RegGetValueW_filters");
    root = oracle_reg_root();

    static const wchar_t text[] = L"abc";
    static const unsigned char raw[16] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
    static const struct val vals[] = {
        { L"None", REG_NONE, 4 },
        { L"None0", REG_NONE, 0 },
        { L"Sz", REG_SZ, 8 },
        { L"Exp", REG_EXPAND_SZ, 8 },
        { L"Bin4", REG_BINARY, 4 },
        { L"Bin8", REG_BINARY, 8 },
        { L"Bin3", REG_BINARY, 3 },
        { L"Dword", REG_DWORD, 4 },
        { L"DwordBE", REG_DWORD_BIG_ENDIAN, 4 },
        { L"Link", REG_LINK, 6 },
        { L"Multi", REG_MULTI_SZ, 8 },
        { L"Qword", REG_QWORD, 8 },
        { L"Res", REG_RESOURCE_LIST, 4 },
        { L"Undef", 12, 4 },
    };
    static const struct { const char *name; DWORD flags; } filters[] = {
        { "none", RRF_RT_REG_NONE },
        { "sz", RRF_RT_REG_SZ },
        { "expand_noexpand", RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND },
        { "binary", RRF_RT_REG_BINARY },
        { "reg_dword", RRF_RT_REG_DWORD },
        { "multi", RRF_RT_REG_MULTI_SZ },
        { "reg_qword", RRF_RT_REG_QWORD },
        { "dword", RRF_RT_DWORD },
        { "qword", RRF_RT_QWORD },
        { "any", RRF_RT_ANY },
        { "sz_or_multi", RRF_RT_REG_SZ | RRF_RT_REG_MULTI_SZ },
        { "unused_bits", 0x0000ff00 },
    };

    for (size_t i = 0; i < sizeof vals / sizeof vals[0]; i++)
        RegSetValueExW(root, vals[i].name, 0, vals[i].type,
                       vals[i].type == REG_SZ || vals[i].type == REG_EXPAND_SZ || vals[i].type == REG_LINK
                       || vals[i].type == REG_MULTI_SZ ? (const BYTE *)text : raw,
                       vals[i].size);

    for (size_t i = 0; i < sizeof vals / sizeof vals[0]; i++)
        for (size_t f = 0; f < sizeof filters / sizeof filters[0]; f++) {
            char cname[64];
            snprintf(cname, sizeof cname, "%s_as_%s", oracle_utf8(vals[i].name), filters[f].name);
            unsigned char b[32];
            DWORD t = 0xAAAAAAAAu, sz = sizeof b;
            LONG st;
            ORACLE_CALL(st, RegGetValueW(root, NULL, vals[i].name, filters[f].flags, &t, b, &sz));
            oracle_case(cname);
            o_status("ret", st);
            o_lasterr();
            o_u32("type", t);
            o_u32("size", sz);
            oracle_case_end();
        }

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
