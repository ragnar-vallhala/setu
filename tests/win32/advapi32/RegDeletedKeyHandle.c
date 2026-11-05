/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: what each key API returns on a handle whose key was deleted
 * (ERROR_KEY_DELETED, or something else?), whether a pending change
 * notification fires on deletion, handles to keys deleted by RegDeleteTreeW
 * from above, and whether re-creating a key with the same name revives the
 * old handle.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regdeletekeyw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regnotifychangekeyvalue
 *       https://learn.microsoft.com/en-us/windows/win32/debug/system-error-codes--1000-1299-
 */
#include "oracle.h"

typedef LONG(WINAPI *rename_fn)(HKEY, LPCWSTR, LPCWSTR);
static HKEY root;

static HKEY mk(const wchar_t *path)
{
    HKEY k = NULL;
    DWORD v = 1;
    RegCreateKeyExW(root, path, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL);
    RegSetValueExW(k, L"v", 0, REG_DWORD, (const BYTE *)&v, sizeof v);
    return k;
}

static void ops(const char *name, HKEY d)
{
    rename_fn pRename = (rename_fn)(void (*)(void))GetProcAddress(GetModuleHandleW(L"advapi32.dll"), "RegRenameKey");
    HKEY k = NULL;
    wchar_t nm[32];
    DWORD n = 32, v = 1, sz = sizeof v;
    oracle_case(name);
    DWORD nsub = 77, nval = 77;
    o_status("query_info", RegQueryInfoKeyW(d, NULL, NULL, NULL, &nsub, NULL, NULL, &nval, NULL, NULL, NULL, NULL));
    o_u32("query_info_subkeys", nsub);
    o_status("enum_key", RegEnumKeyExW(d, 0, nm, &n, NULL, NULL, NULL, NULL));
    n = 32;
    o_status("enum_value", RegEnumValueW(d, 0, nm, &n, NULL, NULL, NULL, NULL));
    o_status("query_value", RegQueryValueExW(d, L"v", NULL, NULL, (BYTE *)&v, &sz));
    o_status("set_value", RegSetValueExW(d, L"w", 0, REG_DWORD, (const BYTE *)&v, sizeof v));
    o_status("delete_value", RegDeleteValueW(d, L"v"));
    LONG st = RegOpenKeyExW(d, NULL, 0, KEY_READ, &k);
    o_status("open_null_subkey", st);
    if (!st) {
        nsub = 77;
        o_status("reopened_query_info", RegQueryInfoKeyW(k, NULL, NULL, NULL, &nsub, NULL, NULL, NULL, NULL, NULL, NULL, NULL));
        RegCloseKey(k);
    }
    k = NULL;
    st = RegOpenKeyExW(d, L"child", 0, KEY_READ, &k);
    o_status("open_child", st);
    if (!st) RegCloseKey(k);
    k = NULL;
    st = RegCreateKeyExW(d, L"", 0, NULL, 0, KEY_READ, NULL, &k, NULL);
    o_status("create_empty_subkey", st);
    if (!st) RegCloseKey(k);
    k = NULL;
    st = RegCreateKeyExW(d, L"newchild", 0, NULL, 0, KEY_READ, NULL, &k, NULL);
    o_status("create_child", st);
    if (!st) RegCloseKey(k);
    o_status("delete_self_again", RegDeleteKeyW(d, L""));
    o_status("delete_tree_null", RegDeleteTreeW(d, NULL));
    o_status("flush", RegFlushKey(d));
    char sd[512];
    DWORD sdlen = sizeof sd;
    o_status("get_security", RegGetKeySecurity(d, OWNER_SECURITY_INFORMATION, sd, &sdlen));
    {   /* Asynchronous, so a call that succeeds cannot block the test. */
        HANDLE ev = CreateEventW(NULL, TRUE, FALSE, NULL);
        o_status("notify", RegNotifyChangeKeyValue(d, FALSE, REG_NOTIFY_CHANGE_NAME, ev, TRUE));
        CloseHandle(ev);
    }
    {
        HKEY dst = NULL;
        RegCreateKeyExW(root, L"copydst", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &dst, NULL);
        o_status("copy_tree_from", RegCopyTreeW(d, NULL, dst));
        RegCloseKey(dst);
        RegDeleteTreeW(root, L"copydst");
    }
    if (pRename) o_status("rename_self", pRename(d, NULL, L"renamed"));
    oracle_case_end();
}

