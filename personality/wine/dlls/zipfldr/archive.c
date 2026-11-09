/*
 * Compressed folders: reading archives
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
 * Formats, from their public specifications:
 *  - zip: PKWARE APPNOTE.TXT (local and central headers, the end of
 *    central directory record and its zip64 form, extra fields 0x0001,
 *    0x5455 and 0x7075); methods 0 (stored) and 8 (deflate).
 *  - tar: POSIX ustar, GNU long names ('L'), pax extended headers ('x').
 *  - gzip: RFC 1952 (one or more members), deflate: RFC 1951, both through
 *    zlib. A gzip file holding a tar archive is read as the tar archive;
 *    any other gzip file is shown as the one file it contains.
 * Everything else goes through libarchive (arclib.c): 7z, rar, cab and cpio
 * archives are listed and read with it; bzip2, xz, zstd and uuencoded
 * streams are decompressed with it and then treated like gzip (a tar
 * archive inside, or else one file).
 */

#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include "zipfldr_private.h"
#include "arclib.h"
#include "shlwapi.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(zipfldr);

#define MAX_DECOMPRESSED ((SIZE_T)1 << 31)

/* zlib is built with Z_SOLO: it needs our allocator */
static voidpf zip_zalloc(voidpf opaque, uInt items, uInt size)
{
    return calloc(items, size);
}

static void zip_zfree(voidpf opaque, voidpf address)
{
    free(address);
}

static void zstream_init(z_stream *z)
{
    memset(z, 0, sizeof(*z));
    z->zalloc = zip_zalloc;
    z->zfree = zip_zfree;
}

static CRITICAL_SECTION cache_cs;
static CRITICAL_SECTION_DEBUG cache_cs_debug =
{
    0, 0, &cache_cs,
    { &cache_cs_debug.ProcessLocksList, &cache_cs_debug.ProcessLocksList },
      0, 0, { (DWORD_PTR)(__FILE__ ": cache_cs") }
};
static CRITICAL_SECTION cache_cs = { &cache_cs_debug, -1, 0, 0, 0, 0 };

/* A few recently read archives, so that walking a long PIDL does not read
 * the archive again for every level. */
#define CACHE_SIZE 4
static struct archive *cache[CACHE_SIZE];

/* ---- tree ---- */

static void free_nodes(struct arc_node *node)
{
    struct arc_node *c = node->child, *next;
    while (c)
    {
        next = c->next;
        free_nodes(c);
        free(c->name);
        free(c);
        c = next;
    }
    node->child = node->last_child = NULL;
}

struct arc_node *archive_find(struct archive *arc, struct arc_node *dir, const WCHAR *name)
{
    struct arc_node *c;
    if (!dir) dir = &arc->root;
    for (c = dir->child; c; c = c->next)
        if (!wcsicmp(c->name, name)) return c;
    return NULL;
}

BOOL node_has_subdirs(const struct arc_node *node)
{
    const struct arc_node *c;
    for (c = node->child; c; c = c->next)
        if (c->dir) return TRUE;
    return FALSE;
}

static struct arc_node *add_child(struct arc_node *dir, const WCHAR *name, int len)
{
    struct arc_node *n = calloc(1, sizeof(*n));
    if (!n) return NULL;
    if (!(n->name = malloc((len + 1) * sizeof(WCHAR))))
    {
        free(n);
        return NULL;
    }
    memcpy(n->name, name, len * sizeof(WCHAR));
    n->name[len] = 0;
    n->parent = dir;
    if (dir->last_child) dir->last_child->next = n;
    else dir->child = n;
    dir->last_child = n;
    return n;
}

/* Adds an entry with a '/' or '\' separated path. Missing parent
 * directories are created; "." and ".." components are dropped, so that no
 * entry can point outside the archive. */
static struct arc_node *add_entry(struct archive *arc, const WCHAR *path, BOOL dir)
{
    struct arc_node *cur = &arc->root, *found;
    const WCHAR *p = path, *start;
    int len;

    for (;;)
    {
        while (*p == '/' || *p == '\\') p++;
        if (!*p) break;
        start = p;
        while (*p && *p != '/' && *p != '\\') p++;
        len = p - start;
        while (*p == '/' || *p == '\\') p++;
        if ((len == 1 && start[0] == '.') || (len == 2 && start[0] == '.' && start[1] == '.')) continue;

        for (found = cur->child; found; found = found->next)
            if (!wcsnicmp(found->name, start, len) && !found->name[len]) break;
        if (!found)
        {
            if (!(found = add_child(cur, start, len))) return NULL;
            found->dir = !!*p || dir;
            if (found->dir) found->attrs = FILE_ATTRIBUTE_DIRECTORY;
        }
        else if (*p && !found->dir)
            return NULL;  /* a file used as a directory: ignore the entry */
        cur = found;
        if (!*p)
        {
            if (dir && cur->dir) cur->explicit_dir = TRUE;
            return cur;
        }
    }
    return NULL;
}

