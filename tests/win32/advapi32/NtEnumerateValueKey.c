/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: NtEnumerateValueKey (ntdll, through GetProcAddress) with the
 * Basic, Full and Partial information classes: status and ResultLength for
 * exact, short and zero-length buffers, the bytes written, indexes past
 * the end, and an undefined class.
 *
 * Docs: https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-zwenumeratevaluekey
 *       https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/ne-wdm-_key_value_information_class
 */
#include "oracle.h"

static HKEY root;
typedef LONG (WINAPI *ntevk_fn)(HANDLE, ULONG, int, void *, ULONG, ULONG *);
static ntevk_fn ntevk;

static void e_case(const char *cname, ULONG index, int cls, ULONG bufsize)
{
    union { ULONGLONG align; unsigned char b[256]; } buf;
    memset(buf.b, 0xCC, sizeof buf.b);
    ULONG len = 0xAAAAAAAAu;
    LONG st = ntevk(root, index, cls, buf.b, bufsize, &len);
    oracle_case(cname);
    o_hex32("status", (DWORD)st);
    o_u32("result_length", len);
    o_bytes("buffer", buf.b, bufsize + 4 < sizeof buf.b ? bufsize + 4 : sizeof buf.b);
    oracle_case_end();
}

int main(void)
{
    oracle_begin("advapi32/NtEnumerateValueKey");
    root = oracle_reg_root();
    ntevk = (ntevk_fn)(void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtEnumerateValueKey");
    RegSetValueExW(root, L"Val", 0, REG_BINARY, (const BYTE *)"\x11\x22\x33", 3);
    RegSetValueExW(root, NULL, 0, REG_SZ, (const BYTE *)L"d", 4);

    e_case("basic_big", 0, 0, 128);
    e_case("basic_exact", 0, 0, 18);
    e_case("basic_header_only", 0, 0, 12);
    e_case("basic_short", 0, 0, 4);
    e_case("basic_zero", 0, 0, 0);
    e_case("full_big", 0, 1, 128);
    e_case("full_one_short", 0, 1, 28);
    e_case("full_header_only", 0, 1, 20);
    e_case("full_zero", 0, 1, 0);
    e_case("partial_big", 0, 2, 128);
    e_case("partial_exact", 0, 2, 15);
    e_case("partial_header_only", 0, 2, 12);
    e_case("partial_zero", 0, 2, 0);
    e_case("default_basic", 1, 0, 128);
    e_case("default_full", 1, 1, 128);
    e_case("past_end", 2, 2, 128);
    e_case("past_end_zero_buffer", 2, 2, 0);
    e_case("class_99", 0, 99, 128);

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
