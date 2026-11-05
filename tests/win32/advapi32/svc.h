/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * svc.h: helpers shared by the advapi32 Service Control Manager tests.
 *
 * A test program is also its own service: the services it creates run
 *   "<its own exe>" --svc <options>
 * and main() calls svc_child_main() first, which runs the service side when
 * argv[1] is "--svc" (StartServiceCtrlDispatcherW with a generic service
 * whose behaviour is chosen by the options) and returns -1 otherwise.
 *
 * The service side reports what it sees as lines appended to the REG_SZ
 * value "log" under HKLM\SOFTWARE\SetuOracle\<dll>_<test> (a named mutex
 * serialises writers). The test prints that log with o_svclog() and clears
 * it. Nothing machine-specific is logged: no paths, PIDs or handle values.
 *
 * Service names are fixed: SetuOracle_<test>_<n>. svc_purge() stops and
 * deletes a leftover service of that name (Setu's prefix persists between
 * runs, so a crashed earlier run may have left one), and every test calls
 * svc_purge() again on everything it created before it ends.
 *
 * Synchronisation never depends on timing: the test waits (with a generous
 * timeout) for the state it wants and prints only states, never how long
 * anything took.
 *
 * Generic service options (space-separated words after --svc):
 *   accept=<hex>     controls accepted once running (default 0x1, STOP)
 *   exit=<n>         dwWin32ExitCode reported with SERVICE_STOPPED
 *   specific=<n>     dwServiceSpecificExitCode reported with SERVICE_STOPPED
 *   pending          report START_PENDING (checkpoint 7, wait hint 12345),
 *                    signal <name>_ack, wait for <name>_go, then RUNNING
 *   stoppending      on STOP the handler reports STOP_PENDING (checkpoint 3,
 *                    hint 4321); the main thread signals <name>_ack, waits
 *                    for <name>_go, then reports STOPPED
 *   failstart=<n>    report SERVICE_STOPPED with exit code n at once
 *   noreport         ServiceMain returns without calling SetServiceStatus
 *   exitprocess=<n>  once RUNNING, wait for <name>_go then ExitProcess(n)
 *                    without reporting SERVICE_STOPPED
 *   nohandler        don't call RegisterServiceCtrlHandlerExW (also implies
 *                    failstart-like behaviour: logs and returns)
 *   share=<a>,<b>    dispatcher table with these two service names
 *   name=<x>         the service's name (svc_cmdline() always adds it)
 *   tablename=<x>    dispatcher table name instead of name= (own process)
 *   badstatus        once running, try a few invalid SetServiceStatus calls
 *                    and log their results
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/services/service-programs
 *       https://learn.microsoft.com/en-us/windows/win32/api/winsvc/nf-winsvc-startservicectrldispatcherw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winsvc/nf-winsvc-registerservicectrlhandlerexw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winsvc/nf-winsvc-setservicestatus
 *       https://learn.microsoft.com/en-us/windows/win32/api/winsvc/nc-winsvc-lphandler_function_ex
 */
#ifndef SETU_SVC_H
#define SETU_SVC_H

#include "oracle.h"
#include <stdarg.h>

static wchar_t svc_exe[MAX_PATH];     /* this program's own path */
static wchar_t svc_flat[128];         /* "<dll>_<test>" */
static wchar_t svc_short[96];         /* "<test>" */
static wchar_t svc_logkey[256];       /* SOFTWARE\SetuOracle\<dll>_<test> */
static wchar_t svc_mutex_name[256];

static inline void svc_init(const char *test)
{
    size_t n = 0;
    const char *slash = strchr(test, '/');
    for (const char *p = test; *p && n < 127; p++) svc_flat[n++] = (*p == '/') ? L'_' : (wchar_t)*p;
    svc_flat[n] = 0;
    n = 0;
    for (const char *p = slash ? slash + 1 : test; *p && n < 95; p++) svc_short[n++] = (wchar_t)*p;
    svc_short[n] = 0;
    swprintf(svc_logkey, 256, L"SOFTWARE\\SetuOracle\\%ls", svc_flat);
    swprintf(svc_mutex_name, 256, L"Global\\SetuOracle_%ls_log", svc_flat);
    GetModuleFileNameW(NULL, svc_exe, MAX_PATH);
}

