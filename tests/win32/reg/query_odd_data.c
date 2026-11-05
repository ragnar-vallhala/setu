/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: how "reg query" and "reg export" show data whose size does not
 * fit its type: strings without a terminating NUL or with embedded NULs, odd
 * byte counts, short and long DWORD/QWORD data, REG_MULTI_SZ without the
 * final empty string or with empty strings inside, zero-length data of every
 * type, and the REG_LINK and resource types.
 *
 * Docs: https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/reg-query
 *       https://learn.microsoft.com/en-us/windows/win32/sysinfo/registry-value-types
 */
#include "regtool.h"

#define K L"HKCU\\Software\\SetuOracle\\reg_query_odd_data"

static HKEY root;

static void setraw(const wchar_t *name, DWORD type, const void *data, DWORD size)
{
    RegSetValueExW(root, name, 0, type, (const BYTE *)data, size);
}

/* Queries one value. */
static void q(const char *name, const wchar_t *value)
{
    static wchar_t cmd[512];
    swprintf(cmd, 512, L"reg.exe query " K L" /v %ls", value);
    rt_case(name, cmd);
}

int main(void)
{
    oracle_begin("reg/query_odd_data");
    root = oracle_reg_root();

    const BYTE b8[] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
    setraw(L"sz_no_nul", REG_SZ, L"abc", 6);
    setraw(L"sz_embedded_nul", REG_SZ, L"ab\0cd", 12);
    setraw(L"sz_odd_size", REG_SZ, L"abc", 5);
    setraw(L"sz_zero", REG_SZ, NULL, 0);
    setraw(L"expand_no_nul", REG_EXPAND_SZ, L"%x%", 6);
    setraw(L"expand_zero", REG_EXPAND_SZ, NULL, 0);
    setraw(L"dword_short", REG_DWORD, b8, 2);
    setraw(L"dword_long", REG_DWORD, b8, 6);
    setraw(L"dword_zero", REG_DWORD, NULL, 0);
    setraw(L"qword_short", REG_QWORD, b8, 4);
    setraw(L"qword_long", REG_QWORD, b8, 9);
    setraw(L"qword_zero", REG_QWORD, NULL, 0);
    setraw(L"multi_no_final", REG_MULTI_SZ, L"a\0b", 8);
    setraw(L"multi_empty_inside", REG_MULTI_SZ, L"a\0\0b\0", 12);
    setraw(L"multi_one_nul", REG_MULTI_SZ, L"", 2);
    setraw(L"multi_zero", REG_MULTI_SZ, NULL, 0);
    setraw(L"multi_odd", REG_MULTI_SZ, L"ab\0", 7);
    setraw(L"link", REG_LINK, L"\\Registry\\x", 22);
    setraw(L"resource_list", REG_RESOURCE_LIST, b8, 3);
    setraw(L"full_resource", REG_FULL_RESOURCE_DESCRIPTOR, b8, 3);
    setraw(L"requirements", REG_RESOURCE_REQUIREMENTS_LIST, b8, 3);
    setraw(L"type_big", 0x7fffffff, b8, 2);
    setraw(L"none_zero", REG_NONE, NULL, 0);

    q("sz_no_nul", L"sz_no_nul");
    q("sz_embedded_nul", L"sz_embedded_nul");
    q("sz_odd_size", L"sz_odd_size");
    q("sz_zero", L"sz_zero");
    q("expand_no_nul", L"expand_no_nul");
    q("expand_zero", L"expand_zero");
    q("dword_short", L"dword_short");
    q("dword_long", L"dword_long");
    q("dword_zero", L"dword_zero");
    q("qword_short", L"qword_short");
    q("qword_long", L"qword_long");
    q("qword_zero", L"qword_zero");
    q("multi_no_final", L"multi_no_final");
    q("multi_empty_inside", L"multi_empty_inside");
    q("multi_one_nul", L"multi_one_nul");
    q("multi_zero", L"multi_zero");
    q("multi_odd", L"multi_odd");
    q("link", L"link");
    q("resource_list", L"resource_list");
    q("full_resource", L"full_resource");
    q("requirements", L"requirements");
    q("type_big", L"type_big");
    q("none_zero", L"none_zero");

    DeleteFileW(L"odd.reg");
    oracle_case("export");
    rt_fields(L"reg.exe export " K L" odd.reg /y");
    o_text_file("file", L"odd.reg");
    oracle_case_end();
    DeleteFileW(L"odd.reg");

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
