/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: the 16-bit-compatible RegSetValueW, which sets the default
 * value of a subkey (creating it): only REG_SZ is accepted; what the cbData
 * argument means (ignored? used?); NULL and "" subkeys; nested and missing
 * subkeys; NULL data; overwriting a non-string default; read-only parent.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regsetvaluew
 */
#include "oracle.h"

#define BASE ORACLE_REG_BASE L"\\advapi32_RegSetValueW"

static HKEY root;

static void s_case(const char *cname, HKEY key, const wchar_t *sub, DWORD type, const wchar_t *data, DWORD cb)
{
    LONG st;
    ORACLE_CALL(st, RegSetValueW(key, sub, type, data, cb));
    oracle_case(cname);
    o_status("ret", st);
    o_lasterr();
    HKEY k = NULL;
    LONG o = RegOpenKeyExW(root, sub, 0, KEY_READ, &k);
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
    oracle_begin("advapi32/RegSetValueW");
    root = oracle_reg_root();
    static const wchar_t padded[64] = L"hi"; /* cb_too_big must not read past the array */

    s_case("sz_creates_subkey", root, L"a", REG_SZ, L"hello", 12);
    s_case("cb_zero", root, L"b", REG_SZ, L"hello", 0);
    s_case("cb_too_small", root, L"c", REG_SZ, L"hello", 2);
    s_case("cb_too_big", root, L"d", REG_SZ, padded, 100);
    s_case("cb_odd", root, L"e", REG_SZ, L"hello", 5);
    s_case("expand_sz_rejected", root, L"f", REG_EXPAND_SZ, L"x", 4);
    s_case("dword_rejected", root, L"g", REG_DWORD, L"x", 4);
    s_case("binary_rejected", root, L"g2", REG_BINARY, L"x", 4);
    s_case("multi_sz_rejected", root, L"g3", REG_MULTI_SZ, L"x", 4);
    s_case("null_subkey", root, NULL, REG_SZ, L"own", 8);
    s_case("empty_subkey", root, L"", REG_SZ, L"own2", 10);
    s_case("nested_missing", root, L"n1\\n2", REG_SZ, L"deep", 10);
    s_case("null_data", root, L"h", REG_SZ, NULL, 0);
    s_case("empty_string", root, L"i", REG_SZ, L"", 2);
    {
        HKEY k = NULL;
        RegCreateKeyExW(root, L"j", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL);
        RegSetValueExW(k, NULL, 0, REG_DWORD, (const BYTE *)"\1\0\0\0", 4);
        RegCloseKey(k);
    }
    s_case("overwrite_dword_default", root, L"j", REG_SZ, L"now sz", 14);
    s_case("existing_subkey_other_case", root, L"A", REG_SZ, L"upper", 12);
    {
        HKEY ro = NULL;
        RegOpenKeyExW(HKEY_CURRENT_USER, BASE, 0, KEY_READ, &ro);
        s_case("readonly_parent_existing", ro, L"a", REG_SZ, L"ro", 6);
        s_case("readonly_parent_new", ro, L"k", REG_SZ, L"ro", 6);
        RegCloseKey(ro);
    }
    s_case("invalid_handle", (HKEY)(ULONG_PTR)0x1234, L"z", REG_SZ, L"x", 4);

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