/* SetuOracle_<test>_<n> */
static const wchar_t *svc_name(int n)
{
    static wchar_t bufs[8][128];
    static int idx;
    wchar_t *b = bufs[idx++ & 7];
    swprintf(b, 128, L"SetuOracle_%ls_%d", svc_short, n);
    return b;
}

/* ---- the log (both sides) ---------------------------------------------- */

static inline HANDLE svc__mutex(void)
{
    static HANDLE m;
    if (!m) m = CreateMutexW(NULL, FALSE, svc_mutex_name);
    return m;
}

static inline void svc_log(const wchar_t *fmt, ...)
{
    wchar_t line[512];
    static wchar_t buf[16384];
    va_list ap;
    va_start(ap, fmt);
    vswprintf(line, 512, fmt, ap);
    va_end(ap);
    HANDLE m = svc__mutex();
    if (m) WaitForSingleObject(m, 10000);
    HKEY k;
    if (!RegCreateKeyExW(HKEY_LOCAL_MACHINE, svc_logkey, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL)) {
        DWORD size = sizeof(buf) - sizeof(wchar_t), type;
        if (RegQueryValueExW(k, L"log", NULL, &type, (BYTE *)buf, &size) || type != REG_SZ) size = 0;
        buf[size / sizeof(wchar_t)] = 0;
        size_t len = wcslen(buf);
        if (len + wcslen(line) + 2 < sizeof(buf) / sizeof(wchar_t)) {
            if (len) buf[len++] = L'|';
            wcscpy(buf + len, line);
        }
        RegSetValueExW(k, L"log", 0, REG_SZ, (const BYTE *)buf, (DWORD)((wcslen(buf) + 1) * sizeof(wchar_t)));
        RegCloseKey(k);
    }
    if (m) ReleaseMutex(m);
}

/* Prints the log as field k and clears it. */
static inline void o_svclog(const char *k)
{
    static wchar_t buf[16384];
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
    o_wstr(k, buf);
}

static inline void svc_log_clear(void)
{
    HKEY key;
    if (!RegOpenKeyExW(HKEY_LOCAL_MACHINE, svc_logkey, 0, KEY_ALL_ACCESS, &key)) {
        RegDeleteValueW(key, L"log");
        RegCloseKey(key);
    }
}

static inline void svc_log_remove(void)
{
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, svc_logkey);
    RegDeleteKeyW(HKEY_LOCAL_MACHINE, svc_logkey);
    RegDeleteKeyW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\SetuOracle"); /* fails while others use it */
}

/* Named events used to step a service: Global\SetuOracle_<service>_<what>. */
static inline HANDLE svc_event(const wchar_t *service, const wchar_t *what)
{
    wchar_t n[256];
    swprintf(n, 256, L"Global\\%ls_%ls", service, what);
    return CreateEventW(NULL, FALSE, FALSE, n);
}

/* ---- names and strings ------------------------------------------------- */

static const char *svc_state_name(DWORD s)
{
    switch (s) {
    case SERVICE_STOPPED: return "STOPPED";
    case SERVICE_START_PENDING: return "START_PENDING";
    case SERVICE_STOP_PENDING: return "STOP_PENDING";
    case SERVICE_RUNNING: return "RUNNING";
    case SERVICE_CONTINUE_PENDING: return "CONTINUE_PENDING";
    case SERVICE_PAUSE_PENDING: return "PAUSE_PENDING";
    case SERVICE_PAUSED: return "PAUSED";
    }
    static char b[16];
    snprintf(b, sizeof b, "%lu", (unsigned long)s);
    return b;
}

/* A string with this program's path replaced by "<exe>". */
static inline void o_wstr_exe(const char *k, const wchar_t *s)
{
    static wchar_t out[2048];
    size_t n = wcslen(svc_exe), o = 0;
    if (!s) {
        o_str(k, NULL);
        return;
    }
    while (*s && o < 2000) {
        if (n && !_wcsnicmp(s, svc_exe, n)) {
            wcscpy(out + o, L"<exe>");
            o += 5;
            s += n;
        } else {
            out[o++] = *s++;
        }
    }
    out[o] = 0;
    o_wstr(k, out);
}

