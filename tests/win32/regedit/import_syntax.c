/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: "regedit /s" value-syntax edge cases (the same files as
 * reg/import_syntax). Every run uses /s, so regedit never opens a window or
 * a message box.
 *
 * Docs: https://learn.microsoft.com/en-us/troubleshoot/windows-server/performance/add-modify-delete-registry-subkeys-values
 */
#define SC_SUB L"Software\\SetuOracle\\regedit_import_syntax"
#define SC_KEY L"HKEY_CURRENT_USER\\Software\\SetuOracle\\regedit_import_syntax"
#define SC_CMD L"regedit.exe /s in.reg"
#include "../reg/syntax_cases.h"

int main(void)
{
    oracle_begin("regedit/import_syntax");
    HKEY root = oracle_reg_root();
    sc_run_all();
    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
