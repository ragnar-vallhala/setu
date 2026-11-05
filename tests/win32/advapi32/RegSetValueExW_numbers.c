/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: RegSetValueExW with REG_DWORD, REG_DWORD_BIG_ENDIAN and
 * REG_QWORD data of the right size and of wrong sizes (0, short, long).
 * Read back with RegQueryValueExW and RegGetValueW(RRF_RT_DWORD/QWORD).
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regsetvalueexw
 *       https://learn.microsoft.com/en-us/windows/win32/sysinfo/registry-value-types
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-reggetvaluew
 */
#include "oracle.h"

static HKEY root;

static void set_case(const char *cname, const wchar_t *value, DWORD type, DWORD size)
{
    static const unsigned char d[16] = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
                                         0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x00 };
    LONG st;
    ORACLE_CALL(st, RegSetValueExW(root, value, 0, type, d, size));
    oracle_case(cname);
    o_status("ret", st);
    o_lasterr();
    unsigned char b[32];
    memset(b, 0xCC, sizeof b);
    DWORD t = 0xAAAAAAAAu, sz = sizeof b;
    LONG q = RegQueryValueExW(root, value, NULL, &t, b, &sz);
    o_status("q_ret", q);
    if (!q) { o_u32("q_type", t); o_u32("q_size", sz); o_bytes("q_data", b, sz); }
    static const DWORD flags[] = { RRF_RT_DWORD, RRF_RT_QWORD, RRF_RT_REG_DWORD, RRF_RT_REG_QWORD, RRF_RT_ANY };
    static const char *fn[] = { "g_dword", "g_qword", "g_reg_dword", "g_reg_qword", "g_any" };
    for (int i = 0; i < 5; i++) {
        char k[48];
        memset(b, 0xCC, sizeof b);
        DWORD gs = sizeof b, gt = 0xAAAAAAAAu;
        LONG g = RegGetValueW(root, NULL, value, flags[i], &gt, b, &gs);
        snprintf(k, sizeof k, "%s_ret", fn[i]); o_status(k, g);
        snprintf(k, sizeof k, "%s_size", fn[i]); o_u32(k, gs);
        if (!g) { snprintf(k, sizeof k, "%s_data", fn[i]); o_bytes(k, b, gs); }
    }
    oracle_case_end();
}

int main(void)
{
    oracle_begin("advapi32/RegSetValueExW_numbers");
    root = oracle_reg_root();

    set_case("dword_4", L"D4", REG_DWORD, 4);
    set_case("dword_0", L"D0", REG_DWORD, 0);
    set_case("dword_1", L"D1", REG_DWORD, 1);
    set_case("dword_3", L"D3", REG_DWORD, 3);
    set_case("dword_5", L"D5", REG_DWORD, 5);
    set_case("dword_8", L"D8", REG_DWORD, 8);
    set_case("dword_be_4", L"B4", REG_DWORD_BIG_ENDIAN, 4);
    set_case("dword_be_2", L"B2", REG_DWORD_BIG_ENDIAN, 2);
    set_case("dword_be_8", L"B8", REG_DWORD_BIG_ENDIAN, 8);
    set_case("qword_8", L"Q8", REG_QWORD, 8);
    set_case("qword_0", L"Q0", REG_QWORD, 0);
    set_case("qword_4", L"Q4", REG_QWORD, 4);
    set_case("qword_7", L"Q7", REG_QWORD, 7);
    set_case("qword_9", L"Q9", REG_QWORD, 9);
    set_case("qword_16", L"Q16", REG_QWORD, 16);
    set_case("binary_4", L"Bin4", REG_BINARY, 4);
    set_case("binary_8", L"Bin8", REG_BINARY, 8);
    set_case("binary_5", L"Bin5", REG_BINARY, 5);
    set_case("none_4", L"None4", REG_NONE, 4);

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
