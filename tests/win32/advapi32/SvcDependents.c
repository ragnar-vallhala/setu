/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: service dependencies. EnumDependentServicesW (direct and
 * indirect dependents, order, state filters, buffer sizes, A form), and
 * what dependencies do at run time: StartService on a service whose
 * dependency is stopped starts the dependency first; a dependency that is
 * disabled or missing makes the start fail (ERROR_SERVICE_DEPENDENCY_FAIL /
 * ERROR_SERVICE_DEPENDENCY_DELETED ...); stopping a service with running
 * dependents fails with ERROR_DEPENDENT_SERVICES_RUNNING; group
 * dependencies ("+group").
 *
 * The chain is: 3 depends on 2, 2 depends on 1, 4 depends on 1, 5 depends
 * on a missing service, 6 depends on the group of 1, 7 depends on the
 * disabled service 8.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winsvc/nf-winsvc-enumdependentservicesw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winsvc/nf-winsvc-createservicew
 *       https://learn.microsoft.com/en-us/windows/win32/services/service-startup
 */
#include "svc.h"

static SC_HANDLE scm;
static SC_HANDLE h[9];
static BYTE buf[65536];

static void list(const ENUM_SERVICE_STATUSW *e, DWORD count)
{
    char out[2048] = "";
    for (DWORD i = 0; i < count; i++) {
        char item[300];
        snprintf(item, sizeof item, "%s%s/%s", i ? ";" : "", oracle_utf8(e[i].lpServiceName),
                 svc_state_name(e[i].ServiceStatus.dwCurrentState));
        strncat(out, item, sizeof out - strlen(out) - 1);
    }
    o_str("dependents", out);
}

static void dep_case(const char *name, SC_HANDLE s, DWORD state, DWORD size)
{
    DWORD need = 0xdead, count = 0xdead;
    BOOL r;
    ORACLE_CALL(r, EnumDependentServicesW(s, state, size ? (ENUM_SERVICE_STATUSW *)buf : NULL, size, &need, &count));
    oracle_case(name);
    o_bool("ret", r);
    o_lasterr();
    o_u32("need", need);
    o_u32("count", count);
    if (r && count != 0xdead) list((ENUM_SERVICE_STATUSW *)buf, count);
    oracle_case_end();
}

static const char *st(int i)
{
    SERVICE_STATUS_PROCESS s;
    if (!svc_query(h[i], &s)) return "query failed";
    return svc_state_name(s.dwCurrentState);
}

static SC_HANDLE mk(int i, const wchar_t *group, const wchar_t *deps, DWORD start)
{
    return CreateServiceW(scm, svc_name(i), svc_name(i), SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS, start,
                          SERVICE_ERROR_IGNORE, svc_cmdline(svc_name(i), L""), group, NULL, deps, NULL, NULL);
}

