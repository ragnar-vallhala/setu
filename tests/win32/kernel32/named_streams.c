/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: NTFS named data streams ("file:stream") through the Win32
 * file APIs: create, read, size, enumerate (FindFirstStreamW,
 * FileStreamInfo), delete, rename, the ::$DATA syntax, invalid names, and
 * directory streams.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/fileio/file-streams
 *       https://learn.microsoft.com/en-us/windows/win32/fileio/naming-a-file
 *       https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-findfirststreamw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winbase/ns-winbase-file_stream_info
 */
#include "oracle.h"

static const DWORD SH = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;

static void write_case(const char *name, const wchar_t *path, const char *data, DWORD disp)
{
    HANDLE h;
    ORACLE_CALL(h, CreateFileW(path, GENERIC_WRITE, SH, NULL, disp, 0, NULL));
    oracle_case(name);
    o_handle("ret", h);
    o_lasterr();
    if (h != INVALID_HANDLE_VALUE) {
        DWORD w = 0;
        o_bool("write", WriteFile(h, data, (DWORD)strlen(data), &w, NULL));
        CloseHandle(h);
    }
    oracle_case_end();
}

static void read_case(const char *name, const wchar_t *path)
{
    HANDLE h;
    ORACLE_CALL(h, CreateFileW(path, GENERIC_READ, SH, NULL, OPEN_EXISTING, 0, NULL));
    oracle_case(name);
    o_handle("ret", h);
    o_lasterr();
    if (h != INVALID_HANDLE_VALUE) {
        char buf[64];
        DWORD n = 0;
        ReadFile(h, buf, sizeof buf, &n, NULL);
        o_bytes("data", buf, n);
        LARGE_INTEGER sz;
        GetFileSizeEx(h, &sz);
        o_i64("size", sz.QuadPart);
        CloseHandle(h);
    }
    oracle_case_end();
}

static void streams_case(const char *name, const wchar_t *path)
{
    WIN32_FIND_STREAM_DATA sd;
    HANDLE h;
    char out[1024] = "";
    ORACLE_CALL(h, FindFirstStreamW(path, FindStreamInfoStandard, &sd, 0));
    oracle_case(name);
    o_handle("ret", h);
    o_lasterr();
    if (h != INVALID_HANDLE_VALUE) {
        do {
            char item[MAX_PATH * 2];
            snprintf(item, sizeof item, "%s%s=%lld", out[0] ? "," : "", oracle_utf8(sd.cStreamName),
                     sd.StreamSize.QuadPart);
            strncat(out, item, sizeof out - strlen(out) - 1);
        } while (FindNextStreamW(h, &sd));
        o_u32("end_error", GetLastError());
        FindClose(h);
        o_str("streams", out);
    }
    oracle_case_end();
}

static void attrs_case(const char *name, const wchar_t *path)
{
    WIN32_FILE_ATTRIBUTE_DATA fad;
    BOOL r;
    ORACLE_CALL(r, GetFileAttributesExW(path, GetFileExInfoStandard, &fad));
    oracle_case(name);
    o_bool("ret", r);
    o_lasterr();
    if (r) {
        o_hex32("attrs", fad.dwFileAttributes);
        o_u32("size", fad.nFileSizeLow);
    }
    oracle_case_end();
}

