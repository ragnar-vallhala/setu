/*
 * Compressed folders: the libarchive backend
 *
 * Copyright 2026 The Setu Contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

/*
 * The formats zipfldr's own reader (archive.c: zip, tar, gzip) does not
 * handle are read with libarchive (libs/libarchive, a static library with
 * its decoders in libs/bzip2, libs/lzma, libs/zstd and Wine's libs/zlib):
 *  - archives: 7z, RAR 4 and RAR 5 (read only), Microsoft cabinet, cpio;
 *  - compressed streams: bzip2, xz, lzma, lzip, zstd and uuencoded data,
 *    which archive.c then reads as a tar archive or shows as one file.
 * Encrypted entries are listed but never decrypted.
 */

#include <stdarg.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "windef.h"
#include "winbase.h"
#include "winnls.h"
#include <archive.h>
#include <archive_entry.h>
#include "arclib.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(zipfldr);

/* bzip2 is built with BZ_NO_STDIO: it reports internal errors here. */
void bz_internal_error(int code)
{
    ERR("bzip2 internal error %d\n", code);
}

struct reader
{
    struct la_source src;
    LONGLONG pos;
    BYTE buf[65536];
};

static la_ssize_t read_cb(struct archive *a, void *data, const void **buffer)
{
    struct reader *r = data;
    LARGE_INTEGER pos;
    DWORD got;

    if (r->pos >= (LONGLONG)r->src.len) return 0;
    if (r->src.mem)
    {
        ULONGLONG n = min(r->src.len - r->pos, 1 << 20);
        *buffer = r->src.mem + r->pos;
        r->pos += n;
        return n;
    }
    pos.QuadPart = r->pos;
    if (!SetFilePointerEx(r->src.file, pos, NULL, FILE_BEGIN) || !ReadFile(r->src.file, r->buf, sizeof(r->buf), &got, NULL))
    {
        archive_set_error(a, EIO, "read error");
        return -1;
    }
    *buffer = r->buf;
    r->pos += got;
    return got;
}

static la_int64_t seek_cb(struct archive *a, void *data, la_int64_t offset, int whence)
{
    struct reader *r = data;
    LONGLONG pos;

    switch (whence)
    {
    case SEEK_SET: pos = offset; break;
    case SEEK_CUR: pos = r->pos + offset; break;
    case SEEK_END: pos = r->src.len + offset; break;
    default: return ARCHIVE_FATAL;
    }
    if (pos < 0) return ARCHIVE_FATAL;
    if (pos > (LONGLONG)r->src.len) pos = r->src.len;
    return r->pos = pos;
}

static la_int64_t skip_cb(struct archive *a, void *data, la_int64_t request)
{
    struct reader *r = data;
    LONGLONG left = r->src.len - r->pos;

    if (request > left) request = left;
    if (request < 0) return 0;
    r->pos += request;
    return request;
}

static struct archive *open_reader(const struct la_source *src, BOOL streams, struct reader **out)
{
    struct archive *a;
    struct reader *r;

    if (!(r = malloc(sizeof(*r)))) return NULL;
    r->src = *src;
    r->pos = 0;
    if (!(a = archive_read_new()))
    {
        free(r);
        return NULL;
    }
    if (streams)
    {
        archive_read_support_filter_bzip2(a);
        archive_read_support_filter_xz(a);
        archive_read_support_filter_lzma(a);
        archive_read_support_filter_lzip(a);
        archive_read_support_filter_zstd(a);
        archive_read_support_filter_uu(a);
        archive_read_support_format_raw(a);
    }
    else
    {
        archive_read_support_format_7zip(a);
        archive_read_support_format_rar(a);
        archive_read_support_format_rar5(a);
        archive_read_support_format_cab(a);
        archive_read_support_format_cpio(a);
    }
    archive_read_set_callback_data(a, r);
    archive_read_set_read_callback(a, read_cb);
    archive_read_set_seek_callback(a, seek_cb);
    archive_read_set_skip_callback(a, skip_cb);
    if (archive_read_open1(a) != ARCHIVE_OK)
    {
        TRACE("open: %s\n", debugstr_a(archive_error_string(a)));
        archive_read_free(a);
        free(r);
        return NULL;
    }
    *out = r;
    return a;
}

static void close_reader(struct archive *a, struct reader *r)
{
    archive_read_free(a);
    free(r);
}

