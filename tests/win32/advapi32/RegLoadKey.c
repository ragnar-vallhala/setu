/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: RegLoadKeyW / RegUnLoadKeyW / RegReplaceKeyW / RegLoadAppKeyW:
 * privilege requirements, where a hive may be loaded (HKU, HKLM), loading the
 * same name or file twice, unloading with open handles, changes made inside a
 * loaded hive surviving unload and reload (a round trip), and error codes. Hive
 * files are made with RegSaveKeyW by the same system and never read directly.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regloadkeyw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regunloadkeyw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regreplacekeyw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regloadappkeyw
 */
#include "oracle.h"

#define HIVE L"SetuOracle_advapi32_RegLoadKey"
#define HIVE2 L"SetuOracle_advapi32_RegLoadKey2"

static HKEY root;

/* Unloads every hive a case below might have loaded, so that no hive file
 * stays in use after the test (and a second run starts clean). */
static void unload_all(void)
{
    RegUnLoadKeyW(HKEY_USERS, HIVE);
    RegUnLoadKeyW(HKEY_USERS, HIVE2);
    RegUnLoadKeyW(HKEY_USERS, L"SetuOracleX\\Y");
    RegUnLoadKeyW(HKEY_LOCAL_MACHINE, HIVE);
    RegUnLoadKeyW(HKEY_CURRENT_USER, HIVE);
    RegUnLoadKeyW(HKEY_CURRENT_USER, L"Software\\SetuOracle\\advapi32_RegLoadKey\\loaded");
    RegDeleteKeyW(HKEY_USERS, L"SetuOracleX");
}

static BOOL set_privilege(const wchar_t *name, BOOL on)
{
    HANDLE tok;
    TOKEN_PRIVILEGES tp = {1, {{{0, 0}, on ? SE_PRIVILEGE_ENABLED : 0}}};
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &tok)) return FALSE;
    LookupPrivilegeValueW(NULL, name, &tp.Privileges[0].Luid);
    BOOL ok = AdjustTokenPrivileges(tok, FALSE, &tp, 0, NULL, NULL) && GetLastError() == ERROR_SUCCESS;
    CloseHandle(tok);
    return ok;
}

static void privs(BOOL on)
{
    set_privilege(L"SeBackupPrivilege", on);
    set_privilege(L"SeRestorePrivilege", on);
}

static void st_case(const char *name, LONG st)
{
    oracle_case(name);
    o_status("ret", st);
    o_lasterr();
    oracle_case_end();
}

#define CALL_CASE(name, expr) do { LONG st_; ORACLE_CALL(st_, (expr)); st_case(name, st_); } while (0)

