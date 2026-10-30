/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: RegCreateKeyExW / RegOpenKeyExW / RegDeleteKeyW dispositions,
 * case-insensitive key names, multi-level creation, and error codes.
 *
 * All work happens under HKCU\Software\SetuOracle\advapi32_RegCreateKeyExW,
 * which is wiped before and after the run.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regcreatekeyexw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regopenkeyexw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regdeletekeyw
 *       https://learn.microsoft.com/en-us/windows/win32/sysinfo/registry-element-size-limits
 */
#include "oracle.h"

static HKEY root;

static void create_case(const char *name, HKEY parent, const wchar_t *sub, DWORD options, REGSAM sam)
{
    HKEY k = NULL;
    DWORD disp = 0xAAAAAAAAu;
    LONG st;
    ORACLE_CALL(st, RegCreateKeyExW(parent, sub, 0, NULL, options, sam, NULL, &k, &disp));
    oracle_case(name);
    o_status("ret", st);
    o_lasterr();
    o_u32("disposition", disp);
    o_bool("got_key", k != NULL);
    oracle_case_end();
    if (!st && k) RegCloseKey(k);
}

static void open_case(const char *name, HKEY parent, const wchar_t *sub, REGSAM sam)
{
    HKEY k = NULL;
    LONG st;
    ORACLE_CALL(st, RegOpenKeyExW(parent, sub, 0, sam, &k));
    oracle_case(name);
    o_status("ret", st);
    o_lasterr();
    o_bool("got_key", k != NULL);
    oracle_case_end();
    if (!st && k) RegCloseKey(k);
}

static void delete_case(const char *name, HKEY parent, const wchar_t *sub)
{
    LONG st;
    ORACLE_CALL(st, RegDeleteKeyW(parent, sub));
    oracle_case(name);
    o_status("ret", st);
    o_lasterr();
    oracle_case_end();
}

int main(void)
{
    oracle_begin("advapi32/RegCreateKeyExW");
    root = oracle_reg_root();

    create_case("create_new", root, L"Alpha", 0, KEY_ALL_ACCESS);
    create_case("create_existing", root, L"Alpha", 0, KEY_ALL_ACCESS);
    create_case("create_existing_other_case", root, L"ALPHA", 0, KEY_READ);
    create_case("create_multi_level", root, L"One\\Two\\Three", 0, KEY_ALL_ACCESS);
    create_case("create_trailing_backslash", root, L"Trail\\", 0, KEY_ALL_ACCESS);
    create_case("create_leading_backslash", root, L"\\Lead", 0, KEY_ALL_ACCESS);
    create_case("create_double_backslash", root, L"Dbl\\\\Inner", 0, KEY_ALL_ACCESS);
    create_case("create_empty_subkey", root, L"", 0, KEY_ALL_ACCESS);
    create_case("create_volatile", root, L"Vol", REG_OPTION_VOLATILE, KEY_ALL_ACCESS);
    create_case("create_nonvolatile_under_volatile", root, L"Vol\\Child", 0, KEY_ALL_ACCESS);
    create_case("create_invalid_hkey", (HKEY)(ULONG_PTR)0x1234, L"X", 0, KEY_ALL_ACCESS);
    {   /* Key name of exactly 255 and 256 characters (documented limit: 255). */
        wchar_t n255[257], n256[258];
        wmemset(n255, L'k', 255); n255[255] = 0;
        wmemset(n256, L'k', 256); n256[256] = 0;
        create_case("create_name_255_chars", root, n255, 0, KEY_ALL_ACCESS);
        create_case("create_name_256_chars", root, n256, 0, KEY_ALL_ACCESS);
    }

    open_case("open_existing", root, L"Alpha", KEY_READ);
    open_case("open_other_case_multi", root, L"one\\TWO\\three", KEY_READ);
    open_case("open_missing", root, L"Missing", KEY_READ);
    open_case("open_missing_parent", root, L"Missing\\Child", KEY_READ);
    open_case("open_null_subkey", root, NULL, KEY_READ);

    {   /* Creating a subkey through a read-only handle. */
        HKEY ro = NULL;
        RegOpenKeyExW(root, L"Alpha", 0, KEY_READ, &ro);
        create_case("create_via_readonly_handle", ro, L"Sub", 0, KEY_ALL_ACCESS);
        RegCloseKey(ro);
    }

    delete_case("delete_with_subkeys", root, L"One");
    delete_case("delete_leaf", root, L"One\\Two\\Three");
    delete_case("delete_missing", root, L"Nope");
    delete_case("delete_other_case", root, L"ALPHA");
    open_case("open_after_delete", root, L"Alpha", KEY_READ);

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