/* Double-NUL-terminated list as "a,b,c" ("" when empty). */
static inline void o_multisz(const char *k, const wchar_t *s)
{
    static wchar_t out[2048];
    size_t o = 0;
    if (!s) {
        o_str(k, NULL);
        return;
    }
    out[0] = 0;
    for (; *s && o < 1800; s += wcslen(s) + 1) {
        if (o) out[o++] = L',';
        wcscpy(out + o, s);
        o += wcslen(s);
    }
    out[o] = 0;
    o_wstr(k, out);
}

/* SERVICE_STATUS fields, with the prefix p. */
static inline void o_svcstatus(const char *p, const SERVICE_STATUS *s)
{
    char k[64];
    snprintf(k, sizeof k, "%s.type", p); o_hex32(k, s->dwServiceType);
    snprintf(k, sizeof k, "%s.state", p); o_str(k, svc_state_name(s->dwCurrentState));
    snprintf(k, sizeof k, "%s.accepted", p); o_hex32(k, s->dwControlsAccepted);
    snprintf(k, sizeof k, "%s.win32_exit", p); o_u32(k, s->dwWin32ExitCode);
    snprintf(k, sizeof k, "%s.specific_exit", p); o_u32(k, s->dwServiceSpecificExitCode);
    snprintf(k, sizeof k, "%s.checkpoint", p); o_u32(k, s->dwCheckPoint);
    snprintf(k, sizeof k, "%s.wait_hint", p); o_u32(k, s->dwWaitHint);
}

static inline void o_svcstatus_ex(const char *p, const SERVICE_STATUS_PROCESS *s)
{
    char k[64];
    SERVICE_STATUS st;
    st.dwServiceType = s->dwServiceType;
    st.dwCurrentState = s->dwCurrentState;
    st.dwControlsAccepted = s->dwControlsAccepted;
    st.dwWin32ExitCode = s->dwWin32ExitCode;
    st.dwServiceSpecificExitCode = s->dwServiceSpecificExitCode;
    st.dwCheckPoint = s->dwCheckPoint;
    st.dwWaitHint = s->dwWaitHint;
    o_svcstatus(p, &st);
    snprintf(k, sizeof k, "%s.has_pid", p); o_bool(k, s->dwProcessId != 0);
    snprintf(k, sizeof k, "%s.flags", p); o_hex32(k, s->dwServiceFlags);
}

/* ---- test-side helpers ------------------------------------------------- */

static inline SC_HANDLE svc_scm(void)
{
    SC_HANDLE m = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!m) {
        fprintf(stderr, "svc: OpenSCManagerW failed %lu\n", (unsigned long)GetLastError());
        exit(3);
    }
    return m;
}

static inline int svc_query(SC_HANDLE h, SERVICE_STATUS_PROCESS *st)
{
    DWORD need;
    memset(st, 0, sizeof *st);
    return QueryServiceStatusEx(h, SC_STATUS_PROCESS_INFO, (BYTE *)st, sizeof *st, &need);
}

/* Polls until the state is `state` (or the service has stopped, or ~8 s
 * passed). Returns the last status seen. */
static inline SERVICE_STATUS_PROCESS svc_wait_state(SC_HANDLE h, DWORD state)
{
    SERVICE_STATUS_PROCESS st;
    for (int i = 0; i < 400; i++) {
        if (!svc_query(h, &st)) break;
        if (st.dwCurrentState == state) break;
        if (st.dwCurrentState == SERVICE_STOPPED && state != SERVICE_STOPPED) break;
        Sleep(20);
    }
    return st;
}

/* Waits until the service's process has exited (it must be running now). */
static inline void svc_wait_process_exit(DWORD pid)
{
    if (!pid) return;
    HANDLE p = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (p) {
        WaitForSingleObject(p, 8000);
        CloseHandle(p);
    }
}

/* Stops (or kills) and deletes the service `name`, and waits until it is
 * gone. Prints nothing. */
