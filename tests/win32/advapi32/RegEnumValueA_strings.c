/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: RegEnumValueA on Unicode values: names and string data
 * converted to ANSI, data sizes reported for string and non-string types,
 * size-only queries, odd-length and unterminated strings, and a high-ANSI
 * character.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regenumvaluea
 */
#include "oracle.h"

static HKEY root;

int main(void)
{
    oracle_begin("advapi32/RegEnumValueA_strings");
    root = oracle_reg_root();

    static const struct { const wchar_t *name; DWORD type; const void *data; DWORD size; } v[] = {
        { L"Sz", REG_SZ, L"hello", 12 },
        { L"SzNoNul", REG_SZ, L"hello", 10 },
        { L"SzOdd", REG_SZ, L"hello", 7 },
        { L"Exp", REG_EXPAND_SZ, L"%X%", 8 },
        { L"Multi", REG_MULTI_SZ, L"ab\0cd\0", 14 },
        { L"Bin", REG_BINARY, L"hello", 12 },
        { L"Dw", REG_DWORD, "\1\2\3\4", 4 },
        { L"Link", REG_LINK, L"lnk", 6 },
        { L"caf\x00e9", REG_SZ, L"\x00e9t\x00e9", 8 },
        { L"Empty", REG_SZ, NULL, 0 },
    };
    const DWORD nv = sizeof v / sizeof v[0];
    for (DWORD i = 0; i < nv; i++) RegSetValueExW(root, v[i].name, 0, v[i].type, v[i].data, v[i].size);

    for (DWORD i = 0; i < nv; i++) {
        char cname[32], name[32];
        unsigned char data[64];
        snprintf(cname, sizeof cname, "index_%u", (unsigned)i);
        oracle_case(cname);
        DWORD nl = 32, dl = 0, t = 0;
        LONG st;
        ORACLE_CALL(st, RegEnumValueA(root, i, name, &nl, NULL, &t, NULL, &dl));
        o_status("sizeonly_ret", st);
        o_lasterr();
        o_u32("sizeonly_name_len", nl);
        o_u32("sizeonly_data_len", dl);
        memset(data, 0xCC, sizeof data);
        nl = 32;
        dl = sizeof data;
        st = RegEnumValueA(root, i, name, &nl, NULL, &t, data, &dl);
        o_status("ret", st);
        o_u32("name_len", nl);
        o_bytes("name", name, nl);
        o_u32("type", t);
        o_u32("data_len", dl);
        if (!st) o_bytes("data", data, dl + 2);
        oracle_case_end();
    }

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
