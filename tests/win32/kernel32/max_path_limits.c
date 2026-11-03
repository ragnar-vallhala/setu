/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: where MAX_PATH (260) applies for a program that is not
 * long-path aware (no manifest entry), API by API.
 *
 * The full path length (after the current directory is prepended) decides,
 * not the length of the string passed in. The test builds names whose full
 * path is exactly N characters (cwd + "\" + a long directory + "\" + name)
 * and calls CreateFileW, GetFileAttributesW, FindFirstFileW, DeleteFileW,
 * MoveFileW, CopyFileW, CreateDirectoryW (documented limit 248) and
 * RemoveDirectoryW at lengths around the limits. Everything is created first
 * with a "\\?\" path, which has no such limit.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/fileio/maximum-file-path-limitation
 *       https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createdirectoryw
 *       https://learn.microsoft.com/en-us/windows/win32/fileio/naming-a-file
 */
#include "oracle.h"

static wchar_t cwd[MAX_PATH];
static wchar_t dir[300]; /* relative name of the long directory */

/* A relative name "dir\fff...f" (padded with 'f') whose full path is N chars. */
static const wchar_t *rel_of_len(size_t n, const wchar_t *tag)
{
    static wchar_t bufs[4][1024];
    static int i;
    wchar_t *b = bufs[i++ & 3];
    size_t base = wcslen(cwd) + 1 + wcslen(dir) + 1 + wcslen(tag);
    swprintf(b, 1024, L"%ls\\%ls", dir, tag);
    for (size_t k = base; k < n; k++) wcscat(b, L"f");
    return b;
}

static const wchar_t *V(const wchar_t *rel)
{
    static wchar_t bufs[4][1024];
    static int i;
    wchar_t *b = bufs[i++ & 3];
    swprintf(b, 1024, L"\\\\?\\%ls\\%ls", cwd, rel);
    return b;
}

static const wchar_t *A(const wchar_t *rel)
{
    static wchar_t bufs[4][1024];
    static int i;
    wchar_t *b = bufs[i++ & 3];
    swprintf(b, 1024, L"%ls\\%ls", cwd, rel);
    return b;
}

static void make_file(const wchar_t *vpath)
{
    HANDLE h = CreateFileW(vpath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
}

static void result(const char *name, BOOL ok)
{
    oracle_case(name);
    o_bool("ret", ok);
    o_lasterr();
    oracle_case_end();
}

static void per_length(size_t n)
{
    char name[64];
    const wchar_t *rel = rel_of_len(n, L"x");
    BOOL ok;
    HANDLE h;
    DWORD a;

    make_file(V(rel));

    snprintf(name, sizeof name, "open_relative_%zu", n);
    ORACLE_CALL(h, CreateFileW(rel, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL));
    result(name, h != INVALID_HANDLE_VALUE);
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);

    snprintf(name, sizeof name, "open_absolute_%zu", n);
    ORACLE_CALL(h, CreateFileW(A(rel), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL));
    result(name, h != INVALID_HANDLE_VALUE);
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);

    snprintf(name, sizeof name, "attr_%zu", n);
    ORACLE_CALL(a, GetFileAttributesW(rel));
    result(name, a != INVALID_FILE_ATTRIBUTES);

    snprintf(name, sizeof name, "find_%zu", n);
    {
        WIN32_FIND_DATAW fd;
        HANDLE f;
        ORACLE_CALL(f, FindFirstFileW(rel, &fd));
        result(name, f != INVALID_HANDLE_VALUE);
        if (f != INVALID_HANDLE_VALUE) FindClose(f);
    }

    snprintf(name, sizeof name, "copy_to_%zu", n);
    const wchar_t *rel2 = rel_of_len(n, L"y");
    ORACLE_CALL(ok, CopyFileW(L"src.txt", rel2, FALSE));
    result(name, ok);
    DeleteFileW(V(rel2));

    snprintf(name, sizeof name, "move_from_%zu", n);
    ORACLE_CALL(ok, MoveFileW(rel, L"moved.txt"));
    result(name, ok);
    if (ok) MoveFileW(L"moved.txt", V(rel));

    snprintf(name, sizeof name, "create_new_%zu", n);
    const wchar_t *rel3 = rel_of_len(n, L"z");
    ORACLE_CALL(h, CreateFileW(rel3, GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL));
    result(name, h != INVALID_HANDLE_VALUE);
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    DeleteFileW(V(rel3));

    snprintf(name, sizeof name, "delete_%zu", n);
    ORACLE_CALL(ok, DeleteFileW(rel));
    result(name, ok);
    DeleteFileW(V(rel));
}