static inline void svc_purge(SC_HANDLE scm, const wchar_t *name)
{
    SC_HANDLE h = OpenServiceW(scm, name, SERVICE_ALL_ACCESS);
    if (!h) return;
    SERVICE_STATUS_PROCESS st;
    SERVICE_STATUS ss;
    if (svc_query(h, &st) && st.dwCurrentState != SERVICE_STOPPED) {
        DWORD pid = st.dwProcessId;
        /* Release a service parked on one of its events first. */
        HANDLE e = svc_event(name, L"go");
        for (int i = 0; i < 3; i++) SetEvent(e), Sleep(10);
        CloseHandle(e);
        if (ControlService(h, SERVICE_CONTROL_STOP, &ss))
            st = svc_wait_state(h, SERVICE_STOPPED);
        else /* a short grace period, e.g. for a service in STOP_PENDING */
            for (int i = 0; i < 100 && svc_query(h, &st) && st.dwCurrentState != SERVICE_STOPPED; i++) Sleep(20);
        if (st.dwCurrentState != SERVICE_STOPPED && pid) {
            HANDLE p = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
            if (p) {
                TerminateProcess(p, 1);
                WaitForSingleObject(p, 10000);
                CloseHandle(p);
            }
            svc_wait_state(h, SERVICE_STOPPED);
        }
        svc_wait_process_exit(pid);
    }
    DeleteService(h);
    CloseServiceHandle(h);
    for (int i = 0; i < 250; i++) {
        h = OpenServiceW(scm, name, SERVICE_QUERY_STATUS);
        if (!h) break;
        CloseServiceHandle(h);
        Sleep(20);
    }
}

/* "<exe>" --svc name=<name> <opts> */
static const wchar_t *svc_cmdline(const wchar_t *name, const wchar_t *opts)
{
    static wchar_t bufs[4][MAX_PATH + 512];
    static int idx;
    wchar_t *b = bufs[idx++ & 3];
    swprintf(b, MAX_PATH + 512, L"\"%ls\" --svc name=%ls %ls", svc_exe, name, opts);
    return b;
}

/* Creates an own-process, demand-start service running this program. */
static inline SC_HANDLE svc_create(SC_HANDLE scm, const wchar_t *name, const wchar_t *opts)
{
    svc_purge(scm, name);
    return CreateServiceW(scm, name, name, SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS, SERVICE_DEMAND_START,
                          SERVICE_ERROR_IGNORE, svc_cmdline(name, opts), NULL, NULL, NULL, NULL, NULL);
}

/* Starts a service and waits until it is RUNNING (or stopped). */
static inline SERVICE_STATUS_PROCESS svc_start_wait(SC_HANDLE h, DWORD argc, const wchar_t **argv)
{
    SERVICE_STATUS_PROCESS st;
    if (!StartServiceW(h, argc, argv)) {
        memset(&st, 0, sizeof st);
        st.dwCurrentState = 0;
        return st;
    }
    return svc_wait_state(h, SERVICE_RUNNING);
}

/* Stops a running service and waits until it stopped and its process ended. */
static inline void svc_stop_wait(SC_HANDLE h)
{
    SERVICE_STATUS_PROCESS st;
    SERVICE_STATUS ss;
    svc_query(h, &st);
    DWORD pid = st.dwProcessId;
    ControlService(h, SERVICE_CONTROL_STOP, &ss);
    svc_wait_state(h, SERVICE_STOPPED);
    svc_wait_process_exit(pid);
}

/* Waits until the log contains `needle` (the service is asynchronous). */
static inline void svc_wait_log(const wchar_t *needle)
{
    static wchar_t buf[16384];
    for (int i = 0; i < 400; i++) {
        HKEY key;
        buf[0] = 0;
        if (!RegOpenKeyExW(HKEY_LOCAL_MACHINE, svc_logkey, 0, KEY_READ, &key)) {
            DWORD size = sizeof(buf) - sizeof(wchar_t), type;
            if (RegQueryValueExW(key, L"log", NULL, &type, (BYTE *)buf, &size)) size = 0;
            buf[size / sizeof(wchar_t)] = 0;
            RegCloseKey(key);
        }
        if (wcsstr(buf, needle)) return;
        Sleep(20);
    }
}

/* ---- service side ------------------------------------------------------ */

