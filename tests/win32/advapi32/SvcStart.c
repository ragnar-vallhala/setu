/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: StartServiceW / StartServiceA. Arguments as the service sees
 * them (argv[0] is the service name; none, several, empty strings, spaces,
 * argc 0 with a non-NULL vector, ANSI arguments), the state right after
 * StartService returns, starting a running service
 * (ERROR_SERVICE_ALREADY_RUNNING), a disabled one (ERROR_SERVICE_DISABLED),
 * one whose binary is missing, one marked for delete, a service whose
 * ServiceMain fails at once with an exit code, a service that exits its
 * process without reporting SERVICE_STOPPED, and a demand-start service
 * that is started again after it stopped.
 *
 * The services are this program itself (see svc.h): they log what they see
 * to HKLM\SOFTWARE\SetuOracle\advapi32_SvcStart, which the test prints.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winsvc/nf-winsvc-startservicew
 *       https://learn.microsoft.com/en-us/windows/win32/api/winsvc/nc-winsvc-lpservice_main_functionw
 *       https://learn.microsoft.com/en-us/windows/win32/services/service-servicemain-function
 */
#include "svc.h"

static SC_HANDLE scm;

static void start_case(const char *name, SC_HANDLE s, DWORD argc, const wchar_t **argv)
{
    BOOL r;
    SERVICE_STATUS_PROCESS st;
    ORACLE_CALL(r, StartServiceW(s, argc, argv));
    oracle_case(name);
    o_bool("ret", r);
    o_lasterr();
    if (r) {
        st = svc_wait_state(s, SERVICE_RUNNING);
        o_str("state", svc_state_name(st.dwCurrentState));
        o_hex32("accepted", st.dwControlsAccepted);
        o_bool("has_pid", st.dwProcessId != 0);
        svc_stop_wait(s);
        o_svclog("log");
    }
    oracle_case_end();
}

