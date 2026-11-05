/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: RegNotifyChangeKeyValue and the registering thread. An
 * asynchronous notification registered on a thread that then exits, with and
 * without REG_NOTIFY_THREAD_AGNOSTIC; whether a later change still signals
 * the event; and a registration made on one thread for a handle that another
 * thread then closes. Every wait is bounded and only the signalled state is
 * printed.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regnotifychangekeyvalue
 */
#include "oracle.h"

#ifndef REG_NOTIFY_THREAD_AGNOSTIC
#define REG_NOTIFY_THREAD_AGNOSTIC 0x10000000L
#endif

static HKEY root;

struct reg_args { HKEY key; HANDLE event; DWORD filter; LONG ret; };

static DWORD WINAPI register_thread(void *p)
{
    struct reg_args *a = p;
    a->ret = RegNotifyChangeKeyValue(a->key, FALSE, a->filter, a->event, TRUE);
    return 0;
}

/* Registers on a short-lived thread and waits for it to exit. */
static LONG register_on_thread(HKEY key, HANDLE event, DWORD filter)
{
    struct reg_args a = {key, event, filter, -1};
    HANDLE th = CreateThread(NULL, 0, register_thread, &a, 0, NULL);
    WaitForSingleObject(th, 5000);
    CloseHandle(th);
    return a.ret;
}

static BOOL signalled(HANDLE e, DWORD ms) { return WaitForSingleObject(e, ms) == WAIT_OBJECT_0; }

static void run(const char *name, DWORD filter)
{
    HKEY k = NULL;
    HANDLE e = CreateEventW(NULL, TRUE, FALSE, NULL);
    DWORD v = 1;
    RegCreateKeyExW(root, L"w", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL);
    LONG st = register_on_thread(k, e, filter);
    oracle_case(name);
    o_status("register", st);
    o_bool("signalled_after_thread_exit", signalled(e, 500));
    ResetEvent(e);
    RegSetValueExW(k, L"v", 0, REG_DWORD, (const BYTE *)&v, 4);
    o_bool("signalled_after_change", signalled(e, 2000));
    oracle_case_end();
    RegCloseKey(k);
    RegDeleteKeyW(root, L"w");
    CloseHandle(e);
}

int main(void)
{
    oracle_begin("advapi32/RegNotifyChangeKeyValue_thread");
    root = oracle_reg_root();

    run("thread_exit_last_set", REG_NOTIFY_CHANGE_LAST_SET);
    run("thread_exit_agnostic_last_set", REG_NOTIFY_CHANGE_LAST_SET | REG_NOTIFY_THREAD_AGNOSTIC);
    run("thread_exit_name", REG_NOTIFY_CHANGE_NAME);
    run("thread_exit_agnostic_name", REG_NOTIFY_CHANGE_NAME | REG_NOTIFY_THREAD_AGNOSTIC);

    /* Registered on this thread; the handle is closed on another. */
    {
        HKEY k = NULL;
        HANDLE e = CreateEventW(NULL, TRUE, FALSE, NULL);
        RegCreateKeyExW(root, L"w2", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL);
        LONG st = RegNotifyChangeKeyValue(k, FALSE, REG_NOTIFY_CHANGE_LAST_SET, e, TRUE);
        HANDLE th = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)(void *)RegCloseKey, k, 0, NULL);
        WaitForSingleObject(th, 5000);
        CloseHandle(th);
        oracle_case("close_on_other_thread");
        o_status("register", st);
        o_bool("signalled", signalled(e, 2000));
        oracle_case_end();
        RegDeleteKeyW(root, L"w2");
        CloseHandle(e);
    }

    /* Two registrations from two threads on one handle; one thread exits. */
    {
        HKEY k = NULL;
        HANDLE e1 = CreateEventW(NULL, TRUE, FALSE, NULL), e2 = CreateEventW(NULL, TRUE, FALSE, NULL);
        DWORD v = 2;
        RegCreateKeyExW(root, L"w3", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL);
        LONG st1 = register_on_thread(k, e1, REG_NOTIFY_CHANGE_LAST_SET);
        LONG st2 = RegNotifyChangeKeyValue(k, FALSE, REG_NOTIFY_CHANGE_LAST_SET, e2, TRUE);
        oracle_case("exited_thread_and_live_thread");
        o_status("register_exited", st1);
        o_status("register_live", st2);
        o_bool("exited_signalled_early", signalled(e1, 500));
        o_bool("live_signalled_early", signalled(e2, 0));
        RegSetValueExW(k, L"v", 0, REG_DWORD, (const BYTE *)&v, 4);
        o_bool("live_signalled_after_change", signalled(e2, 2000));
        oracle_case_end();
        RegCloseKey(k);
        RegDeleteKeyW(root, L"w3");
        CloseHandle(e1);
        CloseHandle(e2);
    }

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
