/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: GetFileInformationByHandle(Ex) and SetFileInformationByHandle
 * (basic, standard, name, attribute-tag, stream, end-of-file, allocation,
 * disposition and rename classes), hard links (CreateHardLinkW and link
 * counts), and FILE_FLAG_DELETE_ON_CLOSE / delete-pending behaviour.
 *
 * Timestamps, file ids and volume serial numbers are never printed. The
 * FileNameInfo path starts at the volume root, so only its last component
 * is printed.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getfileinformationbyhandle
 *       https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-getfileinformationbyhandleex
 *       https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-setfileinformationbyhandle
 *       https://learn.microsoft.com/en-us/windows/win32/api/minwinbase/ne-minwinbase-file_info_by_handle_class
 *       https://learn.microsoft.com/en-us/windows/win32/api/winbase/ns-winbase-file_rename_info
 *       https://learn.microsoft.com/en-us/windows/win32/api/winbase/ns-winbase-file_disposition_info
 *       https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-createhardlinkw
 *       https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew
 *       https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-deletefilew
 */
#include "oracle.h"

#define BOOL_CASE(name, expr)     \
    do {                          \
        BOOL ok_;                 \
        ORACLE_CALL(ok_, (expr)); \
        oracle_case(name);        \
        o_bool("ret", ok_);       \
        o_lasterr();              \
        oracle_case_end();        \
    } while (0)

static HANDLE mk(const wchar_t *name, const char *data)
{
    HANDLE h = CreateFileW(name, GENERIC_READ | GENERIC_WRITE | DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, CREATE_ALWAYS, 0, NULL);
    DWORD w;
    if (data) WriteFile(h, data, (DWORD)strlen(data), &w, NULL);
    return h;
}

static void touch(const wchar_t *name, const char *data) { CloseHandle(mk(name, data)); }

static HANDLE open_case(const char *name, const wchar_t *path, DWORD access, DWORD share, DWORD disp, DWORD flags)
{
    HANDLE h;
    ORACLE_CALL(h, CreateFileW(path, access, share, NULL, disp, flags, NULL));
    oracle_case(name);
    o_handle("ret", h);
    o_lasterr();
    oracle_case_end();
    return h;
}

static void std_case(const char *name, HANDLE h)
{
    FILE_STANDARD_INFO si;
    BOOL ok;
    memset(&si, 0xAA, sizeof si);
    ORACLE_CALL(ok, GetFileInformationByHandleEx(h, FileStandardInfo, &si, sizeof si));
    oracle_case(name);
    o_bool("ret", ok);
    o_lasterr();
    if (ok) {
        o_i64("end_of_file", si.EndOfFile.QuadPart);
        o_bool("alloc_ge_eof", si.AllocationSize.QuadPart >= si.EndOfFile.QuadPart);
        o_u32("links", si.NumberOfLinks);
        o_u32("delete_pending", si.DeletePending);
        o_u32("directory", si.Directory);
    }
    oracle_case_end();
}

static void classic_case(const char *name, HANDLE h)
{
    BY_HANDLE_FILE_INFORMATION bi;
    BOOL ok;
    ORACLE_CALL(ok, GetFileInformationByHandle(h, &bi));
    oracle_case(name);
    o_bool("ret", ok);
    o_lasterr();
    if (ok) {
        o_hex32("attrs", bi.dwFileAttributes);
        o_u32("size", bi.nFileSizeLow);
        o_u32("links", bi.nNumberOfLinks);
    }
    oracle_case_end();
}

