/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: DeviceIoControl basics on files: FSCTL_GET_REPARSE_POINT on a
 * non-reparse file, compression and sparse controls, allocated-range
 * queries, unknown control codes, buffer-size errors, and controls sent to
 * directories, devices, pipes and non-file handles.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-deviceiocontrol
 *       https://learn.microsoft.com/en-us/windows/win32/api/winioctl/ni-winioctl-fsctl_get_reparse_point
 *       https://learn.microsoft.com/en-us/windows/win32/api/winioctl/ni-winioctl-fsctl_set_sparse
 *       https://learn.microsoft.com/en-us/windows/win32/api/winioctl/ni-winioctl-fsctl_query_allocated_ranges
 *       https://learn.microsoft.com/en-us/windows/win32/api/winioctl/ni-winioctl-fsctl_get_compression
 */
#include "oracle.h"
#include <winioctl.h>

static BYTE out[16384];

static BOOL ioctl_case(const char *name, HANDLE h, DWORD code, void *in, DWORD in_len, DWORD out_len)
{
    DWORD ret = 0xAAAA;
    BOOL r;
    memset(out, 0xcc, sizeof out);
    ORACLE_CALL(r, DeviceIoControl(h, code, in, in_len, out_len ? out : NULL, out_len, &ret, NULL));
    oracle_case(name);
    o_bool("ret", r);
    o_lasterr();
    if (ret == 0xAAAA) o_str("bytes", "untouched");
    else o_u32("bytes", ret);
    if (r && ret && ret != 0xAAAA && ret <= 64) o_bytes("out", out, ret);
    oracle_case_end();
    return r;
}

static HANDLE op(const wchar_t *p, DWORD access, DWORD flags)
{
    return CreateFileW(p, access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, flags,
                       NULL);
}

