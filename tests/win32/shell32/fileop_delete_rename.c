/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: SHFileOperationW FO_DELETE and FO_RENAME without UI
 * (FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI), and the error codes
 * SHFileOperationW returns for bad input: missing sources, wildcards that
 * match nothing, read-only and in-use files, renaming onto an existing
 * name, renaming with a wildcard or into another directory, an empty or
 * invalid operation, a missing pTo, sources on a missing drive, and
 * FOF_FILESONLY / FOF_NORECURSION with deletes.
 *
 * Each case runs in its own fresh directory under the working directory and
 * prints the return value, fAnyOperationsAborted and a sorted recursive
 * listing of the case directory afterwards ("name:size" for files,
 * "name/" for directories). Deletes here never use FOF_ALLOWUNDO (the
 * Recycle Bin is covered by shell32/recycle_bin).
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-shfileoperationw
 *       https://learn.microsoft.com/en-us/windows/win32/api/shellapi/ns-shellapi-shfileopstructw
 */
#include "oracle.h"
#include <shellapi.h>

static int(WINAPI *pSHFileOperationW)(SHFILEOPSTRUCTW *);
static void(WINAPI *pSHFreeNameMappings)(HANDLE);

#define LOAD(m, f) (*(FARPROC *)&p##f = GetProcAddress(m, #f))
#define QUIET (FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI | FOF_NOCONFIRMMKDIR)

static wchar_t root[MAX_PATH];
static int case_no;