/* Values of HKU\<name>: "v=<data>" for REG_SZ values, sorted by enumeration. */
static void o_hive(const char *k, HKEY base, const wchar_t *name)
{
    char out[512] = "";
    HKEY h;
    LONG st = RegOpenKeyExW(base, name, 0, KEY_READ, &h);
    if (st) { snprintf(out, sizeof out, "open failed %ld", (long)st); o_str(k, out); return; }
    wchar_t nm[64], data[64];
    for (DWORD i = 0;; i++) {
        DWORD nl = 64, dl = sizeof data, type;
        if (RegEnumValueW(h, i, nm, &nl, NULL, &type, (BYTE *)data, &dl)) break;
        char item[200];
        snprintf(item, sizeof item, "%s%s=%s", i ? ";" : "", oracle_utf8(nm), type == REG_SZ ? oracle_utf8(data) : "?");
        strncat(out, item, sizeof out - strlen(out) - 1);
    }
    DWORD subs = 0;
    RegQueryInfoKeyW(h, NULL, NULL, NULL, &subs, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
    char t[32];
    snprintf(t, sizeof t, " subkeys=%lu", (unsigned long)subs);
    strncat(out, t, sizeof out - strlen(out) - 1);
    RegCloseKey(h);
    o_str(k, out);
}

int main(void)
{
    oracle_begin("advapi32/RegLoadKey");

    /* Hive files can stay in use for a while after the test (Windows unloads
     * some hives lazily), which would stop the harness from clearing its work
     * directory before the next run. Work in a per-process directory under
     * %TEMP% instead; its name is never printed. */
    {
        wchar_t dir[MAX_PATH], tmp[MAX_PATH];
        GetTempPathW(MAX_PATH, tmp);
        swprintf(dir, MAX_PATH, L"%lssetu_regloadkey_%lu", tmp, GetCurrentProcessId());
        CreateDirectoryW(dir, NULL);
        SetCurrentDirectoryW(dir);
    }

    /* Leftovers from an interrupted run. */
    privs(TRUE);
    unload_all();
    {
        static const wchar_t *files[] = {L"h1.hiv", L"h2.hiv", L"h3.hiv", L"junk.hiv", L"old.hiv", L"app.hiv"};
        for (size_t i = 0; i < sizeof files / sizeof files[0]; i++) DeleteFileW(files[i]);
    }
    root = oracle_reg_root();

    HKEY src = NULL, c = NULL;
    RegCreateKeyExW(root, L"src", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &src, NULL);
    RegSetValueExW(src, L"a", 0, REG_SZ, (const BYTE *)L"alpha", 12);
    RegSetValueExW(src, L"b", 0, REG_SZ, (const BYTE *)L"beta", 10);
    RegCreateKeyExW(src, L"child", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &c, NULL);
    RegCloseKey(c);
    LONG saved = RegSaveKeyW(src, L"h1.hiv", NULL);
    LONG saved2 = RegSaveKeyW(src, L"h2.hiv", NULL);
    LONG saved3 = RegSaveKeyW(src, L"h3.hiv", NULL);
    oracle_case("make_hives");
    o_status("save1", saved);
    o_status("save2", saved2);
    o_status("save3", saved3);
    oracle_case_end();
    RegCloseKey(src);

    privs(FALSE);
    CALL_CASE("load_without_privileges", RegLoadKeyW(HKEY_USERS, HIVE, L"h1.hiv"));
    set_privilege(L"SeRestorePrivilege", TRUE);
    CALL_CASE("load_restore_privilege_only", RegLoadKeyW(HKEY_USERS, HIVE, L"h1.hiv"));
    RegUnLoadKeyW(HKEY_USERS, HIVE);
    privs(TRUE);

    CALL_CASE("load_into_hkcu", RegLoadKeyW(HKEY_CURRENT_USER, HIVE, L"h1.hiv"));
    unload_all();
    CALL_CASE("load_into_ordinary_key", RegLoadKeyW(root, L"loaded", L"h1.hiv"));
    unload_all();
    CALL_CASE("load_nested_name", RegLoadKeyW(HKEY_USERS, L"SetuOracleX\\Y", L"h1.hiv"));
    unload_all();
    CALL_CASE("load_missing_file", RegLoadKeyW(HKEY_USERS, HIVE, L"nosuch.hiv"));
    {
        HANDLE h = CreateFileW(L"junk.hiv", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        DWORD w;
        WriteFile(h, "junk junk junk junk\r\n", 21, &w, NULL);
        CloseHandle(h);
        CALL_CASE("load_junk_file", RegLoadKeyW(HKEY_USERS, HIVE, L"junk.hiv"));
    }

    CALL_CASE("load_hku", RegLoadKeyW(HKEY_USERS, HIVE, L"h1.hiv"));
    oracle_case("loaded_contents");
    o_hive("hive", HKEY_USERS, HIVE);
    oracle_case_end();
    CALL_CASE("load_same_name_again", RegLoadKeyW(HKEY_USERS, HIVE, L"h2.hiv"));
    CALL_CASE("load_same_file_other_name", RegLoadKeyW(HKEY_USERS, HIVE2, L"h1.hiv"));
    RegUnLoadKeyW(HKEY_USERS, HIVE2);

    {   /* Change the loaded hive, unload, load again. */
        HKEY h = NULL;
        RegOpenKeyExW(HKEY_USERS, HIVE, 0, KEY_ALL_ACCESS, &h);
        RegSetValueExW(h, L"c", 0, REG_SZ, (const BYTE *)L"added", 12);
        RegDeleteValueW(h, L"a");
        CALL_CASE("unload_with_open_handle", RegUnLoadKeyW(HKEY_USERS, HIVE));
        RegCloseKey(h);
        CALL_CASE("unload", RegUnLoadKeyW(HKEY_USERS, HIVE));
        oracle_case("after_unload");
        o_hive("hive", HKEY_USERS, HIVE);
        oracle_case_end();
        CALL_CASE("unload_again", RegUnLoadKeyW(HKEY_USERS, HIVE));
        CALL_CASE("reload", RegLoadKeyW(HKEY_USERS, HIVE, L"h1.hiv"));
        oracle_case("reloaded_contents");
        o_hive("hive", HKEY_USERS, HIVE);
        oracle_case_end();
        privs(FALSE);
        CALL_CASE("unload_without_privileges", RegUnLoadKeyW(HKEY_USERS, HIVE));
        privs(TRUE);
        CALL_CASE("unload_reloaded", RegUnLoadKeyW(HKEY_USERS, HIVE));
    }

    CALL_CASE("load_hklm", RegLoadKeyW(HKEY_LOCAL_MACHINE, HIVE, L"h3.hiv"));
    oracle_case("hklm_contents");
    o_hive("hive", HKEY_LOCAL_MACHINE, HIVE);
    oracle_case_end();
    CALL_CASE("unload_hklm", RegUnLoadKeyW(HKEY_LOCAL_MACHINE, HIVE));

    CALL_CASE("unload_ordinary_key", RegUnLoadKeyW(HKEY_CURRENT_USER, L"Software\\SetuOracle\\advapi32_RegLoadKey"));
    CALL_CASE("unload_missing", RegUnLoadKeyW(HKEY_USERS, L"SetuOracleNoSuchHive"));

    {   /* RegReplaceKey: only on a loaded hive's root; takes effect on reload. */
        /* Only on a hive this test loaded: replacing a key of the user's own
         * hive would schedule a replacement of that hive. */
        CALL_CASE("replace_not_loaded", RegReplaceKeyW(HKEY_USERS, HIVE, L"h3.hiv", L"old.hiv"));
        RegLoadKeyW(HKEY_USERS, HIVE, L"h2.hiv");
        CALL_CASE("replace_missing_new_file", RegReplaceKeyW(HKEY_USERS, HIVE, L"nosuch.hiv", L"old.hiv"));
        CALL_CASE("replace_loaded_hive", RegReplaceKeyW(HKEY_USERS, HIVE, L"h3.hiv", L"old.hiv"));
        oracle_case("files_after_replace");
        o_bool("old_exists", GetFileAttributesW(L"old.hiv") != INVALID_FILE_ATTRIBUTES);
        o_bool("new_exists", GetFileAttributesW(L"h3.hiv") != INVALID_FILE_ATTRIBUTES);
        oracle_case_end();
        CALL_CASE("unload_replaced", RegUnLoadKeyW(HKEY_USERS, HIVE));
        oracle_case("files_after_unload");
        o_bool("old_exists", GetFileAttributesW(L"old.hiv") != INVALID_FILE_ATTRIBUTES);
        o_bool("new_exists", GetFileAttributesW(L"h3.hiv") != INVALID_FILE_ATTRIBUTES);
        o_bool("loaded_file_exists", GetFileAttributesW(L"h2.hiv") != INVALID_FILE_ATTRIBUTES);
        oracle_case_end();
    }

    {   /* RegLoadAppKey: private hive, no privileges needed, file created on demand. */
        typedef LONG (WINAPI *loadapp_fn)(LPCWSTR, PHKEY, REGSAM, DWORD, DWORD);
        loadapp_fn la = (loadapp_fn)(void *)GetProcAddress(GetModuleHandleW(L"advapi32.dll"), "RegLoadAppKeyW");
        privs(FALSE);
        oracle_case("loadappkey_available");
        o_bool("available", la != NULL);
        oracle_case_end();
        if (la) {
            /* Windows unloads app hives lazily, so their files stay in use after
             * the test; use per-process names in %TEMP% so a second run is not
             * blocked by them. The names are never printed. */
            wchar_t tmp[MAX_PATH], app_file[MAX_PATH], saved_file[MAX_PATH];
            GetTempPathW(MAX_PATH, tmp);
            swprintf(app_file, MAX_PATH, L"%lssetu_regloadkey_%lu_app.hiv", tmp, GetCurrentProcessId());
            swprintf(saved_file, MAX_PATH, L"%lssetu_regloadkey_%lu_saved.hiv", tmp, GetCurrentProcessId());
            CopyFileW(L"h1.hiv", saved_file, FALSE);
            HKEY app = NULL;
            LONG st;
            ORACLE_CALL(st, la(app_file, &app, KEY_ALL_ACCESS, 0, 0));
            oracle_case("loadappkey_new_file");
            o_status("ret", st);
            o_lasterr();
            o_bool("file_exists", GetFileAttributesW(app_file) != INVALID_FILE_ATTRIBUTES);
            oracle_case_end();
            if (!st) {
                RegSetValueExW(app, L"x", 0, REG_SZ, (const BYTE *)L"app", 8);
                RegCloseKey(app);
                app = NULL;
                ORACLE_CALL(st, la(app_file, &app, KEY_READ, 0, 0));
                oracle_case("loadappkey_reopen");
                o_status("ret", st);
                if (!st) {
                    wchar_t data[16];
                    DWORD size = sizeof data;
                    o_status("query", RegQueryValueExW(app, L"x", NULL, NULL, (BYTE *)data, &size));
                    o_wstr("data", data);
                }
                oracle_case_end();
                if (app) RegCloseKey(app);
            }
            app = NULL;
            ORACLE_CALL(st, la(saved_file, &app, KEY_READ, 0, 0));
            oracle_case("loadappkey_saved_hive");
            o_status("ret", st);
            o_lasterr();
            oracle_case_end();
            if (app) RegCloseKey(app);
        }
    }

    privs(TRUE);
    unload_all();
    privs(FALSE);
    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
