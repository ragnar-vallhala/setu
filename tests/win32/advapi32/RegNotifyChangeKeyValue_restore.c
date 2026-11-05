/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: which RegNotifyChangeKeyValue filters fire when the watched
 * key's contents are replaced by RegRestoreKey (on the key itself and on its
 * parent with a subtree watch), and when a hive is loaded and unloaded under
 * HKEY_USERS while HKEY_USERS... is not watched (the loaded hive's root is
 * watched instead, then unloaded). Hive files are only written by RegSaveKey
 * and read back; their bytes are never examined.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regnotifychangekeyvalue
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regrestorekeyw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regloadkeyw
 */
#include "oracle.h"

static HKEY root;

static void set_privilege(const wchar_t *name)
{
    HANDLE tok;
    TOKEN_PRIVILEGES tp = {1, {{{0, 0}, SE_PRIVILEGE_ENABLED}}};
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &tok)) return;
    LookupPrivilegeValueW(NULL, name, &tp.Privileges[0].Luid);
    AdjustTokenPrivileges(tok, FALSE, &tp, 0, NULL, NULL);
    CloseHandle(tok);
}

static HKEY mk(HKEY parent, const wchar_t *name)
{
    HKEY k = NULL;
    RegCreateKeyExW(parent, name, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL);
    return k;
}

static const struct { const char *name; DWORD filter; } filters[] = {
    {"name", REG_NOTIFY_CHANGE_NAME},
    {"attributes", REG_NOTIFY_CHANGE_ATTRIBUTES},
    {"last_set", REG_NOTIFY_CHANGE_LAST_SET},
    {"security", REG_NOTIFY_CHANGE_SECURITY},
};

/* Restores src.hiv into <target> while <watched> (target or its parent) is watched. */
static void restore_case(const char *name, const wchar_t *watched, const wchar_t *target, BOOL subtree)
{
    oracle_case(name);
    for (size_t i = 0; i < sizeof filters / sizeof filters[0]; i++) {
        HKEY w = mk(root, watched), t = mk(root, target);
        HANDLE e = CreateEventW(NULL, TRUE, FALSE, NULL);
        DWORD v = 1;
        RegSetValueExW(t, L"old", 0, REG_DWORD, (const BYTE *)&v, 4);
        RegNotifyChangeKeyValue(w, subtree, filters[i].filter, e, TRUE);
        LONG st = RegRestoreKeyW(t, L"src.hiv", REG_FORCE_RESTORE);
        char k[64];
        snprintf(k, sizeof k, "%s.restore", filters[i].name);
        o_status(k, st);
        snprintf(k, sizeof k, "%s.signalled", filters[i].name);
        o_bool(k, WaitForSingleObject(e, 400) == WAIT_OBJECT_0);
        RegCloseKey(t);
        RegCloseKey(w);
        CloseHandle(e);
    }
    oracle_case_end();
}

int main(void)
{
    oracle_begin("advapi32/RegNotifyChangeKeyValue_restore");
    root = oracle_reg_root();
    set_privilege(L"SeBackupPrivilege");
    set_privilege(L"SeRestorePrivilege");
    DeleteFileW(L"src.hiv");
    {
        HKEY s = mk(root, L"src"), c = mk(s, L"child");
        RegSetValueExW(s, L"new", 0, REG_SZ, (const BYTE *)L"n", 4);
        LONG st = RegSaveKeyW(s, L"src.hiv", NULL);
        oracle_case("save_source");
        o_status("ret", st);
        oracle_case_end();
        RegCloseKey(c);
        RegCloseKey(s);
    }

    restore_case("restore_watched_key", L"t1", L"t1", FALSE);
    restore_case("restore_child_of_watched_no_subtree", L"p2", L"p2\\t2", FALSE);
    restore_case("restore_child_of_watched_subtree", L"p3", L"p3\\t3", TRUE);

    /* A loaded hive's root, watched, then unloaded. */
    {
        static const wchar_t *hive = L"SetuOracleNotifyRestore";
        LONG st = RegLoadKeyW(HKEY_USERS, hive, L"src.hiv");
        HKEY h = NULL;
        HANDLE e = CreateEventW(NULL, TRUE, FALSE, NULL);
        oracle_case("load_hive");
        o_status("ret", st);
        oracle_case_end();
        if (!st && !RegOpenKeyExW(HKEY_USERS, hive, 0, KEY_NOTIFY | KEY_READ, &h)) {
            LONG nt = RegNotifyChangeKeyValue(h, TRUE, REG_NOTIFY_CHANGE_NAME | REG_NOTIFY_CHANGE_LAST_SET, e, TRUE);
            RegCloseKey(h);  /* An open handle would keep the hive loaded. */
            oracle_case("watch_loaded_root_then_close");
            o_status("notify", nt);
            o_bool("signalled_by_close", WaitForSingleObject(e, 400) == WAIT_OBJECT_0);
            oracle_case_end();
        }
        if (!st) {
            oracle_case("unload_hive");
            o_status("ret", RegUnLoadKeyW(HKEY_USERS, hive));
            oracle_case_end();
        }
        CloseHandle(e);
    }

    DeleteFileW(L"src.hiv");
    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
