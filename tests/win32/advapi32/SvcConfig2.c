/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: ChangeServiceConfig2W / QueryServiceConfig2W (and the A
 * forms for the description): defaults of a fresh service and round trips
 * for SERVICE_CONFIG_DESCRIPTION, _FAILURE_ACTIONS, _FAILURE_ACTIONS_FLAG,
 * _DELAYED_AUTO_START_INFO, _SERVICE_SID_INFO, _REQUIRED_PRIVILEGES_INFO,
 * _PRESHUTDOWN_INFO, _TRIGGER_INFO, _LAUNCH_PROTECTED; bytes-needed values
 * for NULL and small buffers; invalid levels and values; and the access
 * rights each call needs.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winsvc/nf-winsvc-changeserviceconfig2w
 *       https://learn.microsoft.com/en-us/windows/win32/api/winsvc/nf-winsvc-queryserviceconfig2w
 *       https://learn.microsoft.com/en-us/windows/win32/api/winsvc/ns-winsvc-service_failure_actionsw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winsvc/ns-winsvc-service_sid_info
 *       https://learn.microsoft.com/en-us/windows/win32/api/winsvc/ns-winsvc-service_required_privileges_infow
 *       https://learn.microsoft.com/en-us/windows/win32/api/winsvc/ns-winsvc-service_preshutdown_info
 *       https://learn.microsoft.com/en-us/windows/win32/api/winsvc/ns-winsvc-service_delayed_auto_start_info
 */
#include "svc.h"

#ifndef SERVICE_CONFIG_LAUNCH_PROTECTED
#define SERVICE_CONFIG_LAUNCH_PROTECTED 12
#endif

/* SERVICE_TRIGGER_INFO (winsvc.h, Windows 7+), declared here because the
 * mingw-w64 header only defines it for newer _WIN32_WINNT values. */
typedef struct {
    DWORD cTriggers;
    void *pTriggers;
    BYTE *pReserved;
} trigger_info;

static SC_HANDLE scm;
/* MS-SCMR limits the query buffer to 8 KiB; Windows fails larger ones with
 * RPC_X_BAD_STUB_DATA. */
static BYTE qbuf[8192];

/* Queries `level`, printing ret, error, bytes needed for a NULL buffer and
 * the decoded value. */
static void query_level(SC_HANDLE s, DWORD level)
{
    BOOL r;
    DWORD need = 0xdead;
    ORACLE_CALL(r, QueryServiceConfig2W(s, level, NULL, 0, &need));
    o_bool("null.ret", r);
    o_u32("null.err", oracle_last_error);
    o_u32("null.need", need);
    memset(qbuf, 0xcc, sizeof qbuf);
    ORACLE_CALL(r, QueryServiceConfig2W(s, level, qbuf, sizeof qbuf, &need));
    o_bool("ret", r);
    o_lasterr();
    if (!r) return;
    switch (level) {
    case SERVICE_CONFIG_DESCRIPTION: {
        SERVICE_DESCRIPTIONW *d = (SERVICE_DESCRIPTIONW *)qbuf;
        o_wstr("description", d->lpDescription);
        break;
    }
    case SERVICE_CONFIG_FAILURE_ACTIONS: {
        SERVICE_FAILURE_ACTIONSW *f = (SERVICE_FAILURE_ACTIONSW *)qbuf;
        o_u32("reset_period", f->dwResetPeriod);
        o_wstr("reboot_msg", f->lpRebootMsg);
        o_wstr("command", f->lpCommand);
        o_u32("actions", f->cActions);
        o_bool("actions_ptr_null", f->lpsaActions == NULL);
        for (DWORD i = 0; i < f->cActions && i < 8 && f->lpsaActions; i++) {
            char k[32];
            snprintf(k, sizeof k, "action%lu.type", (unsigned long)i);
            o_u32(k, f->lpsaActions[i].Type);
            snprintf(k, sizeof k, "action%lu.delay", (unsigned long)i);
            o_u32(k, f->lpsaActions[i].Delay);
        }
        break;
    }
    case SERVICE_CONFIG_FAILURE_ACTIONS_FLAG:
        o_u32("flag", ((SERVICE_FAILURE_ACTIONS_FLAG *)qbuf)->fFailureActionsOnNonCrashFailures);
        break;
    case SERVICE_CONFIG_DELAYED_AUTO_START_INFO:
        o_u32("delayed", ((SERVICE_DELAYED_AUTO_START_INFO *)qbuf)->fDelayedAutostart);
        break;
    case SERVICE_CONFIG_SERVICE_SID_INFO:
        o_u32("sid_type", ((SERVICE_SID_INFO *)qbuf)->dwServiceSidType);
        break;
    case SERVICE_CONFIG_REQUIRED_PRIVILEGES_INFO:
        o_multisz("privileges", ((SERVICE_REQUIRED_PRIVILEGES_INFOW *)qbuf)->pmszRequiredPrivileges);
        break;
    case SERVICE_CONFIG_PRESHUTDOWN_INFO:
        o_u32("preshutdown_timeout", ((SERVICE_PRESHUTDOWN_INFO *)qbuf)->dwPreshutdownTimeout);
        break;
    case SERVICE_CONFIG_TRIGGER_INFO:
        o_u32("triggers", ((trigger_info *)qbuf)->cTriggers);
        break;
    case SERVICE_CONFIG_LAUNCH_PROTECTED:
        o_u32("launch_protected", *(DWORD *)qbuf);
        break;
    }
}

