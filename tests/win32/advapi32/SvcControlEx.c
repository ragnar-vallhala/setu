/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: ControlServiceExW / ControlServiceExA with
 * SERVICE_CONTROL_STATUS_REASON_INFO: stopping with valid reason codes
 * (planned / unplanned / custom flags, major and minor values) and a
 * comment, invalid reasons, a comment over the documented 128-character
 * limit, NULL parameters, other info levels, non-stop controls through the
 * Ex function, and the SERVICE_STATUS_PROCESS it returns.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winsvc/nf-winsvc-controlserviceexw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winsvc/ns-winsvc-service_control_status_reason_paramsw
 */
#include "svc.h"

static SC_HANDLE scm;

static void ex_case(const char *name, SC_HANDLE s, DWORD code, DWORD level, DWORD reason, const wchar_t *comment,
                    int null_params)
{
    SERVICE_CONTROL_STATUS_REASON_PARAMSW p;
    BOOL r;
    memset(&p, 0xcc, sizeof p);
    p.dwReason = reason;
    p.pszComment = (wchar_t *)comment;
    ORACLE_CALL(r, ControlServiceExW(s, code, level, null_params ? NULL : &p));
    oracle_case(name);
    o_bool("ret", r);
    o_lasterr();
    if (!null_params && code == SERVICE_CONTROL_STOP && r) {
        /* The service reports STOPPED on another thread: either may be seen. */
        o_bool("ss.pending_or_stopped", p.ServiceStatus.dwCurrentState == SERVICE_STOP_PENDING ||
                                            p.ServiceStatus.dwCurrentState == SERVICE_STOPPED);
    } else if (!null_params) {
        SERVICE_STATUS st;
        st.dwServiceType = p.ServiceStatus.dwServiceType;
        st.dwCurrentState = p.ServiceStatus.dwCurrentState;
        st.dwControlsAccepted = p.ServiceStatus.dwControlsAccepted;
        st.dwWin32ExitCode = p.ServiceStatus.dwWin32ExitCode;
        st.dwServiceSpecificExitCode = p.ServiceStatus.dwServiceSpecificExitCode;
        st.dwCheckPoint = p.ServiceStatus.dwCheckPoint;
        st.dwWaitHint = p.ServiceStatus.dwWaitHint;
        o_svcstatus("ss", &st);
        o_bool("ss.has_pid", p.ServiceStatus.dwProcessId != 0 && p.ServiceStatus.dwProcessId != 0xcccccccc);
        o_hex32("ss.flags", p.ServiceStatus.dwServiceFlags);
    }
    if (code != SERVICE_CONTROL_STOP || !r) o_svclog("log");
    oracle_case_end();
    if (code == SERVICE_CONTROL_STOP && r) {
        /* Accepted: let it stop and start it again for the next case. */
        DWORD pid = p.ServiceStatus.dwProcessId;
        svc_wait_state(s, SERVICE_STOPPED);
        svc_wait_process_exit(pid);
        svc_log_clear();
        svc_start_wait(s, 0, NULL);
        svc_log_clear();
    }
}

#define PLANNED_APP (SERVICE_STOP_REASON_FLAG_PLANNED | SERVICE_STOP_REASON_MAJOR_APPLICATION | SERVICE_STOP_REASON_MINOR_MAINTENANCE)

