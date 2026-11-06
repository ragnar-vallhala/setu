/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: SHFileOperationW FO_COPY and FO_MOVE without UI
 * (FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI, plus FOF_NOCONFIRMMKDIR
 * where a destination directory must be created): single files, multiple
 * sources, wildcards, whole directories, FOF_MULTIDESTFILES, FOF_FILESONLY,
 * FOF_NORECURSION, overwriting, FOF_RENAMEONCOLLISION (the generated
 * names), FOF_WANTMAPPINGHANDLE, copying a file onto itself, a directory
 * into itself, and relative paths.
 *
 * Each case runs in its own fresh directory under the working directory and
 * prints the return value, fAnyOperationsAborted, whether hNameMappings was
 * set, and a sorted recursive listing of the case directory afterwards
 * ("name:size" for files, "name/" for directories). File contents are
 * checked by size: every source file has a distinct size.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-shfileoperationw
 *       https://learn.microsoft.com/en-us/windows/win32/api/shellapi/ns-shellapi-shfileopstructw
 *       https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-shfreenamemappings
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

static void prep_dst_collide(void)
{
    make_file(L"dst\\a.txt", 9);
    make_file(L"dst\\b.txt", 10);
}

static void prep_readonly_dst(void)
{
    make_file(L"dst\\a.txt", 9);
    SetFileAttributesW(L"dst\\a.txt", FILE_ATTRIBUTE_READONLY);
}

static void prep_dst_dir_collide(void)
{
    CreateDirectoryW(L"dst\\dir", NULL);
    make_file(L"dst\\dir\\d.txt", 11);
    make_file(L"dst\\dir\\only_here.txt", 12);
}

static void prep_copy_exists(void)
{
    make_file(L"a - Copy.txt", 13);
}