int main(void)
{
    oracle_begin("kernel32/DeviceIoControl_fs");
    HANDLE h;
    DWORD w;

    h = CreateFileW(L"f.txt", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    WriteFile(h, "hello", 5, &w, NULL);
    CloseHandle(h);
    CreateDirectoryW(L"d", NULL);

    h = op(L"f.txt", GENERIC_READ, 0);
    ioctl_case("get_reparse_point_regular_file", h, FSCTL_GET_REPARSE_POINT, NULL, 0, MAXIMUM_REPARSE_DATA_BUFFER_SIZE);
    ioctl_case("get_reparse_point_no_buffer", h, FSCTL_GET_REPARSE_POINT, NULL, 0, 0);
    ioctl_case("get_compression", h, FSCTL_GET_COMPRESSION, NULL, 0, sizeof(USHORT));
    ioctl_case("get_compression_buffer_1", h, FSCTL_GET_COMPRESSION, NULL, 0, 1);
    ioctl_case("get_compression_buffer_8", h, FSCTL_GET_COMPRESSION, NULL, 0, 8);
    ioctl_case("unknown_fsctl", h, CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 0xfff, METHOD_BUFFERED, FILE_ANY_ACCESS), NULL, 0,
               16);
    ioctl_case("unknown_device_type", h, CTL_CODE(0x8888, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS), NULL, 0, 16);
    ioctl_case("code_zero", h, 0, NULL, 0, 16);
    ioctl_case("disk_ioctl_on_file", h, IOCTL_DISK_GET_DRIVE_GEOMETRY, NULL, 0, sizeof(DISK_GEOMETRY));
    ioctl_case("storage_ioctl_on_file", h, IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL, 0, 64);
    ioctl_case("set_sparse_read_handle", h, FSCTL_SET_SPARSE, NULL, 0, 0);
    ioctl_case("is_volume_mounted_on_file", h, FSCTL_IS_VOLUME_MOUNTED, NULL, 0, 0);
    ioctl_case("get_object_id_none", h, FSCTL_GET_OBJECT_ID, NULL, 0, sizeof(FILE_OBJECTID_BUFFER));
    {
        FILE_ALLOCATED_RANGE_BUFFER q = {{{0}}, {{0}}};
        q.Length.QuadPart = 1 << 20;
        ioctl_case("query_allocated_ranges_dense", h, FSCTL_QUERY_ALLOCATED_RANGES, &q, sizeof q, 1024);
        ioctl_case("query_allocated_ranges_short_input", h, FSCTL_QUERY_ALLOCATED_RANGES, &q, 8, 1024);
        ioctl_case("query_allocated_ranges_short_output", h, FSCTL_QUERY_ALLOCATED_RANGES, &q, sizeof q, 8);
        q.FileOffset.QuadPart = -1;
        ioctl_case("query_allocated_ranges_negative_offset", h, FSCTL_QUERY_ALLOCATED_RANGES, &q, sizeof q, 1024);
    }
    CloseHandle(h);

    /* Sparse file round trip. */
    h = CreateFileW(L"s.bin", GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    ioctl_case("set_sparse", h, FSCTL_SET_SPARSE, NULL, 0, 0);
    {
        FILE_SET_SPARSE_BUFFER sb = {FALSE};
        LARGE_INTEGER li;
        li.QuadPart = 4 << 20;
        SetFilePointerEx(h, li, NULL, FILE_BEGIN);
        SetEndOfFile(h);
        li.QuadPart = 1 << 20;
        SetFilePointerEx(h, li, NULL, FILE_BEGIN);
        static char blk[65536];
        memset(blk, 'x', sizeof blk);
        WriteFile(h, blk, sizeof blk, &w, NULL);
        FILE_ALLOCATED_RANGE_BUFFER q;
        q.FileOffset.QuadPart = 0;
        q.Length.QuadPart = 4 << 20;
        ioctl_case("query_allocated_ranges_sparse", h, FSCTL_QUERY_ALLOCATED_RANGES, &q, sizeof q, 1024);
        FILE_ZERO_DATA_INFORMATION z;
        z.FileOffset.QuadPart = 0;
        z.BeyondFinalZero.QuadPart = 4 << 20;
        ioctl_case("set_zero_data_all", h, FSCTL_SET_ZERO_DATA, &z, sizeof z, 0);
        ioctl_case("query_allocated_ranges_after_zero", h, FSCTL_QUERY_ALLOCATED_RANGES, &q, sizeof q, 1024);
        z.FileOffset.QuadPart = 100;
        z.BeyondFinalZero.QuadPart = 50;
        ioctl_case("set_zero_data_reversed", h, FSCTL_SET_ZERO_DATA, &z, sizeof z, 0);
        ioctl_case("set_sparse_explicit_false", h, FSCTL_SET_SPARSE, &sb, sizeof sb, 0);
        oracle_case("attrs_after_unsparse");
        o_hex32("attrs", GetFileAttributesW(L"s.bin"));
        oracle_case_end();
    }
    CloseHandle(h);

    /* Compression state changes (NTFS supports LZNT1). */
    h = op(L"f.txt", GENERIC_READ | GENERIC_WRITE, 0);
    {
        USHORT fmt = COMPRESSION_FORMAT_DEFAULT;
        ioctl_case("set_compression_default", h, FSCTL_SET_COMPRESSION, &fmt, sizeof fmt, 0);
        ioctl_case("get_compression_after_set", h, FSCTL_GET_COMPRESSION, NULL, 0, sizeof(USHORT));
        oracle_case("attrs_after_compress");
        o_hex32("attrs", GetFileAttributesW(L"f.txt"));
        oracle_case_end();
        fmt = COMPRESSION_FORMAT_NONE;
        ioctl_case("set_compression_none", h, FSCTL_SET_COMPRESSION, &fmt, sizeof fmt, 0);
        fmt = 0x55;
        ioctl_case("set_compression_bogus_format", h, FSCTL_SET_COMPRESSION, &fmt, sizeof fmt, 0);
    }
    CloseHandle(h);

    /* Directories. */
    h = op(L"d", GENERIC_READ, FILE_FLAG_BACKUP_SEMANTICS);
    ioctl_case("dir_get_reparse_point", h, FSCTL_GET_REPARSE_POINT, NULL, 0, MAXIMUM_REPARSE_DATA_BUFFER_SIZE);
    ioctl_case("dir_get_compression", h, FSCTL_GET_COMPRESSION, NULL, 0, sizeof(USHORT));
    CloseHandle(h);

    /* Other handle kinds. */
    h = op(L"NUL", GENERIC_READ, 0);
    ioctl_case("nul_get_reparse_point", h, FSCTL_GET_REPARSE_POINT, NULL, 0, 1024);
    ioctl_case("nul_unknown", h, CTL_CODE(0x8888, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS), NULL, 0, 16);
    CloseHandle(h);
    {
        HANDLE r, wr;
        CreatePipe(&r, &wr, NULL, 0);
        ioctl_case("pipe_get_reparse_point", r, FSCTL_GET_REPARSE_POINT, NULL, 0, 1024);
        WriteFile(wr, "abc", 3, &w, NULL);
        ioctl_case("pipe_peek", r, 0x0011400C /* FSCTL_PIPE_PEEK */, NULL, 0, 16 + 8);
        ioctl_case("pipe_peek_small", r, 0x0011400C /* FSCTL_PIPE_PEEK */, NULL, 0, 4);
        CloseHandle(r);
        CloseHandle(wr);
    }
    {
        HANDLE ev = CreateEventW(NULL, FALSE, FALSE, NULL);
        ioctl_case("event_handle", ev, FSCTL_GET_REPARSE_POINT, NULL, 0, 1024);
        CloseHandle(ev);
    }
    ioctl_case("invalid_handle_value", INVALID_HANDLE_VALUE, FSCTL_GET_REPARSE_POINT, NULL, 0, 1024);
    ioctl_case("null_handle", NULL, FSCTL_GET_REPARSE_POINT, NULL, 0, 1024);

    return oracle_end();
}
