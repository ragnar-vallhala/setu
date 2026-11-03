/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: OpenFileById with file IDs taken from the file system
 * (FileIdType from BY_HANDLE_FILE_INFORMATION, ExtendedFileIdType from
 * FileIdInfo), files in other directories, deleted files, and the error
 * paths for bad hints, types, sizes and IDs. IDs are never printed.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-openfilebyid
 *       https://learn.microsoft.com/en-us/windows/win32/api/winbase/ns-winbase-file_id_descriptor
 *       https://learn.microsoft.com/en-us/windows/win32/api/winbase/ns-winbase-file_id_info
 */
#include "oracle.h"

static const DWORD SHARE_ALL = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;

static void make_file(const wchar_t *path, const char *content)
{
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    DWORD w;
    WriteFile(h, content, (DWORD)strlen(content), &w, NULL);
    CloseHandle(h);
}

static LONGLONG file_id(const wchar_t *path)
{
    BY_HANDLE_FILE_INFORMATION bi;
    HANDLE h = CreateFileW(path, 0, SHARE_ALL, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    GetFileInformationByHandle(h, &bi);
    CloseHandle(h);
    return (LONGLONG)(((ULONGLONG)bi.nFileIndexHigh << 32) | bi.nFileIndexLow);
}

static void name_of(HANDLE h, char *out, size_t n)
{
    wchar_t buf[MAX_PATH];
    DWORD len = GetFinalPathNameByHandleW(h, buf, MAX_PATH, VOLUME_NAME_NONE);
    const wchar_t *base = buf;
    /* Last two components, enough to identify the test's own files. */
    int seen = 0;
    for (DWORD i = len; i > 0; i--)
        if (buf[i - 1] == L'\\' && ++seen == 2) {
            base = buf + i;
            break;
        }
    snprintf(out, n, "%s", len ? oracle_utf8(base) : "(none)");
}

static void obi_case(const char *name, HANDLE hint, FILE_ID_DESCRIPTOR *d, DWORD access, DWORD share, DWORD flags)
{
    HANDLE h;
    ORACLE_CALL(h, OpenFileById(hint, d, access, share, NULL, flags));
    oracle_case(name);
    o_handle("ret", h);
    o_lasterr();
    if (h != INVALID_HANDLE_VALUE) {
        char nm[MAX_PATH * 2];
        name_of(h, nm, sizeof nm);
        o_str("name", nm);
        char buf[16];
        DWORD n = 0;
        if (access & GENERIC_READ) {
            if (ReadFile(h, buf, sizeof buf, &n, NULL)) o_bytes("data", buf, n);
            else o_u32("read_error", GetLastError());
        }
        CloseHandle(h);
    }
    oracle_case_end();
}

int main(void)
{
    oracle_begin("kernel32/OpenFileById");
    FILE_ID_DESCRIPTOR d;
    make_file(L"a.txt", "alpha");
    CreateDirectoryW(L"sub", NULL);
    CreateDirectoryW(L"sub\\deeper", NULL);
    make_file(L"sub\\deeper\\b.txt", "beta");

    HANDLE cwd = CreateFileW(L".", GENERIC_READ, SHARE_ALL, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    HANDLE sub = CreateFileW(L"sub", GENERIC_READ, SHARE_ALL, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    HANDLE fileh = CreateFileW(L"a.txt", GENERIC_READ, SHARE_ALL, NULL, OPEN_EXISTING, 0, NULL);

    memset(&d, 0, sizeof d);
    d.dwSize = sizeof d;
    d.Type = FileIdType;
    d.FileId.QuadPart = file_id(L"a.txt");
    obi_case("file_id_same_dir", cwd, &d, GENERIC_READ, SHARE_ALL, 0);
    obi_case("file_id_hint_other_dir", sub, &d, GENERIC_READ, SHARE_ALL, 0);
    obi_case("file_id_hint_is_file", fileh, &d, GENERIC_READ, SHARE_ALL, 0);
    obi_case("file_id_no_access", cwd, &d, 0, SHARE_ALL, 0);
    obi_case("file_id_write_access", cwd, &d, GENERIC_WRITE, SHARE_ALL, 0);

    d.FileId.QuadPart = file_id(L"sub\\deeper\\b.txt");
    obi_case("file_id_nested", cwd, &d, GENERIC_READ, SHARE_ALL, 0);
    d.FileId.QuadPart = file_id(L"sub\\deeper");
    obi_case("directory_id_no_backup_semantics", cwd, &d, GENERIC_READ, SHARE_ALL, 0);
    obi_case("directory_id_backup_semantics", cwd, &d, GENERIC_READ, SHARE_ALL, FILE_FLAG_BACKUP_SEMANTICS);
    d.FileId.QuadPart = file_id(L".");
    obi_case("cwd_id", cwd, &d, GENERIC_READ, SHARE_ALL, FILE_FLAG_BACKUP_SEMANTICS);

    /* Sharing against an exclusive open. */
    make_file(L"x.txt", "excl");
    d.FileId.QuadPart = file_id(L"x.txt");
    {
        HANDLE ex = CreateFileW(L"x.txt", GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
        obi_case("sharing_violation", cwd, &d, GENERIC_READ, SHARE_ALL, 0);
        CloseHandle(ex);
    }
    obi_case("after_exclusive_closed", cwd, &d, GENERIC_READ, SHARE_ALL, 0);

    /* Renamed and deleted files. */
    MoveFileW(L"x.txt", L"sub\\x_moved.txt");
    obi_case("after_rename_to_subdir", cwd, &d, GENERIC_READ, SHARE_ALL, 0);
    DeleteFileW(L"sub\\x_moved.txt");
    obi_case("after_delete", cwd, &d, GENERIC_READ, SHARE_ALL, 0);
    make_file(L"y.txt", "pending");
    d.FileId.QuadPart = file_id(L"y.txt");
    {
        HANDLE p = CreateFileW(L"y.txt", DELETE, SHARE_ALL, NULL, OPEN_EXISTING, FILE_FLAG_DELETE_ON_CLOSE, NULL);
        obi_case("delete_pending", cwd, &d, GENERIC_READ, SHARE_ALL, 0);
        CloseHandle(p);
    }

    /* Bogus IDs. */
    d.FileId.QuadPart = 0;
    obi_case("id_zero", cwd, &d, GENERIC_READ, SHARE_ALL, 0);
    d.FileId.QuadPart = 0x0000123456789abcLL;
    obi_case("id_bogus", cwd, &d, GENERIC_READ, SHARE_ALL, 0);
    d.FileId.QuadPart = -1;
    obi_case("id_minus_one", cwd, &d, GENERIC_READ, SHARE_ALL, 0);

    /* Descriptor validation. */
    d.FileId.QuadPart = file_id(L"a.txt");
    d.dwSize = 4;
    obi_case("size_4", cwd, &d, GENERIC_READ, SHARE_ALL, 0);
    d.dwSize = 0;
    obi_case("size_0", cwd, &d, GENERIC_READ, SHARE_ALL, 0);
    d.dwSize = sizeof d + 16;
    obi_case("size_large", cwd, &d, GENERIC_READ, SHARE_ALL, 0);
    d.dwSize = sizeof d;
    d.Type = (FILE_ID_TYPE)7;
    obi_case("type_unknown", cwd, &d, GENERIC_READ, SHARE_ALL, 0);
    d.Type = ObjectIdType;
    obi_case("type_object_id_none", cwd, &d, GENERIC_READ, SHARE_ALL, 0);

    /* 128-bit IDs. */
    {
        FILE_ID_INFO fi;
        HANDLE f = CreateFileW(L"a.txt", 0, SHARE_ALL, NULL, OPEN_EXISTING, 0, NULL);
        BOOL r;
        ORACLE_CALL(r, GetFileInformationByHandleEx(f, FileIdInfo, &fi, sizeof fi));
        CloseHandle(f);
        oracle_case("file_id_info");
        o_bool("ret", r);
        o_lasterr();
        if (r) {
            ULONGLONG lo;
            memcpy(&lo, fi.FileId.Identifier, 8);
            o_bool("low_matches_legacy_id", lo == (ULONGLONG)file_id(L"a.txt"));
            ULONGLONG hi;
            memcpy(&hi, fi.FileId.Identifier + 8, 8);
            o_bool("high_zero", hi == 0);
        }
        oracle_case_end();
        if (r) {
            memset(&d, 0, sizeof d);
            d.dwSize = sizeof d;
            d.Type = ExtendedFileIdType;
            memcpy(&d.ExtendedFileId, &fi.FileId, sizeof fi.FileId);
            obi_case("extended_id", cwd, &d, GENERIC_READ, SHARE_ALL, 0);
        }
    }

    /* Hint handles. */
    memset(&d, 0, sizeof d);
    d.dwSize = sizeof d;
    d.Type = FileIdType;
    d.FileId.QuadPart = file_id(L"a.txt");
    obi_case("hint_null", NULL, &d, GENERIC_READ, SHARE_ALL, 0);
    obi_case("hint_invalid", INVALID_HANDLE_VALUE, &d, GENERIC_READ, SHARE_ALL, 0);
    {
        HANDLE ev = CreateEventW(NULL, FALSE, FALSE, NULL);
        obi_case("hint_event", ev, &d, GENERIC_READ, SHARE_ALL, 0);
        CloseHandle(ev);
        HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, SHARE_ALL, NULL, OPEN_EXISTING, 0, NULL);
        obi_case("hint_nul_device", nul, &d, GENERIC_READ, SHARE_ALL, 0);
        CloseHandle(nul);
    }
    obi_case("delete_on_close_flag", cwd, &d, GENERIC_READ | DELETE, SHARE_ALL, FILE_FLAG_DELETE_ON_CLOSE);
    oracle_case("after_delete_on_close_by_id");
    o_file_state("a", L"a.txt");
    oracle_case_end();

    CloseHandle(fileh);
    CloseHandle(sub);
    CloseHandle(cwd);
    return oracle_end();
}
