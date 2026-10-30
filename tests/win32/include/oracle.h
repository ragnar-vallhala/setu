/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * oracle.h: tiny helper for Setu oracle test programs.
 *
 * A test program prints deterministic JSON Lines on stdout:
 *
 *   {"oracle":1,"test":"kernel32/CreateFileW"}
 *   {"case":"create_new","ret":"valid","last_error":0}
 *   ...
 *   {"end":true,"cases":12}
 *
 * Rules (see docs/ORACLE.md):
 *   - Never print pointers, handle values, timestamps, PIDs, absolute paths,
 *     or anything else that differs between runs or machines.
 *   - Use ORACLE_CALL() so last_error is captured right after the API call,
 *     with a sentinel set before it. That shows whether the API touched the
 *     thread's last-error value at all.
 *   - Work only in the current directory (the harness gives each test a fresh
 *     one) and under HKCU\Software\SetuOracle\<name>.
 *
 * Header-only on purpose: each test is one .c file plus this header.
 */
#ifndef SETU_ORACLE_H
#define SETU_ORACLE_H

/* C99 printf semantics (%llu, %ls) identical on every msvcrt version. */
#define __USE_MINGW_ANSI_STDIO 1
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define ORACLE_SCHEMA 1
/* Value placed in the thread's last-error slot before each call under test. */
#define ORACLE_SENTINEL 0xDEADBEEFu
#define ORACLE_REG_BASE L"Software\\SetuOracle"

/* ---- output buffer ---------------------------------------------------- */

static char oracle__buf[65536];
static size_t oracle__len;
static int oracle__first;
static int oracle__ncases;
static DWORD oracle_last_error;
static wchar_t oracle__regroot[256];

static inline void oracle__raw(const char *s, size_t n)
{
    if (oracle__len + n >= sizeof(oracle__buf) - 4) {
        n = sizeof(oracle__buf) - 4 - oracle__len; /* truncate, never overflow */
    }
    memcpy(oracle__buf + oracle__len, s, n);
    oracle__len += n;
    oracle__buf[oracle__len] = 0;
}
static inline void oracle__puts(const char *s) { oracle__raw(s, strlen(s)); }

static inline void oracle__str(const char *s) /* JSON string from UTF-8 */
{
    char tmp[8];
    oracle__raw("\"", 1);
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        switch (*p) {
        case '"': oracle__raw("\\\"", 2); break;
        case '\\': oracle__raw("\\\\", 2); break;
        case '\n': oracle__raw("\\n", 2); break;
        case '\r': oracle__raw("\\r", 2); break;
        case '\t': oracle__raw("\\t", 2); break;
        default:
            if (*p < 0x20) {
                snprintf(tmp, sizeof tmp, "\\u%04x", *p);
                oracle__puts(tmp);
            } else {
                oracle__raw((const char *)p, 1);
            }
        }
    }
    oracle__raw("\"", 1);
}

static inline void oracle__key(const char *k)
{
    if (!oracle__first) oracle__raw(",", 1);
    oracle__first = 0;
    oracle__str(k);
    oracle__raw(":", 1);
}

static inline void oracle__flush(void)
{
    oracle__raw("\n", 1);
    fwrite(oracle__buf, 1, oracle__len, stdout);
    fflush(stdout); /* flush per line: a crash still leaves earlier cases */
    oracle__len = 0;
    oracle__buf[0] = 0;
}

/* Convert a wide string to UTF-8 in a static rotating buffer. */
static inline const char *oracle_utf8(const wchar_t *w)
{
    static char bufs[4][1024];
    static int idx;
    char *b = bufs[idx++ & 3];
    if (!w) return "(null)";
    if (!WideCharToMultiByte(CP_UTF8, 0, w, -1, b, sizeof bufs[0], NULL, NULL))
        strcpy(b, "(unconvertible)");
    return b;
}

/* ---- fields ------------------------------------------------------------ */

static inline void o_str(const char *k, const char *v) { oracle__key(k); if (v) oracle__str(v); else oracle__puts("null"); }
static inline void o_wstr(const char *k, const wchar_t *v) { if (v) o_str(k, oracle_utf8(v)); else o_str(k, NULL); }
static inline void o_bool(const char *k, int v) { oracle__key(k); oracle__puts(v ? "true" : "false"); }
static inline void o_u64(const char *k, unsigned long long v)
{
    char t[32];
    snprintf(t, sizeof t, "%llu", v);
    oracle__key(k);
    oracle__puts(t);
}
static inline void o_i64(const char *k, long long v)
{
    char t[32];
    snprintf(t, sizeof t, "%lld", v);
    oracle__key(k);
    oracle__puts(t);
}
static inline void o_u32(const char *k, DWORD v) { o_u64(k, v); }
static inline void o_hex32(const char *k, DWORD v)
{
    char t[16];
    snprintf(t, sizeof t, "0x%08lx", (unsigned long)v);
    o_str(k, t);
}
/* Bytes as lowercase hex, e.g. "48690a". Capped at 256 bytes. */
static inline void o_bytes(const char *k, const void *data, size_t n)
{
    static const char hx[] = "0123456789abcdef";
    char t[3] = {0};
    const unsigned char *p = data;
    oracle__key(k);
    oracle__raw("\"", 1);
    for (size_t i = 0; i < n && i < 256; i++) {
        t[0] = hx[p[i] >> 4];
        t[1] = hx[p[i] & 15];
        oracle__raw(t, 2);
    }
    if (n > 256) oracle__puts("...");
    oracle__raw("\"", 1);
}
/* A HANDLE is classified, never printed: "valid", "INVALID_HANDLE_VALUE" or "NULL". */
static inline void o_handle(const char *k, HANDLE h)
{
    o_str(k, h == INVALID_HANDLE_VALUE ? "INVALID_HANDLE_VALUE" : h == NULL ? "NULL" : "valid");
}
/* last_error captured by ORACLE_CALL; "unchanged" if the API left the sentinel. */
static inline void o_lasterr(void)
{
    if (oracle_last_error == ORACLE_SENTINEL) o_str("last_error", "unchanged");
    else o_u32("last_error", oracle_last_error);
}
/* An LSTATUS / error-code return value (registry APIs). */
static inline void o_status(const char *k, LONG v) { o_u32(k, (DWORD)v); }

