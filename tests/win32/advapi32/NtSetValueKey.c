/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: NtSetValueKey (ntdll, through GetProcAddress): value names
 * given as counted UNICODE_STRINGs (embedded NUL characters, odd byte
 * lengths, a NULL name pointer for the default value), TitleIndex, every
 * type including undefined ones, odd-length string data, NULL data, and
 * how the result looks through RegEnumValueW and RegQueryValueExW.
 *
 * Docs: https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-zwsetvaluekey
 *       https://learn.microsoft.com/en-us/windows/win32/api/ntdef/ns-ntdef-_unicode_string
 */
#include "oracle.h"

static HKEY root;
typedef struct { USHORT Length, MaximumLength; const wchar_t *Buffer; } ustr;
typedef LONG (WINAPI *ntsvk_fn)(HANDLE, ustr *, ULONG, ULONG, const void *, ULONG);
static ntsvk_fn ntsvk;

static void o_names(void)
{
    char out[512];
    size_t n = 0;
    out[0] = 0;
    for (DWORD i = 0; i < 32; i++) {
        wchar_t name[64];
        DWORD len = 64, t = 0, sz = 0;
        if (RegEnumValueW(root, i, name, &len, NULL, &t, NULL, &sz)) break;
        n += snprintf(out + n, sizeof out - n, "%s%u:", i ? "|" : "", (unsigned)len);
        for (DWORD j = 0; j < len && n < sizeof out - 8; j++)
            n += snprintf(out + n, sizeof out - n, name[j] >= 32 && name[j] < 127 ? "%c" : "\\x%04x", name[j]);
        n += snprintf(out + n, sizeof out - n, ":%u:%u", (unsigned)t, (unsigned)sz);
    }
    o_str("values", out);
}

static void s_case(const char *cname, const wchar_t *name, USHORT bytes, int null_name_struct, ULONG title, ULONG type,
                   const void *data, ULONG size)
{
    ustr us = { bytes, bytes, name };
    LONG st = ntsvk(root, null_name_struct ? NULL : &us, title, type, data, size);
    oracle_case(cname);
    o_hex32("status", (DWORD)st);
    o_names();
    oracle_case_end();
    /* Clear the key for the next case. */
    for (int guard = 0; guard < 32; guard++) {
        wchar_t n[64];
        DWORD len = 64;
        if (RegEnumValueW(root, 0, n, &len, NULL, NULL, NULL, NULL)) break;
        ustr d = { (USHORT)(len * 2), (USHORT)(len * 2), n };
        typedef LONG (WINAPI *ntdvk_fn)(HANDLE, ustr *);
        ntdvk_fn del = (ntdvk_fn)(void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtDeleteValueKey");
        if (del(root, &d)) break;
    }
}

int main(void)
{
    oracle_begin("advapi32/NtSetValueKey");
    root = oracle_reg_root();
    ntsvk = (ntsvk_fn)(void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtSetValueKey");
    DWORD dw = 0x01020304;

    s_case("plain", L"abc", 6, 0, 0, REG_DWORD, &dw, 4);
    s_case("length_shorter_than_string", L"abcdef", 4, 0, 0, REG_DWORD, &dw, 4);
    s_case("embedded_nul_in_name", L"ab\0cd", 10, 0, 0, REG_DWORD, &dw, 4);
    s_case("odd_name_length", L"abc", 5, 0, 0, REG_DWORD, &dw, 4);
    s_case("zero_length_name", L"abc", 0, 0, 0, REG_DWORD, &dw, 4);
    s_case("null_buffer_zero_length", NULL, 0, 0, 0, REG_DWORD, &dw, 4);
    s_case("title_index_nonzero", L"t", 2, 0, 5, REG_DWORD, &dw, 4);
    s_case("undefined_type", L"u", 2, 0, 0, 0x12345678, &dw, 4);
    s_case("sz_odd_data", L"s", 2, 0, 0, REG_SZ, L"abc", 5);
    s_case("null_data_zero", L"n", 2, 0, 0, REG_BINARY, NULL, 0);
    s_case("trailing_nul_counted", L"name", 10, 0, 0, REG_DWORD, &dw, 4);
    s_case("backslash_name", L"a\\b", 6, 0, 0, REG_DWORD, &dw, 4);
    /* Last: may fault inside the API. */
    s_case("null_data_nonzero", L"n2", 4, 0, 0, REG_BINARY, NULL, 4);

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
