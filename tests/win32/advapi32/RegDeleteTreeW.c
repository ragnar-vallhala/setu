/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: RegDeleteTreeW: deep and wide trees, NULL vs empty subkey
 * (empties the key itself, values included), access needed on the handle,
 * open handles into the deleted tree, volatile subkeys, a subtree with a key
 * whose DACL denies deletion (what is left after a partial failure), bad
 * handles and missing subkeys.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regdeletetreew
 *       https://learn.microsoft.com/en-us/windows/win32/secauthz/security-descriptor-string-format
 */
#include "oracle.h"
#include <sddl.h>

static HKEY root;

static void mk(const wchar_t *path, DWORD options)
{
    HKEY k;
    DWORD v = 1;
    if (!RegCreateKeyExW(root, path, 0, NULL, options, KEY_ALL_ACCESS, NULL, &k, NULL)) {
        RegSetValueExW(k, L"v", 0, REG_DWORD, (const BYTE *)&v, sizeof v);
        RegCloseKey(k);
    }
}

/* Counts all keys and values below (and including the values of) `path`. */
static void count_tree(HKEY k, DWORD *keys, DWORD *values)
{
    DWORD nsub = 0, nval = 0;
    RegQueryInfoKeyW(k, NULL, NULL, NULL, &nsub, NULL, NULL, &nval, NULL, NULL, NULL, NULL);
    *values += nval;
    for (DWORD i = 0; i < nsub; i++) {
        wchar_t nm[256];
        DWORD n = 256;
        HKEY c;
        if (RegEnumKeyExW(k, i, nm, &n, NULL, NULL, NULL, NULL)) break;
        (*keys)++;
        if (!RegOpenKeyExW(k, nm, 0, KEY_READ, &c)) {
            count_tree(c, keys, values);
            RegCloseKey(c);
        }
    }
}

static void state(const wchar_t *path)
{
    HKEY k;
    LONG st = RegOpenKeyExW(root, path, 0, KEY_READ, &k);
    o_status("open_after", st);
    if (!st) {
        DWORD keys = 0, values = 0;
        count_tree(k, &keys, &values);
        o_u32("keys_below", keys);
        o_u32("values_total", values);
        RegCloseKey(k);
    }
}

#define TREE_CASE(name, hk, sub, check)        \
    do {                                       \
        LONG st_;                              \
        ORACLE_CALL(st_, RegDeleteTreeW((hk), (sub))); \
        oracle_case(name);                     \
        o_status("ret", st_);                  \
        o_lasterr();                           \
        state(check);                          \
        oracle_case_end();                     \
    } while (0)

