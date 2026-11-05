/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: NotifyServiceStatusChangeW. Registering for a state the
 * service is already in (the callback is queued at once), for a state it
 * reaches later (RUNNING after StartService, STOPPED after a stop, PAUSED),
 * the fields of SERVICE_NOTIFYW in the callback (version, status, the
 * notification that triggered, the service status), a pending registration
 * on a second handle, an invalid version, a mask of 0, notifications on an
 * SCM handle (SERVICE_NOTIFY_CREATED / _DELETED: whether our name is
 * reported, with its documented '/' prefix), and SERVICE_NOTIFY_DELETE_PENDING
 * on the service handle.
 *
 * Callbacks are APCs: the test waits in alertable SleepEx until the callback
 * has run, so only what happened is printed, never when.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winsvc/nf-winsvc-notifyservicestatuschangew
 *       https://learn.microsoft.com/en-us/windows/win32/api/winsvc/ns-winsvc-service_notify_2w
 */
#include "svc.h"

static SC_HANDLE scm;
static volatile LONG fired;
static SERVICE_NOTIFYW last;
static wchar_t last_names[2048];

static void CALLBACK notify_cb(void *param)
{
    SERVICE_NOTIFYW *n = param;
    last = *n;
    last_names[0] = 0;
    if (n->pszServiceNames) {
        size_t o = 0;
        for (wchar_t *p = n->pszServiceNames; *p && o < 1900; p += wcslen(p) + 1) {
            /* Keep only our own names: other services may come and go. */
            if (!wcsstr(p, L"SetuOracle_SvcNotify_")) continue;
            if (o) last_names[o++] = L',';
            wcsncpy(last_names + o, p, 1900 - o);
            o += wcslen(p);
            last_names[o] = 0;
        }
        LocalFree(n->pszServiceNames);
    }
    InterlockedIncrement(&fired);
}

/* Waits (alertable) until the callback ran or ~4 s passed. */
static int wait_fired(LONG before)
{
    for (int i = 0; i < 200 && fired == before; i++) SleepEx(20, TRUE);
    return fired != before;
}

static void o_notify(void)
{
    o_u32("n.version", last.dwVersion);
    o_u32("n.status", last.dwNotificationStatus);
    o_hex32("n.triggered", last.dwNotificationTriggered);
    o_str("n.state", svc_state_name(last.ServiceStatus.dwCurrentState));
    o_hex32("n.type", last.ServiceStatus.dwServiceType);
    o_hex32("n.accepted", last.ServiceStatus.dwControlsAccepted);
    o_u32("n.win32_exit", last.ServiceStatus.dwWin32ExitCode);
    o_bool("n.has_pid", last.ServiceStatus.dwProcessId != 0);
}

static SERVICE_NOTIFYW *mk(void)
{
    static SERVICE_NOTIFYW pool[64];
    static int idx;
    SERVICE_NOTIFYW *n = &pool[idx++ & 63];
    memset(n, 0, sizeof *n);
    n->dwVersion = SERVICE_NOTIFY_STATUS_CHANGE;
    n->pfnNotifyCallback = notify_cb;
    n->pContext = n;
    return n;
}

