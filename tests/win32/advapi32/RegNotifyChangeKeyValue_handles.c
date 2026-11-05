/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: RegNotifyChangeKeyValue lifetime: what happens to a pending
 * notification when the key handle is closed or the key is deleted, whether a
 * notification fires once or keeps firing, re-arming, two notifications on one
 * handle, notifications on two handles to the same key, the access right the
 * handle needs, and the synchronous (blocking) mode driven from a helper
 * thread.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regnotifychangekeyvalue
 *       https://learn.microsoft.com/en-us/windows/win32/sysinfo/registry-key-security-and-access-rights
 */
#include "oracle.h"

static HKEY root;

static const char *wait_str(DWORD w)
{
    return w == WAIT_OBJECT_0 ? "signaled" : w == WAIT_TIMEOUT ? "timeout" : "failed";
}

static HKEY fresh(const wchar_t *name)
{
    HKEY k = NULL;
    RegDeleteTreeW(root, name);
    RegDeleteKeyW(root, name);
    RegCreateKeyExW(root, name, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL);
    return k;
}

static void setv(HKEY k, const wchar_t *name, DWORD v)
{
    RegSetValueExW(k, name, 0, REG_DWORD, (const BYTE *)&v, 4);
}

struct sync_arg { HKEY key; DWORD filter; BOOL subtree; LONG ret; };

static DWORD WINAPI sync_thread(void *p)
{
    struct sync_arg *a = p;
    a->ret = RegNotifyChangeKeyValue(a->key, a->subtree, a->filter, NULL, FALSE);
    return 0;
}

