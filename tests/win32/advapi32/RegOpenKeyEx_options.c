/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: RegOpenKeyExW ulOptions values, what *phkResult holds after a
 * failure (unchanged or NULL), opening the same key twice (distinct handles),
 * and opening through a handle that was itself opened with a subkey path.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regopenkeyexw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regcreatekeyexw
 */
#include "oracle.h"

#define SENT ((HKEY)(ULONG_PTR)0x5E5E5E50u)

static HKEY root;

static void open_case(const char *name, HKEY parent, const wchar_t *sub, DWORD options, REGSAM sam)
{
    HKEY k = SENT;
    LONG st;
    ORACLE_CALL(st, RegOpenKeyExW(parent, sub, options, sam, &k));
    oracle_case(name);
    o_status("ret", st);
    o_lasterr();
    o_str("result", k == SENT ? "unchanged" : k == NULL ? "NULL" : "key");
    oracle_case_end();
    if (!st && k && k != SENT) RegCloseKey(k);
}

static void create_fail_case(const char *name, HKEY parent, const wchar_t *sub, DWORD options)
{
    HKEY k = SENT;
    DWORD disp = 0xAAAAAAAAu;
    LONG st;
    ORACLE_CALL(st, RegCreateKeyExW(parent, sub, 0, NULL, options, KEY_ALL_ACCESS, NULL, &k, &disp));
    oracle_case(name);
    o_status("ret", st);
    o_lasterr();
    o_str("result", k == SENT ? "unchanged" : k == NULL ? "NULL" : "key");
    o_u32("disposition", disp);
    oracle_case_end();
    if (!st && k && k != SENT) RegCloseKey(k);
}

int main(void)
{
    oracle_begin("advapi32/RegOpenKeyEx_options");
    root = oracle_reg_root();
    HKEY a;
    RegCreateKeyExW(root, L"a\\b", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &a, NULL);
    RegCloseKey(a);

    open_case("options_0", root, L"a", 0, KEY_READ);
    open_case("options_volatile_bit", root, L"a", REG_OPTION_VOLATILE, KEY_READ);
    open_case("options_create_link_bit", root, L"a", REG_OPTION_CREATE_LINK, KEY_READ);
    open_case("options_backup_restore", root, L"a", REG_OPTION_BACKUP_RESTORE, KEY_READ);
    open_case("options_open_link", root, L"a", REG_OPTION_OPEN_LINK, KEY_READ);
    open_case("options_dont_virtualize", root, L"a", 0x10 /* REG_OPTION_DONT_VIRTUALIZE */, KEY_READ);
    open_case("options_0x20", root, L"a", 0x20, KEY_READ);
    open_case("options_0x80000000", root, L"a", 0x80000000u, KEY_READ);
    open_case("options_all_bits", root, L"a", 0xFFFFFFFFu, KEY_READ);
    open_case("options_open_link_missing", root, L"zz", REG_OPTION_OPEN_LINK, KEY_READ);

    open_case("fail_missing_result", root, L"missing", 0, KEY_READ);
    open_case("fail_bad_parent_result", (HKEY)(ULONG_PTR)0x1234, L"a", 0, KEY_READ);
    open_case("fail_null_parent_result", NULL, L"a", 0, KEY_READ);
    open_case("fail_leading_backslash_result", root, L"\\a", 0, KEY_READ);
    create_fail_case("create_fail_bad_parent_result", (HKEY)(ULONG_PTR)0x1234, L"a", 0);
    create_fail_case("create_fail_leading_backslash_result", root, L"\\x", 0);
    create_fail_case("create_fail_bad_options_result", root, L"x", 0x20);

    {   /* Opening the same key twice gives two handles; closing one keeps the other. */
        HKEY h1 = NULL, h2 = NULL;
        RegOpenKeyExW(root, L"a", 0, KEY_READ, &h1);
        RegOpenKeyExW(root, L"a", 0, KEY_READ, &h2);
        oracle_case("open_twice_distinct");
        o_bool("distinct", h1 != h2);
        RegCloseKey(h1);
        DWORD n = 0;
        o_status("second_still_works",
                 RegQueryInfoKeyW(h2, NULL, NULL, NULL, &n, NULL, NULL, NULL, NULL, NULL, NULL, NULL));
        o_u32("subkeys", n);
        oracle_case_end();
        RegCloseKey(h2);
    }
    {   /* Relative open through a child handle, and NULL/empty subkey through it. */
        HKEY h = NULL;
        RegOpenKeyExW(root, L"a", 0, KEY_READ, &h);
        open_case("relative_open", h, L"b", 0, KEY_READ);
        open_case("relative_open_other_case", h, L"B", 0, KEY_READ);
        open_case("relative_null_subkey", h, NULL, 0, KEY_READ);
        open_case("relative_empty_subkey", h, L"", 0, KEY_READ);
        open_case("relative_parent_dotdot", h, L"..", 0, KEY_READ);
        RegCloseKey(h);
    }
    {   /* NULL phkResult. */
        LONG st;
        ORACLE_CALL(st, RegOpenKeyExW(root, L"a", 0, KEY_READ, NULL));
        oracle_case("null_result_pointer");
        o_status("ret", st);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(st, RegCreateKeyExW(root, L"a", 0, NULL, 0, KEY_READ, NULL, NULL, NULL));
        oracle_case("create_null_result_pointer");
        o_status("ret", st);
        o_lasterr();
        oracle_case_end();
    }

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
