/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: RegSetValueExW for each common value type, size edge cases,
 * the default value, read-only handles; each value read back raw.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regsetvalueexw
 *       https://learn.microsoft.com/en-us/windows/win32/sysinfo/registry-value-types
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regqueryvalueexw
 */
#include "oracle.h"

static HKEY root;

/* NtQueryValueKey (documented as ZwQueryValueKey) returns the stored bytes
 * without any of RegQueryValueExW's string handling. */
typedef struct { ULONG TitleIndex, Type, DataLength; UCHAR Data[1]; } oracle_kvpi;
typedef LONG (WINAPI *oracle_ntqvk)(HANDLE, void *, int, void *, ULONG, ULONG *);

static void raw_case(const char *name, const wchar_t *value)
{
    oracle_ntqvk q = (oracle_ntqvk)(void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryValueKey");
    struct { USHORT Length, MaximumLength; const wchar_t *Buffer; } us;
    us.Length = us.MaximumLength = (USHORT)(wcslen(value) * sizeof(wchar_t));
    us.Buffer = value;
    union { oracle_kvpi info; unsigned char raw[512]; } buf;
    ULONG len = 0;
    LONG st = q(root, &us, 2 /* KeyValuePartialInformation */, &buf, sizeof buf, &len);
    oracle_case(name);
    o_hex32("status", (DWORD)st);
    if (!st) {
        o_u32("type", buf.info.Type);
        o_u32("size", buf.info.DataLength);
        o_bytes("data", buf.info.Data, buf.info.DataLength);
    }
    oracle_case_end();
}

static void set_case(const char *name, HKEY key, const wchar_t *value, DWORD type, const void *data, DWORD size)
{
    LONG st;
    ORACLE_CALL(st, RegSetValueExW(key, value, 0, type, data, size));
    oracle_case(name);
    o_status("ret", st);
    o_lasterr();
    /* Read the stored value back raw, to see exactly what was stored. */
    unsigned char buf[512];
    DWORD t = 0xAAAAAAAAu, sz = sizeof buf;
    LONG q = RegQueryValueExW(root, value, NULL, &t, buf, &sz);
    o_status("readback_ret", q);
    if (!q) {
        o_u32("readback_type", t);
        o_u32("readback_size", sz);
        o_bytes("readback_data", buf, sz);
    }
    oracle_case_end();
}

int main(void)
{
    oracle_begin("advapi32/RegSetValueExW");
    root = oracle_reg_root();

    const wchar_t sz[] = L"hello";
    set_case("sz_with_nul", root, L"Sz", REG_SZ, sz, sizeof sz);
    set_case("sz_without_nul", root, L"SzNoNul", REG_SZ, sz, sizeof sz - sizeof(wchar_t));
    set_case("sz_odd_size", root, L"SzOdd", REG_SZ, sz, 3);
    raw_case("sz_odd_size_stored_raw", L"SzOdd");
    set_case("sz_odd_size_5", root, L"SzOdd5", REG_SZ, sz, 5);
    set_case("sz_zero_size", root, L"SzEmpty", REG_SZ, sz, 0);
    set_case("expand_sz", root, L"Exp", REG_EXPAND_SZ, L"%PATH%", sizeof L"%PATH%");

    const wchar_t multi[] = L"one\0two\0";
    set_case("multi_sz", root, L"Multi", REG_MULTI_SZ, multi, sizeof multi);

    DWORD dw = 0x11223344;
    set_case("dword", root, L"Dw", REG_DWORD, &dw, sizeof dw);
    set_case("dword_short_size", root, L"DwShort", REG_DWORD, &dw, 2);
    set_case("dword_long_size", root, L"DwLong", REG_DWORD, multi, 8);
    DWORD be = 0x11223344;
    set_case("dword_big_endian", root, L"DwBE", REG_DWORD_BIG_ENDIAN, &be, sizeof be);
    ULONGLONG qw = 0x0102030405060708ull;
    set_case("qword", root, L"Qw", REG_QWORD, &qw, sizeof qw);

    const unsigned char bin[] = {0, 1, 2, 0xfe, 0xff};
    set_case("binary", root, L"Bin", REG_BINARY, bin, sizeof bin);
    set_case("binary_empty", root, L"BinEmpty", REG_BINARY, NULL, 0);
    set_case("none_type", root, L"NoneT", REG_NONE, bin, 2);
    set_case("unknown_type_1234", root, L"Weird", 1234, bin, sizeof bin);

    set_case("default_value_null_name", root, NULL, REG_SZ, sz, sizeof sz);
    set_case("overwrite_changes_type", root, L"Dw", REG_SZ, sz, sizeof sz);
    set_case("name_case_insensitive_overwrite", root, L"SZ", REG_DWORD, &dw, sizeof dw);
    set_case("null_data_nonzero_size", root, L"NullData", REG_BINARY, NULL, 4);

    {
        HKEY ro = NULL;
        RegOpenKeyExW(HKEY_CURRENT_USER, ORACLE_REG_BASE L"\\advapi32_RegSetValueExW", 0, KEY_READ, &ro);
        set_case("set_via_readonly_handle", ro, L"RoVal", REG_DWORD, &dw, sizeof dw);
        RegCloseKey(ro);
    }
    set_case("set_invalid_hkey", (HKEY)(ULONG_PTR)0x1234, L"X", REG_DWORD, &dw, sizeof dw);

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
