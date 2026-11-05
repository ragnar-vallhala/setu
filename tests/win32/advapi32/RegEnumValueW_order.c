/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: RegEnumValueW enumeration order: values created in a
 * non-alphabetical order, the default value, names differing only in case,
 * an overwritten value (does it move?), a deleted and re-created value,
 * and indexes past the end.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regenumvaluew
 */
#include "oracle.h"

static HKEY root;

static void o_order(const char *k)
{
    char out[1024];
    size_t n = 0;
    out[0] = 0;
    for (DWORD i = 0; i < 64; i++) {
        wchar_t name[64];
        DWORD len = 64;
        LONG r = RegEnumValueW(root, i, name, &len, NULL, NULL, NULL, NULL);
        if (r) break;
        n += snprintf(out + n, sizeof out - n, "%s%s", i ? "|" : "", len ? oracle_utf8(name) : "(default)");
        if (n >= sizeof out) break;
    }
    o_str(k, out);
}

static void put(const wchar_t *name, DWORD v)
{
    RegSetValueExW(root, name, 0, REG_DWORD, (const BYTE *)&v, 4);
}

int main(void)
{
    oracle_begin("advapi32/RegEnumValueW_order");
    root = oracle_reg_root();

    static const wchar_t *names[] = { L"zeta", L"Alpha", L"mid", L"beta", L"Zed", L"_u", L"10", L"9", L"a b",
                                      L"\x00e9", L"e", L"F" };
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) put(names[i], (DWORD)i);
    oracle_case("creation_order");
    o_order("order");
    oracle_case_end();

    put(NULL, 100);
    oracle_case("default_added_last");
    o_order("order");
    oracle_case_end();

    put(L"mid", 200);
    oracle_case("overwrite_same_type");
    o_order("order");
    oracle_case_end();

    RegSetValueExW(root, L"beta", 0, REG_SZ, (const BYTE *)L"s", 4);
    oracle_case("overwrite_new_type");
    o_order("order");
    oracle_case_end();

    put(L"ALPHA", 300);
    oracle_case("overwrite_other_case");
    o_order("order");
    oracle_case_end();

    RegDeleteValueW(root, L"zeta");
    put(L"zeta", 400);
    oracle_case("delete_and_recreate");
    o_order("order");
    oracle_case_end();

    RegDeleteValueW(root, L"Alpha");
    oracle_case("delete_first");
    o_order("order");
    oracle_case_end();

    {
        DWORD n = 0;
        RegQueryInfoKeyW(root, NULL, NULL, NULL, NULL, NULL, NULL, &n, NULL, NULL, NULL, NULL);
        wchar_t name[64];
        DWORD len = 64;
        LONG st;
        ORACLE_CALL(st, RegEnumValueW(root, n, name, &len, NULL, NULL, NULL, NULL));
        oracle_case("index_equal_to_count");
        o_status("ret", st);
        o_lasterr();
        o_u32("len", len);
        oracle_case_end();
        len = 64;
        ORACLE_CALL(st, RegEnumValueW(root, 0xFFFFFFFFu, name, &len, NULL, NULL, NULL, NULL));
        oracle_case("index_max");
        o_status("ret", st);
        o_lasterr();
        o_u32("len", len);
        oracle_case_end();
    }
    {
        HKEY e = NULL;
        RegCreateKeyExW(root, L"empty", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &e, NULL);
        wchar_t name[8];
        DWORD len = 8;
        LONG st;
        ORACLE_CALL(st, RegEnumValueW(e, 0, name, &len, NULL, NULL, NULL, NULL));
        oracle_case("empty_key");
        o_status("ret", st);
        o_lasterr();
        o_u32("len", len);
        oracle_case_end();
        RegCloseKey(e);
    }

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
