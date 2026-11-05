/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * regtool.h: helpers for oracle tests that run reg.exe and regedit.exe.
 *
 * Shared by tests/win32/reg/ and tests/win32/regedit/. The harness hashes this
 * file into each including test's source_sha256, so editing it marks those
 * golden files stale.
 *
 * A tool runs with stdin on an empty pipe (any prompt reads end-of-file),
 * stdout and stderr on separate pipes. Output is decoded from the OEM code page
 * (what console programs write to a redirected handle) and printed as UTF-8.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/procthread/creating-a-child-process-with-redirected-input-and-output
 *       https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/reg
 */
#ifndef SETU_REGTOOL_H
#define SETU_REGTOOL_H

#include "oracle.h"

#define RT_BUF 49152
static char rt_out[RT_BUF], rt_err[RT_BUF];
static DWORD rt_exit;

struct rt_reader { HANDLE pipe; char *buf; DWORD len; };

static DWORD WINAPI rt_read_thread(void *arg)
{
    struct rt_reader *r = arg;
    DWORD got;
    r->len = 0;
    while (r->len < RT_BUF - 1 && ReadFile(r->pipe, r->buf + r->len, RT_BUF - 1 - r->len, &got, NULL) && got)
        r->len += got;
    r->buf[r->len] = 0;
    return 0;
}

/* OEM bytes -> UTF-8, in place (bounded by RT_BUF). */
static inline void rt_decode(char *buf, DWORD len)
{
    static wchar_t w[RT_BUF];
    int n = MultiByteToWideChar(CP_OEMCP, 0, buf, (int)len, w, RT_BUF - 1);
    w[n] = 0;
    n = WideCharToMultiByte(CP_UTF8, 0, w, n, buf, RT_BUF - 1, NULL, NULL);
    buf[n] = 0;
}

/* Runs a command line; fills rt_out, rt_err, rt_exit. Returns 0 if the process
 * could not be started (rt_exit is then 0xffffffff). */
