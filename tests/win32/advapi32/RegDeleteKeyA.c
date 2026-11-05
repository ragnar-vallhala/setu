/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: RegDeleteKeyA and RegDeleteKeyExA: leaf and non-leaf keys,
 * missing keys and parents, NULL/empty subkeys (deleting the key itself),
 * case-insensitive names, cp1252 names, backslash variants, view flags and
 * reserved values, and deleting through a parent opened with KEY_READ.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regdeletekeya
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regdeletekeyexa
 */
#include "oracle.h"

static HKEY root;

static void mk(const char *path)
{
    HKEY k;
    if (!RegCreateKeyExA(root, path, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL)) RegCloseKey(k);
}

static LONG exists(const char *path)
{
    HKEY k;
    LONG st = RegOpenKeyExA(root, path, 0, KEY_READ, &k);
    if (!st) RegCloseKey(k);
    return st;
}

#define DEL_CASE(name, expr, check)        \
    do {                                   \
        LONG st_;                          \
        ORACLE_CALL(st_, (expr));          \
        oracle_case(name);                 \
        o_status("ret", st_);              \
        o_lasterr();                       \
        o_status("exists_after", exists(check)); \
        oracle_case_end();                 \
    } while (0)

int main(void)
{
    oracle_begin("advapi32/RegDeleteKeyA");
    root = oracle_reg_root();

    mk("a\\b\\c");
    mk("leaf");
    mk("Upper");
    mk("caf\xe9");
    mk("tb");
    mk("x\\y");
    mk("ex1");
    mk("ex2");
    mk("ex3");
    mk("ro\\child");

    DEL_CASE("delete_leaf", RegDeleteKeyA(root, "leaf"), "leaf");
    DEL_CASE("delete_leaf_again", RegDeleteKeyA(root, "leaf"), "leaf");
    DEL_CASE("delete_non_leaf", RegDeleteKeyA(root, "a"), "a");
    DEL_CASE("delete_deep_leaf_path", RegDeleteKeyA(root, "a\\b\\c"), "a\\b\\c");
    DEL_CASE("delete_missing_parent", RegDeleteKeyA(root, "nope\\c"), "nope");
    DEL_CASE("delete_other_case", RegDeleteKeyA(root, "UPPER"), "Upper");
    DEL_CASE("delete_cp1252_upper", RegDeleteKeyA(root, "CAF\xc9"), "caf\xe9");
    DEL_CASE("delete_trailing_backslash", RegDeleteKeyA(root, "tb\\"), "tb");
    DEL_CASE("delete_leading_backslash", RegDeleteKeyA(root, "\\x\\y"), "x\\y");
    DEL_CASE("delete_double_backslash", RegDeleteKeyA(root, "x\\\\y"), "x\\y");
    {
        HKEY b = NULL;
        RegOpenKeyExA(root, "a\\b", 0, KEY_ALL_ACCESS, &b);
        DEL_CASE("delete_empty_subkey_self", RegDeleteKeyA(b, ""), "a\\b");
        DEL_CASE("delete_empty_subkey_self_again", RegDeleteKeyA(b, ""), "a\\b");
        RegCloseKey(b);
    }
    {
        HKEY a = NULL;
        RegOpenKeyExA(root, "a", 0, KEY_ALL_ACCESS, &a);
        LONG st;
        ORACLE_CALL(st, RegDeleteKeyA(a, NULL));
        oracle_case("delete_null_subkey");
        o_status("ret", st);
        o_lasterr();
        o_status("exists_after", exists("a"));
        oracle_case_end();
        RegCloseKey(a);
    }
    {
        HKEY ro = NULL;
        RegOpenKeyExA(root, "ro", 0, KEY_READ, &ro);
        DEL_CASE("delete_via_readonly_parent", RegDeleteKeyA(ro, "child"), "ro\\child");
        RegCloseKey(ro);
    }
    DEL_CASE("delete_invalid_parent", RegDeleteKeyA((HKEY)(ULONG_PTR)0x1234, "ex1"), "ex1");
    DEL_CASE("delete_ex_64key", RegDeleteKeyExA(root, "ex1", KEY_WOW64_64KEY, 0), "ex1");
    DEL_CASE("delete_ex_32key", RegDeleteKeyExA(root, "ex2", KEY_WOW64_32KEY, 0), "ex2");
    DEL_CASE("delete_ex_zero_sam", RegDeleteKeyExA(root, "ex3", 0, 0), "ex3");
    mk("ex4");
    DEL_CASE("delete_ex_reserved_1", RegDeleteKeyExA(root, "ex4", 0, 1), "ex4");
    DEL_CASE("delete_ex_access_bits_in_sam", RegDeleteKeyExA(root, "ex4", KEY_READ, 0), "ex4");
    mk("ex5");
    DEL_CASE("delete_ex_both_views", RegDeleteKeyExA(root, "ex5", KEY_WOW64_32KEY | KEY_WOW64_64KEY, 0), "ex5");
    DEL_CASE("delete_root_itself_has_children", RegDeleteKeyA(HKEY_CURRENT_USER, "Software\\SetuOracle\\advapi32_RegDeleteKeyA"),
             "");

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
