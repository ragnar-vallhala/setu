/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: OpenServiceW / OpenServiceA: missing services, NULL and
 * empty names, NULL and bogus SCM handles, case-insensitive names, display
 * names, and for a handle opened with each single service access right,
 * which operations succeed and which fail with ERROR_ACCESS_DENIED
 * (QueryServiceStatus, QueryServiceConfig, QueryServiceConfig2,
 * ChangeServiceConfig, StartService, ControlService(STOP / INTERROGATE / a
 * user-defined code), DeleteService, EnumDependentServices,
 * QueryServiceObjectSecurity).
 *
 * The service is never started successfully: its binary path doesn't exist,
 * so only access checks (which come first) are observed for StartService.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winsvc/nf-winsvc-openservicew
 *       https://learn.microsoft.com/en-us/windows/win32/services/service-security-and-access-rights
 */
#include "svc.h"

static SC_HANDLE scm;

static void open_case(const char *name, SC_HANDLE m, const wchar_t *svc, DWORD access)
{
    SC_HANDLE h;
    ORACLE_CALL(h, OpenServiceW(m, svc, access));
    oracle_case(name);
    o_handle("ret", h);
    o_lasterr();
    oracle_case_end();
    if (h) CloseServiceHandle(h);
}

static void rights_case(const char *name, const wchar_t *svc, DWORD access)
{
    SC_HANDLE h;
    BOOL r;
    SERVICE_STATUS ss;
    static BYTE buf[8192];
    DWORD need;
    h = OpenServiceW(scm, svc, access);
    oracle_case(name);
    o_handle("open", h);
    if (!h) {
        o_u32("open_err", GetLastError());
        oracle_case_end();
        return;
    }
#define STEP(key, expr)                       \
    do {                                      \
        ORACLE_CALL(r, expr);                 \
        o_u32(key, r ? 0 : oracle_last_error); \
    } while (0)
    STEP("query_status", QueryServiceStatus(h, &ss));
    STEP("query_config", QueryServiceConfigW(h, (QUERY_SERVICE_CONFIGW *)buf, sizeof buf, &need));
    STEP("query_config2", QueryServiceConfig2W(h, SERVICE_CONFIG_DESCRIPTION, buf, sizeof buf, &need));
    STEP("change_config", ChangeServiceConfigW(h, SERVICE_NO_CHANGE, SERVICE_NO_CHANGE, SERVICE_NO_CHANGE, NULL, NULL,
                                               NULL, NULL, NULL, NULL, NULL));
    STEP("start", StartServiceW(h, 0, NULL));
    STEP("control_stop", ControlService(h, SERVICE_CONTROL_STOP, &ss));
    STEP("control_interrogate", ControlService(h, SERVICE_CONTROL_INTERROGATE, &ss));
    STEP("control_user", ControlService(h, 130, &ss));
    {
        ENUM_SERVICE_STATUSW deps[2];
        DWORD count = 0;
        STEP("enum_dependents", EnumDependentServicesW(h, SERVICE_STATE_ALL, deps, sizeof deps, &need, &count));
    }
    STEP("query_security", QueryServiceObjectSecurity(h, DACL_SECURITY_INFORMATION, buf, sizeof buf, &need));
    STEP("delete", DeleteService(h));
#undef STEP
    oracle_case_end();
    CloseServiceHandle(h);
}

int main(int argc, char **argv)
{
    int rc = svc_child_main("advapi32/SvcOpen", argc, argv);
    if (rc >= 0) return rc;
    oracle_begin("advapi32/SvcOpen");
    scm = svc_scm();
    const wchar_t *svc = svc_name(1);
    svc_purge(scm, svc);
    SC_HANDLE s = CreateServiceW(scm, svc, L"SetuOracle SvcOpen display", SERVICE_ALL_ACCESS,
                                 SERVICE_WIN32_OWN_PROCESS, SERVICE_DEMAND_START, SERVICE_ERROR_IGNORE,
                                 L"C:\\setu-oracle\\missing\\none.exe", NULL, NULL, NULL, NULL, NULL);
    oracle_case("setup");
    o_handle("created", s);
    oracle_case_end();

    open_case("missing", scm, L"SetuOracleNoSuchService", SERVICE_QUERY_STATUS);
    open_case("null_name", scm, NULL, SERVICE_QUERY_STATUS);
    open_case("empty_name", scm, L"", SERVICE_QUERY_STATUS);
    open_case("null_scm", NULL, svc, SERVICE_QUERY_STATUS);
    open_case("exact", scm, svc, SERVICE_QUERY_STATUS);
    open_case("lower", scm, L"setuoracle_svcopen_1", SERVICE_QUERY_STATUS);
    open_case("display_name", scm, L"SetuOracle SvcOpen display", SERVICE_QUERY_STATUS);
    open_case("trailing_space", scm, L"SetuOracle_SvcOpen_1 ", SERVICE_QUERY_STATUS);
    open_case("access_zero", scm, svc, 0);
    open_case("access_all", scm, svc, SERVICE_ALL_ACCESS);
    open_case("access_generic_all", scm, svc, GENERIC_ALL);
    open_case("access_maximum", scm, svc, MAXIMUM_ALLOWED);
    open_case("access_undefined_bit", scm, svc, 0x00004000);
    open_case("access_system_security", scm, svc, ACCESS_SYSTEM_SECURITY);
    {
        SC_HANDLE h;
        ORACLE_CALL(h, OpenServiceA(scm, "setuoracle_SVCOPEN_1", SERVICE_QUERY_STATUS));
        oracle_case("ansi_lower");
        o_handle("ret", h);
        o_lasterr();
        oracle_case_end();
        if (h) CloseServiceHandle(h);
    }
    {
        /* SCM handle with only SC_MANAGER_CONNECT can open services. */
        SC_HANDLE m = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
        open_case("connect_only_scm", m, svc, SERVICE_QUERY_STATUS);
        CloseServiceHandle(m);
        m = OpenSCManagerW(NULL, NULL, 0);
        open_case("zero_access_scm", m, svc, SERVICE_QUERY_STATUS);
        CloseServiceHandle(m);
    }

    rights_case("rights_query_status", svc, SERVICE_QUERY_STATUS);
    rights_case("rights_query_config", svc, SERVICE_QUERY_CONFIG);
    rights_case("rights_change_config", svc, SERVICE_CHANGE_CONFIG);
    rights_case("rights_enumerate_dependents", svc, SERVICE_ENUMERATE_DEPENDENTS);
    rights_case("rights_interrogate", svc, SERVICE_INTERROGATE);
    rights_case("rights_user_defined", svc, SERVICE_USER_DEFINED_CONTROL);
    rights_case("rights_stop", svc, SERVICE_STOP);
    rights_case("rights_pause_continue", svc, SERVICE_PAUSE_CONTINUE);
    rights_case("rights_read_control", svc, READ_CONTROL);
    rights_case("rights_generic_read", svc, GENERIC_READ);
    rights_case("rights_generic_execute", svc, GENERIC_EXECUTE);
    rights_case("rights_generic_write", svc, GENERIC_WRITE);
    /* Last: SERVICE_START reaches the start attempt (missing binary), and
     * DELETE deletes the service. */
    rights_case("rights_start", svc, SERVICE_START);
    rights_case("rights_delete", svc, DELETE);

    if (s) CloseServiceHandle(s);
    svc_purge(scm, svc);
    /* Last: a made-up handle value. */
    open_case("bogus_scm", (SC_HANDLE)(ULONG_PTR)0xdead0, svc, SERVICE_QUERY_STATUS);

    CloseServiceHandle(scm);
    svc_log_remove();
    return oracle_end();
}
