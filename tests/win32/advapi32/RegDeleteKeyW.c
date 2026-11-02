/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: deleting and opening registry keys and values: RegDeleteKeyW,
 * RegDeleteKeyExW, RegDeleteValueW, RegDeleteTreeW, RegDeleteKeyValueW,
 * RegOpenKeyExW error paths, RegCloseKey, and operations on deleted keys.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regdeletekeyw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regdeletekeyexw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regdeletevaluew
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regdeletetreew
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regdeletekeyvaluew
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regopenkeyexw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regclosekey
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regrenamekey
 *       https://learn.microsoft.com/en-us/windows/win32/sysinfo/registry-element-size-limits
 */
#include "oracle.h"

#define ST_CASE(name, expr)       \
    do {                          \
        LONG st_;                 \
        ORACLE_CALL(st_, (expr)); \
        oracle_case(name);        \
        o_status("ret", st_);     \
        o_lasterr();              \
        oracle_case_end();        \
    } while (0)

static HKEY root;

static HKEY mk(const wchar_t *path)
{
    HKEY k = NULL;
    RegCreateKeyExW(root, path, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL);
    return k;
}

static void setv(HKEY k, const wchar_t *name, DWORD v)
{
    RegSetValueExW(k, name, 0, REG_DWORD, (const BYTE *)&v, sizeof v);
}

static void exists_case(const char *name, const wchar_t *path)
{
    HKEY k;
    LONG st = RegOpenKeyExW(root, path, 0, KEY_READ, &k);
    oracle_case(name);
    o_status("open", st);
    if (!st) {
        DWORD subkeys = 0, values = 0;
        RegQueryInfoKeyW(k, NULL, NULL, NULL, &subkeys, NULL, NULL, &values, NULL, NULL, NULL, NULL);
        o_u32("subkeys", subkeys);
        o_u32("values", values);
        RegCloseKey(k);
    }
    oracle_case_end();
}

typedef LONG(WINAPI *rename_fn)(HKEY, LPCWSTR, LPCWSTR);

