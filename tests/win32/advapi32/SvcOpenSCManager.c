/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: OpenSCManagerW / OpenSCManagerA: database names, machine
 * names, access rights (each SC_MANAGER_* right, generic rights, zero, bits
 * outside the defined set), and which operations a handle opened with a
 * given right allows (CreateService needs SC_MANAGER_CREATE_SERVICE,
 * EnumServicesStatusEx needs SC_MANAGER_ENUMERATE_SERVICE; LockServiceDatabase
 * is in SvcLock), CloseServiceHandle on bad handles, and using a
 * service handle where an SCM handle is expected.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winsvc/nf-winsvc-openscmanagerw
 *       https://learn.microsoft.com/en-us/windows/win32/services/service-security-and-access-rights
 *       https://learn.microsoft.com/en-us/windows/win32/api/winsvc/nf-winsvc-closeservicehandle
 */
#include "svc.h"

static void open_case(const char *name, const wchar_t *machine, const wchar_t *db, DWORD access)
{
    SC_HANDLE h;
    ORACLE_CALL(h, OpenSCManagerW(machine, db, access));
    oracle_case(name);
    o_handle("ret", h);
    o_lasterr();
    oracle_case_end();
    if (h) CloseServiceHandle(h);
}

/* What a handle opened with `access` allows. */
static void rights_case(const char *name, DWORD access)
{
    SC_HANDLE m, s;
    BOOL r;
    ENUM_SERVICE_STATUS_PROCESSW buf[4];
    DWORD need, count, resume;
    const wchar_t *svc = svc_name(9);
    m = OpenSCManagerW(NULL, NULL, access);
    oracle_case(name);
    o_handle("open", m);
    if (m) {
        ORACLE_CALL(s, CreateServiceW(m, svc, svc, SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS,
                                      SERVICE_DEMAND_START, SERVICE_ERROR_IGNORE, L"C:\\setu-oracle\\none.exe",
                                      NULL, NULL, NULL, NULL, NULL));
        o_handle("create", s);
        o_lasterr();
        if (s) {
            DeleteService(s);
            CloseServiceHandle(s);
        }
        need = count = resume = 0;
        ORACLE_CALL(r, EnumServicesStatusExW(m, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL, (BYTE *)buf,
                                             sizeof buf, &need, &count, &resume, L"SetuOracleNoSuchGroup"));
        o_bool("enum", r);
        o_lasterr();
        ORACLE_CALL(s, OpenServiceW(m, L"SetuOracleNoSuchService", SERVICE_QUERY_STATUS));
        o_handle("open_service", s);
        o_lasterr();
        if (s) CloseServiceHandle(s);
        CloseServiceHandle(m);
    }
    oracle_case_end();
}

int main(int argc, char **argv)
{
    int rc = svc_child_main("advapi32/SvcOpenSCManager", argc, argv);
    if (rc >= 0) return rc;
    oracle_begin("advapi32/SvcOpenSCManager");
    SC_HANDLE scm = svc_scm();
    svc_purge(scm, svc_name(9));

    open_case("all_access", NULL, NULL, SC_MANAGER_ALL_ACCESS);
    open_case("connect", NULL, NULL, SC_MANAGER_CONNECT);
    open_case("zero_access", NULL, NULL, 0);
    open_case("generic_read", NULL, NULL, GENERIC_READ);
    open_case("generic_write", NULL, NULL, GENERIC_WRITE);
    open_case("generic_execute", NULL, NULL, GENERIC_EXECUTE);
    open_case("generic_all", NULL, NULL, GENERIC_ALL);
    open_case("maximum_allowed", NULL, NULL, MAXIMUM_ALLOWED);
    open_case("undefined_bits", NULL, NULL, 0x00008000);
    open_case("undefined_bit_0x100", NULL, NULL, 0x00000100);
    open_case("read_control", NULL, NULL, READ_CONTROL);
    open_case("write_dac", NULL, NULL, WRITE_DAC);
    open_case("db_active", NULL, SERVICES_ACTIVE_DATABASEW, SC_MANAGER_CONNECT);
    open_case("db_active_lower", NULL, L"servicesactive", SC_MANAGER_CONNECT);
    open_case("db_failed", NULL, SERVICES_FAILED_DATABASEW, SC_MANAGER_CONNECT);
    open_case("db_bogus", NULL, L"NoSuchDatabase", SC_MANAGER_CONNECT);
    open_case("db_empty", NULL, L"", SC_MANAGER_CONNECT);
    open_case("machine_empty", L"", NULL, SC_MANAGER_CONNECT);
    open_case("machine_localhost_dot", L".", NULL, SC_MANAGER_CONNECT);
    open_case("machine_backslash_dot", L"\\\\.", NULL, SC_MANAGER_CONNECT);

    {
        SC_HANDLE h;
        ORACLE_CALL(h, OpenSCManagerA(NULL, "ServicesActive", SC_MANAGER_CONNECT));
        oracle_case("ansi_db_active");
        o_handle("ret", h);
        o_lasterr();
        oracle_case_end();
        if (h) CloseServiceHandle(h);
        ORACLE_CALL(h, OpenSCManagerA(NULL, "Bogus", SC_MANAGER_CONNECT));
        oracle_case("ansi_db_bogus");
        o_handle("ret", h);
        o_lasterr();
        oracle_case_end();
        if (h) CloseServiceHandle(h);
    }

    rights_case("rights_connect_only", SC_MANAGER_CONNECT);
    rights_case("rights_create", SC_MANAGER_CONNECT | SC_MANAGER_CREATE_SERVICE);
    rights_case("rights_enumerate", SC_MANAGER_CONNECT | SC_MANAGER_ENUMERATE_SERVICE);
    rights_case("rights_lock", SC_MANAGER_CONNECT | SC_MANAGER_LOCK);
    rights_case("rights_zero", 0);
    rights_case("rights_generic_read", GENERIC_READ);
    rights_case("rights_generic_write", GENERIC_WRITE);
    rights_case("rights_generic_execute", GENERIC_EXECUTE);
    rights_case("rights_all", SC_MANAGER_ALL_ACCESS);

    {
        BOOL r;
        ORACLE_CALL(r, CloseServiceHandle(NULL));
        oracle_case("close_null");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();

        SC_HANDLE h = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
        CloseServiceHandle(h);
        ORACLE_CALL(r, CloseServiceHandle(h));
        oracle_case("close_twice");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
    }

    {
        /* A service handle passed where an SCM handle is expected. */
        const wchar_t *svc = svc_name(1);
        SC_HANDLE s = svc_create(scm, svc, L"");
        SC_HANDLE s2;
        ORACLE_CALL(s2, OpenServiceW(s, svc, SERVICE_QUERY_STATUS));
        oracle_case("service_handle_as_scm");
        o_handle("created", s);
        o_handle("ret", s2);
        o_lasterr();
        oracle_case_end();
        if (s2) CloseServiceHandle(s2);
        SC_HANDLE m2;
        SERVICE_STATUS ss;
        BOOL r;
        m2 = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
        ORACLE_CALL(r, QueryServiceStatus(m2, &ss));
        oracle_case("scm_handle_as_service");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, DeleteService(m2));
        oracle_case("delete_scm_handle");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        CloseServiceHandle(m2);
        if (s) CloseServiceHandle(s);
        svc_purge(scm, svc);
    }

    svc_purge(scm, svc_name(9));
    CloseServiceHandle(scm);
    svc_log_remove();
    return oracle_end();
}
