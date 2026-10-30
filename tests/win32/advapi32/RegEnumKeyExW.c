/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: RegEnumKeyExW / RegEnumValueW ordering, name-buffer sizing,
 * ERROR_NO_MORE_ITEMS, and RegQueryInfoKeyW counts.
 *
 * Keys and values are created in a deliberately scrambled order, so the
 * enumeration order Windows uses is captured.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regenumkeyexw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regenumvaluew
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regqueryinfokeyw
 */
#include "oracle.h"

int main(void)
{
    oracle_begin("advapi32/RegEnumKeyExW");
    HKEY root = oracle_reg_root();

    static const wchar_t *keys[] = {L"zulu", L"Alpha", L"_x", L"bravo", L"10", L"9", L"Charlie", L"alpha2"};
    for (size_t i = 0; i < sizeof keys / sizeof *keys; i++) {
        HKEY k;
        RegCreateKeyExW(root, keys[i], 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL);
        RegCloseKey(k);
    }
    static const wchar_t *vals[] = {L"Zed", L"ant", L"Bee", L"", L"10", L"9"};
    for (size_t i = 0; i < sizeof vals / sizeof *vals; i++) {
        DWORD v = (DWORD)i;
        RegSetValueExW(root, vals[i], 0, REG_DWORD, (const BYTE *)&v, sizeof v);
    }

    {
        DWORD nsub = 0, maxsub = 0, nval = 0, maxval = 0, maxdata = 0;
        LONG st;
        ORACLE_CALL(st, RegQueryInfoKeyW(root, NULL, NULL, NULL, &nsub, &maxsub, NULL, &nval, &maxval, &maxdata,
                                         NULL, NULL));
        oracle_case("query_info");
        o_status("ret", st);
        o_u32("subkeys", nsub);
        o_u32("max_subkey_len", maxsub);
        o_u32("values", nval);
        o_u32("max_value_name_len", maxval);
        o_u32("max_value_data", maxdata);
        oracle_case_end();
    }
    {
        char order[1024] = "";
        LONG st;
        DWORD i = 0;
        for (;; i++) {
            wchar_t name[64];
            DWORD n = 64;
            ORACLE_CALL(st, RegEnumKeyExW(root, i, name, &n, NULL, NULL, NULL, NULL));
            if (st) break;
            if (i) strcat(order, ",");
            strcat(order, oracle_utf8(name));
        }
        oracle_case("enum_keys_order");
        o_str("order", order);
        o_u32("count", i);
        o_status("final_ret", st);
        oracle_case_end();
    }
    {
        char order[1024] = "";
        LONG st;
        DWORD i = 0;
        for (;; i++) {
            wchar_t name[64];
            DWORD n = 64;
            ORACLE_CALL(st, RegEnumValueW(root, i, name, &n, NULL, NULL, NULL, NULL));
            if (st) break;
            if (i) strcat(order, ",");
            strcat(order, n ? oracle_utf8(name) : "(default)");
        }
        oracle_case("enum_values_order");
        o_str("order", order);
        o_u32("count", i);
        o_status("final_ret", st);
        oracle_case_end();
    }
    {   /* Name buffer too small for "Charlie" (needs 7 chars + NUL). */
        DWORD idx;
        wchar_t name[64];
        for (idx = 0;; idx++) {
            DWORD n = 64;
            if (RegEnumKeyExW(root, idx, name, &n, NULL, NULL, NULL, NULL)) break;
            if (!wcscmp(name, L"Charlie")) break;
        }
        DWORD n = 7;
        LONG st;
        ORACLE_CALL(st, RegEnumKeyExW(root, idx, name, &n, NULL, NULL, NULL, NULL));
        oracle_case("enum_key_name_buffer_exact_len_no_nul_room");
        o_status("ret", st);
        o_lasterr();
        o_u32("len_after", n);
        oracle_case_end();
    }
    {
        wchar_t name[64];
        DWORD n = 64;
        LONG st;
        ORACLE_CALL(st, RegEnumKeyExW(root, 1000, name, &n, NULL, NULL, NULL, NULL));
        oracle_case("enum_key_index_out_of_range");
        o_status("ret", st);
        o_lasterr();
        oracle_case_end();
    }

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