int main(int argc, char **argv)
{
    int rc = svc_child_main("advapi32/SvcStart", argc, argv);
    if (rc >= 0) return rc;
    oracle_begin("advapi32/SvcStart");
    scm = svc_scm();
    for (int i = 1; i <= 8; i++) svc_purge(scm, svc_name(i));
    svc_log_clear();
    BOOL r;

    const wchar_t *n1 = svc_name(1);
    SC_HANDLE s = svc_create(scm, n1, L"");
    oracle_case("setup");
    o_handle("created", s);
    oracle_case_end();

    start_case("no_args", s, 0, NULL);
    {
        const wchar_t *a[] = { L"one", L"two words", L"", L"\"quoted\"", L"tab\there" };
        start_case("five_args", s, 5, a);
        start_case("argc_zero_with_vector", s, 0, a);
        const wchar_t *one[] = { L"single" };
        start_case("one_arg", s, 1, one);
    }
    {
        /* ANSI arguments. */
        const char *a[] = { "ansi1", "ansi two" };
        ORACLE_CALL(r, StartServiceA(s, 2, a));
        oracle_case("ansi_args");
        o_bool("ret", r);
        o_lasterr();
        if (r) {
            svc_wait_state(s, SERVICE_RUNNING);
            svc_stop_wait(s);
            o_svclog("log");
        }
        oracle_case_end();
    }

    /* State observed immediately after StartService returns, while the
     * service is held in START_PENDING. */
    {
        const wchar_t *n = svc_name(2);
        SC_HANDLE p = svc_create(scm, n, L"pending");
        HANDLE ack = svc_event(n, L"ack"), go = svc_event(n, L"go");
        ORACLE_CALL(r, StartServiceW(p, 0, NULL));
        WaitForSingleObject(ack, 20000);
        SERVICE_STATUS_PROCESS st;
        svc_query(p, &st);
        oracle_case("pending_after_start");
        o_bool("ret", r);
        o_lasterr();
        o_svcstatus_ex("st", &st);
        oracle_case_end();
        BOOL r2;
        ORACLE_CALL(r2, StartServiceW(p, 0, NULL));
        oracle_case("start_while_start_pending");
        o_bool("ret", r2);
        o_lasterr();
        oracle_case_end();
        SERVICE_STATUS ss;
        ORACLE_CALL(r2, ControlService(p, SERVICE_CONTROL_STOP, &ss));
        oracle_case("stop_while_start_pending");
        o_bool("ret", r2);
        o_lasterr();
        oracle_case_end();
        SetEvent(go);
        st = svc_wait_state(p, SERVICE_RUNNING);
        oracle_case("pending_released");
        o_svcstatus_ex("st", &st);
        oracle_case_end();
        ORACLE_CALL(r2, StartServiceW(p, 0, NULL));
        oracle_case("start_while_running");
        o_bool("ret", r2);
        o_lasterr();
        oracle_case_end();
        svc_stop_wait(p);
        oracle_case("pending_log");
        o_svclog("log");
        oracle_case_end();
        CloseHandle(ack);
        CloseHandle(go);
        CloseServiceHandle(p);
        svc_purge(scm, n);
    }

    /* ServiceMain reports SERVICE_STOPPED with an error at once. */
    {
        const wchar_t *n = svc_name(3);
        SC_HANDLE f = svc_create(scm, n, L"failstart=1066 specific=42");
        ORACLE_CALL(r, StartServiceW(f, 0, NULL));
        SERVICE_STATUS_PROCESS st = svc_wait_state(f, SERVICE_STOPPED);
        svc_wait_log(L"dispatcher=");
        oracle_case("failstart_specific");
        o_bool("ret", r);
        o_lasterr();
        o_svcstatus_ex("st", &st);
        o_svclog("log");
        oracle_case_end();
        CloseServiceHandle(f);
        svc_purge(scm, n);
        n = svc_name(4);
        f = svc_create(scm, n, L"failstart=5");
        ORACLE_CALL(r, StartServiceW(f, 0, NULL));
        st = svc_wait_state(f, SERVICE_STOPPED);
        svc_wait_log(L"dispatcher=");
        oracle_case("failstart_win32");
        o_bool("ret", r);
        o_lasterr();
        o_svcstatus_ex("st", &st);
        o_svclog("log");
        oracle_case_end();
        CloseServiceHandle(f);
        svc_purge(scm, n);
    }

    /* The process exits while RUNNING without reporting SERVICE_STOPPED. */
    {
        const wchar_t *n = svc_name(5);
        SC_HANDLE x = svc_create(scm, n, L"exitprocess=7");
        /* The event is created before the service starts: one the service
         * (LocalSystem) creates first can't be signalled by the test. */
        HANDLE go = svc_event(n, L"go");
        SERVICE_STATUS_PROCESS st = svc_start_wait(x, 0, NULL);
        DWORD pid = st.dwProcessId;
        SetEvent(go);
        svc_wait_process_exit(pid);
        /* The SCM notices the exit some time later: wait up to 20 s for the
         * state to settle on STOPPED. */
        for (int i = 0; i < 1000; i++) {
            if (!svc_query(x, &st) || st.dwCurrentState == SERVICE_STOPPED) break;
            Sleep(20);
        }
        oracle_case("process_exit_without_stopped");
        o_svcstatus_ex("st", &st);
        o_svclog("log");
        oracle_case_end();
        CloseHandle(go);
        /* It can be started again. */
        go = svc_event(n, L"go");
        ORACLE_CALL(r, StartServiceW(x, 0, NULL));
        st = svc_wait_state(x, SERVICE_RUNNING);
        oracle_case("restart_after_process_exit");
        o_bool("ret", r);
        o_lasterr();
        o_str("state", svc_state_name(st.dwCurrentState));
        oracle_case_end();
        SetEvent(go);
        svc_wait_process_exit(st.dwProcessId);
        svc_wait_state(x, SERVICE_STOPPED);
        CloseHandle(go);
        svc_log_clear();
        CloseServiceHandle(x);
        svc_purge(scm, n);
    }

    /* Errors before any process is started. */
    {
        const wchar_t *n = svc_name(6);
        svc_purge(scm, n);
        SC_HANDLE d = CreateServiceW(scm, n, n, SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS, SERVICE_DISABLED,
                                     SERVICE_ERROR_IGNORE, svc_cmdline(n, L""), NULL, NULL, NULL, NULL, NULL);
        ORACLE_CALL(r, StartServiceW(d, 0, NULL));
        oracle_case("disabled");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        CloseServiceHandle(d);
        svc_purge(scm, n);

        n = svc_name(7);
        d = CreateServiceW(scm, n, n, SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS, SERVICE_DEMAND_START,
                           SERVICE_ERROR_IGNORE, L"C:\\setu-oracle\\missing\\none.exe", NULL, NULL, NULL, NULL, NULL);
        ORACLE_CALL(r, StartServiceW(d, 0, NULL));
        SERVICE_STATUS_PROCESS st;
        svc_query(d, &st);
        oracle_case("missing_binary");
        o_bool("ret", r);
        o_lasterr();
        o_svcstatus_ex("st", &st);
        oracle_case_end();
        ChangeServiceConfigW(d, SERVICE_NO_CHANGE, SERVICE_NO_CHANGE, SERVICE_NO_CHANGE, L"", NULL, NULL, NULL, NULL,
                             NULL, NULL);
        ORACLE_CALL(r, StartServiceW(d, 0, NULL));
        oracle_case("empty_binary_path");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        ChangeServiceConfigW(d, SERVICE_NO_CHANGE, SERVICE_NO_CHANGE, SERVICE_NO_CHANGE,
                             L"C:\\setu-oracle\\missing dir\\none.exe -x", NULL, NULL, NULL, NULL, NULL, NULL);
        ORACLE_CALL(r, StartServiceW(d, 0, NULL));
        oracle_case("missing_binary_unquoted_space");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        DeleteService(d);
        ORACLE_CALL(r, StartServiceW(d, 0, NULL));
        oracle_case("marked_for_delete");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        CloseServiceHandle(d);
        svc_purge(scm, n);

        ORACLE_CALL(r, StartServiceW(NULL, 0, NULL));
        oracle_case("null_handle");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        SC_HANDLE q = OpenServiceW(scm, n1, SERVICE_QUERY_STATUS);
        ORACLE_CALL(r, StartServiceW(q, 0, NULL));
        oracle_case("no_start_right");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        CloseServiceHandle(q);
    }

    /* A driver whose file doesn't exist. */
    {
        const wchar_t *n = svc_name(8);
        SC_HANDLE d = CreateServiceW(scm, n, n, SERVICE_ALL_ACCESS, SERVICE_KERNEL_DRIVER, SERVICE_DEMAND_START,
                                     SERVICE_ERROR_IGNORE, L"C:\\setu-oracle\\missing\\none.sys", NULL, NULL, NULL,
                                     NULL, NULL);
        ORACLE_CALL(r, StartServiceW(d, 0, NULL));
        oracle_case("driver_missing_file");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        CloseServiceHandle(d);
        svc_purge(scm, n);
    }

    CloseServiceHandle(s);
    for (int i = 1; i <= 8; i++) svc_purge(scm, svc_name(i));
    CloseServiceHandle(scm);
    svc_log_remove();
    return oracle_end();
}
