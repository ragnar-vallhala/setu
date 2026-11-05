/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: QueryServiceConfigW / QueryServiceConfigA buffer handling
 * (NULL buffer with size 0, too-small sizes, exact size, the bytes-needed
 * value for W and A with and without optional strings) and
 * ChangeServiceConfigW / ChangeServiceConfigA: changing each field alone,
 * SERVICE_NO_CHANGE, invalid types / start types / error controls, empty
 * strings versus NULL, display names that clash with other services,
 * dependencies (including on itself), passwords, and changes on a service
 * marked for delete.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winsvc/nf-winsvc-queryserviceconfigw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winsvc/nf-winsvc-changeserviceconfigw
 */
#include "svc.h"

static SC_HANDLE scm;

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

static void size_cases(const char *prefix, SC_HANDLE s)
{
    static BYTE buf[8192];
    char n[64];
    BOOL r;
    DWORD need = 0xdead;
    ORACLE_CALL(r, QueryServiceConfigW(s, NULL, 0, &need));
    snprintf(n, sizeof n, "%s_w_null", prefix);
    oracle_case(n);
    o_bool("ret", r);
    o_lasterr();
    o_u32("need", need);
    oracle_case_end();
    DWORD full = need;

    need = 0xdead;
    ORACLE_CALL(r, QueryServiceConfigW(s, (QUERY_SERVICE_CONFIGW *)buf, sizeof(QUERY_SERVICE_CONFIGW), &need));
    snprintf(n, sizeof n, "%s_w_struct_only", prefix);
    oracle_case(n);
    o_bool("ret", r);
    o_lasterr();
    o_u32("need", need);
    oracle_case_end();

    need = 0xdead;
    ORACLE_CALL(r, QueryServiceConfigW(s, (QUERY_SERVICE_CONFIGW *)buf, full - 1, &need));
    snprintf(n, sizeof n, "%s_w_one_short", prefix);
    oracle_case(n);
    o_bool("ret", r);
    o_lasterr();
    o_u32("need", need);
    oracle_case_end();

    need = 0xdead;
    ORACLE_CALL(r, QueryServiceConfigW(s, (QUERY_SERVICE_CONFIGW *)buf, full, &need));
    snprintf(n, sizeof n, "%s_w_exact", prefix);
    oracle_case(n);
    o_bool("ret", r);
    o_lasterr();
    o_bool("need_unchanged", need == 0xdead);
    oracle_case_end();

    need = 0xdead;
    ORACLE_CALL(r, QueryServiceConfigA(s, NULL, 0, &need));
    snprintf(n, sizeof n, "%s_a_null", prefix);
    oracle_case(n);
    o_bool("ret", r);
    o_lasterr();
    o_u32("need", need);
    oracle_case_end();
    full = need;

    need = 0xdead;
    ORACLE_CALL(r, QueryServiceConfigA(s, (QUERY_SERVICE_CONFIGA *)buf, full, &need));
    snprintf(n, sizeof n, "%s_a_exact", prefix);
    oracle_case(n);
    o_bool("ret", r);
    o_lasterr();
    if (r) {
        QUERY_SERVICE_CONFIGA *c = (QUERY_SERVICE_CONFIGA *)buf;
        o_str("path", c->lpBinaryPathName);
        o_str("group", c->lpLoadOrderGroup);
        o_str("account", c->lpServiceStartName);
        o_str("display", c->lpDisplayName);
        o_bool("deps_empty", c->lpDependencies && !c->lpDependencies[0]);
    }
    oracle_case_end();
}

static void change_case(const char *name, SC_HANDLE s, DWORD type, DWORD start, DWORD err, const wchar_t *path,
                        const wchar_t *group, const wchar_t *deps, const wchar_t *account, const wchar_t *password,
                        const wchar_t *display)
{
    BOOL r;
    ORACLE_CALL(r, ChangeServiceConfigW(s, type, start, err, path, group, NULL, deps, account, password, display));
    oracle_case(name);
    o_bool("ret", r);
    o_lasterr();
    o_config(s);
    oracle_case_end();
}

#define NC SERVICE_NO_CHANGE