static void dir_length(size_t n)
{
    char name[64];
    const wchar_t *rel = rel_of_len(n, L"d");
    BOOL ok;
    snprintf(name, sizeof name, "mkdir_%zu", n);
    ORACLE_CALL(ok, CreateDirectoryW(rel, NULL));
    result(name, ok);
    if (!ok) CreateDirectoryW(V(rel), NULL);
    snprintf(name, sizeof name, "dir_attr_%zu", n);
    DWORD a;
    ORACLE_CALL(a, GetFileAttributesW(rel));
    result(name, a != INVALID_FILE_ATTRIBUTES);
    snprintf(name, sizeof name, "setcd_%zu", n);
    ORACLE_CALL(ok, SetCurrentDirectoryW(rel));
    result(name, ok);
    SetCurrentDirectoryW(cwd);
    snprintf(name, sizeof name, "rmdir_%zu", n);
    ORACLE_CALL(ok, RemoveDirectoryW(rel));
    result(name, ok);
    RemoveDirectoryW(V(rel));
}

int main(void)
{
    oracle_begin("kernel32/max_path_limits");
    GetCurrentDirectoryW(MAX_PATH, cwd);
    /* A directory name that makes cwd + "\" + dir exactly 200 chars. */
    size_t dl = 200 - wcslen(cwd) - 1;
    wcscpy(dir, L"L");
    while (wcslen(dir) < dl) wcscat(dir, L"l");
    CreateDirectoryW(dir, NULL);
    make_file(L"src.txt");

    oracle_case("setup");
    o_bool("dir_created", GetFileAttributesW(dir) != INVALID_FILE_ATTRIBUTES);
    oracle_case_end();

    static const size_t lens[] = { 258, 259, 260, 261, 300 };
    for (size_t i = 0; i < sizeof lens / sizeof lens[0]; i++) per_length(lens[i]);
    static const size_t dlens[] = { 246, 247, 248, 249, 259, 260 };
    for (size_t i = 0; i < sizeof dlens / sizeof dlens[0]; i++) dir_length(dlens[i]);

    /* A long input that resolves to a short path. */
    {
        wchar_t lng[1024] = L"";
        for (int i = 0; i < 60; i++) wcscat(lng, L".\\");
        wcscat(lng, L"sub\\..\\src.txt");
        for (int i = 0; i < 30; i++) {
            wchar_t t[1024];
            swprintf(t, 1024, L"sub\\..\\%ls", lng);
            wcscpy(lng, t);
        }
        oracle_case("long_input_length");
        o_bool("over_260", wcslen(lng) > 260);
        oracle_case_end();
        HANDLE h;
        ORACLE_CALL(h, CreateFileW(lng, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL));
        result("long_input_resolves_short_open", h != INVALID_HANDLE_VALUE);
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        DWORD a;
        ORACLE_CALL(a, GetFileAttributesW(lng));
        result("long_input_resolves_short_attr", a != INVALID_FILE_ATTRIBUTES);
    }
    /* Probe: the shortest input (relative, then absolute) that fails although
     * it resolves to the short, existing "src.txt" ("./" padding). */
    for (int absolute = 0; absolute < 2; absolute++) {
        size_t first_fail_open = 0, first_fail_attr = 0;
        for (size_t len = 150; len <= 330; len++) {
            wchar_t in[1024] = L"";
            if (absolute) swprintf(in, 1024, L"%ls\\", cwd);
            size_t left = len - wcslen(in) - wcslen(L"src.txt");
            if (left & 1) {
                wcscat(in, L"sub\\..\\");
                left -= 7;
            }
            for (size_t k = 0; k < left / 2; k++) wcscat(in, L".\\");
            wcscat(in, L"src.txt");
            if (wcslen(in) != len) continue;
            HANDLE h = CreateFileW(in, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
            if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
            else if (!first_fail_open) first_fail_open = len;
            if (GetFileAttributesW(in) == INVALID_FILE_ATTRIBUTES && !first_fail_attr) first_fail_attr = len;
        }
        oracle_case(absolute ? "probe_input_len_absolute" : "probe_input_len_relative");
        o_u32("first_failing_open", (DWORD)first_fail_open);
        o_u32("first_failing_attr", (DWORD)first_fail_attr);
        oracle_case_end();
    }
    /* GetFullPathNameW itself has no MAX_PATH limit. */
    {
        wchar_t buf[1024];
        DWORD r;
        ORACLE_CALL(r, GetFullPathNameW(rel_of_len(300, L"x"), 1024, buf, NULL));
        oracle_case("fullpath_300");
        o_bool("ret_is_300", r == 300);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, GetLongPathNameW(rel_of_len(300, L"x"), buf, 1024));
        result("longpath_300_missing", r != 0);
    }
    /* Component length limit (255) without the prefix. */
    {
        wchar_t comp[300] = L"";
        for (int k = 0; k < 256; k++) wcscat(comp, L"c");
        HANDLE h;
        ORACLE_CALL(h, CreateFileW(comp, GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL));
        result("create_component_256", h != INVALID_HANDLE_VALUE);
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        DWORD a;
        ORACLE_CALL(a, GetFileAttributesW(comp));
        result("attr_component_256", a != INVALID_FILE_ATTRIBUTES);
        comp[255] = 0;
        ORACLE_CALL(h, CreateFileW(comp, GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL));
        result("create_component_255", h != INVALID_HANDLE_VALUE);
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        DeleteFileW(comp);
    }
    return oracle_end();
}