/* A file of a flat archive: a child of the root named with its whole path,
 * '\' separated, without "." and ".." components. */
static struct arc_node *add_flat(struct archive *arc, const WCHAR *path)
{
    WCHAR name[MAX_PATH];
    const WCHAR *p = path, *start;
    int len, n = 0;

    for (;;)
    {
        while (*p == '/' || *p == '\\') p++;
        if (!*p) break;
        start = p;
        while (*p && *p != '/' && *p != '\\') p++;
        len = p - start;
        if ((len == 1 && start[0] == '.') || (len == 2 && start[0] == '.' && start[1] == '.')) continue;
        if (n + len + 2 > MAX_PATH) return NULL;
        if (n) name[n++] = '\\';
        memcpy(name + n, start, len * sizeof(WCHAR));
        n += len;
    }
    return n ? add_child(&arc->root, name, n) : NULL;
}

static int __cdecl compare_nodes(const void *a, const void *b)
{
    const struct arc_node *x = *(const struct arc_node *const *)a, *y = *(const struct arc_node *const *)b;
    return lstrcmpiW(x->name, y->name);
}

static void sort_children(struct arc_node *dir)
{
    struct arc_node *c, **list;
    unsigned int n = 0, i;

    for (c = dir->child; c; c = c->next) n++;
    if (n > 1 && (list = malloc(n * sizeof(*list))))
    {
        for (c = dir->child, i = 0; c; c = c->next) list[i++] = c;
        qsort(list, n, sizeof(*list), compare_nodes);
        dir->child = list[0];
        for (i = 0; i + 1 < n; i++) list[i]->next = list[i + 1];
        list[n - 1]->next = NULL;
        dir->last_child = list[n - 1];
        free(list);
    }
    for (c = dir->child; c; c = c->next)
        if (c->dir) sort_children(c);
}

/* ---- reading the file ---- */

struct source
{
    HANDLE file;
    const BYTE *mem;
    ULONGLONG len;
};

static BOOL src_read(const struct source *src, ULONGLONG offset, void *buf, DWORD len)
{
    LARGE_INTEGER pos;
    DWORD got;

    if (offset > src->len || len > src->len - offset) return FALSE;
    if (src->mem)
    {
        memcpy(buf, src->mem + offset, len);
        return TRUE;
    }
    pos.QuadPart = offset;
    if (!SetFilePointerEx(src->file, pos, NULL, FILE_BEGIN)) return FALSE;
    return ReadFile(src->file, buf, len, &got, NULL) && got == len;
}

static WORD get16(const BYTE *p) { return p[0] | (p[1] << 8); }
static DWORD get32(const BYTE *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((DWORD)p[3] << 24); }
static ULONGLONG get64(const BYTE *p) { return get32(p) | ((ULONGLONG)get32(p + 4) << 32); }

static void unix_to_filetime(LONGLONG t, FILETIME *ft)
{
    ULONGLONG v = (ULONGLONG)(t + 11644473600ll) * 10000000;
    ft->dwLowDateTime = (DWORD)v;
    ft->dwHighDateTime = (DWORD)(v >> 32);
}

static WCHAR *decode_name(const char *s, int len, UINT cp)
{
    int n = MultiByteToWideChar(cp, cp == CP_UTF8 ? MB_ERR_INVALID_CHARS : 0, s, len, NULL, 0);
    WCHAR *w;

    if (!n && cp == CP_UTF8) return decode_name(s, len, CP_ACP);
    if (!(w = malloc((n + 1) * sizeof(WCHAR)))) return NULL;
    MultiByteToWideChar(cp, 0, s, len, w, n);
    w[n] = 0;
    return w;
}

/* ---- zip ---- */

static BOOL zip_find_end(const struct source *src, ULONGLONG *cd_offset, ULONGLONG *cd_size, ULONGLONG *count)
{
    BYTE buf[65536 + 22], loc[20], end64[56];
    DWORD len = src->len < sizeof(buf) ? (DWORD)src->len : sizeof(buf);
    ULONGLONG start = src->len - len;
    int i;

    if (len < 22 || !src_read(src, start, buf, len)) return FALSE;
    for (i = len - 22; i >= 0; i--)
    {
        if (get32(buf + i) != 0x06054b50) continue;
        *count = get16(buf + i + 10);
        *cd_size = get32(buf + i + 12);
        *cd_offset = get32(buf + i + 16);
        if ((*count == 0xffff || *cd_size == 0xffffffff || *cd_offset == 0xffffffff) && start + i >= 20
            && src_read(src, start + i - 20, loc, 20) && get32(loc) == 0x07064b50
            && src_read(src, get64(loc + 8), end64, 56) && get32(end64) == 0x06064b50)
        {
            *count = get64(end64 + 32);
            *cd_size = get64(end64 + 40);
            *cd_offset = get64(end64 + 48);
        }
        return TRUE;
    }
    return FALSE;
}

