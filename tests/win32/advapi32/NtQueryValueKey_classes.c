/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: NtQueryValueKey (ntdll, through GetProcAddress) with each
 * documented KEY_VALUE_INFORMATION_CLASS (Basic, Full, Partial,
 * FullAlign64, PartialAlign64) and an undefined class, with exact, short
 * (header only, less than the header) and zero-length buffers: the status,
 * the ResultLength and the bytes written (buffer filled with 0xCC first).
 *
 * Docs: https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-zwqueryvaluekey
 *       https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/ne-wdm-_key_value_information_class
 *       https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/ns-wdm-_key_value_full_information
 */
#include "oracle.h"

static HKEY root;
typedef LONG (WINAPI *ntqvk_fn)(HANDLE, void *, int, void *, ULONG, ULONG *);
static ntqvk_fn ntqvk;

static void n_case(const char *cname, const wchar_t *value, int cls, ULONG bufsize, int with_len)
{
    struct { USHORT Length, MaximumLength; const wchar_t *Buffer; } us;
    us.Length = us.MaximumLength = (USHORT)(wcslen(value) * sizeof(wchar_t));
    us.Buffer = value;
    /* 8-byte aligned buffer, offset 0 */
    union { ULONGLONG align; unsigned char b[256]; } buf;
    memset(buf.b, 0xCC, sizeof buf.b);
    ULONG len = 0xAAAAAAAAu;
    LONG st = ntqvk(root, &us, cls, buf.b, bufsize, with_len ? &len : NULL);
    oracle_case(cname);
    o_hex32("status", (DWORD)st);
    if (with_len) o_u32("result_length", len);
    o_bytes("buffer", buf.b, bufsize + 4 < sizeof buf.b ? bufsize + 4 : sizeof buf.b);
    oracle_case_end();
}

int main(void)
{
    oracle_begin("advapi32/NtQueryValueKey_classes");
    root = oracle_reg_root();
    ntqvk = (ntqvk_fn)(void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryValueKey");
    RegSetValueExW(root, L"Val", 0, REG_BINARY, (const BYTE *)"\x11\x22\x33\x44\x55", 5);
    RegSetValueExW(root, L"Empty", 0, REG_DWORD, NULL, 0);

    /* Basic: TitleIndex, Type, NameLength, Name[] -> 12 + 6 = 18 bytes */
    n_case("basic_big", L"Val", 0, 128, 1);
    n_case("basic_exact", L"Val", 0, 18, 1);
    n_case("basic_header_only", L"Val", 0, 12, 1);
    n_case("basic_less_than_header", L"Val", 0, 8, 1);
    n_case("basic_zero", L"Val", 0, 0, 1);
    /* Full: TitleIndex, Type, DataOffset, DataLength, NameLength, Name[], data */
    n_case("full_big", L"Val", 1, 128, 1);
    n_case("full_header_plus_name", L"Val", 1, 26, 1);
    n_case("full_header_only", L"Val", 1, 20, 1);
    n_case("full_less_than_header", L"Val", 1, 10, 1);
    n_case("full_zero", L"Val", 1, 0, 1);
    /* Partial: TitleIndex, Type, DataLength, Data[] -> 12 + 5 */
    n_case("partial_big", L"Val", 2, 128, 1);
    n_case("partial_exact", L"Val", 2, 17, 1);
    n_case("partial_one_short", L"Val", 2, 16, 1);
    n_case("partial_header_only", L"Val", 2, 12, 1);
    n_case("partial_less_than_header", L"Val", 2, 11, 1);
    n_case("partial_zero", L"Val", 2, 0, 1);
    n_case("full_align64_big", L"Val", 3, 128, 1);
    n_case("full_align64_zero", L"Val", 3, 0, 1);
    n_case("partial_align64_big", L"Val", 4, 128, 1);
    n_case("partial_align64_zero", L"Val", 4, 0, 1);
    n_case("class_5", L"Val", 5, 128, 1);
    n_case("class_6", L"Val", 6, 128, 1);
    n_case("class_99", L"Val", 99, 128, 1);
    n_case("empty_partial", L"Empty", 2, 128, 1);
    n_case("empty_full", L"Empty", 1, 128, 1);
    n_case("missing_partial", L"Missing", 2, 128, 1);
    n_case("other_case_basic", L"VAL", 0, 128, 1);
    n_case("default_unset", L"", 2, 128, 1);

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