static void query_case(const char *name, SC_HANDLE s, DWORD level)
{
    oracle_case(name);
    query_level(s, level);
    oracle_case_end();
}

static void change_case(const char *name, SC_HANDLE s, DWORD level, void *info)
{
    BOOL r;
    ORACLE_CALL(r, ChangeServiceConfig2W(s, level, info));
    oracle_case(name);
    o_bool("change.ret", r);
    o_u32("change.err", r ? 0 : oracle_last_error);
    query_level(s, level);
    oracle_case_end();
}

int main(int argc, char **argv)
{
    int rc = svc_child_main("advapi32/SvcConfig2", argc, argv);
    if (rc >= 0) return rc;
    oracle_begin("advapi32/SvcConfig2");
    scm = svc_scm();
    const wchar_t *a = svc_name(1), *d = svc_name(2);
    svc_purge(scm, a);
    svc_purge(scm, d);
    SC_HANDLE s = CreateServiceW(scm, a, a, SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS, SERVICE_DEMAND_START,
                                 SERVICE_ERROR_NORMAL, L"C:\\setu-oracle\\a.exe", NULL, NULL, NULL, NULL, NULL);
    SC_HANDLE drv = CreateServiceW(scm, d, d, SERVICE_ALL_ACCESS, SERVICE_KERNEL_DRIVER, SERVICE_DEMAND_START,
                                   SERVICE_ERROR_NORMAL, L"C:\\setu-oracle\\d.sys", NULL, NULL, NULL, NULL, NULL);
    oracle_case("setup");
    o_handle("svc", s);
    o_handle("driver", drv);
    oracle_case_end();

    /* Defaults. */
    query_case("default_description", s, SERVICE_CONFIG_DESCRIPTION);
    query_case("default_failure_actions", s, SERVICE_CONFIG_FAILURE_ACTIONS);
    query_case("default_failure_flag", s, SERVICE_CONFIG_FAILURE_ACTIONS_FLAG);
    query_case("default_delayed", s, SERVICE_CONFIG_DELAYED_AUTO_START_INFO);
    query_case("default_sid", s, SERVICE_CONFIG_SERVICE_SID_INFO);
    query_case("default_privileges", s, SERVICE_CONFIG_REQUIRED_PRIVILEGES_INFO);
    query_case("default_preshutdown", s, SERVICE_CONFIG_PRESHUTDOWN_INFO);
    query_case("default_triggers", s, SERVICE_CONFIG_TRIGGER_INFO);
    query_case("default_launch_protected", s, SERVICE_CONFIG_LAUNCH_PROTECTED);
    query_case("driver_default_sid", drv, SERVICE_CONFIG_SERVICE_SID_INFO);
    query_case("driver_default_delayed", drv, SERVICE_CONFIG_DELAYED_AUTO_START_INFO);
    query_case("level_0", s, 0);
    query_case("level_10", s, 10);
    query_case("level_99", s, 99);

    /* Description. */
    {
        SERVICE_DESCRIPTIONW dsc = { (wchar_t *)L"SetuOracle description text" };
        change_case("description_set", s, SERVICE_CONFIG_DESCRIPTION, &dsc);
        /* Bytes-needed behaviour with a set description. */
        BOOL r;
        DWORD need = 0xdead;
        ORACLE_CALL(r, QueryServiceConfig2W(s, SERVICE_CONFIG_DESCRIPTION, qbuf, sizeof(SERVICE_DESCRIPTIONW), &need));
        oracle_case("description_struct_only_buffer");
        o_bool("ret", r);
        o_lasterr();
        o_u32("need", need);
        oracle_case_end();
        DWORD full = 0;
        QueryServiceConfig2W(s, SERVICE_CONFIG_DESCRIPTION, NULL, 0, &full);
        need = 0xdead;
        ORACLE_CALL(r, QueryServiceConfig2W(s, SERVICE_CONFIG_DESCRIPTION, qbuf, full, &need));
        oracle_case("description_exact_buffer");
        o_bool("ret", r);
        o_lasterr();
        o_u32("need", need);
        oracle_case_end();
        need = 0xdead;
        ORACLE_CALL(r, QueryServiceConfig2A(s, SERVICE_CONFIG_DESCRIPTION, NULL, 0, &need));
        oracle_case("description_ansi_null");
        o_bool("ret", r);
        o_lasterr();
        o_u32("need", need);
        oracle_case_end();
        ORACLE_CALL(r, QueryServiceConfig2A(s, SERVICE_CONFIG_DESCRIPTION, qbuf, sizeof qbuf, &need));
        oracle_case("description_ansi");
        o_bool("ret", r);
        o_lasterr();
        if (r) o_str("description", ((SERVICE_DESCRIPTIONA *)qbuf)->lpDescription);
        oracle_case_end();

        dsc.lpDescription = NULL;
        change_case("description_null_keeps", s, SERVICE_CONFIG_DESCRIPTION, &dsc);
        dsc.lpDescription = (wchar_t *)L"";
        change_case("description_empty_clears", s, SERVICE_CONFIG_DESCRIPTION, &dsc);
        SERVICE_DESCRIPTIONA dsca = { (char *)"SetuOracle ANSI description" };
        ORACLE_CALL(r, ChangeServiceConfig2A(s, SERVICE_CONFIG_DESCRIPTION, &dsca));
        oracle_case("description_ansi_set");
        o_bool("change.ret", r);
        query_level(s, SERVICE_CONFIG_DESCRIPTION);
        oracle_case_end();
        ORACLE_CALL(r, ChangeServiceConfig2W(s, SERVICE_CONFIG_DESCRIPTION, NULL));
        oracle_case("description_null_info");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
    }

    /* Failure actions. */
    {
        SC_ACTION act[3] = { { SC_ACTION_RESTART, 1000 }, { SC_ACTION_NONE, 0 }, { SC_ACTION_RUN_COMMAND, 5000 } };
        SERVICE_FAILURE_ACTIONSW fa = { 86400, (wchar_t *)L"SetuOracle reboot message",
                                        (wchar_t *)L"C:\\setu-oracle\\cmd.exe /x", 3, act };
        change_case("failure_set", s, SERVICE_CONFIG_FAILURE_ACTIONS, &fa);
        fa.lpsaActions = NULL;
        fa.cActions = 0;
        fa.lpCommand = NULL;
        fa.lpRebootMsg = NULL;
        fa.dwResetPeriod = 60;
        change_case("failure_null_actions_keeps", s, SERVICE_CONFIG_FAILURE_ACTIONS, &fa);
        fa.lpCommand = (wchar_t *)L"";
        fa.lpRebootMsg = (wchar_t *)L"";
        change_case("failure_empty_strings", s, SERVICE_CONFIG_FAILURE_ACTIONS, &fa);
        fa.lpsaActions = act;
        fa.cActions = 0;
        change_case("failure_zero_actions_clears", s, SERVICE_CONFIG_FAILURE_ACTIONS, &fa);
        SC_ACTION reboot = { SC_ACTION_REBOOT, 1000 };
        fa.lpsaActions = &reboot;
        fa.cActions = 1;
        change_case("failure_reboot_no_privilege", s, SERVICE_CONFIG_FAILURE_ACTIONS, &fa);
        SC_ACTION bogus = { 7, 10 };
        fa.lpsaActions = &bogus;
        change_case("failure_bogus_type", s, SERVICE_CONFIG_FAILURE_ACTIONS, &fa);
        {
            /* SC_ACTION_RESTART needs SERVICE_START on the handle. */
            SC_HANDLE h = OpenServiceW(scm, a, SERVICE_CHANGE_CONFIG | SERVICE_QUERY_CONFIG);
            SC_ACTION restart = { SC_ACTION_RESTART, 2000 };
            fa.lpsaActions = &restart;
            change_case("failure_restart_without_start_right", h, SERVICE_CONFIG_FAILURE_ACTIONS, &fa);
            CloseServiceHandle(h);
        }
        SERVICE_FAILURE_ACTIONS_FLAG fl = { TRUE };
        change_case("failure_flag_set", s, SERVICE_CONFIG_FAILURE_ACTIONS_FLAG, &fl);
        fl.fFailureActionsOnNonCrashFailures = 5;
        change_case("failure_flag_5", s, SERVICE_CONFIG_FAILURE_ACTIONS_FLAG, &fl);
        fl.fFailureActionsOnNonCrashFailures = FALSE;
        change_case("failure_flag_clear", s, SERVICE_CONFIG_FAILURE_ACTIONS_FLAG, &fl);
    }

    /* Delayed auto-start. */
    {
        SERVICE_DELAYED_AUTO_START_INFO dl = { TRUE };
        change_case("delayed_on_demand_start", s, SERVICE_CONFIG_DELAYED_AUTO_START_INFO, &dl);
        ChangeServiceConfigW(s, SERVICE_NO_CHANGE, SERVICE_AUTO_START, SERVICE_NO_CHANGE, NULL, NULL, NULL, NULL, NULL,
                             NULL, NULL);
        change_case("delayed_on_auto_start", s, SERVICE_CONFIG_DELAYED_AUTO_START_INFO, &dl);
        dl.fDelayedAutostart = 2;
        change_case("delayed_2", s, SERVICE_CONFIG_DELAYED_AUTO_START_INFO, &dl);
        dl.fDelayedAutostart = FALSE;
        change_case("delayed_off", s, SERVICE_CONFIG_DELAYED_AUTO_START_INFO, &dl);
        dl.fDelayedAutostart = TRUE;
        change_case("delayed_driver", drv, SERVICE_CONFIG_DELAYED_AUTO_START_INFO, &dl);
        ChangeServiceConfigW(s, SERVICE_NO_CHANGE, SERVICE_DEMAND_START, SERVICE_NO_CHANGE, NULL, NULL, NULL, NULL,
                             NULL, NULL, NULL);
    }

    /* Service SID. */
    {
        SERVICE_SID_INFO si = { SERVICE_SID_TYPE_UNRESTRICTED };
        change_case("sid_unrestricted", s, SERVICE_CONFIG_SERVICE_SID_INFO, &si);
        si.dwServiceSidType = SERVICE_SID_TYPE_RESTRICTED;
        change_case("sid_restricted", s, SERVICE_CONFIG_SERVICE_SID_INFO, &si);
        si.dwServiceSidType = 2;
        change_case("sid_2", s, SERVICE_CONFIG_SERVICE_SID_INFO, &si);
        si.dwServiceSidType = SERVICE_SID_TYPE_NONE;
        change_case("sid_none", s, SERVICE_CONFIG_SERVICE_SID_INFO, &si);
        si.dwServiceSidType = SERVICE_SID_TYPE_UNRESTRICTED;
        change_case("sid_driver", drv, SERVICE_CONFIG_SERVICE_SID_INFO, &si);
    }

    /* Required privileges. */
    {
        SERVICE_REQUIRED_PRIVILEGES_INFOW pi = { (wchar_t *)L"SeShutdownPrivilege\0SeTimeZonePrivilege\0" };
        change_case("privileges_set", s, SERVICE_CONFIG_REQUIRED_PRIVILEGES_INFO, &pi);
        pi.pmszRequiredPrivileges = (wchar_t *)L"SeNoSuchPrivilege\0";
        change_case("privileges_bogus", s, SERVICE_CONFIG_REQUIRED_PRIVILEGES_INFO, &pi);
        pi.pmszRequiredPrivileges = (wchar_t *)L"\0";
        change_case("privileges_empty", s, SERVICE_CONFIG_REQUIRED_PRIVILEGES_INFO, &pi);
        pi.pmszRequiredPrivileges = NULL;
        change_case("privileges_null", s, SERVICE_CONFIG_REQUIRED_PRIVILEGES_INFO, &pi);
    }

    /* Preshutdown, triggers, launch protection. */
    {
        SERVICE_PRESHUTDOWN_INFO ps = { 5000 };
        change_case("preshutdown_5000", s, SERVICE_CONFIG_PRESHUTDOWN_INFO, &ps);
        ps.dwPreshutdownTimeout = 0;
        change_case("preshutdown_0", s, SERVICE_CONFIG_PRESHUTDOWN_INFO, &ps);
        trigger_info ti;
        memset(&ti, 0, sizeof ti);
        change_case("triggers_none", s, SERVICE_CONFIG_TRIGGER_INFO, &ti);
        DWORD lp = 0;
        change_case("launch_protected_none", s, SERVICE_CONFIG_LAUNCH_PROTECTED, &lp);
        BOOL r;
        ORACLE_CALL(r, ChangeServiceConfig2W(s, 0, &lp));
        oracle_case("change_level_0");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, ChangeServiceConfig2W(s, 99, &lp));
        oracle_case("change_level_99");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
    }

    /* Access rights. */
    {
        SC_HANDLE h = OpenServiceW(scm, a, SERVICE_QUERY_STATUS);
        SERVICE_DESCRIPTIONW dsc = { (wchar_t *)L"x" };
        BOOL r;
        DWORD need;
        ORACLE_CALL(r, ChangeServiceConfig2W(h, SERVICE_CONFIG_DESCRIPTION, &dsc));
        oracle_case("access_change_without_right");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, QueryServiceConfig2W(h, SERVICE_CONFIG_DESCRIPTION, qbuf, sizeof qbuf, &need));
        oracle_case("access_query_without_right");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        CloseServiceHandle(h);
        ORACLE_CALL(r, QueryServiceConfig2W(NULL, SERVICE_CONFIG_DESCRIPTION, qbuf, sizeof qbuf, &need));
        oracle_case("query_null_handle");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
    }

    CloseServiceHandle(s);
    CloseServiceHandle(drv);
    svc_purge(scm, a);
    svc_purge(scm, d);
    CloseServiceHandle(scm);
    svc_log_remove();
    return oracle_end();
}
