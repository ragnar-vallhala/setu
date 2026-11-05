/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: RegSetValueExW with large data (2 KB to 8 MB of REG_BINARY,
 * a 1 MB REG_SZ), read back with RegQueryValueExW and compared by size and
 * checksum. Also the size reported by RegQueryInfoKeyW (max data length).
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regsetvalueexw
 *       https://learn.microsoft.com/en-us/windows/win32/sysinfo/registry-element-size-limits
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regqueryinfokeyw
 */
#include "oracle.h"

static HKEY root;

static DWORD fnv(const unsigned char *p, DWORD n)
{
    DWORD h = 2166136261u;
    for (DWORD i = 0; i < n; i++) { h ^= p[i]; h *= 16777619u; }
    return h;
}

static void big_case(const char *cname, const wchar_t *value, DWORD type, DWORD size)
{
    unsigned char *d = malloc(size + 16), *b = malloc(size + 16);
    for (DWORD i = 0; i < size; i++) d[i] = (unsigned char)(i * 7 + (i >> 8));
    if (type == REG_SZ) {
        for (DWORD i = 0; i + 1 < size; i += 2) { d[i] = (unsigned char)('a' + (i / 2) % 26); d[i + 1] = 0; }
        if (size >= 2) d[size - 2] = d[size - 1] = 0;
    }
    LONG st;
    ORACLE_CALL(st, RegSetValueExW(root, value, 0, type, d, size));
    oracle_case(cname);
    o_status("ret", st);
    o_lasterr();
    DWORD need = 0, t = 0;
    LONG q0 = RegQueryValueExW(root, value, NULL, &t, NULL, &need);
    o_status("sizeonly_ret", q0);
    o_u32("sizeonly_size", need);
    if (!q0) {
        DWORD sz = size + 16;
        memset(b, 0xCC, size + 16);
        LONG q = RegQueryValueExW(root, value, NULL, &t, b, &sz);
        o_status("q_ret", q);
        o_u32("q_type", t);
        o_u32("q_size", sz);
        o_bool("q_same", sz == size && !memcmp(b, d, size));
        o_hex32("q_fnv", fnv(b, sz));
        DWORD small = size - 1;
        memset(b, 0xCC, size + 16);
        LONG qs = RegQueryValueExW(root, value, NULL, NULL, b, &small);
        o_status("short_ret", qs);
        o_u32("short_size", small);
        o_hex32("short_fnv", fnv(b, size - 1));
    }
    DWORD maxdata = 0;
    RegQueryInfoKeyW(root, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &maxdata, NULL, NULL);
    o_u32("info_max_data", maxdata);
    if (!st) RegDeleteValueW(root, value);
    oracle_case_end();
    free(d);
    free(b);
}

int main(void)
{
    oracle_begin("advapi32/RegSetValueExW_large");
    root = oracle_reg_root();

    big_case("binary_2048", L"B2k", REG_BINARY, 2048);
    big_case("binary_2049", L"B2k1", REG_BINARY, 2049);
    big_case("binary_16k", L"B16k", REG_BINARY, 16384);
    big_case("binary_16k_plus_1", L"B16k1", REG_BINARY, 16385);
    big_case("binary_64k", L"B64k", REG_BINARY, 65536);
    big_case("binary_1m", L"B1m", REG_BINARY, 1 << 20);
    big_case("binary_8m", L"B8m", REG_BINARY, 8 << 20);
    big_case("sz_1m", L"S1m", REG_SZ, 1 << 20);
    big_case("sz_64k_plus_2", L"S64k", REG_SZ, 65538);

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
