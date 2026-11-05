/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: RegRenameKey (looked up with GetProcAddress): renaming a
 * subkey and the key itself (NULL/empty subkey), case-only renames, renaming
 * onto existing names, names with backslashes, long names, keys with
 * children and values, open handles to the renamed key, enumeration order
 * afterwards, volatile keys, access needed, and errors.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regrenamekey
 *       https://learn.microsoft.com/en-us/windows/win32/api/winternl/nf-winternl-ntrenamekey
 */
#include "oracle.h"

typedef LONG(WINAPI *rename_fn)(HKEY, LPCWSTR, LPCWSTR);
static rename_fn pRegRenameKey;
static HKEY root;

static HKEY mk(const wchar_t *path, DWORD options)
{
    HKEY k = NULL;
    DWORD v = 1;
    RegCreateKeyExW(root, path, 0, NULL, options, KEY_ALL_ACCESS, NULL, &k, NULL);
    RegSetValueExW(k, L"v", 0, REG_DWORD, (const BYTE *)&v, sizeof v);
    return k;
}

static void children(const char *field, const wchar_t *path)
{
    char list[1024] = "";
    HKEY k;
    LONG st = RegOpenKeyExW(root, path, 0, KEY_READ, &k);
    if (st) snprintf(list, sizeof list, "missing(%ld)", (long)st);
    else {
        for (DWORD i = 0;; i++) {
            wchar_t nm[300];
            DWORD n = 300;
            if (RegEnumKeyExW(k, i, nm, &n, NULL, NULL, NULL, NULL)) break;
            if (i) strcat(list, ",");
            if (n > 40) snprintf(list + strlen(list), 32, "<%lu chars>", (unsigned long)n);
            else strcat(list, oracle_utf8(nm));
        }
        RegCloseKey(k);
    }
    o_str(field, list);
}

#define REN_CASE(name, hk, sub, newname, show)                  \
    do {                                                        \
        LONG st_;                                               \
        ORACLE_CALL(st_, pRegRenameKey((hk), (sub), (newname))); \
        oracle_case(name);                                      \
        o_status("ret", st_);                                   \
        o_lasterr();                                            \
        children("children", show);                             \
        oracle_case_end();                                      \
    } while (0)

int main(void)
{
    oracle_begin("advapi32/RegRenameKey");
    pRegRenameKey = (rename_fn)(void (*)(void))GetProcAddress(GetModuleHandleW(L"advapi32.dll"), "RegRenameKey");
    oracle_case("available");
    o_bool("available", pRegRenameKey != NULL);
    oracle_case_end();
    if (!pRegRenameKey) return oracle_end();
    root = oracle_reg_root();

    RegCloseKey(mk(L"p\\alpha", 0));
    RegCloseKey(mk(L"p\\beta\\child", 0));
    RegCloseKey(mk(L"p\\gamma", 0));
    RegCloseKey(mk(L"p\\delta", 0));

    REN_CASE("rename_simple", root, L"p\\alpha", L"zeta", L"p");
    REN_CASE("rename_with_children", root, L"p\\beta", L"aaa", L"p");
    oracle_case("children_follow_rename");
    children("aaa", L"p\\aaa");
    oracle_case_end();
    REN_CASE("rename_case_only", root, L"p\\gamma", L"GAMMA", L"p");
    REN_CASE("rename_same_name", root, L"p\\delta", L"delta", L"p");
    REN_CASE("rename_onto_existing", root, L"p\\delta", L"zeta", L"p");
    REN_CASE("rename_onto_existing_other_case", root, L"p\\delta", L"ZETA", L"p");
    REN_CASE("rename_missing", root, L"p\\nope", L"x", L"p");
    REN_CASE("rename_new_with_backslash", root, L"p\\delta", L"a\\b", L"p");
    REN_CASE("rename_new_empty", root, L"p\\delta", L"", L"p");
    REN_CASE("rename_new_null", root, L"p\\delta", NULL, L"p");
    {
        wchar_t n255[256], n256[257];
        wmemset(n255, L'r', 255); n255[255] = 0;
        wmemset(n256, L's', 256); n256[256] = 0;
        REN_CASE("rename_to_255_chars", root, L"p\\delta", n255, L"p");
        REN_CASE("rename_255_back", root, n255, L"x", L"p");
        REN_CASE("rename_to_256_chars", root, L"p\\delta", n256, L"p");
    }
    {   /* Rename the key itself via NULL and empty subkey. */
        HKEY z = NULL;
        RegOpenKeyExW(root, L"p\\zeta", 0, KEY_ALL_ACCESS, &z);
        REN_CASE("rename_self_null_subkey", z, NULL, L"eta", L"p");
        REN_CASE("rename_self_empty_subkey", z, L"", L"theta", L"p");
        DWORD v = 2;
        oracle_case("handle_after_rename");
        o_status("set_value", RegSetValueExW(z, L"w", 0, REG_DWORD, (const BYTE *)&v, sizeof v));
        DWORD nval = 0;
        o_status("query_info", RegQueryInfoKeyW(z, NULL, NULL, NULL, NULL, NULL, NULL, &nval, NULL, NULL, NULL, NULL));
        o_u32("values", nval);
        oracle_case_end();
        RegCloseKey(z);
    }
    {   /* Access: a handle without KEY_WRITE. */
        HKEY r = NULL;
        RegOpenKeyExW(root, L"p", 0, KEY_READ, &r);
        REN_CASE("rename_via_readonly_parent", r, L"theta", L"iota", L"p");
        RegCloseKey(r);
        RegOpenKeyExW(root, L"p\\GAMMA", 0, KEY_READ, &r);
        REN_CASE("rename_self_readonly_handle", r, NULL, L"kappa", L"p");
        RegCloseKey(r);
    }
    {
        HKEY v = mk(L"p\\vol", REG_OPTION_VOLATILE);
        RegCloseKey(v);
        REN_CASE("rename_volatile", root, L"p\\vol", L"vol2", L"p");
    }
    REN_CASE("rename_invalid_handle", (HKEY)(ULONG_PTR)0x1234, L"x", L"y", L"p");
    REN_CASE("rename_leading_backslash", root, L"\\p\\delta", L"y", L"p");
    REN_CASE("rename_multi_level_source", root, L"p\\aaa\\child", L"kid", L"p\\aaa");
    REN_CASE("rename_with_dot_new_name", root, L"p\\delta", L".", L"p");

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
