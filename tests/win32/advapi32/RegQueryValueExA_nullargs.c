/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: RegQueryValueExA with optional arguments left out (NULL type,
 * data or size, a non-NULL Reserved pointer), the default value through
 * NULL and "" names, and invalid handles.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regqueryvalueexa
 */
#include "oracle.h"

static HKEY root;

static void q_case(const char *cname, HKEY key, const char *value, int use_reserved, int use_type,
                   int use_data, int use_size, DWORD bufsize)
{
    unsigned char b[32];
    memset(b, 0xCC, sizeof b);
    DWORD t = 0xAAAAAAAAu, sz = bufsize, res = 0x55555555u;
    LONG st;
    ORACLE_CALL(st, RegQueryValueExA(key, value, use_reserved ? &res : NULL, use_type ? &t : NULL,
                                     use_data ? b : NULL, use_size ? &sz : NULL));
    oracle_case(cname);
    o_status("ret", st);
    o_lasterr();
    if (use_type) o_u32("type", t);
    if (use_size) o_u32("size", sz);
    if (use_data) o_bytes("buffer", b, 12);
    oracle_case_end();
}

int main(void)
{
    oracle_begin("advapi32/RegQueryValueExA_nullargs");
    root = oracle_reg_root();

    static const wchar_t hi[] = L"hi";
    DWORD dw = 5;
    RegSetValueExW(root, L"Sz", 0, REG_SZ, (const BYTE *)hi, sizeof hi);
    RegSetValueExW(root, L"Dw", 0, REG_DWORD, (const BYTE *)&dw, 4);

    q_case("all_null_exists", root, "Sz", 0, 0, 0, 0, 0);
    q_case("all_null_missing", root, "Missing", 0, 0, 0, 0, 0);
    q_case("type_only_sz", root, "Sz", 0, 1, 0, 0, 0);
    q_case("size_only_sz", root, "Sz", 0, 0, 0, 1, 0);
    q_case("data_without_size_sz", root, "Sz", 0, 1, 1, 0, 0);
    q_case("data_without_size_dw", root, "Dw", 0, 1, 1, 0, 0);
    q_case("data_without_type_sz", root, "Sz", 0, 0, 1, 1, 16);
    q_case("reserved_nonnull", root, "Sz", 1, 1, 1, 1, 16);
    q_case("default_null_unset", root, NULL, 0, 1, 1, 1, 16);
    RegSetValueExW(root, NULL, 0, REG_SZ, (const BYTE *)hi, sizeof hi);
    q_case("default_null_set", root, NULL, 0, 1, 1, 1, 16);
    q_case("default_empty_set", root, "", 0, 1, 1, 1, 16);
    q_case("invalid_handle", (HKEY)(ULONG_PTR)0x1234, "Sz", 0, 1, 1, 1, 16);
    q_case("null_handle", NULL, "Sz", 0, 1, 1, 1, 16);
    q_case("name_other_case", root, "SZ", 0, 1, 1, 1, 16);

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
