/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: the legacy GetFileInformationByHandle (BY_HANDLE_FILE_INFORMATION):
 * attributes, sizes, link counts, file index identity, volume serial
 * consistency, and the handle kinds it rejects. Identifiers are compared,
 * never printed.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getfileinformationbyhandle
 *       https://learn.microsoft.com/en-us/windows/win32/api/fileapi/ns-fileapi-by_handle_file_information
 */
#include "oracle.h"

static unsigned long long idx(const BY_HANDLE_FILE_INFORMATION *i)
{
    return ((unsigned long long)i->nFileIndexHigh << 32) | i->nFileIndexLow;
}

static void make_file(const wchar_t *path, const char *content)
{
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    DWORD w;
    WriteFile(h, content, (DWORD)strlen(content), &w, NULL);
    CloseHandle(h);
}

static BY_HANDLE_FILE_INFORMATION ref; /* f.txt, for identity comparisons */

static BOOL info_case(const char *name, HANDLE h, BY_HANDLE_FILE_INFORMATION *out)
{
    BY_HANDLE_FILE_INFORMATION i;
    BOOL r;
    memset(&i, 0, sizeof i);
    ORACLE_CALL(r, GetFileInformationByHandle(h, &i));
    oracle_case(name);
    o_bool("ret", r);
    o_lasterr();
    if (r) {
        o_hex32("attrs", i.dwFileAttributes);
        o_u64("size", ((unsigned long long)i.nFileSizeHigh << 32) | i.nFileSizeLow);
        o_u32("links", i.nNumberOfLinks);
        o_bool("serial_matches_ref", i.dwVolumeSerialNumber == ref.dwVolumeSerialNumber);
        o_bool("index_matches_ref", idx(&i) == idx(&ref));
        o_bool("index_nonzero", idx(&i) != 0);
    }
    oracle_case_end();
    if (out) *out = i;
    return r;
}

static HANDLE op(const wchar_t *p, DWORD access, DWORD flags)
{
    return CreateFileW(p, access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                       flags, NULL);
}

