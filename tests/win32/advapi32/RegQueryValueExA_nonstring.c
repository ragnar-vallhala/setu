/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: RegQueryValueExA on values that are not string types
 * (REG_NONE, REG_BINARY, REG_DWORD, REG_DWORD_BIG_ENDIAN, REG_QWORD,
 * REG_LINK, resource types, undefined type numbers) holding Unicode text:
 * the data must come back unconverted. Size-only queries, full reads and
 * short buffers.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regqueryvalueexa
 *       https://learn.microsoft.com/en-us/windows/win32/sysinfo/registry-value-types
 */
#include "oracle.h"

static HKEY root;

static void q_case(const char *cname, DWORD type, DWORD size)
{
    static const wchar_t text[] = L"abcdefg"; /* 16 bytes */
    RegSetValueExW(root, L"V", 0, type, (const BYTE *)text, size);
    DWORD t = 0, need = 0;
    LONG st;
    ORACLE_CALL(st, RegQueryValueExA(root, "V", NULL, &t, NULL, &need));
    oracle_case(cname);
    o_status("sizeonly_ret", st);
    o_lasterr();
    o_u32("sizeonly_type", t);
    o_u32("sizeonly_size", need);
    unsigned char b[64];
    memset(b, 0xCC, sizeof b);
    DWORD sz = sizeof b;
    LONG q = RegQueryValueExA(root, "V", NULL, &t, b, &sz);
    o_status("ret", q);
    o_u32("size", sz);
    if (!q) o_bytes("data", b, sz);
    memset(b, 0xCC, sizeof b);
    DWORD small = size / 2;
    LONG qs = RegQueryValueExA(root, "V", NULL, NULL, b, &small);
    o_status("half_ret", qs);
    o_u32("half_size", small);
    o_bytes("half_buffer", b, size / 2 + 2);
    RegDeleteValueW(root, L"V");
    oracle_case_end();
}

int main(void)
{
    oracle_begin("advapi32/RegQueryValueExA_nonstring");
    root = oracle_reg_root();

    q_case("none", REG_NONE, 16);
    q_case("binary", REG_BINARY, 16);
    q_case("binary_odd", REG_BINARY, 7);
    q_case("dword", REG_DWORD, 4);
    q_case("dword_be", REG_DWORD_BIG_ENDIAN, 4);
    q_case("qword", REG_QWORD, 8);
    q_case("link", REG_LINK, 14);
    q_case("resource_list", REG_RESOURCE_LIST, 16);
    q_case("full_resource_descriptor", REG_FULL_RESOURCE_DESCRIPTOR, 16);
    q_case("resource_requirements_list", REG_RESOURCE_REQUIREMENTS_LIST, 16);
    q_case("undefined_12", 12, 16);
    q_case("undefined_0x80000001", 0x80000001u, 16);

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