int main(void)
{
    oracle_begin("kernel32/named_streams");

    write_case("create_main", L"f.txt", "main data", CREATE_ALWAYS);
    write_case("create_stream", L"f.txt:s1", "stream one!", CREATE_NEW);
    write_case("create_stream_explicit_type", L"f.txt:s2:$DATA", "two", CREATE_NEW);
    write_case("create_stream_exists", L"f.txt:s1", "x", CREATE_NEW);
    write_case("create_stream_missing_file", L"g.txt:s", "auto", CREATE_NEW);
    write_case("create_stream_bad_type", L"f.txt:s3:$BOGUS", "x", CREATE_NEW);
    write_case("create_stream_empty_name_data", L"f.txt::$DATA", "main via $DATA", OPEN_EXISTING);
    write_case("create_stream_invalid_char", L"f.txt:a\\b", "x", CREATE_NEW);
    write_case("create_stream_too_many_colons", L"f.txt:a:b:c", "x", CREATE_NEW);

    read_case("read_main", L"f.txt");
    read_case("read_stream", L"f.txt:s1");
    read_case("read_stream_case_insensitive", L"F.TXT:S1");
    read_case("read_stream_with_type", L"f.txt:s1:$DATA");
    read_case("read_stream_s2", L"f.txt:s2");
    read_case("read_missing_stream", L"f.txt:nope");
    read_case("read_main_via_data", L"f.txt::$DATA");
    read_case("read_auto_created_file", L"g.txt");
    read_case("read_auto_created_stream", L"g.txt:s");

    attrs_case("attrs_main", L"f.txt");
    attrs_case("attrs_stream", L"f.txt:s1");
    attrs_case("attrs_missing_stream", L"f.txt:nope");

    streams_case("find_streams", L"f.txt");
    streams_case("find_streams_auto", L"g.txt");
    streams_case("find_streams_missing", L"nope.txt");

    {
        BYTE buf[1024];
        HANDLE h = CreateFileW(L"f.txt", GENERIC_READ, SH, NULL, OPEN_EXISTING, 0, NULL);
        BOOL r;
        ORACLE_CALL(r, GetFileInformationByHandleEx(h, FileStreamInfo, buf, sizeof buf));
        oracle_case("stream_info_class");
        o_bool("ret", r);
        o_lasterr();
        if (r) {
            char out[1024] = "";
            FILE_STREAM_INFO *si = (FILE_STREAM_INFO *)buf;
            for (;;) {
                wchar_t nm[MAX_PATH];
                char item[MAX_PATH * 2];
                DWORD len = si->StreamNameLength / sizeof(WCHAR);
                memcpy(nm, si->StreamName, len * sizeof(WCHAR));
                nm[len] = 0;
                snprintf(item, sizeof item, "%s%s=%lld", out[0] ? "," : "", oracle_utf8(nm), si->StreamSize.QuadPart);
                strncat(out, item, sizeof out - strlen(out) - 1);
                if (!si->NextEntryOffset) break;
                si = (FILE_STREAM_INFO *)((BYTE *)si + si->NextEntryOffset);
            }
            o_str("streams", out);
        }
        oracle_case_end();
        CloseHandle(h);
    }

    /* Directory listing does not show streams. */
    oracle_case("listing");
    o_dir_listing("root", L".", 1);
    oracle_case_end();

    /* Sharing is per stream. */
    {
        HANDLE a = CreateFileW(L"f.txt", GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
        HANDLE b;
        ORACLE_CALL(b, CreateFileW(L"f.txt:s1", GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL));
        oracle_case("stream_open_while_main_exclusive");
        o_handle("ret", b);
        o_lasterr();
        oracle_case_end();
        if (b != INVALID_HANDLE_VALUE) CloseHandle(b);
        CloseHandle(a);
    }

    /* Delete a stream, then the file. */
    {
        BOOL r;
        ORACLE_CALL(r, DeleteFileW(L"f.txt:s2"));
        oracle_case("delete_stream");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        streams_case("find_after_delete_stream", L"f.txt");
        ORACLE_CALL(r, DeleteFileW(L"f.txt:nope"));
        oracle_case("delete_missing_stream");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
    }

    /* Copy keeps streams; truncating the main stream does not touch them. */
    {
        BOOL r;
        ORACLE_CALL(r, CopyFileW(L"f.txt", L"copy.txt", FALSE));
        oracle_case("copy_file");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        streams_case("find_streams_copy", L"copy.txt");
        write_case("truncate_main", L"f.txt", "", TRUNCATE_EXISTING);
        read_case("stream_after_main_truncate", L"f.txt:s1");
        write_case("create_always_main", L"f.txt", "fresh", CREATE_ALWAYS);
        streams_case("streams_after_create_always", L"f.txt");
    }

    /* Directory streams. */
    CreateDirectoryW(L"d", NULL);
    write_case("dir_stream_create", L"d:ds", "dir stream", CREATE_NEW);
    read_case("dir_stream_read", L"d:ds");
    streams_case("dir_streams", L"d");
    attrs_case("dir_attrs_after_stream", L"d");

    /* Rename a stream via MoveFileW is not allowed. */
    {
        BOOL r;
        ORACLE_CALL(r, MoveFileW(L"copy.txt:s1", L"copy.txt:s9"));
        oracle_case("move_stream");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
    }

    return oracle_end();
}