int main(void)
{
    oracle_begin("advapi32/RegDeleteTreeW");
    root = oracle_reg_root();

    /* Deep tree. */
    {
        wchar_t path[1024] = L"deep";
        for (int i = 0; i < 40; i++) wcscat(path, L"\\n");
        mk(path, 0);
        TREE_CASE("deep_40_levels", root, L"deep", L"deep");
    }
    /* Wide tree. */
    for (int i = 0; i < 300; i++) {
        wchar_t p[64];
        swprintf(p, 64, L"wide\\k%03d\\leaf", i);
        mk(p, 0);
    }
    TREE_CASE("wide_300_children", root, L"wide", L"wide");

    mk(L"self\\a\\b", 0);
    mk(L"self\\c", 0);
    mk(L"self", 0);
    {
        HKEY s;
        RegOpenKeyExW(root, L"self", 0, KEY_ALL_ACCESS, &s);
        TREE_CASE("null_subkey_empties_key", s, NULL, L"self");
        mk(L"self\\x", 0);
        mk(L"self", 0);
        TREE_CASE("empty_subkey", s, L"", L"self");
        TREE_CASE("null_subkey_on_empty_key", s, NULL, L"self");
        RegCloseKey(s);
    }
    mk(L"ro\\a\\b", 0);
    {
        HKEY r;
        RegOpenKeyExW(root, L"ro", 0, KEY_READ, &r);
        TREE_CASE("readonly_handle_subkey", r, L"a", L"ro");
        mk(L"ro\\a\\b", 0);
        TREE_CASE("readonly_handle_null", r, NULL, L"ro");
        RegCloseKey(r);
        RegOpenKeyExW(root, L"ro", 0, KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE | DELETE, &r);
        mk(L"ro\\a\\b", 0);
        TREE_CASE("enum_query_delete_handle_null", r, NULL, L"ro");
        RegCloseKey(r);
    }
    mk(L"oh\\inner\\leaf", 0);
    {
        HKEY in;
        RegOpenKeyExW(root, L"oh\\inner", 0, KEY_ALL_ACCESS, &in);
        TREE_CASE("open_handle_inside", root, L"oh", L"oh");
        DWORD v = 2;
        oracle_case("open_handle_inside_after");
        o_status("set_value", RegSetValueExW(in, L"z", 0, REG_DWORD, (const BYTE *)&v, sizeof v));
        o_status("close", RegCloseKey(in));
        oracle_case_end();
    }
    mk(L"vt", 0);
    mk(L"vt\\vol", REG_OPTION_VOLATILE);
    mk(L"vt\\vol\\vol2", REG_OPTION_VOLATILE);
    TREE_CASE("volatile_children", root, L"vt", L"vt");
    TREE_CASE("missing", root, L"missing", L"missing");
    TREE_CASE("missing_parent", root, L"missing\\x", L"missing");
    TREE_CASE("invalid_handle", (HKEY)(ULONG_PTR)0x1234, L"x", L"x");
    mk(L"bs\\in", 0);
    TREE_CASE("trailing_backslash", root, L"bs\\", L"bs");
    mk(L"cs\\in", 0);
    TREE_CASE("other_case", root, L"CS", L"cs");

    /* A locked subkey: everyone is denied all access except the owner's implicit rights. */
    mk(L"lk\\a1", 0);
    mk(L"lk\\locked\\inner", 0);
    mk(L"lk\\z9", 0);
    {
        HKEY l;
        PSECURITY_DESCRIPTOR sd = NULL;
        RegOpenKeyExW(root, L"lk\\locked", 0, KEY_ALL_ACCESS, &l);
        ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(D;;KA;;;WD)", SDDL_REVISION_1, &sd, NULL);
        LONG s = RegSetKeySecurity(l, DACL_SECURITY_INFORMATION, sd);
        LocalFree(sd);
        RegCloseKey(l);
        oracle_case("lock_set");
        o_status("ret", s);
        oracle_case_end();
        TREE_CASE("partial_locked_subkey", root, L"lk", L"lk");
        {
            char list[256] = "";
            HKEY k;
            if (!RegOpenKeyExW(root, L"lk", 0, KEY_READ, &k)) {
                for (DWORD i = 0;; i++) {
                    wchar_t nm[64];
                    DWORD n = 64;
                    if (RegEnumKeyExW(k, i, nm, &n, NULL, NULL, NULL, NULL)) break;
                    if (i) strcat(list, ",");
                    strcat(list, oracle_utf8(nm));
                }
                RegCloseKey(k);
            }
            oracle_case("partial_locked_remaining");
            o_str("children", list);
            oracle_case_end();
        }
        /* Unlock (owner keeps WRITE_DAC) and clean up. */
        if (!RegOpenKeyExW(root, L"lk\\locked", 0, WRITE_DAC, &l)) {
            ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:(A;;KA;;;WD)", SDDL_REVISION_1, &sd, NULL);
            RegSetKeySecurity(l, DACL_SECURITY_INFORMATION, sd);
            LocalFree(sd);
            RegCloseKey(l);
        }
        TREE_CASE("after_unlock", root, L"lk", L"lk");
    }

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