struct svc_opts {
    DWORD accept, exit_code, specific, failstart_code;
    int pending, stoppending, failstart, noreport, exitprocess, nohandler, badstatus;
    DWORD exitprocess_code;
    wchar_t name[128], share_a[128], share_b[128], tablename[128];
};
static struct svc_opts svc_o;

struct svc_ctx {
    wchar_t name[128];
    SERVICE_STATUS_HANDLE h;
    SERVICE_STATUS st;
    HANDLE stop;
    int stopping;
};
static struct svc_ctx svc_ctxs[2];
static int svc_nctx;
static CRITICAL_SECTION svc_cs;

static inline DWORD svc__set(struct svc_ctx *c, DWORD state, DWORD cp, DWORD hint)
{
    c->st.dwCurrentState = state;
    c->st.dwCheckPoint = cp;
    c->st.dwWaitHint = hint;
    SetLastError(ORACLE_SENTINEL);
    BOOL r = SetServiceStatus(c->h, &c->st);
    return r ? 0 : GetLastError();
}

/* Logs "<name>:<what>" and then reports the state, so a test that waits for
 * the state always finds the line; a failure is logged afterwards. */
static inline void svc__setlog(struct svc_ctx *c, const wchar_t *what, DWORD state, DWORD cp, DWORD hint)
{
    svc_log(L"%ls:%ls", c->name, what);
    DWORD err = svc__set(c, state, cp, hint);
    if (err) svc_log(L"%ls:%ls_failed=%lu", c->name, what, (unsigned long)err);
}

static inline DWORD WINAPI svc__handler(DWORD control, DWORD type, LPVOID data, LPVOID context)
{
    struct svc_ctx *c = context;
    (void)data;
    EnterCriticalSection(&svc_cs);
    svc_log(L"%ls:ctl=%lu,type=%lu", c->name, (unsigned long)control, (unsigned long)type);
    DWORD ret = NO_ERROR;
    switch (control) {
    case SERVICE_CONTROL_STOP:
        if (!c->stopping) {
            c->stopping = 1;
            if (!svc_o.stoppending) svc__set(c, SERVICE_STOP_PENDING, 1, 3000);
            else svc__setlog(c, L"set_stop_pending", SERVICE_STOP_PENDING, 3, 4321);
            SetEvent(c->stop);
        }
        break;
    case SERVICE_CONTROL_PAUSE:
        svc__set(c, SERVICE_PAUSED, 0, 0);
        break;
    case SERVICE_CONTROL_CONTINUE:
        svc__set(c, SERVICE_RUNNING, 0, 0);
        break;
    case SERVICE_CONTROL_INTERROGATE:
        break;
    case 200: ret = ERROR_INVALID_DATA; break;           /* user-defined code that "fails" */
    case 201: ret = ERROR_CALL_NOT_IMPLEMENTED; break;
    case 202: /* user-defined code that changes the reported state */
        c->st.dwControlsAccepted |= SERVICE_ACCEPT_PAUSE_CONTINUE;
        svc__set(c, SERVICE_RUNNING, 0, 0);
        break;
    default:
        if (control < 128) ret = ERROR_CALL_NOT_IMPLEMENTED;
        break;
    }
    LeaveCriticalSection(&svc_cs);
    return ret;
}

static inline void svc__wait_named(const wchar_t *name, const wchar_t *what)
{
    HANDLE e = svc_event(name, what);
    WaitForSingleObject(e, 30000);
    CloseHandle(e);
}

static inline void svc__signal_named(const wchar_t *name, const wchar_t *what)
{
    HANDLE e = svc_event(name, what);
    SetEvent(e);
    CloseHandle(e);
}