int main(int argc, char **argv)
{
    int rc = svc_child_main("advapi32/SvcNotify", argc, argv);
    if (rc >= 0) return rc;
    oracle_begin("advapi32/SvcNotify");
    scm = svc_scm();
    for (int i = 1; i <= 3; i++) svc_purge(scm, svc_name(i));
    svc_log_clear();
    DWORD r;
    LONG before;

    const wchar_t *n1 = svc_name(1);
    SC_HANDLE s = svc_create(scm, n1, L"accept=0x3");

    /* Already in the requested state. */
    before = fired;
    ORACLE_CALL(r, NotifyServiceStatusChangeW(s, SERVICE_NOTIFY_STOPPED, mk()));
    oracle_case("already_stopped");
    o_u32("ret", r);
    o_lasterr();
    o_bool("fired", wait_fired(before));
    if (fired != before) o_notify();
    oracle_case_end();

    /* RUNNING after a start. */
    before = fired;
    ORACLE_CALL(r, NotifyServiceStatusChangeW(s, SERVICE_NOTIFY_RUNNING, mk()));
    oracle_case("running_registered");
    o_u32("ret", r);
    o_lasterr();
    SleepEx(0, TRUE);
    o_bool("fired_before_start", fired != before);
    oracle_case_end();
    StartServiceW(s, 0, NULL);
    oracle_case("running_after_start");
    o_bool("fired", wait_fired(before));
    if (fired != before) o_notify();
    oracle_case_end();
    svc_wait_state(s, SERVICE_RUNNING);

    /* PAUSED, through a mask of several states. */
    before = fired;
    ORACLE_CALL(r, NotifyServiceStatusChangeW(s, SERVICE_NOTIFY_PAUSED | SERVICE_NOTIFY_STOPPED, mk()));
    SERVICE_STATUS ss;
    ControlService(s, SERVICE_CONTROL_PAUSE, &ss);
    oracle_case("paused");
    o_u32("ret", r);
    o_bool("fired", wait_fired(before));
    if (fired != before) o_notify();
    oracle_case_end();
    svc_wait_state(s, SERVICE_PAUSED);
    ControlService(s, SERVICE_CONTROL_CONTINUE, &ss);
    svc_wait_state(s, SERVICE_RUNNING);

    /* A registration while another is pending on the same handle. */
    {
        before = fired;
        ORACLE_CALL(r, NotifyServiceStatusChangeW(s, SERVICE_NOTIFY_STOPPED, mk()));
        DWORD r2;
        ORACLE_CALL(r2, NotifyServiceStatusChangeW(s, SERVICE_NOTIFY_STOPPED, mk()));
        oracle_case("second_pending_same_handle");
        o_u32("ret1", r);
        o_u32("ret2", r2);
        oracle_case_end();
        /* And one on another handle. */
        SC_HANDLE s2 = OpenServiceW(scm, n1, SERVICE_ALL_ACCESS);
        DWORD r3;
        LONG before_all = fired;
        ORACLE_CALL(r3, NotifyServiceStatusChangeW(s2, SERVICE_NOTIFY_STOPPED, mk()));
        oracle_case("pending_other_handle");
        o_u32("ret", r3);
        oracle_case_end();
        SERVICE_STATUS_PROCESS st;
        svc_query(s, &st);
        svc_stop_wait(s);
        for (int i = 0; i < 500 && fired - before_all < (r3 ? 1 : 2); i++) SleepEx(20, TRUE);
        SleepEx(100, TRUE);
        oracle_case("stopped_both_handles");
        o_u32("callbacks", (DWORD)(fired - before_all));
        o_notify();
        oracle_case_end();
        CloseServiceHandle(s2);
    }

    /* Bad parameters. */
    {
        SERVICE_NOTIFYW *n = mk();
        n->dwVersion = 1;
        ORACLE_CALL(r, NotifyServiceStatusChangeW(s, SERVICE_NOTIFY_RUNNING, n));
        oracle_case("version_1");
        o_u32("ret", r);
        o_lasterr();
        oracle_case_end();
        n = mk();
        n->dwVersion = 7;
        ORACLE_CALL(r, NotifyServiceStatusChangeW(s, SERVICE_NOTIFY_RUNNING, n));
        oracle_case("version_7");
        o_u32("ret", r);
        o_lasterr();
        oracle_case_end();
        SC_HANDLE q = OpenServiceW(scm, n1, SERVICE_QUERY_CONFIG);
        ORACLE_CALL(r, NotifyServiceStatusChangeW(q, SERVICE_NOTIFY_RUNNING, mk()));
        oracle_case("no_query_status_right");
        o_u32("ret", r);
        o_lasterr();
        oracle_case_end();
        CloseServiceHandle(q);
        ORACLE_CALL(r, NotifyServiceStatusChangeW(NULL, SERVICE_NOTIFY_RUNNING, mk()));
        oracle_case("null_handle");
        o_u32("ret", r);
        o_lasterr();
        oracle_case_end();
    }

    /* Mask 0, then closing the handle with the registration pending. */
    {
        SC_HANDLE s3 = OpenServiceW(scm, n1, SERVICE_ALL_ACCESS);
        before = fired;
        ORACLE_CALL(r, NotifyServiceStatusChangeW(s3, 0, mk()));
        oracle_case("mask_zero");
        o_u32("ret", r);
        o_lasterr();
        SleepEx(200, TRUE);
        o_bool("fired", fired != before);
        oracle_case_end();
        s3 = OpenServiceW(scm, n1, SERVICE_ALL_ACCESS);
        before = fired;
        NotifyServiceStatusChangeW(s3, SERVICE_NOTIFY_RUNNING, mk());
        BOOL c;
        ORACLE_CALL(c, CloseServiceHandle(s3));
        oracle_case("close_with_pending");
        o_bool("ret", c);
        o_lasterr();
        SleepEx(200, TRUE);
        o_bool("fired", fired != before);
        oracle_case_end();
    }

    /* SCM-level: created and deleted services. */
    {
        SC_HANDLE m = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
        const wchar_t *n2 = svc_name(2);
        before = fired;
        ORACLE_CALL(r, NotifyServiceStatusChangeW(m, SERVICE_NOTIFY_CREATED, mk()));
        oracle_case("scm_created_registered");
        o_u32("ret", r);
        o_lasterr();
        oracle_case_end();
        SC_HANDLE c = svc_create(scm, n2, L"");
        int got = 0;
        /* Other services may be created meanwhile: wait for ours. */
        for (int i = 0; i < 3 && !got; i++) {
            if (!wait_fired(before)) break;
            got = last_names[0] != 0;
            if (!got) {
                before = fired;
                NotifyServiceStatusChangeW(m, SERVICE_NOTIFY_CREATED, mk());
            }
        }
        oracle_case("scm_created");
        o_bool("fired", got);
        if (got) {
            o_hex32("n.triggered", last.dwNotificationTriggered);
            o_wstr("names", last_names);
        }
        oracle_case_end();
        before = fired;
        ORACLE_CALL(r, NotifyServiceStatusChangeW(m, SERVICE_NOTIFY_DELETED, mk()));
        DeleteService(c);
        CloseServiceHandle(c);
        got = 0;
        for (int i = 0; i < 3 && !got; i++) {
            if (!wait_fired(before)) break;
            got = last_names[0] != 0;
            if (!got) {
                before = fired;
                NotifyServiceStatusChangeW(m, SERVICE_NOTIFY_DELETED, mk());
            }
        }
        oracle_case("scm_deleted");
        o_u32("ret", r);
        o_bool("fired", got);
        if (got) {
            o_hex32("n.triggered", last.dwNotificationTriggered);
            o_wstr("names", last_names);
        }
        oracle_case_end();
        ORACLE_CALL(r, NotifyServiceStatusChangeW(m, SERVICE_NOTIFY_RUNNING, mk()));
        oracle_case("scm_running_mask");
        o_u32("ret", r);
        o_lasterr();
        oracle_case_end();
        CloseServiceHandle(m);
    }

    /* DELETE_PENDING on a service handle. */
    {
        const wchar_t *n3 = svc_name(3);
        SC_HANDLE d = svc_create(scm, n3, L"");
        SC_HANDLE keep = OpenServiceW(scm, n3, SERVICE_ALL_ACCESS);
        before = fired;
        ORACLE_CALL(r, NotifyServiceStatusChangeW(keep, SERVICE_NOTIFY_DELETE_PENDING, mk()));
        DeleteService(d);
        oracle_case("delete_pending");
        o_u32("ret", r);
        o_lasterr();
        o_bool("fired", wait_fired(before));
        if (fired != before) o_hex32("n.triggered", last.dwNotificationTriggered);
        oracle_case_end();
        CloseServiceHandle(d);
        CloseServiceHandle(keep);
        svc_purge(scm, n3);
    }

    CloseServiceHandle(s);
    for (int i = 1; i <= 3; i++) svc_purge(scm, svc_name(i));
    svc_log_clear();
    CloseServiceHandle(scm);
    svc_log_remove();
    return oracle_end();
}
