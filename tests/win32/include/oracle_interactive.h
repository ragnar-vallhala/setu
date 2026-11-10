/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Running a test in the interactive session.
 *
 * DirectComposition and Windows.UI.Composition need the desktop window
 * manager, which a non-interactive logon (the oracle's SSH session)
 * doesn't have. A test that needs it calls oracle_interactive_main(): when
 * need_relay() says this session can't compose, the test runs itself again
 * in the interactive session of the same user, as a scheduled task with an
 * interactive token (schtasks /IT), and copies that run's output. Otherwise
 * (and always under Setu) the cases run in this process. The output is the
 * same either way. The same scheme as the dcomp tests.
 */
#ifndef ORACLE_INTERACTIVE_H
#define ORACLE_INTERACTIVE_H

#include <windows.h>
#include <stdio.h>
#include <wchar.h>

static int oi_run_cmd(const wchar_t *cmd)
{
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    DWORD code = 1;
    wchar_t buf[2048];
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    wcsncpy(buf, cmd, 2047);
    buf[2047] = 0;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = si.hStdOutput = si.hStdError = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE, 3, NULL, OPEN_EXISTING, 0, NULL);
    if (!CreateProcessW(NULL, buf, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) return -1;
    WaitForSingleObject(pi.hProcess, 60000);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(si.hStdInput);
    return (int)code;
}

static int oi_relay(const wchar_t *task)
{
    wchar_t exe[MAX_PATH], dir[MAX_PATH], out[MAX_PATH], done[MAX_PATH], cmd[2048];
    char buf[4096];
    DWORD n;
    HANDLE f;
    int ok = 0;

    GetModuleFileNameW(NULL, exe, MAX_PATH);
    GetCurrentDirectoryW(MAX_PATH, dir);
    swprintf(out, MAX_PATH, L"%ls\\interactive.out", dir);
    swprintf(done, MAX_PATH, L"%ls\\interactive.done", dir);
    DeleteFileW(out);
    DeleteFileW(done);
    swprintf(cmd, 2048, L"schtasks.exe /create /f /it /rl highest /sc once /st 00:00 /tn %ls /tr \"%ls child %ls\"", task, exe, dir);
    if (oi_run_cmd(cmd)) return 0;
    /* the interactive session can still be starting right after the VM is
     * restored: try a few times */
    for (int tries = 0; tries < 5 && !ok; tries++) {
        swprintf(cmd, 2048, L"schtasks.exe /run /tn %ls", task);
        if (!oi_run_cmd(cmd)) {
            for (int i = 0; i < 600 && GetFileAttributesW(done) == INVALID_FILE_ATTRIBUTES; i++) Sleep(100);
            ok = GetFileAttributesW(done) != INVALID_FILE_ATTRIBUTES;
        }
        if (!ok) Sleep(3000);
    }
    swprintf(cmd, 2048, L"schtasks.exe /delete /f /tn %ls", task);
    oi_run_cmd(cmd);
    if (!ok) return 0;
    f = CreateFileW(out, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    while (ReadFile(f, buf, sizeof buf, &n, NULL) && n) fwrite(buf, 1, n, stdout);
    fflush(stdout);
    CloseHandle(f);
    return 1;
}

/* in the scheduled task: the output goes to <dir>\interactive.out, and
 * <dir>\interactive.done marks the end (also after a crash) */
static wchar_t oi_child_done[MAX_PATH];
static LONG WINAPI oi_child_crash(EXCEPTION_POINTERS *ep)
{
    (void)ep;
    fflush(stdout);
    CloseHandle(CreateFileW(oi_child_done, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL));
    return EXCEPTION_EXECUTE_HANDLER;
}

/* main() for such a test: run_cases prints everything (oracle_begin to
 * oracle_end); need_relay returns nonzero where this session can't compose.
 * Extra arguments after " child <dir>" are not supported. */
static int oracle_interactive_main(const wchar_t *task, int (*run_cases)(void), int (*need_relay)(void))
{
    const wchar_t *child = wcsstr(GetCommandLineW(), L" child ");
    if (child) {
        wchar_t out[MAX_PATH];
        const wchar_t *dir = child + 7;
        swprintf(out, MAX_PATH, L"%ls\\interactive.out", dir);
        swprintf(oi_child_done, MAX_PATH, L"%ls\\interactive.done", dir);
        SetCurrentDirectoryW(dir);
        _wfreopen(out, L"wb", stdout);
        SetUnhandledExceptionFilter(oi_child_crash);
        run_cases();
        fflush(stdout);
        fclose(stdout);
        CloseHandle(CreateFileW(oi_child_done, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL));
        return 0;
    }
    if (need_relay() && oi_relay(task)) return 0;
    return run_cases();
}

/* the usual need_relay: DirectComposition device creation is refused */
static int oracle_dcomp_denied(void)
{
    typedef HRESULT (WINAPI *oi_create_fn)(void *, const GUID *, void **);
    typedef ULONG (STDMETHODCALLTYPE *oi_release_fn)(void *);
    static const GUID iid_desktop = { 0x5f4633fe, 0x1e08, 0x4cb8, { 0x8c, 0x75, 0xce, 0x24, 0x33, 0x3f, 0x56, 0x02 } };
    HMODULE m = LoadLibraryW(L"dcomp.dll");
    oi_create_fn f = m ? (oi_create_fn)(void (*)(void))GetProcAddress(m, "DCompositionCreateDevice2") : NULL;
    void *dev = NULL;
    HRESULT hr;
    if (!f) return 0;
    hr = f(NULL, &iid_desktop, &dev);
    if (SUCCEEDED(hr) && dev) ((oi_release_fn)((*(void ***)dev)[2]))(dev);
    return hr == E_ACCESSDENIED;
}

#endif
