/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: the 16-bit-compatible RegSetValueA: sets a subkey's default
 * value from ANSI text (stored bytes read back with RegQueryValueExW);
 * how cbData is used; other types rejected; NULL/"" subkeys; NULL data.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regsetvaluea
 */
#include "oracle.h"

static HKEY root;

static void s_case(const char *cname, const char *sub, DWORD type, const char *data, DWORD cb)
{
    LONG st;
    ORACLE_CALL(st, RegSetValueA(root, sub, type, data, cb));
    oracle_case(cname);
    o_status("ret", st);
    o_lasterr();
    HKEY k = NULL;
    LONG o = RegOpenKeyExA(root, sub, 0, KEY_READ, &k);
    o_status("subkey_open", o);
    if (!o) {
        unsigned char b[64];
        DWORD t = 0, sz = sizeof b;
        LONG q = RegQueryValueExW(k, NULL, NULL, &t, b, &sz);
        o_status("q_ret", q);
        if (!q) { o_u32("q_type", t); o_u32("q_size", sz); o_bytes("q_data", b, sz); }
        RegCloseKey(k);
    }
    oracle_case_end();
}

int main(void)
{
    oracle_begin("advapi32/RegSetValueA");
    root = oracle_reg_root();
    static const char padded[128] = "hi"; /* cb_too_big must not read past the array */

    s_case("sz", "a", REG_SZ, "hello", 6);
    s_case("cb_zero", "b", REG_SZ, "hello", 0);
    s_case("cb_too_small", "c", REG_SZ, "hello", 2);
    s_case("cb_too_big", "d", REG_SZ, padded, 100);
    s_case("expand_sz_rejected", "e", REG_EXPAND_SZ, "x", 2);
    s_case("dword_rejected", "f", REG_DWORD, "xxxx", 4);
    s_case("null_subkey", NULL, REG_SZ, "own", 4);
    s_case("empty_subkey", "", REG_SZ, "own2", 5);
    s_case("null_data", "g", REG_SZ, NULL, 0);
    s_case("high_ansi", "h", REG_SZ, "caf\xe9", 5);
    s_case("nested", "n1\\n2", REG_SZ, "deep", 5);

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
