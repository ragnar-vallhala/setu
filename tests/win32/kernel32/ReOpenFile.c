/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: ReOpenFile access/share/flag combinations against the
 * original handle's access and sharing, on files, directories and
 * non-file handles.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-reopenfile
 */
#include "oracle.h"

static void make_file(const wchar_t *path, const char *content)
{
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    DWORD w;
    WriteFile(h, content, (DWORD)strlen(content), &w, NULL);
    CloseHandle(h);
}

static HANDLE ro(const char *name, HANDLE orig, DWORD access, DWORD share, DWORD flags)
{
    HANDLE h;
    ORACLE_CALL(h, ReOpenFile(orig, access, share, flags));
    oracle_case(name);
    o_handle("ret", h);
    o_lasterr();
    if (h != INVALID_HANDLE_VALUE) {
        LARGE_INTEGER pos = {{0}}, zero = {{0}};
        SetFilePointerEx(h, zero, &pos, FILE_CURRENT);
        o_i64("position", pos.QuadPart);
        char buf[16];
        DWORD n = 0;
        if ((access & GENERIC_READ) && !(flags & FILE_FLAG_OVERLAPPED)) {
            if (ReadFile(h, buf, sizeof buf, &n, NULL)) o_bytes("data", buf, n);
            else o_u32("read_error", GetLastError());
        }
    }
    oracle_case_end();
    return h;
}

static void ro_close(const char *name, HANDLE orig, DWORD access, DWORD share, DWORD flags)
{
    HANDLE h = ro(name, orig, access, share, flags);
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
}

int main(void)
{
    oracle_begin("kernel32/ReOpenFile");
    const DWORD SH = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
    HANDLE h;

    make_file(L"f.txt", "0123456789");

    h = CreateFileW(L"f.txt", GENERIC_READ, SH, NULL, OPEN_EXISTING, 0, NULL);
    SetFilePointer(h, 4, NULL, FILE_BEGIN);
    ro_close("same_access_read", h, GENERIC_READ, SH, 0);
    ro_close("upgrade_to_write", h, GENERIC_READ | GENERIC_WRITE, SH, 0);
    ro_close("downgrade_no_access", h, 0, SH, 0);
    ro_close("share_none_conflicts_with_orig", h, GENERIC_READ, 0, 0);
    ro_close("share_read_only", h, GENERIC_READ, FILE_SHARE_READ, 0);
    ro_close("overlapped", h, GENERIC_READ, SH, FILE_FLAG_OVERLAPPED);
    ro_close("no_buffering", h, GENERIC_READ, SH, FILE_FLAG_NO_BUFFERING);
    ro_close("sequential_scan", h, GENERIC_READ, SH, FILE_FLAG_SEQUENTIAL_SCAN);
    ro_close("write_through", h, GENERIC_READ, SH, FILE_FLAG_WRITE_THROUGH);
    ro_close("backup_semantics", h, GENERIC_READ, SH, FILE_FLAG_BACKUP_SEMANTICS);
    ro_close("delete_on_close_flag", h, GENERIC_READ | DELETE, SH, FILE_FLAG_DELETE_ON_CLOSE);
    oracle_case("after_delete_on_close_reopen");
    o_file_state("f", L"f.txt");
    oracle_case_end();
    ro_close("attribute_bits_in_flags", h, GENERIC_READ, SH, FILE_ATTRIBUTE_HIDDEN);
    ro_close("open_reparse_point_flag", h, GENERIC_READ, SH, FILE_FLAG_OPEN_REPARSE_POINT);
    ro_close("security_flags", h, GENERIC_READ, SH, SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION);
    CloseHandle(h);

    make_file(L"f.txt", "0123456789");
    /* Original opened without sharing write. */
    h = CreateFileW(L"f.txt", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    ro_close("orig_share_read_reopen_write", h, GENERIC_WRITE, SH, 0);
    ro_close("orig_share_read_reopen_read", h, GENERIC_READ, SH, 0);
    ro_close("orig_share_read_reopen_read_share_read", h, GENERIC_READ, FILE_SHARE_READ, 0);
    CloseHandle(h);

    /* Original without any data access. */
    h = CreateFileW(L"f.txt", FILE_READ_ATTRIBUTES, SH, NULL, OPEN_EXISTING, 0, NULL);
    ro_close("from_attributes_only_to_read", h, GENERIC_READ, SH, 0);
    CloseHandle(h);

    /* Original exclusive. */
    h = CreateFileW(L"f.txt", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    ro_close("orig_exclusive_reopen_read", h, GENERIC_READ, SH, 0);
    ro_close("orig_exclusive_reopen_attributes", h, FILE_READ_ATTRIBUTES, SH, 0);
    CloseHandle(h);

    /* Original deleted pending. */
    make_file(L"g.txt", "pending");
    h = CreateFileW(L"g.txt", GENERIC_READ | DELETE, SH, NULL, OPEN_EXISTING, FILE_FLAG_DELETE_ON_CLOSE, NULL);
    ro_close("orig_delete_on_close", h, GENERIC_READ, SH, 0);
    CloseHandle(h);
    make_file(L"g.txt", "renamed");
    h = CreateFileW(L"g.txt", GENERIC_READ, SH, NULL, OPEN_EXISTING, 0, NULL);
    MoveFileW(L"g.txt", L"g2.txt");
    ro_close("orig_renamed", h, GENERIC_READ, SH, 0);
    CloseHandle(h);

    /* Directories. */
    CreateDirectoryW(L"d", NULL);
    h = CreateFileW(L"d", GENERIC_READ, SH, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    ro_close("directory_no_backup_semantics", h, GENERIC_READ, SH, 0);
    ro_close("directory_backup_semantics", h, GENERIC_READ, SH, FILE_FLAG_BACKUP_SEMANTICS);
    CloseHandle(h);

    /* Non-file handles. */
    {
        HANDLE r, w;
        CreatePipe(&r, &w, NULL, 0);
        ro_close("anon_pipe", r, GENERIC_READ, SH, 0);
        CloseHandle(r);
        CloseHandle(w);
        HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, SH, NULL, OPEN_EXISTING, 0, NULL);
        ro_close("nul_device", nul, GENERIC_READ, SH, 0);
        CloseHandle(nul);
        HANDLE ev = CreateEventW(NULL, FALSE, FALSE, NULL);
        ro_close("event", ev, GENERIC_READ, SH, 0);
        CloseHandle(ev);
    }
    ro_close("invalid_handle", INVALID_HANDLE_VALUE, GENERIC_READ, SH, 0);
    ro_close("null_handle", NULL, GENERIC_READ, SH, 0);

    return oracle_end();
}
