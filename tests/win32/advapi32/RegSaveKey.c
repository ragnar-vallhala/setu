/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: RegSaveKeyW / RegSaveKeyExW / RegRestoreKeyW: privilege
 * requirements (SeBackupPrivilege, SeRestorePrivilege), error codes (existing
 * file, bad path, bad flags, missing access), and round trips: saving a tree
 * and restoring it into a scratch key gives back the same values and subkeys,
 * replacing what the scratch key held. The saved file is never read: only its
 * existence is reported.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regsavekeyw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regsavekeyexw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regrestorekeyw
 *       https://learn.microsoft.com/en-us/windows/win32/secauthz/privilege-constants
 */
#include "oracle.h"

#define SUB L"Software\\SetuOracle\\advapi32_RegSaveKey"

static HKEY root;

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

static int cmpw(const void *a, const void *b) { return _wcsicmp(*(wchar_t *const *)a, *(wchar_t *const *)b); }

/* Stable dump of a tree: values sorted by name, subkeys sorted. */
static void dump(HKEY key, const wchar_t *path, char *out, size_t cap)
{
    wchar_t *names[64], nm[256];
    int n = 0;
    DWORD len;
    char tmp[700];
    snprintf(tmp, sizeof tmp, "[%s]", oracle_utf8(path));
    strncat(out, tmp, cap - strlen(out) - 1);
    for (DWORD i = 0; n < 64; i++) {
        len = 256;
        if (RegEnumValueW(key, i, nm, &len, NULL, NULL, NULL, NULL)) break;
        names[n++] = _wcsdup(nm);
    }
    qsort(names, n, sizeof names[0], cmpw);
    for (int j = 0; j < n; j++) {
        BYTE data[128];
        DWORD type = 0, size = sizeof data;
        char hex[260] = "";
        RegQueryValueExW(key, names[j], NULL, &type, data, &size);
        for (DWORD b = 0; b < size && b < sizeof data; b++) snprintf(hex + 2 * b, 3, "%02x", data[b]);
        snprintf(tmp, sizeof tmp, "%s=%lu:%s;", names[j][0] ? oracle_utf8(names[j]) : "@", (unsigned long)type, hex);
        strncat(out, tmp, cap - strlen(out) - 1);
        free(names[j]);
    }
    n = 0;
    for (DWORD i = 0; n < 64; i++) {
        len = 256;
        if (RegEnumKeyExW(key, i, nm, &len, NULL, NULL, NULL, NULL)) break;
        names[n++] = _wcsdup(nm);
    }
    qsort(names, n, sizeof names[0], cmpw);
    for (int j = 0; j < n; j++) {
        HKEY sub;
        wchar_t p[300];
        swprintf(p, 300, L"%ls\\%ls", path, names[j]);
        strncat(out, " ", cap - strlen(out) - 1);
        if (!RegOpenKeyExW(key, names[j], 0, KEY_READ, &sub)) {
            dump(sub, p, out, cap);
            RegCloseKey(sub);
        }
        free(names[j]);
    }
}

static void o_tree(const char *k, const wchar_t *sub)
{
    static char out[8192];
    HKEY key;
    out[0] = 0;
    LONG st = RegOpenKeyExW(root, sub, 0, KEY_READ, &key);
    if (st) snprintf(out, sizeof out, "open failed %ld", (long)st);
    else { dump(key, L"", out, sizeof out); RegCloseKey(key); }
    o_str(k, out);
}

static void st_case(const char *name, LONG st)
{
    oracle_case(name);
    o_status("ret", st);
    o_lasterr();
    oracle_case_end();
}

#define CALL_CASE(name, expr) do { LONG st_; ORACLE_CALL(st_, (expr)); st_case(name, st_); } while (0)

