/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: ControlService. Against a running service (this program, see
 * svc.h): STOP, PAUSE / CONTINUE (accepted or not), INTERROGATE, the
 * status structure returned for each, user-defined codes 128-255 (the
 * handler's NO_ERROR, ERROR_INVALID_DATA and ERROR_CALL_NOT_IMPLEMENTED
 * returns), codes the docs reserve (0, 11-127 such as PARAMCHANGE and
 * NETBIND*, 256 and up), SHUTDOWN / PRESHUTDOWN (which applications may not
 * send), controls on stopped and pending services, and the access right
 * each control needs. The control codes and event types the service's
 * handler receives are printed from its log.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winsvc/nf-winsvc-controlservice
 *       https://learn.microsoft.com/en-us/windows/win32/api/winsvc/nc-winsvc-lphandler_function_ex
 *       https://learn.microsoft.com/en-us/windows/win32/services/service-control-handler-function
 */
#include "svc.h"

static SC_HANDLE scm;

static void ctl_case(const char *name, SC_HANDLE s, DWORD code)
{
    SERVICE_STATUS ss;
    BOOL r;
    memset(&ss, 0xcc, sizeof ss);
    ORACLE_CALL(r, ControlService(s, code, &ss));
    oracle_case(name);
    o_bool("ret", r);
    o_lasterr();
    if (code == SERVICE_CONTROL_STOP && r)
        /* The service reports STOPPED on another thread: either may be seen. */
        o_bool("ss.pending_or_stopped", ss.dwCurrentState == SERVICE_STOP_PENDING || ss.dwCurrentState == SERVICE_STOPPED);
    else
        o_svcstatus("ss", &ss);
    /* The handler runs before ControlService returns, so the log is complete
     * (except for STOP, where the service's main thread reports later). */
    if (code != SERVICE_CONTROL_STOP) o_svclog("log");
    oracle_case_end();
}