static int same_file(const wchar_t *a, const wchar_t *b)
{
    BY_HANDLE_FILE_INFORMATION x, y;
    HANDLE ha = CreateFileW(a, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    HANDLE hb = CreateFileW(b, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    int r = ha != INVALID_HANDLE_VALUE && hb != INVALID_HANDLE_VALUE && GetFileInformationByHandle(ha, &x) &&
            GetFileInformationByHandle(hb, &y) && x.nFileIndexLow == y.nFileIndexLow &&
            x.nFileIndexHigh == y.nFileIndexHigh && x.dwVolumeSerialNumber == y.dwVolumeSerialNumber;
    if (ha != INVALID_HANDLE_VALUE) CloseHandle(ha);
    if (hb != INVALID_HANDLE_VALUE) CloseHandle(hb);
    return r;
}

static BOOL rename_to(HANDLE h, const wchar_t *target, BOOL replace, HANDLE root)
{
    BYTE buf[sizeof(FILE_RENAME_INFO) + 64 * sizeof(wchar_t)];
    FILE_RENAME_INFO *ri = (FILE_RENAME_INFO *)buf;
    memset(buf, 0, sizeof buf);
    ri->ReplaceIfExists = replace;
    ri->RootDirectory = root;
    ri->FileNameLength = (DWORD)(wcslen(target) * sizeof(wchar_t));
    wcscpy(ri->FileName, target);
    return SetFileInformationByHandle(h, FileRenameInfo, ri, sizeof buf);
}

static BOOL set_disposition(HANDLE h, BOOL del)
{
    FILE_DISPOSITION_INFO di = {del};
    return SetFileInformationByHandle(h, FileDispositionInfo, &di, sizeof di);
}

int main(void)
{
    oracle_begin("kernel32/file_info_by_handle");

    HANDLE h, h2;
    BOOL ok;

    /* ---- GetFileInformationByHandleEx classes ---- */
    h = mk(L"info.txt", "hello world");
    std_case("std_info_file", h);
    classic_case("classic_info_file", h);
    {
        FILE_BASIC_INFO bi;
        ORACLE_CALL(ok, GetFileInformationByHandleEx(h, FileBasicInfo, &bi, sizeof bi));
        oracle_case("basic_info");
        o_bool("ret", ok);
        o_lasterr();
        o_hex32("attrs", bi.FileAttributes);
        o_bool("times_nonzero", bi.CreationTime.QuadPart && bi.LastWriteTime.QuadPart);
        oracle_case_end();
        ORACLE_CALL(ok, GetFileInformationByHandleEx(h, FileBasicInfo, &bi, sizeof bi - 1));
        oracle_case("basic_info_short_buffer");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(ok, GetFileInformationByHandleEx(h, FileBasicInfo, &bi, sizeof bi + 16));
        oracle_case("basic_info_long_buffer");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
    }
    {
        BYTE buf[1024];
        FILE_NAME_INFO *ni = (FILE_NAME_INFO *)buf;
        ORACLE_CALL(ok, GetFileInformationByHandleEx(h, FileNameInfo, buf, sizeof buf));
        oracle_case("name_info");
        o_bool("ret", ok);
        o_lasterr();
        if (ok) {
            wchar_t nm[512];
            DWORD n = ni->FileNameLength / 2;
            wcsncpy(nm, ni->FileName, n);
            nm[n] = 0;
            wchar_t *last = wcsrchr(nm, L'\\');
            o_wstr("last", last ? last + 1 : nm);
            o_bool("starts_with_backslash", nm[0] == L'\\');
            o_bool("no_drive", !(nm[0] && nm[1] == L':'));
        }
        oracle_case_end();
        memset(buf, 0, sizeof buf);
        ORACLE_CALL(ok, GetFileInformationByHandleEx(h, FileNameInfo, buf, sizeof(FILE_NAME_INFO)));
        oracle_case("name_info_small_buffer");
        o_bool("ret", ok);
        o_lasterr();
        o_bool("length_set", ni->FileNameLength > 0);
        oracle_case_end();
        ORACLE_CALL(ok, GetFileInformationByHandleEx(h, FileNameInfo, buf, 2));
        oracle_case("name_info_tiny_buffer");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
    }
    {
        FILE_ATTRIBUTE_TAG_INFO ti;
        ORACLE_CALL(ok, GetFileInformationByHandleEx(h, FileAttributeTagInfo, &ti, sizeof ti));
        oracle_case("attribute_tag_info");
        o_bool("ret", ok);
        o_lasterr();
        o_hex32("attrs", ti.FileAttributes);
        o_hex32("tag", ti.ReparseTag);
        oracle_case_end();
    }
    {
        BYTE buf[1024];
        FILE_STREAM_INFO *si = (FILE_STREAM_INFO *)buf;
        ORACLE_CALL(ok, GetFileInformationByHandleEx(h, FileStreamInfo, buf, sizeof buf));
        oracle_case("stream_info");
        o_bool("ret", ok);
        o_lasterr();
        if (ok) {
            wchar_t nm[128];
            DWORD n = si->StreamNameLength / 2;
            wcsncpy(nm, si->StreamName, n < 127 ? n : 127);
            nm[n < 127 ? n : 127] = 0;
            o_wstr("first_stream", nm);
            o_i64("size", si->StreamSize.QuadPart);
            o_bool("only_one", si->NextEntryOffset == 0);
        }
        oracle_case_end();
    }
    {
        FILE_ID_BOTH_DIR_INFO di;
        ORACLE_CALL(ok, GetFileInformationByHandleEx(h, FileIdBothDirectoryInfo, &di, sizeof di));
        oracle_case("dir_info_on_file");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        BYTE b[64];
        ORACLE_CALL(ok, GetFileInformationByHandleEx(h, (FILE_INFO_BY_HANDLE_CLASS)999, b, sizeof b));
        oracle_case("invalid_class");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(ok, GetFileInformationByHandleEx(h, FileRenameInfo, b, sizeof b));
        oracle_case("get_set_only_class");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
    }

    /* ---- SetFileInformationByHandle ---- */
    {
        FILE_END_OF_FILE_INFO eof = {{.QuadPart = 5}};
        BOOL_CASE("set_end_of_file_shrink", SetFileInformationByHandle(h, FileEndOfFileInfo, &eof, sizeof eof));
        oracle_case("after_shrink");
        o_file_state("file", L"info.txt");
        oracle_case_end();
        eof.EndOfFile.QuadPart = 8;
        BOOL_CASE("set_end_of_file_grow", SetFileInformationByHandle(h, FileEndOfFileInfo, &eof, sizeof eof));
        oracle_case("after_grow");
        o_file_state("file", L"info.txt");
        oracle_case_end();
        eof.EndOfFile.QuadPart = -1;
        BOOL_CASE("set_end_of_file_negative", SetFileInformationByHandle(h, FileEndOfFileInfo, &eof, sizeof eof));
        FILE_ALLOCATION_INFO ai = {{.QuadPart = 2}};
        BOOL_CASE("set_allocation_below_eof", SetFileInformationByHandle(h, FileAllocationInfo, &ai, sizeof ai));
        oracle_case("after_allocation_below_eof");
        o_file_state("file", L"info.txt");
        oracle_case_end();
        BOOL_CASE("set_eof_short_buffer", SetFileInformationByHandle(h, FileEndOfFileInfo, &eof, 4));

        FILE_BASIC_INFO bi;
        memset(&bi, 0, sizeof bi);
        bi.FileAttributes = FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_READONLY;
        BOOL_CASE("set_basic_attrs", SetFileInformationByHandle(h, FileBasicInfo, &bi, sizeof bi));
        oracle_case("attrs_after_set_basic");
        o_hex32("attrs", GetFileAttributesW(L"info.txt"));
        oracle_case_end();
        bi.FileAttributes = FILE_ATTRIBUTE_DIRECTORY;
        BOOL_CASE("set_basic_directory_attr", SetFileInformationByHandle(h, FileBasicInfo, &bi, sizeof bi));
        oracle_case("attrs_after_set_directory_attr");
        o_hex32("attrs", GetFileAttributesW(L"info.txt"));
        oracle_case_end();
        bi.FileAttributes = FILE_ATTRIBUTE_NORMAL;
        BOOL_CASE("set_basic_normal", SetFileInformationByHandle(h, FileBasicInfo, &bi, sizeof bi));
        oracle_case("attrs_after_set_normal");
        o_hex32("attrs", GetFileAttributesW(L"info.txt"));
        oracle_case_end();
        bi.FileAttributes = 0;
        bi.LastWriteTime.QuadPart = 125911584000000000LL; /* 2000-01-01 */
        BOOL_CASE("set_basic_write_time", SetFileInformationByHandle(h, FileBasicInfo, &bi, sizeof bi));
        FILE_BASIC_INFO back;
        GetFileInformationByHandleEx(h, FileBasicInfo, &back, sizeof back);
        oracle_case("write_time_after_set");
        o_i64("last_write", back.LastWriteTime.QuadPart);
        o_hex32("attrs", back.FileAttributes);
        oracle_case_end();
        BOOL_CASE("set_standard_info_class", SetFileInformationByHandle(h, FileStandardInfo, &bi, sizeof bi));
        BOOL_CASE("set_invalid_class", SetFileInformationByHandle(h, (FILE_INFO_BY_HANDLE_CLASS)999, &bi, sizeof bi));
    }
    CloseHandle(h);
    {   /* Read-only handle cannot set end of file. */
        HANDLE r = CreateFileW(L"info.txt", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        FILE_END_OF_FILE_INFO eof = {{.QuadPart = 1}};
        BOOL_CASE("set_eof_read_only_handle", SetFileInformationByHandle(r, FileEndOfFileInfo, &eof, sizeof eof));
        FILE_BASIC_INFO bi;
        memset(&bi, 0, sizeof bi);
        bi.FileAttributes = FILE_ATTRIBUTE_HIDDEN;
        BOOL_CASE("set_basic_read_only_handle", SetFileInformationByHandle(r, FileBasicInfo, &bi, sizeof bi));
        BOOL_CASE("set_disposition_without_delete_access", set_disposition(r, TRUE));
        CloseHandle(r);
    }

    /* ---- rename via handle ---- */
    h = mk(L"ren_src.txt", "src");
    touch(L"ren_existing.txt", "existing");
    BOOL_CASE("rename_to_existing_no_replace", rename_to(h, L"ren_existing.txt", FALSE, NULL));
    BOOL_CASE("rename_to_existing_replace", rename_to(h, L"ren_existing.txt", TRUE, NULL));
    oracle_case("after_rename_replace");
    o_dir_listing("dir", L".", 1);
    o_file_state("target", L"ren_existing.txt");
    oracle_case_end();
    BOOL_CASE("rename_to_new_name", rename_to(h, L"ren_new.txt", FALSE, NULL));
    BOOL_CASE("rename_to_subdir_missing", rename_to(h, L"nodir\\x.txt", FALSE, NULL));
    BOOL_CASE("rename_to_empty", rename_to(h, L"", FALSE, NULL));
    BOOL_CASE("rename_to_same_name", rename_to(h, L"ren_new.txt", FALSE, NULL));
    BOOL_CASE("rename_case_only", rename_to(h, L"REN_NEW.txt", FALSE, NULL));
    oracle_case("after_rename_case_only");
    o_dir_listing("dir", L".", 1);
    oracle_case_end();
    CloseHandle(h);
    {
        HANDLE noDel = CreateFileW(L"REN_NEW.txt", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        BOOL_CASE("rename_without_delete_access", rename_to(noDel, L"x.txt", FALSE, NULL));
        CloseHandle(noDel);
    }

    /* ---- hard links ---- */
    touch(L"link_a.txt", "linked");
    BOOL_CASE("hardlink_create", CreateHardLinkW(L"link_b.txt", L"link_a.txt", NULL));
    oracle_case("hardlink_same_file");
    o_bool("same", same_file(L"link_a.txt", L"link_b.txt"));
    o_file_state("b", L"link_b.txt");
    oracle_case_end();
    h = CreateFileW(L"link_a.txt", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    classic_case("hardlink_count_2", h);
    std_case("hardlink_std_info", h);
    {
        DWORD w;
        SetFilePointer(h, 0, NULL, FILE_END);
        WriteFile(h, "+more", 5, &w, NULL);
    }
    CloseHandle(h);
    oracle_case("hardlink_write_visible");
    o_file_state("b", L"link_b.txt");
    oracle_case_end();
    BOOL_CASE("hardlink_target_exists", CreateHardLinkW(L"link_b.txt", L"link_a.txt", NULL));
    BOOL_CASE("hardlink_source_missing", CreateHardLinkW(L"link_c.txt", L"missing.txt", NULL));
    CreateDirectoryW(L"adir", NULL);
    BOOL_CASE("hardlink_to_directory", CreateHardLinkW(L"dirlink", L"adir", NULL));
    BOOL_CASE("hardlink_into_missing_dir", CreateHardLinkW(L"nodir\\l.txt", L"link_a.txt", NULL));
    BOOL_CASE("hardlink_same_name", CreateHardLinkW(L"link_a.txt", L"link_a.txt", NULL));
    BOOL_CASE("hardlink_into_subdir", CreateHardLinkW(L"adir\\link_d.txt", L"link_a.txt", NULL));
    BOOL_CASE("hardlink_delete_original", DeleteFileW(L"link_a.txt"));
    oracle_case("hardlink_after_delete");
    o_file_state("b", L"link_b.txt");
    o_bool("a_exists", GetFileAttributesW(L"link_a.txt") != INVALID_FILE_ATTRIBUTES);
    oracle_case_end();
    h = CreateFileW(L"link_b.txt", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    classic_case("hardlink_count_after_delete", h);
    CloseHandle(h);
    {   /* Readonly attribute is per file, so it shows through every link. */
        SetFileAttributesW(L"link_b.txt", FILE_ATTRIBUTE_READONLY);
        oracle_case("hardlink_readonly_shared");
        o_hex32("other_link_attrs", GetFileAttributesW(L"adir\\link_d.txt"));
        oracle_case_end();
        SetFileAttributesW(L"link_b.txt", FILE_ATTRIBUTE_NORMAL);
    }

    /* ---- FILE_FLAG_DELETE_ON_CLOSE ---- */
    h = open_case("doc_create", L"doc.txt", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  CREATE_NEW, FILE_FLAG_DELETE_ON_CLOSE);
    std_case("doc_std_info", h);
    h2 = open_case("doc_open_without_share_delete", L"doc.txt", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, OPEN_EXISTING, 0);
    if (h2 != INVALID_HANDLE_VALUE) CloseHandle(h2);
    h2 = open_case("doc_open_with_share_delete", L"doc.txt", GENERIC_READ,
                   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, OPEN_EXISTING, 0);
    CloseHandle(h);
    oracle_case("doc_after_first_close");
    o_bool("exists", GetFileAttributesW(L"doc.txt") != INVALID_FILE_ATTRIBUTES);
    o_lasterr();
    oracle_case_end();
    if (h2 != INVALID_HANDLE_VALUE) {
        std_case("doc_second_handle_std_info", h2);
        HANDLE h3 = open_case("doc_open_while_delete_pending", L"doc.txt", GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, OPEN_EXISTING, 0);
        if (h3 != INVALID_HANDLE_VALUE) CloseHandle(h3);
        h3 = open_case("doc_create_while_delete_pending", L"doc.txt", GENERIC_READ,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, CREATE_ALWAYS, 0);
        if (h3 != INVALID_HANDLE_VALUE) CloseHandle(h3);
        oracle_case("doc_listing_while_delete_pending");
        {
            DWORD a;
            ORACLE_CALL(a, GetFileAttributesW(L"doc.txt"));
            o_bool("attrs_ok", a != INVALID_FILE_ATTRIBUTES);
            o_lasterr();
        }
        {
            WIN32_FIND_DATAW fd;
            HANDLE f = FindFirstFileW(L"doc.txt", &fd);
            o_bool("find_first_finds_it", f != INVALID_HANDLE_VALUE);
            if (f != INVALID_HANDLE_VALUE) FindClose(f);
        }
        oracle_case_end();
        CloseHandle(h2);
    }
    oracle_case("doc_after_last_close");
    o_bool("exists", GetFileAttributesW(L"doc.txt") != INVALID_FILE_ATTRIBUTES);
    oracle_case_end();

    touch(L"ro_doc.txt", "x");
    SetFileAttributesW(L"ro_doc.txt", FILE_ATTRIBUTE_READONLY);
    h = open_case("doc_on_readonly_file", L"ro_doc.txt", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, OPEN_EXISTING,
                  FILE_FLAG_DELETE_ON_CLOSE);
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    SetFileAttributesW(L"ro_doc.txt", FILE_ATTRIBUTE_NORMAL);
    h = open_case("doc_without_delete_access_generic_read", L"ro_doc.txt", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
                  OPEN_EXISTING, FILE_FLAG_DELETE_ON_CLOSE);
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    oracle_case("doc_without_delete_access_result");
    o_bool("exists", GetFileAttributesW(L"ro_doc.txt") != INVALID_FILE_ATTRIBUTES);
    oracle_case_end();
    h = open_case("doc_directory", L"adir", GENERIC_READ | DELETE, FILE_SHARE_READ | FILE_SHARE_DELETE, OPEN_EXISTING,
                  FILE_FLAG_DELETE_ON_CLOSE | FILE_FLAG_BACKUP_SEMANTICS);
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    oracle_case("doc_nonempty_directory_result");
    o_bool("exists", GetFileAttributesW(L"adir") != INVALID_FILE_ATTRIBUTES);
    oracle_case_end();

    /* ---- disposition via SetFileInformationByHandle ---- */
    h = mk(L"disp.txt", "d");
    BOOL_CASE("disposition_set", set_disposition(h, TRUE));
    std_case("disposition_std_info", h);
    BOOL_CASE("disposition_clear", set_disposition(h, FALSE));
    std_case("disposition_cleared_std_info", h);
    CloseHandle(h);
    oracle_case("disposition_cleared_file_kept");
    o_bool("exists", GetFileAttributesW(L"disp.txt") != INVALID_FILE_ATTRIBUTES);
    oracle_case_end();
    h = mk(L"disp.txt", "d");
    set_disposition(h, TRUE);
    BOOL_CASE("disposition_then_write", WriteFile(h, "x", 1, (DWORD[]){0}, NULL));
    BOOL_CASE("delete_file_while_pending", DeleteFileW(L"disp.txt"));
    CloseHandle(h);
    oracle_case("disposition_file_gone");
    o_bool("exists", GetFileAttributesW(L"disp.txt") != INVALID_FILE_ATTRIBUTES);
    oracle_case_end();
    SetFileAttributesW(L"rodisp.txt", FILE_ATTRIBUTE_NORMAL);
    h = mk(L"rodisp.txt", "d");
    {
        FILE_BASIC_INFO bi;
        memset(&bi, 0, sizeof bi);
        bi.FileAttributes = FILE_ATTRIBUTE_READONLY;
        SetFileInformationByHandle(h, FileBasicInfo, &bi, sizeof bi);
    }
    BOOL_CASE("disposition_readonly_file", set_disposition(h, TRUE));
    CloseHandle(h);
    SetFileAttributesW(L"rodisp.txt", FILE_ATTRIBUTE_NORMAL);

    /* ---- non-file handles ---- */
    {
        HANDLE ev = CreateEventW(NULL, TRUE, FALSE, NULL);
        FILE_STANDARD_INFO si;
        ORACLE_CALL(ok, GetFileInformationByHandleEx(ev, FileStandardInfo, &si, sizeof si));
        oracle_case("std_info_event_handle");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        CloseHandle(ev);
        HANDLE d = CreateFileW(L"adir", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                               OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
        std_case("std_info_directory", d);
        classic_case("classic_info_directory", d);
        BYTE buf[2048];
        ORACLE_CALL(ok, GetFileInformationByHandleEx(d, FileIdBothDirectoryRestartInfo, buf, sizeof buf));
        oracle_case("dir_enum_restart");
        o_bool("ret", ok);
        o_lasterr();
        if (ok) {
            FILE_ID_BOTH_DIR_INFO *e = (FILE_ID_BOTH_DIR_INFO *)buf;
            wchar_t names[512] = L"";
            int count = 0;
            for (;;) {
                wchar_t nm[64];
                DWORD n = e->FileNameLength / 2;
                wcsncpy(nm, e->FileName, n < 63 ? n : 63);
                nm[n < 63 ? n : 63] = 0;
                if (count) wcscat(names, L",");
                wcscat(names, nm);
                count++;
                if (!e->NextEntryOffset || count > 16) break;
                e = (FILE_ID_BOTH_DIR_INFO *)((BYTE *)e + e->NextEntryOffset);
            }
            o_wstr("names_in_order", names);
        }
        oracle_case_end();
        CloseHandle(d);
    }

    return oracle_end();
}