int main(int argc, char **argv)
{
    int rc = svc_child_main("advapi32/SvcDependents", argc, argv);
    if (rc >= 0) return rc;
    oracle_begin("advapi32/SvcDependents");
    scm = svc_scm();
    for (int i = 1; i <= 8; i++) svc_purge(scm, svc_name(i));

    h[1] = mk(1, L"SetuOracleDepGroup", NULL, SERVICE_DEMAND_START);
    h[2] = mk(2, NULL, L"SetuOracle_SvcDependents_1\0", SERVICE_DEMAND_START);
    h[3] = mk(3, NULL, L"SetuOracle_SvcDependents_2\0", SERVICE_DEMAND_START);
    h[4] = mk(4, NULL, L"setuoracle_svcdependents_1\0", SERVICE_DEMAND_START);
    h[5] = mk(5, NULL, L"SetuOracle_SvcDependents_Missing\0", SERVICE_DEMAND_START);
    h[6] = mk(6, NULL, L"+SetuOracleDepGroup\0", SERVICE_DEMAND_START);
    h[8] = mk(8, NULL, NULL, SERVICE_DISABLED);
    h[7] = mk(7, NULL, L"SetuOracle_SvcDependents_8\0", SERVICE_DEMAND_START);
    oracle_case("setup");
    int ok = 1;
    for (int i = 1; i <= 8; i++) ok &= h[i] != NULL;
    o_bool("all_created", ok);
    oracle_case_end();

    dep_case("deps_of_1_all", h[1], SERVICE_STATE_ALL, sizeof buf);
    dep_case("deps_of_1_active", h[1], SERVICE_ACTIVE, sizeof buf);
    dep_case("deps_of_1_inactive", h[1], SERVICE_INACTIVE, sizeof buf);
    dep_case("deps_of_1_null_buffer", h[1], SERVICE_STATE_ALL, 0);
    dep_case("deps_of_1_small_buffer", h[1], SERVICE_STATE_ALL, 16);
    dep_case("deps_of_2", h[2], SERVICE_STATE_ALL, sizeof buf);
    dep_case("deps_of_3_none", h[3], SERVICE_STATE_ALL, sizeof buf);
    dep_case("deps_of_3_none_null_buffer", h[3], SERVICE_STATE_ALL, 0);
    dep_case("deps_of_8", h[8], SERVICE_STATE_ALL, sizeof buf);
    dep_case("bad_state", h[1], 0x10, sizeof buf);
    {
        SC_HANDLE q = OpenServiceW(scm, svc_name(1), SERVICE_QUERY_STATUS);
        dep_case("no_enumerate_right", q, SERVICE_STATE_ALL, sizeof buf);
        CloseServiceHandle(q);
        DWORD need = 0, count = 0;
        BOOL r;
        ORACLE_CALL(r, EnumDependentServicesA(h[1], SERVICE_STATE_ALL, NULL, 0, &need, &count));
        oracle_case("ansi_null_buffer");
        o_bool("ret", r);
        o_lasterr();
        o_u32("need", need);
        oracle_case_end();
        ORACLE_CALL(r, EnumDependentServicesA(h[1], SERVICE_STATE_ALL, (ENUM_SERVICE_STATUSA *)buf, sizeof buf, &need,
                                              &count));
        oracle_case("ansi");
        o_bool("ret", r);
        o_lasterr();
        o_u32("count", count);
        if (r) {
            ENUM_SERVICE_STATUSA *e = (ENUM_SERVICE_STATUSA *)buf;
            char out[1024] = "";
            for (DWORD i = 0; i < count; i++) {
                strncat(out, e[i].lpServiceName, sizeof out - strlen(out) - 2);
                strcat(out, ";");
            }
            o_str("dependents", out);
        }
        oracle_case_end();
    }

    /* Starting 3 starts 1 and 2 first. */
    {
        BOOL r;
        ORACLE_CALL(r, StartServiceW(h[3], 0, NULL));
        svc_wait_state(h[3], SERVICE_RUNNING);
        svc_wait_state(h[2], SERVICE_RUNNING);
        svc_wait_state(h[1], SERVICE_RUNNING);
        oracle_case("start_chain");
        o_bool("ret", r);
        o_lasterr();
        o_str("s1", st(1));
        o_str("s2", st(2));
        o_str("s3", st(3));
        o_str("s4", st(4));
        oracle_case_end();
        oracle_case("start_chain_log");
        o_svclog("log");
        oracle_case_end();
        dep_case("deps_of_1_active_running", h[1], SERVICE_ACTIVE, sizeof buf);
        dep_case("deps_of_1_inactive_running", h[1], SERVICE_INACTIVE, sizeof buf);

        SERVICE_STATUS ss;
        ORACLE_CALL(r, ControlService(h[1], SERVICE_CONTROL_STOP, &ss));
        oracle_case("stop_with_running_dependents");
        o_bool("ret", r);
        o_lasterr();
        o_str("s1", st(1));
        oracle_case_end();
        ORACLE_CALL(r, ControlService(h[2], SERVICE_CONTROL_STOP, &ss));
        oracle_case("stop_middle_with_running_dependent");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        /* Stopping 1 with 4 (stopped) as a dependent is fine once 2 and 3 stop. */
        svc_stop_wait(h[3]);
        svc_stop_wait(h[2]);
        ORACLE_CALL(r, ControlService(h[1], SERVICE_CONTROL_STOP, &ss));
        oracle_case("stop_after_dependents_stopped");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        svc_wait_state(h[1], SERVICE_STOPPED);
        Sleep(0);
        oracle_case("stop_after_dependents_stopped_states");
        o_str("s1", st(1));
        o_str("s2", st(2));
        o_str("s3", st(3));
        oracle_case_end();
        /* Let the processes exit before reading the log. */
        for (int i = 0; i < 250; i++) {
            DWORD need, count, resume = 0;
            EnumServicesStatusExW(scm, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_ACTIVE, buf, sizeof buf, &need,
                                  &count, &resume, L"SetuOracleDepGroup");
            if (!count) break;
            Sleep(20);
        }
        svc_wait_log(L"SetuOracle_SvcDependents_1:set_stopped");
        Sleep(200);
        svc_log_clear();
    }

    /* Broken dependencies. */
    {
        BOOL r;
        ORACLE_CALL(r, StartServiceW(h[5], 0, NULL));
        oracle_case("start_missing_dependency");
        o_bool("ret", r);
        o_lasterr();
        o_str("s5", st(5));
        oracle_case_end();
        ORACLE_CALL(r, StartServiceW(h[7], 0, NULL));
        oracle_case("start_disabled_dependency");
        o_bool("ret", r);
        o_lasterr();
        o_str("s7", st(7));
        o_str("s8", st(8));
        oracle_case_end();
        ORACLE_CALL(r, StartServiceW(h[6], 0, NULL));
        svc_wait_state(h[6], SERVICE_RUNNING);
        oracle_case("start_group_dependency_stopped");
        o_bool("ret", r);
        o_lasterr();
        o_str("s6", st(6));
        o_str("s1", st(1));
        oracle_case_end();
        svc_stop_wait(h[6]);
        /* With the group's member running. */
        svc_start_wait(h[1], 0, NULL);
        ORACLE_CALL(r, StartServiceW(h[6], 0, NULL));
        svc_wait_state(h[6], SERVICE_RUNNING);
        oracle_case("start_group_dependency_running");
        o_bool("ret", r);
        o_lasterr();
        o_str("s6", st(6));
        oracle_case_end();
        svc_stop_wait(h[6]);
        svc_stop_wait(h[1]);
        svc_log_clear();
    }

    for (int i = 1; i <= 8; i++)
        if (h[i]) CloseServiceHandle(h[i]);
    for (int i = 8; i >= 1; i--) svc_purge(scm, svc_name(i));
    CloseServiceHandle(scm);
    svc_log_remove();
    return oracle_end();
}