static void dos_to_filetime(WORD date, WORD time, FILETIME *ft)
{
    FILETIME local;
    if (!DosDateTimeToFileTime(date, time, &local) || !LocalFileTimeToFileTime(&local, ft))
        ft->dwLowDateTime = ft->dwHighDateTime = 0;
}

static BOOL read_zip(struct archive *arc, const struct source *src)
{
    ULONGLONG cd_offset, cd_size, count, pos, end, i;
    BYTE hdr[46];

    if (!zip_find_end(src, &cd_offset, &cd_size, &count)) return FALSE;
    arc->format = ARC_ZIP;
    pos = cd_offset;
    end = cd_offset + cd_size;
    for (i = 0; i < count && pos + 46 <= end; i++)
    {
        WORD name_len, extra_len, comment_len, flags, made_by;
        ULONGLONG size, csize, offset;
        DWORD ext_attrs;
        char *name_buf;
        BYTE *extra;
        WCHAR *name = NULL;
        struct arc_node *node;
        FILETIME mtime;
        BOOL dir;
        DWORD j;

        if (!src_read(src, pos, hdr, 46) || get32(hdr) != 0x02014b50) return FALSE;
        made_by = get16(hdr + 4) >> 8;
        flags = get16(hdr + 8);
        csize = get32(hdr + 20);
        size = get32(hdr + 24);
        name_len = get16(hdr + 28);
        extra_len = get16(hdr + 30);
        comment_len = get16(hdr + 32);
        ext_attrs = get32(hdr + 38);
        offset = get32(hdr + 42);
        dos_to_filetime(get16(hdr + 14), get16(hdr + 12), &mtime);

        if (!(name_buf = malloc(name_len + extra_len + 1))) return FALSE;
        if (!src_read(src, pos + 46, name_buf, name_len + extra_len))
        {
            free(name_buf);
            return FALSE;
        }
        extra = (BYTE *)name_buf + name_len;
        for (j = 0; j + 4 <= extra_len; )
        {
            WORD id = get16(extra + j), sz = get16(extra + j + 2);
            const BYTE *d = extra + j + 4;
            if (j + 4 + sz > extra_len) break;
            if (id == 0x0001)  /* zip64 */
            {
                int k = 0;
                if (size == 0xffffffff && k + 8 <= sz) { size = get64(d + k); k += 8; }
                if (csize == 0xffffffff && k + 8 <= sz) { csize = get64(d + k); k += 8; }
                if (offset == 0xffffffff && k + 8 <= sz) { offset = get64(d + k); k += 8; }
            }
            else if (id == 0x5455 && sz >= 5 && (d[0] & 1))  /* extended timestamp, UTC */
                unix_to_filetime((LONG)get32(d + 1), &mtime);
            else if (id == 0x7075 && sz > 5 && d[0] == 1 && !name)  /* Info-ZIP Unicode path */
                name = decode_name((const char *)d + 5, sz - 5, CP_UTF8);
            j += 4 + sz;
        }
        if (!name) name = decode_name(name_buf, name_len, (flags & 0x800) ? CP_UTF8 : CP_OEMCP);
        free(name_buf);
        if (!name) return FALSE;

        dir = name_len && (name[wcslen(name) - 1] == '/' || name[wcslen(name) - 1] == '\\');
        if (made_by == 3 && ((ext_attrs >> 16) & 0170000) == 0040000) dir = TRUE;
        else if (made_by != 3 && (ext_attrs & FILE_ATTRIBUTE_DIRECTORY)) dir = TRUE;

        if ((node = add_entry(arc, name, dir)))
        {
            node->mtime = mtime;
            if (made_by != 3) node->attrs = (ext_attrs & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_SYSTEM));
            if (dir) node->attrs |= FILE_ATTRIBUTE_DIRECTORY;
            if (!dir)
            {
                node->size = size;
                node->csize = csize;
                node->offset = offset;
                node->method = get16(hdr + 10);
                node->flags = flags;
            }
        }
        free(name);
        pos += 46 + name_len + extra_len + comment_len;
    }
    return i == count;
}

static HRESULT inflate_raw(const BYTE *in, SIZE_T in_len, BYTE *out, SIZE_T out_len)
{
    z_stream z;
    int ret;

    zstream_init(&z);
    if (inflateInit2(&z, -MAX_WBITS) != Z_OK) return E_OUTOFMEMORY;
    z.next_in = (BYTE *)in;
    z.next_out = out;
    ret = Z_OK;
    /* zlib counts in uInt: feed large entries in pieces. */
    while (ret == Z_OK)
    {
        SIZE_T in_left = in_len - (z.next_in - in), out_left = out_len - (z.next_out - out);
        z.avail_in = in_left > 0x40000000 ? 0x40000000 : (uInt)in_left;
        z.avail_out = out_left > 0x40000000 ? 0x40000000 : (uInt)out_left;
        if (!z.avail_out) break;
        ret = inflate(&z, Z_NO_FLUSH);
        if (ret == Z_BUF_ERROR && !z.avail_in) break;
    }
    inflateEnd(&z);
    if ((SIZE_T)(z.next_out - out) != out_len || (ret != Z_STREAM_END && ret != Z_OK && ret != Z_BUF_ERROR))
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    return S_OK;
}

