/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: CreateServiceW parameters: every service type (own/shared
 * process, kernel and file system drivers, interactive flag, invalid
 * combinations), start types (boot/system only for drivers, auto, demand,
 * disabled, out of range), error control values, NULL and empty binary
 * paths, load order groups and tags, dependencies, account names and
 * passwords, NULL SCM handle, and the configuration each accepted call
 * stores (read back with QueryServiceConfigW).
 *
 * Every service uses a fixed name, SetuOracle_SvcCreate_<n>, is never
 * started, and is deleted before the test ends.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winsvc/nf-winsvc-createservicew
 *       https://learn.microsoft.com/en-us/windows/win32/api/winsvc/nf-winsvc-queryserviceconfigw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winsvc/ns-winsvc-query_service_configw
 */
#include "svc.h"

static SC_HANDLE scm;
#define BIN L"C:\\setu-oracle\\none.exe"

static void o_config(SC_HANDLE s)
{
    static BYTE buf[8192];
    QUERY_SERVICE_CONFIGW *c = (QUERY_SERVICE_CONFIGW *)buf;
    DWORD need = 0;
    BOOL r = QueryServiceConfigW(s, c, sizeof buf, &need);
    o_bool("cfg.ok", r);
    if (!r) return;
    o_hex32("cfg.type", c->dwServiceType);
    o_u32("cfg.start", c->dwStartType);
    o_u32("cfg.error", c->dwErrorControl);
    o_wstr("cfg.path", c->lpBinaryPathName);
    o_wstr("cfg.group", c->lpLoadOrderGroup);
    o_u32("cfg.tag", c->dwTagId);
    o_multisz("cfg.deps", c->lpDependencies);
    o_wstr("cfg.account", c->lpServiceStartName);
    o_wstr("cfg.display", c->lpDisplayName);
}

static int n_next = 1;

/* Creates a service with the given parameters, prints the result and the
 * stored configuration, and deletes it. */
static void create_case(const char *name, DWORD type, DWORD start, DWORD err, const wchar_t *path,
                        const wchar_t *group, int want_tag, const wchar_t *deps, const wchar_t *account,
                        const wchar_t *password)
{
    const wchar_t *svc = svc_name(n_next++);
    DWORD tag = 0xabcdef;
    SC_HANDLE s;
    svc_purge(scm, svc);
    ORACLE_CALL(s, CreateServiceW(scm, svc, L"Setu oracle display", SERVICE_ALL_ACCESS, type, start, err, path,
                                  group, want_tag ? &tag : NULL, deps, account, password));
    oracle_case(name);
    o_handle("ret", s);
    o_lasterr();
    if (want_tag) o_bool("tag_changed", tag != 0xabcdef), o_bool("tag_nonzero", tag != 0 && tag != 0xabcdef);
    if (s) o_config(s);
    oracle_case_end();
    if (s) {
        DeleteService(s);
        CloseServiceHandle(s);
    }
    svc_purge(scm, svc);
}

#define OWN SERVICE_WIN32_OWN_PROCESS
#define DEMAND SERVICE_DEMAND_START
#define IGN SERVICE_ERROR_IGNORE

