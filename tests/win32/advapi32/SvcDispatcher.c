/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: the service side. StartServiceCtrlDispatcherW called by a
 * process the SCM didn't start (ERROR_FAILED_SERVICE_CONTROLLER_CONNECT),
 * with an empty table, and RegisterServiceCtrlHandlerExW outside a service;
 * an own-process service whose dispatcher table uses a different name (the
 * docs say the name is ignored for SERVICE_WIN32_OWN_PROCESS) and what
 * argv[0] then holds; SERVICE_WIN32_SHARE_PROCESS with two services in one
 * process (both run in the same process, the second start doesn't create a
 * new one, stopping one leaves the other running, the process ends after
 * the last one stops); a shared-process service whose name is missing from
 * the table; and a service whose ServiceMain returns without reporting
 * any status.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winsvc/nf-winsvc-startservicectrldispatcherw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winsvc/ns-winsvc-service_table_entryw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winsvc/nf-winsvc-registerservicectrlhandlerexw
 *       https://learn.microsoft.com/en-us/windows/win32/services/service-entry-point
 */
#include "svc.h"

static SC_HANDLE scm;

static void WINAPI dummy_main(DWORD argc, LPWSTR *argv)
{
    (void)argc;
    (void)argv;
}

static DWORD WINAPI dummy_handler(DWORD c, DWORD t, LPVOID d, LPVOID x)
{
    (void)c, (void)t, (void)d, (void)x;
    return NO_ERROR;
}

/* Prints the log with its lines sorted and clears it, for lines written by
 * two services whose order is not fixed. */
static void o_svclog_sorted(const char *k)
{
    static wchar_t buf[16384], out[16384];
    wchar_t *lines[256];
    int n = 0;
    HANDLE m = svc__mutex();
    if (m) WaitForSingleObject(m, 10000);
    HKEY key;
    buf[0] = 0;
    if (!RegOpenKeyExW(HKEY_LOCAL_MACHINE, svc_logkey, 0, KEY_ALL_ACCESS, &key)) {
        DWORD size = sizeof(buf) - sizeof(wchar_t), type;
        if (RegQueryValueExW(key, L"log", NULL, &type, (BYTE *)buf, &size)) size = 0;
        buf[size / sizeof(wchar_t)] = 0;
        RegDeleteValueW(key, L"log");
        RegCloseKey(key);
    }
    if (m) ReleaseMutex(m);
    for (wchar_t *p = buf; *p && n < 256;) {
        lines[n++] = p;
        wchar_t *e = wcschr(p, L'|');
        if (!e) break;
        *e = 0;
        p = e + 1;
    }
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0 && wcscmp(lines[j - 1], lines[j]) > 0; j--) {
            wchar_t *t = lines[j];
            lines[j] = lines[j - 1];
            lines[j - 1] = t;
        }
    out[0] = 0;
    for (int i = 0; i < n; i++) {
        if (i) wcscat(out, L"|");
        wcscat(out, lines[i]);
    }
    o_wstr(k, out);
}

static SC_HANDLE create_share(const wchar_t *name, const wchar_t *opts)
{
    svc_purge(scm, name);
    return CreateServiceW(scm, name, name, SERVICE_ALL_ACCESS, SERVICE_WIN32_SHARE_PROCESS, SERVICE_DEMAND_START,
                          SERVICE_ERROR_IGNORE, svc_cmdline(name, opts), NULL, NULL, NULL, NULL, NULL);
}

