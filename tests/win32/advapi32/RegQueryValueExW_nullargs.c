/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: RegQueryValueExW with optional arguments left out: NULL type,
 * NULL data, NULL size (with and without data), a non-NULL Reserved
 * pointer, and all of them NULL (an existence check).
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regqueryvalueexw
 */
#include "oracle.h"

static HKEY root;

static void q_case(const char *cname, const wchar_t *value, int use_reserved, int use_type, int use_data,
                   int use_size, DWORD bufsize)
{
    unsigned char b[32];
    memset(b, 0xCC, sizeof b);
    DWORD t = 0xAAAAAAAAu, sz = bufsize, res = 0x55555555u;
    LONG st;
    ORACLE_CALL(st, RegQueryValueExW(root, value, use_reserved ? &res : NULL, use_type ? &t : NULL,
                                     use_data ? b : NULL, use_size ? &sz : NULL));
    oracle_case(cname);
    o_status("ret", st);
    o_lasterr();
    if (use_type) o_u32("type", t);
    if (use_size) o_u32("size", sz);
    if (use_reserved) o_hex32("reserved", res);
    if (use_data) o_bytes("buffer", b, 16);
    oracle_case_end();
}

int main(void)
{
    oracle_begin("advapi32/RegQueryValueExW_nullargs");
    root = oracle_reg_root();

    static const wchar_t hi[] = L"hi";
    DWORD dw = 5;
    RegSetValueExW(root, L"Sz", 0, REG_SZ, (const BYTE *)hi, sizeof hi);
    RegSetValueExW(root, L"Dw", 0, REG_DWORD, (const BYTE *)&dw, 4);
    RegSetValueExW(root, L"Empty", 0, REG_BINARY, NULL, 0);

    /*                         name   res type data size bufsize */
    q_case("all_null_exists", L"Sz", 0, 0, 0, 0, 0);
    q_case("all_null_missing", L"Missing", 0, 0, 0, 0, 0);
    q_case("type_only", L"Sz", 0, 1, 0, 0, 0);
    q_case("type_only_missing", L"Missing", 0, 1, 0, 0, 0);
    q_case("size_only", L"Sz", 0, 0, 0, 1, 0);
    q_case("size_only_nonzero_in", L"Dw", 0, 0, 0, 1, 1000);
    q_case("data_without_size", L"Sz", 0, 1, 1, 0, 0);
    q_case("data_without_size_missing", L"Missing", 0, 1, 1, 0, 0);
    q_case("data_without_type", L"Dw", 0, 0, 1, 1, 16);
    q_case("reserved_nonnull", L"Dw", 1, 1, 1, 1, 16);
    q_case("reserved_nonnull_missing", L"Missing", 1, 1, 1, 1, 16);
    q_case("reserved_nonnull_size_only", L"Dw", 1, 0, 0, 1, 0);
    q_case("empty_value_size_only", L"Empty", 0, 1, 0, 1, 0);
    q_case("empty_value_data", L"Empty", 0, 1, 1, 1, 16);
    q_case("empty_value_data_zero_buf", L"Empty", 0, 1, 1, 1, 0);
    q_case("default_unset_type_only", NULL, 0, 1, 0, 0, 0);

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