static void unix_to_filetime(LONGLONG t, LONG nsec, FILETIME *ft)
{
    ULONGLONG v = (ULONGLONG)(t + 11644473600ll) * 10000000 + nsec / 100;
    ft->dwLowDateTime = (DWORD)v;
    ft->dwHighDateTime = (DWORD)(v >> 32);
}

static WCHAR *entry_path(struct archive_entry *e)
{
    const wchar_t *w = archive_entry_pathname_w(e);
    const char *s;
    WCHAR *out;
    int n;

    if (w) return wcsdup(w);
    if (!(s = archive_entry_pathname(e))) return NULL;
    if (!(n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, NULL, 0)))
    {
        n = MultiByteToWideChar(CP_ACP, 0, s, -1, NULL, 0);
        if (!(out = malloc(n * sizeof(WCHAR)))) return NULL;
        MultiByteToWideChar(CP_ACP, 0, s, -1, out, n);
        return out;
    }
    if (!(out = malloc(n * sizeof(WCHAR)))) return NULL;
    MultiByteToWideChar(CP_UTF8, 0, s, -1, out, n);
    return out;
}

/* Reads the next header; S_FALSE at the end. */
static HRESULT next_header(struct archive *a, struct archive_entry **e)
{
    int ret = archive_read_next_header(a, e);
    if (ret == ARCHIVE_EOF) return S_FALSE;
    if (ret == ARCHIVE_OK || ret == ARCHIVE_WARN) return S_OK;
    TRACE("header: %s\n", debugstr_a(archive_error_string(a)));
    return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
}

HRESULT la_list(const struct la_source *src, la_list_cb cb, void *ctx)
{
    struct archive_entry *e;
    struct reader *r;
    struct archive *a;
    ULONGLONG index;
    HRESULT hr;

    if (!(a = open_reader(src, FALSE, &r))) return S_FALSE;
    for (index = 0; (hr = next_header(a, &e)) == S_OK; index++)
    {
        struct la_entry entry;
        unsigned long set, clear;
        WCHAR *path;

        if (!(path = entry_path(e))) continue;
        memset(&entry, 0, sizeof(entry));
        entry.path = path;
        entry.dir = archive_entry_filetype(e) == AE_IFDIR;
        entry.link = archive_entry_filetype(e) == AE_IFLNK || archive_entry_hardlink(e);
        entry.size_known = archive_entry_size_is_set(e);
        entry.size = entry.size_known ? archive_entry_size(e) : 0;
        if (archive_entry_mtime_is_set(e))
            unix_to_filetime(archive_entry_mtime(e), archive_entry_mtime_nsec(e), &entry.mtime);
        archive_entry_fflags(e, &set, &clear);
        entry.attrs = set & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
        entry.encrypted = archive_entry_is_encrypted(e);
        if (!cb(ctx, index, &entry)) hr = E_OUTOFMEMORY;
        free(path);
        if (hr != S_OK) break;
    }
    /* nothing recognised: not one of our formats */
    if (FAILED(hr) && !index && !archive_format(a)) hr = S_FALSE;
    else if (hr == S_FALSE) hr = S_OK;
    TRACE("format %#x (%s), %s entries, hr %#lx\n", archive_format(a), debugstr_a(archive_format_name(a)),
          wine_dbgstr_longlong(index), hr);
    close_reader(a, r);
    return hr;
}

/* Reads the current entry's data, up to max bytes. */
static HRESULT read_data(struct archive *a, struct archive_entry *e, SIZE_T max, BYTE **out, SIZE_T *out_len)
{
    SIZE_T cap, len = 0;
    BYTE *buf;

    if (archive_entry_is_data_encrypted(e)) return HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED);
    if (archive_entry_size_is_set(e))
    {
        if ((ULONGLONG)archive_entry_size(e) >= max) return E_OUTOFMEMORY;
        cap = archive_entry_size(e) + 1;
    }
    else cap = 65536;
    if (!(buf = malloc(cap))) return E_OUTOFMEMORY;
    for (;;)
    {
        la_ssize_t n;
        if (len == cap)
        {
            BYTE *nbuf;
            if (cap >= max || !(nbuf = realloc(buf, cap * 2)))
            {
                free(buf);
                return E_OUTOFMEMORY;
            }
            buf = nbuf;
            cap *= 2;
        }
        n = archive_read_data(a, buf + len, cap - len);
        if (n < 0)
        {
            TRACE("data: %s\n", debugstr_a(archive_error_string(a)));
            free(buf);
            return archive_entry_is_encrypted(e) ? HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED)
                                                  : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        if (!n) break;
        len += n;
    }
    *out = buf;
    *out_len = len;
    return S_OK;
}

