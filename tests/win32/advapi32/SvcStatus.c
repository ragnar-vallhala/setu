/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: QueryServiceStatus and QueryServiceStatusEx: a never-started
 * service, a running one, one held in START_PENDING / STOP_PENDING with
 * checkpoints and wait hints, one that stopped with Win32 and
 * service-specific exit codes; buffer sizes (NULL, too small, exact) and
 * bytes needed; invalid info levels; the access right needed; the
 * dwServiceFlags and process id (only whether it is set) fields. Also
 * SetServiceStatus from the service side with invalid states, types and
 * controls-accepted values, and a NULL status handle.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winsvc/nf-winsvc-queryservicestatus
 *       https://learn.microsoft.com/en-us/windows/win32/api/winsvc/nf-winsvc-queryservicestatusex
 *       https://learn.microsoft.com/en-us/windows/win32/api/winsvc/ns-winsvc-service_status_process
 *       https://learn.microsoft.com/en-us/windows/win32/api/winsvc/nf-winsvc-setservicestatus
 */
#include "svc.h"

static SC_HANDLE scm;

static void status_case(const char *name, SC_HANDLE s)
{
    SERVICE_STATUS ss;
    SERVICE_STATUS_PROCESS sp;
    DWORD need = 0xdead;
    BOOL r;
    oracle_case(name);
    memset(&ss, 0xcc, sizeof ss);
    ORACLE_CALL(r, QueryServiceStatus(s, &ss));
    o_bool("ret", r);
    o_lasterr();
    if (r) o_svcstatus("ss", &ss);
    memset(&sp, 0xcc, sizeof sp);
    ORACLE_CALL(r, QueryServiceStatusEx(s, SC_STATUS_PROCESS_INFO, (BYTE *)&sp, sizeof sp, &need));
    o_bool("ex.ret", r);
    o_u32("ex.err", r ? 0 : oracle_last_error);
    o_bool("ex.need_unchanged", need == 0xdead);
    if (r) o_svcstatus_ex("sp", &sp);
    oracle_case_end();
}

int main(int argc, char **argv)
{
    int rc = svc_child_main("advapi32/SvcStatus", argc, argv);
    if (rc >= 0) return rc;
    oracle_begin("advapi32/SvcStatus");
    scm = svc_scm();
    for (int i = 1; i <= 4; i++) svc_purge(scm, svc_name(i));
    svc_log_clear();
    BOOL r;

    const wchar_t *n = svc_name(1);
    SC_HANDLE s = svc_create(scm, n, L"accept=0x3 exit=1066 specific=77");
    status_case("never_started", s);

    /* Buffer handling. */
    {
        BYTE buf[64];
        DWORD need = 0xdead;
        ORACLE_CALL(r, QueryServiceStatusEx(s, SC_STATUS_PROCESS_INFO, NULL, 0, &need));
        oracle_case("ex_null_buffer");
        o_bool("ret", r);
        o_lasterr();
        o_u32("need", need);
        oracle_case_end();
        need = 0xdead;
        ORACLE_CALL(r, QueryServiceStatusEx(s, SC_STATUS_PROCESS_INFO, buf, sizeof(SERVICE_STATUS_PROCESS) - 1, &need));
        oracle_case("ex_one_short");
        o_bool("ret", r);
        o_lasterr();
        o_u32("need", need);
        oracle_case_end();
        need = 0xdead;
        ORACLE_CALL(r, QueryServiceStatusEx(s, SC_STATUS_PROCESS_INFO, buf, sizeof buf, &need));
        oracle_case("ex_large_buffer");
        o_bool("ret", r);
        o_lasterr();
        o_bool("need_unchanged", need == 0xdead);
        oracle_case_end();
        ORACLE_CALL(r, QueryServiceStatusEx(s, 1, buf, sizeof buf, &need));
        oracle_case("ex_level_1");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, QueryServiceStatusEx(scm, SC_STATUS_PROCESS_INFO, buf, sizeof buf, &need));
        oracle_case("ex_scm_handle");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, QueryServiceStatusEx(NULL, SC_STATUS_PROCESS_INFO, buf, sizeof buf, &need));
        oracle_case("ex_null_handle");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        SC_HANDLE q = OpenServiceW(scm, n, SERVICE_QUERY_CONFIG);
        status_case("no_query_status_right", q);
        CloseServiceHandle(q);
    }

    /* Running, paused, stopped with exit codes. */
    {
        SERVICE_STATUS_PROCESS st = svc_start_wait(s, 0, NULL);
        DWORD pid = st.dwProcessId;
        status_case("running", s);
        SERVICE_STATUS ss;
        ControlService(s, SERVICE_CONTROL_PAUSE, &ss);
        svc_wait_state(s, SERVICE_PAUSED);
        status_case("paused", s);
        ControlService(s, SERVICE_CONTROL_STOP, &ss);
        svc_wait_state(s, SERVICE_STOPPED);
        svc_wait_process_exit(pid);
        status_case("stopped_with_exit_codes", s);
        oracle_case("log1");
        o_svclog("log");
        oracle_case_end();
    }

    /* START_PENDING then STOP_PENDING held by the service. */
    {
        const wchar_t *n2 = svc_name(2);
        SC_HANDLE p = svc_create(scm, n2, L"pending stoppending accept=0x1");
        HANDLE ack = svc_event(n2, L"ack"), go = svc_event(n2, L"go");
        StartServiceW(p, 0, NULL);
        WaitForSingleObject(ack, 20000);
        status_case("start_pending", p);
        SetEvent(go);
        SERVICE_STATUS_PROCESS st = svc_wait_state(p, SERVICE_RUNNING);
        DWORD pid = st.dwProcessId;
        SERVICE_STATUS ss;
        ControlService(p, SERVICE_CONTROL_STOP, &ss);
        WaitForSingleObject(ack, 20000);
        status_case("stop_pending", p);
        SetEvent(go);
        svc_wait_state(p, SERVICE_STOPPED);
        svc_wait_process_exit(pid);
        status_case("stopped_clean", p);
        oracle_case("log2");
        o_svclog("log");
        oracle_case_end();
        CloseHandle(ack);
        CloseHandle(go);
        CloseServiceHandle(p);
    }

    /* Invalid SetServiceStatus calls from the service. */
    {
        const wchar_t *n3 = svc_name(3);
        SC_HANDLE b = svc_create(scm, n3, L"badstatus");
        SERVICE_STATUS_PROCESS st = svc_start_wait(b, 0, NULL);
        svc_wait_log(L"restore=");
        status_case("after_bad_status", b);
        oracle_case("log3");
        o_svclog("log");
        oracle_case_end();
        svc_stop_wait(b);
        (void)st;
        svc_log_clear();
        CloseServiceHandle(b);
    }

    /* A service with the interactive flag reports it in dwServiceType? */
    {
        const wchar_t *n4 = svc_name(4);
        svc_purge(scm, n4);
        SC_HANDLE i = CreateServiceW(scm, n4, n4, SERVICE_ALL_ACCESS,
                                     SERVICE_WIN32_OWN_PROCESS | SERVICE_INTERACTIVE_PROCESS, SERVICE_DEMAND_START,
                                     SERVICE_ERROR_IGNORE, svc_cmdline(n4, L""), NULL, NULL, NULL, NULL, NULL);
        status_case("interactive_never_started", i);
        CloseServiceHandle(i);
    }

    CloseServiceHandle(s);
    for (int i = 1; i <= 4; i++) svc_purge(scm, svc_name(i));
    CloseServiceHandle(scm);
    svc_log_remove();
    return oracle_end();
}
