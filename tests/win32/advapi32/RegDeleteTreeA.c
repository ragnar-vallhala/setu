/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: RegDeleteTreeA: subtree deletion, NULL and empty subkey,
 * cp1252 names, case-insensitive names, missing keys and bad handles.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regdeletetreea
 */
#include "oracle.h"

static HKEY root;

static void mk(const char *path)
{
    HKEY k;
    DWORD v = 1;
    if (!RegCreateKeyExA(root, path, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL)) {
        RegSetValueExA(k, "v", 0, REG_DWORD, (const BYTE *)&v, sizeof v);
        RegCloseKey(k);
    }
}

static void state(const char *path)
{
    HKEY k;
    LONG st = RegOpenKeyExA(root, path, 0, KEY_READ, &k);
    o_status("open_after", st);
    if (!st) {
        DWORD nsub = 0, nval = 0;
        RegQueryInfoKeyA(k, NULL, NULL, NULL, &nsub, NULL, NULL, &nval, NULL, NULL, NULL, NULL);
        o_u32("subkeys", nsub);
        o_u32("values", nval);
        RegCloseKey(k);
    }
}

#define TREE_CASE(name, hk, sub, check)                \
    do {                                               \
        LONG st_;                                      \
        ORACLE_CALL(st_, RegDeleteTreeA((hk), (sub))); \
        oracle_case(name);                             \
        o_status("ret", st_);                          \
        o_lasterr();                                   \
        state(check);                                  \
        oracle_case_end();                             \
    } while (0)

int main(void)
{
    oracle_begin("advapi32/RegDeleteTreeA");
    root = oracle_reg_root();

    mk("t\\a\\b\\c");
    mk("t\\d");
    mk("t");
    TREE_CASE("subtree", root, "t\\a", "t");
    TREE_CASE("whole", root, "t", "t");
    mk("s\\a");
    mk("s");
    {
        HKEY s;
        RegOpenKeyExA(root, "s", 0, KEY_ALL_ACCESS, &s);
        TREE_CASE("null_subkey", s, NULL, "s");
        mk("s\\b");
        mk("s");
        TREE_CASE("empty_subkey", s, "", "s");
        RegCloseKey(s);
    }
    mk("caf\xe9\\x");
    TREE_CASE("cp1252_upper", root, "CAF\xc9", "caf\xe9");
    mk("Mixed\\x");
    TREE_CASE("other_case", root, "mIXED", "Mixed");
    TREE_CASE("missing", root, "missing", "missing");
    TREE_CASE("leading_backslash", root, "\\s", "s");
    TREE_CASE("invalid_handle", (HKEY)(ULONG_PTR)0x1234, "s", "s");
    TREE_CASE("null_handle", NULL, "s", "s");

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