int main(void)
{
    oracle_begin("advapi32/RegDeleteKeyW");
    root = oracle_reg_root();

    HKEY a = mk(L"a");
    HKEY ab = mk(L"a\\b");
    mk(L"a\\b\\c");
    setv(a, L"v1", 1);
    setv(a, L"v2", 2);
    setv(a, NULL, 3);
    setv(ab, L"vb", 4);

    ST_CASE("delete_key_with_subkeys", RegDeleteKeyW(root, L"a"));
    ST_CASE("delete_missing_key", RegDeleteKeyW(root, L"missing"));
    ST_CASE("delete_missing_parent", RegDeleteKeyW(root, L"missing\\x"));
    ST_CASE("delete_empty_subkey_name", RegDeleteKeyW(a, L""));
    ST_CASE("delete_leaf_other_case", RegDeleteKeyW(root, L"A\\B\\C"));
    exists_case("after_leaf_delete", L"a\\b");

    ST_CASE("delete_value", RegDeleteValueW(a, L"v1"));
    ST_CASE("delete_value_again", RegDeleteValueW(a, L"v1"));
    ST_CASE("delete_value_other_case", RegDeleteValueW(a, L"V2"));
    ST_CASE("delete_default_value", RegDeleteValueW(a, NULL));
    ST_CASE("delete_default_value_again", RegDeleteValueW(a, L""));
    exists_case("after_value_deletes", L"a");

    setv(ab, L"vb2", 5);
    ST_CASE("delete_key_value", RegDeleteKeyValueW(root, L"a\\b", L"vb"));
    ST_CASE("delete_key_value_missing_key", RegDeleteKeyValueW(root, L"a\\zz", L"vb"));
    ST_CASE("delete_key_value_missing_value", RegDeleteKeyValueW(root, L"a\\b", L"nope"));
    exists_case("after_delete_key_value", L"a\\b");

    {   /* An open handle to a deleted key. */
        HKEY d = mk(L"doomed");
        setv(d, L"x", 1);
        ST_CASE("delete_open_key", RegDeleteKeyW(root, L"doomed"));
        DWORD v = 9;
        ST_CASE("set_value_on_deleted_key",
                RegSetValueExW(d, L"y", 0, REG_DWORD, (const BYTE *)&v, sizeof v));
        DWORD size = sizeof v;
        ST_CASE("query_value_on_deleted_key", RegQueryValueExW(d, L"x", NULL, NULL, (BYTE *)&v, &size));
        HKEY sub = NULL;
        ST_CASE("create_subkey_under_deleted_key",
                RegCreateKeyExW(d, L"sub", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &sub, NULL));
        if (sub) RegCloseKey(sub);
        wchar_t nm[16];
        DWORD n = 16;
        ST_CASE("enum_on_deleted_key", RegEnumValueW(d, 0, nm, &n, NULL, NULL, NULL, NULL));
        ST_CASE("close_deleted_key", RegCloseKey(d));
        ST_CASE("close_twice", RegCloseKey(d));
    }

    {   /* RegDeleteTreeW */
        HKEY t = mk(L"tree");
        HKEY t1 = mk(L"tree\\one\\deep");
        mk(L"tree\\two");
        setv(t, L"tv", 1);
        setv(t1, L"dv", 1);
        RegCloseKey(t1);
        ST_CASE("delete_tree_subkey", RegDeleteTreeW(root, L"tree\\one"));
        exists_case("after_delete_tree_subkey", L"tree");
        ST_CASE("delete_tree_null_subkey_keeps_key", RegDeleteTreeW(t, NULL));
        exists_case("after_delete_tree_null", L"tree");
        ST_CASE("delete_tree_missing", RegDeleteTreeW(root, L"tree\\nope"));
        RegCloseKey(t);
        ST_CASE("delete_tree_whole", RegDeleteTreeW(root, L"tree"));
        exists_case("after_delete_tree_whole", L"tree");
    }

    {   /* RegDeleteKeyExW with view flags. */
        mk(L"ex");
        ST_CASE("delete_key_ex_64", RegDeleteKeyExW(root, L"ex", KEY_WOW64_64KEY, 0));
        mk(L"ex2");
        ST_CASE("delete_key_ex_reserved_nonzero", RegDeleteKeyExW(root, L"ex2", 0, 1));
        exists_case("after_reserved_nonzero", L"ex2");
    }

    {   /* Open error paths. */
        HKEY k = NULL;
        ST_CASE("open_missing", RegOpenKeyExW(root, L"nope", 0, KEY_READ, &k));
        ST_CASE("open_leading_backslash", RegOpenKeyExW(root, L"\\a", 0, KEY_READ, &k));
        if (k) { RegCloseKey(k); k = NULL; }
        ST_CASE("open_trailing_backslash", RegOpenKeyExW(root, L"a\\", 0, KEY_READ, &k));
        if (k) { RegCloseKey(k); k = NULL; }
        ST_CASE("open_double_backslash", RegOpenKeyExW(root, L"a\\\\b", 0, KEY_READ, &k));
        if (k) { RegCloseKey(k); k = NULL; }
        ST_CASE("open_forward_slash_name", RegOpenKeyExW(root, L"a/b", 0, KEY_READ, &k));
        if (k) { RegCloseKey(k); k = NULL; }
        ST_CASE("open_null_subkey", RegOpenKeyExW(a, NULL, 0, KEY_READ, &k));
        if (k) { RegCloseKey(k); k = NULL; }
        ST_CASE("open_empty_subkey", RegOpenKeyExW(a, L"", 0, KEY_READ, &k));
        if (k) { RegCloseKey(k); k = NULL; }
        ST_CASE("open_bad_root", RegOpenKeyExW((HKEY)(ULONG_PTR)0x7ff0, L"a", 0, KEY_READ, &k));
        ST_CASE("open_null_root", RegOpenKeyExW(NULL, L"a", 0, KEY_READ, &k));
        ST_CASE("open_options_link", RegOpenKeyExW(root, L"a", REG_OPTION_OPEN_LINK, KEY_READ, &k));
        if (k) { RegCloseKey(k); k = NULL; }

        wchar_t longname[300];
        wmemset(longname, L'k', 299);
        longname[299] = 0;
        HKEY lk = NULL;
        ST_CASE("create_name_299_chars",
                RegCreateKeyExW(root, longname, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &lk, NULL));
        if (lk) RegCloseKey(lk);
        longname[255] = 0;
        lk = NULL;
        ST_CASE("create_name_255_chars",
                RegCreateKeyExW(root, longname, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &lk, NULL));
        if (lk) RegCloseKey(lk);
        longname[256] = L'k';
        longname[256] = 0;
        longname[255] = L'k';
        lk = NULL;
        ST_CASE("create_name_256_chars",
                RegCreateKeyExW(root, longname, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &lk, NULL));
        if (lk) RegCloseKey(lk);

        HKEY ro = NULL;
        RegOpenKeyExW(root, L"a", 0, KEY_READ, &ro);
        ST_CASE("delete_value_via_readonly_handle", RegDeleteValueW(ro, L"nothing"));
        DWORD v = 1;
        ST_CASE("set_value_via_readonly_handle",
                RegSetValueExW(ro, L"x", 0, REG_DWORD, (const BYTE *)&v, sizeof v));
        ST_CASE("delete_subkey_via_readonly_parent", RegDeleteKeyW(ro, L"b"));
        RegCloseKey(ro);
    }

    {   /* RegRenameKey (exported since Vista; looked up so a missing export is visible). */
        rename_fn ren = (rename_fn)(void *)GetProcAddress(GetModuleHandleW(L"advapi32.dll"), "RegRenameKey");
        oracle_case("rename_available");
        o_bool("available", ren != NULL);
        oracle_case_end();
        if (ren) {
            mk(L"old");
            mk(L"taken");
            ST_CASE("rename_key", ren(root, L"old", L"new"));
            exists_case("after_rename_old", L"old");
            exists_case("after_rename_new", L"new");
            ST_CASE("rename_onto_existing", ren(root, L"new", L"taken"));
            ST_CASE("rename_missing", ren(root, L"ghost", L"x"));
            ST_CASE("rename_with_backslash", ren(root, L"new", L"x\\y"));
        }
    }

    RegCloseKey(ab);
    RegCloseKey(a);
    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
