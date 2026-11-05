/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: RegNotifyChangeKeyValue in asynchronous mode (with an event):
 * which REG_NOTIFY_CHANGE_* filter fires for which change (subkey create and
 * delete, value set and delete, security change), non-matching changes, and
 * parameter errors. Each case arms a fresh notification on a fresh handle,
 * makes one change, and reports whether the event was signalled.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regnotifychangekeyvalue
 */
#include "oracle.h"

static HKEY root;

enum change { CH_NONE, CH_CREATE_SUBKEY, CH_DELETE_SUBKEY, CH_SET_VALUE, CH_SET_SAME_VALUE,
              CH_DELETE_VALUE, CH_SET_SECURITY, CH_OPEN_SUBKEY, CH_QUERY_VALUE,
              CH_SET_VALUE_IN_SUBKEY, CH_FLUSH };

static void do_change(HKEY k, enum change c)
{
    HKEY sub = NULL;
    DWORD v = 5, size = sizeof v;
    switch (c) {
    case CH_NONE: break;
    case CH_CREATE_SUBKEY:
        RegCreateKeyExW(k, L"newsub", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &sub, NULL);
        RegCloseKey(sub);
        break;
    case CH_DELETE_SUBKEY: RegDeleteKeyW(k, L"existing"); break;
    case CH_SET_VALUE: RegSetValueExW(k, L"val", 0, REG_DWORD, (const BYTE *)&v, 4); break;
    case CH_SET_SAME_VALUE: v = 1; RegSetValueExW(k, L"present", 0, REG_DWORD, (const BYTE *)&v, 4); break;
    case CH_DELETE_VALUE: RegDeleteValueW(k, L"present"); break;
    case CH_SET_SECURITY: {
        BYTE buf[1024];
        DWORD n = sizeof buf;
        if (!RegGetKeySecurity(k, DACL_SECURITY_INFORMATION, buf, &n))
            RegSetKeySecurity(k, DACL_SECURITY_INFORMATION, buf);
        break;
    }
    case CH_OPEN_SUBKEY:
        RegOpenKeyExW(k, L"existing", 0, KEY_READ, &sub);
        RegCloseKey(sub);
        break;
    case CH_QUERY_VALUE: RegQueryValueExW(k, L"present", NULL, NULL, (BYTE *)&v, &size); break;
    case CH_SET_VALUE_IN_SUBKEY:
        RegOpenKeyExW(k, L"existing", 0, KEY_ALL_ACCESS, &sub);
        RegSetValueExW(sub, L"deep", 0, REG_DWORD, (const BYTE *)&v, 4);
        RegCloseKey(sub);
        break;
    case CH_FLUSH: RegFlushKey(k); break;
    }
}

/* Fresh key "w" with value "present"=1 and subkey "existing". */
static HKEY fresh(void)
{
    HKEY k = NULL, sub = NULL;
    DWORD v = 1;
    RegDeleteTreeW(root, L"w");
    RegDeleteKeyW(root, L"w");
    RegCreateKeyExW(root, L"w", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL);
    RegSetValueExW(k, L"present", 0, REG_DWORD, (const BYTE *)&v, 4);
    RegCreateKeyExW(k, L"existing", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &sub, NULL);
    RegCloseKey(sub);
    return k;
}

static const char *wait_str(DWORD w)
{
    return w == WAIT_OBJECT_0 ? "signaled" : w == WAIT_TIMEOUT ? "timeout" : "failed";
}

static void notify_case(const char *name, DWORD filter, BOOL subtree, enum change c)
{
    HKEY k = fresh();
    HANDLE ev = CreateEventW(NULL, TRUE, FALSE, NULL);
    LONG st;
    ORACLE_CALL(st, RegNotifyChangeKeyValue(k, subtree, filter, ev, TRUE));
    do_change(k, c);
    DWORD w = WaitForSingleObject(ev, st ? 0 : 500);
    oracle_case(name);
    o_status("ret", st);
    o_lasterr();
    o_str("event", wait_str(w));
    oracle_case_end();
    RegCloseKey(k);
    CloseHandle(ev);
}