int main(void)
{
    oracle_begin("shell32/fileop_copy_move");
    HMODULE sh = LoadLibraryW(L"shell32.dll");
    LOAD(sh, SHFileOperationW);
    LOAD(sh, SHFreeNameMappings);
    oracle_case("setup");
    o_bool("loaded", pSHFileOperationW != NULL);
    oracle_case_end();
    if (!pSHFileOperationW) return oracle_end();
    GetCurrentDirectoryW(MAX_PATH, root);

    /* ---- FO_COPY ---- */
    op_case("copy_file_new_name", FO_COPY, L"a.txt", L"z.txt", QUIET, NULL);
    op_case("copy_file_into_dir", FO_COPY, L"a.txt", L"dst", QUIET, NULL);
    op_case("copy_file_into_dir_trailing_backslash", FO_COPY, L"a.txt", L"dst\\", QUIET, NULL);
    op_case("copy_file_to_missing_dir_path", FO_COPY, L"a.txt", L"newdir\\z.txt", QUIET, NULL);
    op_case("copy_multi_into_dir", FO_COPY, L"a.txt|b.txt|c.dat", L"dst", QUIET, NULL);
    op_case("copy_multi_to_missing_dir", FO_COPY, L"a.txt|b.txt", L"newdir", QUIET, NULL);
    op_case("copy_wildcard", FO_COPY, L"*.txt", L"dst", QUIET, NULL);
    op_case("copy_wildcard_question", FO_COPY, L"?.dat", L"dst", QUIET, NULL);
    op_case("copy_star_dot_star", FO_COPY, L"dir\\*.*", L"dst", QUIET, NULL);
    op_case("copy_wildcard_filesonly", FO_COPY, L"dir\\*", L"dst", QUIET | FOF_FILESONLY, NULL);
    op_case("copy_wildcard_no_match", FO_COPY, L"*.none", L"dst", QUIET, NULL);
    op_case("copy_dir_recursive", FO_COPY, L"dir", L"dst", QUIET, NULL);
    op_case("copy_dir_new_name", FO_COPY, L"dir", L"dir2", QUIET, NULL);
    op_case("copy_dir_norecursion", FO_COPY, L"dir", L"dst", QUIET | FOF_NORECURSION, NULL);
    op_case("copy_multidestfiles", FO_COPY, L"a.txt|b.txt", L"x1.txt|x2.txt", QUIET | FOF_MULTIDESTFILES, NULL);
    op_case("copy_multidestfiles_into_dirs", FO_COPY, L"a.txt|b.txt", L"dst|dir", QUIET | FOF_MULTIDESTFILES, NULL);
    op_case("copy_overwrite", FO_COPY, L"a.txt|b.txt", L"dst", QUIET, prep_dst_collide);
    op_case("copy_overwrite_readonly", FO_COPY, L"a.txt", L"dst", QUIET, prep_readonly_dst);
    op_case("copy_dir_merge", FO_COPY, L"dir", L"dst", QUIET, prep_dst_dir_collide);
    op_case("copy_rename_on_collision", FO_COPY, L"a.txt|b.txt", L"dst", QUIET | FOF_RENAMEONCOLLISION, prep_dst_collide);
    op_case("copy_rename_on_collision_mappings", FO_COPY, L"a.txt", L"dst", QUIET | FOF_RENAMEONCOLLISION | FOF_WANTMAPPINGHANDLE, prep_dst_collide);
    op_case("copy_rename_on_collision_dir", FO_COPY, L"dir", L"dst", QUIET | FOF_RENAMEONCOLLISION, prep_dst_dir_collide);
    op_case("copy_onto_itself", FO_COPY, L"a.txt", L"a.txt", QUIET, NULL);
    op_case("copy_onto_itself_rename", FO_COPY, L"a.txt", L"a.txt", QUIET | FOF_RENAMEONCOLLISION, NULL);
    op_case("copy_onto_itself_rename_twice_exists", FO_COPY, L"a.txt", L"a.txt", QUIET | FOF_RENAMEONCOLLISION, prep_copy_exists);
    op_case("copy_into_own_dir", FO_COPY, L"a.txt", L"~.", QUIET | FOF_RENAMEONCOLLISION, NULL);
    op_case("copy_dir_into_itself", FO_COPY, L"dir", L"dir\\sub", QUIET, NULL);
    op_case("copy_relative_paths", FO_COPY, L"~a.txt|~b.txt", L"~dst", QUIET, NULL);
    op_case("copy_mappings_no_collision", FO_COPY, L"a.txt", L"dst", QUIET | FOF_WANTMAPPINGHANDLE, NULL);

    /* ---- FO_MOVE ---- */
    op_case("move_file_new_name", FO_MOVE, L"a.txt", L"z.txt", QUIET, NULL);
    op_case("move_file_into_dir", FO_MOVE, L"a.txt", L"dst", QUIET, NULL);
    op_case("move_multi_into_dir", FO_MOVE, L"a.txt|b.txt|c.dat", L"dst", QUIET, NULL);
    op_case("move_wildcard", FO_MOVE, L"*.txt", L"dst", QUIET, NULL);
    op_case("move_dir", FO_MOVE, L"dir", L"dst", QUIET, NULL);
    op_case("move_dir_new_name", FO_MOVE, L"dir", L"dir2", QUIET, NULL);
    op_case("move_to_missing_dir", FO_MOVE, L"a.txt|b.txt", L"newdir", QUIET, NULL);
    op_case("move_overwrite", FO_MOVE, L"a.txt|b.txt", L"dst", QUIET, prep_dst_collide);
    op_case("move_rename_on_collision", FO_MOVE, L"a.txt", L"dst", QUIET | FOF_RENAMEONCOLLISION, prep_dst_collide);
    op_case("move_dir_merge", FO_MOVE, L"dir", L"dst", QUIET, prep_dst_dir_collide);
    op_case("move_multidestfiles", FO_MOVE, L"a.txt|b.txt", L"x1.txt|x2.txt", QUIET | FOF_MULTIDESTFILES, NULL);
    op_case("move_onto_itself", FO_MOVE, L"a.txt", L"a.txt", QUIET, NULL);
    op_case("move_dir_into_itself", FO_MOVE, L"dir", L"dir\\sub", QUIET, NULL);
    op_case("move_relative_paths", FO_MOVE, L"~a.txt", L"~dst", QUIET, NULL);
    return oracle_end();
}
