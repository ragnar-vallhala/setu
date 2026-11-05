/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: RegCreateKeyExA / RegOpenKeyExA: dispositions, ANSI names
 * (code page 1252 letters become Unicode key names), class strings, name
 * length limits counted in bytes, and error codes.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regcreatekeyexa
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regopenkeyexa
 *       https://learn.microsoft.com/en-us/windows/win32/sysinfo/registry-element-size-limits
 */
#include "oracle.h"

static HKEY root;

static void create_case(const char *name, HKEY parent, const char *sub, const char *cls, DWORD options, REGSAM sam)
{
    HKEY k = NULL;
    DWORD disp = 0xAAAAAAAAu;
    LONG st;
    ORACLE_CALL(st, RegCreateKeyExA(parent, sub, 0, (char *)cls, options, sam, NULL, &k, &disp));
    oracle_case(name);
    o_status("ret", st);
    o_lasterr();
    o_u32("disposition", disp);
    o_bool("got_key", k != NULL);
    oracle_case_end();
    if (!st && k) RegCloseKey(k);
}

static void open_case(const char *name, HKEY parent, const char *sub, REGSAM sam)
{
    HKEY k = NULL;
    LONG st;
    ORACLE_CALL(st, RegOpenKeyExA(parent, sub, 0, sam, &k));
    oracle_case(name);
    o_status("ret", st);
    o_lasterr();
    o_bool("got_key", k != NULL);
    oracle_case_end();
    if (!st && k) RegCloseKey(k);
}

/* Opens a key by its Unicode name and reports whether it exists. */
static void wide_exists_case(const char *name, const wchar_t *sub)
{
    HKEY k = NULL;
    LONG st = RegOpenKeyExW(root, sub, 0, KEY_READ, &k);
    oracle_case(name);
    o_status("open_w", st);
    oracle_case_end();
    if (!st) RegCloseKey(k);
}

static void class_case(const char *name, const char *sub)
{
    HKEY k = NULL;
    char cls[32];
    DWORD n = sizeof cls;
    LONG st = RegOpenKeyExA(root, sub, 0, KEY_READ, &k);
    if (!st) {
        memset(cls, '#', sizeof cls);
        st = RegQueryInfoKeyA(k, cls, &n, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        RegCloseKey(k);
    }
    oracle_case(name);
    o_status("ret", st);
    if (!st) {
        o_str("class", cls);
        o_u32("class_len", n);
    }
    oracle_case_end();
}

int main(void)
{
    oracle_begin("advapi32/RegCreateKeyExA");
    root = oracle_reg_root();

    create_case("create_new", root, "Alpha", NULL, 0, KEY_ALL_ACCESS);
    create_case("create_existing", root, "Alpha", NULL, 0, KEY_ALL_ACCESS);
    create_case("create_existing_upper", root, "ALPHA", NULL, 0, KEY_READ);
    create_case("create_multi_level", root, "One\\Two\\Three", NULL, 0, KEY_ALL_ACCESS);
    create_case("create_trailing_backslash", root, "Trail\\", NULL, 0, KEY_ALL_ACCESS);
    create_case("create_leading_backslash", root, "\\Lead", NULL, 0, KEY_ALL_ACCESS);
    create_case("create_double_backslash", root, "Dbl\\\\Inner", NULL, 0, KEY_ALL_ACCESS);
    create_case("create_empty", root, "", NULL, 0, KEY_ALL_ACCESS);
    create_case("create_null_subkey", root, NULL, NULL, 0, KEY_ALL_ACCESS);
    create_case("create_with_class", root, "Classy", "ClassA", 0, KEY_ALL_ACCESS);
    create_case("create_existing_new_class", root, "Classy", "Other", 0, KEY_ALL_ACCESS);
    class_case("class_kept_from_first_create", "Classy");
    create_case("create_empty_class", root, "EmptyClass", "", 0, KEY_ALL_ACCESS);
    class_case("class_empty", "EmptyClass");
    create_case("create_invalid_hkey", (HKEY)(ULONG_PTR)0x1234, "X", NULL, 0, KEY_ALL_ACCESS);
    create_case("create_null_hkey", NULL, "X", NULL, 0, KEY_ALL_ACCESS);
    create_case("create_volatile", root, "Vol", NULL, REG_OPTION_VOLATILE, KEY_ALL_ACCESS);
    create_case("create_nonvolatile_under_volatile", root, "Vol\\Child", NULL, 0, KEY_ALL_ACCESS);

    /* cp1252 letters: e-acute (0xe9) and the euro sign (0x80 -> U+20AC). */
    create_case("create_cp1252_name", root, "caf\xe9\x80", NULL, 0, KEY_ALL_ACCESS);
    wide_exists_case("cp1252_name_as_unicode", L"café€");
    open_case("open_cp1252_upper", root, "CAF\xc9\x80", KEY_READ);

    {   /* Length limits: 255 and 256 single-byte characters, and a 299-char name. */
        char n255[300], n256[300], n299[300];
        memset(n255, 'k', 255); n255[255] = 0;
        memset(n256, 'm', 256); n256[256] = 0;
        memset(n299, 'z', 299); n299[299] = 0;
        create_case("create_name_255", root, n255, NULL, 0, KEY_ALL_ACCESS);
        create_case("create_name_256", root, n256, NULL, 0, KEY_ALL_ACCESS);
        create_case("create_name_299", root, n299, NULL, 0, KEY_ALL_ACCESS);
        open_case("open_name_255", root, n255, KEY_READ);
        open_case("open_name_299", root, n299, KEY_READ);
    }

    open_case("open_existing", root, "Alpha", KEY_READ);
    open_case("open_lower_multi", root, "one\\two\\three", KEY_READ);
    open_case("open_missing", root, "Missing", KEY_READ);
    open_case("open_missing_parent", root, "Missing\\Child", KEY_READ);
    open_case("open_null_subkey", root, NULL, KEY_READ);
    open_case("open_empty_subkey", root, "", KEY_READ);
    open_case("open_leading_backslash", root, "\\Alpha", KEY_READ);
    open_case("open_trailing_backslash", root, "Alpha\\", KEY_READ);
    open_case("open_invalid_hkey", (HKEY)(ULONG_PTR)0x1234, "Alpha", KEY_READ);
    {
        HKEY k = NULL;
        LONG st;
        ORACLE_CALL(st, RegOpenKeyExA(root, "Alpha", 1, KEY_READ, &k));
        oracle_case("open_options_1");
        o_status("ret", st);
        o_lasterr();
        oracle_case_end();
        if (!st) RegCloseKey(k);
    }

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
