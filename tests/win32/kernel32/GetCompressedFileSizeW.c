/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: GetCompressedFileSizeW on plain, empty, large, sparse files,
 * directories and bad paths, including the high-DWORD output and the
 * INVALID_FILE_SIZE / last-error convention.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getcompressedfilesizew
 *       https://learn.microsoft.com/en-us/windows/win32/fileio/sparse-files
 */
#include "oracle.h"
#include <winioctl.h>

static void make_file(const wchar_t *path, DWORD size)
{
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    static char block[65536];
    memset(block, 'x', sizeof block);
    while (size) {
        DWORD n = size > sizeof block ? sizeof block : size, w;
        WriteFile(h, block, n, &w, NULL);
        size -= n;
    }
    CloseHandle(h);
}

static void cfs_case(const char *name, const wchar_t *path, int want_high)
{
    DWORD high = 0xAAAAAAAA, low;
    ORACLE_CALL(low, GetCompressedFileSizeW(path, want_high ? &high : NULL));
    oracle_case(name);
    o_u32("low", low);
    if (want_high) o_hex32("high", high);
    o_lasterr();
    oracle_case_end();
}

int main(void)
{
    oracle_begin("kernel32/GetCompressedFileSizeW");

    make_file(L"empty.bin", 0);
    make_file(L"small.bin", 5);
    make_file(L"k4.bin", 4096);
    make_file(L"odd.bin", 4097);
    make_file(L"big.bin", 1048576 + 3);

    cfs_case("empty", L"empty.bin", 1);
    cfs_case("small", L"small.bin", 1);
    cfs_case("small_no_high", L"small.bin", 0);
    cfs_case("exact_4k", L"k4.bin", 1);
    cfs_case("4k_plus_1", L"odd.bin", 1);
    cfs_case("one_mib_plus_3", L"big.bin", 1);
    cfs_case("other_case", L"SMALL.BIN", 1);
    cfs_case("missing", L"nope.bin", 1);
    cfs_case("missing_no_high", L"nope.bin", 0);
    cfs_case("missing_dir", L"nodir\\x.bin", 1);
    cfs_case("empty_path", L"", 1);
    cfs_case("invalid_char", L"a<b", 1);
    cfs_case("wildcard", L"*.bin", 1);
    CreateDirectoryW(L"d", NULL);
    cfs_case("directory", L"d", 1);
    cfs_case("directory_trailing_backslash", L"d\\", 1);
    cfs_case("current_dir", L".", 1);
    cfs_case("nul_device", L"NUL", 1);

    /* Attributes don't matter. */
    SetFileAttributesW(L"small.bin", FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN);
    cfs_case("readonly_hidden", L"small.bin", 1);
    SetFileAttributesW(L"small.bin", FILE_ATTRIBUTE_NORMAL);

    /* Open handles. */
    {
        HANDLE h = CreateFileW(L"small.bin", GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
        cfs_case("open_exclusive", L"small.bin", 1);
        CloseHandle(h);
    }
    {
        HANDLE h = CreateFileW(L"grow.bin", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, 0,
                               NULL);
        DWORD w;
        WriteFile(h, "0123456789", 10, &w, NULL);
        cfs_case("open_after_write", L"grow.bin", 1);
        LARGE_INTEGER li;
        li.QuadPart = 300000;
        SetFilePointerEx(h, li, NULL, FILE_BEGIN);
        SetEndOfFile(h);
        cfs_case("open_after_extend", L"grow.bin", 1);
        CloseHandle(h);
        cfs_case("closed_after_extend", L"grow.bin", 1);
    }

    /* Sparse file: a large hole contributes nothing. */
    {
        HANDLE h = CreateFileW(L"sparse.bin", GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        DWORD ret = 0;
        BOOL ok;
        ORACLE_CALL(ok, DeviceIoControl(h, FSCTL_SET_SPARSE, NULL, 0, NULL, 0, &ret, NULL));
        oracle_case("set_sparse");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        LARGE_INTEGER li;
        li.QuadPart = 64LL << 20;
        SetFilePointerEx(h, li, NULL, FILE_BEGIN);
        SetEndOfFile(h);
        CloseHandle(h);
        cfs_case("sparse_64mib_hole", L"sparse.bin", 1);
        oracle_case("sparse_attrs");
        o_hex32("attrs", GetFileAttributesW(L"sparse.bin"));
        oracle_case_end();

        h = CreateFileW(L"sparse.bin", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        li.QuadPart = 32LL << 20;
        SetFilePointerEx(h, li, NULL, FILE_BEGIN);
        static char blk[65536];
        memset(blk, 'y', sizeof blk);
        DWORD w;
        WriteFile(h, blk, sizeof blk, &w, NULL);
        CloseHandle(h);
        cfs_case("sparse_with_64k_data", L"sparse.bin", 1);
    }

    /* Probes: when data written into a sparse file is counted. */
    {
        static char blk[65536];
        memset(blk, 'w', sizeof blk);
        DWORD ret = 0, w;
        LARGE_INTEGER li;
        HANDLE h = CreateFileW(L"sp_flush.bin", GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        DeviceIoControl(h, FSCTL_SET_SPARSE, NULL, 0, NULL, 0, &ret, NULL);
        li.QuadPart = 64LL << 20;
        SetFilePointerEx(h, li, NULL, FILE_BEGIN);
        SetEndOfFile(h);
        li.QuadPart = 32LL << 20;
        SetFilePointerEx(h, li, NULL, FILE_BEGIN);
        WriteFile(h, blk, sizeof blk, &w, NULL);
        cfs_case("probe_sparse_inside_open", L"sp_flush.bin", 1);
        FlushFileBuffers(h);
        cfs_case("probe_sparse_inside_flushed", L"sp_flush.bin", 1);
        CloseHandle(h);
        cfs_case("probe_sparse_inside_closed", L"sp_flush.bin", 1);

        h = CreateFileW(L"sp_ext.bin", GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        DeviceIoControl(h, FSCTL_SET_SPARSE, NULL, 0, NULL, 0, &ret, NULL);
        li.QuadPart = 1LL << 20;
        SetFilePointerEx(h, li, NULL, FILE_BEGIN);
        WriteFile(h, blk, 4096, &w, NULL);
        cfs_case("probe_sparse_extending_4k_open", L"sp_ext.bin", 1);
        CloseHandle(h);
        cfs_case("probe_sparse_extending_4k_closed", L"sp_ext.bin", 1);

        h = CreateFileW(L"sp_start.bin", GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        DeviceIoControl(h, FSCTL_SET_SPARSE, NULL, 0, NULL, 0, &ret, NULL);
        WriteFile(h, blk, sizeof blk, &w, NULL);
        li.QuadPart = 64LL << 20;
        SetFilePointerEx(h, li, NULL, FILE_BEGIN);
        SetEndOfFile(h);
        CloseHandle(h);
        cfs_case("probe_sparse_data_then_extend", L"sp_start.bin", 1);

        h = CreateFileW(L"sp_small.bin", GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        DeviceIoControl(h, FSCTL_SET_SPARSE, NULL, 0, NULL, 0, &ret, NULL);
        WriteFile(h, blk, 5, &w, NULL);
        CloseHandle(h);
        cfs_case("probe_sparse_5_bytes", L"sp_small.bin", 1);

        h = CreateFileW(L"sp_mid.bin", GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        DeviceIoControl(h, FSCTL_SET_SPARSE, NULL, 0, NULL, 0, &ret, NULL);
        li.QuadPart = 1LL << 20;
        SetFilePointerEx(h, li, NULL, FILE_BEGIN);
        SetEndOfFile(h);
        li.QuadPart = 4096;
        SetFilePointerEx(h, li, NULL, FILE_BEGIN);
        WriteFile(h, blk, 4096, &w, NULL);
        CloseHandle(h);
        cfs_case("probe_sparse_inside_4k_at_4k", L"sp_mid.bin", 1);

        h = CreateFileW(L"sp_mid2.bin", GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        WriteFile(h, blk, sizeof blk, &w, NULL);
        li.QuadPart = 1LL << 20;
        SetFilePointerEx(h, li, NULL, FILE_BEGIN);
        SetEndOfFile(h);
        DeviceIoControl(h, FSCTL_SET_SPARSE, NULL, 0, NULL, 0, &ret, NULL);
        CloseHandle(h);
        cfs_case("probe_sparse_set_after_data", L"sp_mid2.bin", 1);
    }

    /* Over 4 GiB (sparse so no disk is used): the high DWORD is used. */
    {
        HANDLE h = CreateFileW(L"huge.bin", GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        DWORD ret = 0;
        DeviceIoControl(h, FSCTL_SET_SPARSE, NULL, 0, NULL, 0, &ret, NULL);
        LARGE_INTEGER li;
        li.QuadPart = (5LL << 32) - 65536;
        SetFilePointerEx(h, li, NULL, FILE_BEGIN);
        static char blk[65536 * 2];
        memset(blk, 'z', sizeof blk);
        DWORD w;
        WriteFile(h, blk, sizeof blk, &w, NULL);
        CloseHandle(h);
        cfs_case("sparse_tail_past_4gib", L"huge.bin", 1);
        WIN32_FILE_ATTRIBUTE_DATA fad;
        GetFileAttributesExW(L"huge.bin", GetFileExInfoStandard, &fad);
        oracle_case("huge_logical_size");
        o_u32("size_high", fad.nFileSizeHigh);
        o_u32("size_low", fad.nFileSizeLow);
        oracle_case_end();
        DeleteFileW(L"huge.bin");
    }

    {
        DWORD high = 0, low;
        ORACLE_CALL(low, GetCompressedFileSizeA("small.bin", &high));
        oracle_case("ansi_small");
        o_u32("low", low);
        o_u32("high", high);
        o_lasterr();
        oracle_case_end();
    }

    return oracle_end();
}
