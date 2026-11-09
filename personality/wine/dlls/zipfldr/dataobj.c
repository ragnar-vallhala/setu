/*
 * Compressed folders: copying entries out (streams, data objects,
 * ITransferSource) and the context menu of entries
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

#include <stdlib.h>
#include <string.h>
#include "zipfldr_private.h"
#include "shlwapi.h"
#include "shellapi.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(zipfldr);

/* FILEDESCRIPTOR flags (shlobj.h in the Windows SDK) */
#define FD_ATTRIBUTES  0x0004
#define FD_WRITESTIME  0x0020
#define FD_FILESIZE    0x0040
#define FD_PROGRESSUI  0x4000

/* ---- IStream over a decompressed entry ---- */

struct stream
{
    IStream IStream_iface;
    LONG ref;
    BYTE *data;
    SIZE_T len, pos;
    WCHAR *name;
    FILETIME mtime;
};

static inline struct stream *impl_from_IStream(IStream *iface)
{
    return CONTAINING_RECORD(iface, struct stream, IStream_iface);
}

static HRESULT WINAPI stream_QueryInterface(IStream *iface, REFIID riid, void **out)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ISequentialStream) || IsEqualIID(riid, &IID_IStream))
    {
        *out = iface;
        IStream_AddRef(iface);
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI stream_AddRef(IStream *iface)
{
    return InterlockedIncrement(&impl_from_IStream(iface)->ref);
}

static ULONG WINAPI stream_Release(IStream *iface)
{
    struct stream *s = impl_from_IStream(iface);
    ULONG ref = InterlockedDecrement(&s->ref);
    if (!ref)
    {
        free(s->data);
        free(s->name);
        free(s);
        InterlockedDecrement(&zipfldr_locks);
    }
    return ref;
}

static HRESULT WINAPI stream_Read(IStream *iface, void *buf, ULONG len, ULONG *read)
{
    struct stream *s = impl_from_IStream(iface);
    ULONG n = (ULONG)min((SIZE_T)len, s->len - s->pos);

    memcpy(buf, s->data + s->pos, n);
    s->pos += n;
    if (read) *read = n;
    return n == len ? S_OK : S_FALSE;
}

static HRESULT WINAPI stream_Write(IStream *iface, const void *buf, ULONG len, ULONG *written)
{
    if (written) *written = 0;
    return STG_E_ACCESSDENIED;
}

static HRESULT WINAPI stream_Seek(IStream *iface, LARGE_INTEGER move, DWORD origin, ULARGE_INTEGER *newpos)
{
    struct stream *s = impl_from_IStream(iface);
    LONGLONG base, pos;

    switch (origin)
    {
    case STREAM_SEEK_SET: base = 0; break;
    case STREAM_SEEK_CUR: base = s->pos; break;
    case STREAM_SEEK_END: base = s->len; break;
    default: return STG_E_INVALIDFUNCTION;
    }
    pos = base + move.QuadPart;
    if (pos < 0) return STG_E_INVALIDFUNCTION;
    s->pos = (SIZE_T)min((ULONGLONG)pos, (ULONGLONG)s->len);
    if (newpos) newpos->QuadPart = s->pos;
    return S_OK;
}

static HRESULT WINAPI stream_SetSize(IStream *iface, ULARGE_INTEGER size)
{
    return STG_E_ACCESSDENIED;
}

static HRESULT WINAPI stream_CopyTo(IStream *iface, IStream *dest, ULARGE_INTEGER len, ULARGE_INTEGER *read,
                                    ULARGE_INTEGER *written)
{
    struct stream *s = impl_from_IStream(iface);
    ULONGLONG n = min(len.QuadPart, (ULONGLONG)(s->len - s->pos)), done = 0;
    HRESULT hr = S_OK;

    while (done < n && SUCCEEDED(hr))
    {
        ULONG chunk = (ULONG)min(n - done, (ULONGLONG)0x10000000), w = 0;
        hr = IStream_Write(dest, s->data + s->pos, chunk, &w);
        s->pos += w;
        done += w;
        if (w != chunk) break;
    }
    if (read) read->QuadPart = done;
    if (written) written->QuadPart = done;
    return hr;
}

static HRESULT WINAPI stream_Commit(IStream *iface, DWORD flags)
{
    return S_OK;
}

static HRESULT WINAPI stream_Revert(IStream *iface)
{
    return S_OK;
}

static HRESULT WINAPI stream_LockRegion(IStream *iface, ULARGE_INTEGER off, ULARGE_INTEGER len, DWORD type)
{
    return STG_E_INVALIDFUNCTION;
}

static HRESULT WINAPI stream_UnlockRegion(IStream *iface, ULARGE_INTEGER off, ULARGE_INTEGER len, DWORD type)
{
    return STG_E_INVALIDFUNCTION;
}

static HRESULT WINAPI stream_Stat(IStream *iface, STATSTG *st, DWORD flags)
{
    struct stream *s = impl_from_IStream(iface);

    memset(st, 0, sizeof(*st));
    st->type = STGTY_STREAM;
    st->cbSize.QuadPart = s->len;
    st->mtime = s->mtime;
    st->grfMode = STGM_READ;
    if (!(flags & STATFLAG_NONAME)) return SHStrDupW(s->name, &st->pwcsName);
    return S_OK;
}

static HRESULT WINAPI stream_Clone(IStream *iface, IStream **out)
{
    struct stream *s = impl_from_IStream(iface);
    BYTE *copy = malloc(s->len ? s->len : 1);
    HRESULT hr;

    *out = NULL;
    if (!copy) return E_OUTOFMEMORY;
    memcpy(copy, s->data, s->len);
    if (SUCCEEDED(hr = stream_create(s->name, &s->mtime, copy, s->len, out)))
        impl_from_IStream(*out)->pos = s->pos;
    return hr;
}

static const IStreamVtbl stream_vtbl =
{
    stream_QueryInterface, stream_AddRef, stream_Release, stream_Read, stream_Write, stream_Seek,
    stream_SetSize, stream_CopyTo, stream_Commit, stream_Revert, stream_LockRegion, stream_UnlockRegion,
    stream_Stat, stream_Clone
};

/* Takes ownership of data. */
HRESULT stream_create(const WCHAR *name, const FILETIME *mtime, BYTE *data, SIZE_T len, IStream **out)
{
    struct stream *s = calloc(1, sizeof(*s));

    *out = NULL;
    if (!s || !(s->name = wcsdup(name)))
    {
        free(s);
        free(data);
        return E_OUTOFMEMORY;
    }
    s->IStream_iface.lpVtbl = &stream_vtbl;
    s->ref = 1;
    s->data = data;
    s->len = len;
    if (mtime) s->mtime = *mtime;
    InterlockedIncrement(&zipfldr_locks);
    *out = &s->IStream_iface;
    return S_OK;
}

static HRESULT node_stream(struct archive *arc, const struct arc_node *node, IStream **out)
{
    BYTE *data;
    SIZE_T len;
    HRESULT hr;

    *out = NULL;
    if (FAILED(hr = archive_read_entry(arc, node, &data, &len))) return hr;
    return stream_create(node->name, &node->mtime, data, len, out);
}

/* ---- IDataObject ---- */

struct desc_item
{
    const struct arc_node *node;
    WCHAR path[MAX_PATH];       /* relative to the selection's folder */
};

struct dataobject
{
    IDataObject IDataObject_iface;
    LONG ref;
    IShellFolder2 *folder;
    struct archive *arc;
    BOOL zip;
    LPITEMIDLIST parent;
    UINT cidl;
    LPITEMIDLIST *apidl;
    struct desc_item *items;    /* the selection with directories expanded, pre-order */
    UINT count, cap;
    UINT cf_idlist, cf_descriptor, cf_contents;
};

static inline struct dataobject *impl_from_IDataObject(IDataObject *iface)
{
    return CONTAINING_RECORD(iface, struct dataobject, IDataObject_iface);
}

static void add_items(struct dataobject *d, const struct arc_node *node, const WCHAR *prefix)
{
    const struct arc_node *c;
    struct desc_item *item;

    if (d->count == d->cap)
    {
        UINT cap = d->cap ? d->cap * 2 : 16;
        struct desc_item *n = realloc(d->items, cap * sizeof(*n));
        if (!n) return;
        d->items = n;
        d->cap = cap;
    }
    item = &d->items[d->count++];
    item->node = node;
    if (*prefix) swprintf(item->path, MAX_PATH, L"%s\\%s", prefix, node->name);
    else lstrcpynW(item->path, node->name, MAX_PATH);
    if (node->dir)
    {
        WCHAR path[MAX_PATH];
        lstrcpyW(path, item->path);
        for (c = node->child; c; c = c->next) add_items(d, c, path);
    }
}

static HRESULT WINAPI dataobject_QueryInterface(IDataObject *iface, REFIID riid, void **out)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDataObject))
    {
        *out = iface;
        IDataObject_AddRef(iface);
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI dataobject_AddRef(IDataObject *iface)
{
    return InterlockedIncrement(&impl_from_IDataObject(iface)->ref);
}

static ULONG WINAPI dataobject_Release(IDataObject *iface)
{
    struct dataobject *d = impl_from_IDataObject(iface);
    ULONG ref = InterlockedDecrement(&d->ref), i;

    if (!ref)
    {
        for (i = 0; i < d->cidl; i++) ILFree(d->apidl[i]);
        free(d->apidl);
        free(d->items);
        ILFree(d->parent);
        IShellFolder2_Release(d->folder);
        free(d);
    }
    return ref;
}

static HGLOBAL make_cida(struct dataobject *d)
{
    UINT i, size = sizeof(CIDA) + d->cidl * sizeof(UINT), offset;
    HGLOBAL mem;
    CIDA *cida;

    size += ILGetSize(d->parent);
    for (i = 0; i < d->cidl; i++) size += ILGetSize(d->apidl[i]);
    if (!(mem = GlobalAlloc(GHND | GMEM_SHARE, size))) return NULL;
    cida = GlobalLock(mem);
    cida->cidl = d->cidl;
    offset = sizeof(CIDA) + d->cidl * sizeof(UINT);
    cida->aoffset[0] = offset;
    memcpy((BYTE *)cida + offset, d->parent, ILGetSize(d->parent));
    offset += ILGetSize(d->parent);
    for (i = 0; i < d->cidl; i++)
    {
        cida->aoffset[i + 1] = offset;
        memcpy((BYTE *)cida + offset, d->apidl[i], ILGetSize(d->apidl[i]));
        offset += ILGetSize(d->apidl[i]);
    }
    GlobalUnlock(mem);
    return mem;
}

static HGLOBAL make_descriptor(struct dataobject *d)
{
    FILEGROUPDESCRIPTORW *fgd;
    HGLOBAL mem;
    UINT i;

    if (!(mem = GlobalAlloc(GHND | GMEM_SHARE, offsetof(FILEGROUPDESCRIPTORW, fgd[d->count ? d->count : 1]))))
        return NULL;
    fgd = GlobalLock(mem);
    fgd->cItems = d->count;
    for (i = 0; i < d->count; i++)
    {
        FILEDESCRIPTORW *fd = &fgd->fgd[i];
        const struct arc_node *node = d->items[i].node;

        fd->dwFlags = FD_PROGRESSUI | FD_FILESIZE | FD_ATTRIBUTES;
        fd->dwFileAttributes = FILE_ATTRIBUTE_VIRTUAL | (node->dir ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_ARCHIVE)
                               | (node->attrs & FILE_ATTRIBUTE_HIDDEN);
        if (!node->dir)
        {
            fd->nFileSizeLow = (DWORD)node->size;
            fd->nFileSizeHigh = (DWORD)(node->size >> 32);
        }
        if (node->mtime.dwLowDateTime || node->mtime.dwHighDateTime)
        {
            fd->dwFlags |= FD_WRITESTIME;
            fd->ftLastWriteTime = node->mtime;
        }
        lstrcpynW(fd->cFileName, d->items[i].path, MAX_PATH);
    }
    GlobalUnlock(mem);
    return mem;
}

static BOOL format_ok(struct dataobject *d, const FORMATETC *fmt)
{
    if (fmt->cfFormat == d->cf_idlist) return !!(fmt->tymed & TYMED_HGLOBAL);
    if (!d->zip) return FALSE;
    if (fmt->cfFormat == d->cf_descriptor) return !!(fmt->tymed & TYMED_HGLOBAL);
    if (fmt->cfFormat == d->cf_contents) return !!(fmt->tymed & TYMED_ISTREAM);
    return FALSE;
}

static HRESULT WINAPI dataobject_GetData(IDataObject *iface, FORMATETC *fmt, STGMEDIUM *medium)
{
    struct dataobject *d = impl_from_IDataObject(iface);

    TRACE("(%p, %u, %ld, %#lx)\n", d, fmt->cfFormat, fmt->lindex, fmt->tymed);
    if (!format_ok(d, fmt)) return DV_E_FORMATETC;
    memset(medium, 0, sizeof(*medium));
    if (fmt->cfFormat == d->cf_contents)
    {
        HRESULT hr;
        if (fmt->lindex < 0 || (UINT)fmt->lindex >= d->count || d->items[fmt->lindex].node->dir) return E_FAIL;
        if (FAILED(hr = node_stream(d->arc, d->items[fmt->lindex].node, &medium->pstm))) return hr;
        medium->tymed = TYMED_ISTREAM;
        return S_OK;
    }
    medium->hGlobal = fmt->cfFormat == d->cf_idlist ? make_cida(d) : make_descriptor(d);
    if (!medium->hGlobal) return E_OUTOFMEMORY;
    medium->tymed = TYMED_HGLOBAL;
    return S_OK;
}

static HRESULT WINAPI dataobject_GetDataHere(IDataObject *iface, FORMATETC *fmt, STGMEDIUM *medium)
{
    return E_NOTIMPL;
}

static HRESULT WINAPI dataobject_QueryGetData(IDataObject *iface, FORMATETC *fmt)
{
    return format_ok(impl_from_IDataObject(iface), fmt) ? S_OK : DV_E_FORMATETC;
}

static HRESULT WINAPI dataobject_GetCanonicalFormatEtc(IDataObject *iface, FORMATETC *in, FORMATETC *out)
{
    *out = *in;
    out->ptd = NULL;
    return DATA_S_SAMEFORMATETC;
}

static HRESULT WINAPI dataobject_SetData(IDataObject *iface, FORMATETC *fmt, STGMEDIUM *medium, BOOL release)
{
    /* Drop targets report the performed drop effect: nothing to do with it. */
    if (release) ReleaseStgMedium(medium);
    return S_OK;
}

static HRESULT WINAPI dataobject_EnumFormatEtc(IDataObject *iface, DWORD dir, IEnumFORMATETC **out)
{
    struct dataobject *d = impl_from_IDataObject(iface);
    FORMATETC fmts[3] =
    {
        { d->cf_idlist, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL },
        { d->cf_descriptor, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL },
        { d->cf_contents, NULL, DVASPECT_CONTENT, -1, TYMED_ISTREAM },
    };

    if (dir != DATADIR_GET) return E_NOTIMPL;
    return SHCreateStdEnumFmtEtc(d->zip ? 3 : 1, fmts, out);
}

static HRESULT WINAPI dataobject_DAdvise(IDataObject *iface, FORMATETC *fmt, DWORD flags, IAdviseSink *sink, DWORD *conn)
{
    return OLE_E_ADVISENOTSUPPORTED;
}

static HRESULT WINAPI dataobject_DUnadvise(IDataObject *iface, DWORD conn)
{
    return OLE_E_ADVISENOTSUPPORTED;
}

static HRESULT WINAPI dataobject_EnumDAdvise(IDataObject *iface, IEnumSTATDATA **out)
{
    return OLE_E_ADVISENOTSUPPORTED;
}

static const IDataObjectVtbl dataobject_vtbl =
{
    dataobject_QueryInterface, dataobject_AddRef, dataobject_Release, dataobject_GetData,
    dataobject_GetDataHere, dataobject_QueryGetData, dataobject_GetCanonicalFormatEtc, dataobject_SetData,
    dataobject_EnumFormatEtc, dataobject_DAdvise, dataobject_DUnadvise, dataobject_EnumDAdvise
};

HRESULT dataobject_create(IShellFolder2 *folder, BOOL zip, LPCITEMIDLIST parent, UINT cidl,
                          LPCITEMIDLIST *apidl, REFIID riid, void **out)
{
    struct dataobject *d = calloc(1, sizeof(*d));
    HRESULT hr;
    UINT i;

    if (!d) return E_OUTOFMEMORY;
    d->IDataObject_iface.lpVtbl = &dataobject_vtbl;
    d->ref = 1;
    d->folder = folder;
    IShellFolder2_AddRef(folder);
    d->zip = zip;
    d->parent = ILClone(parent);
    d->apidl = calloc(cidl, sizeof(*d->apidl));
    d->cf_idlist = RegisterClipboardFormatW(CFSTR_SHELLIDLISTW);
    d->cf_descriptor = RegisterClipboardFormatW(CFSTR_FILEDESCRIPTORW);
    d->cf_contents = RegisterClipboardFormatW(CFSTR_FILECONTENTSW);
    if (d->apidl && d->parent)
    {
        for (i = 0; i < cidl; i++)
        {
            const struct arc_node *node = folder_lookup(folder, apidl[i], &d->arc);
            if (!(d->apidl[d->cidl] = ILClone(apidl[i]))) break;
            d->cidl++;
            if (node) add_items(d, node, L"");
        }
    }
    hr = d->cidl == cidl ? IDataObject_QueryInterface(&d->IDataObject_iface, riid, out) : E_OUTOFMEMORY;
    IDataObject_Release(&d->IDataObject_iface);
    return hr;
}

/* ---- ITransferSource: lets IFileOperation-style copiers read entries ---- */

struct transfer
{
    ITransferSource ITransferSource_iface;
    LONG ref;
    IShellFolder2 *folder;
    UINT depth;                 /* number of archive levels in the folder's PIDL */
};

static inline struct transfer *impl_from_ITransferSource(ITransferSource *iface)
{
    return CONTAINING_RECORD(iface, struct transfer, ITransferSource_iface);
}

static BOOL is_archive_id(LPCITEMIDLIST pidl)
{
    return pidl->mkid.cb >= 6 && *(const WORD *)((const BYTE *)pidl + 2) == 0x5a53;
}

static UINT archive_depth(LPCITEMIDLIST pidl)
{
    UINT n = 0;
    for (; pidl && pidl->mkid.cb; pidl = ILGetNext(pidl)) if (is_archive_id(pidl)) n++;
    return n;
}

/* The part of an item's PIDL below the transfer source's folder. */
static LPCITEMIDLIST relative_id(struct transfer *t, LPCITEMIDLIST pidl)
{
    UINT skip = t->depth;
    for (; pidl && pidl->mkid.cb && !is_archive_id(pidl); pidl = ILGetNext(pidl)) ;
    for (; pidl && pidl->mkid.cb && skip; pidl = ILGetNext(pidl), skip--) ;
    return pidl && pidl->mkid.cb ? pidl : NULL;
}

static HRESULT WINAPI transfer_QueryInterface(ITransferSource *iface, REFIID riid, void **out)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ITransferSource))
    {
        *out = iface;
        ITransferSource_AddRef(iface);
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI transfer_AddRef(ITransferSource *iface)
{
    return InterlockedIncrement(&impl_from_ITransferSource(iface)->ref);
}

static ULONG WINAPI transfer_Release(ITransferSource *iface)
{
    struct transfer *t = impl_from_ITransferSource(iface);
    ULONG ref = InterlockedDecrement(&t->ref);
    if (!ref)
    {
        IShellFolder2_Release(t->folder);
        free(t);
    }
    return ref;
}

static HRESULT WINAPI transfer_Advise(ITransferSource *iface, ITransferAdviseSink *sink, DWORD *cookie)
{
    *cookie = 1;
    return S_OK;
}

static HRESULT WINAPI transfer_Unadvise(ITransferSource *iface, DWORD cookie)
{
    return S_OK;
}

static HRESULT WINAPI transfer_SetProperties(ITransferSource *iface, IPropertyChangeArray *array)
{
    return E_NOTIMPL;
}

static const struct arc_node *item_node(struct transfer *t, IShellItem *item, struct archive **arc)
{
    LPITEMIDLIST pidl;
    LPCITEMIDLIST rel;
    const struct arc_node *node = NULL;

    *arc = NULL;
    if (FAILED(SHGetIDListFromObject((IUnknown *)item, &pidl))) return NULL;
    if ((rel = relative_id(t, pidl))) node = folder_lookup(t->folder, rel, arc);
    ILFree(pidl);
    return node;
}

static HRESULT WINAPI transfer_OpenItem(ITransferSource *iface, IShellItem *item, TRANSFER_SOURCE_FLAGS flags,
                                        REFIID riid, void **out)
{
    struct transfer *t = impl_from_ITransferSource(iface);
    const struct arc_node *node;
    struct archive *arc;

    TRACE("(%p, %p, %#lx, %s)\n", t, item, flags, debugstr_guid(riid));
    *out = NULL;
    if (!(node = item_node(t, item, &arc))) return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    if (node->dir) return IShellItem_BindToHandler(item, NULL, &BHID_SFObject, riid, out);
    if (!IsEqualIID(riid, &IID_IStream) && !IsEqualIID(riid, &IID_ISequentialStream)) return E_NOINTERFACE;
    return node_stream(arc, node, (IStream **)out);
}

static HRESULT WINAPI transfer_MoveItem(ITransferSource *iface, IShellItem *item, IShellItem *dest, LPCWSTR name,
                                        TRANSFER_SOURCE_FLAGS flags, IShellItem **newitem)
{
    *newitem = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI transfer_RecycleItem(ITransferSource *iface, IShellItem *item, IShellItem *dest,
                                           TRANSFER_SOURCE_FLAGS flags, IShellItem **newitem)
{
    *newitem = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI transfer_RemoveItem(ITransferSource *iface, IShellItem *item, TRANSFER_SOURCE_FLAGS flags)
{
    return E_NOTIMPL;
}

static HRESULT WINAPI transfer_RenameItem(ITransferSource *iface, IShellItem *item, LPCWSTR name,
                                          TRANSFER_SOURCE_FLAGS flags, IShellItem **newitem)
{
    *newitem = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI transfer_LinkItem(ITransferSource *iface, IShellItem *item, IShellItem *dest, LPCWSTR name,
                                        TRANSFER_SOURCE_FLAGS flags, IShellItem **newitem)
{
    *newitem = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI transfer_ApplyPropertiesToItem(ITransferSource *iface, IShellItem *item, IShellItem **newitem)
{
    *newitem = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI transfer_GetDefaultDestinationName(ITransferSource *iface, IShellItem *item, IShellItem *dest,
                                                         LPWSTR *name)
{
    struct transfer *t = impl_from_ITransferSource(iface);
    const struct arc_node *node;
    struct archive *arc;
    WCHAR buf[MAX_PATH];

    *name = NULL;
    if (!(node = item_node(t, item, &arc))) return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    archive_sanitize_name(node->name, buf, MAX_PATH);
    return SHStrDupW(buf, name);
}

static HRESULT WINAPI transfer_EnterFolder(ITransferSource *iface, IShellItem *folder)
{
    return S_OK;
}

static HRESULT WINAPI transfer_LeaveFolder(ITransferSource *iface, IShellItem *folder)
{
    return S_OK;
}

static const ITransferSourceVtbl transfer_vtbl =
{
    transfer_QueryInterface, transfer_AddRef, transfer_Release, transfer_Advise, transfer_Unadvise,
    transfer_SetProperties, transfer_OpenItem, transfer_MoveItem, transfer_RecycleItem, transfer_RemoveItem,
    transfer_RenameItem, transfer_LinkItem, transfer_ApplyPropertiesToItem, transfer_GetDefaultDestinationName,
    transfer_EnterFolder, transfer_LeaveFolder
};

HRESULT transfer_source_create(IShellFolder2 *folder, LPCITEMIDLIST pidl, void **out)
{
    struct transfer *t = calloc(1, sizeof(*t));

    *out = NULL;
    if (!t) return E_OUTOFMEMORY;
    t->ITransferSource_iface.lpVtbl = &transfer_vtbl;
    t->ref = 1;
    t->folder = folder;
    IShellFolder2_AddRef(folder);
    t->depth = archive_depth(pidl);
    *out = &t->ITransferSource_iface;
    return S_OK;
}

/* ---- context menu of entries: Open (files) and Copy ---- */

enum { CMD_OPEN, CMD_COPY, CMD_COUNT };

struct item_menu
{
    IContextMenu IContextMenu_iface;
    LONG ref;
    IShellFolder2 *folder;
    LPITEMIDLIST parent;
    UINT cidl;
    LPITEMIDLIST *apidl;
    BOOL has_open;
};

static inline struct item_menu *impl_from_IContextMenu(IContextMenu *iface)
{
    return CONTAINING_RECORD(iface, struct item_menu, IContextMenu_iface);
}

static HRESULT WINAPI menu_QueryInterface(IContextMenu *iface, REFIID riid, void **out)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IContextMenu))
    {
        *out = iface;
        IContextMenu_AddRef(iface);
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI menu_AddRef(IContextMenu *iface)
{
    return InterlockedIncrement(&impl_from_IContextMenu(iface)->ref);
}

static ULONG WINAPI menu_Release(IContextMenu *iface)
{
    struct item_menu *m = impl_from_IContextMenu(iface);
    ULONG ref = InterlockedDecrement(&m->ref), i;
    if (!ref)
    {
        for (i = 0; i < m->cidl; i++) ILFree(m->apidl[i]);
        free(m->apidl);
        ILFree(m->parent);
        IShellFolder2_Release(m->folder);
        free(m);
    }
    return ref;
}

static HRESULT WINAPI menu_QueryContextMenu(IContextMenu *iface, HMENU menu, UINT index, UINT first, UINT last, UINT flags)
{
    struct item_menu *m = impl_from_IContextMenu(iface);
    WCHAR text[64];
    UINT n = 0;

    if (flags & CMF_DEFAULTONLY && !m->has_open) return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 0);
    if (m->has_open && first + CMD_OPEN <= last)
    {
        LoadStringW(zipfldr_instance, IDS_OPEN_MENU, text, ARRAY_SIZE(text));
        InsertMenuW(menu, index + n++, MF_BYPOSITION | MF_STRING, first + CMD_OPEN, text);
        SetMenuDefaultItem(menu, first + CMD_OPEN, FALSE);
    }
    if (!(flags & CMF_DEFAULTONLY) && first + CMD_COPY <= last)
    {
        LoadStringW(zipfldr_instance, IDS_COPY_MENU, text, ARRAY_SIZE(text));
        InsertMenuW(menu, index + n++, MF_BYPOSITION | MF_STRING, first + CMD_COPY, text);
    }
    return MAKE_HRESULT(SEVERITY_SUCCESS, 0, CMD_COUNT);
}

/* Opens a file of the archive: a copy goes to the temporary directory and
 * is opened from there, as Explorer does. */
static HRESULT open_entry(struct item_menu *m, HWND hwnd, LPCITEMIDLIST pidl)
{
    WCHAR dir[MAX_PATH], path[MAX_PATH], name[MAX_PATH];
    const struct arc_node *node;
    struct archive *arc;
    BYTE *data;
    SIZE_T len;
    HANDLE file;
    DWORD written;
    HRESULT hr;

    if (!(node = folder_lookup(m->folder, pidl, &arc)) || node->dir) return E_FAIL;
    if (!GetTempPathW(MAX_PATH, dir) || !PathAppendW(dir, L"Temp1_archive")) return E_FAIL;
    swprintf(path, MAX_PATH, L"%s_%08lx", dir, GetTickCount());
    lstrcpyW(dir, path);
    if (!CreateDirectoryW(dir, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) return HRESULT_FROM_WIN32(GetLastError());
    archive_sanitize_name(node->name, name, MAX_PATH);
    if (!PathCombineW(path, dir, name)) return E_FAIL;
    if (FAILED(hr = archive_read_entry(arc, node, &data, &len))) return hr;
    file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) hr = HRESULT_FROM_WIN32(GetLastError());
    else
    {
        if (len && (!WriteFile(file, data, (DWORD)len, &written, NULL) || written != len))
            hr = HRESULT_FROM_WIN32(GetLastError());
        CloseHandle(file);
    }
    free(data);
    if (SUCCEEDED(hr) && (INT_PTR)ShellExecuteW(hwnd, NULL, path, NULL, dir, SW_SHOWNORMAL) <= 32) hr = E_FAIL;
    return hr;
}

static HRESULT WINAPI menu_InvokeCommand(IContextMenu *iface, CMINVOKECOMMANDINFO *info)
{
    struct item_menu *m = impl_from_IContextMenu(iface);
    UINT cmd;

    if (IS_INTRESOURCE(info->lpVerb)) cmd = LOWORD(info->lpVerb);
    else if (!lstrcmpiA(info->lpVerb, "open")) cmd = CMD_OPEN;
    else if (!lstrcmpiA(info->lpVerb, "copy")) cmd = CMD_COPY;
    else return E_INVALIDARG;

    if (cmd == CMD_OPEN)
    {
        if (!m->has_open) return E_INVALIDARG;
        return open_entry(m, info->hwnd, m->apidl[0]);
    }
    if (cmd == CMD_COPY)
    {
        IDataObject *obj;
        HRESULT hr = dataobject_create(m->folder, TRUE, m->parent, m->cidl, (LPCITEMIDLIST *)m->apidl,
                                       &IID_IDataObject, (void **)&obj);
        if (FAILED(hr)) return hr;
        hr = OleSetClipboard(obj);
        IDataObject_Release(obj);
        return hr;
    }
    return E_INVALIDARG;
}

static HRESULT WINAPI menu_GetCommandString(IContextMenu *iface, UINT_PTR cmd, UINT type, UINT *reserved,
                                            LPSTR name, UINT len)
{
    static const char *verbs[] = { "open", "copy" };

    if (cmd >= CMD_COUNT) return E_INVALIDARG;
    switch (type)
    {
    case GCS_VERBA:
        lstrcpynA(name, verbs[cmd], len);
        return S_OK;
    case GCS_VERBW:
        MultiByteToWideChar(CP_ACP, 0, verbs[cmd], -1, (WCHAR *)name, len);
        return S_OK;
    case GCS_VALIDATEA:
    case GCS_VALIDATEW:
        return S_OK;
    }
    return E_NOTIMPL;
}

static const IContextMenuVtbl menu_vtbl =
{
    menu_QueryInterface, menu_AddRef, menu_Release, menu_QueryContextMenu, menu_InvokeCommand,
    menu_GetCommandString
};

HRESULT item_menu_create(IShellFolder2 *folder, LPCITEMIDLIST parent, UINT cidl, LPCITEMIDLIST *apidl,
                         REFIID riid, void **out)
{
    struct item_menu *m = calloc(1, sizeof(*m));
    struct archive *arc;
    const struct arc_node *node;
    HRESULT hr;
    UINT i;

    if (!m) return E_OUTOFMEMORY;
    m->IContextMenu_iface.lpVtbl = &menu_vtbl;
    m->ref = 1;
    m->folder = folder;
    IShellFolder2_AddRef(folder);
    m->parent = ILClone(parent);
    m->apidl = calloc(cidl, sizeof(*m->apidl));
    for (i = 0; m->apidl && i < cidl; i++)
        if ((m->apidl[m->cidl] = ILClone(apidl[i]))) m->cidl++;
    node = folder_lookup(folder, apidl[0], &arc);
    m->has_open = cidl == 1 && node && !node->dir;
    hr = m->cidl == cidl ? IContextMenu_QueryInterface(&m->IContextMenu_iface, riid, out) : E_OUTOFMEMORY;
    IContextMenu_Release(&m->IContextMenu_iface);
    return hr;
}
