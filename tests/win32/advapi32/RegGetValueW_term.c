/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: RegGetValueW's documented termination guarantee for string
 * types: REG_SZ / REG_EXPAND_SZ stored without a NUL, REG_MULTI_SZ stored
 * without one or both final NULs, odd byte counts, empty values. For each:
 * size-only query, a large buffer, a buffer of exactly the stored size and
 * one of the size the size-only query reported.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-reggetvaluew
 */
#include "oracle.h"

static HKEY root;

static void t_case(const char *cname, DWORD type, const void *data, DWORD size)
{
    RegSetValueExW(root, L"V", 0, type, data, size);
    DWORD flags = type == REG_MULTI_SZ ? RRF_RT_REG_MULTI_SZ
                : type == REG_EXPAND_SZ ? RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND : RRF_RT_REG_SZ;
    oracle_case(cname);
    DWORD need = 0;
    LONG st;
    ORACLE_CALL(st, RegGetValueW(root, NULL, L"V", flags, NULL, NULL, &need));
    o_status("sizeonly_ret", st);
    o_lasterr();
    o_u32("sizeonly_size", need);
    unsigned char b[64];
    const DWORD sizes[3] = { sizeof b, size, need };
    const char *names[3] = { "big", "stored", "reported" };
    for (int i = 0; i < 3; i++) {
        char k[32];
        memset(b, 0xCC, sizeof b);
        DWORD sz = sizes[i];
        LONG r = RegGetValueW(root, NULL, L"V", flags, NULL, b, &sz);
        snprintf(k, sizeof k, "%s_ret", names[i]); o_status(k, r);
        snprintf(k, sizeof k, "%s_size", names[i]); o_u32(k, sz);
        snprintf(k, sizeof k, "%s_buffer", names[i]);
        o_bytes(k, b, sizes[i] + 4 < sizeof b ? sizes[i] + 4 : sizeof b);
    }
    RegDeleteValueW(root, L"V");
    oracle_case_end();
}

int main(void)
{
    oracle_begin("advapi32/RegGetValueW_term");
    root = oracle_reg_root();

    static const wchar_t abc[] = L"abc";
    static const wchar_t m[] = L"ab\0c\0";

    t_case("sz_terminated", REG_SZ, abc, 8);
    t_case("sz_no_nul", REG_SZ, abc, 6);
    t_case("sz_odd_7", REG_SZ, abc, 7);
    t_case("sz_odd_5", REG_SZ, abc, 5);
    t_case("sz_one_byte", REG_SZ, abc, 1);
    t_case("sz_empty", REG_SZ, NULL, 0);
    t_case("sz_two_nuls", REG_SZ, L"abc\0", 10);
    t_case("expand_no_nul", REG_EXPAND_SZ, abc, 6);
    t_case("expand_odd_5", REG_EXPAND_SZ, abc, 5);
    t_case("expand_empty", REG_EXPAND_SZ, NULL, 0);
    t_case("multi_terminated", REG_MULTI_SZ, m, 12);
    t_case("multi_missing_final", REG_MULTI_SZ, m, 10);
    t_case("multi_missing_both", REG_MULTI_SZ, m, 8);
    t_case("multi_odd_11", REG_MULTI_SZ, m, 11);
    t_case("multi_odd_9", REG_MULTI_SZ, m, 9);
    t_case("multi_single_nul", REG_MULTI_SZ, m, 0);
    t_case("multi_one_nul_only", REG_MULTI_SZ, L"", 2);

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