int main(int argc, char **argv)
{
    int rc = svc_child_main("advapi32/SvcControlEx", argc, argv);
    if (rc >= 0) return rc;
    oracle_begin("advapi32/SvcControlEx");
    scm = svc_scm();
    const wchar_t *n = svc_name(1);
    svc_purge(scm, n);
    svc_log_clear();
    SC_HANDLE s = svc_create(scm, n, L"accept=0x3");
    SERVICE_STATUS_PROCESS st = svc_start_wait(s, 0, NULL);
    oracle_case("started");
    o_str("state", svc_state_name(st.dwCurrentState));
    oracle_case_end();
    svc_log_clear();

    ex_case("interrogate_level1", s, SERVICE_CONTROL_INTERROGATE, SERVICE_CONTROL_STATUS_REASON_INFO, PLANNED_APP,
            NULL, 0);
    ex_case("user_level1", s, 133, SERVICE_CONTROL_STATUS_REASON_INFO, PLANNED_APP, NULL, 0);
    ex_case("pause_level1", s, SERVICE_CONTROL_PAUSE, SERVICE_CONTROL_STATUS_REASON_INFO, PLANNED_APP, NULL, 0);
    svc_wait_state(s, SERVICE_PAUSED);
    ex_case("continue_level1", s, SERVICE_CONTROL_CONTINUE, SERVICE_CONTROL_STATUS_REASON_INFO, PLANNED_APP, NULL, 0);
    svc_wait_state(s, SERVICE_RUNNING);
    ex_case("level0", s, SERVICE_CONTROL_INTERROGATE, 0, PLANNED_APP, NULL, 0);
    ex_case("level2", s, SERVICE_CONTROL_INTERROGATE, 2, PLANNED_APP, NULL, 0);
    ex_case("level1_null_params", s, SERVICE_CONTROL_INTERROGATE, SERVICE_CONTROL_STATUS_REASON_INFO, 0, NULL, 1);
    ex_case("stop_reason_zero", s, SERVICE_CONTROL_STOP, SERVICE_CONTROL_STATUS_REASON_INFO, 0, NULL, 0);
    ex_case("stop_reason_no_flag", s, SERVICE_CONTROL_STOP, SERVICE_CONTROL_STATUS_REASON_INFO,
            SERVICE_STOP_REASON_MAJOR_APPLICATION | SERVICE_STOP_REASON_MINOR_OTHER, NULL, 0);
    ex_case("stop_reason_bad_major", s, SERVICE_CONTROL_STOP, SERVICE_CONTROL_STATUS_REASON_INFO,
            SERVICE_STOP_REASON_FLAG_PLANNED | 0x00070000 | SERVICE_STOP_REASON_MINOR_OTHER, NULL, 0);
    ex_case("stop_reason_bad_minor", s, SERVICE_CONTROL_STOP, SERVICE_CONTROL_STATUS_REASON_INFO,
            SERVICE_STOP_REASON_FLAG_PLANNED | SERVICE_STOP_REASON_MAJOR_APPLICATION | 0x19, NULL, 0);
    ex_case("stop_reason_two_flags", s, SERVICE_CONTROL_STOP, SERVICE_CONTROL_STATUS_REASON_INFO,
            SERVICE_STOP_REASON_FLAG_PLANNED | SERVICE_STOP_REASON_FLAG_UNPLANNED |
                SERVICE_STOP_REASON_MAJOR_APPLICATION | SERVICE_STOP_REASON_MINOR_OTHER,
            NULL, 0);
    {
        wchar_t longc[200];
        for (int i = 0; i < 199; i++) longc[i] = L'a' + i % 26;
        longc[199] = 0;
        ex_case("stop_comment_too_long", s, SERVICE_CONTROL_STOP, SERVICE_CONTROL_STATUS_REASON_INFO, PLANNED_APP,
                longc, 0);
    }
    ex_case("stop_level0", s, SERVICE_CONTROL_STOP, 0, PLANNED_APP, NULL, 0);
    ex_case("stop_null_params", s, SERVICE_CONTROL_STOP, SERVICE_CONTROL_STATUS_REASON_INFO, 0, NULL, 1);

    {
        SERVICE_CONTROL_STATUS_REASON_PARAMSW p;
        BOOL r;
        memset(&p, 0, sizeof p);
        svc_query(s, &st);
        DWORD pid = st.dwProcessId;
        p.dwReason = PLANNED_APP;
        p.pszComment = (wchar_t *)L"SetuOracle stop comment";
        ORACLE_CALL(r, ControlServiceExW(s, SERVICE_CONTROL_STOP, SERVICE_CONTROL_STATUS_REASON_INFO, &p));
        oracle_case("stop_planned_comment");
        o_bool("ret", r);
        o_lasterr();
        o_bool("ss.pending_or_stopped", p.ServiceStatus.dwCurrentState == SERVICE_STOP_PENDING ||
                                            p.ServiceStatus.dwCurrentState == SERVICE_STOPPED);
        oracle_case_end();
        st = svc_wait_state(s, SERVICE_STOPPED);
        svc_wait_process_exit(pid);
        oracle_case("stopped");
        o_str("state", svc_state_name(st.dwCurrentState));
        o_svclog("log");
        oracle_case_end();
    }
    ex_case("stop_when_stopped", s, SERVICE_CONTROL_STOP, SERVICE_CONTROL_STATUS_REASON_INFO, PLANNED_APP, NULL, 0);

    /* Custom reason, through the A function. */
    st = svc_start_wait(s, 0, NULL);
    DWORD pid = st.dwProcessId;
    svc_log_clear();
    {
        SERVICE_CONTROL_STATUS_REASON_PARAMSA p;
        BOOL r;
        memset(&p, 0, sizeof p);
        p.dwReason = SERVICE_STOP_REASON_FLAG_CUSTOM | SERVICE_STOP_REASON_MAJOR_MIN_CUSTOM |
                     SERVICE_STOP_REASON_MINOR_MIN_CUSTOM;
        p.pszComment = (char *)"SetuOracle custom";
        ORACLE_CALL(r, ControlServiceExA(s, SERVICE_CONTROL_STOP, SERVICE_CONTROL_STATUS_REASON_INFO, &p));
        oracle_case("ansi_stop_custom");
        o_bool("ret", r);
        o_lasterr();
        o_bool("ss.pending_or_stopped", p.ServiceStatus.dwCurrentState == SERVICE_STOP_PENDING ||
                                            p.ServiceStatus.dwCurrentState == SERVICE_STOPPED);
        oracle_case_end();
        st = svc_wait_state(s, SERVICE_STOPPED);
        svc_wait_process_exit(pid);
        oracle_case("ansi_stopped");
        o_str("state", svc_state_name(st.dwCurrentState));
        o_svclog("log");
        oracle_case_end();
    }

    CloseServiceHandle(s);
    svc_purge(scm, n);
    CloseServiceHandle(scm);
    svc_log_remove();
    return oracle_end();
}
