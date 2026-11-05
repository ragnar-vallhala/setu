/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: RegSetValueExW with every documented value type number and
 * some undefined ones; each value read back with RegQueryValueExW, with
 * RegEnumValueW and raw with NtQueryValueKey.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regsetvalueexw
 *       https://learn.microsoft.com/en-us/windows/win32/sysinfo/registry-value-types
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

static void enum_type(const wchar_t *value)
{
    wchar_t name[64];
    for (DWORD i = 0;; i++) {
        DWORD n = 64, t = 0xAAAAAAAAu, sz = 0;
        if (RegEnumValueW(root, i, name, &n, NULL, &t, NULL, &sz)) break;
        if (!wcscmp(name, value)) { o_u32("enum_type", t); o_u32("enum_size", sz); return; }
    }
    o_str("enum_type", "not found");
}

static void set_case(const char *cname, const wchar_t *value, DWORD type, const void *data, DWORD size)
{
    LONG st;
    ORACLE_CALL(st, RegSetValueExW(root, value, 0, type, data, size));
    oracle_case(cname);
    o_status("ret", st);
    o_lasterr();
    unsigned char b[600];
    memset(b, 0xCC, sizeof b);
    DWORD t = 0xAAAAAAAAu, sz = sizeof b;
    LONG q = RegQueryValueExW(root, value, NULL, &t, b, &sz);
    o_status("q_ret", q);
    if (!q) { o_u32("q_type", t); o_u32("q_size", sz); o_bytes("q_data", b, sz); }
    raw(value);
    enum_type(value);
    oracle_case_end();
}

int main(void)
{
    oracle_begin("advapi32/RegSetValueExW_types");
    root = oracle_reg_root();

    static const unsigned char d5[] = { 'a', 0, 'b', 0, 0 };
    static const wchar_t str[] = L"text";
    static const unsigned char d8[] = { 1, 2, 3, 4, 5, 6, 7, 8 };

    set_case("t0_none", L"T0", REG_NONE, d5, sizeof d5);
    set_case("t1_sz", L"T1", REG_SZ, str, sizeof str);
    set_case("t2_expand_sz", L"T2", REG_EXPAND_SZ, str, sizeof str);
    set_case("t3_binary", L"T3", REG_BINARY, d5, sizeof d5);
    set_case("t4_dword", L"T4", REG_DWORD, d8, 4);
    set_case("t5_dword_be", L"T5", REG_DWORD_BIG_ENDIAN, d8, 4);
    set_case("t6_link", L"T6", REG_LINK, str, sizeof str - sizeof(wchar_t));
    set_case("t7_multi_sz", L"T7", REG_MULTI_SZ, L"a\0b\0", sizeof L"a\0b\0");
    set_case("t8_resource_list", L"T8", REG_RESOURCE_LIST, d8, sizeof d8);
    set_case("t9_full_resource_descriptor", L"T9", REG_FULL_RESOURCE_DESCRIPTOR, d8, sizeof d8);
    set_case("t10_resource_requirements_list", L"T10", REG_RESOURCE_REQUIREMENTS_LIST, d8, sizeof d8);
    set_case("t11_qword", L"T11", REG_QWORD, d8, sizeof d8);
    set_case("t12_undefined", L"T12", 12, d5, sizeof d5);
    set_case("t13_undefined", L"T13", 13, d5, sizeof d5);
    set_case("t255_undefined", L"T255", 255, d5, sizeof d5);
    set_case("t65535_undefined", L"T65535", 0xFFFF, d5, sizeof d5);
    set_case("t0x10000_undefined", L"T10000", 0x10000, d5, sizeof d5);
    set_case("t0x7fffffff_undefined", L"T7FFFFFFF", 0x7FFFFFFF, d5, sizeof d5);
    set_case("t0x80000000_undefined", L"T80000000", 0x80000000u, d5, sizeof d5);
    set_case("t0xffffffff_undefined", L"TFFFFFFFF", 0xFFFFFFFFu, d5, sizeof d5);
    set_case("t0_none_empty", L"T0E", REG_NONE, NULL, 0);
    set_case("t4_dword_empty", L"T4E", REG_DWORD, NULL, 0);
    set_case("t11_qword_empty", L"T11E", REG_QWORD, NULL, 0);
    set_case("t7_multi_sz_empty", L"T7E", REG_MULTI_SZ, NULL, 0);
    set_case("t6_link_odd", L"T6O", REG_LINK, str, 5);
    set_case("t2_expand_sz_odd", L"T2O", REG_EXPAND_SZ, str, 5);
    set_case("t7_multi_sz_odd", L"T7O", REG_MULTI_SZ, str, 5);
    set_case("t3_binary_odd", L"T3O", REG_BINARY, d5, 3);
    set_case("retype_sz_to_binary", L"T1", REG_BINARY, d5, sizeof d5);
    set_case("retype_binary_to_qword", L"T1", REG_QWORD, d8, sizeof d8);
    set_case("retype_to_undefined", L"T1", 0x1234, d8, 2);

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
