/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: DeleteService: deleting a stopped service with no other
 * handles, deleting twice through the same handle
 * (ERROR_SERVICE_MARKED_FOR_DELETE), what other open handles can still do
 * while the service is marked for delete (query status / config, start,
 * open a new handle by name, change config), when the name disappears, and
 * deleting a running service (it keeps running until stopped, then goes
 * away). Also the service's registry key under
 * HKLM\SYSTEM\CurrentControlSet\Services while marked for delete (only
 * whether it exists, and the documented DeleteFlag value).
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winsvc/nf-winsvc-deleteservice
 *       https://learn.microsoft.com/en-us/windows/win32/api/winsvc/nf-winsvc-closeservicehandle
 */
#include "svc.h"

static SC_HANDLE scm;

static void o_regkey(const wchar_t *svc)
{
    wchar_t path[256];
    HKEY k;
    swprintf(path, 256, L"SYSTEM\\CurrentControlSet\\Services\\%ls", svc);
    LONG st = RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &k);
    o_bool("regkey", st == 0);
    if (!st) {
        DWORD v = 0, size = sizeof v, type;
        LONG q = RegQueryValueExW(k, L"DeleteFlag", NULL, &type, (BYTE *)&v, &size);
        o_bool("deleteflag.present", q == 0);
        if (!q) o_u32("deleteflag", v);
        RegCloseKey(k);
    }
}

static SC_HANDLE create(const wchar_t *name)
{
    return CreateServiceW(scm, name, name, SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS, SERVICE_DEMAND_START,
                          SERVICE_ERROR_IGNORE, L"C:\\setu-oracle\\none.exe", NULL, NULL, NULL, NULL, NULL);
}