int main(int argc, char **argv)
{
    int rc = svc_child_main("advapi32/SvcCreate", argc, argv);
    if (rc >= 0) return rc;
    oracle_begin("advapi32/SvcCreate");
    scm = svc_scm();

    /* Service types. */
    create_case("type_own", OWN, DEMAND, IGN, BIN, NULL, 0, NULL, NULL, NULL);
    create_case("type_share", SERVICE_WIN32_SHARE_PROCESS, DEMAND, IGN, BIN, NULL, 0, NULL, NULL, NULL);
    create_case("type_own_interactive", OWN | SERVICE_INTERACTIVE_PROCESS, DEMAND, IGN, BIN, NULL, 0, NULL, NULL, NULL);
    create_case("type_share_interactive", SERVICE_WIN32_SHARE_PROCESS | SERVICE_INTERACTIVE_PROCESS, DEMAND, IGN, BIN,
                NULL, 0, NULL, NULL, NULL);
    create_case("type_kernel_driver", SERVICE_KERNEL_DRIVER, DEMAND, IGN, L"C:\\setu-oracle\\none.sys", NULL, 0,
                NULL, NULL, NULL);
    create_case("type_fs_driver", SERVICE_FILE_SYSTEM_DRIVER, DEMAND, IGN, L"C:\\setu-oracle\\none.sys", NULL, 0,
                NULL, NULL, NULL);
    create_case("type_win32_both", SERVICE_WIN32, DEMAND, IGN, BIN, NULL, 0, NULL, NULL, NULL);
    create_case("type_driver_both", SERVICE_DRIVER, DEMAND, IGN, BIN, NULL, 0, NULL, NULL, NULL);
    create_case("type_zero", 0, DEMAND, IGN, BIN, NULL, 0, NULL, NULL, NULL);
    create_case("type_interactive_only", SERVICE_INTERACTIVE_PROCESS, DEMAND, IGN, BIN, NULL, 0, NULL, NULL, NULL);
    create_case("type_driver_interactive", SERVICE_KERNEL_DRIVER | SERVICE_INTERACTIVE_PROCESS, DEMAND, IGN, BIN,
                NULL, 0, NULL, NULL, NULL);
    create_case("type_adapter", SERVICE_ADAPTER, DEMAND, IGN, BIN, NULL, 0, NULL, NULL, NULL);
    create_case("type_recognizer", SERVICE_RECOGNIZER_DRIVER, DEMAND, IGN, BIN, NULL, 0, NULL, NULL, NULL);
    create_case("type_bogus_bit", OWN | 0x1000, DEMAND, IGN, BIN, NULL, 0, NULL, NULL, NULL);
    create_case("type_user_own", 0x50 /* SERVICE_USER_OWN_PROCESS */, DEMAND, IGN, BIN, NULL, 0, NULL, NULL, NULL);
    create_case("type_own_interactive_account", OWN | SERVICE_INTERACTIVE_PROCESS, DEMAND, IGN, BIN, NULL, 0, NULL,
                L"NT AUTHORITY\\LocalService", L"");

    /* Start types. */
    create_case("start_boot_own", OWN, SERVICE_BOOT_START, IGN, BIN, NULL, 0, NULL, NULL, NULL);
    create_case("start_system_own", OWN, SERVICE_SYSTEM_START, IGN, BIN, NULL, 0, NULL, NULL, NULL);
    create_case("start_boot_driver", SERVICE_KERNEL_DRIVER, SERVICE_BOOT_START, IGN, L"C:\\setu-oracle\\none.sys",
                NULL, 0, NULL, NULL, NULL);
    create_case("start_system_driver", SERVICE_KERNEL_DRIVER, SERVICE_SYSTEM_START, IGN,
                L"C:\\setu-oracle\\none.sys", NULL, 0, NULL, NULL, NULL);
    create_case("start_auto", OWN, SERVICE_AUTO_START, IGN, BIN, NULL, 0, NULL, NULL, NULL);
    create_case("start_disabled", OWN, SERVICE_DISABLED, IGN, BIN, NULL, 0, NULL, NULL, NULL);
    create_case("start_5", OWN, 5, IGN, BIN, NULL, 0, NULL, NULL, NULL);
    create_case("start_huge", OWN, 0xffffffff, IGN, BIN, NULL, 0, NULL, NULL, NULL);

    /* Error control. */
    create_case("error_normal", OWN, DEMAND, SERVICE_ERROR_NORMAL, BIN, NULL, 0, NULL, NULL, NULL);
    create_case("error_severe", OWN, DEMAND, SERVICE_ERROR_SEVERE, BIN, NULL, 0, NULL, NULL, NULL);
    create_case("error_critical", OWN, DEMAND, SERVICE_ERROR_CRITICAL, BIN, NULL, 0, NULL, NULL, NULL);
    create_case("error_4", OWN, DEMAND, 4, BIN, NULL, 0, NULL, NULL, NULL);
    create_case("error_huge", OWN, DEMAND, 0x80000000, BIN, NULL, 0, NULL, NULL, NULL);

    /* Binary paths. */
    create_case("path_null", OWN, DEMAND, IGN, NULL, NULL, 0, NULL, NULL, NULL);
    create_case("path_empty", OWN, DEMAND, IGN, L"", NULL, 0, NULL, NULL, NULL);
    create_case("path_relative", OWN, DEMAND, IGN, L"relative.exe -x", NULL, 0, NULL, NULL, NULL);
    create_case("path_quoted_args", OWN, DEMAND, IGN, L"\"C:\\Program Files\\x y.exe\" -a \"b c\"", NULL, 0, NULL,
                NULL, NULL);
    create_case("path_envvar", OWN, DEMAND, IGN, L"%SystemRoot%\\none.exe", NULL, 0, NULL, NULL, NULL);
    create_case("path_driver_relative", SERVICE_KERNEL_DRIVER, DEMAND, IGN, L"system32\\drivers\\none.sys", NULL, 0,
                NULL, NULL, NULL);

    /* Groups, tags, dependencies. */
    create_case("group_set", OWN, DEMAND, IGN, BIN, L"SetuOracleGroup", 0, NULL, NULL, NULL);
    create_case("group_empty", OWN, DEMAND, IGN, BIN, L"", 0, NULL, NULL, NULL);
    create_case("tag_own_process", OWN, DEMAND, IGN, BIN, L"SetuOracleGroup", 1, NULL, NULL, NULL);
    create_case("tag_own_no_group", OWN, DEMAND, IGN, BIN, NULL, 1, NULL, NULL, NULL);
    create_case("tag_driver_demand", SERVICE_KERNEL_DRIVER, DEMAND, IGN, L"C:\\setu-oracle\\none.sys",
                L"SetuOracleGroup", 1, NULL, NULL, NULL);
    create_case("tag_driver_boot", SERVICE_KERNEL_DRIVER, SERVICE_BOOT_START, IGN, L"C:\\setu-oracle\\none.sys",
                L"SetuOracleGroup", 1, NULL, NULL, NULL);
    create_case("deps_one", OWN, DEMAND, IGN, BIN, NULL, 0, L"SetuOracleDepA\0", NULL, NULL);
    create_case("deps_two_and_group", OWN, DEMAND, IGN, BIN, NULL, 0, L"SetuOracleDepA\0+SetuOracleGroup\0", NULL,
                NULL);
    create_case("deps_empty", OWN, DEMAND, IGN, BIN, NULL, 0, L"\0", NULL, NULL);

    /* Accounts. */
    create_case("account_localsystem", OWN, DEMAND, IGN, BIN, NULL, 0, NULL, L"LocalSystem", NULL);
    create_case("account_dot_localsystem", OWN, DEMAND, IGN, BIN, NULL, 0, NULL, L".\\LocalSystem", NULL);
    create_case("account_localservice", OWN, DEMAND, IGN, BIN, NULL, 0, NULL, L"NT AUTHORITY\\LocalService", L"");
    create_case("account_networkservice", OWN, DEMAND, IGN, BIN, NULL, 0, NULL, L"NT AUTHORITY\\NetworkService",
                NULL);
    create_case("account_share_localservice", SERVICE_WIN32_SHARE_PROCESS, DEMAND, IGN, BIN, NULL, 0, NULL,
                L"NT AUTHORITY\\LocalService", NULL);
    create_case("account_driver_object", SERVICE_KERNEL_DRIVER, DEMAND, IGN, L"C:\\setu-oracle\\none.sys", NULL, 0,
                NULL, L"\\Driver\\SetuOracle", NULL);
    create_case("account_empty", OWN, DEMAND, IGN, BIN, NULL, 0, NULL, L"", NULL);

    /* Bad handles and access. */
    {
        SC_HANDLE s;
        ORACLE_CALL(s, CreateServiceW(NULL, svc_name(90), NULL, SERVICE_ALL_ACCESS, OWN, DEMAND, IGN, BIN, NULL, NULL,
                                      NULL, NULL, NULL));
        oracle_case("null_scm");
        o_handle("ret", s);
        o_lasterr();
        oracle_case_end();
        if (s) DeleteService(s), CloseServiceHandle(s);

        ORACLE_CALL(s, CreateServiceW(scm, NULL, NULL, SERVICE_ALL_ACCESS, OWN, DEMAND, IGN, BIN, NULL, NULL, NULL,
                                      NULL, NULL));
        oracle_case("null_name");
        o_handle("ret", s);
        o_lasterr();
        oracle_case_end();
        if (s) DeleteService(s), CloseServiceHandle(s);

        /* Display name NULL: the key name is used. */
        const wchar_t *svc = svc_name(91);
        svc_purge(scm, svc);
        ORACLE_CALL(s, CreateServiceW(scm, svc, NULL, SERVICE_ALL_ACCESS, OWN, DEMAND, IGN, BIN, NULL, NULL, NULL,
                                      NULL, NULL));
        oracle_case("display_null");
        o_handle("ret", s);
        o_lasterr();
        if (s) o_config(s);
        oracle_case_end();
        if (s) DeleteService(s), CloseServiceHandle(s);
        svc_purge(scm, svc);

        ORACLE_CALL(s, CreateServiceW(scm, svc, L"", SERVICE_ALL_ACCESS, OWN, DEMAND, IGN, BIN, NULL, NULL, NULL,
                                      NULL, NULL));
        oracle_case("display_empty");
        o_handle("ret", s);
        o_lasterr();
        if (s) o_config(s);
        oracle_case_end();
        if (s) DeleteService(s), CloseServiceHandle(s);
        svc_purge(scm, svc);

        /* Desired access on the returned handle. */
        ORACLE_CALL(s, CreateServiceW(scm, svc, NULL, SERVICE_QUERY_STATUS, OWN, DEMAND, IGN, BIN, NULL, NULL, NULL,
                                      NULL, NULL));
        oracle_case("access_query_status_only");
        o_handle("ret", s);
        o_lasterr();
        if (s) {
            BOOL r;
            SERVICE_STATUS ss;
            ORACLE_CALL(r, QueryServiceStatus(s, &ss));
            o_bool("query_status", r);
            ORACLE_CALL(r, DeleteService(s));
            o_bool("delete", r);
            o_u32("delete_err", oracle_last_error);
            CloseServiceHandle(s);
        }
        oracle_case_end();
        svc_purge(scm, svc);

        ORACLE_CALL(s, CreateServiceW(scm, svc, NULL, 0, OWN, DEMAND, IGN, BIN, NULL, NULL, NULL, NULL, NULL));
        oracle_case("access_zero");
        o_handle("ret", s);
        o_lasterr();
        if (s) CloseServiceHandle(s);
        oracle_case_end();
        svc_purge(scm, svc);

        /* The A function. */
        char svca[128];
        WideCharToMultiByte(CP_ACP, 0, svc, -1, svca, sizeof svca, NULL, NULL);
        ORACLE_CALL(s, CreateServiceA(scm, svca, "Setu ANSI display", SERVICE_ALL_ACCESS, OWN, DEMAND, IGN,
                                      "C:\\setu-oracle\\ansi.exe", "SetuOracleGroup", NULL, "SetuOracleDepA\0", NULL,
                                      NULL));
        oracle_case("ansi_create");
        o_handle("ret", s);
        o_lasterr();
        if (s) o_config(s);
        oracle_case_end();
        if (s) DeleteService(s), CloseServiceHandle(s);
        svc_purge(scm, svc);
    }

    CloseServiceHandle(scm);
    svc_log_remove();
    return oracle_end();
}
