/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: RegSetKeyValueA: string conversion of REG_SZ, REG_EXPAND_SZ
 * and REG_MULTI_SZ data (stored bytes read raw with RegQueryValueExW),
 * non-string types, missing subkeys, the default value and NULL subkeys.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regsetkeyvaluea
 */
#include "oracle.h"

static HKEY root;

static void k_case(const char *cname, const char *sub, const char *value, DWORD type, const void *data,
                   DWORD size)
{
    LONG st;
    ORACLE_CALL(st, RegSetKeyValueA(root, sub, value, type, data, size));
    oracle_case(cname);
    o_status("ret", st);
    o_lasterr();
    HKEY k = NULL;
    LONG o = RegOpenKeyExA(root, sub, 0, KEY_READ, &k);
    o_status("subkey_open", o);
    if (!o) {
        wchar_t wv[32];
        if (value) MultiByteToWideChar(CP_ACP, 0, value, -1, wv, 32);
        unsigned char b[64];
        DWORD t = 0, sz = sizeof b;
        LONG q = RegQueryValueExW(k, value ? wv : NULL, NULL, &t, b, &sz);
        o_status("qw_ret", q);
        if (!q) { o_u32("qw_type", t); o_u32("qw_size", sz); o_bytes("qw_data", b, sz); }
        RegCloseKey(k);
    }
    oracle_case_end();
}

int main(void)
{
    oracle_begin("advapi32/RegSetKeyValueA");
    root = oracle_reg_root();
    DWORD dw = 0x1234;

    k_case("sz", NULL, "Sz", REG_SZ, "hello", 6);
    k_case("sz_no_nul", NULL, "SzNoNul", REG_SZ, "hello", 5);
    k_case("expand", NULL, "Exp", REG_EXPAND_SZ, "%X%", 4);
    k_case("multi", NULL, "Multi", REG_MULTI_SZ, "a\0bc\0", 6);
    k_case("binary_unconverted", NULL, "Bin", REG_BINARY, "hello", 6);
    k_case("dword", NULL, "Dw", REG_DWORD, &dw, 4);
    k_case("missing_subkey", "newsub", "V", REG_SZ, "x", 2);
    k_case("nested_missing", "a\\b", "V", REG_DWORD, &dw, 4);
    k_case("default_null", "newsub", NULL, REG_SZ, "def", 4);
    k_case("default_empty", "newsub", "", REG_SZ, "def2", 5);
    k_case("empty_subkey", "", "E", REG_DWORD, &dw, 4);
    k_case("high_ansi", NULL, "Hi", REG_SZ, "caf\xe9", 5);
    k_case("sz_zero_size", NULL, "Z", REG_SZ, "abc", 0);

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