int main(void)
{
    oracle_begin("advapi32/RegNotifyChangeKeyValue");
    root = oracle_reg_root();

    const DWORD N = REG_NOTIFY_CHANGE_NAME, A = REG_NOTIFY_CHANGE_ATTRIBUTES,
                L = REG_NOTIFY_CHANGE_LAST_SET, S = REG_NOTIFY_CHANGE_SECURITY;

    notify_case("name_no_change", N, FALSE, CH_NONE);
    notify_case("name_create_subkey", N, FALSE, CH_CREATE_SUBKEY);
    notify_case("name_delete_subkey", N, FALSE, CH_DELETE_SUBKEY);
    notify_case("name_set_value", N, FALSE, CH_SET_VALUE);
    notify_case("name_delete_value", N, FALSE, CH_DELETE_VALUE);
    notify_case("name_set_security", N, FALSE, CH_SET_SECURITY);
    notify_case("last_set_set_value", L, FALSE, CH_SET_VALUE);
    notify_case("last_set_same_value", L, FALSE, CH_SET_SAME_VALUE);
    notify_case("last_set_delete_value", L, FALSE, CH_DELETE_VALUE);
    notify_case("last_set_create_subkey", L, FALSE, CH_CREATE_SUBKEY);
    notify_case("last_set_delete_subkey", L, FALSE, CH_DELETE_SUBKEY);
    notify_case("last_set_set_security", L, FALSE, CH_SET_SECURITY);
    notify_case("last_set_query_value", L, FALSE, CH_QUERY_VALUE);
    notify_case("last_set_open_subkey", L, FALSE, CH_OPEN_SUBKEY);
    notify_case("last_set_flush", L, FALSE, CH_FLUSH);
    notify_case("attributes_set_value", A, FALSE, CH_SET_VALUE);
    notify_case("attributes_create_subkey", A, FALSE, CH_CREATE_SUBKEY);
    notify_case("attributes_set_security", A, FALSE, CH_SET_SECURITY);
    notify_case("security_set_security", S, FALSE, CH_SET_SECURITY);
    notify_case("security_set_value", S, FALSE, CH_SET_VALUE);
    notify_case("security_create_subkey", S, FALSE, CH_CREATE_SUBKEY);
    notify_case("all_set_value", N | A | L | S, FALSE, CH_SET_VALUE);
    notify_case("subtree_false_value_in_subkey", L, FALSE, CH_SET_VALUE_IN_SUBKEY);
    notify_case("subtree_true_value_in_subkey", L, TRUE, CH_SET_VALUE_IN_SUBKEY);
    notify_case("filter_zero", 0, FALSE, CH_SET_VALUE);
    notify_case("filter_unknown_bit", 0x100, FALSE, CH_SET_VALUE);
    notify_case("filter_thread_agnostic_only", REG_NOTIFY_THREAD_AGNOSTIC, FALSE, CH_SET_VALUE);
    notify_case("filter_thread_agnostic_last_set", REG_NOTIFY_THREAD_AGNOSTIC | L, FALSE, CH_SET_VALUE);

    {   /* Asynchronous without an event. */
        HKEY k = fresh();
        LONG st;
        ORACLE_CALL(st, RegNotifyChangeKeyValue(k, FALSE, L, NULL, TRUE));
        oracle_case("async_null_event");
        o_status("ret", st);
        o_lasterr();
        oracle_case_end();
        RegCloseKey(k);
    }
    {   /* Bad handles. */
        HANDLE ev = CreateEventW(NULL, TRUE, FALSE, NULL);
        LONG st;
        ORACLE_CALL(st, RegNotifyChangeKeyValue(NULL, FALSE, L, ev, TRUE));
        oracle_case("null_key");
        o_status("ret", st);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(st, RegNotifyChangeKeyValue((HKEY)(ULONG_PTR)0x1234, FALSE, L, ev, TRUE));
        oracle_case("bogus_key");
        o_status("ret", st);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(st, RegNotifyChangeKeyValue(root, FALSE, L, (HANDLE)(ULONG_PTR)0x1234, TRUE));
        oracle_case("bogus_event");
        o_status("ret", st);
        o_lasterr();
        oracle_case_end();
        CloseHandle(ev);
    }

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
