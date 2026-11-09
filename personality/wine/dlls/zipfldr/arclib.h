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

#ifndef __ZIPFLDR_ARCLIB_H
#define __ZIPFLDR_ARCLIB_H

/* This header does not use zipfldr's struct archive, whose name clashes
 * with libarchive's: arclib.c includes only this one. */

/* An archive file (file != NULL) or a decompressed copy in memory. */
struct la_source
{
    HANDLE file;
    const BYTE *mem;
    ULONGLONG len;
};

struct la_entry
{
    const WCHAR *path;
    BOOL dir;
    BOOL link;               /* symbolic or hard link: listed without data */
    BOOL size_known;
    ULONGLONG size;
    FILETIME mtime;          /* zero when the archive has none */
    DWORD attrs;             /* FILE_ATTRIBUTE_READONLY/HIDDEN/SYSTEM */
    BOOL encrypted;
};

/* Called for every entry in archive order; index counts from 0. */
typedef BOOL (*la_list_cb)(void *ctx, ULONGLONG index, const struct la_entry *entry);

/* 7z, rar (RAR 4 and 5), cab and cpio. S_FALSE: not one of these. */
HRESULT la_list(const struct la_source *src, la_list_cb cb, void *ctx);

/* A bzip2, xz, lzma, lzip, zstd or uuencoded stream, decompressed.
 * S_FALSE: the file is none of these. */
HRESULT la_decompress(const struct la_source *src, SIZE_T max, BYTE **out, SIZE_T *len);

/* The data of entry number index. */
HRESULT la_read(const struct la_source *src, ULONGLONG index, SIZE_T max, BYTE **data, SIZE_T *len);

/* Extraction in one pass: for each file entry, cb gives the path to write
 * it to (FALSE to skip it); the file gets the entry's time. */
typedef BOOL (*la_extract_cb)(void *ctx, ULONGLONG index, WCHAR *path, DWORD len, DWORD *attrs, FILETIME *mtime);
HRESULT la_extract(const struct la_source *src, la_extract_cb cb, void *ctx);

#endif