int main(void)
{
    oracle_begin("advapi32/RegDeletedKeyHandle");
    root = oracle_reg_root();

    {   /* Leaf deleted with RegDeleteKeyW through another handle. */
        HKEY d = mk(L"leaf");
        HANDLE ev = CreateEventW(NULL, TRUE, FALSE, NULL);
        LONG nst = RegNotifyChangeKeyValue(d, TRUE, REG_NOTIFY_CHANGE_NAME | REG_NOTIFY_CHANGE_LAST_SET, ev, TRUE);
        LONG st;
        ORACLE_CALL(st, RegDeleteKeyW(root, L"leaf"));
        oracle_case("delete_leaf");
        o_status("notify_armed", nst);
        o_status("ret", st);
        o_lasterr();
        o_bool("notify_signaled", WaitForSingleObject(ev, 500) == WAIT_OBJECT_0);
        oracle_case_end();
        CloseHandle(ev);
        ops("ops_on_deleted_leaf", d);
        HKEY again = mk(L"leaf");
        oracle_case("recreated_same_name");
        o_bool("distinct_handle", again != d);
        DWORD nval = 77;
        o_status("old_handle_query_info", RegQueryInfoKeyW(d, NULL, NULL, NULL, NULL, NULL, NULL, &nval, NULL, NULL, NULL, NULL));
        nval = 77;
        o_status("new_handle_query_info", RegQueryInfoKeyW(again, NULL, NULL, NULL, NULL, NULL, NULL, &nval, NULL, NULL, NULL, NULL));
        o_u32("new_values", nval);
        oracle_case_end();
        oracle_case("close_deleted");
        o_status("close", RegCloseKey(d));
        oracle_case_end();
        RegCloseKey(again);
    }
    {   /* Key deleted through its own handle (empty subkey). */
        HKEY d = mk(L"self");
        LONG st;
        ORACLE_CALL(st, RegDeleteKeyW(d, L""));
        oracle_case("delete_self");
        o_status("ret", st);
        oracle_case_end();
        ops("ops_on_self_deleted", d);
        RegCloseKey(d);
    }
    {   /* Descendants deleted by RegDeleteTreeW from above. */
        HKEY mid = mk(L"tree\\mid");
        HKEY low = mk(L"tree\\mid\\low");
        LONG st;
        ORACLE_CALL(st, RegDeleteTreeW(root, L"tree"));
        oracle_case("delete_tree");
        o_status("ret", st);
        oracle_case_end();
        ops("ops_on_tree_deleted_mid", mid);
        ops("ops_on_tree_deleted_low", low);
        RegCloseKey(mid);
        RegCloseKey(low);
    }
    {   /* Handle to a key whose subtree was emptied (RegDeleteTreeW NULL): still alive. */
        HKEY t = mk(L"emptied");
        HKEY c = mk(L"emptied\\c");
        RegDeleteTreeW(t, NULL);
        ops("ops_on_child_of_emptied", c);
        RegCloseKey(c);
        oracle_case("emptied_key_alive");
        DWORD nsub = 77;
        o_status("query_info", RegQueryInfoKeyW(t, NULL, NULL, NULL, &nsub, NULL, NULL, NULL, NULL, NULL, NULL, NULL));
        o_u32("subkeys", nsub);
        oracle_case_end();
        RegCloseKey(t);
    }

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
