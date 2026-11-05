/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: EnumServicesStatusExW / EnumServicesStatusW restricted to
 * services this test creates (a private load-order group, so Windows' own
 * service list is never printed): the order of results, state and type
 * filters, the group filter (including "" and a missing group), buffer
 * sizes (NULL / too small / exact, bytes needed, resume handles), invalid
 * levels, types and states, and the A function.
 *
 * Unfiltered enumerations of the whole database are made only to check
 * that our services appear in them; nothing else from them is printed.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winsvc/nf-winsvc-enumservicesstatusexw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winsvc/nf-winsvc-enumservicesstatusw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winsvc/ns-winsvc-enum_service_status_processw
 */
#include "svc.h"

static SC_HANDLE scm;
#define GROUP L"SetuOracleEnumGroup"
static BYTE buf[1 << 20];
/* Windows 10 fails with RPC_X_BAD_STUB_DATA when the buffer is larger than
 * the data (even 2 KiB for 465 bytes), so "big" enumerations ask for the size
 * first and pass exactly that. */
#define EXACT 0xffffffff

static BOOL enum_w(DWORD type, DWORD state, DWORD size, DWORD *need, DWORD *count, DWORD *resume, const wchar_t *group)
{
    if (size == EXACT) {
        DWORD saved = resume ? *resume : 0;
        if (EnumServicesStatusExW(scm, SC_ENUM_PROCESS_INFO, type, state, NULL, 0, need, count, resume, group))
            return TRUE;
        if (GetLastError() != ERROR_MORE_DATA || *need > sizeof buf) return FALSE;
        if (resume) *resume = saved;
        size = *need;
    }
    return EnumServicesStatusExW(scm, SC_ENUM_PROCESS_INFO, type, state, size ? buf : NULL, size, need, count, resume,
                                 group);
}

static void list(const ENUM_SERVICE_STATUS_PROCESSW *e, DWORD count)
{
    char out[4096] = "";
    for (DWORD i = 0; i < count; i++) {
        char item[400];
        snprintf(item, sizeof item, "%s%s/%s/%s/%lx", i ? ";" : "", oracle_utf8(e[i].lpServiceName),
                 oracle_utf8(e[i].lpDisplayName), svc_state_name(e[i].ServiceStatusProcess.dwCurrentState),
                 (unsigned long)e[i].ServiceStatusProcess.dwServiceType);
        strncat(out, item, sizeof out - strlen(out) - 1);
    }
    o_str("services", out);
}

static void enum_case(const char *name, DWORD type, DWORD state, const wchar_t *group, DWORD size, int use_resume)
{
    DWORD need = 0xdead, count = 0xdead, resume = 0;
    BOOL r;
    ORACLE_CALL(r, enum_w(type, state, size, &need, &count, use_resume ? &resume : NULL, group));
    oracle_case(name);
    o_bool("ret", r);
    o_lasterr();
    o_u32("need", need);
    o_u32("count", count);
    if (use_resume) o_bool("resume_nonzero", resume != 0);
    if (count != 0xdead && count) list((ENUM_SERVICE_STATUS_PROCESSW *)buf, count);
    oracle_case_end();
}

/* Is `name` in a full enumeration? */
static int in_full(DWORD type, DWORD state, const wchar_t *name)
{
    DWORD need, count, resume = 0;
    for (;;) {
        BOOL r = enum_w(type, state, EXACT, &need, &count, &resume, NULL);
        if (!r && GetLastError() != ERROR_MORE_DATA) return -1;
        ENUM_SERVICE_STATUS_PROCESSW *e = (ENUM_SERVICE_STATUS_PROCESSW *)buf;
        for (DWORD i = 0; i < count; i++)
            if (!_wcsicmp(e[i].lpServiceName, name)) return 1;
        if (r) return 0;
    }
}

