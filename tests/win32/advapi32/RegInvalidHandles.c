/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: key APIs called with handles that are not open registry keys:
 * NULL, a made-up value, INVALID_HANDLE_VALUE, a handle that was just closed,
 * a handle to an event object, and undefined values in the predefined-key
 * range (0x80000007, 0x80000010, 0x8000FFFF).
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/sysinfo/predefined-keys
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regclosekey
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regopenkeyexw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regqueryinfokeyw
 */
#include "oracle.h"

static void all_ops(const char *name, HKEY h)
{
    HKEY k = NULL;
    DWORD n = 0;
    wchar_t nm[32];
    DWORD v = 1, sz = sizeof v;
    oracle_case(name);
    o_status("open", RegOpenKeyExW(h, L"Software", 0, KEY_READ, &k));
    if (k) { RegCloseKey(k); k = NULL; }
    o_status("open_null_sub", RegOpenKeyExW(h, NULL, 0, KEY_READ, &k));
    if (k) { RegCloseKey(k); k = NULL; }
    o_status("create", RegCreateKeyExW(h, L"SetuOracleNever", 0, NULL, REG_OPTION_VOLATILE, KEY_READ, NULL, &k, NULL));
    if (k) { RegCloseKey(k); RegDeleteKeyW(h, L"SetuOracleNever"); k = NULL; }
    o_status("delete_key", RegDeleteKeyW(h, L"SetuOracleNeverExists"));
    n = 32;
    o_status("enum_key", RegEnumKeyExW(h, 0, nm, &n, NULL, NULL, NULL, NULL));
    o_status("query_info", RegQueryInfoKeyW(h, NULL, NULL, NULL, &n, NULL, NULL, NULL, NULL, NULL, NULL, NULL));
    o_status("query_value", RegQueryValueExW(h, L"SetuOracleNever", NULL, NULL, (BYTE *)&v, &sz));
    o_status("flush", RegFlushKey(h));
    o_status("delete_tree", RegDeleteTreeW(h, L"SetuOracleNeverExists"));
    {   /* Asynchronous, so a valid key cannot block the test. */
        HANDLE ev = CreateEventW(NULL, TRUE, FALSE, NULL);
        o_status("notify", RegNotifyChangeKeyValue(h, FALSE, REG_NOTIFY_CHANGE_NAME, ev, TRUE));
        CloseHandle(ev);
    }
    oracle_case_end();
}

static void close_case(const char *name, HKEY h)
{
    LONG st;
    ORACLE_CALL(st, RegCloseKey(h));
    oracle_case(name);
    o_status("ret", st);
    o_lasterr();
    oracle_case_end();
}

int main(void)
{
    oracle_begin("advapi32/RegInvalidHandles");
    HKEY root = oracle_reg_root();

    all_ops("null_handle", NULL);
    all_ops("made_up_handle", (HKEY)(ULONG_PTR)0x1234);
    all_ops("invalid_handle_value", (HKEY)INVALID_HANDLE_VALUE);
    all_ops("predefined_0x80000007", (HKEY)(LONG_PTR)(LONG)0x80000007);
    all_ops("predefined_0x80000010", (HKEY)(LONG_PTR)(LONG)0x80000010);
    all_ops("predefined_0x8000ffff", (HKEY)(LONG_PTR)(LONG)0x8000FFFF);
    {
        HANDLE ev = CreateEventW(NULL, TRUE, FALSE, NULL);
        all_ops("event_handle", (HKEY)ev);
        close_case("close_event_handle", (HKEY)ev);
        oracle_case("event_still_open_after_regclosekey");
        o_bool("set_event_ok", SetEvent(ev));
        oracle_case_end();
        CloseHandle(ev);
    }
    {
        HKEY k = NULL;
        RegOpenKeyExW(root, NULL, 0, KEY_READ, &k);
        RegCloseKey(k);
        all_ops("closed_key_handle", k);
    }

    close_case("close_null", NULL);
    close_case("close_made_up", (HKEY)(ULONG_PTR)0x1234);
    close_case("close_invalid_handle_value", (HKEY)INVALID_HANDLE_VALUE);
    close_case("close_predefined_0x80000010", (HKEY)(LONG_PTR)(LONG)0x80000010);
    {
        HKEY k = NULL;
        RegOpenKeyExW(root, NULL, 0, KEY_READ, &k);
        close_case("close_valid", k);
        close_case("close_again", k);
    }

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
