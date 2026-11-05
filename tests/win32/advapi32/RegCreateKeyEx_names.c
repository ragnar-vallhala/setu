/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: which key names RegCreateKeyExW accepts, and what name the new
 * key gets (read back with RegEnumKeyExW): backslashes (leading, trailing,
 * doubled, only backslashes), spaces, dots, wildcards, slashes, control
 * characters and an embedded NUL in a counted name (NtCreateKey-style names
 * are not reachable through this API, so only the C-string part counts).
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regcreatekeyexw
 *       https://learn.microsoft.com/en-us/windows/win32/sysinfo/structure-of-the-registry
 *       https://learn.microsoft.com/en-us/windows/win32/sysinfo/registry-element-size-limits
 */
#include "oracle.h"

static HKEY root;

/* Lists the subkeys of `k` in enumeration order, names escaped as \uXXXX
 * when not printable ASCII. */
static void list_into(HKEY k, char *out, size_t cap)
{
    out[0] = 0;
    for (DWORD i = 0;; i++) {
        wchar_t name[300];
        DWORD n = 300;
        if (RegEnumKeyExW(k, i, name, &n, NULL, NULL, NULL, NULL)) break;
        if (i) strncat(out, "|", cap - strlen(out) - 1);
        for (DWORD j = 0; j < n; j++) {
            char t[16];
            if (name[j] >= 0x20 && name[j] < 0x7f) { t[0] = (char)name[j]; t[1] = 0; }
            else snprintf(t, sizeof t, "\\u%04x", name[j]);
            strncat(out, t, cap - strlen(out) - 1);
        }
    }
}

/* Creates `sub` under a fresh container key and reports the result and the
 * container's subkey listing (one level, and the next level under the first). */
static void name_case(const char *name, const wchar_t *sub)
{
    HKEY c = NULL, k = NULL;
    DWORD disp = 0;
    char list[1024], list2[1024] = "";
    RegCreateKeyExW(root, L"c", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &c, NULL);
    LONG st;
    ORACLE_CALL(st, RegCreateKeyExW(c, sub, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, &disp));
    if (!st) RegCloseKey(k);
    list_into(c, list, sizeof list);
    {
        wchar_t first[300];
        DWORD n = 300;
        HKEY f;
        if (!RegEnumKeyExW(c, 0, first, &n, NULL, NULL, NULL, NULL) && !RegOpenKeyExW(c, first, 0, KEY_READ, &f)) {
            list_into(f, list2, sizeof list2);
            RegCloseKey(f);
        }
    }
    oracle_case(name);
    o_status("ret", st);
    o_lasterr();
    if (!st) o_u32("disposition", disp);
    o_str("children", list);
    o_str("grandchildren", list2);
    oracle_case_end();
    RegCloseKey(c);
    RegDeleteTreeW(root, L"c");
    RegDeleteKeyW(root, L"c");
}

static void open_case(const char *name, const wchar_t *sub)
{
    HKEY k = NULL;
    LONG st;
    ORACLE_CALL(st, RegOpenKeyExW(root, sub, 0, KEY_READ, &k));
    oracle_case(name);
    o_status("ret", st);
    o_lasterr();
    oracle_case_end();
    if (!st) RegCloseKey(k);
}

int main(void)
{
    oracle_begin("advapi32/RegCreateKeyEx_names");
    root = oracle_reg_root();

    name_case("plain", L"plain");
    name_case("trailing_backslash", L"tb\\");
    name_case("two_trailing_backslashes", L"tb2\\\\");
    name_case("leading_backslash", L"\\lead");
    name_case("double_backslash_middle", L"a\\\\b");
    name_case("triple_backslash_middle", L"a\\\\\\b");
    name_case("only_backslash", L"\\");
    name_case("only_two_backslashes", L"\\\\");
    name_case("space_name", L" ");
    name_case("leading_trailing_spaces", L"  sp  ");
    name_case("space_component", L"x\\ \\y");
    name_case("dot", L".");
    name_case("dotdot", L"..");
    name_case("dot_component", L"x\\.\\y");
    name_case("dotdot_component", L"x\\..\\y");
    name_case("trailing_dot", L"td.");
    name_case("wildcards", L"w*?");
    name_case("forward_slash", L"f/s");
    name_case("colon", L"c:d");
    name_case("quote_lt_gt_pipe", L"q\"<>|");
    name_case("tab", L"t\tab");
    name_case("newline", L"n\nl");
    name_case("control_01", L"c\x01");
    name_case("del_7f", L"d\x7f");
    name_case("nbsp", L"n b");
    name_case("ideographic_space", L"i　s");
    name_case("bom_char", L"﻿bom");
    name_case("lone_high_surrogate", L"h\xd800");
    name_case("lone_low_surrogate", L"l\xdc00");
    name_case("surrogate_pair", L"p\xd83d\xde00");
    name_case("noncharacter_ffff", L"x\xffff");
    name_case("long_multi_level", L"l1\\l2\\l3\\l4\\l5\\l6\\l7\\l8\\l9\\l10\\l11\\l12\\l13\\l14\\l15\\l16");

    {   /* Open with variants of a name, created once. */
        HKEY k;
        RegCreateKeyExW(root, L"o\\p", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL);
        RegCloseKey(k);
        open_case("open_trailing_backslash", L"o\\p\\");
        open_case("open_two_trailing_backslashes", L"o\\p\\\\");
        open_case("open_double_backslash", L"o\\\\p");
        open_case("open_leading_backslash", L"\\o\\p");
        open_case("open_only_backslash", L"\\");
        open_case("open_dot_component", L"o\\.\\p");
        open_case("open_trailing_space", L"o\\p ");
        open_case("open_forward_slash", L"o/p");
    }

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
