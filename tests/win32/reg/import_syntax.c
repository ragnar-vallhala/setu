/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: "reg import" value-syntax edge cases: hex digit and type
 * spelling, spaces and trailing commas in hex lists, line continuations next
 * to comments and at the end of the file, short and long dword: data, spaces
 * around "=", unterminated strings, default-value forms, deleting missing
 * values and keys, and comment and indentation forms.
 *
 * Docs: https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/reg-import
 *       https://learn.microsoft.com/en-us/troubleshoot/windows-server/performance/add-modify-delete-registry-subkeys-values
 */
#define SC_SUB L"Software\\SetuOracle\\reg_import_syntax"
#define SC_KEY L"HKEY_CURRENT_USER\\Software\\SetuOracle\\reg_import_syntax"
#define SC_CMD L"reg.exe import in.reg"
#include "syntax_cases.h"

int main(void)
{
    oracle_begin("reg/import_syntax");
    HKEY root = oracle_reg_root();
    sc_run_all();
    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
