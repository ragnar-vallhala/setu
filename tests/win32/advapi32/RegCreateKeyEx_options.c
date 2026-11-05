/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: RegCreateKeyExW dwOptions and Reserved: volatile and
 * non-volatile keys (and how volatility of an existing key is kept),
 * REG_OPTION_BACKUP_RESTORE with and without SeBackup/SeRestore enabled,
 * REG_OPTION_OPEN_LINK on a normal key, REG_OPTION_DONT_VIRTUALIZE (0x10), undefined
 * option bits, a non-zero Reserved, and a NULL disposition pointer.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regcreatekeyexw
 *       https://learn.microsoft.com/en-us/windows/win32/secauthz/privilege-constants
 *       https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-adjusttokenprivileges
 */
#include "oracle.h"

static HKEY root;

static void create_case(const char *name, const wchar_t *sub, DWORD reserved, DWORD options, REGSAM sam)
{
    HKEY k = NULL;
    DWORD disp = 0xAAAAAAAAu;
    LONG st;
    ORACLE_CALL(st, RegCreateKeyExW(root, sub, reserved, NULL, options, sam, NULL, &k, &disp));
    oracle_case(name);
    o_status("ret", st);
    o_lasterr();
    o_u32("disposition", disp);
    o_bool("got_key", k != NULL);
    if (!st && k) {
        DWORD v = 1;
        o_status("set_value", RegSetValueExW(k, L"v", 0, REG_DWORD, (const BYTE *)&v, sizeof v));
        DWORD n = 0;
        o_status("query_info", RegQueryInfoKeyW(k, NULL, NULL, NULL, &n, NULL, NULL, NULL, NULL, NULL, NULL, NULL));
    }
    oracle_case_end();
    if (!st && k) RegCloseKey(k);
}

/* A key is volatile iff a non-volatile child cannot be created under it. */
static void volatile_probe(const char *name, const wchar_t *sub)
{
    HKEY p = NULL, k = NULL;
    LONG st = RegOpenKeyExW(root, sub, 0, KEY_ALL_ACCESS, &p);
    LONG cst = -1;
    if (!st) {
        cst = RegCreateKeyExW(p, L"nvprobe", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL);
        if (!cst) { RegCloseKey(k); RegDeleteKeyW(p, L"nvprobe"); }
        RegCloseKey(p);
    }
    oracle_case(name);
    o_status("open", st);
    o_status("nonvolatile_child", cst);
    oracle_case_end();
}

static BOOL set_privilege(const wchar_t *priv, BOOL enable)
{
    HANDLE tok;
    TOKEN_PRIVILEGES tp;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &tok)) return FALSE;
    tp.PrivilegeCount = 1;
    LookupPrivilegeValueW(NULL, priv, &tp.Privileges[0].Luid);
    tp.Privileges[0].Attributes = enable ? SE_PRIVILEGE_ENABLED : 0;
    BOOL ok = AdjustTokenPrivileges(tok, FALSE, &tp, sizeof tp, NULL, NULL) && GetLastError() == ERROR_SUCCESS;
    CloseHandle(tok);
    return ok;
}

