/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: CreateSymbolicLinkW (file and directory links, relative and
 * absolute targets, dangling links, the ALLOW_UNPRIVILEGED_CREATE flag,
 * invalid flags) and how other APIs see the link (attributes, find data
 * reparse tag, opening through the link, FILE_FLAG_OPEN_REPARSE_POINT,
 * deleting the link). Absolute target paths are never printed.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-createsymboliclinkw
 *       https://learn.microsoft.com/en-us/windows/win32/fileio/symbolic-links
 *       https://learn.microsoft.com/en-us/windows/win32/fileio/symbolic-link-effects-on-file-systems-functions
 */
#include "oracle.h"

#ifndef SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE
#define SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE 0x2
#endif

static void make_file(const wchar_t *path, const char *content)
{
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    DWORD w;
    WriteFile(h, content, (DWORD)strlen(content), &w, NULL);
    CloseHandle(h);
}

static BOOLEAN sl(const char *name, const wchar_t *link, const wchar_t *target, DWORD flags)
{
    BOOLEAN r;
    ORACLE_CALL(r, CreateSymbolicLinkW(link, target, flags));
    oracle_case(name);
    o_bool("ret", r);
    o_lasterr();
    DWORD a = GetFileAttributesW(link);
    if (a == INVALID_FILE_ATTRIBUTES) o_str("link_attrs", "missing");
    else o_hex32("link_attrs", a);
    oracle_case_end();
    return r;
}

