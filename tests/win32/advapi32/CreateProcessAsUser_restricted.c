/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: CreateProcessAsUser with restricted primary tokens, the way a
 * sandbox broker (Chromium's, Firefox's) starts its child processes. The
 * creator opens and maps the child's image with its own access, so the child
 * starts even when its primary token's restricting SIDs (the Null SID, "lockdown")
 * grant it access to no file at all, including its own .exe; only what the
 * child does afterwards is checked against that token. A broker then makes the
 * initial thread of the suspended child impersonate a less restricted token
 * (SetThreadToken on the CREATE_SUSPENDED thread) so that startup can load DLLs.
 *
 * Without that impersonation token the child dies during startup, before
 * main(), with STATUS_ACCESS_DENIED: its loader cannot open any DLL (Windows
 * exits 0xC0000022; those cases do not print child_flags, which Setu does not
 * reproduce yet).
 *
 * The test re-runs its own .exe as the child. Each child returns what it saw
 * as an exit code: bit 0 IsTokenRestricted, bit 1 it could open its own .exe for
 * reading, bit 2 the thread has an impersonation token. PIDs and paths are
 * never printed.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessasusera
 *       https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-createrestrictedtoken
 *       https://learn.microsoft.com/en-us/windows/win32/secauthz/restricted-tokens
 *       https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-setthreadtoken
 *       https://learn.microsoft.com/en-us/windows/win32/secauthz/access-tokens
 */
#include "oracle.h"

/* ---- child side -------------------------------------------------------- */
static int child_main(void)
{
    char self[MAX_PATH];
    BOOL restricted = FALSE;
    HANDLE tok, f;
    int code = 0;

    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) {
        restricted = IsTokenRestricted(tok);
        CloseHandle(tok);
    }
    if (restricted) code |= 1;
    GetModuleFileNameA(NULL, self, sizeof self);
    f = CreateFileA(self, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (f != INVALID_HANDLE_VALUE) { code |= 2; CloseHandle(f); }
    if (OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, FALSE, &tok)) { code |= 4; CloseHandle(tok); }
    return code;
}

/* ---- parent side ------------------------------------------------------- */
#define MODE_ALL_SIDS 1   /* restricting SIDs: every group and the user */
#define MODE_NULL_SID 2   /* restricting SID: the Null SID only */
#define MODE_NONE     3   /* no restriction at all */

static HANDLE make_token(int mode, int low_integrity, int impersonation)
{
    HANDLE tok, nt = NULL, dup;
    BYTE gbuf[4096], ubuf[512];
    DWORD n;
    SID_AND_ATTRIBUTES rs[64];
    int c = 0;
    static BYTE nullsid[SECURITY_MAX_SID_SIZE];
    DWORD nl = sizeof nullsid;

    OpenProcessToken(GetCurrentProcess(), TOKEN_ALL_ACCESS, &tok);
    if (mode == MODE_ALL_SIDS) {
        TOKEN_GROUPS *g = (TOKEN_GROUPS *)gbuf;
        GetTokenInformation(tok, TokenGroups, gbuf, sizeof gbuf, &n);
        GetTokenInformation(tok, TokenUser, ubuf, sizeof ubuf, &n);
        for (DWORD i = 0; i < g->GroupCount && c < 60; i++) { rs[c].Sid = g->Groups[i].Sid; rs[c].Attributes = 0; c++; }
        rs[c].Sid = ((TOKEN_USER *)ubuf)->User.Sid; rs[c].Attributes = 0; c++;
    } else if (mode == MODE_NULL_SID) {
        CreateWellKnownSid(WinNullSid, NULL, nullsid, &nl);
        rs[0].Sid = nullsid; rs[0].Attributes = 0; c = 1;
    }
    if (!CreateRestrictedToken(tok, 0, 0, NULL, 0, NULL, c, c ? rs : NULL, &nt)) nt = NULL;
    CloseHandle(tok);
    if (nt && low_integrity) {
        BYTE sid[SECURITY_MAX_SID_SIZE];
        DWORD sl = sizeof sid;
        TOKEN_MANDATORY_LABEL l;
        CreateWellKnownSid(WinLowLabelSid, NULL, sid, &sl);
        l.Label.Sid = sid;
        l.Label.Attributes = SE_GROUP_INTEGRITY;
        SetTokenInformation(nt, TokenIntegrityLevel, &l, sizeof l + sl);
    }
    if (nt && impersonation) {
        if (DuplicateTokenEx(nt, TOKEN_ALL_ACCESS, NULL, SecurityImpersonation, TokenImpersonation, &dup)) {
            CloseHandle(nt);
            nt = dup;
        }
    }
    return nt;
}

static char self[MAX_PATH];

static void run_case(const char *name, HANDLE primary, HANDLE thread_token, const char *args, int report_exit)
{
    char cmd[MAX_PATH + 64];
    STARTUPINFOA si = {.cb = sizeof si};
    PROCESS_INFORMATION pi = {0};
    BOOL ok;
    DWORD ec = 0xdead, w = 0;

    snprintf(cmd, sizeof cmd, "\"%s\" %s", self, args);
    ORACLE_CALL(ok, CreateProcessAsUserA(primary, NULL, cmd, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL, &si, &pi));
    oracle_case(name);
    o_bool("token_restricted", primary ? IsTokenRestricted(primary) : FALSE);
    o_bool("ret", ok);
    o_lasterr();
    if (ok) {
        BOOL st = TRUE;
        if (thread_token) st = SetThreadToken(&pi.hThread, thread_token);
        o_bool("set_thread_token", st);
        ResumeThread(pi.hThread);
        w = WaitForSingleObject(pi.hProcess, 20000);
        GetExitCodeProcess(pi.hProcess, &ec);
        o_str("wait", w == WAIT_OBJECT_0 ? "signaled" : "other");
        if (report_exit) o_u32("child_flags", ec);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    oracle_case_end();
}

int main(int argc, char **argv)
{
    HANDLE t;

    if (argc > 1 && !strcmp(argv[1], "child")) return child_main();
    GetModuleFileNameA(NULL, self, sizeof self);
    oracle_begin("advapi32/CreateProcessAsUser_restricted");

    /* the same restrictions applied to our own thread, for reference */
    run_case("unrestricted_copy", t = make_token(MODE_NONE, 0, 0), NULL, "child", 1);
    if (t) CloseHandle(t);
    run_case("restricted_all_sids", t = make_token(MODE_ALL_SIDS, 0, 0), NULL, "child", 1);
    if (t) CloseHandle(t);
    run_case("restricted_all_sids_low", t = make_token(MODE_ALL_SIDS, 1, 0), NULL, "child", 1);
    if (t) CloseHandle(t);
    run_case("restricted_null_sid", t = make_token(MODE_NULL_SID, 0, 0), NULL, "child", 0);
    if (t) CloseHandle(t);
    run_case("restricted_null_sid_low", t = make_token(MODE_NULL_SID, 1, 0), NULL, "child", 0);
    if (t) CloseHandle(t);

    /* the broker pattern: lockdown primary token, less restricted initial thread token */
    {
        HANDLE lock = make_token(MODE_NULL_SID, 1, 0);
        HANDLE init = make_token(MODE_ALL_SIDS, 1, 1);
        run_case("lockdown_with_initial_thread_token", lock, init, "child", 1);
        if (lock) CloseHandle(lock);
        if (init) CloseHandle(init);
    }
    return oracle_end();
}
