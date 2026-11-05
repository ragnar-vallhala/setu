/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: RegFlushKey and RegCloseKey on many kinds of handles: test
 * keys (non-volatile and volatile), read-only handles, handles with no
 * access, predefined keys, a deleted key, bad handles; and RegCloseKey
 * ordering (a child handle keeps working after its parent handle is
 * closed; DuplicateHandle copies of a key handle).
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regflushkey
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regclosekey
 *       https://learn.microsoft.com/en-us/windows/win32/api/handleapi/nf-handleapi-duplicatehandle
 */
#include "oracle.h"

static void flush_case(const char *name, HKEY k)
{
    LONG st;
    ORACLE_CALL(st, RegFlushKey(k));
    oracle_case(name);
    o_status("ret", st);
    o_lasterr();
    oracle_case_end();
}

int main(void)
{
    oracle_begin("advapi32/RegFlushKey");
    HKEY root = oracle_reg_root(), k, v, ro, none;

    RegCreateKeyExW(root, L"nv", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL);
    RegCreateKeyExW(root, L"vol", 0, NULL, REG_OPTION_VOLATILE, KEY_ALL_ACCESS, NULL, &v, NULL);
    RegOpenKeyExW(root, L"nv", 0, KEY_READ, &ro);
    RegOpenKeyExW(root, L"nv", 0, 0, &none);

    flush_case("nonvolatile", k);
    flush_case("volatile", v);
    flush_case("readonly_handle", ro);
    flush_case("no_access_handle", none);
    flush_case("hkcu", HKEY_CURRENT_USER);
    flush_case("hklm", HKEY_LOCAL_MACHINE);
    flush_case("hkcr", HKEY_CLASSES_ROOT);
    flush_case("hku", HKEY_USERS);
    flush_case("hkcc", HKEY_CURRENT_CONFIG);
    flush_case("hkpd", HKEY_PERFORMANCE_DATA);
    flush_case("null", NULL);
    flush_case("made_up", (HKEY)(ULONG_PTR)0x1234);
    RegDeleteKeyW(root, L"vol");
    flush_case("deleted_key", v);
    RegCloseKey(v);
    RegCloseKey(ro);
    RegCloseKey(none);

    {   /* Child handle outlives its parent handle. */
        HKEY p, c;
        RegCreateKeyExW(root, L"p", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &p, NULL);
        RegCreateKeyExW(p, L"c", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &c, NULL);
        LONG cp = RegCloseKey(p);
        DWORD val = 3;
        LONG s = RegSetValueExW(c, L"x", 0, REG_DWORD, (const BYTE *)&val, sizeof val);
        HKEY g = NULL;
        LONG g1 = RegCreateKeyExW(c, L"g", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &g, NULL);
        if (!g1) RegCloseKey(g);
        oracle_case("child_after_parent_closed");
        o_status("close_parent", cp);
        o_status("set_value_child", s);
        o_status("create_grandchild", g1);
        o_status("close_child", RegCloseKey(c));
        oracle_case_end();
    }
    {   /* DuplicateHandle copy of a key handle. */
        HANDLE dup = NULL;
        BOOL ok = DuplicateHandle(GetCurrentProcess(), k, GetCurrentProcess(), &dup, 0, FALSE, DUPLICATE_SAME_ACCESS);
        LONG c1 = RegCloseKey(k);
        DWORD n = 0;
        LONG q = RegQueryInfoKeyW((HKEY)dup, NULL, NULL, NULL, &n, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        oracle_case("duplicate_handle");
        o_bool("duplicated", ok);
        o_status("close_original", c1);
        o_status("query_via_duplicate", q);
        o_status("close_duplicate_with_regclosekey", RegCloseKey((HKEY)dup));
        o_bool("closehandle_after", CloseHandle(dup));
        oracle_case_end();
    }
    {   /* CloseHandle on a key handle. */
        HKEY h;
        RegOpenKeyExW(root, L"nv", 0, KEY_READ, &h);
        BOOL ok = CloseHandle(h);
        LONG again = RegCloseKey(h);
        oracle_case("closehandle_on_key");
        o_bool("closehandle", ok);
        o_status("regclosekey_after", again);
        oracle_case_end();
    }

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