static void find_case(const char *name, const wchar_t *path)
{
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(path, &fd);
    oracle_case(name);
    o_handle("ret", h);
    if (h != INVALID_HANDLE_VALUE) {
        o_hex32("attrs", fd.dwFileAttributes);
        o_hex32("reserved0", (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ? fd.dwReserved0 : 0);
        o_u32("size", fd.nFileSizeLow);
        FindClose(h);
    }
    oracle_case_end();
}

static void open_case(const char *name, const wchar_t *path, DWORD flags)
{
    HANDLE h;
    ORACLE_CALL(h, CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                               OPEN_EXISTING, flags, NULL));
    oracle_case(name);
    o_handle("ret", h);
    o_lasterr();
    if (h != INVALID_HANDLE_VALUE) {
        BY_HANDLE_FILE_INFORMATION bi;
        GetFileInformationByHandle(h, &bi);
        o_hex32("attrs", bi.dwFileAttributes);
        if (!(bi.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            char buf[32];
            DWORD n = 0;
            if (ReadFile(h, buf, sizeof buf, &n, NULL)) o_bytes("data", buf, n);
            else o_u32("read_error", GetLastError());
        }
        FILE_ATTRIBUTE_TAG_INFO tag;
        if (GetFileInformationByHandleEx(h, FileAttributeTagInfo, &tag, sizeof tag))
            o_hex32("reparse_tag", tag.ReparseTag);
        CloseHandle(h);
    }
    oracle_case_end();
}

int main(void)
{
    oracle_begin("kernel32/CreateSymbolicLinkW");
    wchar_t cwd[MAX_PATH], abs_target[MAX_PATH + 16];
    GetCurrentDirectoryW(MAX_PATH, cwd);
    make_file(L"target.txt", "target data");
    CreateDirectoryW(L"tdir", NULL);
    make_file(L"tdir\\inside.txt", "inside");

    /* Probe: plain call. With SeCreateSymbolicLinkPrivilege (e.g. an
     * elevated administrator) it succeeds; otherwise 1314. */
    if (!sl("file_link_relative", L"flink.txt", L"target.txt", 0)) {
        /* Without the privilege, nothing below can create links. */
        sl("file_link_unprivileged_flag", L"flink.txt", L"target.txt", SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE);
        return oracle_end();
    }
    sl("file_link_unprivileged_flag", L"flink2.txt", L"target.txt", SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE);
    swprintf(abs_target, MAX_PATH + 16, L"%ls\\target.txt", cwd);
    sl("file_link_absolute", L"flink_abs.txt", abs_target, 0);
    sl("dir_link_relative", L"dlink", L"tdir", SYMBOLIC_LINK_FLAG_DIRECTORY);
    sl("file_flag_on_dir_target", L"dlink_as_file", L"tdir", 0);
    sl("dir_flag_on_file_target", L"flink_as_dir", L"target.txt", SYMBOLIC_LINK_FLAG_DIRECTORY);
    sl("dangling_file_link", L"dangling.txt", L"missing.txt", 0);
    sl("dangling_dir_link", L"dangling_dir", L"missing_dir", SYMBOLIC_LINK_FLAG_DIRECTORY);
    sl("link_name_exists", L"flink.txt", L"target.txt", 0);
    sl("link_name_is_existing_file", L"target.txt", L"tdir", 0);
    sl("link_in_missing_dir", L"nodir\\l.txt", L"target.txt", 0);
    sl("invalid_flag_4", L"bad4.txt", L"target.txt", 4);
    sl("invalid_flag_high", L"badh.txt", L"target.txt", 0x80000000);
    sl("empty_target", L"empty_target.txt", L"", 0);
    sl("link_to_itself", L"self.txt", L"self.txt", 0);
    sl("target_dotdot_relative", L"tdir\\up.txt", L"..\\target.txt", 0);
    sl("link_to_link", L"chain.txt", L"flink.txt", 0);

    find_case("find_file_link", L"flink.txt");
    find_case("find_dir_link", L"dlink");
    find_case("find_dangling", L"dangling.txt");
    find_case("find_through_dir_link", L"dlink\\*.txt");

    open_case("open_file_link_follows", L"flink.txt", 0);
    open_case("open_file_link_reparse_point", L"flink.txt", FILE_FLAG_OPEN_REPARSE_POINT);
    open_case("open_absolute_link_follows", L"flink_abs.txt", 0);
    open_case("open_dangling", L"dangling.txt", 0);
    open_case("open_dangling_reparse_point", L"dangling.txt", FILE_FLAG_OPEN_REPARSE_POINT);
    open_case("open_dir_link_follows", L"dlink", FILE_FLAG_BACKUP_SEMANTICS);
    open_case("open_dir_link_reparse_point", L"dlink", FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT);
    open_case("open_file_through_dir_link", L"dlink\\inside.txt", 0);
    open_case("open_relative_dotdot_link", L"tdir\\up.txt", 0);
    open_case("open_chain", L"chain.txt", 0);
    open_case("open_self_loop", L"self.txt", 0);
    open_case("open_dir_link_created_as_file", L"dlink_as_file", FILE_FLAG_BACKUP_SEMANTICS);
    open_case("open_file_link_created_as_dir", L"flink_as_dir", 0);

    {
        WIN32_FILE_ATTRIBUTE_DATA fad;
        BOOL r;
        ORACLE_CALL(r, GetFileAttributesExW(L"flink.txt", GetFileExInfoStandard, &fad));
        oracle_case("attributes_ex_of_link");
        o_bool("ret", r);
        o_hex32("attrs", fad.dwFileAttributes);
        o_u32("size", fad.nFileSizeLow);
        oracle_case_end();
    }
    {
        wchar_t out[MAX_PATH];
        HANDLE h = CreateFileW(L"flink.txt", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        DWORD n = GetFinalPathNameByHandleW(h, out, MAX_PATH, VOLUME_NAME_NONE);
        oracle_case("final_path_through_link");
        o_bool("ends_with_target", n > 11 && !wcscmp(out + n - 11, L"\\target.txt"));
        oracle_case_end();
        CloseHandle(h);
    }

    /* Deleting links never touches the target. */
    {
        BOOL r;
        ORACLE_CALL(r, DeleteFileW(L"flink.txt"));
        oracle_case("delete_file_link");
        o_bool("ret", r);
        o_lasterr();
        o_file_state("target", L"target.txt");
        oracle_case_end();
        ORACLE_CALL(r, DeleteFileW(L"dlink"));
        oracle_case("deletefile_on_dir_link");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, RemoveDirectoryW(L"dlink"));
        oracle_case("rmdir_dir_link");
        o_bool("ret", r);
        o_lasterr();
        o_file_state("target_dir", L"tdir");
        o_file_state("target_inside", L"tdir\\inside.txt");
        oracle_case_end();
        ORACLE_CALL(r, DeleteFileW(L"dangling.txt"));
        oracle_case("delete_dangling");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
    }
    {
        BOOL r;
        ORACLE_CALL(r, MoveFileW(L"chain.txt", L"chain_moved.txt"));
        oracle_case("move_link");
        o_bool("ret", r);
        o_lasterr();
        o_file_state("moved", L"chain_moved.txt");
        oracle_case_end();
        ORACLE_CALL(r, CopyFileW(L"flink2.txt", L"copied.txt", FALSE));
        oracle_case("copy_link_copies_target");
        o_bool("ret", r);
        o_lasterr();
        o_file_state("copy", L"copied.txt");
        oracle_case_end();
    }

    oracle_case("final_listing");
    o_dir_listing("root", L".", 1);
    oracle_case_end();

    return oracle_end();
}
