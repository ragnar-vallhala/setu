/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: DeleteFileW and RemoveDirectoryW edge cases not covered by
 * directory_ops: trailing separators and dots, wildcards, "." and "..",
 * \\?\ paths, attributes, open handles with various share modes, delete
 * pending state.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-deletefilew
 *       https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-removedirectoryw
 *       https://learn.microsoft.com/en-us/windows/win32/fileio/naming-a-file
 */
#include "oracle.h"

static void make_file(const wchar_t *path, const char *content)
{
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    DWORD w;
    WriteFile(h, content, (DWORD)strlen(content), &w, NULL);
    CloseHandle(h);
}

static void del(const char *name, const wchar_t *path, const wchar_t *state)
{
    BOOL r;
    ORACLE_CALL(r, DeleteFileW(path));
    oracle_case(name);
    o_bool("ret", r);
    o_lasterr();
    if (state) o_file_state("file", state);
    oracle_case_end();
}

static void rmd(const char *name, const wchar_t *path, const wchar_t *state)
{
    BOOL r;
    ORACLE_CALL(r, RemoveDirectoryW(path));
    oracle_case(name);
    o_bool("ret", r);
    o_lasterr();
    if (state) o_file_state("dir", state);
    oracle_case_end();
}

int main(void)
{
    oracle_begin("kernel32/delete_remove_edge");
    wchar_t cwd[MAX_PATH], full[MAX_PATH + 8];
    GetCurrentDirectoryW(MAX_PATH, cwd);
    make_file(L"keep", "k"); /* the working directory is never empty */

    /* DeleteFileW name forms. */
    make_file(L"a.txt", "a");
    del("delete_trailing_backslash", L"a.txt\\", L"a.txt");
    del("delete_trailing_slash", L"a.txt/", L"a.txt");
    del("delete_trailing_dot", L"a.txt.", L"a.txt");
    make_file(L"a.txt", "a");
    del("delete_trailing_space", L"a.txt ", L"a.txt");
    make_file(L"a.txt", "a");
    del("delete_wildcard_star", L"*.txt", L"a.txt");
    del("delete_wildcard_question", L"?.txt", L"a.txt");
    del("delete_forward_slash_path", L"./a.txt", L"a.txt");
    make_file(L"a.txt", "a");
    swprintf(full, MAX_PATH + 8, L"\\\\?\\%ls\\a.txt", cwd);
    del("delete_long_prefix", full, L"a.txt");
    make_file(L"a.txt", "a");
    swprintf(full, MAX_PATH + 8, L"\\\\?\\%ls\\a.txt.", cwd);
    del("delete_long_prefix_trailing_dot", full, L"a.txt");
    del("delete_dot", L".", NULL);
    del("delete_dotdot", L"..", NULL);
    del("delete_empty", L"", NULL);
    del("delete_device_nul", L"nul", NULL);
    del("delete_invalid_char", L"a<b", NULL);
    del("delete_case_insensitive", L"A.TXT", L"a.txt");

    /* Attributes. */
    make_file(L"h.txt", "h");
    SetFileAttributesW(L"h.txt", FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
    del("delete_hidden_system", L"h.txt", L"h.txt");
    make_file(L"ro.txt", "r");
    SetFileAttributesW(L"ro.txt", FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN);
    del("delete_readonly_hidden", L"ro.txt", L"ro.txt");
    SetFileAttributesW(L"ro.txt", FILE_ATTRIBUTE_NORMAL);
    del("delete_after_clearing_readonly", L"ro.txt", L"ro.txt");

    /* Open handles. */
    make_file(L"o.txt", "o");
    HANDLE h = CreateFileW(L"o.txt", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    del("delete_open_share_rw", L"o.txt", L"o.txt");
    CloseHandle(h);
    h = CreateFileW(L"o.txt", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    del("delete_open_share_delete", L"o.txt", L"o.txt");
    del("delete_again_while_pending", L"o.txt", L"o.txt");
    {
        HANDLE h2;
        ORACLE_CALL(h2, CreateFileW(L"o.txt", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    NULL, OPEN_EXISTING, 0, NULL));
        oracle_case("open_while_delete_pending");
        o_handle("ret", h2);
        o_lasterr();
        oracle_case_end();
        if (h2 != INVALID_HANDLE_VALUE) CloseHandle(h2);
        ORACLE_CALL(h2, CreateFileW(L"o.txt", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL));
        oracle_case("create_while_delete_pending");
        o_handle("ret", h2);
        o_lasterr();
        oracle_case_end();
        if (h2 != INVALID_HANDLE_VALUE) CloseHandle(h2);
    }
    CloseHandle(h);
    oracle_case("after_pending_close");
    o_file_state("file", L"o.txt");
    oracle_case_end();

    make_file(L"m.txt", "m");
    h = CreateFileW(L"m.txt", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    HANDLE map = CreateFileMappingW(h, NULL, PAGE_READONLY, 0, 0, NULL);
    void *view = MapViewOfFile(map, FILE_MAP_READ, 0, 0, 0);
    CloseHandle(map);
    CloseHandle(h);
    del("delete_mapped_view", L"m.txt", L"m.txt");
    UnmapViewOfFile(view);
    del("delete_after_unmap", L"m.txt", L"m.txt");

    /* RemoveDirectoryW name forms. */
    CreateDirectoryW(L"d", NULL);
    rmd("rmdir_trailing_backslash", L"d\\", L"d");
    CreateDirectoryW(L"d", NULL);
    rmd("rmdir_trailing_slash", L"d/", L"d");
    CreateDirectoryW(L"d", NULL);
    rmd("rmdir_trailing_dot", L"d.", L"d");
    rmd("rmdir_wildcard", L"d*", L"d");
    rmd("rmdir_dot", L".", NULL);
    rmd("rmdir_dotdot", L"..", NULL);
    rmd("rmdir_empty", L"", NULL);
    rmd("rmdir_case_insensitive", L"D", L"d");
    CreateDirectoryW(L"d", NULL);
    swprintf(full, MAX_PATH + 8, L"\\\\?\\%ls\\d", cwd);
    rmd("rmdir_long_prefix", full, L"d");
    CreateDirectoryW(L"d", NULL);
    CreateDirectoryW(L"d\\sub", NULL);
    rmd("rmdir_dotdot_component", L"d\\sub\\..\\sub", L"d\\sub");
    rmd("rmdir_parent_nonempty", L"d", L"d");

    /* Attributes on directories. */
    CreateDirectoryW(L"rod", NULL);
    SetFileAttributesW(L"rod", FILE_ATTRIBUTE_READONLY);
    rmd("rmdir_readonly", L"rod", L"rod");
    SetFileAttributesW(L"rod", FILE_ATTRIBUTE_NORMAL);
    rmd("rmdir_after_clearing_readonly", L"rod", L"rod");
    CreateDirectoryW(L"hd", NULL);
    SetFileAttributesW(L"hd", FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
    rmd("rmdir_hidden_system", L"hd", L"hd");

    /* Directory with only a hidden file inside. */
    CreateDirectoryW(L"hf", NULL);
    make_file(L"hf\\x", "x");
    SetFileAttributesW(L"hf\\x", FILE_ATTRIBUTE_HIDDEN);
    rmd("rmdir_with_hidden_child", L"hf", L"hf");

    /* Open directory handles. */
    CreateDirectoryW(L"od", NULL);
    h = CreateFileW(L"od", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                    FILE_FLAG_BACKUP_SEMANTICS, NULL);
    rmd("rmdir_open_no_share_delete", L"od", L"od");
    CloseHandle(h);
    h = CreateFileW(L"od", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                    FILE_FLAG_BACKUP_SEMANTICS, NULL);
    rmd("rmdir_open_share_delete", L"od", L"od");
    {
        BOOL r;
        ORACLE_CALL(r, CreateDirectoryW(L"od\\new", NULL));
        oracle_case("mkdir_inside_pending_dir");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, CreateDirectoryW(L"od", NULL));
        oracle_case("mkdir_over_pending_dir");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
    }
    CloseHandle(h);
    oracle_case("after_pending_dir_close");
    o_file_state("dir", L"od");
    oracle_case_end();

    /* Files and dirs mixed. */
    make_file(L"f", "f");
    rmd("rmdir_on_file_trailing_backslash", L"f\\", L"f");
    CreateDirectoryW(L"e", NULL);
    del("delete_on_dir_trailing_backslash", L"e\\", L"e");

    oracle_case("final_listing");
    o_dir_listing("root", L".", 1);
    oracle_case_end();

    return oracle_end();
}
