/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: the value statistics RegQueryInfoKeyW and RegQueryInfoKeyA
 * report (number of values, longest value name, longest data) as values are
 * added, grown, shrunk and deleted: are the maximums ever lowered? Does
 * the default value count? Does the A function report ANSI or Unicode
 * sizes for names and string data?
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regqueryinfokeyw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regqueryinfokeya
 */
#include "oracle.h"

static HKEY root;

static void info(const char *cname)
{
    DWORD n = 9999, mn = 9999, md = 9999, an = 9999, amn = 9999, amd = 9999;
    LONG st;
    ORACLE_CALL(st, RegQueryInfoKeyW(root, NULL, NULL, NULL, NULL, NULL, NULL, &n, &mn, &md, NULL, NULL));
    oracle_case(cname);
    o_status("ret", st);
    o_u32("values", n);
    o_u32("max_name", mn);
    o_u32("max_data", md);
    LONG sa = RegQueryInfoKeyA(root, NULL, NULL, NULL, NULL, NULL, NULL, &an, &amn, &amd, NULL, NULL);
    o_status("a_ret", sa);
    o_u32("a_values", an);
    o_u32("a_max_name", amn);
    o_u32("a_max_data", amd);
    oracle_case_end();
}

static void put(const wchar_t *name, DWORD type, const void *data, DWORD size)
{
    RegSetValueExW(root, name, 0, type, data, size);
}

int main(void)
{
    oracle_begin("advapi32/RegQueryInfoKey_values");
    root = oracle_reg_root();
    static const unsigned char big[100] = { 0 };

    info("empty_key");
    put(L"abc", REG_DWORD, big, 4);
    info("one_value");
    put(L"longername", REG_BINARY, big, 20);
    info("longer_name_bigger_data");
    put(L"longername", REG_BINARY, big, 2);
    info("data_shrunk");
    RegDeleteValueW(root, L"longername");
    info("longest_deleted");
    put(NULL, REG_SZ, L"default!", 18);
    info("default_value_added");
    put(L"s", REG_SZ, L"0123456789", 22);
    info("sz_22_bytes");
    put(L"m", REG_MULTI_SZ, L"a\0b\0", 10);
    put(L"\x00e9\x00e9\x00e9\x00e9\x00e9\x00e9\x00e9\x00e9\x00e9\x00e9\x00e9\x00e9", REG_DWORD, big, 4);
    info("non_ascii_name_12");
    put(L"bin", REG_BINARY, big, 100);
    info("binary_100");
    RegDeleteValueW(root, L"bin");
    RegDeleteValueW(root, L"s");
    RegDeleteValueW(root, L"m");
    RegDeleteValueW(root, NULL);
    RegDeleteValueW(root, L"abc");
    RegDeleteValueW(root, L"\x00e9\x00e9\x00e9\x00e9\x00e9\x00e9\x00e9\x00e9\x00e9\x00e9\x00e9\x00e9");
    info("all_deleted");

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