int main(void)
{
    oracle_begin("advapi32/RegNotifyChangeKeyValue_handles");
    root = oracle_reg_root();
    const DWORD L = REG_NOTIFY_CHANGE_LAST_SET, N = REG_NOTIFY_CHANGE_NAME;

    {   /* Closing the watched handle. */
        HKEY k = fresh(L"close");
        HANDLE ev = CreateEventW(NULL, TRUE, FALSE, NULL);
        LONG st = RegNotifyChangeKeyValue(k, FALSE, L, ev, TRUE);
        RegCloseKey(k);
        oracle_case("close_handle_signals");
        o_status("ret", st);
        o_str("event", wait_str(WaitForSingleObject(ev, 500)));
        oracle_case_end();
        CloseHandle(ev);
    }
    {   /* Deleting the watched key through another handle. */
        HKEY k = fresh(L"del");
        HANDLE ev = CreateEventW(NULL, TRUE, FALSE, NULL);
        LONG st = RegNotifyChangeKeyValue(k, FALSE, L, ev, TRUE);
        LONG del = RegDeleteKeyW(root, L"del");
        oracle_case("delete_key_last_set");
        o_status("ret", st);
        o_status("delete", del);
        o_str("event", wait_str(WaitForSingleObject(ev, 500)));
        oracle_case_end();
        RegCloseKey(k);
        CloseHandle(ev);
    }
    {
        HKEY k = fresh(L"del2");
        HANDLE ev = CreateEventW(NULL, TRUE, FALSE, NULL);
        LONG st = RegNotifyChangeKeyValue(k, FALSE, N, ev, TRUE);
        RegDeleteKeyW(root, L"del2");
        oracle_case("delete_key_name");
        o_status("ret", st);
        o_str("event", wait_str(WaitForSingleObject(ev, 500)));
        oracle_case_end();
        LONG st2;
        ResetEvent(ev);
        ORACLE_CALL(st2, RegNotifyChangeKeyValue(k, FALSE, N, ev, TRUE));
        oracle_case("notify_on_deleted_key");
        o_status("ret", st2);
        o_lasterr();
        o_str("event", wait_str(WaitForSingleObject(ev, 200)));
        oracle_case_end();
        RegCloseKey(k);
        CloseHandle(ev);
    }
    {   /* Parent watching with subtree when a child key is deleted. */
        HKEY k = fresh(L"parent");
        HKEY c = NULL;
        RegCreateKeyExW(k, L"child", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &c, NULL);
        RegCloseKey(c);
        HANDLE ev = CreateEventW(NULL, TRUE, FALSE, NULL);
        RegNotifyChangeKeyValue(k, TRUE, N, ev, TRUE);
        RegDeleteKeyW(k, L"child");
        oracle_case("subtree_child_deleted");
        o_str("event", wait_str(WaitForSingleObject(ev, 500)));
        oracle_case_end();
        RegCloseKey(k);
        CloseHandle(ev);
    }
    {   /* One-shot: after it fires, further changes do not fire again. */
        HKEY k = fresh(L"once");
        HANDLE ev = CreateEventW(NULL, TRUE, FALSE, NULL);
        RegNotifyChangeKeyValue(k, FALSE, L, ev, TRUE);
        setv(k, L"a", 1);
        DWORD w1 = WaitForSingleObject(ev, 500);
        ResetEvent(ev);
        setv(k, L"b", 2);
        DWORD w2 = WaitForSingleObject(ev, 300);
        oracle_case("one_shot");
        o_str("first", wait_str(w1));
        o_str("second_without_rearm", wait_str(w2));
        oracle_case_end();
        ResetEvent(ev);
        LONG st = RegNotifyChangeKeyValue(k, FALSE, L, ev, TRUE);
        setv(k, L"c", 3);
        oracle_case("rearm");
        o_status("ret", st);
        o_str("event", wait_str(WaitForSingleObject(ev, 500)));
        oracle_case_end();
        RegCloseKey(k);
        CloseHandle(ev);
    }
    {   /* Change before arming: not reported. Two changes, one signal. */
        HKEY k = fresh(L"before");
        HANDLE ev = CreateEventW(NULL, TRUE, FALSE, NULL);
        setv(k, L"early", 1);
        RegNotifyChangeKeyValue(k, FALSE, L, ev, TRUE);
        oracle_case("change_before_arming");
        o_str("event", wait_str(WaitForSingleObject(ev, 300)));
        oracle_case_end();
        RegCloseKey(k);
        CloseHandle(ev);
    }
    {   /* Two pending notifications on one handle, different events. */
        HKEY k = fresh(L"two");
        HANDLE e1 = CreateEventW(NULL, TRUE, FALSE, NULL), e2 = CreateEventW(NULL, TRUE, FALSE, NULL);
        LONG s1 = RegNotifyChangeKeyValue(k, FALSE, L, e1, TRUE);
        LONG s2 = RegNotifyChangeKeyValue(k, FALSE, N, e2, TRUE);
        setv(k, L"x", 1);
        DWORD w1 = WaitForSingleObject(e1, 500), w2 = WaitForSingleObject(e2, 300);
        oracle_case("two_on_one_handle_value_change");
        o_status("ret1", s1);
        o_status("ret2", s2);
        o_str("event_last_set", wait_str(w1));
        o_str("event_name", wait_str(w2));
        oracle_case_end();
        HKEY c = NULL;
        RegCreateKeyExW(k, L"c", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &c, NULL);
        RegCloseKey(c);
        oracle_case("two_on_one_handle_then_subkey");
        o_str("event_name", wait_str(WaitForSingleObject(e2, 500)));
        oracle_case_end();
        RegCloseKey(k);
        CloseHandle(e1);
        CloseHandle(e2);
    }
    {   /* Two handles to the same key; the change goes through a third. */
        HKEY k = fresh(L"multi");
        HKEY h1 = NULL, h2 = NULL;
        RegOpenKeyExW(root, L"multi", 0, KEY_NOTIFY, &h1);
        RegOpenKeyExW(root, L"multi", 0, KEY_NOTIFY, &h2);
        HANDLE e1 = CreateEventW(NULL, TRUE, FALSE, NULL), e2 = CreateEventW(NULL, TRUE, FALSE, NULL);
        RegNotifyChangeKeyValue(h1, FALSE, L, e1, TRUE);
        RegNotifyChangeKeyValue(h2, FALSE, L, e2, TRUE);
        setv(k, L"v", 1);
        oracle_case("two_handles_both_fire");
        o_str("event1", wait_str(WaitForSingleObject(e1, 500)));
        o_str("event2", wait_str(WaitForSingleObject(e2, 500)));
        oracle_case_end();
        RegCloseKey(h1);
        RegCloseKey(h2);
        RegCloseKey(k);
        CloseHandle(e1);
        CloseHandle(e2);
    }
    {   /* Access rights. */
        HKEY k = fresh(L"access");
        HANDLE ev = CreateEventW(NULL, TRUE, FALSE, NULL);
        static const struct { const char *name; REGSAM sam; } acc[] = {
            {"access_key_notify", KEY_NOTIFY},
            {"access_key_query_value", KEY_QUERY_VALUE},
            {"access_key_set_value", KEY_SET_VALUE},
            {"access_key_read", KEY_READ},
            {"access_key_write", KEY_WRITE},
        };
        for (unsigned i = 0; i < sizeof acc / sizeof acc[0]; i++) {
            HKEY h = NULL;
            LONG st;
            RegOpenKeyExW(root, L"access", 0, acc[i].sam, &h);
            ResetEvent(ev);
            ORACLE_CALL(st, RegNotifyChangeKeyValue(h, FALSE, L, ev, TRUE));
            oracle_case(acc[i].name);
            o_status("ret", st);
            o_lasterr();
            oracle_case_end();
            RegCloseKey(h);
        }
        RegCloseKey(k);
        CloseHandle(ev);
    }
    {   /* Synchronous mode: blocks until a change. */
        HKEY k = fresh(L"sync");
        struct sync_arg a = {k, L, FALSE, -1};
        HANDLE t = CreateThread(NULL, 0, sync_thread, &a, 0, NULL);
        DWORD before = WaitForSingleObject(t, 300);
        setv(k, L"v", 1);
        DWORD after = WaitForSingleObject(t, 2000);
        oracle_case("sync_blocks_until_change");
        o_str("before_change", wait_str(before));
        o_str("after_change", wait_str(after));
        o_status("ret", a.ret);
        oracle_case_end();
        CloseHandle(t);

        struct sync_arg b = {k, N, FALSE, -1};
        t = CreateThread(NULL, 0, sync_thread, &b, 0, NULL);
        WaitForSingleObject(t, 300);
        setv(k, L"w", 2);
        DWORD unmatched = WaitForSingleObject(t, 300);
        HKEY c = NULL;
        RegCreateKeyExW(k, L"c", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &c, NULL);
        RegCloseKey(c);
        DWORD matched = WaitForSingleObject(t, 2000);
        oracle_case("sync_name_filter");
        o_str("after_value_change", wait_str(unmatched));
        o_str("after_subkey_create", wait_str(matched));
        o_status("ret", b.ret);
        oracle_case_end();
        CloseHandle(t);

        struct sync_arg s = {k, L, TRUE, -1};
        t = CreateThread(NULL, 0, sync_thread, &s, 0, NULL);
        WaitForSingleObject(t, 300);
        HKEY cc = NULL;
        RegOpenKeyExW(k, L"c", 0, KEY_ALL_ACCESS, &cc);
        setv(cc, L"deep", 1);
        RegCloseKey(cc);
        DWORD sub = WaitForSingleObject(t, 2000);
        oracle_case("sync_subtree");
        o_str("after_subkey_value", wait_str(sub));
        o_status("ret", s.ret);
        oracle_case_end();
        CloseHandle(t);

        RegCloseKey(k);
    }
    {   /* Synchronous wait ended by closing the handle from another thread. */
        HKEY k = fresh(L"syncclose");
        struct sync_arg a = {k, L, FALSE, -1};
        HANDLE t = CreateThread(NULL, 0, sync_thread, &a, 0, NULL);
        WaitForSingleObject(t, 300);
        RegCloseKey(k);
        DWORD w = WaitForSingleObject(t, 2000);
        oracle_case("sync_close_handle");
        o_str("after_close", wait_str(w));
        if (w == WAIT_OBJECT_0) o_status("ret", a.ret);
        oracle_case_end();
        CloseHandle(t);
    }

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
