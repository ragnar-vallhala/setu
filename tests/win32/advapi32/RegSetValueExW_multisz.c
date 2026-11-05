/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: RegSetValueExW with REG_MULTI_SZ data: properly terminated
 * lists, lists missing one or both final NULs, empty strings inside the
 * list, odd byte counts. Read back with RegQueryValueExW (large and exact
 * buffers) and with RegGetValueW(RRF_RT_REG_MULTI_SZ).
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regsetvalueexw
 *       https://learn.microsoft.com/en-us/windows/win32/sysinfo/registry-value-types
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-reggetvaluew
 */
#include "oracle.h"

static HKEY root;

static void set_case(const char *cname, const wchar_t *value, const void *data, DWORD size)
{
    LONG st;
    ORACLE_CALL(st, RegSetValueExW(root, value, 0, REG_MULTI_SZ, data, size));
    oracle_case(cname);
    o_status("ret", st);
    o_lasterr();
    unsigned char b[256];
    memset(b, 0xCC, sizeof b);
    DWORD t = 0xAAAAAAAAu, sz = sizeof b;
    LONG q = RegQueryValueExW(root, value, NULL, &t, b, &sz);
    o_status("q_ret", q);
    if (!q) {
        o_u32("q_type", t);
        o_u32("q_size", sz);
        o_bytes("q_data", b, sz + 4);
        DWORD exact = sz;
        memset(b, 0xCC, sizeof b);
        LONG q2 = RegQueryValueExW(root, value, NULL, NULL, b, &exact);
        o_status("exact_ret", q2);
        o_u32("exact_size", exact);
        o_bytes("exact_data", b, sz + 4);
    }
    memset(b, 0xCC, sizeof b);
    DWORD gs = sizeof b, gt = 0xAAAAAAAAu;
    LONG g = RegGetValueW(root, NULL, value, RRF_RT_REG_MULTI_SZ, &gt, b, &gs);
    o_status("g_ret", g);
    o_u32("g_size", gs);
    if (!g) o_bytes("g_data", b, gs + 4);
    /* Size-only RegGetValueW query. */
    DWORD ns = 0;
    LONG gn = RegGetValueW(root, NULL, value, RRF_RT_REG_MULTI_SZ, NULL, NULL, &ns);
    o_status("g_sizeonly_ret", gn);
    o_u32("g_sizeonly_size", ns);
    oracle_case_end();
}

int main(void)
{
    oracle_begin("advapi32/RegSetValueExW_multisz");
    root = oracle_reg_root();

    static const wchar_t two[] = L"one\0two\0";      /* 20 bytes incl. both NULs */
    static const wchar_t inner_empty[] = L"a\0\0b\0"; /* empty string inside */
    static const wchar_t just_nul[] = L"";

    set_case("two_strings_terminated", L"Two", two, sizeof two);
    set_case("missing_final_nul", L"NoFinal", two, sizeof two - 2);
    set_case("missing_both_nuls", L"NoBoth", two, sizeof two - 4);
    set_case("inner_empty_string", L"InnerEmpty", inner_empty, sizeof inner_empty);
    set_case("single_nul", L"SingleNul", just_nul, 2);
    set_case("double_nul", L"DoubleNul", L"\0", 4);
    set_case("empty", L"Empty", two, 0);
    set_case("odd_size_7", L"Odd7", two, 7);
    set_case("odd_size_19", L"Odd19", two, 19);
    set_case("single_string_no_nul", L"NoNul", two, 6);
    set_case("three_trailing_nuls", L"Three", L"x\0\0", 8);

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