int main(int argc, char **argv)
{
    int rc = svc_child_main("advapi32/SvcEnum", argc, argv);
    if (rc >= 0) return rc;
    oracle_begin("advapi32/SvcEnum");
    scm = svc_scm();
    for (int i = 1; i <= 5; i++) svc_purge(scm, svc_name(i));

    /* Created out of alphabetical order (3, 1, 4 by display name "Z", "A", "M"). */
    SC_HANDLE s3 = CreateServiceW(scm, svc_name(3), L"SetuOracle Enum Z", SERVICE_ALL_ACCESS,
                                  SERVICE_WIN32_OWN_PROCESS, SERVICE_DEMAND_START, SERVICE_ERROR_IGNORE,
                                  svc_cmdline(svc_name(3), L""), GROUP, NULL, NULL, NULL, NULL);
    SC_HANDLE s1 = CreateServiceW(scm, svc_name(1), L"SetuOracle Enum A", SERVICE_ALL_ACCESS,
                                  SERVICE_WIN32_OWN_PROCESS, SERVICE_DEMAND_START, SERVICE_ERROR_IGNORE,
                                  L"C:\\setu-oracle\\none.exe", GROUP, NULL, NULL, NULL, NULL);
    SC_HANDLE s4 = CreateServiceW(scm, svc_name(4), L"SetuOracle Enum M", SERVICE_ALL_ACCESS, SERVICE_KERNEL_DRIVER,
                                  SERVICE_DEMAND_START, SERVICE_ERROR_IGNORE, L"C:\\setu-oracle\\none.sys", GROUP, NULL,
                                  NULL, NULL, NULL);
    SC_HANDLE s2 = CreateServiceW(scm, svc_name(2), L"SetuOracle Enum B", SERVICE_ALL_ACCESS,
                                  SERVICE_WIN32_SHARE_PROCESS, SERVICE_DISABLED, SERVICE_ERROR_IGNORE,
                                  L"C:\\setu-oracle\\none.exe", L"setuoracleenumgroup", NULL, NULL, NULL, NULL);
    SC_HANDLE s5 = CreateServiceW(scm, svc_name(5), L"SetuOracle Enum other", SERVICE_ALL_ACCESS,
                                  SERVICE_WIN32_OWN_PROCESS, SERVICE_DEMAND_START, SERVICE_ERROR_IGNORE,
                                  L"C:\\setu-oracle\\none.exe", NULL, NULL, NULL, NULL, NULL);
    oracle_case("setup");
    o_bool("all_created", s1 && s2 && s3 && s4 && s5);
    oracle_case_end();

    enum_case("group_all", SERVICE_WIN32 | SERVICE_DRIVER, SERVICE_STATE_ALL, GROUP, EXACT, 1);
    enum_case("group_win32", SERVICE_WIN32, SERVICE_STATE_ALL, GROUP, EXACT, 1);
    enum_case("group_own", SERVICE_WIN32_OWN_PROCESS, SERVICE_STATE_ALL, GROUP, EXACT, 1);
    enum_case("group_driver", SERVICE_DRIVER, SERVICE_STATE_ALL, GROUP, EXACT, 1);
    enum_case("group_kernel_driver", SERVICE_KERNEL_DRIVER, SERVICE_STATE_ALL, GROUP, EXACT, 1);
    enum_case("group_active", SERVICE_WIN32, SERVICE_ACTIVE, GROUP, EXACT, 1);
    enum_case("group_inactive", SERVICE_WIN32, SERVICE_INACTIVE, GROUP, EXACT, 1);
    enum_case("group_case", SERVICE_WIN32, SERVICE_STATE_ALL, L"SETUORACLEENUMGROUP", EXACT, 1);
    enum_case("group_missing", SERVICE_WIN32, SERVICE_STATE_ALL, L"SetuOracleNoSuchGroup", EXACT, 1);
    enum_case("group_null_buffer", SERVICE_WIN32, SERVICE_STATE_ALL, GROUP, 0, 1);
    enum_case("group_tiny_buffer", SERVICE_WIN32, SERVICE_STATE_ALL, GROUP, 8, 1);
    enum_case("group_no_resume", SERVICE_WIN32, SERVICE_STATE_ALL, GROUP, EXACT, 0);
    enum_case("group_no_resume_null_buffer", SERVICE_WIN32, SERVICE_STATE_ALL, GROUP, 0, 0);
    enum_case("bad_type", 0x1234, SERVICE_STATE_ALL, GROUP, EXACT, 1);
    enum_case("type_zero", 0, SERVICE_STATE_ALL, GROUP, EXACT, 1);
    enum_case("type_interactive", SERVICE_WIN32 | SERVICE_INTERACTIVE_PROCESS, SERVICE_STATE_ALL, GROUP, EXACT, 1);
    enum_case("bad_state", SERVICE_WIN32, 0x10, GROUP, EXACT, 1);
    enum_case("state_zero", SERVICE_WIN32, 0, GROUP, EXACT, 1);

    /* Exactly sized and one byte short (with a resume handle: paging). */
    {
        DWORD need = 0, count = 0, resume = 0;
        BOOL r;
        EnumServicesStatusExW(scm, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL, NULL, 0, &need, &count,
                              &resume, GROUP);
        DWORD full = need;
        resume = 0;
        ORACLE_CALL(r, EnumServicesStatusExW(scm, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL, buf, full,
                                             &need, &count, &resume, GROUP));
        oracle_case("group_exact_buffer");
        o_bool("ret", r);
        o_lasterr();
        o_u32("count", count);
        o_bool("resume_zero", resume == 0);
        oracle_case_end();
        resume = 0;
        ORACLE_CALL(r, EnumServicesStatusExW(scm, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL, buf,
                                             full - 1, &need, &count, &resume, GROUP));
        oracle_case("group_page1");
        o_bool("ret", r);
        o_lasterr();
        o_u32("count", count);
        o_u32("need", need);
        if (count) list((ENUM_SERVICE_STATUS_PROCESSW *)buf, count);
        oracle_case_end();
        ORACLE_CALL(r, enum_w(SERVICE_WIN32, SERVICE_STATE_ALL, EXACT, &need, &count, &resume, GROUP));
        oracle_case("group_page2");
        o_bool("ret", r);
        o_lasterr();
        o_u32("count", count);
        if (count) list((ENUM_SERVICE_STATUS_PROCESSW *)buf, count);
        o_bool("resume_zero", resume == 0);
        oracle_case_end();
    }

    /* A running service. */
    {
        SERVICE_STATUS_PROCESS st = svc_start_wait(s3, 0, NULL);
        enum_case("running_active", SERVICE_WIN32, SERVICE_ACTIVE, GROUP, EXACT, 1);
        enum_case("running_inactive", SERVICE_WIN32, SERVICE_INACTIVE, GROUP, EXACT, 1);
        {
            DWORD need, count, resume = 0;
            enum_w(SERVICE_WIN32, SERVICE_ACTIVE, EXACT, &need, &count, &resume, GROUP);
            ENUM_SERVICE_STATUS_PROCESSW *e = (ENUM_SERVICE_STATUS_PROCESSW *)buf;
            oracle_case("running_fields");
            o_str("state_seen_by_start", svc_state_name(st.dwCurrentState));
            if (count == 1) o_svcstatus_ex("st", &e[0].ServiceStatusProcess);
            oracle_case_end();
        }
        svc_stop_wait(s3);
    }

    /* The whole database: only "is ours there". */
    oracle_case("full_contains");
    o_i64("own_all", in_full(SERVICE_WIN32, SERVICE_STATE_ALL, svc_name(5)));
    o_i64("own_active", in_full(SERVICE_WIN32, SERVICE_ACTIVE, svc_name(5)));
    o_i64("driver_in_win32", in_full(SERVICE_WIN32, SERVICE_STATE_ALL, svc_name(4)));
    o_i64("driver_in_driver", in_full(SERVICE_DRIVER, SERVICE_STATE_ALL, svc_name(4)));
    oracle_case_end();
    {
        /* Group "" means services that belong to no group. */
        DWORD need, count, resume = 0;
        BOOL r;
        int ours5 = 0, ours1 = 0;
        do {
            r = enum_w(SERVICE_WIN32, SERVICE_STATE_ALL, EXACT, &need, &count, &resume, L"");
            if (!r && GetLastError() != ERROR_MORE_DATA) break;
            ENUM_SERVICE_STATUS_PROCESSW *e = (ENUM_SERVICE_STATUS_PROCESSW *)buf;
            for (DWORD i = 0; i < count; i++) {
                if (!_wcsicmp(e[i].lpServiceName, svc_name(5))) ours5 = 1;
                if (!_wcsicmp(e[i].lpServiceName, svc_name(1))) ours1 = 1;
            }
        } while (!r);
        oracle_case("group_empty_string");
        o_bool("ret", r);
        o_bool("ungrouped_listed", ours5);
        o_bool("grouped_listed", ours1);
        oracle_case_end();
    }

    /* Bad parameters. */
    {
        DWORD need, count, resume = 0;
        BOOL r;
        ORACLE_CALL(r, EnumServicesStatusExW(scm, 7, SERVICE_WIN32, SERVICE_STATE_ALL, buf, sizeof buf, &need, &count,
                                             &resume, GROUP));
        oracle_case("bad_level");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, EnumServicesStatusExW(NULL, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL, buf,
                                             sizeof buf, &need, &count, &resume, GROUP));
        oracle_case("null_scm");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        SC_HANDLE m = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
        ORACLE_CALL(r, EnumServicesStatusExW(m, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL, NULL, 0,
                                             &need, &count, &resume, GROUP));
        oracle_case("no_enumerate_right");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        CloseServiceHandle(m);
    }

    /* The A function, filtered by group. */
    {
        DWORD need = 0, count = 0, resume = 0;
        BOOL r;
        ORACLE_CALL(r, EnumServicesStatusExA(scm, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL, NULL, 0,
                                             &need, &count, &resume, "SetuOracleEnumGroup"));
        oracle_case("ansi_null_buffer");
        o_bool("ret", r);
        o_lasterr();
        o_u32("need", need);
        oracle_case_end();
        resume = 0;
        ORACLE_CALL(r, EnumServicesStatusExA(scm, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL, buf,
                                             need, &need, &count, &resume, "SetuOracleEnumGroup"));
        oracle_case("ansi_group");
        o_bool("ret", r);
        o_lasterr();
        o_u32("count", count);
        if (r) {
            ENUM_SERVICE_STATUS_PROCESSA *e = (ENUM_SERVICE_STATUS_PROCESSA *)buf;
            char out[1024] = "";
            for (DWORD i = 0; i < count; i++) {
                strncat(out, e[i].lpServiceName, sizeof out - strlen(out) - 2);
                strcat(out, ";");
            }
            o_str("services", out);
        }
        oracle_case_end();
    }

    SC_HANDLE all[] = { s1, s2, s3, s4, s5 };
    for (int i = 0; i < 5; i++)
        if (all[i]) CloseServiceHandle(all[i]);
    for (int i = 1; i <= 5; i++) svc_purge(scm, svc_name(i));
    CloseServiceHandle(scm);
    svc_log_remove();
    return oracle_end();
}
