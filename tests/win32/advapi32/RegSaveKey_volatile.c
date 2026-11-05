/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: RegSaveKey / RegRestoreKey round trips with volatile keys and
 * restore flags: saving a key that has volatile subkeys, saving a volatile
 * key itself, restoring over a key that already has values and subkeys (what
 * is left of them), restoring with REG_WHOLE_HIVE_VOLATILE onto an ordinary
 * key, and with REG_NO_LAZY_FLUSH. Hive files are only written by RegSaveKey
 * and read back by RegRestoreKey; their bytes are never examined.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regsavekeyw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regrestorekeyw
 */
#include "../reg/regtool.h"

#ifndef REG_WHOLE_HIVE_VOLATILE
#define REG_WHOLE_HIVE_VOLATILE 0x00000001L
#endif
#ifndef REG_NO_LAZY_FLUSH
#define REG_NO_LAZY_FLUSH 0x00000004L
#endif

#define SUB L"Software\\SetuOracle\\advapi32_RegSaveKey_volatile"

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

static HKEY mk(HKEY parent, const wchar_t *name, DWORD options)
{
    HKEY k = NULL;
    RegCreateKeyExW(parent, name, 0, NULL, options, KEY_ALL_ACCESS, NULL, &k, NULL);
    return k;
}

static void sz(HKEY k, const wchar_t *name, const wchar_t *v)
{
    RegSetValueExW(k, name, 0, REG_SZ, (const BYTE *)v, (DWORD)(wcslen(v) + 1) * 2);
}

static void tree_case(const char *name, LONG st, const wchar_t *rel)
{
    wchar_t sub[256];
    swprintf(sub, 256, L"%ls\\%ls", SUB, rel);
    oracle_case(name);
    o_status("ret", st);
    o_regtree("tree", HKEY_CURRENT_USER, sub);
    oracle_case_end();
}

static LONG save(HKEY k, const wchar_t *file)
{
    DeleteFileW(file);
    return RegSaveKeyW(k, file, NULL);
}

/* Restores <file> into <rel> under the test root. */
static LONG restore(const wchar_t *rel, const wchar_t *file, DWORD flags)
{
    HKEY k = NULL;
    LONG st = RegOpenKeyExW(root, rel, 0, KEY_ALL_ACCESS, &k);
    if (st) return st;
    st = RegRestoreKeyW(k, file, flags);
    RegCloseKey(k);
    return st;
}

int main(void)
{
    static const wchar_t *files[] = {L"mixed.hiv", L"vol.hiv"};
    oracle_begin("advapi32/RegSaveKey_volatile");
    root = oracle_reg_root();
    set_privilege(L"SeBackupPrivilege");
    set_privilege(L"SeRestorePrivilege");

    HKEY d = mk(root, L"mixed", 0), v, s;
    sz(d, L"a", L"alpha");
    v = mk(d, L"vol", REG_OPTION_VOLATILE);
    sz(v, L"inside", L"volatile");
    s = mk(d, L"stable", 0);
    sz(s, L"b", L"beta");
    RegCloseKey(s);
    RegCloseKey(v);

    tree_case("save_with_volatile_subkey", save(d, files[0]), L"mixed");
    RegCloseKey(d);
    RegCloseKey(mk(root, L"r1", 0));
    tree_case("restore_mixed", restore(L"r1", files[0], 0), L"r1");
    {
        HKEY k = NULL;
        DWORD disp = 0;
        RegCreateKeyExW(root, L"r1\\stable", 0, NULL, REG_OPTION_VOLATILE, KEY_READ, NULL, &k, &disp);
        oracle_case("restored_subkey_is_not_volatile");
        o_u32("disposition", disp);
        oracle_case_end();
        if (k) RegCloseKey(k);
    }

    v = NULL;
    RegOpenKeyExW(root, L"mixed\\vol", 0, KEY_ALL_ACCESS, &v);
    tree_case("save_volatile_key_itself", v ? save(v, files[1]) : -1, L"mixed\\vol");
    if (v) RegCloseKey(v);

    /* Restore over existing content: values and subkeys of the key go. */
    d = mk(root, L"r2", 0);
    sz(d, L"old", L"old value");
    RegCloseKey(mk(d, L"oldsub", 0));
    RegCloseKey(d);
    tree_case("restore_over_existing", restore(L"r2", files[0], 0), L"r2");

    RegCloseKey(mk(root, L"r3", 0));
    tree_case("restore_whole_hive_volatile_ordinary_key", restore(L"r3", files[0], REG_WHOLE_HIVE_VOLATILE), L"r3");
    RegCloseKey(mk(root, L"r4", 0));
    tree_case("restore_no_lazy_flush", restore(L"r4", files[0], REG_NO_LAZY_FLUSH), L"r4");

    /* Restoring into a volatile key. */
    RegCloseKey(mk(root, L"r5", REG_OPTION_VOLATILE));
    tree_case("restore_into_volatile_key", restore(L"r5", files[0], 0), L"r5");

    for (size_t i = 0; i < sizeof files / sizeof files[0]; i++) DeleteFileW(files[i]);
    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
