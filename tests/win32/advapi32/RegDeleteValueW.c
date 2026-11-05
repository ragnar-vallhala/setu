/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: RegDeleteValueW and RegDeleteValueA: existing, missing and
 * already-deleted values, NULL and "" for the default value (set and
 * unset), case-insensitive names, a name that looks like a path, handles
 * without KEY_SET_VALUE, a deleted key, invalid handles, and the value
 * count afterwards.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regdeletevaluew
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regdeletevaluea
 */
#include "oracle.h"

#define BASE ORACLE_REG_BASE L"\\advapi32_RegDeleteValueW"

static HKEY root;

static void put(HKEY k, const wchar_t *name)
{
    DWORD dw = 1;
    RegSetValueExW(k, name, 0, REG_DWORD, (const BYTE *)&dw, 4);
}

static void after(void)
{
    DWORD n = 0;
    RegQueryInfoKeyW(root, NULL, NULL, NULL, NULL, NULL, NULL, &n, NULL, NULL, NULL, NULL);
    o_u32("values_after", n);
}

static void w_case(const char *cname, HKEY key, const wchar_t *value)
{
    LONG st;
    ORACLE_CALL(st, RegDeleteValueW(key, value));
    oracle_case(cname);
    o_status("ret", st);
    o_lasterr();
    after();
    oracle_case_end();
}

static void a_case(const char *cname, HKEY key, const char *value)
{
    LONG st;
    ORACLE_CALL(st, RegDeleteValueA(key, value));
    oracle_case(cname);
    o_status("ret", st);
    o_lasterr();
    after();
    oracle_case_end();
}

int main(void)
{
    oracle_begin("advapi32/RegDeleteValueW");
    root = oracle_reg_root();
    put(root, L"A");
    put(root, L"Mixed");
    put(root, L"x\\y");
    put(root, L"Ansi");
    put(root, L"AnsiMixed");
    put(root, L"Ro");

    w_case("existing", root, L"A");
    w_case("already_deleted", root, L"A");
    w_case("missing", root, L"Missing");
    w_case("other_case", root, L"MIXED");
    w_case("path_like_name", root, L"x\\y");
    w_case("default_unset_null", root, NULL);
    w_case("default_unset_empty", root, L"");
    put(root, NULL);
    w_case("default_set_null", root, NULL);
    put(root, NULL);
    w_case("default_set_empty", root, L"");
    {
        HKEY k = NULL;
        RegOpenKeyExW(HKEY_CURRENT_USER, BASE, 0, KEY_READ, &k);
        w_case("readonly_handle", k, L"Ro");
        RegCloseKey(k);
        RegOpenKeyExW(HKEY_CURRENT_USER, BASE, 0, KEY_QUERY_VALUE, &k);
        w_case("query_only_handle", k, L"Ro");
        RegCloseKey(k);
        RegOpenKeyExW(HKEY_CURRENT_USER, BASE, 0, KEY_SET_VALUE, &k);
        w_case("set_only_handle", k, L"Ro");
        RegCloseKey(k);
    }
    w_case("invalid_handle", (HKEY)(ULONG_PTR)0x1234, L"Ansi");
    w_case("null_handle", NULL, L"Ansi");
    a_case("ansi_existing", root, "Ansi");
    a_case("ansi_missing", root, "Ansi");
    a_case("ansi_other_case", root, "ANSIMIXED");
    a_case("ansi_default_null_unset", root, NULL);
    put(root, NULL);
    a_case("ansi_default_empty_set", root, "");
    {
        HKEY s = NULL, k = NULL;
        RegCreateKeyExW(root, L"sub", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &s, NULL);
        put(s, L"V");
        RegOpenKeyExW(root, L"sub", 0, KEY_ALL_ACCESS, &k);
        RegDeleteKeyW(root, L"sub");
        LONG st;
        ORACLE_CALL(st, RegDeleteValueW(k, L"V"));
        oracle_case("deleted_key");
        o_status("ret", st);
        o_lasterr();
        oracle_case_end();
        RegCloseKey(k);
        RegCloseKey(s);
    }

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