static HRESULT zip_read_entry(const struct source *src, const struct arc_node *node, BYTE **data, SIZE_T *len)
{
    BYTE hdr[30], *in = NULL, *out;
    ULONGLONG start;
    HRESULT hr;

    if (node->flags & 1) return HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED);  /* encrypted */
    if (node->method != 0 && node->method != 8) return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
    if (node->size >= MAX_DECOMPRESSED || node->csize >= MAX_DECOMPRESSED) return E_OUTOFMEMORY;
    if (!src_read(src, node->offset, hdr, 30) || get32(hdr) != 0x04034b50)
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    start = node->offset + 30 + get16(hdr + 26) + get16(hdr + 28);

    if (!(out = malloc(node->size ? node->size : 1))) return E_OUTOFMEMORY;
    if (node->method == 0)
    {
        if (node->size && !src_read(src, start, out, (DWORD)node->size))
        {
            free(out);
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
    }
    else
    {
        if (!(in = malloc(node->csize ? node->csize : 1)) || (node->csize && !src_read(src, start, in, (DWORD)node->csize)))
        {
            free(in);
            free(out);
            return in ? HRESULT_FROM_WIN32(ERROR_INVALID_DATA) : E_OUTOFMEMORY;
        }
        hr = inflate_raw(in, node->csize, out, node->size);
        free(in);
        if (FAILED(hr))
        {
            free(out);
            return hr;
        }
    }
    *data = out;
    *len = node->size;
    return S_OK;
}

/* ---- tar ---- */

static ULONGLONG tar_number(const char *p, int len)
{
    ULONGLONG v = 0;
    int i = 0;

    if ((unsigned char)p[0] & 0x80)  /* base-256 */
    {
        v = p[0] & 0x7f;
        for (i = 1; i < len; i++) v = (v << 8) | (unsigned char)p[i];
        return v;
    }
    while (i < len && (p[i] == ' ' || p[i] == 0)) i++;
    for (; i < len && p[i] >= '0' && p[i] <= '7'; i++) v = v * 8 + (p[i] - '0');
    return v;
}

static BOOL tar_header_ok(const BYTE *h)
{
    unsigned int sum = 0, i;
    for (i = 0; i < 512; i++) sum += (i >= 148 && i < 156) ? ' ' : h[i];
    return sum == tar_number((const char *)h + 148, 8);
}

static BOOL is_tar(const struct source *src)
{
    BYTE h[512];
    return src_read(src, 0, h, 512) && (!memcmp(h + 257, "ustar", 5) || (h[0] && tar_header_ok(h)));
}

/* pax extended header records: "<len> key=value\n" */
static void pax_parse(const char *p, SIZE_T len, char **path, ULONGLONG *size, LONGLONG *mtime)
{
    SIZE_T pos = 0;
    while (pos < len)
    {
        SIZE_T rec = 0, i = pos;
        const char *kv, *eq;
        while (i < len && p[i] >= '0' && p[i] <= '9') rec = rec * 10 + (p[i++] - '0');
        if (!rec || pos + rec > len || i >= len || p[i] != ' ') break;
        kv = p + i + 1;
        eq = memchr(kv, '=', p + pos + rec - kv);
        if (eq)
        {
            SIZE_T klen = eq - kv, vlen = p + pos + rec - 1 - (eq + 1);
            if (klen == 4 && !memcmp(kv, "path", 4))
            {
                free(*path);
                if ((*path = malloc(vlen + 1))) { memcpy(*path, eq + 1, vlen); (*path)[vlen] = 0; }
            }
            else if (klen == 4 && !memcmp(kv, "size", 4)) *size = _strtoui64(eq + 1, NULL, 10);
            else if (klen == 5 && !memcmp(kv, "mtime", 5)) *mtime = _strtoi64(eq + 1, NULL, 10);
        }
        pos += rec;
    }
}

