/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: reg.exe with large registry data: a 6000-byte REG_BINARY and
 * REG_SZ value (query output and export line wrapping), a key with 200 values,
 * keys nested 30 deep (recursive query, export and delete), and long key and
 * value names near the documented limits (255 and 16383 characters).
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/sysinfo/registry-element-size-limits
 *       https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/reg-query
 */
#include "regtool.h"

#define K L"HKCU\\Software\\SetuOracle\\reg_large"
#define SUB L"Software\\SetuOracle\\reg_large"

static wchar_t cmd[40000];

/* Prints only the length and a short hash of the output, to keep goldens small. */
static void sized_case(const char *name, const wchar_t *cmdline)
{
    rt_run(cmdline);
    oracle_case(name);
    o_u32("exit", rt_exit);
    o_u32("out_len", (DWORD)strlen(rt_out));
    unsigned long h = 5381;
    for (const char *p = rt_out; *p; p++) h = h * 33 + (unsigned char)*p;
    o_u32("out_hash", (DWORD)h);
    o_str("err", rt_err);
    oracle_case_end();
}

int main(void)
{
    oracle_begin("reg/large");
    HKEY root = oracle_reg_root(), k, cur;
    static BYTE big[6000];
    static wchar_t bigsz[3000], name[16384];

    for (int i = 0; i < 6000; i++) big[i] = (BYTE)(i * 7);
    for (int i = 0; i < 2999; i++) bigsz[i] = L'a' + i % 26;
    bigsz[2999] = 0;

    RegCreateKeyExW(root, L"big", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL);
    RegSetValueExW(k, L"bin", 0, REG_BINARY, big, sizeof big);
    RegSetValueExW(k, L"sz", 0, REG_SZ, (const BYTE *)bigsz, sizeof bigsz);
    RegCloseKey(k);
    sized_case("query_big_binary", L"reg.exe query " K L"\\big /v bin");
    sized_case("query_big_sz", L"reg.exe query " K L"\\big /v sz");
    DeleteFileW(L"big.reg");
    oracle_case("export_big");
    rt_fields(L"reg.exe export " K L"\\big big.reg /y");
    oracle_case_end();
    RegDeleteTreeW(root, L"big");
    RegDeleteKeyW(root, L"big");
    oracle_case("import_big");
    rt_fields(L"reg.exe import big.reg");
    {
        DWORD type = 0, size = 0;
        static BYTE got[6000];
        LONG st;
        size = sizeof got;
        st = RegGetValueW(HKEY_CURRENT_USER, SUB L"\\big", L"bin", RRF_RT_ANY, &type, got, &size);
        o_status("bin_ret", st);
        o_u32("bin_size", size);
        o_bool("bin_same", !st && size == sizeof big && !memcmp(got, big, sizeof big));
        size = sizeof got;
        st = RegGetValueW(HKEY_CURRENT_USER, SUB L"\\big", L"sz", RRF_RT_ANY | RRF_NOEXPAND, &type, got, &size);
        o_status("sz_ret", st);
        o_u32("sz_size", size);
        o_bool("sz_same", !st && size == sizeof bigsz && !memcmp(got, bigsz, sizeof bigsz));
    }
    oracle_case_end();
    DeleteFileW(L"big.reg");

    RegCreateKeyExW(root, L"many", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL);
    for (DWORD i = 0; i < 200; i++) {
        wchar_t n[16];
        swprintf(n, 16, L"v%03lu", (unsigned long)i);
        RegSetValueExW(k, n, 0, REG_DWORD, (const BYTE *)&i, 4);
    }
    RegCloseKey(k);
    sized_case("query_many_values", L"reg.exe query " K L"\\many");
    rt_case("find_in_many", L"reg.exe query " K L"\\many /f v19 /v");

    cur = root;
    RegOpenKeyExW(root, NULL, 0, KEY_ALL_ACCESS, &cur);
    for (int i = 0; i < 30; i++) {
        HKEY next;
        RegCreateKeyExW(cur, i ? L"n" : L"deep", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &next, NULL);
        RegSetValueExW(next, L"level", 0, REG_DWORD, (const BYTE *)&i, 4);
        RegCloseKey(cur);
        cur = next;
    }
    RegCloseKey(cur);
    sized_case("query_deep_recursive", L"reg.exe query " K L"\\deep /s");
    rt_case("find_deepest", L"reg.exe query " K L"\\deep /s /f 0x1d /d");
    rt_case("delete_deep", L"reg.exe delete " K L"\\deep /f");
    oracle_case("deep_after_delete");
    o_regtree("tree", HKEY_CURRENT_USER, SUB L"\\deep");
    oracle_case_end();

    /* A 255-character key name and a 16383-character value name. */
    for (int i = 0; i < 255; i++) name[i] = L'k';
    name[255] = 0;
    swprintf(cmd, 40000, L"reg.exe add " K L"\\%ls /f", name);
    rt_case("add_key_255", cmd);
    name[255] = L'k';
    name[256] = 0;
    swprintf(cmd, 40000, L"reg.exe add " K L"\\%ls /f", name);
    rt_case("add_key_256", cmd);
    for (int i = 0; i < 16383; i++) name[i] = L'v';
    name[16383] = 0;
    RegCreateKeyExW(root, L"longname", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL);
    oracle_case("set_value_name_16383");
    o_status("ret", RegSetValueExW(k, name, 0, REG_SZ, (const BYTE *)L"x", 4));
    oracle_case_end();
    RegCloseKey(k);
    sized_case("query_long_value_name", L"reg.exe query " K L"\\longname");

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
