/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: key name length limits. The docs give 255 characters per key
 * name component and 512 levels of nesting; this probes the boundaries for
 * one component, for the whole path in one call, for depth, and for the
 * lengths that RegEnumKeyExW / RegQueryInfoKeyW report for long names.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/sysinfo/registry-element-size-limits
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regcreatekeyexw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regenumkeyexw
 */
#include "oracle.h"

static HKEY root;
static wchar_t big[70000];

static LONG create(HKEY parent, const wchar_t *sub)
{
    HKEY k = NULL;
    LONG st = RegCreateKeyExW(parent, sub, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL);
    if (!st) RegCloseKey(k);
    return st;
}

static void component_case(const char *name, size_t len)
{
    wmemset(big, L'n', len);
    big[len] = 0;
    LONG st;
    HKEY k = NULL;
    ORACLE_CALL(st, RegCreateKeyExW(root, big, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL));
    oracle_case(name);
    o_status("ret", st);
    o_lasterr();
    if (!st) {
        HKEY o = NULL;
        o_status("reopen", RegOpenKeyExW(root, big, 0, KEY_READ, &o));
        if (o) RegCloseKey(o);
        DWORD maxsub = 0;
        RegQueryInfoKeyW(root, NULL, NULL, NULL, NULL, &maxsub, NULL, NULL, NULL, NULL, NULL, NULL);
        o_u32("max_subkey_len", maxsub);
        static wchar_t got[1024];
        DWORD n = 1024;
        LONG e = RegEnumKeyExW(root, 0, got, &n, NULL, NULL, NULL, NULL);
        o_status("enum", e);
        o_u32("enum_len", n);
        n = 256;
        o_status("enum_256_buffer", RegEnumKeyExW(root, 0, got, &n, NULL, NULL, NULL, NULL));
        RegCloseKey(k);
        o_status("delete", RegDeleteKeyW(root, big));
    }
    oracle_case_end();
}

static void open_long_case(const char *name, size_t len)
{
    wmemset(big, L'q', len);
    big[len] = 0;
    LONG st;
    HKEY k = NULL;
    ORACLE_CALL(st, RegOpenKeyExW(root, big, 0, KEY_READ, &k));
    oracle_case(name);
    o_status("ret", st);
    o_lasterr();
    oracle_case_end();
    if (!st) RegCloseKey(k);
}

/* A path of `count` components of `clen` characters each, in one call. */
static void path_case(const char *name, int count, int clen)
{
    size_t p = 0;
    for (int i = 0; i < count; i++) {
        if (i) big[p++] = L'\\';
        for (int j = 0; j < clen; j++) big[p++] = (wchar_t)(L'a' + (i % 26));
    }
    big[p] = 0;
    LONG st;
    HKEY k = NULL;
    ORACLE_CALL(st, RegCreateKeyExW(root, big, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL));
    oracle_case(name);
    o_u32("path_chars", (DWORD)p);
    o_status("ret", st);
    o_lasterr();
    if (!st) RegCloseKey(k);
    {   /* How many levels exist afterwards (a failed call may leave a partial chain). */
        HKEY cur;
        int depth = 0;
        RegOpenKeyExW(root, NULL, 0, KEY_READ, &cur);
        for (;;) {
            wchar_t nm[300];
            DWORD n = 300;
            HKEY next;
            if (RegEnumKeyExW(cur, 0, nm, &n, NULL, NULL, NULL, NULL)) break;
            if (RegOpenKeyExW(cur, nm, 0, KEY_READ, &next)) break;
            RegCloseKey(cur);
            cur = next;
            depth++;
        }
        RegCloseKey(cur);
        o_u32("levels_created", depth);
    }
    oracle_case_end();
    RegDeleteTreeW(root, NULL);
}

/* Creates a chain one level per call; reports the first depth that fails. */
static void depth_case(const char *name, int max)
{
    HKEY cur, next;
    int depth = 0;
    LONG st = 0;
    RegOpenKeyExW(root, NULL, 0, KEY_ALL_ACCESS, &cur);
    while (depth < max) {
        st = RegCreateKeyExW(cur, L"d", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &next, NULL);
        if (st) break;
        RegCloseKey(cur);
        cur = next;
        depth++;
    }
    RegCloseKey(cur);
    oracle_case(name);
    o_u32("depth_reached", depth);
    o_status("failing_ret", st);
    oracle_case_end();
    RegDeleteTreeW(root, NULL);
}

int main(void)
{
    oracle_begin("advapi32/RegCreateKeyEx_length");
    root = oracle_reg_root();

    component_case("component_1", 1);
    component_case("component_254", 254);
    component_case("component_255", 255);
    component_case("component_256", 256);
    component_case("component_257", 257);
    component_case("component_260", 260);
    component_case("component_280", 280);
    component_case("component_297", 297);
    component_case("component_298", 298);
    component_case("component_299", 299);
    component_case("component_300", 300);
    component_case("component_512", 512);
    component_case("component_32767", 32767);
    component_case("component_32768", 32768);
    component_case("component_65535", 65535);

    open_long_case("open_missing_255", 255);
    open_long_case("open_missing_256", 256);
    open_long_case("open_missing_300", 300);
    open_long_case("open_missing_32768", 32768);

    {   /* A 255-char component followed by a 256-char one. */
        wmemset(big, L'x', 255);
        big[255] = L'\\';
        wmemset(big + 256, L'y', 256);
        big[512] = 0;
        LONG st;
        ORACLE_CALL(st, create(root, big));
        oracle_case("second_component_too_long_256");
        o_status("ret", st);
        o_lasterr();
        oracle_case_end();
        RegDeleteTreeW(root, NULL);
    }

    path_case("path_10x255", 10, 255);
    path_case("path_100x255", 100, 255);
    path_case("path_127x255", 127, 255);
    path_case("path_128x255", 128, 255);
    path_case("path_130x255", 130, 255);
    path_case("path_513x1", 513, 1);
    path_case("path_600x1", 600, 1);

    depth_case("depth_one_per_call", 600);

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