int main(void)
{
    oracle_begin("advapi32/RegCreateKeyEx_options");
    root = oracle_reg_root();

    create_case("nonvolatile", L"nv", 0, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS);
    volatile_probe("nonvolatile_is_nonvolatile", L"nv");
    create_case("volatile", L"vol", 0, REG_OPTION_VOLATILE, KEY_ALL_ACCESS);
    volatile_probe("volatile_is_volatile", L"vol");
    create_case("volatile_on_existing_nonvolatile", L"nv", 0, REG_OPTION_VOLATILE, KEY_ALL_ACCESS);
    volatile_probe("existing_nonvolatile_stays", L"nv");
    create_case("nonvolatile_on_existing_volatile", L"vol", 0, 0, KEY_ALL_ACCESS);
    volatile_probe("existing_volatile_stays", L"vol");
    create_case("volatile_under_volatile", L"vol\\child", 0, REG_OPTION_VOLATILE, KEY_ALL_ACCESS);
    create_case("nonvolatile_under_volatile", L"vol\\nv", 0, 0, KEY_ALL_ACCESS);
    create_case("volatile_under_nonvolatile", L"nv\\vchild", 0, REG_OPTION_VOLATILE, KEY_ALL_ACCESS);
    create_case("volatile_multi_level_new", L"vm1\\vm2\\vm3", 0, REG_OPTION_VOLATILE, KEY_ALL_ACCESS);
    volatile_probe("multi_level_intermediate_volatile", L"vm1");
    volatile_probe("multi_level_leaf_volatile", L"vm1\\vm2\\vm3");
    create_case("open_link_option_normal_key", L"nv", 0, REG_OPTION_OPEN_LINK, KEY_ALL_ACCESS);
    create_case("open_link_option_new_key", L"ol", 0, REG_OPTION_OPEN_LINK, KEY_ALL_ACCESS);
    create_case("dont_virtualize", L"dv", 0, 0x10 /* REG_OPTION_DONT_VIRTUALIZE */, KEY_ALL_ACCESS);
    create_case("option_0x20", L"o20", 0, 0x20, KEY_ALL_ACCESS);
    create_case("option_0x100", L"o100", 0, 0x100, KEY_ALL_ACCESS);
    create_case("option_0x80000000", L"ohi", 0, 0x80000000u, KEY_ALL_ACCESS);
    create_case("volatile_plus_0x40", L"vx", 0, REG_OPTION_VOLATILE | 0x40, KEY_ALL_ACCESS);
    create_case("reserved_1", L"r1", 1, 0, KEY_ALL_ACCESS);
    create_case("reserved_1_existing", L"nv", 1, 0, KEY_ALL_ACCESS);

    /* Backup/restore without the privileges enabled. */
    set_privilege(L"SeBackupPrivilege", FALSE);
    set_privilege(L"SeRestorePrivilege", FALSE);
    create_case("backup_restore_no_priv_existing", L"nv", 0, REG_OPTION_BACKUP_RESTORE, KEY_ALL_ACCESS);
    create_case("backup_restore_no_priv_new", L"br", 0, REG_OPTION_BACKUP_RESTORE, KEY_ALL_ACCESS);
    create_case("backup_restore_volatile_no_priv", L"brv", 0, REG_OPTION_BACKUP_RESTORE | REG_OPTION_VOLATILE,
                KEY_ALL_ACCESS);
    {
        BOOL b = set_privilege(L"SeBackupPrivilege", TRUE);
        BOOL r = set_privilege(L"SeRestorePrivilege", TRUE);
        oracle_case("enable_backup_restore_privileges");
        o_bool("backup", b);
        o_bool("restore", r);
        oracle_case_end();
    }
    create_case("backup_restore_with_priv_existing", L"nv", 0, REG_OPTION_BACKUP_RESTORE, KEY_ALL_ACCESS);
    create_case("backup_restore_with_priv_new", L"br2", 0, REG_OPTION_BACKUP_RESTORE, KEY_ALL_ACCESS);
    create_case("backup_restore_with_priv_read_sam", L"nv", 0, REG_OPTION_BACKUP_RESTORE, KEY_READ);
    set_privilege(L"SeBackupPrivilege", FALSE);
    set_privilege(L"SeRestorePrivilege", FALSE);

    {   /* NULL disposition pointer. */
        HKEY k = NULL;
        LONG st;
        ORACLE_CALL(st, RegCreateKeyExW(root, L"nodisp", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL));
        oracle_case("null_disposition");
        o_status("ret", st);
        o_lasterr();
        oracle_case_end();
        if (!st) RegCloseKey(k);
    }
    {   /* Which keys exist now (sorted listing). */
        char list[1024] = "";
        for (DWORD i = 0;; i++) {
            wchar_t nm[64];
            DWORD n = 64;
            if (RegEnumKeyExW(root, i, nm, &n, NULL, NULL, NULL, NULL)) break;
            if (i) strcat(list, ",");
            strcat(list, oracle_utf8(nm));
        }
        oracle_case("keys_after");
        o_str("keys", list);
        oracle_case_end();
    }

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
