/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: RegQueryMultipleValuesW: several values of different types
 * into one buffer (each entry's type, length and offset into the buffer),
 * a buffer that is too small (ERROR_MORE_DATA and the total size), a NULL
 * buffer, a missing value among existing ones, the default value, zero
 * entries, and a handle without KEY_QUERY_VALUE.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regquerymultiplevaluesw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/ns-winreg-valentw
 */
#include "oracle.h"

#define BASE ORACLE_REG_BASE L"\\advapi32_RegQueryMultipleValuesW"

static HKEY root;

static void m_case(const char *cname, HKEY key, const wchar_t **names, DWORD count, int with_buf, DWORD bufsize)
{
    VALENTW ve[8];
    static unsigned char buf[256];
    memset(ve, 0, sizeof ve);
    memset(buf, 0xCC, sizeof buf);
    for (DWORD i = 0; i < count; i++) {
        ve[i].ve_valuename = (wchar_t *)names[i];
        ve[i].ve_valuelen = 0xAAAAu;
        ve[i].ve_type = 0xAAAAu;
        ve[i].ve_valueptr = (DWORD_PTR)0xA5A5u;
    }
    DWORD tot = bufsize;
    LONG st;
    ORACLE_CALL(st, RegQueryMultipleValuesW(key, ve, count, with_buf ? (wchar_t *)buf : NULL, &tot));
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
    oracle_begin("advapi32/RegQueryMultipleValuesW");
    root = oracle_reg_root();
    RegSetValueExW(root, L"Sz", 0, REG_SZ, (const BYTE *)L"abc", 8);
    RegSetValueExW(root, L"Dw", 0, REG_DWORD, (const BYTE *)"\1\2\3\4", 4);
    RegSetValueExW(root, L"Bin", 0, REG_BINARY, (const BYTE *)"\5\6\7", 3);
    RegSetValueExW(root, L"Empty", 0, REG_BINARY, NULL, 0);
    RegSetValueExW(root, NULL, 0, REG_SZ, (const BYTE *)L"d", 4);

    static const wchar_t *three[] = { L"Sz", L"Dw", L"Bin" };
    static const wchar_t *with_missing[] = { L"Sz", L"Missing", L"Dw" };
    static const wchar_t *with_default[] = { L"", L"Dw" };
    static const wchar_t *with_empty[] = { L"Empty", L"Bin" };
    static const wchar_t *odd_first[] = { L"Bin", L"Sz" };
    static const wchar_t *dup[] = { L"Dw", L"dw" };

    m_case("three_big_buffer", root, three, 3, 1, 256);
    m_case("three_exact", root, three, 3, 1, 15);
    m_case("three_one_short", root, three, 3, 1, 14);
    m_case("three_zero_buffer", root, three, 3, 1, 0);
    m_case("three_null_buffer", root, three, 3, 0, 0);
    m_case("three_null_buffer_nonzero_size", root, three, 3, 0, 100);
    m_case("missing_value", root, with_missing, 3, 1, 256);
    m_case("default_value_empty_name", root, with_default, 2, 1, 256);
    m_case("empty_value", root, with_empty, 2, 1, 256);
    m_case("odd_length_first_alignment", root, odd_first, 2, 1, 256);
    m_case("same_value_twice_other_case", root, dup, 2, 1, 256);
    m_case("zero_entries", root, three, 0, 1, 256);
    {
        HKEY k = NULL;
        RegOpenKeyExW(HKEY_CURRENT_USER, BASE, 0, KEY_SET_VALUE, &k);
        m_case("set_value_only_handle", k, three, 3, 1, 256);
        RegCloseKey(k);
    }
    m_case("invalid_handle", (HKEY)(ULONG_PTR)0x1234, three, 3, 1, 256);

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