int main(int argc, char **argv)
{
    int rc = svc_child_main("advapi32/SvcDispatcher", argc, argv);
    if (rc >= 0) return rc;
    oracle_begin("advapi32/SvcDispatcher");
    scm = svc_scm();
    for (int i = 1; i <= 6; i++) svc_purge(scm, svc_name(i));
    svc_log_clear();
    BOOL r;

    /* Not started by the SCM. */
    {
        SERVICE_TABLE_ENTRYW table[] = { { (wchar_t *)L"SetuOracleNotAService", dummy_main }, { NULL, NULL } };
        ORACLE_CALL(r, StartServiceCtrlDispatcherW(table));
        oracle_case("dispatcher_not_a_service");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        SERVICE_TABLE_ENTRYW empty[] = { { NULL, NULL } };
        ORACLE_CALL(r, StartServiceCtrlDispatcherW(empty));
        oracle_case("dispatcher_empty_table");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        SERVICE_STATUS_HANDLE h;
        ORACLE_CALL(h, RegisterServiceCtrlHandlerExW(L"SetuOracleNotAService", dummy_handler, NULL));
        oracle_case("register_not_a_service");
        o_bool("ret", h != NULL);
        o_lasterr();
        oracle_case_end();
    }

    /* Own process, table name differs from the service name. */
    {
        const wchar_t *n = svc_name(1);
        SC_HANDLE s = svc_create(scm, n, L"tablename=SomethingElse");
        SERVICE_STATUS_PROCESS st = svc_start_wait(s, 2, (const wchar_t *[]){ L"a1", L"a2" });
        oracle_case("own_table_name_ignored");
        o_str("state", svc_state_name(st.dwCurrentState));
        svc_stop_wait(s);
        o_svclog("log");
        oracle_case_end();
        CloseServiceHandle(s);
        svc_purge(scm, n);
    }

    /* Two services sharing one process. */
    {
        const wchar_t *a = svc_name(2), *b = svc_name(3);
        wchar_t opts[256];
        swprintf(opts, 256, L"share=%ls,%ls accept=0x1", a, b);
        SC_HANDLE sa = create_share(a, opts);
        SC_HANDLE sb = create_share(b, opts);
        SERVICE_STATUS_PROCESS sta = svc_start_wait(sa, 0, NULL);
        oracle_case("share_first_started");
        o_svcstatus_ex("a", &sta);
        o_svclog("log");
        oracle_case_end();
        SERVICE_STATUS_PROCESS stb = svc_start_wait(sb, 1, (const wchar_t *[]){ L"barg" });
        svc_query(sa, &sta);
        oracle_case("share_second_started");
        o_svcstatus_ex("b", &stb);
        o_bool("same_process", sta.dwProcessId == stb.dwProcessId);
        o_svclog("log");
        oracle_case_end();
        DWORD pid = sta.dwProcessId;
        /* Opened while both run, so it stays valid after the process ends. */
        HANDLE proc = OpenProcess(SYNCHRONIZE, FALSE, pid);
        SERVICE_STATUS ss;
        ControlService(sa, SERVICE_CONTROL_STOP, &ss);
        svc_wait_state(sa, SERVICE_STOPPED);
        svc_query(sa, &sta);
        svc_query(sb, &stb);
        oracle_case("share_first_stopped");
        o_bool("opened_process", proc != NULL);
        o_str("a.state", svc_state_name(sta.dwCurrentState));
        o_str("b.state", svc_state_name(stb.dwCurrentState));
        o_bool("process_alive", proc && WaitForSingleObject(proc, 0) == WAIT_TIMEOUT);
        o_svclog("log");
        oracle_case_end();
        /* Restart the first one: it joins the running process. */
        sta = svc_start_wait(sa, 0, NULL);
        oracle_case("share_first_restarted");
        o_str("a.state", svc_state_name(sta.dwCurrentState));
        o_bool("same_process", sta.dwProcessId == pid);
        o_svclog("log");
        oracle_case_end();
        ControlService(sa, SERVICE_CONTROL_STOP, &ss);
        svc_wait_state(sa, SERVICE_STOPPED);
        ControlService(sb, SERVICE_CONTROL_STOP, &ss);
        svc_wait_state(sb, SERVICE_STOPPED);
        if (proc) WaitForSingleObject(proc, 20000);
        oracle_case("share_both_stopped");
        o_bool("process_exited", proc && WaitForSingleObject(proc, 0) == WAIT_OBJECT_0);
        o_svclog_sorted("log");
        oracle_case_end();
        if (proc) CloseHandle(proc);
        CloseServiceHandle(sa);
        CloseServiceHandle(sb);
        svc_purge(scm, a);
        svc_purge(scm, b);
    }

    /* ServiceMain returns without reporting a status. */
    {
        const wchar_t *n = svc_name(5);
        SC_HANDLE s = svc_create(scm, n, L"noreport");
        ORACLE_CALL(r, StartServiceW(s, 0, NULL));
        svc_wait_log(L"main");
        SERVICE_STATUS_PROCESS st;
        svc_query(s, &st);
        oracle_case("noreport");
        o_bool("ret", r);
        o_lasterr();
        o_str("state", svc_state_name(st.dwCurrentState));
        oracle_case_end();
        CloseServiceHandle(s);
        svc_purge(scm, n);
        svc_log_clear();
    }

    /* Last (may take long if it never connects): a shared-process service whose name isn't in the table. */
    {
        const wchar_t *n = svc_name(4);
        SC_HANDLE s = create_share(n, L"share=SetuOracleOther1,SetuOracleOther2");
        SERVICE_STATUS_PROCESS st;
        ORACLE_CALL(r, StartServiceW(s, 0, NULL));
        DWORD err = oracle_last_error;
        st = svc_wait_state(s, SERVICE_STOPPED);
        oracle_case("share_name_not_in_table");
        o_bool("ret", r);
        o_u32("err", r ? 0 : err);
        o_str("state", svc_state_name(st.dwCurrentState));
        o_u32("win32_exit", st.dwWin32ExitCode);
        oracle_case_end();
        CloseServiceHandle(s);
        svc_purge(scm, n);
        svc_log_clear();
    }

    for (int i = 1; i <= 6; i++) svc_purge(scm, svc_name(i));
    {
        /* Last: a made-up status handle. */
        SERVICE_STATUS ss;
        memset(&ss, 0, sizeof ss);
        ss.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
        ss.dwCurrentState = SERVICE_RUNNING;
        ORACLE_CALL(r, SetServiceStatus((SERVICE_STATUS_HANDLE)(ULONG_PTR)0x1234, &ss));
        oracle_case("set_status_bogus_handle");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
    }
    CloseServiceHandle(scm);
    svc_log_remove();
    return oracle_end();
}
