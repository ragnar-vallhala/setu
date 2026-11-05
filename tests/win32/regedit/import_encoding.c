/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: "regedit /s" imports of .reg files in different encodings
 * (the same files as reg/import_encoding). Every run uses /s, so regedit
 * never opens a window or a message box.
 *
 * Docs: https://learn.microsoft.com/en-us/troubleshoot/windows-server/performance/add-modify-delete-registry-subkeys-values
 */
#define EC_SUB L"Software\\SetuOracle\\regedit_import_encoding"
#define EC_KEY "HKEY_CURRENT_USER\\Software\\SetuOracle\\regedit_import_encoding"
#define EC_CMD L"regedit.exe /s in.reg"
#include "../reg/encoding_cases.h"

int main(void)
{
    oracle_begin("regedit/import_encoding");
    HKEY root = oracle_reg_root();
    ec_run_all();
    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
