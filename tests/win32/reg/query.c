/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: "reg query" output for each value type, /v, /ve, a key with
 * subkeys, an empty key, and name lookup case.
 *
 * Docs: https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/reg-query
 */
#include "regtool.h"

#define K L"HKCU\\Software\\SetuOracle\\reg_query"

static void setraw(HKEY k, const wchar_t *name, DWORD type, const void *data, DWORD size)
{
    RegSetValueExW(k, name, 0, type, data, size);
}

int main(void)
{
    oracle_begin("reg/query");
    HKEY root = oracle_reg_root();
    HKEY sub = NULL, sub2 = NULL, deep = NULL;

    DWORD dw = 0x1234abcd;
    ULONGLONG qw = 0x0102030405060708ULL;
    BYTE bin[] = {0xde, 0xad, 0xbe, 0xef, 0x00, 0x01};
    setraw(root, L"str", REG_SZ, L"Hello, World", 26);
    setraw(root, L"empty", REG_SZ, L"", 2);
    setraw(root, L"dw", REG_DWORD, &dw, 4);
    setraw(root, L"qw", REG_QWORD, &qw, 8);
    setraw(root, L"bin", REG_BINARY, bin, sizeof bin);
    setraw(root, L"multi", REG_MULTI_SZ, L"one\0two\0three\0", 30);
    setraw(root, L"exp", REG_EXPAND_SZ, L"%SystemRoot%\\x", 30);
    setraw(root, L"none", REG_NONE, bin, 2);
    setraw(root, L"bin_empty", REG_BINARY, NULL, 0);
    setraw(root, L"dw_be", REG_DWORD_BIG_ENDIAN, &dw, 4);
    setraw(root, L"odd", 0x1234, bin, 3);
    setraw(root, L"with space", REG_SZ, L"a b", 8);
    RegCreateKeyExW(root, L"sub", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &sub, NULL);
    RegCreateKeyExW(root, L"sub\\deep", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &deep, NULL);
    RegCreateKeyExW(root, L"sub2", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &sub2, NULL);
    setraw(sub, NULL, REG_SZ, L"default data", 26);
    setraw(sub, L"inner", REG_DWORD, &dw, 4);

    rt_case("query_key", L"reg.exe query " K);
    rt_case("query_v_str", L"reg.exe query " K L" /v str");
    rt_case("query_v_empty_string", L"reg.exe query " K L" /v empty");
    rt_case("query_v_dword", L"reg.exe query " K L" /v dw");
    rt_case("query_v_qword", L"reg.exe query " K L" /v qw");
    rt_case("query_v_binary", L"reg.exe query " K L" /v bin");
    rt_case("query_v_binary_empty", L"reg.exe query " K L" /v bin_empty");
    rt_case("query_v_multi", L"reg.exe query " K L" /v multi");
    rt_case("query_v_expand", L"reg.exe query " K L" /v exp");
    rt_case("query_v_none", L"reg.exe query " K L" /v none");
    rt_case("query_v_dword_big_endian", L"reg.exe query " K L" /v dw_be");
    rt_case("query_v_unknown_type", L"reg.exe query " K L" /v odd");
    rt_case("query_v_quoted_space", L"reg.exe query " K L" /v \"with space\"");
    rt_case("query_v_other_case", L"reg.exe query " K L" /v STR");
    rt_case("query_v_missing", L"reg.exe query " K L" /v nosuch");
    rt_case("query_ve_set", L"reg.exe query " K L"\\sub /ve");
    rt_case("query_ve_unset", L"reg.exe query " K L"\\sub2 /ve");
    rt_case("query_subkey_with_default", L"reg.exe query " K L"\\sub");
    rt_case("query_empty_key", L"reg.exe query " K L"\\sub2");
    rt_case("query_long_root_name", L"reg.exe query HKEY_CURRENT_USER\\Software\\SetuOracle\\reg_query\\sub");
    rt_case("query_trailing_backslash", L"reg.exe query " K L"\\sub\\");
    rt_case("query_key_other_case", L"reg.exe query hkcu\\SOFTWARE\\setuoracle\\REG_QUERY\\SUB");
    rt_case("query_dash_switch", L"reg.exe query " K L" -v str");
    rt_case("query_upper_switch", L"reg.exe query " K L" /V str");

    RegCloseKey(deep);
    RegCloseKey(sub2);
    RegCloseKey(sub);
    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