static BOOL read_tar(struct archive *arc, const struct source *src)
{
    ULONGLONG pos = 0, pax_size = ~0ull;
    LONGLONG pax_mtime = -1;
    char *long_name = NULL, *pax_path = NULL;
    BYTE h[512];
    BOOL ok = TRUE;

    arc->format = ARC_TAR;
    while (pos + 512 <= src->len)
    {
        ULONGLONG size, data;
        char type, *name, full[257];
        struct arc_node *node;
        WCHAR *wname;
        BOOL dir;

        if (!src_read(src, pos, h, 512)) { ok = FALSE; break; }
        if (!h[0]) break;  /* end of archive */
        if (!tar_header_ok(h)) { ok = FALSE; break; }
        type = h[156];
        size = tar_number((const char *)h + 124, 12);
        data = pos + 512;
        pos = data + ((size + 511) & ~(ULONGLONG)511);

        if (type == 'L' || type == 'x')
        {
            char *buf;
            if (size > (1 << 20) || !(buf = malloc(size + 1)) || !src_read(src, data, buf, (DWORD)size)) { ok = FALSE; break; }
            buf[size] = 0;
            if (type == 'L')
            {
                free(long_name);
                long_name = buf;
            }
            else
            {
                pax_parse(buf, size, &pax_path, &pax_size, &pax_mtime);
                free(buf);
            }
            continue;
        }
        if (type == 'g' || type == 'K' || type == 'V') continue;

        if (pax_path) name = pax_path;
        else if (long_name) name = long_name;
        else
        {
            int nlen = strnlen((const char *)h, 100), plen = strnlen((const char *)h + 345, 155);
            if (!memcmp(h + 257, "ustar", 5) && plen)
            {
                memcpy(full, h + 345, plen);
                full[plen] = '/';
                memcpy(full + plen + 1, h, nlen);
                full[plen + 1 + nlen] = 0;
            }
            else
            {
                memcpy(full, h, nlen);
                full[nlen] = 0;
            }
            name = full;
        }
        if (pax_size != ~0ull) size = pax_size;

        dir = type == '5' || (*name && name[strlen(name) - 1] == '/');
        if ((type == '0' || type == 0 || type == '7' || type == '1' || type == '2' || dir)
            && (wname = decode_name(name, -1, CP_UTF8)))
        {
            if ((node = add_entry(arc, wname, dir)))
            {
                unix_to_filetime(pax_mtime >= 0 ? pax_mtime : (LONGLONG)tar_number((const char *)h + 136, 12), &node->mtime);
                if (!dir && (type == '0' || type == 0 || type == '7'))
                {
                    node->size = size;
                    node->offset = data;
                }
            }
            free(wname);
        }
        free(long_name);
        free(pax_path);
        long_name = pax_path = NULL;
        pax_size = ~0ull;
        pax_mtime = -1;
    }
    free(long_name);
    free(pax_path);
    return ok;
}

/* ---- gzip ---- */

static BOOL gunzip(HANDLE file, BYTE **out, SIZE_T *out_len, char **fname)
{
    BYTE in[65536];
    SIZE_T cap = 65536, len = 0;
    BYTE *buf = malloc(cap);
    z_stream z;
    DWORD got;
    int ret = Z_OK;
    BOOL header = TRUE;

    if (!buf) return FALSE;
    zstream_init(&z);
    if (inflateInit2(&z, MAX_WBITS + 16) != Z_OK)
    {
        free(buf);
        return FALSE;
    }
    SetFilePointer(file, 0, NULL, FILE_BEGIN);
    if (!ReadFile(file, in, sizeof(in), &got, NULL) || got < 18 || in[0] != 0x1f || in[1] != 0x8b)
        goto fail;
    if (fname && (in[3] & 8))  /* FNAME, after FEXTRA */
    {
        DWORD p = 10, e;
        if (in[3] & 4) p += 2 + get16(in + 10);
        for (e = p; e < got && in[e]; e++) ;
        if (e < got && (*fname = malloc(e - p + 1)))
        {
            memcpy(*fname, in + p, e - p);
            (*fname)[e - p] = 0;
        }
    }
    z.next_in = in;
    z.avail_in = got;
    for (;;)
    {
        if (!z.avail_in)
        {
            if (!ReadFile(file, in, sizeof(in), &got, NULL)) goto fail;
            if (!got) break;
            z.next_in = in;
            z.avail_in = got;
        }
        if (len == cap)
        {
            BYTE *n;
            if (cap >= MAX_DECOMPRESSED || !(n = realloc(buf, cap * 2))) goto fail;
            buf = n;
            cap *= 2;
        }
        z.next_out = buf + len;
        z.avail_out = (uInt)min(cap - len, 0x40000000);
        ret = inflate(&z, Z_NO_FLUSH);
        len = z.next_out - buf;
        if (ret == Z_STREAM_END)
        {
            header = FALSE;
            /* another member may follow */
            if (!z.avail_in && (!ReadFile(file, in, sizeof(in), &got, NULL) || !got)) break;
            if (!z.avail_in) { z.next_in = in; z.avail_in = got; }
            if (z.avail_in < 2 || z.next_in[0] != 0x1f || z.next_in[1] != 0x8b) break;
            inflateReset(&z);
            header = TRUE;
            continue;
        }
        if (ret != Z_OK) goto fail;
    }
    if (header && ret != Z_STREAM_END) goto fail;
    inflateEnd(&z);
    *out = buf;
    *out_len = len;
    return TRUE;
fail:
    inflateEnd(&z);
    free(buf);
    return FALSE;
}