int main(int argc, char **argv)
{
    int rc = svc_child_main("advapi32/SvcConfig", argc, argv);
    if (rc >= 0) return rc;
    oracle_begin("advapi32/SvcConfig");
    scm = svc_scm();
    const wchar_t *a = svc_name(1), *b = svc_name(2);
    svc_purge(scm, a);
    svc_purge(scm, b);

    SC_HANDLE s = CreateServiceW(scm, a, L"SetuOracle SvcConfig A", SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS,
                                 SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL, L"C:\\setu-oracle\\a.exe", NULL, NULL,
                                 NULL, NULL, NULL);
    SC_HANDLE s2 = CreateServiceW(scm, b, L"SetuOracle SvcConfig B", SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS,
                                  SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL, L"C:\\setu-oracle\\bb.exe",
                                  L"SetuOracleGroup", NULL, L"SetuOracleDep1\0SetuOracleDep2\0", NULL, NULL);
    oracle_case("setup");
    o_handle("a", s);
    o_handle("b", s2);
    oracle_case_end();

    size_cases("size_plain", s);
    size_cases("size_group_deps", s2);

    change_case("no_change", s, NC, NC, NC, NULL, NULL, NULL, NULL, NULL, NULL);
    change_case("start_auto", s, NC, SERVICE_AUTO_START, NC, NULL, NULL, NULL, NULL, NULL, NULL);
    change_case("start_disabled", s, NC, SERVICE_DISABLED, NC, NULL, NULL, NULL, NULL, NULL, NULL);
    change_case("start_boot_own", s, NC, SERVICE_BOOT_START, NC, NULL, NULL, NULL, NULL, NULL, NULL);
    change_case("start_7", s, NC, 7, NC, NULL, NULL, NULL, NULL, NULL, NULL);
    change_case("start_demand", s, NC, SERVICE_DEMAND_START, NC, NULL, NULL, NULL, NULL, NULL, NULL);
    change_case("error_severe", s, NC, NC, SERVICE_ERROR_SEVERE, NULL, NULL, NULL, NULL, NULL, NULL);
    change_case("error_9", s, NC, NC, 9, NULL, NULL, NULL, NULL, NULL, NULL);
    change_case("type_share", s, SERVICE_WIN32_SHARE_PROCESS, NC, NC, NULL, NULL, NULL, NULL, NULL, NULL);
    change_case("type_interactive", s, SERVICE_WIN32_OWN_PROCESS | SERVICE_INTERACTIVE_PROCESS, NC, NC, NULL, NULL,
                NULL, NULL, NULL, NULL);
    change_case("type_kernel_driver", s, SERVICE_KERNEL_DRIVER, NC, NC, NULL, NULL, NULL, NULL, NULL, NULL);
    change_case("type_bogus", s, 0x1234, NC, NC, NULL, NULL, NULL, NULL, NULL, NULL);
    change_case("type_own", s, SERVICE_WIN32_OWN_PROCESS, NC, NC, NULL, NULL, NULL, NULL, NULL, NULL);
    change_case("path_new", s, NC, NC, NC, L"C:\\setu-oracle\\new path.exe /x", NULL, NULL, NULL, NULL, NULL);
    change_case("path_empty", s, NC, NC, NC, L"", NULL, NULL, NULL, NULL, NULL);
    change_case("group_set", s, NC, NC, NC, NULL, L"SetuOracleGroup", NULL, NULL, NULL, NULL);
    change_case("group_empty", s, NC, NC, NC, NULL, L"", NULL, NULL, NULL, NULL);
    change_case("deps_set", s, NC, NC, NC, NULL, NULL, L"SetuOracleDep1\0+SetuOracleGroup\0", NULL, NULL, NULL);
    change_case("deps_self", s, NC, NC, NC, NULL, NULL, L"SetuOracle_SvcConfig_1\0", NULL, NULL, NULL);
    change_case("deps_other", s, NC, NC, NC, NULL, NULL, L"SetuOracle_SvcConfig_2\0", NULL, NULL, NULL);
    change_case("deps_clear", s, NC, NC, NC, NULL, NULL, L"\0", NULL, NULL, NULL);
    change_case("display_new", s, NC, NC, NC, NULL, NULL, NULL, NULL, NULL, L"SetuOracle SvcConfig A2");
    change_case("display_clash_display", s, NC, NC, NC, NULL, NULL, NULL, NULL, NULL, L"SetuOracle SvcConfig B");
    change_case("display_clash_key", s, NC, NC, NC, NULL, NULL, NULL, NULL, NULL, L"SetuOracle_SvcConfig_2");
    change_case("display_own_key", s, NC, NC, NC, NULL, NULL, NULL, NULL, NULL, L"SetuOracle_SvcConfig_1");
    change_case("display_empty", s, NC, NC, NC, NULL, NULL, NULL, NULL, NULL, L"");
    change_case("account_localservice", s, NC, NC, NC, NULL, NULL, NULL, L"NT AUTHORITY\\LocalService", L"", NULL);
    change_case("account_localsystem", s, NC, NC, NC, NULL, NULL, NULL, L"LocalSystem", L"", NULL);
    change_case("password_only", s, NC, NC, NC, NULL, NULL, NULL, NULL, L"secret", NULL);
    change_case("account_empty", s, NC, NC, NC, NULL, NULL, NULL, L"", NULL, NULL);
    {
        BOOL r;
        ORACLE_CALL(r, ChangeServiceConfigA(s, NC, SERVICE_AUTO_START, NC, "C:\\setu-oracle\\ansi.exe", "", NULL,
                                            NULL, NULL, NULL, "SetuOracle SvcConfig ANSI"));
        oracle_case("ansi_change");
        o_bool("ret", r);
        o_lasterr();
        o_config(s);
        oracle_case_end();
        DWORD tag = 0xabcd;
        ORACLE_CALL(r, ChangeServiceConfigW(s, NC, NC, NC, NULL, L"SetuOracleGroup", &tag, NULL, NULL, NULL, NULL));
        oracle_case("tag_own_process");
        o_bool("ret", r);
        o_lasterr();
        o_bool("tag_untouched", tag == 0xabcd);
        oracle_case_end();
        ORACLE_CALL(r, ChangeServiceConfigW(NULL, NC, NC, NC, NULL, NULL, NULL, NULL, NULL, NULL, NULL));
        oracle_case("null_handle");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, ChangeServiceConfigW(scm, NC, NC, NC, NULL, NULL, NULL, NULL, NULL, NULL, NULL));
        oracle_case("scm_handle");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
    }

    /* Marked for delete. */
    {
        BOOL r;
        DeleteService(s2);
        ORACLE_CALL(r, ChangeServiceConfigW(s2, NC, SERVICE_AUTO_START, NC, NULL, NULL, NULL, NULL, NULL, NULL, NULL));
        oracle_case("change_marked_for_delete");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        static BYTE buf[8192];
        DWORD need;
        ORACLE_CALL(r, QueryServiceConfigW(s2, (QUERY_SERVICE_CONFIGW *)buf, sizeof buf, &need));
        oracle_case("query_marked_for_delete");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
    }

    CloseServiceHandle(s);
    CloseServiceHandle(s2);
    svc_purge(scm, a);
    svc_purge(scm, b);
    CloseServiceHandle(scm);
    svc_log_remove();
    return oracle_end();
}
