/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: "regedit /e" export text for values whose size does not fit
 * their type (strings without a NUL or with an odd size, short and long
 * DWORD/QWORD data, odd REG_MULTI_SZ data, zero-length data, REG_LINK and the
 * resource types), and what "regedit /s" makes of that file after the key is
 * deleted. Every run uses /s or /e on an existing key, so regedit never opens
 * a window or a message box.
 *
 * Docs: https://learn.microsoft.com/en-us/troubleshoot/windows-server/performance/add-modify-delete-registry-subkeys-values
 *       https://learn.microsoft.com/en-us/windows/win32/sysinfo/registry-value-types
 */
#include "../reg/odd_data.h"

#define K L"HKEY_CURRENT_USER\\Software\\SetuOracle\\regedit_export_odd_data"
#define SUB L"Software\\SetuOracle\\regedit_export_odd_data"

int main(void)
{
    oracle_begin("regedit/export_odd_data");
    HKEY root = oracle_reg_root(), d;
    RegCreateKeyExW(root, L"d", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &d, NULL);
    od_set_all(d);
    RegCloseKey(d);

    oracle_case("original");
    o_regtree("tree", HKEY_CURRENT_USER, SUB L"\\d");
    oracle_case_end();

    DeleteFileW(L"odd.reg");
    oracle_case("export");
    rt_fields(L"regedit.exe /e odd.reg " K L"\\d");
    o_text_file("file", L"odd.reg");
    oracle_case_end();

    RegDeleteTreeW(root, L"d");
    RegDeleteKeyW(root, L"d");
    oracle_case("import_after_delete");
    rt_fields(L"regedit.exe /s odd.reg");
    o_regtree("tree", HKEY_CURRENT_USER, SUB L"\\d");
    oracle_case_end();
    DeleteFileW(L"odd.reg");

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