/* ---- libarchive ---- */

static BOOL lib_entry(void *ctx, ULONGLONG index, const struct la_entry *e)
{
    struct archive *arc = ctx;
    struct arc_node *node;

    if (index >= arc->entry_count)
    {
        ULONGLONG count = max(max(64, arc->entry_count * 2), index + 1);
        struct arc_node **entries;
        if (count > MAX_DECOMPRESSED / sizeof(*entries) || !(entries = realloc(arc->entries, count * sizeof(*entries))))
            return FALSE;
        memset(entries + arc->entry_count, 0, (count - arc->entry_count) * sizeof(*entries));
        arc->entries = entries;
        arc->entry_count = count;
    }
    if (arc->flat)
    {
        if (e->dir || !(node = add_flat(arc, e->path))) return TRUE;
    }
    else if (!(node = add_entry(arc, e->path, e->dir)) || node->dir != e->dir) return TRUE;
    if (e->mtime.dwLowDateTime || e->mtime.dwHighDateTime) node->mtime = e->mtime;
    node->attrs = e->attrs | (node->dir ? FILE_ATTRIBUTE_DIRECTORY : 0);
    if (node->dir) return TRUE;
    arc->entries[index] = node;
    node->offset = index;
    node->size = e->link ? 0 : e->size;
    node->method = e->link;    /* no data */
    node->flags = e->encrypted ? 1 : 0;
    return TRUE;
}

/* 7z, rar, cab or cpio: FALSE if the archive is broken, S_FALSE in *hr if
 * it is none of these. */
static BOOL read_lib(struct archive *arc, const struct source *src, HRESULT *hr)
{
    struct la_source ls = { src->mem ? NULL : src->file, src->mem, src->len };

    *hr = la_list(&ls, lib_entry, arc);
    if (*hr == S_FALSE) return FALSE;
    arc->format = ARC_LIB;
    return *hr == S_OK;
}

/* ---- archives ---- */

/* arc->data holds a decompressed stream: a tar archive, another archive, or
 * one file named in the stream's header (fname) or after the archive. */
static BOOL read_stream(struct archive *arc, HANDLE file, char *fname)
{
    struct source src = { NULL, arc->data, arc->data_len };
    HRESULT hr;

    if (is_tar(&src))
    {
        free(fname);
        return read_tar(arc, &src);
    }
    if (read_lib(arc, &src, &hr) || hr != S_FALSE)
    {
        free(fname);
        return SUCCEEDED(hr);
    }
    else
    {
        /* one file: the name stored in the header, or the archive's name
         * without its extension (".gz", ".bz2", ...) */
        WCHAR name[MAX_PATH], *w = NULL;
        struct arc_node *node;

        arc->format = ARC_GZ;
        if (fname && *fname && (w = decode_name(fname, -1, CP_ACP)))
            archive_sanitize_name(w, name, MAX_PATH);
        else
        {
            lstrcpynW(name, PathFindFileNameW(arc->path), MAX_PATH);
            PathRemoveExtensionW(name);
        }
        free(w);
        free(fname);
        if (!(node = add_entry(arc, name, FALSE))) return FALSE;
        node->size = arc->data_len;
        node->offset = 0;
        GetFileTime(file, NULL, NULL, &node->mtime);
        return TRUE;
    }
}

static BOOL read_archive(struct archive *arc, HANDLE file)
{
    struct source src = { file, NULL, arc->file_size };
    struct la_source ls = { file, NULL, arc->file_size };
    BYTE magic[4] = {0};
    HRESULT hr;

    src_read(&src, 0, magic, 4);
    if (magic[0] == 0x1f && magic[1] == 0x8b)
    {
        char *fname = NULL;
        if (!gunzip(file, &arc->data, &arc->data_len, &fname)) return FALSE;
        return read_stream(arc, file, fname);
    }
    if (read_zip(arc, &src)) return TRUE;
    free_nodes(&arc->root);
    arc->format = ARC_NONE;
    if (is_tar(&src)) return read_tar(arc, &src);
    if (read_lib(arc, &src, &hr) || hr != S_FALSE) return SUCCEEDED(hr);
    hr = la_decompress(&ls, MAX_DECOMPRESSED, &arc->data, &arc->data_len);
    if (hr == S_OK) return read_stream(arc, file, NULL);
    return FALSE;
}

void archive_release(struct archive *arc)
{
    if (!arc || InterlockedDecrement(&arc->ref)) return;
    free_nodes(&arc->root);
    free(arc->entries);
    free(arc->data);
    free(arc);
}

