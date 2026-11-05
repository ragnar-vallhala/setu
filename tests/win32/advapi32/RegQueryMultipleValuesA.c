/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: RegQueryMultipleValuesA: string values converted to ANSI
 * (lengths and bytes), non-string values unchanged, the total size
 * reported for a too-small and a NULL buffer, a missing value, and each
 * entry's offset into the buffer.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regquerymultiplevaluesa
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/ns-winreg-valenta
 */
#include "oracle.h"

static HKEY root;

static void m_case(const char *cname, const char **names, DWORD count, int with_buf, DWORD bufsize)
{
    VALENTA ve[8];
    static unsigned char buf[256];
    memset(ve, 0, sizeof ve);
    memset(buf, 0xCC, sizeof buf);
    for (DWORD i = 0; i < count; i++) {
        ve[i].ve_valuename = (char *)names[i];
        ve[i].ve_valuelen = 0xAAAAu;
        ve[i].ve_type = 0xAAAAu;
        ve[i].ve_valueptr = (DWORD_PTR)0xA5A5u;
    }
    DWORD tot = bufsize;
    LONG st;
    ORACLE_CALL(st, RegQueryMultipleValuesA(root, ve, count, with_buf ? (char *)buf : NULL, &tot));
    oracle_case(cname);
    o_status("ret", st);
    o_lasterr();
    o_u32("total", tot);
    for (DWORD i = 0; i < count; i++) {
        char k[32];
        snprintf(k, sizeof k, "e%u_type", (unsigned)i); o_u32(k, ve[i].ve_type);
        snprintf(k, sizeof k, "e%u_len", (unsigned)i); o_u32(k, ve[i].ve_valuelen);
        snprintf(k, sizeof k, "e%u_ptr", (unsigned)i);
        if (ve[i].ve_valueptr == (DWORD_PTR)0xA5A5u) o_str(k, "unchanged");
        else if (ve[i].ve_valueptr >= (DWORD_PTR)buf && ve[i].ve_valueptr < (DWORD_PTR)buf + sizeof buf)
            o_u32(k, (DWORD)(ve[i].ve_valueptr - (DWORD_PTR)buf));
        else if (!ve[i].ve_valueptr) o_str(k, "NULL");
        else o_str(k, "elsewhere");
    }
    if (with_buf) o_bytes("buffer", buf, bufsize + 2 < 64 ? bufsize + 2 : 64);
    oracle_case_end();
}

int main(void)
{
    oracle_begin("advapi32/RegQueryMultipleValuesA");
    root = oracle_reg_root();
    RegSetValueExW(root, L"Sz", 0, REG_SZ, (const BYTE *)L"abc", 8);
    RegSetValueExW(root, L"Multi", 0, REG_MULTI_SZ, (const BYTE *)L"a\0b\0", 10);
    RegSetValueExW(root, L"Dw", 0, REG_DWORD, (const BYTE *)"\1\2\3\4", 4);
    RegSetValueExW(root, L"Bin", 0, REG_BINARY, (const BYTE *)L"xy", 6);

    static const char *mix[] = { "Sz", "Dw", "Multi", "Bin" };
    static const char *with_missing[] = { "Sz", "Missing" };
    static const char *one[] = { "Sz" };

    m_case("mix_big_buffer", mix, 4, 1, 256);
    m_case("mix_null_buffer", mix, 4, 0, 0);
    m_case("mix_zero_buffer", mix, 4, 1, 0);
    m_case("mix_ansi_total", mix, 4, 1, 4 + 4 + 5 + 6);
    m_case("mix_one_short_of_ansi_total", mix, 4, 1, 4 + 4 + 5 + 6 - 1);
    m_case("one_sz_exact_ansi", one, 1, 1, 4);
    m_case("one_sz_exact_unicode", one, 1, 1, 8);
    m_case("one_sz_3", one, 1, 1, 3);
    m_case("missing", with_missing, 2, 1, 256);

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