HRESULT la_decompress(const struct la_source *src, SIZE_T max, BYTE **out, SIZE_T *len)
{
    struct archive_entry *e;
    struct reader *r;
    struct archive *a;
    HRESULT hr;

    if (!(a = open_reader(src, TRUE, &r))) return S_FALSE;
    if (archive_filter_count(a) < 2 && archive_filter_code(a, 0) == ARCHIVE_FILTER_NONE) hr = S_FALSE;
    else if ((hr = next_header(a, &e)) == S_OK) hr = read_data(a, e, max, out, len);
    else hr = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    TRACE("filter %s, hr %#lx\n", debugstr_a(archive_filter_name(a, 0)), hr);
    close_reader(a, r);
    return hr;
}

HRESULT la_read(const struct la_source *src, ULONGLONG index, SIZE_T max, BYTE **data, SIZE_T *len)
{
    struct archive_entry *e;
    struct reader *r;
    struct archive *a;
    ULONGLONG i;
    HRESULT hr;

    if (!(a = open_reader(src, FALSE, &r))) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    for (i = 0; (hr = next_header(a, &e)) == S_OK && i < index; i++) ;
    if (hr == S_OK) hr = read_data(a, e, max, data, len);
    else if (hr == S_FALSE) hr = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    close_reader(a, r);
    return hr;
}

static HRESULT write_entry(struct archive *a, struct archive_entry *e, const WCHAR *path, DWORD attrs, const FILETIME *mtime)
{
    const void *buf;
    size_t size;
    la_int64_t offset;
    HANDLE file;
    HRESULT hr = S_OK;
    int ret;

    if (archive_entry_is_data_encrypted(e)) return HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED);
    file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                       (attrs & FILE_ATTRIBUTE_HIDDEN) ? FILE_ATTRIBUTE_HIDDEN : FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return HRESULT_FROM_WIN32(GetLastError());
    while ((ret = archive_read_data_block(a, &buf, &size, &offset)) == ARCHIVE_OK || ret == ARCHIVE_WARN)
    {
        LARGE_INTEGER pos;
        DWORD written;
        pos.QuadPart = offset;
        while (size)
        {
            DWORD chunk = size > 0x10000000 ? 0x10000000 : (DWORD)size;
            if (!SetFilePointerEx(file, pos, NULL, FILE_BEGIN) || !WriteFile(file, buf, chunk, &written, NULL))
            {
                hr = HRESULT_FROM_WIN32(GetLastError());
                break;
            }
            buf = (const BYTE *)buf + chunk;
            size -= chunk;
            pos.QuadPart += chunk;
        }
        if (FAILED(hr)) break;
    }
    if (SUCCEEDED(hr) && ret != ARCHIVE_EOF)
        hr = archive_entry_is_encrypted(e) ? HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED) : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    if (SUCCEEDED(hr))
    {
        /* sparse entries: the size includes the trailing hole */
        if (archive_entry_size_is_set(e))
        {
            LARGE_INTEGER end;
            end.QuadPart = archive_entry_size(e);
            if (SetFilePointerEx(file, end, NULL, FILE_BEGIN)) SetEndOfFile(file);
        }
        if (mtime->dwLowDateTime || mtime->dwHighDateTime) SetFileTime(file, NULL, NULL, mtime);
    }
    CloseHandle(file);
    return hr;
}

HRESULT la_extract(const struct la_source *src, la_extract_cb cb, void *ctx)
{
    struct archive_entry *e;
    struct reader *r;
    struct archive *a;
    ULONGLONG index;
    HRESULT hr;

    if (!(a = open_reader(src, FALSE, &r))) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    for (index = 0; (hr = next_header(a, &e)) == S_OK; index++)
    {
        WCHAR path[MAX_PATH];
        FILETIME mtime;
        DWORD attrs;

        if (archive_entry_filetype(e) == AE_IFDIR) continue;
        if (!cb(ctx, index, path, MAX_PATH, &attrs, &mtime)) continue;
        if (FAILED(hr = write_entry(a, e, path, attrs, &mtime))) break;
    }
    if (hr == S_FALSE) hr = S_OK;
    close_reader(a, r);
    return hr;
}