struct archive *archive_open(const WCHAR *path, DWORD flags)
{
    BOOL sorted = !!(flags & ARCHIVE_SORTED), flat = !!(flags & ARCHIVE_FLAT);
    WIN32_FILE_ATTRIBUTE_DATA fad;
    struct archive *arc;
    LARGE_INTEGER size;
    HANDLE file;
    int i;

    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &fad) || (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        return NULL;

    EnterCriticalSection(&cache_cs);
    for (i = 0; i < CACHE_SIZE; i++)
    {
        arc = cache[i];
        if (arc && arc->sorted == sorted && arc->flat == flat && !wcsicmp(arc->path, path) && !CompareFileTime(&arc->stamp, &fad.ftLastWriteTime)
            && arc->file_size == (((ULONGLONG)fad.nFileSizeHigh << 32) | fad.nFileSizeLow))
        {
            InterlockedIncrement(&arc->ref);
            LeaveCriticalSection(&cache_cs);
            return arc;
        }
    }
    LeaveCriticalSection(&cache_cs);

    if (!(arc = calloc(1, sizeof(*arc)))) return NULL;
    arc->ref = 1;
    arc->sorted = sorted;
    arc->flat = flat;
    lstrcpynW(arc->path, path, MAX_PATH);
    arc->stamp = fad.ftLastWriteTime;
    arc->root.dir = TRUE;
    arc->root.attrs = FILE_ATTRIBUTE_DIRECTORY;

    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                       OPEN_EXISTING, 0, NULL);
    if (file != INVALID_HANDLE_VALUE)
    {
        if (GetFileSizeEx(file, &size)) arc->file_size = size.QuadPart;
        arc->valid = read_archive(arc, file);
        CloseHandle(file);
    }
    if (!arc->valid) free_nodes(&arc->root);
    else if (sorted) sort_children(&arc->root);
    TRACE("%s: format %d valid %d\n", debugstr_w(path), arc->format, arc->valid);

    if (arc->valid)
    {
        EnterCriticalSection(&cache_cs);
        archive_release(cache[CACHE_SIZE - 1]);
        memmove(cache + 1, cache, (CACHE_SIZE - 1) * sizeof(*cache));
        cache[0] = arc;
        InterlockedIncrement(&arc->ref);
        LeaveCriticalSection(&cache_cs);
    }
    return arc;
}

HRESULT archive_read_entry(struct archive *arc, const struct arc_node *node, BYTE **data, SIZE_T *len)
{
    struct source src = { NULL, arc->data, arc->data_len };
    HANDLE file = INVALID_HANDLE_VALUE;
    HRESULT hr;

    if (node->dir) return E_FAIL;
    if (arc->format == ARC_TAR || arc->format == ARC_GZ)
    {
        BYTE *out;
        if (node->size >= MAX_DECOMPRESSED) return E_OUTOFMEMORY;
        if (!arc->data)
        {
            file = CreateFileW(arc->path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               NULL, OPEN_EXISTING, 0, NULL);
            if (file == INVALID_HANDLE_VALUE) return HRESULT_FROM_WIN32(GetLastError());
            src.file = file;
            src.len = arc->file_size;
        }
        if (!(out = malloc(node->size ? node->size : 1))) hr = E_OUTOFMEMORY;
        else if (node->size && !src_read(&src, node->offset, out, (DWORD)node->size))
        {
            free(out);
            hr = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        else
        {
            *data = out;
            *len = node->size;
            hr = S_OK;
        }
    }
    else if (arc->format == ARC_LIB)
    {
        struct la_source ls = { NULL, arc->data, arc->data_len };

        if (node->flags & 1) return HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED);  /* encrypted */
        if (node->method)  /* a link: no data */
        {
            if (!(*data = malloc(1))) return E_OUTOFMEMORY;
            *len = 0;
            return S_OK;
        }
        if (!arc->data)
        {
            file = CreateFileW(arc->path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               NULL, OPEN_EXISTING, 0, NULL);
            if (file == INVALID_HANDLE_VALUE) return HRESULT_FROM_WIN32(GetLastError());
            ls.file = file;
            ls.len = arc->file_size;
        }
        hr = la_read(&ls, node->offset, MAX_DECOMPRESSED, data, len);
    }
    else
    {
        file = CreateFileW(arc->path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING, 0, NULL);
        if (file == INVALID_HANDLE_VALUE) return HRESULT_FROM_WIN32(GetLastError());
        src.file = file;
        src.mem = NULL;
        src.len = arc->file_size;
        hr = zip_read_entry(&src, node, data, len);
    }
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    return hr;
}

/* A file name that Windows accepts: invalid characters become '_'. */
void archive_sanitize_name(const WCHAR *in, WCHAR *out, int len)
{
    int i;
    for (i = 0; in[i] && i < len - 1; i++)
        out[i] = (in[i] < 32 || wcschr(L"<>:\"/\\|?*", in[i])) ? '_' : in[i];
    out[i] = 0;
    while (i > 0 && (out[i - 1] == '.' || out[i - 1] == ' ')) out[--i] = 0;
    if (!*out) lstrcpynW(out, L"_", len);
}

