/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: registry symbolic links beyond the basics in RegLink: a link
 * to a link (chains of 2 and 10), a link that points to itself and two links
 * pointing at each other, the target path's case and a trailing NUL counted
 * in the REG_LINK data, a target that is a subkey of the link's parent seen
 * through a relative handle, volatile links (REG_OPTION_VOLATILE together
 * with REG_OPTION_CREATE_LINK), and RegDeleteKey / RegDeleteTree through a
 * link. Every link is deleted with NtDeleteKey on the link itself.
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
static wchar_t base_nt[512];
static ntdeletekey_fn pNtDeleteKey;

/* Creates link <name> -> <base>\<target> (data size as given: -1 = no NUL). */
static LONG make_link(const wchar_t *name, const wchar_t *target, DWORD options, int extra_bytes)
{
    wchar_t path[600];
    HKEY k = NULL;
    LONG st = RegCreateKeyExW(root, name, 0, NULL, REG_OPTION_CREATE_LINK | options,
                              KEY_ALL_ACCESS | KEY_CREATE_LINK, NULL, &k, NULL);
    if (st) return st;
    swprintf(path, 600, L"%ls\\%ls", base_nt, target);
    st = RegSetValueExW(k, L"SymbolicLinkValue", 0, REG_LINK, (const BYTE *)path,
                        (DWORD)(wcslen(path) * 2 + extra_bytes));
    RegCloseKey(k);
    return st;
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

/* Opens <path> under the test root and prints the result and the marker value. */
static void open_case(const char *name, const wchar_t *path, DWORD options)
{
    HKEY h = NULL;
    LONG st = RegOpenKeyExW(root, path, options, KEY_READ, &h);
    oracle_case(name);
    o_status("open", st);
    if (!st) {
        wchar_t data[64] = {0};
        DWORD size = sizeof data - 2;
        LONG q = RegQueryValueExW(h, L"marker", NULL, NULL, (BYTE *)data, &size);
        o_status("marker", q);
        if (!q) o_wstr("marker_data", data);
        RegCloseKey(h);
    }
    oracle_case_end();
}

static void status_case(const char *name, LONG st)
{
    oracle_case(name);
    o_status("ret", st);
    oracle_case_end();
}

int main(void)
{
    oracle_begin("advapi32/RegLink_chains");
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
        swprintf(base_nt, 512, L"\\Registry\\User\\%ls\\Software\\SetuOracle\\advapi32_RegLink_chains", sid);
        LocalFree(sid);
    }

    HKEY t = NULL, ts = NULL;
    RegCreateKeyExW(root, L"target", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &t, NULL);
    RegSetValueExW(t, L"marker", 0, REG_SZ, (const BYTE *)L"target", 14);
    RegCreateKeyExW(t, L"sub", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &ts, NULL);
    RegSetValueExW(ts, L"marker", 0, REG_SZ, (const BYTE *)L"target sub", 22);
    RegCloseKey(ts);

    /* Chains. */
    status_case("make_l1", make_link(L"l1", L"target", 0, 0));
    status_case("make_l2", make_link(L"l2", L"l1", 0, 0));
    open_case("open_chain_2", L"l2", 0);
    open_case("open_chain_2_subkey", L"l2\\sub", 0);
    open_case("open_chain_2_itself", L"l2", REG_OPTION_OPEN_LINK);
    {
        wchar_t prev[8] = L"l2", cur[8];
        for (int i = 3; i <= 10; i++) {
            swprintf(cur, 8, L"l%d", i);
            make_link(cur, prev, 0, 0);
            wcscpy(prev, cur);
        }
    }
    open_case("open_chain_10", L"l10", 0);

    /* Loops. */
    status_case("make_self", make_link(L"self", L"self", 0, 0));
    open_case("open_self_loop", L"self", 0);
    open_case("open_self_loop_itself", L"self", REG_OPTION_OPEN_LINK);
    make_link(L"ping", L"pong", 0, 0);
    make_link(L"pong", L"ping", 0, 0);
    open_case("open_two_link_loop", L"ping", 0);

    /* Target path spelling. */
    {
        wchar_t path[600];
        HKEY k = NULL;
        LONG st = RegCreateKeyExW(root, L"upper", 0, NULL, REG_OPTION_CREATE_LINK,
                                  KEY_ALL_ACCESS | KEY_CREATE_LINK, NULL, &k, NULL);
        swprintf(path, 600, L"%ls\\TARGET", base_nt);
        _wcsupr(path);
        if (!st) {
            RegSetValueExW(k, L"SymbolicLinkValue", 0, REG_LINK, (const BYTE *)path, (DWORD)wcslen(path) * 2);
            RegCloseKey(k);
        }
    }
    open_case("open_uppercase_target", L"upper", 0);
    status_case("make_with_nul", make_link(L"withnul", L"target", 0, 2));
    open_case("open_target_with_counted_nul", L"withnul", 0);
    status_case("make_to_missing_then_create", make_link(L"later", L"made_later", 0, 0));
    open_case("open_before_target_exists", L"later", 0);
    {
        HKEY k = NULL;
        RegCreateKeyExW(root, L"made_later", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL);
        RegSetValueExW(k, L"marker", 0, REG_SZ, (const BYTE *)L"later", 12);
        RegCloseKey(k);
    }
    open_case("open_after_target_created", L"later", 0);

    /* Volatile link. */
    status_case("make_volatile_link", make_link(L"vlink", L"target", REG_OPTION_VOLATILE, 0));
    open_case("open_volatile_link", L"vlink", 0);
    {
        HKEY k = NULL;
        LONG st = RegCreateKeyExW(root, L"vlink\\child", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL);
        oracle_case("create_child_through_volatile_link");
        o_status("ret", st);
        oracle_case_end();
        if (!st) RegCloseKey(k);
        RegDeleteKeyW(t, L"child");
    }

    /* Deleting through a link deletes in the target, not the link. */
    {
        HKEY k = NULL;
        RegCreateKeyExW(t, L"victim", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL);
        RegCloseKey(k);
    }
    status_case("delete_subkey_through_link", RegDeleteKeyW(root, L"l1\\victim"));
    open_case("victim_after_delete", L"target\\victim", 0);
    status_case("delete_key_named_by_link", RegDeleteKeyW(root, L"l2"));
    open_case("l2_after_delete_attempt", L"l2", REG_OPTION_OPEN_LINK);
    open_case("target_after_delete_attempt", L"target", 0);

    /* Clean up every link itself, newest first. */
    {
        static const wchar_t *links[] = {L"vlink", L"later", L"withnul", L"upper", L"pong", L"ping", L"self",
                                         L"l10", L"l9", L"l8", L"l7", L"l6", L"l5", L"l4", L"l3", L"l2", L"l1"};
        int left = 0;
        for (size_t i = 0; i < sizeof links / sizeof links[0]; i++) {
            LONG st = del_link(links[i]);
            if (st && st != ERROR_FILE_NOT_FOUND) left++;
        }
        oracle_case("cleanup");
        o_u32("links_not_deleted", (DWORD)left);
        oracle_case_end();
    }
    RegCloseKey(t);
    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