static inline void WINAPI svc__main(DWORD argc, LPWSTR *argv)
{
    struct svc_ctx *c;
    wchar_t args[1024] = L"";
    EnterCriticalSection(&svc_cs);
    c = &svc_ctxs[svc_nctx++ & 1];
    LeaveCriticalSection(&svc_cs);
    /* Own process: the configured name (argv[0] is observed, not relied on). */
    wcsncpy(c->name, !svc_o.share_a[0] && svc_o.name[0] ? svc_o.name
                     : argc && argv && argv[0] ? argv[0] : L"(none)", 127);
    if (!svc_o.share_a[0] && svc_o.tablename[0] && argc && argv && argv[0])
        svc_log(L"argv0_is_table=%d,argv0_is_name=%d", !wcscmp(argv[0], svc_o.tablename),
                !_wcsicmp(argv[0], svc_o.name));
    for (DWORD i = 1; i < argc && argv; i++) {
        if (wcslen(args) + wcslen(argv[i]) + 4 > 1000) break;
        if (i > 1) wcscat(args, L",");
        wcscat(args, L"[");
        wcscat(args, argv[i]);
        wcscat(args, L"]");
    }
    svc_log(L"%ls:main,argc=%lu,args=%ls", c->name, (unsigned long)argc, args);
    if (svc_o.nohandler) return;
    if (svc_o.noreport) {
        /* Register but never report a state. */
        RegisterServiceCtrlHandlerExW(c->name, svc__handler, c);
        return;
    }
    c->stop = CreateEventW(NULL, TRUE, FALSE, NULL);
    SetLastError(ORACLE_SENTINEL);
    c->h = RegisterServiceCtrlHandlerExW(c->name, svc__handler, c);
    DWORD err = GetLastError();
    svc_log(L"%ls:register=%ls,err=%lu", c->name, c->h ? L"ok" : L"NULL", c->h ? 0ul : (unsigned long)err);
    if (!c->h) return;
    c->st.dwServiceType = svc_o.share_a[0] ? SERVICE_WIN32_SHARE_PROCESS : SERVICE_WIN32_OWN_PROCESS;
    c->st.dwControlsAccepted = 0;
    c->st.dwWin32ExitCode = NO_ERROR;
    c->st.dwServiceSpecificExitCode = 0;
    if (svc_o.failstart) {
        c->st.dwWin32ExitCode = svc_o.failstart_code;
        c->st.dwServiceSpecificExitCode = svc_o.specific;
        EnterCriticalSection(&svc_cs);
        svc__setlog(c, L"set_stopped", SERVICE_STOPPED, 0, 0);
        LeaveCriticalSection(&svc_cs);
        return;
    }
    if (svc_o.pending) {
        EnterCriticalSection(&svc_cs);
        svc__setlog(c, L"set_pending", SERVICE_START_PENDING, 7, 12345);
        LeaveCriticalSection(&svc_cs);
        svc__signal_named(c->name, L"ack");
        svc__wait_named(c->name, L"go");
    }
    c->st.dwControlsAccepted = svc_o.accept;
    EnterCriticalSection(&svc_cs);
    svc__setlog(c, L"set_running", SERVICE_RUNNING, 0, 0);
    LeaveCriticalSection(&svc_cs);
    if (svc_o.badstatus) {
        SERVICE_STATUS bad = c->st;
        bad.dwCurrentState = 0x42;
        SetLastError(ORACLE_SENTINEL);
        BOOL r = SetServiceStatus(c->h, &bad);
        svc_log(L"bad_state=%d,err=%lu", r, r ? 0ul : (unsigned long)GetLastError());
        bad = c->st;
        bad.dwServiceType = 0x12345;
        SetLastError(ORACLE_SENTINEL);
        r = SetServiceStatus(c->h, &bad);
        svc_log(L"bad_type=%d,err=%lu", r, r ? 0ul : (unsigned long)GetLastError());
        bad = c->st;
        bad.dwControlsAccepted = 0xffffffff;
        SetLastError(ORACLE_SENTINEL);
        r = SetServiceStatus(c->h, &bad);
        svc_log(L"bad_accept=%d,err=%lu", r, r ? 0ul : (unsigned long)GetLastError());
        SetLastError(ORACLE_SENTINEL);
        r = SetServiceStatus(NULL, &c->st);
        svc_log(L"null_handle=%d,err=%lu", r, r ? 0ul : (unsigned long)GetLastError());
        SetLastError(ORACLE_SENTINEL);
        r = SetServiceStatus(c->h, &c->st);
        svc_log(L"restore=%d,err=%lu", r, r ? 0ul : (unsigned long)GetLastError());
    }
    if (svc_o.exitprocess) {
        svc__wait_named(c->name, L"go");
        svc_log(L"%ls:exitprocess", c->name);
        ExitProcess(svc_o.exitprocess_code);
    }
    WaitForSingleObject(c->stop, INFINITE);
    if (svc_o.stoppending) {
        svc__signal_named(c->name, L"ack");
        svc__wait_named(c->name, L"go");
    }
    c->st.dwWin32ExitCode = svc_o.exit_code;
    c->st.dwServiceSpecificExitCode = svc_o.specific;
    c->st.dwControlsAccepted = 0;
    EnterCriticalSection(&svc_cs);
    svc__setlog(c, L"set_stopped", SERVICE_STOPPED, 0, 0);
    LeaveCriticalSection(&svc_cs);
}