/* ---- tree helpers ---- */
static void make_file(const wchar_t *name, DWORD size)
{
    HANDLE h = CreateFileW(name, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    static char b[4096];
    DWORD done;
    memset(b, 'q', sizeof b);
    if (h == INVALID_HANDLE_VALUE) return;
    WriteFile(h, b, size, &done, NULL);
    CloseHandle(h);
}

static int wcmp(const void *a, const void *b) { return _wcsicmp(*(wchar_t *const *)a, *(wchar_t *const *)b); }

static void collect(const wchar_t *dir, const wchar_t *rel, wchar_t **out, int *n)
{
    wchar_t pat[MAX_PATH];
    WIN32_FIND_DATAW fd;
    swprintf(pat, MAX_PATH, L"%ls\\*", dir);
    HANDLE h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        if (*n >= 255) break;
        wchar_t r[MAX_PATH], full[MAX_PATH];
        swprintf(r, MAX_PATH, L"%ls%ls%ls", rel, rel[0] ? L"\\" : L"", fd.cFileName);
        swprintf(full, MAX_PATH, L"%ls\\%ls", dir, fd.cFileName);
        out[*n] = malloc(sizeof(wchar_t) * (MAX_PATH + 16));
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            swprintf(out[(*n)++], MAX_PATH + 16, L"%ls/", r);
            collect(full, r, out, n);
        } else {
            swprintf(out[(*n)++], MAX_PATH + 16, L"%ls:%lu", r, (unsigned long)fd.nFileSizeLow);
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

static void o_tree(const char *k, const wchar_t *dir)
{
    wchar_t *items[256];
    int n = 0;
    static wchar_t buf[16384];
    collect(dir, L"", items, &n);
    qsort(items, n, sizeof items[0], wcmp);
    buf[0] = 0;
    for (int i = 0; i < n; i++) {
        if (i) wcscat(buf, L",");
        if (wcslen(buf) + wcslen(items[i]) < 16000) wcscat(buf, items[i]);
        free(items[i]);
    }
    o_wstr(k, buf);
}

/* A fresh case directory with a standard source tree:
 *   a.txt:1 b.txt:2 c.dat:3 dir/ dir/d.txt:4 dir/sub/ dir/sub/e.txt:5 dst/ */
static void fresh(wchar_t *dir)
{
    wchar_t p[MAX_PATH];
    swprintf(dir, MAX_PATH, L"%ls\\c%02d", root, ++case_no);
    CreateDirectoryW(dir, NULL);
    SetCurrentDirectoryW(dir);
    make_file(L"a.txt", 1);
    make_file(L"b.txt", 2);
    make_file(L"c.dat", 3);
    CreateDirectoryW(L"dir", NULL);
    make_file(L"dir\\d.txt", 4);
    CreateDirectoryW(L"dir\\sub", NULL);
    make_file(L"dir\\sub\\e.txt", 5);
    CreateDirectoryW(L"dst", NULL);
    (void)p;
}

/* Build a double-null-terminated list from '|'-separated names, each made
 * absolute under the case directory unless it starts with '~' (kept as is,
 * without the '~'). */
static void list(wchar_t *out, const wchar_t *dir, const wchar_t *names)
{
    wchar_t *o = out;
    const wchar_t *s = names;
    while (*s) {
        const wchar_t *e = wcschr(s, L'|');
        size_t len = e ? (size_t)(e - s) : wcslen(s);
        if (*s == L'~') {
            memcpy(o, s + 1, (len - 1) * sizeof(wchar_t));
            o += len - 1;
        } else {
            o += swprintf(o, MAX_PATH, L"%ls\\", dir);
            memcpy(o, s, len * sizeof(wchar_t));
            o += len;
        }
        *o++ = 0;
        s += len;
        if (*s == L'|') s++;
    }
    *o = 0;
}

static void op_case(const char *name, UINT func, const wchar_t *from, const wchar_t *to, FILEOP_FLAGS flags,
                    void (*prep)(void))
{
    wchar_t dir[MAX_PATH];
    static wchar_t f[8192], t[8192];
    fresh(dir);
    if (prep) prep();
    list(f, dir, from);
    if (to) list(t, dir, to);
    SHFILEOPSTRUCTW op;
    memset(&op, 0, sizeof op);
    op.wFunc = func;
    op.pFrom = f;
    op.pTo = to ? t : NULL;
    op.fFlags = flags;
    op.fAnyOperationsAborted = 77;
    int r;
    ORACLE_CALL(r, pSHFileOperationW(&op));
    oracle_case(name);
    o_hex32("ret", (DWORD)r);
    o_lasterr();
    o_u32("aborted", (DWORD)op.fAnyOperationsAborted);
    o_bool("mappings", op.hNameMappings != NULL);
    if (op.hNameMappings) {
        /* SHNAMEMAPPING array behind a documented-opaque handle: count only via
         * the DSA layout is undocumented, so just free it. */
        if (pSHFreeNameMappings) pSHFreeNameMappings(op.hNameMappings);
    }
    o_tree("tree", dir);
    oracle_case_end();
    SetCurrentDirectoryW(root);
}

static HANDLE held;

static void prep_readonly(void) { SetFileAttributesW(L"a.txt", FILE_ATTRIBUTE_READONLY); }
static void prep_readonly_in_dir(void) { SetFileAttributesW(L"dir\\d.txt", FILE_ATTRIBUTE_READONLY); }
static void prep_hidden_system(void) { SetFileAttributesW(L"a.txt", FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM); }
static void prep_in_use(void)
{
    held = CreateFileW(L"a.txt", GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
}
static void prep_in_use_in_dir(void)
{
    held = CreateFileW(L"dir\\d.txt", GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
}
static void release_held(void)
{
    if (held && held != INVALID_HANDLE_VALUE) CloseHandle(held);
    held = NULL;
}

static void raw_case(const char *name, UINT func, const wchar_t *from, const wchar_t *to, FILEOP_FLAGS flags)
{
    wchar_t dir[MAX_PATH];
    fresh(dir);
    SHFILEOPSTRUCTW op;
    memset(&op, 0, sizeof op);
    op.wFunc = func;
    op.pFrom = from;
    op.pTo = to;
    op.fFlags = flags;
    int r;
    ORACLE_CALL(r, pSHFileOperationW(&op));
    oracle_case(name);
    o_hex32("ret", (DWORD)r);
    o_lasterr();
    o_u32("aborted", (DWORD)op.fAnyOperationsAborted);
    o_tree("tree", dir);
    oracle_case_end();
    SetCurrentDirectoryW(root);
}

int main(void)
{
    oracle_begin("shell32/fileop_delete_rename");
    HMODULE sh = LoadLibraryW(L"shell32.dll");
    LOAD(sh, SHFileOperationW);
    LOAD(sh, SHFreeNameMappings);
    oracle_case("setup");
    o_bool("loaded", pSHFileOperationW != NULL);
    oracle_case_end();
    if (!pSHFileOperationW) return oracle_end();
    GetCurrentDirectoryW(MAX_PATH, root);

    /* ---- FO_DELETE ---- */
    op_case("delete_file", FO_DELETE, L"a.txt", NULL, QUIET, NULL);
    op_case("delete_multi", FO_DELETE, L"a.txt|b.txt|c.dat", NULL, QUIET, NULL);
    op_case("delete_wildcard", FO_DELETE, L"*.txt", NULL, QUIET, NULL);
    op_case("delete_wildcard_in_dir", FO_DELETE, L"dir\\*", NULL, QUIET, NULL);
    op_case("delete_wildcard_filesonly", FO_DELETE, L"dir\\*", NULL, QUIET | FOF_FILESONLY, NULL);
    op_case("delete_dir", FO_DELETE, L"dir", NULL, QUIET, NULL);
    op_case("delete_dir_norecursion", FO_DELETE, L"dir", NULL, QUIET | FOF_NORECURSION, NULL);
    op_case("delete_dir_trailing_backslash", FO_DELETE, L"dir\\", NULL, QUIET, NULL);
    op_case("delete_empty_dir", FO_DELETE, L"dst", NULL, QUIET, NULL);
    op_case("delete_readonly", FO_DELETE, L"a.txt", NULL, QUIET, prep_readonly);
    op_case("delete_dir_with_readonly", FO_DELETE, L"dir", NULL, QUIET, prep_readonly_in_dir);
    op_case("delete_hidden_system", FO_DELETE, L"a.txt", NULL, QUIET, prep_hidden_system);
    op_case("delete_in_use", FO_DELETE, L"a.txt", NULL, QUIET, prep_in_use);
    release_held();
    op_case("delete_dir_in_use", FO_DELETE, L"dir", NULL, QUIET, prep_in_use_in_dir);
    release_held();
    op_case("delete_missing", FO_DELETE, L"nothing.txt", NULL, QUIET, NULL);
    op_case("delete_missing_and_present", FO_DELETE, L"nothing.txt|a.txt", NULL, QUIET, NULL);
    op_case("delete_present_and_missing", FO_DELETE, L"a.txt|nothing.txt", NULL, QUIET, NULL);
    op_case("delete_wildcard_no_match", FO_DELETE, L"*.none", NULL, QUIET, NULL);
    op_case("delete_missing_dir_path", FO_DELETE, L"nodir\\a.txt", NULL, QUIET, NULL);
    op_case("delete_relative", FO_DELETE, L"~a.txt", NULL, QUIET, NULL);
    op_case("delete_ignores_pto", FO_DELETE, L"a.txt", L"dst", QUIET, NULL);

    /* ---- FO_RENAME ---- */
    op_case("rename_file", FO_RENAME, L"a.txt", L"z.txt", QUIET, NULL);
    op_case("rename_dir", FO_RENAME, L"dir", L"dir2", QUIET, NULL);
    op_case("rename_case_only", FO_RENAME, L"a.txt", L"A.TXT", QUIET, NULL);
    op_case("rename_onto_existing", FO_RENAME, L"a.txt", L"b.txt", QUIET, NULL);
    op_case("rename_onto_existing_dir", FO_RENAME, L"a.txt", L"dst", QUIET, NULL);
    op_case("rename_into_other_dir", FO_RENAME, L"a.txt", L"dst\\a.txt", QUIET, NULL);
    op_case("rename_into_missing_dir", FO_RENAME, L"a.txt", L"nodir\\a.txt", QUIET, NULL);
    op_case("rename_wildcard_source", FO_RENAME, L"*.txt", L"z.txt", QUIET, NULL);
    op_case("rename_wildcard_dest", FO_RENAME, L"a.txt", L"*.bak", QUIET, NULL);
    op_case("rename_multi_sources", FO_RENAME, L"a.txt|b.txt", L"z.txt", QUIET, NULL);
    op_case("rename_multidestfiles", FO_RENAME, L"a.txt|b.txt", L"y.txt|z.txt", QUIET | FOF_MULTIDESTFILES, NULL);
    op_case("rename_missing", FO_RENAME, L"nothing.txt", L"z.txt", QUIET, NULL);
    op_case("rename_onto_itself", FO_RENAME, L"a.txt", L"a.txt", QUIET, NULL);
    op_case("rename_rename_on_collision", FO_RENAME, L"a.txt", L"b.txt", QUIET | FOF_RENAMEONCOLLISION, NULL);
    op_case("rename_relative_dest", FO_RENAME, L"a.txt", L"~z.txt", QUIET, NULL);
    op_case("rename_in_use", FO_RENAME, L"a.txt", L"z.txt", QUIET, prep_in_use);
    release_held();
    op_case("rename_readonly", FO_RENAME, L"a.txt", L"z.txt", QUIET, prep_readonly);
    op_case("rename_invalid_char", FO_RENAME, L"a.txt", L"bad|name.txt", QUIET, NULL);

    /* ---- bad input (pFrom/pTo passed as is) ---- */
    raw_case("err_empty_from", FO_DELETE, L"\0", NULL, QUIET);
    raw_case("err_func_zero", 0, L"a.txt\0", NULL, QUIET);
    raw_case("err_func_bogus", 9, L"a.txt\0", NULL, QUIET);
    raw_case("err_copy_null_to", FO_COPY, L"a.txt\0", NULL, QUIET);
    raw_case("err_copy_empty_to", FO_COPY, L"a.txt\0", L"\0", QUIET);
    raw_case("err_move_null_to", FO_MOVE, L"a.txt\0", NULL, QUIET);
    raw_case("err_rename_null_to", FO_RENAME, L"a.txt\0", NULL, QUIET);
    raw_case("err_copy_missing_source", FO_COPY, L"nothing.txt\0", L"dst\0", QUIET);
    raw_case("err_move_missing_source", FO_MOVE, L"nothing.txt\0", L"dst\0", QUIET);
    raw_case("err_copy_missing_drive", FO_COPY, L"Q:\\setu\\x.txt\0", L"dst\0", QUIET);
    raw_case("err_copy_to_missing_drive", FO_COPY, L"a.txt\0", L"Q:\\setu\\\0", QUIET);
    raw_case("err_delete_missing_drive", FO_DELETE, L"Q:\\setu\\x.txt\0", NULL, QUIET);
    raw_case("err_delete_drive_root_wildcard_missing", FO_DELETE, L"Q:\\*\0", NULL, QUIET);
    raw_case("err_copy_bad_name", FO_COPY, L"a.txt\0", L"bad|name.txt\0", QUIET);
    raw_case("err_delete_bad_name", FO_DELETE, L"bad|name.txt\0", NULL, QUIET);
    raw_case("err_delete_dot", FO_DELETE, L".\\nothing\\..\\nothing.txt\0", NULL, QUIET);
    return oracle_end();
}