static inline int rt_run(const wchar_t *cmdline)
{
    SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
    HANDLE in_r, in_w, out_r, out_w, err_r, err_w;
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    static wchar_t cmd[4096];
    struct rt_reader ro, re;
    HANDLE th[2];

    rt_out[0] = rt_err[0] = 0;
    rt_exit = 0xffffffff;
    CreatePipe(&in_r, &in_w, &sa, 0);
    CreatePipe(&out_r, &out_w, &sa, 0);
    CreatePipe(&err_r, &err_w, &sa, 0);
    SetHandleInformation(in_w, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(out_r, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(err_r, HANDLE_FLAG_INHERIT, 0);

    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = in_r;
    si.hStdOutput = out_w;
    si.hStdError = err_w;
    wcsncpy(cmd, cmdline, 4095);
    cmd[4095] = 0;
    BOOL ok = CreateProcessW(NULL, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    CloseHandle(in_r);
    CloseHandle(out_w);
    CloseHandle(err_w);
    CloseHandle(in_w); /* stdin: immediate end-of-file */
    if (!ok) {
        CloseHandle(out_r);
        CloseHandle(err_r);
        return 0;
    }
    ro.pipe = out_r; ro.buf = rt_out;
    re.pipe = err_r; re.buf = rt_err;
    th[0] = CreateThread(NULL, 0, rt_read_thread, &ro, 0, NULL);
    th[1] = CreateThread(NULL, 0, rt_read_thread, &re, 0, NULL);
    if (WaitForSingleObject(pi.hProcess, 30000) == WAIT_TIMEOUT)
        TerminateProcess(pi.hProcess, 0xdead);
    WaitForMultipleObjects(2, th, TRUE, 10000);
    GetExitCodeProcess(pi.hProcess, &rt_exit);
    CloseHandle(th[0]);
    CloseHandle(th[1]);
    CloseHandle(out_r);
    CloseHandle(err_r);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    rt_decode(rt_out, ro.len);
    rt_decode(rt_err, re.len);
    return 1;
}

/* One case: run the command, print exit code, stdout and stderr. */
static inline void rt_case(const char *name, const wchar_t *cmdline)
{
    rt_run(cmdline);
    oracle_case(name);
    o_u32("exit", rt_exit);
    o_str("out", rt_out);
    o_str("err", rt_err);
    oracle_case_end();
}

/* Same, but only the exit code and whether anything was printed (for help
 * text and other long messages that the test should not record). */
static inline void rt_case_exit(const char *name, const wchar_t *cmdline)
{
    rt_run(cmdline);
    oracle_case(name);
    o_u32("exit", rt_exit);
    o_bool("out_nonempty", rt_out[0] != 0);
    o_bool("err_nonempty", rt_err[0] != 0);
    oracle_case_end();
}

/* Run a command whose result is checked through other fields of the same case:
 * prints exit/out/err into the currently open case. */
static inline void rt_fields(const wchar_t *cmdline)
{
    rt_run(cmdline);
    o_u32("exit", rt_exit);
    o_str("out", rt_out);
    o_str("err", rt_err);
}

/* ---- files ------------------------------------------------------------ */

/* Writes raw bytes to a file in the working directory. */
static inline void rt_write_file(const wchar_t *path, const void *data, DWORD len)
{
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    DWORD got;
    if (h == INVALID_HANDLE_VALUE) return;
    WriteFile(h, data, len, &got, NULL);
    CloseHandle(h);
}

/* Writes a UTF-16LE file with BOM from a wide string. */
static inline void rt_write_utf16(const wchar_t *path, const wchar_t *text)
{
    static unsigned char b[RT_BUF];
    size_t n = wcslen(text) * 2;
    if (n > RT_BUF - 2) n = RT_BUF - 2;
    b[0] = 0xff; b[1] = 0xfe;
    memcpy(b + 2, text, n);
    rt_write_file(path, b, (DWORD)n + 2);
}

/* Writes an ANSI (ASCII) file. */
static inline void rt_write_ansi(const wchar_t *path, const char *text)
{
    rt_write_file(path, text, (DWORD)strlen(text));
}

/* Prints <prefix>.exists, .encoding ("utf16le-bom", "utf8-bom" or "ansi") and
 * .text (the file decoded to UTF-8) for a text file such as a .reg export. */
static inline void o_text_file(const char *prefix, const wchar_t *path)
{
    static unsigned char b[RT_BUF];
    static wchar_t w[RT_BUF / 2 + 1];
    static char u[RT_BUF];
    char k[64];
    DWORD got = 0;
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    snprintf(k, sizeof k, "%s.exists", prefix);
    o_bool(k, h != INVALID_HANDLE_VALUE);
    if (h == INVALID_HANDLE_VALUE) return;
    ReadFile(h, b, RT_BUF - 2, &got, NULL);
    CloseHandle(h);
    const char *enc;
    int n;
    if (got >= 2 && b[0] == 0xff && b[1] == 0xfe) {
        enc = "utf16le-bom";
        n = (int)(got - 2) / 2;
        memcpy(w, b + 2, (size_t)n * 2);
    } else if (got >= 3 && b[0] == 0xef && b[1] == 0xbb && b[2] == 0xbf) {
        enc = "utf8-bom";
        n = MultiByteToWideChar(CP_UTF8, 0, (char *)b + 3, (int)got - 3, w, RT_BUF / 2);
    } else {
        enc = "ansi";
        n = MultiByteToWideChar(CP_ACP, 0, (char *)b, (int)got, w, RT_BUF / 2);
    }
    n = WideCharToMultiByte(CP_UTF8, 0, w, n, u, RT_BUF - 1, NULL, NULL);
    u[n] = 0;
    snprintf(k, sizeof k, "%s.encoding", prefix);
    o_str(k, enc);
    snprintf(k, sizeof k, "%s.text", prefix);
    o_str(k, u);
}

/* ---- registry state ---------------------------------------------------- */

/* Prints a value as <prefix>.ret / .type / .data (hex). */
static inline void o_regval(const char *prefix, HKEY root, const wchar_t *sub, const wchar_t *name)
{
    char k[96];
    BYTE data[512];
    DWORD type = 0, size = sizeof data;
    HKEY key;
    LONG st = RegOpenKeyExW(root, sub, 0, KEY_QUERY_VALUE, &key);
    if (!st) {
        st = RegQueryValueExW(key, name, NULL, &type, data, &size);
        RegCloseKey(key);
    }
    snprintf(k, sizeof k, "%s.ret", prefix);
    o_status(k, st);
    if (st) return;
    snprintf(k, sizeof k, "%s.type", prefix);
    o_u32(k, type);
    snprintf(k, sizeof k, "%s.data", prefix);
    o_bytes(k, data, size);
}

/* Prints a whole key tree in a stable form (values sorted within each key,
 * subkeys sorted): "name|type|hexdata;..." per key, keys separated by " / ".
 * Used to compare round trips. */
static inline int rt__cmpw(const void *a, const void *b) { return _wcsicmp(*(wchar_t *const *)a, *(wchar_t *const *)b); }

static inline void rt__dump(HKEY key, const wchar_t *path, char *out, size_t cap)
{
    wchar_t *names[128];
    wchar_t nm[256];
    int n = 0;
    DWORD i, len;
    char tmp[1200];
    snprintf(tmp, sizeof tmp, "[%s]", oracle_utf8(path));
    strncat(out, tmp, cap - strlen(out) - 1);
    for (i = 0; n < 128; i++) {
        len = 256;
        if (RegEnumValueW(key, i, nm, &len, NULL, NULL, NULL, NULL)) break;
        names[n++] = _wcsdup(nm);
    }
    qsort(names, n, sizeof names[0], rt__cmpw);
    for (int j = 0; j < n; j++) {
        BYTE data[256];
        DWORD type = 0, size = sizeof data;
        static const char hx[] = "0123456789abcdef";
        char hex[520];
        RegQueryValueExW(key, names[j], NULL, &type, data, &size);
        if (size > sizeof data) size = sizeof data;
        for (DWORD b = 0; b < size; b++) { hex[2 * b] = hx[data[b] >> 4]; hex[2 * b + 1] = hx[data[b] & 15]; }
        hex[2 * size] = 0;
        snprintf(tmp, sizeof tmp, "%s=%lu:%s;", names[j][0] ? oracle_utf8(names[j]) : "@", (unsigned long)type, hex);
        strncat(out, tmp, cap - strlen(out) - 1);
        free(names[j]);
    }
    n = 0;
    for (i = 0; n < 128; i++) {
        len = 256;
        if (RegEnumKeyExW(key, i, nm, &len, NULL, NULL, NULL, NULL)) break;
        names[n++] = _wcsdup(nm);
    }
    qsort(names, n, sizeof names[0], rt__cmpw);
    for (int j = 0; j < n; j++) {
        HKEY sub;
        wchar_t p[512];
        swprintf(p, 512, L"%ls\\%ls", path, names[j]);
        strncat(out, " ", cap - strlen(out) - 1);
        if (!RegOpenKeyExW(key, names[j], 0, KEY_READ, &sub)) {
            rt__dump(sub, p, out, cap);
            RegCloseKey(sub);
        }
        free(names[j]);
    }
}

static inline void o_regtree(const char *k, HKEY root, const wchar_t *sub)
{
    static char out[16384];
    HKEY key;
    out[0] = 0;
    LONG st = RegOpenKeyExW(root, sub, 0, KEY_READ, &key);
    if (st) {
        snprintf(out, sizeof out, "open failed %ld", (long)st);
    } else {
        rt__dump(key, L"", out, sizeof out);
        RegCloseKey(key);
    }
    o_str(k, out);
}

/* The test's key, as reg.exe sees it: HKCU\Software\SetuOracle\<dll>_<api>. */
static inline const wchar_t *rt_key(void) { return oracle__regroot; }

#endif /* SETU_REGTOOL_H */
