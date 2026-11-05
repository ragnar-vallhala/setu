/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: "reg import" of .reg files in different encodings: UTF-16LE
 * with and without a BOM, UTF-16BE, UTF-8 with and without a BOM, the ANSI
 * code page, CR-only and LF-only line ends, non-ASCII key and value names and
 * a file with no final newline. Each case prints the exit code, output and the
 * subkey the file names.
 *
 * Docs: https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/reg-import
 *       https://learn.microsoft.com/en-us/troubleshoot/windows-server/performance/add-modify-delete-registry-subkeys-values
 */
#define EC_SUB L"Software\\SetuOracle\\reg_import_encoding"
#define EC_KEY "HKEY_CURRENT_USER\\Software\\SetuOracle\\reg_import_encoding"
#define EC_CMD L"reg.exe import in.reg"
#include "encoding_cases.h"

int main(void)
{
    oracle_begin("reg/import_encoding");
    HKEY root = oracle_reg_root();
    ec_run_all();
    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
