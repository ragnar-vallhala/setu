/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: what a key handle opened with a limited access mask can do.
 * For each mask, the same set of operations runs on a prepared key (one
 * value "v", one subkey "s"), and each operation's return code is recorded.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/sysinfo/registry-key-security-and-access-rights
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regqueryvalueexw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regsetvalueexw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regenumkeyexw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regnotifychangekeyvalue
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regdeletekeyw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-reggetkeysecurity
 */
#include "oracle.h"

static HKEY root;

static void prepare(void)
{
    HKEY k, s;
    DWORD v = 7;
    RegCreateKeyExW(root, L"t", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL);
    RegSetValueExW(k, L"v", 0, REG_DWORD, (const BYTE *)&v, sizeof v);
    RegCreateKeyExW(k, L"s", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &s, NULL);
    RegCloseKey(s);
    RegCloseKey(k);
}

static void matrix_case(const char *name, REGSAM sam)
{
    HKEY k = NULL;
    prepare();
    LONG st = RegOpenKeyExW(root, L"t", 0, sam, &k);
    oracle_case(name);
    o_status("open", st);
    if (!st) {
        DWORD v = 0, sz = sizeof v, type = 0;
        o_status("query_value", RegQueryValueExW(k, L"v", NULL, &type, (BYTE *)&v, &sz));
        v = 8;
        o_status("set_value", RegSetValueExW(k, L"w", 0, REG_DWORD, (const BYTE *)&v, sizeof v));
        wchar_t nm[32];
        DWORD n = 32;
        o_status("enum_value", RegEnumValueW(k, 0, nm, &n, NULL, NULL, NULL, NULL));
        n = 32;
        o_status("enum_key", RegEnumKeyExW(k, 0, nm, &n, NULL, NULL, NULL, NULL));
        DWORD nsub = 0;
        o_status("query_info", RegQueryInfoKeyW(k, NULL, NULL, NULL, &nsub, NULL, NULL, NULL, NULL, NULL, NULL, NULL));
        HKEY c = NULL;
        LONG cs = RegCreateKeyExW(k, L"new", 0, NULL, 0, KEY_READ, NULL, &c, NULL);
        o_status("create_subkey", cs);
        if (!cs) RegCloseKey(c);
        c = NULL;
        cs = RegCreateKeyExW(k, L"s", 0, NULL, 0, KEY_READ, NULL, &c, NULL);
        o_status("create_existing_subkey", cs);
        if (!cs) RegCloseKey(c);
        c = NULL;
        cs = RegOpenKeyExW(k, L"s", 0, KEY_READ, &c);
        o_status("open_subkey", cs);
        if (!cs) RegCloseKey(c);
        o_status("open_null_subkey", RegOpenKeyExW(k, NULL, 0, KEY_READ, &c));
        if (c) { RegCloseKey(c); c = NULL; }
        HANDLE ev = CreateEventW(NULL, TRUE, FALSE, NULL);
        o_status("notify", RegNotifyChangeKeyValue(k, FALSE, REG_NOTIFY_CHANGE_NAME, ev, TRUE));
        CloseHandle(ev);
        char sd[512];
        DWORD sdlen = sizeof sd;
        o_status("get_security_owner", RegGetKeySecurity(k, OWNER_SECURITY_INFORMATION, sd, &sdlen));
        o_status("flush", RegFlushKey(k));
        o_status("delete_value", RegDeleteValueW(k, L"v"));
        o_status("delete_subkey", RegDeleteKeyW(k, L"s"));
        o_status("delete_self_empty_name", RegDeleteKeyW(k, L""));
        RegCloseKey(k);
    }
    oracle_case_end();
    RegDeleteTreeW(root, L"t");
    RegDeleteKeyW(root, L"t");
}

int main(void)
{
    oracle_begin("advapi32/RegKeyAccessRights");
    root = oracle_reg_root();

    matrix_case("zero", 0);
    matrix_case("query_value", KEY_QUERY_VALUE);
    matrix_case("set_value", KEY_SET_VALUE);
    matrix_case("create_sub_key", KEY_CREATE_SUB_KEY);
    matrix_case("enumerate_sub_keys", KEY_ENUMERATE_SUB_KEYS);
    matrix_case("notify", KEY_NOTIFY);
    matrix_case("create_link", KEY_CREATE_LINK);
    matrix_case("read_control", READ_CONTROL);
    matrix_case("delete", DELETE);
    matrix_case("read", KEY_READ);
    matrix_case("write", KEY_WRITE);
    matrix_case("read_write", KEY_READ | KEY_WRITE);
    matrix_case("all_access", KEY_ALL_ACCESS);
    matrix_case("maximum_allowed", MAXIMUM_ALLOWED);

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