int main(int argc, char **argv)
{
    int rc = svc_child_main("advapi32/SvcDelete", argc, argv);
    if (rc >= 0) return rc;
    oracle_begin("advapi32/SvcDelete");
    scm = svc_scm();
    for (int i = 1; i <= 4; i++) svc_purge(scm, svc_name(i));
    BOOL r;

    /* Simple delete. */
    {
        const wchar_t *n = svc_name(1);
        SC_HANDLE s = create(n);
        oracle_case("simple_before");
        o_handle("created", s);
        o_regkey(n);
        oracle_case_end();
        ORACLE_CALL(r, DeleteService(s));
        oracle_case("simple_delete");
        o_bool("ret", r);
        o_lasterr();
        o_regkey(n);
        oracle_case_end();
        ORACLE_CALL(r, DeleteService(s));
        oracle_case("simple_delete_again");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        SC_HANDLE o;
        ORACLE_CALL(o, OpenServiceW(scm, n, SERVICE_QUERY_STATUS));
        oracle_case("simple_open_while_marked");
        o_handle("ret", o);
        o_lasterr();
        oracle_case_end();
        if (o) CloseServiceHandle(o);
        ORACLE_CALL(r, CloseServiceHandle(s));
        oracle_case("simple_close");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        /* The SCM removes the service once the last handle is closed. */
        for (int i = 0; i < 250; i++) {
            o = OpenServiceW(scm, n, SERVICE_QUERY_STATUS);
            if (!o) break;
            CloseServiceHandle(o);
            Sleep(20);
        }
        ORACLE_CALL(o, OpenServiceW(scm, n, SERVICE_QUERY_STATUS));
        oracle_case("simple_gone");
        o_handle("ret", o);
        o_lasterr();
        o_regkey(n);
        oracle_case_end();
        if (o) CloseServiceHandle(o);
    }

    /* Two handles: one deletes, what can the other do? */
    {
        const wchar_t *n = svc_name(2);
        SC_HANDLE s = create(n);
        SC_HANDLE other = OpenServiceW(scm, n, SERVICE_ALL_ACCESS);
        DeleteService(s);
        CloseServiceHandle(s);
        SERVICE_STATUS ss;
        static BYTE buf[8192];
        DWORD need;
        ORACLE_CALL(r, QueryServiceStatus(other, &ss));
        oracle_case("other_query_status");
        o_bool("ret", r);
        o_lasterr();
        if (r) o_svcstatus("st", &ss);
        oracle_case_end();
        ORACLE_CALL(r, QueryServiceConfigW(other, (QUERY_SERVICE_CONFIGW *)buf, sizeof buf, &need));
        oracle_case("other_query_config");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, QueryServiceConfig2W(other, SERVICE_CONFIG_DESCRIPTION, buf, sizeof buf, &need));
        oracle_case("other_query_config2");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        SERVICE_DESCRIPTIONW d = { (wchar_t *)L"x" };
        ORACLE_CALL(r, ChangeServiceConfig2W(other, SERVICE_CONFIG_DESCRIPTION, &d));
        oracle_case("other_change_config2");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, StartServiceW(other, 0, NULL));
        oracle_case("other_start");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, ControlService(other, SERVICE_CONTROL_INTERROGATE, &ss));
        oracle_case("other_interrogate");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, DeleteService(other));
        oracle_case("other_delete");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        {
            wchar_t out[256];
            DWORD len = 256;
            ORACLE_CALL(r, GetServiceDisplayNameW(scm, n, out, &len));
            oracle_case("other_display_name");
            o_bool("ret", r);
            o_lasterr();
            oracle_case_end();
        }
        {
            ENUM_SERVICE_STATUS_PROCESSW *e = (ENUM_SERVICE_STATUS_PROCESSW *)buf;
            DWORD count = 0, resume = 0;
            ChangeServiceConfigW(other, SERVICE_NO_CHANGE, SERVICE_NO_CHANGE, SERVICE_NO_CHANGE, NULL,
                                 L"SetuOracleDelGroup", NULL, NULL, NULL, NULL, NULL);
            ORACLE_CALL(r, EnumServicesStatusExW(scm, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL, buf,
                                                 sizeof buf, &need, &count, &resume, L"SetuOracleDelGroup"));
            oracle_case("other_enum_group");
            o_bool("ret", r);
            o_lasterr();
            o_u32("count", count);
            if (r && count) o_wstr("first", e[0].lpServiceName);
            oracle_case_end();
        }
        CloseServiceHandle(other);
        svc_purge(scm, n);
    }

    /* Deleting a running service. */
    {
        const wchar_t *n = svc_name(3);
        SC_HANDLE s = svc_create(scm, n, L"");
        SERVICE_STATUS_PROCESS st = svc_start_wait(s, 0, NULL);
        oracle_case("running_started");
        o_str("state", svc_state_name(st.dwCurrentState));
        oracle_case_end();
        DWORD pid = st.dwProcessId;
        ORACLE_CALL(r, DeleteService(s));
        oracle_case("running_delete");
        o_bool("ret", r);
        o_lasterr();
        svc_query(s, &st);
        o_str("state", svc_state_name(st.dwCurrentState));
        o_regkey(n);
        oracle_case_end();
        CloseServiceHandle(s);
        SC_HANDLE o;
        ORACLE_CALL(o, OpenServiceW(scm, n, SERVICE_ALL_ACCESS));
        oracle_case("running_open_after_close");
        o_handle("ret", o);
        o_lasterr();
        oracle_case_end();
        if (o) {
            SERVICE_STATUS ss;
            ORACLE_CALL(r, ControlService(o, SERVICE_CONTROL_STOP, &ss));
            oracle_case("running_stop_marked");
            o_bool("ret", r);
            o_lasterr();
            oracle_case_end();
            svc_wait_state(o, SERVICE_STOPPED);
            svc_wait_process_exit(pid);
            CloseServiceHandle(o);
        } else {
            /* No handle: stop it through the process (cleanup only). */
            HANDLE p = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
            if (p) TerminateProcess(p, 1), WaitForSingleObject(p, 10000), CloseHandle(p);
        }
        for (int i = 0; i < 250; i++) {
            o = OpenServiceW(scm, n, SERVICE_QUERY_STATUS);
            if (!o) break;
            CloseServiceHandle(o);
            Sleep(20);
        }
        ORACLE_CALL(o, OpenServiceW(scm, n, SERVICE_QUERY_STATUS));
        oracle_case("running_gone_after_stop");
        o_handle("ret", o);
        o_lasterr();
        oracle_case_end();
        if (o) CloseServiceHandle(o);
        oracle_case("running_log");
        o_svclog("log");
        oracle_case_end();
    }

    /* Access: a handle without DELETE. */
    {
        const wchar_t *n = svc_name(4);
        SC_HANDLE s = create(n);
        SC_HANDLE q = OpenServiceW(scm, n, SERVICE_ALL_ACCESS & ~DELETE);
        ORACLE_CALL(r, DeleteService(q));
        oracle_case("no_delete_right");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        CloseServiceHandle(q);
        ORACLE_CALL(r, DeleteService(NULL));
        oracle_case("null_handle");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        CloseServiceHandle(s);
        svc_purge(scm, n);
    }

    for (int i = 1; i <= 4; i++) svc_purge(scm, svc_name(i));
    CloseServiceHandle(scm);
    svc_log_remove();
    return oracle_end();
}