/* Call an API with the sentinel in place and capture GetLastError() at once. */
#define ORACLE_CALL(var, expr)              \
    do {                                    \
        SetLastError(ORACLE_SENTINEL);      \
        (var) = (expr);                     \
        oracle_last_error = GetLastError(); \
    } while (0)

/* ---- cases ----------------------------------------------------------- */

static inline void oracle_case(const char *name)
{
    oracle__len = 0;
    oracle__first = 1;
    oracle__raw("{", 1);
    o_str("case", name);
}
static inline void oracle_case_end(void)
{
    oracle__raw("}", 1);
    oracle__flush();
    oracle__ncases++;
}

/* ---- file-state helpers ------------------------------------------------ */

/* Records exists / attributes / size / content (first bytes) of a path. */
static inline void o_file_state(const char *prefix, const wchar_t *path)
{
    char k[64];
    WIN32_FILE_ATTRIBUTE_DATA fad;
    BOOL ok = GetFileAttributesExW(path, GetFileExInfoStandard, &fad);
    snprintf(k, sizeof k, "%s.exists", prefix);
    o_bool(k, ok);
    if (!ok) return;
    snprintf(k, sizeof k, "%s.attrs", prefix);
    o_hex32(k, fad.dwFileAttributes);
    if (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return;
    snprintf(k, sizeof k, "%s.size", prefix);
    o_u64(k, ((unsigned long long)fad.nFileSizeHigh << 32) | fad.nFileSizeLow);
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING, 0, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        unsigned char data[64];
        DWORD got = 0;
        ReadFile(h, data, sizeof data, &got, NULL);
        CloseHandle(h);
        snprintf(k, sizeof k, "%s.data", prefix);
        o_bytes(k, data, got);
    }
}

static inline int oracle__wcmp(const void *a, const void *b)
{
    return wcscmp(*(const wchar_t *const *)a, *(const wchar_t *const *)b);
}

/* Directory listing (names only, without "." and ".."), as one comma-separated
 * string. sorted=1 gives a stable state snapshot; sorted=0 keeps enumeration
 * order, which is itself observable behaviour. */
static inline void o_dir_listing(const char *k, const wchar_t *dir, int sorted)
{
    wchar_t pat[MAX_PATH], *names[256];
    char out[4096] = "";
    int n = 0;
    WIN32_FIND_DATAW fd;
    swprintf(pat, MAX_PATH, L"%ls\\*", dir);
    HANDLE f = FindFirstFileW(pat, &fd);
    if (f != INVALID_HANDLE_VALUE) {
        do {
            if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
            if (n < 256) names[n++] = _wcsdup(fd.cFileName);
        } while (FindNextFileW(f, &fd));
        FindClose(f);
    }
    if (sorted) qsort(names, n, sizeof names[0], oracle__wcmp);
    for (int i = 0; i < n; i++) {
        if (i) strncat(out, ",", sizeof out - strlen(out) - 1);
        strncat(out, oracle_utf8(names[i]), sizeof out - strlen(out) - 1);
        free(names[i]);
    }
    o_str(k, out);
}

/* ---- registry helpers -------------------------------------------------- */

/* Opens (creating if needed) HKCU\Software\SetuOracle\<name>, wiped clean. */
static inline HKEY oracle_reg_root(void)
{
    HKEY k = NULL;
    RegDeleteTreeW(HKEY_CURRENT_USER, oracle__regroot);
    RegDeleteKeyW(HKEY_CURRENT_USER, oracle__regroot);
    if (RegCreateKeyExW(HKEY_CURRENT_USER, oracle__regroot, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL)) {
        fprintf(stderr, "oracle: cannot create registry root\n");
        exit(3);
    }
    return k;
}

static inline void oracle_reg_cleanup(void)
{
    RegDeleteTreeW(HKEY_CURRENT_USER, oracle__regroot);
    RegDeleteKeyW(HKEY_CURRENT_USER, oracle__regroot);
}

/* ---- lifecycle ------------------------------------------------------- */

static inline void oracle_begin(const char *test)
{
    /* Binary stdout: no CRLF translation, identical bytes everywhere. */
    _setmode(_fileno(stdout), _O_BINARY);
    /* Never show the "insert a disk" / GP-fault dialogs: they would hang SSH runs. */
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    size_t n = wcslen(ORACLE_REG_BASE);
    wcscpy(oracle__regroot, ORACLE_REG_BASE);
    oracle__regroot[n++] = L'\\';
    for (const char *p = test; *p && n < 255; p++) /* test names are ASCII */
        oracle__regroot[n++] = (*p == '/') ? L'_' : (wchar_t)*p;
    oracle__regroot[n] = 0;
    oracle__len = 0;
    oracle__first = 1;
    oracle__raw("{", 1);
    o_u32("oracle", ORACLE_SCHEMA);
    o_str("test", test);
    oracle__raw("}", 1);
    oracle__flush();
}

static inline int oracle_end(void)
{
    oracle__len = 0;
    oracle__first = 1;
    oracle__raw("{", 1);
    o_bool("end", 1);
    o_u32("cases", oracle__ncases);
    oracle__raw("}", 1);
    oracle__flush();
    return 0;
}

#endif /* SETU_ORACLE_H */