int main(int argc, char **argv)
{
    int rc = svc_child_main("advapi32/SvcControl", argc, argv);
    if (rc >= 0) return rc;
    oracle_begin("advapi32/SvcControl");
    scm = svc_scm();
    for (int i = 1; i <= 4; i++) svc_purge(scm, svc_name(i));
    svc_log_clear();

    /* Accepts STOP and PAUSE_CONTINUE. */
    const wchar_t *n = svc_name(1);
    SC_HANDLE s = svc_create(scm, n, L"accept=0x3");
    SERVICE_STATUS_PROCESS st = svc_start_wait(s, 0, NULL);
    DWORD pid = st.dwProcessId;
    oracle_case("started");
    o_svcstatus_ex("st", &st);
    oracle_case_end();
    svc_log_clear();

    ctl_case("interrogate", s, SERVICE_CONTROL_INTERROGATE);
    ctl_case("pause", s, SERVICE_CONTROL_PAUSE);
    st = svc_wait_state(s, SERVICE_PAUSED);
    ctl_case("pause_again", s, SERVICE_CONTROL_PAUSE);
    ctl_case("interrogate_paused", s, SERVICE_CONTROL_INTERROGATE);
    ctl_case("continue", s, SERVICE_CONTROL_CONTINUE);
    svc_wait_state(s, SERVICE_RUNNING);
    ctl_case("continue_running", s, SERVICE_CONTROL_CONTINUE);
    ctl_case("user_128", s, 128);
    ctl_case("user_200_handler_error", s, 200);
    ctl_case("user_201_not_implemented", s, 201);
    ctl_case("user_255", s, 255);
    ctl_case("code_256", s, 256);
    ctl_case("code_0", s, 0);
    ctl_case("code_127", s, 127);
    ctl_case("paramchange_not_accepted", s, SERVICE_CONTROL_PARAMCHANGE);
    ctl_case("netbindadd_not_accepted", s, SERVICE_CONTROL_NETBINDADD);
    ctl_case("shutdown", s, SERVICE_CONTROL_SHUTDOWN);
    ctl_case("preshutdown", s, SERVICE_CONTROL_PRESHUTDOWN);
    ctl_case("devicevent", s, SERVICE_CONTROL_DEVICEEVENT);
    ctl_case("timechange", s, SERVICE_CONTROL_TIMECHANGE);
    ctl_case("code_1000", s, 1000);

    /* A user-defined code whose handler changes the state. */
    {
        ctl_case("user_202_sets_status", s, 202);
        SERVICE_STATUS ss;
        QueryServiceStatus(s, &ss);
        oracle_case("after_202");
        o_svcstatus("ss", &ss);
        oracle_case_end();
    }

    /* Access rights. */
    {
        SC_HANDLE q = OpenServiceW(scm, n, SERVICE_QUERY_STATUS);
        ctl_case("no_right_interrogate", q, SERVICE_CONTROL_INTERROGATE);
        ctl_case("no_right_user", q, 130);
        ctl_case("no_right_stop", q, SERVICE_CONTROL_STOP);
        ctl_case("no_right_pause", q, SERVICE_CONTROL_PAUSE);
        CloseServiceHandle(q);
        q = OpenServiceW(scm, n, SERVICE_USER_DEFINED_CONTROL);
        ctl_case("user_right_user", q, 130);
        ctl_case("user_right_interrogate", q, SERVICE_CONTROL_INTERROGATE);
        CloseServiceHandle(q);
        q = OpenServiceW(scm, n, SERVICE_INTERROGATE);
        ctl_case("interrogate_right", q, SERVICE_CONTROL_INTERROGATE);
        CloseServiceHandle(q);
        q = OpenServiceW(scm, n, SERVICE_PAUSE_CONTINUE);
        ctl_case("pause_right_user", q, 130);
        CloseServiceHandle(q);
    }

    ctl_case("stop", s, SERVICE_CONTROL_STOP);
    st = svc_wait_state(s, SERVICE_STOPPED);
    svc_wait_process_exit(pid);
    oracle_case("stopped");
    o_svcstatus_ex("st", &st);
    o_svclog("log");
    oracle_case_end();
    ctl_case("stop_stopped", s, SERVICE_CONTROL_STOP);
    ctl_case("interrogate_stopped", s, SERVICE_CONTROL_INTERROGATE);
    ctl_case("user_stopped", s, 128);
    ctl_case("pause_stopped", s, SERVICE_CONTROL_PAUSE);
    CloseServiceHandle(s);

    /* Accepts only STOP: pause is refused by the SCM, never reaches the handler. */
    {
        n = svc_name(2);
        s = svc_create(scm, n, L"accept=0x1 exit=1234");
        st = svc_start_wait(s, 0, NULL);
        pid = st.dwProcessId;
        svc_log_clear();
        ctl_case("stoponly_pause", s, SERVICE_CONTROL_PAUSE);
        ctl_case("stoponly_continue", s, SERVICE_CONTROL_CONTINUE);
        ctl_case("stoponly_user", s, 140);
        ctl_case("stoponly_stop", s, SERVICE_CONTROL_STOP);
        st = svc_wait_state(s, SERVICE_STOPPED);
        svc_wait_process_exit(pid);
        oracle_case("stoponly_stopped_exit_code");
        o_svcstatus_ex("st", &st);
        o_svclog("log");
        oracle_case_end();
        CloseServiceHandle(s);
    }

    /* Accepts nothing. */
    {
        n = svc_name(3);
        s = svc_create(scm, n, L"accept=0x0");
        st = svc_start_wait(s, 0, NULL);
        pid = st.dwProcessId;
        svc_log_clear();
        ctl_case("none_stop", s, SERVICE_CONTROL_STOP);
        ctl_case("none_interrogate", s, SERVICE_CONTROL_INTERROGATE);
        ctl_case("none_user", s, 150);
        CloseServiceHandle(s);
        svc_purge(scm, n); /* kills it */
        svc_log_clear();
    }

    /* Stop that stays in STOP_PENDING until released. */
    {
        n = svc_name(4);
        s = svc_create(scm, n, L"accept=0x3 stoppending");
        st = svc_start_wait(s, 0, NULL);
        pid = st.dwProcessId;
        HANDLE ack = svc_event(n, L"ack"), go = svc_event(n, L"go");
        svc_log_clear();
        ctl_case("sp_stop", s, SERVICE_CONTROL_STOP);
        WaitForSingleObject(ack, 20000);
        SERVICE_STATUS ss;
        QueryServiceStatus(s, &ss);
        oracle_case("sp_pending_status");
        o_svcstatus("ss", &ss);
        oracle_case_end();
        ctl_case("sp_stop_again", s, SERVICE_CONTROL_STOP);
        ctl_case("sp_interrogate", s, SERVICE_CONTROL_INTERROGATE);
        ctl_case("sp_pause", s, SERVICE_CONTROL_PAUSE);
        ctl_case("sp_user", s, 128);
        SetEvent(go);
        st = svc_wait_state(s, SERVICE_STOPPED);
        svc_wait_process_exit(pid);
        oracle_case("sp_released");
        o_svcstatus_ex("st", &st);
        o_svclog("log");
        oracle_case_end();
        CloseHandle(ack);
        CloseHandle(go);
        CloseServiceHandle(s);
    }

    {
        SERVICE_STATUS ss;
        BOOL r;
        ORACLE_CALL(r, ControlService(NULL, SERVICE_CONTROL_INTERROGATE, &ss));
        oracle_case("null_handle");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
    }

    for (int i = 1; i <= 4; i++) svc_purge(scm, svc_name(i));
    CloseServiceHandle(scm);
    svc_log_remove();
    return oracle_end();
}
