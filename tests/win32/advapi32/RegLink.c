/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: registry symbolic links. RegCreateKeyExW with
 * REG_OPTION_CREATE_LINK, the SymbolicLinkValue (REG_LINK) value holding an
 * absolute \Registry\... path, opening through the link (redirected to the
 * target) and with REG_OPTION_OPEN_LINK (the link key itself), values and
 * subkeys seen through the link, dangling links, creating a link twice, wrong
 * value names or types, enumeration and query info of the link key, and
 * deleting the link with NtDeleteKey.
 *
 * Paths printed in output have the user's SID replaced by "<user>".
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regcreatekeyexw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regopenkeyexw
 *       https://learn.microsoft.com/en-us/windows/win32/sysinfo/registry-value-types
 *       https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-zwdeletekey
 */
#include "oracle.h"
#include <sddl.h>

#ifndef REG_OPTION_CREATE_LINK
#define REG_OPTION_CREATE_LINK 0x00000002
#endif
#ifndef REG_OPTION_OPEN_LINK
#define REG_OPTION_OPEN_LINK 0x00000008
#endif

typedef LONG (WINAPI *ntdeletekey_fn)(HANDLE);

static HKEY root;
static wchar_t base_nt[512];   /* \Registry\User\<sid>\Software\SetuOracle\advapi32_RegLink */
static ntdeletekey_fn pNtDeleteKey;

static LONG set_link(HKEY k, const wchar_t *target_rel, const wchar_t *valname, DWORD type)
{
    wchar_t path[600];
    swprintf(path, 600, L"%ls\\%ls", base_nt, target_rel);
    return RegSetValueExW(k, valname, 0, type, (const BYTE *)path, (DWORD)wcslen(path) * 2);
}

static LONG del_link(const wchar_t *name)
{
    HKEY h = NULL;
    LONG st = RegOpenKeyExW(root, name, REG_OPTION_OPEN_LINK, DELETE | KEY_READ, &h);
    if (st) return st;
    st = pNtDeleteKey ? pNtDeleteKey(h) : -1;
    RegCloseKey(h);
    return st;
}

static void read_case(const char *name, const wchar_t *path, DWORD options)
{
    HKEY h = NULL;
    LONG st;
    ORACLE_CALL(st, RegOpenKeyExW(root, path, options, KEY_READ, &h));
    oracle_case(name);
    o_status("open", st);
    o_lasterr();
    if (!st) {
        wchar_t data[600];
        DWORD size = sizeof data - 2, type = 0, values = 0, subkeys = 0;
        memset(data, 0, sizeof data);
        LONG q = RegQueryValueExW(h, L"marker", NULL, &type, (BYTE *)data, &size);
        o_status("marker", q);
        if (!q) o_wstr("marker_data", data);
        size = sizeof data - 2;
        memset(data, 0, sizeof data);
        q = RegQueryValueExW(h, L"SymbolicLinkValue", NULL, &type, (BYTE *)data, &size);
        o_status("linkvalue", q);
        if (!q) {
            o_u32("linkvalue_type", type);
            /* Print relative to the test's base path. */
            size_t bl = wcslen(base_nt);
            o_bool("linkvalue_under_base", !_wcsnicmp(data, base_nt, bl));
            o_wstr("linkvalue_rest", !_wcsnicmp(data, base_nt, bl) ? data + bl : L"(other)");
        }
        RegQueryInfoKeyW(h, NULL, NULL, NULL, &subkeys, NULL, NULL, &values, NULL, NULL, NULL, NULL);
        o_u32("subkeys", subkeys);
        o_u32("values", values);
        RegCloseKey(h);
    }
    oracle_case_end();
}

