/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: LockServiceDatabase / UnlockServiceDatabase /
 * QueryServiceLockStatusW: locking twice (from the same and another SCM
 * handle), unlocking twice, a NULL lock, the access right needed, the lock
 * status fields (locked or not, owner name only as "is set", duration
 * never printed), and whether CreateService / StartService / ChangeServiceConfig
 * are affected while the database is locked.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winsvc/nf-winsvc-lockservicedatabase
 *       https://learn.microsoft.com/en-us/windows/win32/api/winsvc/nf-winsvc-unlockservicedatabase
 *       https://learn.microsoft.com/en-us/windows/win32/api/winsvc/nf-winsvc-queryservicelockstatusw
 */
#include "svc.h"

static SC_HANDLE scm;

static void lock_status(const char *name, SC_HANDLE m, DWORD size)
{
    static BYTE buf[4096];
    QUERY_SERVICE_LOCK_STATUSW *ls = (QUERY_SERVICE_LOCK_STATUSW *)buf;
    DWORD need = 0xdead;
    BOOL r;
    ORACLE_CALL(r, QueryServiceLockStatusW(m, size ? ls : NULL, size, &need));
    oracle_case(name);
    o_bool("ret", r);
    o_lasterr();
    if (!r) o_u32("need", need);
    if (r) {
        o_u32("locked", ls->fIsLocked);
        o_bool("owner_null", ls->lpLockOwner == NULL);
        o_bool("owner_empty", ls->lpLockOwner && !ls->lpLockOwner[0]);
    }
    oracle_case_end();
}

int main(int argc, char **argv)
{
    int rc = svc_child_main("advapi32/SvcLock", argc, argv);
    if (rc >= 0) return rc;
    oracle_begin("advapi32/SvcLock");
    scm = svc_scm();
    const wchar_t *n = svc_name(1);
    svc_purge(scm, n);
    SC_LOCK lock, lock2;
    BOOL r;

    lock_status("status_unlocked", scm, 4096);
    lock_status("status_null_buffer", scm, 0);

    ORACLE_CALL(lock, LockServiceDatabase(scm));
    oracle_case("lock");
    o_bool("ret", lock != NULL);
    o_lasterr();
    oracle_case_end();

    lock_status("status_locked", scm, 4096);

    ORACLE_CALL(lock2, LockServiceDatabase(scm));
    oracle_case("lock_again_same_handle");
    o_bool("ret", lock2 != NULL);
    o_lasterr();
    oracle_case_end();
    if (lock2) UnlockServiceDatabase(lock2);

    {
        SC_HANDLE m2 = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
        ORACLE_CALL(lock2, LockServiceDatabase(m2));
        oracle_case("lock_again_other_handle");
        o_bool("ret", lock2 != NULL);
        o_lasterr();
        oracle_case_end();
        if (lock2) UnlockServiceDatabase(lock2);
        CloseServiceHandle(m2);
    }

    /* Operations while locked. */
    {
        SC_HANDLE s;
        ORACLE_CALL(s, CreateServiceW(scm, n, n, SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS, SERVICE_DEMAND_START,
                                      SERVICE_ERROR_IGNORE, svc_cmdline(n, L""), NULL, NULL, NULL, NULL, NULL));
        oracle_case("create_while_locked");
        o_handle("ret", s);
        o_lasterr();
        oracle_case_end();
        if (s) {
            ORACLE_CALL(r, ChangeServiceConfigW(s, SERVICE_NO_CHANGE, SERVICE_AUTO_START, SERVICE_NO_CHANGE, NULL,
                                                NULL, NULL, NULL, NULL, NULL, NULL));
            oracle_case("change_while_locked");
            o_bool("ret", r);
            o_lasterr();
            oracle_case_end();
            ORACLE_CALL(r, StartServiceW(s, 0, NULL));
            oracle_case("start_while_locked");
            o_bool("ret", r);
            o_lasterr();
            oracle_case_end();
            if (r) svc_stop_wait(s);
            CloseServiceHandle(s);
        }
    }

    ORACLE_CALL(r, UnlockServiceDatabase(lock));
    oracle_case("unlock");
    o_bool("ret", r);
    o_lasterr();
    oracle_case_end();

    lock_status("status_after_unlock", scm, 4096);

    ORACLE_CALL(r, UnlockServiceDatabase(NULL));
    oracle_case("unlock_null");
    o_bool("ret", r);
    o_lasterr();
    oracle_case_end();

    {
        SC_HANDLE m = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
        ORACLE_CALL(lock2, LockServiceDatabase(m));
        oracle_case("lock_without_right");
        o_bool("ret", lock2 != NULL);
        o_lasterr();
        oracle_case_end();
        if (lock2) UnlockServiceDatabase(lock2);
        lock_status("status_without_right", m, 4096);
        CloseServiceHandle(m);
        m = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT | SC_MANAGER_QUERY_LOCK_STATUS);
        lock_status("status_with_right", m, 4096);
        CloseServiceHandle(m);
    }

    {
        /* Lock released when its SCM handle is closed? */
        SC_HANDLE m = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
        lock2 = LockServiceDatabase(m);
        oracle_case("lock_then_close_handle");
        o_bool("locked", lock2 != NULL);
        oracle_case_end();
        CloseServiceHandle(m);
        lock_status("status_after_closing_lock_owner", scm, 4096);
        if (lock2) {
            ORACLE_CALL(r, UnlockServiceDatabase(lock2));
            oracle_case("unlock_after_close");
            o_bool("ret", r);
            o_lasterr();
            oracle_case_end();
        }
        lock_status("status_end", scm, 4096);
    }

    {
        DWORD need = 0;
        ORACLE_CALL(r, QueryServiceLockStatusA(scm, NULL, 0, &need));
        oracle_case("ansi_status_null_buffer");
        o_bool("ret", r);
        o_lasterr();
        o_u32("need", need);
        oracle_case_end();
    }

    svc_purge(scm, n);
    CloseServiceHandle(scm);
    svc_log_remove();
    return oracle_end();
}
