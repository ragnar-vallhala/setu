/*
 * zlib allocator for libarchive in Wine's PE build
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

/* Wine's libs/zlib is built with Z_SOLO, so zlib has no default allocator
 * and inflateInit fails when zalloc is NULL. config.h renames libarchive's
 * inflateInit_ and inflateInit2_ calls to these wrappers, which supply
 * malloc-based allocators before calling zlib. This file does not include
 * config.h, so the names below are zlib's own. */

#include <stdlib.h>
#include <zlib.h>

static voidpf zalloc_cb(voidpf opaque, uInt items, uInt size)
{
    return calloc(items, size);
}

static void zfree_cb(voidpf opaque, voidpf address)
{
    free(address);
}

static void set_alloc(z_streamp strm)
{
    if (!strm->zalloc) strm->zalloc = zalloc_cb;
    if (!strm->zfree) strm->zfree = zfree_cb;
}

int libarchive_inflateInit_(z_streamp strm, const char *version, int stream_size)
{
    set_alloc(strm);
    return inflateInit_(strm, version, stream_size);
}

int libarchive_inflateInit2_(z_streamp strm, int window_bits, const char *version, int stream_size)
{
    set_alloc(strm);
    return inflateInit2_(strm, window_bits, version, stream_size);
}