int main(void)
{
    oracle_begin("advapi32/RegLink");
    root = oracle_reg_root();
    pNtDeleteKey = (ntdeletekey_fn)(void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtDeleteKey");
    {
        static BYTE tu[256];
        HANDLE tok;
        DWORD n;
        wchar_t *sid = NULL;
        OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok);
        GetTokenInformation(tok, TokenUser, tu, sizeof tu, &n);
        CloseHandle(tok);
        ConvertSidToStringSidW(((TOKEN_USER *)tu)->User.Sid, &sid);
        swprintf(base_nt, 512, L"\\Registry\\User\\%ls\\Software\\SetuOracle\\advapi32_RegLink", sid);
        LocalFree(sid);
    }

    HKEY target = NULL;
    RegCreateKeyExW(root, L"target", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &target, NULL);
    RegSetValueExW(target, L"marker", 0, REG_SZ, (const BYTE *)L"in target", 20);
    {
        HKEY s = NULL;
        RegCreateKeyExW(target, L"tsub", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &s, NULL);
        RegCloseKey(s);
    }

    /* Create the link key. */
    HKEY link = NULL;
    DWORD disp = 0;
    LONG st;
    ORACLE_CALL(st, RegCreateKeyExW(root, L"link", 0, NULL, REG_OPTION_CREATE_LINK, KEY_ALL_ACCESS | KEY_CREATE_LINK,
                                    NULL, &link, &disp));
    oracle_case("create_link");
    o_status("ret", st);
    o_lasterr();
    o_u32("disposition", disp);
    oracle_case_end();
    if (!st) {
        ORACLE_CALL(st, set_link(link, L"target", L"SymbolicLinkValue", REG_LINK));
        oracle_case("set_symbolic_link_value");
        o_status("ret", st);
        o_lasterr();
        oracle_case_end();
        RegCloseKey(link);
    }

    read_case("open_through_link", L"link", 0);
    read_case("open_link_itself", L"link", REG_OPTION_OPEN_LINK);
    read_case("open_subkey_through_link", L"link\\tsub", 0);
    read_case("open_target_with_open_link_option", L"target", REG_OPTION_OPEN_LINK);
    {
        HKEY h = NULL, n = NULL;
        RegOpenKeyExW(root, L"link", 0, KEY_ALL_ACCESS, &h);
        ORACLE_CALL(st, RegCreateKeyExW(h, L"made_via_link", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &n, &disp));
        oracle_case("create_subkey_through_link");
        o_status("ret", st);
        oracle_case_end();
        if (n) RegCloseKey(n);
        if (h) RegCloseKey(h);
        HKEY t = NULL;
        oracle_case("subkey_appears_in_target");
        o_status("open", RegOpenKeyExW(target, L"made_via_link", 0, KEY_READ, &t));
        oracle_case_end();
        if (t) RegCloseKey(t);
    }
    {   /* Creating the link name again, with and without CREATE_LINK. */
        HKEY h = NULL;
        disp = 0;
        ORACLE_CALL(st, RegCreateKeyExW(root, L"link", 0, NULL, REG_OPTION_CREATE_LINK, KEY_ALL_ACCESS, NULL, &h, &disp));
        oracle_case("create_link_again");
        o_status("ret", st);
        o_lasterr();
        if (!st) o_u32("disposition", disp);
        oracle_case_end();
        if (h) { RegCloseKey(h); h = NULL; }
        disp = 0;
        ORACLE_CALL(st, RegCreateKeyExW(root, L"link", 0, NULL, 0, KEY_READ, NULL, &h, &disp));
        oracle_case("create_plain_over_link");
        o_status("ret", st);
        if (!st) o_u32("disposition", disp);
        oracle_case_end();
        if (h) { RegCloseKey(h); h = NULL; }
        ORACLE_CALL(st, RegCreateKeyExW(root, L"target", 0, NULL, REG_OPTION_CREATE_LINK, KEY_ALL_ACCESS, NULL, &h, &disp));
        oracle_case("create_link_over_plain_key");
        o_status("ret", st);
        o_lasterr();
        oracle_case_end();
        if (h) RegCloseKey(h);
    }
    {   /* Enumerating the parent lists the link name. */
        wchar_t names[512] = L"", nm[64];
        for (DWORD i = 0;; i++) {
            DWORD l = 64;
            if (RegEnumKeyExW(root, i, nm, &l, NULL, NULL, NULL, NULL)) break;
            if (i) wcscat(names, L",");
            wcscat(names, nm);
        }
        oracle_case("enum_parent");
        o_wstr("subkeys", names);
        oracle_case_end();
    }
    {   /* Dangling link. */
        HKEY h = NULL;
        RegCreateKeyExW(root, L"dangling", 0, NULL, REG_OPTION_CREATE_LINK, KEY_ALL_ACCESS | KEY_CREATE_LINK, NULL, &h, NULL);
        if (h) { set_link(h, L"nowhere", L"SymbolicLinkValue", REG_LINK); RegCloseKey(h); }
        read_case("open_dangling", L"dangling", 0);
        read_case("open_dangling_itself", L"dangling", REG_OPTION_OPEN_LINK);
    }
    {   /* Wrong value name / type on a link key. */
        HKEY h = NULL;
        RegCreateKeyExW(root, L"badlink", 0, NULL, REG_OPTION_CREATE_LINK, KEY_ALL_ACCESS | KEY_CREATE_LINK, NULL, &h, NULL);
        oracle_case("set_link_wrong_type");
        o_status("ret", h ? set_link(h, L"target", L"SymbolicLinkValue", REG_SZ) : -1);
        oracle_case_end();
        oracle_case("set_other_value_on_link");
        o_status("ret", h ? set_link(h, L"target", L"Other", REG_LINK) : -1);
        oracle_case_end();
        if (h) RegCloseKey(h);
        read_case("open_badlink_itself", L"badlink", REG_OPTION_OPEN_LINK);
        read_case("open_badlink", L"badlink", 0);
    }
    {   /* Link value on a normal key does nothing. */
        HKEY h = NULL;
        RegCreateKeyExW(root, L"plain", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &h, NULL);
        oracle_case("set_link_value_on_plain_key");
        o_status("ret", set_link(h, L"target", L"SymbolicLinkValue", REG_LINK));
        oracle_case_end();
        RegCloseKey(h);
        read_case("open_plain_with_link_value", L"plain", 0);
    }

    oracle_case("ntdeletekey_available");
    o_bool("available", pNtDeleteKey != NULL);
    oracle_case_end();
    oracle_case("delete_link");
    o_status("ret", del_link(L"link"));
    oracle_case_end();
    {
        HKEY t = NULL;
        oracle_case("target_survives_link_delete");
        o_status("open", RegOpenKeyExW(root, L"target", 0, KEY_READ, &t));
        oracle_case_end();
        if (t) RegCloseKey(t);
    }
    del_link(L"dangling");
    del_link(L"badlink");

    RegCloseKey(target);
    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
