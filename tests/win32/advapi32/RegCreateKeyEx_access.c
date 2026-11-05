/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: the samDesired mask of RegCreateKeyExW / RegOpenKeyExW and the
 * access the returned handle really has (NtQueryObject ObjectBasicInformation
 * GrantedAccess): 0, specific rights, generic rights (mapped to KEY_*),
 * MAXIMUM_ALLOWED, ACCESS_SYSTEM_SECURITY without the privilege, undefined
 * bits, and the WOW64 view bits (not part of the granted mask).
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/sysinfo/registry-key-security-and-access-rights
 *       https://learn.microsoft.com/en-us/windows/win32/secauthz/generic-access-rights
 *       https://learn.microsoft.com/en-us/windows/win32/secauthz/access-mask
 *       https://learn.microsoft.com/en-us/windows/win32/api/winternl/nf-winternl-ntqueryobject
 */
#include "oracle.h"

typedef struct {
    ULONG Attributes;
    ACCESS_MASK GrantedAccess;
    ULONG HandleCount;
    ULONG PointerCount;
    ULONG Reserved[10];
} OBJ_BASIC_INFO;
typedef LONG(NTAPI *NtQueryObject_fn)(HANDLE, int, void *, ULONG, ULONG *);
static NtQueryObject_fn pNtQueryObject;

static HKEY root;

static void granted(HKEY k)
{
    OBJ_BASIC_INFO info;
    ULONG len = 0;
    memset(&info, 0, sizeof info);
    LONG s = pNtQueryObject ? pNtQueryObject(k, 0, &info, sizeof info, &len) : -1;
    if (s) o_hex32("query_object_status", (DWORD)s);
    else o_hex32("granted", info.GrantedAccess);
}

static void create_case(const char *name, const wchar_t *sub, REGSAM sam)
{
    HKEY k = NULL;
    DWORD disp = 0;
    LONG st;
    ORACLE_CALL(st, RegCreateKeyExW(root, sub, 0, NULL, 0, sam, NULL, &k, &disp));
    oracle_case(name);
    o_status("ret", st);
    o_lasterr();
    if (!st) {
        o_u32("disposition", disp);
        granted(k);
        RegCloseKey(k);
    }
    oracle_case_end();
}

static void open_case(const char *name, const wchar_t *sub, REGSAM sam)
{
    HKEY k = NULL;
    LONG st;
    ORACLE_CALL(st, RegOpenKeyExW(root, sub, 0, sam, &k));
    oracle_case(name);
    o_status("ret", st);
    o_lasterr();
    if (!st) {
        granted(k);
        RegCloseKey(k);
    }
    oracle_case_end();
}

int main(void)
{
    oracle_begin("advapi32/RegCreateKeyEx_access");
    pNtQueryObject = (NtQueryObject_fn)(void (*)(void))GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryObject");
    root = oracle_reg_root();

    create_case("create_all_access", L"a", KEY_ALL_ACCESS);
    create_case("create_existing_zero", L"a", 0);
    create_case("create_new_zero", L"z", 0);
    create_case("create_read", L"a", KEY_READ);
    create_case("create_write", L"a", KEY_WRITE);
    create_case("create_new_query_value_only", L"q", KEY_QUERY_VALUE);
    create_case("create_generic_read", L"a", GENERIC_READ);
    create_case("create_generic_write", L"a", GENERIC_WRITE);
    create_case("create_generic_execute", L"a", GENERIC_EXECUTE);
    create_case("create_generic_all", L"a", GENERIC_ALL);
    create_case("create_maximum_allowed", L"a", MAXIMUM_ALLOWED);
    create_case("create_access_system_security", L"a", ACCESS_SYSTEM_SECURITY);
    create_case("create_wow64_64key_bit", L"a", KEY_READ | KEY_WOW64_64KEY);
    create_case("create_wow64_32key_bit", L"a", KEY_READ | KEY_WOW64_32KEY);
    create_case("create_both_wow64_bits", L"a", KEY_READ | KEY_WOW64_32KEY | KEY_WOW64_64KEY);
    create_case("create_undefined_bit_0x80", L"a", KEY_READ | 0x80);
    create_case("create_undefined_bit_0x400", L"a", KEY_READ | 0x400);
    create_case("create_standard_rights_only", L"a", READ_CONTROL | WRITE_DAC);
    create_case("create_synchronize", L"a", SYNCHRONIZE);
    create_case("create_delete_only", L"a", DELETE);

    open_case("open_zero", L"a", 0);
    open_case("open_all_access", L"a", KEY_ALL_ACCESS);
    open_case("open_read", L"a", KEY_READ);
    open_case("open_execute", L"a", KEY_EXECUTE);
    open_case("open_write", L"a", KEY_WRITE);
    open_case("open_enumerate_only", L"a", KEY_ENUMERATE_SUB_KEYS);
    open_case("open_notify_only", L"a", KEY_NOTIFY);
    open_case("open_create_link", L"a", KEY_CREATE_LINK);
    open_case("open_generic_read", L"a", GENERIC_READ);
    open_case("open_generic_write", L"a", GENERIC_WRITE);
    open_case("open_generic_execute", L"a", GENERIC_EXECUTE);
    open_case("open_generic_all", L"a", GENERIC_ALL);
    open_case("open_maximum_allowed", L"a", MAXIMUM_ALLOWED);
    open_case("open_maximum_allowed_read", L"a", MAXIMUM_ALLOWED | KEY_READ);
    open_case("open_access_system_security", L"a", ACCESS_SYSTEM_SECURITY);
    open_case("open_wow64_32key_bit", L"a", KEY_READ | KEY_WOW64_32KEY);
    open_case("open_wow64_res_both", L"a", KEY_READ | KEY_WOW64_RES);
    open_case("open_undefined_bit_0x80", L"a", KEY_READ | 0x80);
    open_case("open_high_undefined_bits", L"a", KEY_READ | 0x00800000);

    {   /* Predefined key opened with an empty subkey and limited access. */
        HKEY k = NULL;
        LONG st;
        ORACLE_CALL(st, RegOpenKeyExW(HKEY_CURRENT_USER, L"", 0, KEY_QUERY_VALUE, &k));
        oracle_case("open_hkcu_empty_query_value");
        o_status("ret", st);
        if (!st) { o_bool("same_handle", k == HKEY_CURRENT_USER); granted(k); RegCloseKey(k); }
        oracle_case_end();
        k = NULL;
        ORACLE_CALL(st, RegOpenKeyExW(HKEY_CURRENT_USER, NULL, 0, KEY_READ, &k));
        oracle_case("open_hkcu_null_read");
        o_status("ret", st);
        if (!st) { o_bool("same_handle", k == HKEY_CURRENT_USER); granted(k); if (k != HKEY_CURRENT_USER) RegCloseKey(k); }
        oracle_case_end();
    }

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