static inline void svc__parse(int argc, char **cargv)
{
    static wchar_t wargs[32][256];
    svc_o.accept = SERVICE_ACCEPT_STOP;
    for (int i = 2; i < argc && i < 32; i++) {
        MultiByteToWideChar(CP_ACP, 0, cargv[i], -1, wargs[i], 256);
        const wchar_t *a = wargs[i];
        if (!wcsncmp(a, L"name=", 5)) wcsncpy(svc_o.name, a + 5, 127);
        else if (!wcsncmp(a, L"accept=", 7)) svc_o.accept = wcstoul(a + 7, NULL, 16);
        else if (!wcsncmp(a, L"exit=", 5)) svc_o.exit_code = wcstoul(a + 5, NULL, 10);
        else if (!wcsncmp(a, L"specific=", 9)) svc_o.specific = wcstoul(a + 9, NULL, 10);
        else if (!wcscmp(a, L"pending")) svc_o.pending = 1;
        else if (!wcscmp(a, L"stoppending")) svc_o.stoppending = 1;
        else if (!wcsncmp(a, L"failstart=", 10)) svc_o.failstart = 1, svc_o.failstart_code = wcstoul(a + 10, NULL, 10);
        else if (!wcscmp(a, L"noreport")) svc_o.noreport = 1;
        else if (!wcscmp(a, L"nohandler")) svc_o.nohandler = 1;
        else if (!wcscmp(a, L"badstatus")) svc_o.badstatus = 1;
        else if (!wcsncmp(a, L"exitprocess=", 12)) svc_o.exitprocess = 1, svc_o.exitprocess_code = wcstoul(a + 12, NULL, 10);
        else if (!wcsncmp(a, L"tablename=", 10)) wcsncpy(svc_o.tablename, a + 10, 127);
        else if (!wcsncmp(a, L"share=", 6)) {
            const wchar_t *comma = wcschr(a + 6, L',');
            if (comma) {
                wcsncpy(svc_o.share_a, a + 6, comma - (a + 6) < 127 ? comma - (a + 6) : 127);
                wcsncpy(svc_o.share_b, comma + 1, 127);
            }
        }
    }
}

/* Runs the service side if this process was started as a service. Returns
 * -1 if not (then main() runs the test), else the process exit code. */
static inline int svc_child_main(const char *test, int argc, char **argv)
{
    svc_init(test);
    if (argc < 2 || strcmp(argv[1], "--svc")) return -1;
    InitializeCriticalSection(&svc_cs);
    svc__parse(argc, argv);
    SERVICE_TABLE_ENTRYW table[3];
    memset(table, 0, sizeof table);
    if (svc_o.share_a[0]) {
        table[0].lpServiceName = svc_o.share_a;
        table[0].lpServiceProc = svc__main;
        table[1].lpServiceName = svc_o.share_b;
        table[1].lpServiceProc = svc__main;
    } else {
        table[0].lpServiceName = svc_o.tablename[0] ? svc_o.tablename : svc_o.name;
        table[0].lpServiceProc = svc__main;
    }
    SetLastError(ORACLE_SENTINEL);
    BOOL r = StartServiceCtrlDispatcherW(table);
    DWORD err = GetLastError();
    EnterCriticalSection(&svc_cs);
    svc_log(L"dispatcher=%d,err=%lu", r, r ? 0ul : (unsigned long)err);
    LeaveCriticalSection(&svc_cs);
    return 0;
}

#endif /* SETU_SVC_H */
