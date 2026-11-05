/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: path spellings accepted by RegOpenKeyExW for an existing
 * key a\b\c under the test key: forward slashes, "." and ".." components,
 * leading/trailing spaces, repeated and trailing backslashes, a lone
 * backslash, mixed case, a path that goes through a value name, a path of
 * only backslashes, and the same spellings with RegCreateKeyExW (which
 * names get created). Each create case lists the children afterwards.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regopenkeyexw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regcreatekeyexw
 *       https://learn.microsoft.com/en-us/windows/win32/sysinfo/structure-of-the-registry
 */
#include "oracle.h"

static HKEY root;

static void list(const char *field, HKEY k, const wchar_t *sub)
{
    char out[512] = "";
    HKEY s = k;
    if (sub && RegOpenKeyExW(k, sub, 0, KEY_READ, &s)) {
        o_str(field, "(missing)");
        return;
    }
    for (DWORD i = 0;; i++) {
        wchar_t nm[64];
        DWORD n = 64;
        if (RegEnumKeyExW(s, i, nm, &n, NULL, NULL, NULL, NULL)) break;
        if (i) strcat(out, "|");
        strcat(out, "<");
        strcat(out, oracle_utf8(nm));
        strcat(out, ">");
    }
    if (s != k) RegCloseKey(s);
    o_str(field, out);
}

static void open_case(const char *name, const wchar_t *path)
{
    HKEY k = NULL;
    LONG st;
    ORACLE_CALL(st, RegOpenKeyExW(root, path, 0, KEY_READ, &k));
    oracle_case(name);
    o_status("ret", st);
    o_lasterr();
    if (!st) {
        DWORD n = 99;
        RegQueryInfoKeyW(k, NULL, NULL, NULL, &n, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        o_u32("subkeys", n);
        RegCloseKey(k);
    }
    oracle_case_end();
}

static void create_case(const char *name, const wchar_t *path)
{
    HKEY k = NULL;
    DWORD disp = 0;
    LONG st;
    ORACLE_CALL(st, RegCreateKeyExW(root, path, 0, NULL, REG_OPTION_VOLATILE, KEY_ALL_ACCESS, NULL, &k, &disp));
    oracle_case(name);
    o_status("ret", st);
    o_lasterr();
    if (!st) {
        o_u32("disposition", disp);
        RegCloseKey(k);
    }
    list("root", root, NULL);
    list("a", root, L"a");
    oracle_case_end();
}

int main(void)
{
    oracle_begin("advapi32/RegOpenKeyEx_paths");
    root = oracle_reg_root();
    HKEY k;
    RegCreateKeyExW(root, L"a\\b\\c", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL);
    RegCloseKey(k);
    RegCreateKeyExW(root, L"a", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL);
    RegSetValueExW(k, L"val", 0, REG_SZ, (const BYTE *)L"x", 4);
    RegCloseKey(k);

    open_case("plain", L"a\\b\\c");
    open_case("mixed_case", L"A\\B\\C");
    open_case("forward_slashes", L"a/b/c");
    open_case("dot_component", L"a\\.\\b");
    open_case("dotdot_component", L"a\\b\\..\\b");
    open_case("double_backslash", L"a\\\\b");
    open_case("trailing_backslashes", L"a\\b\\\\\\");
    open_case("lone_backslash", L"\\");
    open_case("only_backslashes", L"\\\\\\");
    open_case("leading_space", L" a");
    open_case("trailing_space", L"a ");
    open_case("space_component", L"a\\ \\b");
    open_case("through_value_name", L"a\\val");
    open_case("tab_instead_of_backslash", L"a\tb");
    open_case("unicode_backslash_lookalike", L"a\x2216" L"b");
    open_case("fullwidth_backslash", L"a\xff3c" L"b");

    create_case("create_forward_slashes", L"a/b/c");
    create_case("create_dot_component", L"a\\.\\d");
    create_case("create_dotdot_component", L"a\\..");
    create_case("create_trailing_space", L"a ");
    create_case("create_space_component", L"a\\ ");
    create_case("create_named_like_value", L"a\\val");

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