int main(void)
{
    oracle_begin("kernel32/GetFileInformationByHandle");
    HANDLE h;
    BY_HANDLE_FILE_INFORMATION i;

    make_file(L"f.txt", "hello world");
    h = op(L"f.txt", GENERIC_READ, 0);
    GetFileInformationByHandle(h, &ref);
    info_case("file_basic", h, NULL);
    CloseHandle(h);

    oracle_case("ref_shape");
    o_bool("serial_nonzero", ref.dwVolumeSerialNumber != 0);
    {
        DWORD serial = 0;
        GetVolumeInformationW(NULL, NULL, 0, &serial, NULL, NULL, NULL, 0);
        o_bool("serial_matches_volume_info", serial == ref.dwVolumeSerialNumber);
    }
    oracle_case_end();

    h = op(L"f.txt", 0, 0);
    info_case("file_no_access", h, NULL);
    CloseHandle(h);
    h = op(L"f.txt", FILE_READ_ATTRIBUTES, 0);
    info_case("file_read_attributes", h, NULL);
    CloseHandle(h);
    h = op(L"F.TXT", GENERIC_WRITE, 0);
    info_case("file_reopened_other_case", h, NULL);
    CloseHandle(h);

    SetFileAttributesW(L"f.txt", FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_READONLY);
    h = op(L"f.txt", GENERIC_READ, 0);
    info_case("file_hidden_readonly", h, NULL);
    CloseHandle(h);
    SetFileAttributesW(L"f.txt", FILE_ATTRIBUTE_NORMAL);
    h = op(L"f.txt", GENERIC_READ, 0);
    info_case("file_attrs_normal", h, NULL);
    CloseHandle(h);
    SetFileAttributesW(L"f.txt", FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED);
    h = op(L"f.txt", GENERIC_READ, 0);
    info_case("file_attrs_system_nci", h, NULL);
    CloseHandle(h);
    SetFileAttributesW(L"f.txt", FILE_ATTRIBUTE_ARCHIVE);

    /* Hard links share the index; the count follows creation and deletion. */
    CreateHardLinkW(L"link1.txt", L"f.txt", NULL);
    h = op(L"link1.txt", GENERIC_READ, 0);
    info_case("hardlink_one", h, NULL);
    CloseHandle(h);
    CreateHardLinkW(L"link2.txt", L"f.txt", NULL);
    h = op(L"f.txt", GENERIC_READ, 0);
    info_case("hardlink_two_via_original", h, NULL);
    DeleteFileW(L"link1.txt");
    info_case("hardlink_after_delete_same_handle", h, NULL);
    CloseHandle(h);

    /* Delete pending: count drops? */
    h = CreateFileW(L"link2.txt", GENERIC_READ | DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    NULL, OPEN_EXISTING, FILE_FLAG_DELETE_ON_CLOSE, NULL);
    info_case("hardlink_delete_on_close_open", h, NULL);
    CloseHandle(h);
    h = op(L"f.txt", GENERIC_READ, 0);
    info_case("hardlink_after_delete_on_close", h, NULL);
    CloseHandle(h);

    /* Different file: different index. */
    make_file(L"g.txt", "");
    h = op(L"g.txt", GENERIC_READ, 0);
    info_case("other_file_empty", h, NULL);
    CloseHandle(h);

    /* Copy and rename. */
    CopyFileW(L"f.txt", L"copy.txt", FALSE);
    h = op(L"copy.txt", GENERIC_READ, 0);
    info_case("copy_has_new_index", h, NULL);
    CloseHandle(h);
    MoveFileW(L"f.txt", L"moved.txt");
    h = op(L"moved.txt", GENERIC_READ, 0);
    info_case("renamed_keeps_index", h, NULL);
    CloseHandle(h);

    /* Size changes through the same handle. */
    h = CreateFileW(L"moved.txt", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    {
        LARGE_INTEGER li;
        li.QuadPart = 5000;
        SetFilePointerEx(h, li, NULL, FILE_BEGIN);
        SetEndOfFile(h);
    }
    info_case("after_extend_5000", h, NULL);
    {
        DWORD w;
        SetFilePointer(h, 0, NULL, FILE_END);
        WriteFile(h, "tail", 4, &w, NULL);
    }
    info_case("after_append_write", h, NULL);
    CloseHandle(h);

    /* Directories. */
    CreateDirectoryW(L"d", NULL);
    h = op(L"d", GENERIC_READ, FILE_FLAG_BACKUP_SEMANTICS);
    info_case("directory_empty", h, &i);
    CloseHandle(h);
    CreateDirectoryW(L"d\\sub1", NULL);
    CreateDirectoryW(L"d\\sub2", NULL);
    make_file(L"d\\x", "x");
    h = op(L"d", GENERIC_READ, FILE_FLAG_BACKUP_SEMANTICS);
    info_case("directory_with_children", h, NULL);
    CloseHandle(h);
    h = op(L".", GENERIC_READ, FILE_FLAG_BACKUP_SEMANTICS);
    info_case("current_directory", h, NULL);
    CloseHandle(h);
    SetFileAttributesW(L"d", FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN);
    h = op(L"d", GENERIC_READ, FILE_FLAG_BACKUP_SEMANTICS);
    info_case("directory_readonly_hidden", h, NULL);
    CloseHandle(h);
    SetFileAttributesW(L"d", FILE_ATTRIBUTE_NORMAL);

    /* Non-disk handles. */
    h = op(L"NUL", GENERIC_READ, 0);
    info_case("nul_device", h, NULL);
    CloseHandle(h);
    {
        HANDLE r, w;
        CreatePipe(&r, &w, NULL, 0);
        info_case("anon_pipe", r, NULL);
        CloseHandle(r);
        CloseHandle(w);
    }
    {
        HANDLE s = CreateNamedPipeW(L"\\\\.\\pipe\\setu_oracle_gfibh", PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE, 1, 64, 64,
                                    0, NULL);
        info_case("named_pipe_server", s, NULL);
        CloseHandle(s);
    }
    {
        HANDLE ev = CreateEventW(NULL, FALSE, FALSE, NULL);
        info_case("event", ev, NULL);
        CloseHandle(ev);
    }
    info_case("invalid_handle_value", INVALID_HANDLE_VALUE, NULL);
    info_case("null_handle", NULL, NULL);
    info_case("current_process", GetCurrentProcess(), NULL);

    return oracle_end();
}
