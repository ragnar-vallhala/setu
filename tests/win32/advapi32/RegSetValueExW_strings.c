/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: RegSetValueExW string data (REG_SZ, REG_EXPAND_SZ) with
 * missing, extra and embedded NUL terminators, odd byte counts and zero
 * lengths. Each value is read back raw (NtQueryValueKey) to see what was
 * stored, and with RegQueryValueExW into a large and an exact buffer.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regsetvalueexw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regqueryvalueexw
 *       https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-zwqueryvaluekey
 */
#include "oracle.h"

static HKEY root;

typedef struct { ULONG TitleIndex, Type, DataLength; UCHAR Data[1]; } kvpi;
typedef LONG (WINAPI *ntqvk_fn)(HANDLE, void *, int, void *, ULONG, ULONG *);

static void raw(const wchar_t *value)
{
    ntqvk_fn q = (ntqvk_fn)(void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryValueKey");
    struct { USHORT Length, MaximumLength; const wchar_t *Buffer; } us;
    us.Length = us.MaximumLength = (USHORT)(wcslen(value) * sizeof(wchar_t));
    us.Buffer = value;
    union { kvpi info; unsigned char b[600]; } buf;
    ULONG len = 0;
    LONG st = q(root, &us, 2, &buf, sizeof buf, &len);
    o_hex32("raw_status", (DWORD)st);
    if (!st) {
        o_u32("raw_type", buf.info.Type);
        o_u32("raw_size", buf.info.DataLength);
        o_bytes("raw_data", buf.info.Data, buf.info.DataLength);
    }
}

static void set_case(const char *cname, const wchar_t *value, DWORD type, const void *data, DWORD size)
{
    LONG st;
    ORACLE_CALL(st, RegSetValueExW(root, value, 0, type, data, size));
    oracle_case(cname);
    o_status("ret", st);
    o_lasterr();
    raw(value);
    unsigned char b[600];
    memset(b, 0xCC, sizeof b);
    DWORD t = 0xAAAAAAAAu, sz = sizeof b;
    LONG q = RegQueryValueExW(root, value, NULL, &t, b, &sz);
    o_status("q_ret", q);
    if (!q) {
        o_u32("q_type", t);
        o_u32("q_size", sz);
        o_bytes("q_data", b, sz + 4); /* also the bytes after the data */
        /* Again with a buffer of exactly the reported size. */
        DWORD exact = sz;
        memset(b, 0xCC, sizeof b);
        LONG q2 = RegQueryValueExW(root, value, NULL, NULL, b, &exact);
        o_status("exact_ret", q2);
        o_u32("exact_size", exact);
        o_bytes("exact_data", b, sz + 4);
    }
    oracle_case_end();
}

int main(void)
{
    oracle_begin("advapi32/RegSetValueExW_strings");
    root = oracle_reg_root();

    static const wchar_t hello[] = L"hello";        /* 12 bytes with NUL */
    static const wchar_t two_nul[] = L"hello\0";     /* 14 bytes */
    static const wchar_t embedded[] = L"ab\0cd";     /* 12 bytes */
    static const wchar_t empty[] = L"";

    for (int t = 0; t < 2; t++) {
        DWORD type = t ? REG_EXPAND_SZ : REG_SZ;
        const char *p = t ? "expand_" : "sz_";
        char n[64];
#define C(suffix, val, data, size) \
        snprintf(n, sizeof n, "%s%s", p, suffix); set_case(n, val, type, data, size)
        C("with_nul", t ? L"EWith" : L"SWith", hello, sizeof hello);
        C("without_nul", t ? L"ENo" : L"SNo", hello, sizeof hello - 2);
        C("two_nuls", t ? L"ETwo" : L"STwo", two_nul, sizeof two_nul);
        C("embedded_nul", t ? L"EEmb" : L"SEmb", embedded, sizeof embedded);
        C("embedded_nul_no_final", t ? L"EEmbNo" : L"SEmbNo", embedded, sizeof embedded - 2);
        C("odd_1", t ? L"EOdd1" : L"SOdd1", hello, 1);
        C("odd_3", t ? L"EOdd3" : L"SOdd3", hello, 3);
        C("odd_11", t ? L"EOdd11" : L"SOdd11", hello, 11);
        C("odd_13_past_nul", t ? L"EOdd13" : L"SOdd13", two_nul, 13);
        C("only_nul", t ? L"ENul" : L"SNul", empty, sizeof empty);
        C("zero_size_with_ptr", t ? L"EZero" : L"SZero", hello, 0);
        C("zero_size_null_ptr", t ? L"EZeroN" : L"SZeroN", NULL, 0);
        C("one_char_no_nul", t ? L"EOne" : L"SOne", hello, 2);
        C("half_nul", t ? L"EHalf" : L"SHalf", empty, 1);
        C("two_chars_no_nul", t ? L"ETwoCh" : L"STwoCh", hello, 4);
        C("four_chars_no_nul", t ? L"EFour" : L"SFour", hello, 8);
        C("odd_5_next_char", t ? L"EOdd5" : L"SOdd5", hello, 5);
#undef C
    }

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