int main(void)
{
    oracle_begin("advapi32/RegSaveKey");
    {   /* Files from an earlier run in the same directory. */
        static const wchar_t *files[] = {L"nopriv.hiv", L"data.hiv", L"std.hiv", L"latest.hiv", L"nocomp.hiv",
                                         L"zero.hiv", L"bad.hiv", L"ro.hiv", L"null.hiv", L"junk.hiv"};
        for (size_t i = 0; i < sizeof files / sizeof files[0]; i++) DeleteFileW(files[i]);
    }
    root = oracle_reg_root();
    HKEY d = NULL, d1 = NULL, d2 = NULL, scratch = NULL;
    RegCreateKeyExW(root, L"data", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &d, NULL);
    RegCreateKeyExW(d, L"one", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &d1, NULL);
    RegCreateKeyExW(d1, L"two", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &d2, NULL);
    DWORD dw = 0x01020304;
    ULONGLONG qw = 0x1122334455667788ULL;
    BYTE bin[16] = {0, 1, 2, 3, 250, 251, 252, 253, 254, 255};
    RegSetValueExW(d, NULL, 0, REG_SZ, (const BYTE *)L"default", 16);
    RegSetValueExW(d, L"sz", 0, REG_SZ, (const BYTE *)L"string", 14);
    RegSetValueExW(d, L"dw", 0, REG_DWORD, (const BYTE *)&dw, 4);
    RegSetValueExW(d, L"qw", 0, REG_QWORD, (const BYTE *)&qw, 8);
    RegSetValueExW(d, L"bin", 0, REG_BINARY, bin, sizeof bin);
    RegSetValueExW(d, L"multi", 0, REG_MULTI_SZ, (const BYTE *)L"a\0b\0", 10);
    RegSetValueExW(d, L"none", 0, REG_NONE, NULL, 0);
    RegSetValueExW(d, L"odd", 0, 0x4321, bin, 3);
    RegSetValueExW(d1, L"v1", 0, REG_SZ, (const BYTE *)L"one", 8);
    RegSetValueExW(d2, L"v2", 0, REG_EXPAND_SZ, (const BYTE *)L"%X%", 8);
    RegCloseKey(d2);
    RegCloseKey(d1);

    oracle_case("original");
    o_tree("tree", L"data");
    oracle_case_end();

    set_privilege(L"SeBackupPrivilege", FALSE);
    set_privilege(L"SeRestorePrivilege", FALSE);
    CALL_CASE("save_without_backup_privilege", RegSaveKeyW(d, L"nopriv.hiv", NULL));
    oracle_case("file_after_failed_save");
    o_bool("exists", GetFileAttributesW(L"nopriv.hiv") != INVALID_FILE_ATTRIBUTES);
    oracle_case_end();

    oracle_case("enable_backup_privilege");
    o_bool("ok", set_privilege(L"SeBackupPrivilege", TRUE));
    oracle_case_end();
    CALL_CASE("save", RegSaveKeyW(d, L"data.hiv", NULL));
    oracle_case("file_after_save");
    o_bool("exists", GetFileAttributesW(L"data.hiv") != INVALID_FILE_ATTRIBUTES);
    oracle_case_end();
    CALL_CASE("save_existing_file", RegSaveKeyW(d, L"data.hiv", NULL));
    CALL_CASE("save_bad_dir", RegSaveKeyW(d, L"nodir\\x.hiv", NULL));
    CALL_CASE("save_ex_standard", RegSaveKeyExW(d, L"std.hiv", NULL, REG_STANDARD_FORMAT));
    CALL_CASE("save_ex_latest", RegSaveKeyExW(d, L"latest.hiv", NULL, REG_LATEST_FORMAT));
    CALL_CASE("save_ex_no_compression", RegSaveKeyExW(d, L"nocomp.hiv", NULL, REG_NO_COMPRESSION));
    CALL_CASE("save_ex_flags_zero", RegSaveKeyExW(d, L"zero.hiv", NULL, 0));
    CALL_CASE("save_ex_flags_bad", RegSaveKeyExW(d, L"bad.hiv", NULL, 8));
    CALL_CASE("save_empty_file", RegSaveKeyW(d, L"", NULL));
    {
        HKEY ro = NULL;
        RegOpenKeyExW(root, L"data", 0, KEY_QUERY_VALUE, &ro);
        CALL_CASE("save_query_value_handle", RegSaveKeyW(ro, L"ro.hiv", NULL));
        RegCloseKey(ro);
    }
    CALL_CASE("save_null_key", RegSaveKeyW(NULL, L"null.hiv", NULL));

    RegCreateKeyExW(root, L"scratch", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &scratch, NULL);
    RegSetValueExW(scratch, L"old", 0, REG_SZ, (const BYTE *)L"gone?", 12);
    {
        HKEY os = NULL;
        RegCreateKeyExW(scratch, L"oldsub", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &os, NULL);
        RegCloseKey(os);
    }
    CALL_CASE("restore_without_restore_privilege", RegRestoreKeyW(scratch, L"data.hiv", 0));
    oracle_case("enable_restore_privilege");
    o_bool("ok", set_privilege(L"SeRestorePrivilege", TRUE));
    oracle_case_end();
    CALL_CASE("restore", RegRestoreKeyW(scratch, L"data.hiv", 0));
    oracle_case("scratch_after_restore");
    o_tree("tree", L"scratch");
    oracle_case_end();
    {
        HKEY s2 = NULL;
        RegCreateKeyExW(root, L"scratch2", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &s2, NULL);
        CALL_CASE("restore_latest_format", RegRestoreKeyW(s2, L"latest.hiv", 0));
        oracle_case("scratch2_after_restore");
        o_tree("tree", L"scratch2");
        oracle_case_end();
        RegCloseKey(s2);
    }
    {   /* Restore over a key that has an open subkey handle. */
        HKEY s3 = NULL, open_sub = NULL;
        RegCreateKeyExW(root, L"scratch3", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &s3, NULL);
        RegCreateKeyExW(s3, L"held", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &open_sub, NULL);
        CALL_CASE("restore_with_open_subkey", RegRestoreKeyW(s3, L"data.hiv", 0));
        CALL_CASE("restore_force_with_open_subkey", RegRestoreKeyW(s3, L"data.hiv", REG_FORCE_RESTORE));
        oracle_case("scratch3_after_restore");
        o_tree("tree", L"scratch3");
        oracle_case_end();
        RegCloseKey(open_sub);
        RegCloseKey(s3);
    }
    CALL_CASE("restore_missing_file", RegRestoreKeyW(scratch, L"nosuch.hiv", 0));
    {
        HANDLE h = CreateFileW(L"junk.hiv", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        DWORD w;
        WriteFile(h, "this is not a hive file at all, just text\r\n", 43, &w, NULL);
        CloseHandle(h);
        CALL_CASE("restore_junk_file", RegRestoreKeyW(scratch, L"junk.hiv", 0));
        oracle_case("scratch_after_junk");
        o_tree("tree", L"scratch");
        oracle_case_end();
    }
    CALL_CASE("restore_bad_flags", RegRestoreKeyW(scratch, L"data.hiv", 0x100));
    CALL_CASE("restore_null_key", RegRestoreKeyW(NULL, L"data.hiv", 0));

    set_privilege(L"SeBackupPrivilege", FALSE);
    set_privilege(L"SeRestorePrivilege", FALSE);
    RegCloseKey(scratch);
    RegCloseKey(d);
    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