static HRESULT extract_node(struct archive *arc, const struct arc_node *dir, const WCHAR *dest)
{
    const struct arc_node *c;
    WCHAR path[MAX_PATH], name[MAX_PATH];
    HRESULT hr = S_OK;

    if (!CreateDirectoryW(dest, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
        return HRESULT_FROM_WIN32(GetLastError());
    for (c = dir->child; c && SUCCEEDED(hr); c = c->next)
    {
        archive_sanitize_name(c->name, name, MAX_PATH);
        if (!PathCombineW(path, dest, name)) return HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE);
        if (c->dir) hr = extract_node(arc, c, path);
        else
        {
            BYTE *data;
            SIZE_T len;
            HANDLE file;
            DWORD written;

            if (FAILED(hr = archive_read_entry(arc, c, &data, &len))) break;
            file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                               (c->attrs & FILE_ATTRIBUTE_HIDDEN) ? FILE_ATTRIBUTE_HIDDEN : FILE_ATTRIBUTE_NORMAL, NULL);
            if (file == INVALID_HANDLE_VALUE) hr = HRESULT_FROM_WIN32(GetLastError());
            else
            {
                if (len && (!WriteFile(file, data, (DWORD)len, &written, NULL) || written != len))
                    hr = HRESULT_FROM_WIN32(GetLastError());
                if (c->mtime.dwLowDateTime || c->mtime.dwHighDateTime) SetFileTime(file, NULL, NULL, &c->mtime);
                CloseHandle(file);
            }
            free(data);
        }
    }
    return hr;
}

/* libarchive formats are extracted in one pass (a solid 7z or rar archive
 * would otherwise be decompressed again for every file): first the
 * directories, then every file as the archive gives it. */
static HRESULT make_dirs(const struct arc_node *dir, const WCHAR *dest)
{
    const struct arc_node *c;
    WCHAR path[MAX_PATH], name[MAX_PATH];
    HRESULT hr = S_OK;

    if (!CreateDirectoryW(dest, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
        return HRESULT_FROM_WIN32(GetLastError());
    for (c = dir->child; c && SUCCEEDED(hr); c = c->next)
    {
        if (!c->dir) continue;
        archive_sanitize_name(c->name, name, MAX_PATH);
        if (!PathCombineW(path, dest, name)) return HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE);
        hr = make_dirs(c, path);
    }
    return hr;
}

struct extract_ctx
{
    struct archive *arc;
    const WCHAR *dest;
};

static BOOL node_path(const struct arc_node *node, const WCHAR *dest, WCHAR *path, DWORD len)
{
    const struct arc_node *stack[64], *n;
    WCHAR name[MAX_PATH], buf[MAX_PATH];
    int depth = 0;

    for (n = node; n && n->parent; n = n->parent)
    {
        if (depth == ARRAY_SIZE(stack)) return FALSE;
        stack[depth++] = n;
    }
    lstrcpynW(buf, dest, MAX_PATH);
    while (depth--)
    {
        archive_sanitize_name(stack[depth]->name, name, MAX_PATH);
        if (!PathCombineW(buf, buf, name)) return FALSE;
    }
    lstrcpynW(path, buf, len);
    return TRUE;
}

static BOOL extract_cb(void *ctx, ULONGLONG index, WCHAR *path, DWORD len, DWORD *attrs, FILETIME *mtime)
{
    struct extract_ctx *x = ctx;
    const struct arc_node *node;
    HANDLE file;

    if (index >= x->arc->entry_count || !(node = x->arc->entries[index]) || node->offset != index) return FALSE;
    if (!node_path(node, x->dest, path, len)) return FALSE;
    *attrs = node->attrs;
    *mtime = node->mtime;
    if (!node->method) return TRUE;
    /* a link: an empty file */
    file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    return FALSE;
}

static HRESULT extract_lib(struct archive *arc, const WCHAR *dest)
{
    struct la_source ls = { NULL, arc->data, arc->data_len };
    struct extract_ctx ctx = { arc, dest };
    HANDLE file = INVALID_HANDLE_VALUE;
    HRESULT hr;

    if (FAILED(hr = make_dirs(&arc->root, dest))) return hr;
    if (!arc->data)
    {
        file = CreateFileW(arc->path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING, 0, NULL);
        if (file == INVALID_HANDLE_VALUE) return HRESULT_FROM_WIN32(GetLastError());
        ls.file = file;
        ls.len = arc->file_size;
    }
    hr = la_extract(&ls, extract_cb, &ctx);
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    return hr;
}

HRESULT archive_extract_all(const WCHAR *path, const WCHAR *dest)
{
    struct archive *arc = archive_open(path, 0);
    HRESULT hr;

    if (!arc) return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    if (!arc->valid) hr = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    else if (arc->format == ARC_LIB) hr = extract_lib(arc, dest);
    else hr = extract_node(arc, &arc->root, dest);
    archive_release(arc);
    return hr;
}
