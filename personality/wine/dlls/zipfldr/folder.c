/*
 * Compressed folders: the shell folder over an archive
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
 * Two classes share this code: the zip folder (CLSID_ZipFolder, ProgID
 * CompressedFolder) and the folder for other archives (CLSID_ArchiveFolder,
 * ProgID ArchiveFolder). What differs between them follows what Windows 11
 * shows (tests/win32/shell32/compressed_folders.c):
 *  - zip keeps the archive's order, the archive folder sorts by name;
 *  - zip entries can be copied out through IStream, FileGroupDescriptorW
 *    and FileContents; the archive folder offers ITransferSource only;
 *  - zip entries are movable and deletable (SFGAO_CANMOVE, _CANDELETE,
 *    _HASPROPSHEET, directories also _DROPTARGET); archive entries are
 *    only copyable;
 *  - zip GetAttributesOf returns all attributes, the archive folder only
 *    the requested ones.
 * A third class, the cabinet folder (CLSID_CABFolder, ProgID CABFolder,
 * registered for cabview.dll, which forwards to this DLL), shows a .cab
 * like Windows 11's cabview: one flat list in archive order, each file
 * named with its path ("docs\inner.txt") and displayed with its last
 * component, only copyable (SFGAO_CANCOPY), copied out through
 * FileGroupDescriptorW and FileContents; BindToObject(IStream) is not
 * implemented, as on Windows.
 * None writes to the archive: moving, deleting, renaming and dropping
 * files in are not implemented.
 */

#include <stdlib.h>
#include <string.h>
#include "zipfldr_private.h"
#include "shlwapi.h"
#include "shellapi.h"
#include "propkey.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(zipfldr);

#define PIDL_MAGIC 0x5a53  /* "SZ" */
#define PIDL_DIR   0x0001

#include "pshpack1.h"
struct zpidl
{
    WORD cb;
    WORD magic;
    WORD flags;
    DWORD attrs;
    ULONGLONG size;
    FILETIME mtime;
    WCHAR name[1];
};
#include "poppack.h"

static const struct zpidl *zpidl(LPCITEMIDLIST pidl)
{
    const struct zpidl *z = (const struct zpidl *)pidl;
    if (!pidl || pidl->mkid.cb < offsetof(struct zpidl, name) + sizeof(WCHAR) || z->magic != PIDL_MAGIC) return NULL;
    return z;
}

static LPITEMIDLIST next_id(LPCITEMIDLIST pidl)
{
    return (LPITEMIDLIST)((const BYTE *)pidl + pidl->mkid.cb);
}

static LPITEMIDLIST make_pidl(const struct arc_node *node)
{
    SIZE_T len = wcslen(node->name), cb = offsetof(struct zpidl, name) + (len + 1) * sizeof(WCHAR);
    struct zpidl *z = CoTaskMemAlloc(cb + sizeof(WORD));

    if (!z) return NULL;
    memset(z, 0, cb + sizeof(WORD));
    z->cb = (WORD)cb;
    z->magic = PIDL_MAGIC;
    z->flags = node->dir ? PIDL_DIR : 0;
    z->attrs = node->attrs;
    z->size = node->size;
    z->mtime = node->mtime;
    memcpy(z->name, node->name, (len + 1) * sizeof(WCHAR));
    return (LPITEMIDLIST)z;
}

struct folder
{
    IShellFolder2 IShellFolder2_iface;
    IPersistFolder2 IPersistFolder2_iface;
    IStorage IStorage_iface;
    LONG ref;
    BOOL zip;
    BOOL cab;
    LPITEMIDLIST pidl;          /* absolute */
    WCHAR arc_path[MAX_PATH];
    WCHAR sub[MAX_PATH];        /* directory inside the archive: "" or "a\b" */
    struct archive *arc;
    BOOL loaded;
};

static inline struct folder *impl_from_IShellFolder2(IShellFolder2 *iface)
{
    return CONTAINING_RECORD(iface, struct folder, IShellFolder2_iface);
}

static inline struct folder *impl_from_IPersistFolder2(IPersistFolder2 *iface)
{
    return CONTAINING_RECORD(iface, struct folder, IPersistFolder2_iface);
}

static inline struct folder *impl_from_IStorage(IStorage *iface)
{
    return CONTAINING_RECORD(iface, struct folder, IStorage_iface);
}

static const CLSID *folder_clsid(const struct folder *f)
{
    return f->zip ? &CLSID_ZipFolder : f->cab ? &CLSID_CABFolder : &CLSID_ArchiveFolder;
}

static struct archive *folder_archive(struct folder *f)
{
    if (!f->loaded)
    {
        f->loaded = TRUE;
        if (*f->arc_path)
            f->arc = archive_open(f->arc_path, f->cab ? ARCHIVE_FLAT : f->zip ? 0 : ARCHIVE_SORTED);
    }
    return f->arc;
}

/* The archive node of this folder's directory. */
static struct arc_node *folder_node(struct folder *f)
{
    struct archive *arc = folder_archive(f);
    struct arc_node *node;
    WCHAR part[MAX_PATH];
    const WCHAR *p;

    if (!arc || !arc->valid) return NULL;
    node = &arc->root;
    for (p = f->sub; node && *p; )
    {
        int n = 0;
        while (*p && *p != '\\' && n < MAX_PATH - 1) part[n++] = *p++;
        part[n] = 0;
        if (*p == '\\') p++;
        node = archive_find(arc, node, part);
        if (node && !node->dir) node = NULL;
    }
    return node;
}

/* The node a (possibly multi-level) relative PIDL points to. */
static struct arc_node *pidl_node(struct folder *f, LPCITEMIDLIST pidl)
{
    struct arc_node *node = folder_node(f);
    const struct zpidl *z;

    while (node && pidl && pidl->mkid.cb)
    {
        if (!(z = zpidl(pidl))) return NULL;
        if (!node->dir) return NULL;
        node = archive_find(f->arc, node, z->name);
        pidl = next_id(pidl);
    }
    return node;
}

static SFGAOF node_attributes(const struct folder *f, const struct arc_node *node)
{
    SFGAOF a;

    if (f->cab) return SFGAO_CANCOPY;
    if (node->dir)
    {
        a = f->zip ? SFGAO_FOLDER | SFGAO_DROPTARGET | SFGAO_HASPROPSHEET | SFGAO_CANDELETE | SFGAO_STORAGE |
                     SFGAO_CANMOVE | SFGAO_CANCOPY
                   : SFGAO_FOLDER | SFGAO_STORAGE | SFGAO_CANCOPY;
        if (node_has_subdirs(node)) a |= SFGAO_HASSUBFOLDER;
    }
    else
        a = f->zip ? SFGAO_STREAM | SFGAO_HASPROPSHEET | SFGAO_CANDELETE | SFGAO_CANMOVE | SFGAO_CANCOPY
                   : SFGAO_STREAM | SFGAO_CANCOPY;
    if (f->zip && (node->attrs & FILE_ATTRIBUTE_HIDDEN)) a |= SFGAO_HIDDEN;
    return a;
}

/* Like a file system folder: hide the extension of registered types,
 * unless the user wants to see extensions. */
static BOOL hide_extension(const WCHAR *name)
{
    DWORD hide = 1, size = sizeof(hide);
    const WCHAR *ext = PathFindExtensionW(name);
    WCHAR progid[MAX_PATH];
    LONG len = sizeof(progid);
    HKEY key;

    if (!*ext || name[0] == '.') return FALSE;
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced",
                 L"HideFileExt", RRF_RT_REG_DWORD, NULL, &hide, &size);
    if (hide)
    {
        if (RegOpenKeyExW(HKEY_CLASSES_ROOT, ext, 0, KEY_READ, &key)) return FALSE;
        RegCloseKey(key);
        return TRUE;
    }
    if (!RegQueryValueW(HKEY_CLASSES_ROOT, ext, progid, &len) && !RegOpenKeyExW(HKEY_CLASSES_ROOT, progid, 0, KEY_READ, &key))
    {
        BOOL never = !RegQueryValueExW(key, L"NeverShowExt", NULL, NULL, NULL, NULL);
        RegCloseKey(key);
        return never;
    }
    return FALSE;
}

static HRESULT folder_create_child(struct folder *parent, LPCITEMIDLIST child, struct folder **out);

/* ---- IEnumIDList ---- */

struct enum_ids
{
    IEnumIDList IEnumIDList_iface;
    LONG ref;
    LPITEMIDLIST *items;
    ULONG count, pos;
};

static inline struct enum_ids *impl_from_IEnumIDList(IEnumIDList *iface)
{
    return CONTAINING_RECORD(iface, struct enum_ids, IEnumIDList_iface);
}

static HRESULT WINAPI enum_QueryInterface(IEnumIDList *iface, REFIID riid, void **out)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IEnumIDList))
    {
        *out = iface;
        IEnumIDList_AddRef(iface);
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI enum_AddRef(IEnumIDList *iface)
{
    return InterlockedIncrement(&impl_from_IEnumIDList(iface)->ref);
}

static ULONG WINAPI enum_Release(IEnumIDList *iface)
{
    struct enum_ids *e = impl_from_IEnumIDList(iface);
    ULONG ref = InterlockedDecrement(&e->ref), i;
    if (!ref)
    {
        for (i = 0; i < e->count; i++) CoTaskMemFree(e->items[i]);
        free(e->items);
        free(e);
    }
    return ref;
}

static HRESULT WINAPI enum_Next(IEnumIDList *iface, ULONG celt, LPITEMIDLIST *items, ULONG *fetched)
{
    struct enum_ids *e = impl_from_IEnumIDList(iface);
    ULONG n = 0;

    if (!items || (celt > 1 && !fetched)) return E_INVALIDARG;
    while (n < celt && e->pos < e->count)
    {
        if (!(items[n] = ILClone(e->items[e->pos]))) break;
        n++;
        e->pos++;
    }
    if (fetched) *fetched = n;
    return n == celt ? S_OK : S_FALSE;
}

static HRESULT WINAPI enum_Skip(IEnumIDList *iface, ULONG celt)
{
    struct enum_ids *e = impl_from_IEnumIDList(iface);
    if (e->count - e->pos < celt)
    {
        e->pos = e->count;
        return S_FALSE;
    }
    e->pos += celt;
    return S_OK;
}

static HRESULT WINAPI enum_Reset(IEnumIDList *iface)
{
    impl_from_IEnumIDList(iface)->pos = 0;
    return S_OK;
}

static HRESULT WINAPI enum_Clone(IEnumIDList *iface, IEnumIDList **out)
{
    *out = NULL;
    return E_NOTIMPL;
}

static const IEnumIDListVtbl enum_vtbl =
{
    enum_QueryInterface, enum_AddRef, enum_Release, enum_Next, enum_Skip, enum_Reset, enum_Clone
};

/* ---- IShellFolder2 ---- */

static HRESULT WINAPI folder_QueryInterface(IShellFolder2 *iface, REFIID riid, void **out)
{
    struct folder *f = impl_from_IShellFolder2(iface);

    TRACE("(%p, %s)\n", f, debugstr_guid(riid));
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IShellFolder) || IsEqualIID(riid, &IID_IShellFolder2))
        *out = &f->IShellFolder2_iface;
    else if (IsEqualIID(riid, &IID_IPersist) || IsEqualIID(riid, &IID_IPersistFolder) || IsEqualIID(riid, &IID_IPersistFolder2))
        *out = &f->IPersistFolder2_iface;
    else if (f->zip && IsEqualIID(riid, &IID_IStorage))
        *out = &f->IStorage_iface;
    else
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    IUnknown_AddRef((IUnknown *)*out);
    return S_OK;
}

static ULONG WINAPI folder_AddRef(IShellFolder2 *iface)
{
    return InterlockedIncrement(&impl_from_IShellFolder2(iface)->ref);
}

static ULONG WINAPI folder_Release(IShellFolder2 *iface)
{
    struct folder *f = impl_from_IShellFolder2(iface);
    ULONG ref = InterlockedDecrement(&f->ref);

    if (!ref)
    {
        archive_release(f->arc);
        ILFree(f->pidl);
        free(f);
        InterlockedDecrement(&zipfldr_locks);
    }
    return ref;
}

static HRESULT WINAPI folder_ParseDisplayName(IShellFolder2 *iface, HWND hwnd, IBindCtx *pbc, LPOLESTR name,
                                              ULONG *eaten, LPITEMIDLIST *ppidl, ULONG *attrs)
{
    struct folder *f = impl_from_IShellFolder2(iface);
    struct arc_node *node;
    LPITEMIDLIST result = NULL, id, tmp;
    const WCHAR *p = name;
    WCHAR part[MAX_PATH];

    TRACE("(%p, %s)\n", f, debugstr_w(name));
    if (!ppidl) return E_INVALIDARG;
    *ppidl = NULL;
    if (eaten) *eaten = 0;
    if (!name || !*name) return E_INVALIDARG;
    if (!(node = folder_node(f))) return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);

    if (f->cab)
    {
        /* one level, named with the whole path */
        if (!(node = archive_find(f->arc, node, name))) return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
        if (!(result = make_pidl(node))) return E_OUTOFMEMORY;
        p = name + wcslen(name);
    }
    while (*p)
    {
        int n = 0;
        while (*p && *p != '\\' && n < MAX_PATH - 1) part[n++] = *p++;
        part[n] = 0;
        while (*p == '\\') p++;
        if (!n) break;
        if (!node->dir || !(node = archive_find(f->arc, node, part)))
        {
            ILFree(result);
            return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
        }
        if (!(id = make_pidl(node)))
        {
            ILFree(result);
            return E_OUTOFMEMORY;
        }
        tmp = result ? ILCombine(result, id) : ILClone(id);
        ILFree(id);
        ILFree(result);
        if (!(result = tmp)) return E_OUTOFMEMORY;
    }
    if (!result) return E_INVALIDARG;
    if (eaten) *eaten = wcslen(name);
    if (attrs)
    {
        /* parsing doesn't look for subfolders (Windows 11 leaves SFGAO_HASSUBFOLDER clear here) */
        if (f->zip) *attrs = node_attributes(f, node) & ~SFGAO_HASSUBFOLDER;
        else *attrs &= node_attributes(f, node);
    }
    *ppidl = result;
    return S_OK;
}

static HRESULT WINAPI folder_EnumObjects(IShellFolder2 *iface, HWND hwnd, SHCONTF flags, IEnumIDList **out)
{
    struct folder *f = impl_from_IShellFolder2(iface);
    struct arc_node *node = folder_node(f), *c;
    struct enum_ids *e;
    ULONG n = 0;

    TRACE("(%p, %#lx)\n", f, flags);
    if (!out) return E_INVALIDARG;
    *out = NULL;
    if (!node) return E_FAIL;
    if (!(e = calloc(1, sizeof(*e)))) return E_OUTOFMEMORY;
    e->IEnumIDList_iface.lpVtbl = &enum_vtbl;
    e->ref = 1;
    for (c = node->child; c; c = c->next) n++;
    if (n && !(e->items = calloc(n, sizeof(*e->items))))
    {
        free(e);
        return E_OUTOFMEMORY;
    }
    for (c = node->child; c; c = c->next)
    {
        if (c->dir ? !(flags & SHCONTF_FOLDERS) : !(flags & SHCONTF_NONFOLDERS)) continue;
        if ((c->attrs & FILE_ATTRIBUTE_HIDDEN) && !(flags & SHCONTF_INCLUDEHIDDEN)) continue;
        if ((e->items[e->count] = make_pidl(c))) e->count++;
    }
    *out = &e->IEnumIDList_iface;
    return S_OK;
}

static HRESULT open_stream(struct folder *f, struct arc_node *node, IStream **out)
{
    BYTE *data;
    SIZE_T len;
    HRESULT hr;

    *out = NULL;
    if (FAILED(hr = archive_read_entry(f->arc, node, &data, &len))) return hr;
    return stream_create(node->name, &node->mtime, data, len, out);
}

static HRESULT WINAPI folder_BindToObject(IShellFolder2 *iface, LPCITEMIDLIST pidl, IBindCtx *pbc,
                                          REFIID riid, void **out)
{
    struct folder *f = impl_from_IShellFolder2(iface), *child;
    struct arc_node *node;
    HRESULT hr;

    TRACE("(%p, %p, %s)\n", f, pidl, debugstr_guid(riid));
    if (!out) return E_POINTER;
    *out = NULL;
    if (!pidl || !pidl->mkid.cb || !zpidl(pidl)) return E_INVALIDARG;
    if (!(node = pidl_node(f, pidl))) return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);

    if (!node->dir)
    {
        if (IsEqualIID(riid, &IID_IStream))
        {
            if (!f->zip) return f->cab ? E_NOTIMPL : E_NOINTERFACE;
            return open_stream(f, node, (IStream **)out);
        }
        if (IsEqualIID(riid, &IID_IStorage)) return f->zip ? E_NOTIMPL : E_NOINTERFACE;
        return E_NOINTERFACE;
    }
    if (f->zip && !next_id(pidl)->mkid.cb && IsEqualIID(riid, &IID_IStream)) return E_FAIL;  /* a folder has no stream */
    if (FAILED(hr = folder_create_child(f, pidl, &child))) return hr;
    if (next_id(pidl)->mkid.cb)
        hr = IShellFolder2_BindToObject(&child->IShellFolder2_iface, next_id(pidl), pbc, riid, out);
    else
        hr = IShellFolder2_QueryInterface(&child->IShellFolder2_iface, riid, out);
    IShellFolder2_Release(&child->IShellFolder2_iface);
    return hr;
}

static HRESULT WINAPI folder_BindToStorage(IShellFolder2 *iface, LPCITEMIDLIST pidl, IBindCtx *pbc,
                                           REFIID riid, void **out)
{
    struct folder *f = impl_from_IShellFolder2(iface);
    struct arc_node *node;

    TRACE("(%p, %p, %s)\n", f, pidl, debugstr_guid(riid));
    if (!out) return E_POINTER;
    *out = NULL;
    if (!IsEqualIID(riid, &IID_IStream) || !f->zip) return E_NOINTERFACE;
    if (!(node = pidl_node(f, pidl))) return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    if (node->dir) return E_FAIL;  /* a folder has no stream */
    return open_stream(f, node, (IStream **)out);
}

static int compare_one(int column, const struct zpidl *a, const struct zpidl *b)
{
    int r = 0;

    if ((a->flags & PIDL_DIR) != (b->flags & PIDL_DIR)) return (a->flags & PIDL_DIR) ? -1 : 1;
    switch (column)
    {
    case 1:
        r = a->size < b->size ? -1 : a->size > b->size;
        break;
    case 2:
        r = lstrcmpiW(PathFindExtensionW(a->name), PathFindExtensionW(b->name));
        break;
    case 3:
        r = CompareFileTime(&a->mtime, &b->mtime);
        break;
    }
    if (!r) r = lstrcmpiW(a->name, b->name);
    return r;
}

static HRESULT WINAPI folder_CompareIDs(IShellFolder2 *iface, LPARAM lparam, LPCITEMIDLIST p1, LPCITEMIDLIST p2)
{
    int column = LOWORD(lparam) & 0xff, r;
    const struct zpidl *a, *b;

    for (;;)
    {
        BOOL e1 = !p1 || !p1->mkid.cb, e2 = !p2 || !p2->mkid.cb;
        if (e1 || e2) return MAKE_HRESULT(SEVERITY_SUCCESS, 0, (USHORT)(e1 && e2 ? 0 : e1 ? -1 : 1));
        if (!(a = zpidl(p1)) || !(b = zpidl(p2))) return E_INVALIDARG;
        if ((r = compare_one(column, a, b))) return MAKE_HRESULT(SEVERITY_SUCCESS, 0, (USHORT)(r < 0 ? -1 : 1));
        p1 = next_id(p1);
        p2 = next_id(p2);
        column = 0;
    }
}

static HRESULT WINAPI folder_CreateViewObject(IShellFolder2 *iface, HWND hwnd, REFIID riid, void **out)
{
    struct folder *f = impl_from_IShellFolder2(iface);

    TRACE("(%p, %s)\n", f, debugstr_guid(riid));
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualIID(riid, &IID_IShellView))
    {
        SFV_CREATE sfv = { sizeof(sfv), (IShellFolder *)&f->IShellFolder2_iface, NULL, NULL };
        IShellView *view;
        HRESULT hr = SHCreateShellFolderView(&sfv, &view);
        if (SUCCEEDED(hr))
        {
            hr = IShellView_QueryInterface(view, riid, out);
            IShellView_Release(view);
        }
        return hr;
    }
    if (IsEqualIID(riid, &IID_ITransferSource))
    {
        return transfer_source_create(&f->IShellFolder2_iface, f->pidl, out);
    }
    return E_NOINTERFACE;
}

static HRESULT WINAPI folder_GetAttributesOf(IShellFolder2 *iface, UINT cidl, LPCITEMIDLIST *apidl, SFGAOF *inout)
{
    struct folder *f = impl_from_IShellFolder2(iface);
    SFGAOF all = ~0u;
    UINT i;

    TRACE("(%p, %u, %#lx)\n", f, cidl, inout ? *inout : 0);
    if (!inout) return E_INVALIDARG;
    if (!cidl)
    {
        *inout &= SFGAO_FOLDER | SFGAO_STORAGE | SFGAO_CANCOPY;
        return S_OK;
    }
    if (!apidl) return E_INVALIDARG;
    for (i = 0; i < cidl; i++)
    {
        struct arc_node *node = pidl_node(f, apidl[i]);
        if (!node) return E_INVALIDARG;
        all &= node_attributes(f, node);
    }
    if (f->zip) *inout = all;
    else *inout &= all;
    return S_OK;
}

static HRESULT WINAPI folder_GetUIObjectOf(IShellFolder2 *iface, HWND hwnd, UINT cidl, LPCITEMIDLIST *apidl,
                                           REFIID riid, UINT *reserved, void **out)
{
    struct folder *f = impl_from_IShellFolder2(iface);
    UINT i;

    TRACE("(%p, %u, %s)\n", f, cidl, debugstr_guid(riid));
    if (!out) return E_POINTER;
    *out = NULL;
    if (!cidl || !apidl) return E_INVALIDARG;
    for (i = 0; i < cidl; i++)
        if (!pidl_node(f, apidl[i])) return E_INVALIDARG;

    if (IsEqualIID(riid, &IID_IExtractIconW) || IsEqualIID(riid, &IID_IExtractIconA))
    {
        struct arc_node *node = pidl_node(f, apidl[0]);
        return SHCreateFileExtractIconW(node->name, node->dir ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL,
                                        riid, out);
    }
    if (IsEqualIID(riid, &IID_IDataObject))
        return dataobject_create(&f->IShellFolder2_iface, f->zip ? KIND_ZIP : f->cab ? KIND_CAB : KIND_ARCHIVE,
                                 f->pidl, cidl, apidl, riid, out);
    if (IsEqualIID(riid, &IID_IContextMenu) || IsEqualIID(riid, &IID_IContextMenu2) || IsEqualIID(riid, &IID_IContextMenu3))
        return item_menu_create(&f->IShellFolder2_iface, f->pidl, cidl, apidl, riid, out);
    return E_NOINTERFACE;
}

/* Full display name of the folder itself, as a file system path. */
static void folder_path(struct folder *f, WCHAR *buf, int len)
{
    lstrcpynW(buf, f->arc_path, len);
    if (*f->sub)
    {
        PathAddBackslashW(buf);
        lstrcatW(buf, f->sub);
    }
}

static HRESULT WINAPI folder_GetDisplayNameOf(IShellFolder2 *iface, LPCITEMIDLIST pidl, SHGDNF flags, STRRET *name)
{
    struct folder *f = impl_from_IShellFolder2(iface);
    WCHAR buf[MAX_PATH * 2];
    const struct zpidl *z = NULL;
    LPCITEMIDLIST p;

    TRACE("(%p, %p, %#lx)\n", f, pidl, flags);
    if (!name) return E_INVALIDARG;
    for (p = pidl; p && p->mkid.cb; p = next_id(p))
        if (!(z = zpidl(p))) return E_INVALIDARG;

    if ((flags & SHGDN_FORPARSING) && !(flags & SHGDN_INFOLDER))
    {
        folder_path(f, buf, MAX_PATH);
        for (p = pidl; p && p->mkid.cb; p = next_id(p))
        {
            PathAddBackslashW(buf);
            if (wcslen(buf) + wcslen(zpidl(p)->name) >= ARRAY_SIZE(buf)) return E_FAIL;
            lstrcatW(buf, zpidl(p)->name);
        }
    }
    else if (z)
    {
        lstrcpynW(buf, (f->cab && !(flags & SHGDN_FORPARSING)) ? PathFindFileNameW(z->name) : z->name, MAX_PATH);
        if (!(flags & SHGDN_FORPARSING) && !(z->flags & PIDL_DIR) && hide_extension(buf))
            PathRemoveExtensionW(buf);
    }
    else
    {
        folder_path(f, buf, MAX_PATH);
        if (!(flags & SHGDN_FORPARSING))
        {
            memmove(buf, PathFindFileNameW(buf), (wcslen(PathFindFileNameW(buf)) + 1) * sizeof(WCHAR));
            if (!*f->sub && hide_extension(buf)) PathRemoveExtensionW(buf);
        }
    }
    name->uType = STRRET_WSTR;
    return SHStrDupW(buf, &name->pOleStr);
}

static HRESULT WINAPI folder_SetNameOf(IShellFolder2 *iface, HWND hwnd, LPCITEMIDLIST pidl, LPCOLESTR name,
                                       SHGDNF flags, LPITEMIDLIST *out)
{
    FIXME("renaming inside an archive is not supported\n");
    if (out) *out = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI folder_GetDefaultSearchGUID(IShellFolder2 *iface, GUID *guid)
{
    return E_NOTIMPL;
}

static HRESULT WINAPI folder_EnumSearches(IShellFolder2 *iface, IEnumExtraSearch **out)
{
    *out = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI folder_GetDefaultColumn(IShellFolder2 *iface, DWORD reserved, ULONG *sort, ULONG *display)
{
    if (sort) *sort = 0;
    if (display) *display = 0;
    return S_OK;
}

enum { COL_NAME, COL_SIZE, COL_TYPE, COL_DATE, COL_COUNT };

static HRESULT WINAPI folder_GetDefaultColumnState(IShellFolder2 *iface, UINT column, SHCOLSTATEF *state)
{
    if (column >= COL_COUNT) return E_INVALIDARG;
    *state = SHCOLSTATE_ONBYDEFAULT | (column == COL_SIZE ? SHCOLSTATE_TYPE_INT :
                                       column == COL_DATE ? SHCOLSTATE_TYPE_DATE : SHCOLSTATE_TYPE_STR);
    return S_OK;
}

static const SHCOLUMNID *column_key(UINT column)
{
    switch (column)
    {
    case COL_NAME: return (const SHCOLUMNID *)&PKEY_ItemNameDisplay;
    case COL_SIZE: return (const SHCOLUMNID *)&PKEY_Size;
    case COL_TYPE: return (const SHCOLUMNID *)&PKEY_ItemTypeText;
    case COL_DATE: return (const SHCOLUMNID *)&PKEY_DateModified;
    }
    return NULL;
}

static void type_name(const struct zpidl *z, WCHAR *buf, int len)
{
    SHFILEINFOW sfi;
    if (SHGetFileInfoW(z->name, (z->flags & PIDL_DIR) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL, &sfi,
                       sizeof(sfi), SHGFI_USEFILEATTRIBUTES | SHGFI_TYPENAME))
        lstrcpynW(buf, sfi.szTypeName, len);
    else
        *buf = 0;
}

static HRESULT WINAPI folder_GetDetailsEx(IShellFolder2 *iface, LPCITEMIDLIST pidl, const SHCOLUMNID *key, VARIANT *v)
{
    const struct zpidl *z = zpidl(pidl);
    WCHAR buf[MAX_PATH];

    if (!z || !key || !v) return E_INVALIDARG;
    if (IsEqualPropertyKey(*key, PKEY_Size))
    {
        if (z->flags & PIDL_DIR) return E_FAIL;
        V_VT(v) = VT_UI8;
        V_UI8(v) = z->size;
        return S_OK;
    }
    if (IsEqualPropertyKey(*key, PKEY_DateModified))
    {
        SYSTEMTIME st;
        FILETIME local;
        DOUBLE d;
        if (!FileTimeToLocalFileTime(&z->mtime, &local) || !FileTimeToSystemTime(&local, &st)
            || !SystemTimeToVariantTime(&st, &d))
            return E_FAIL;
        V_VT(v) = VT_DATE;
        V_DATE(v) = d;
        return S_OK;
    }
    if (IsEqualPropertyKey(*key, PKEY_ItemNameDisplay) || IsEqualPropertyKey(*key, PKEY_ItemTypeText))
    {
        if (IsEqualPropertyKey(*key, PKEY_ItemTypeText)) type_name(z, buf, MAX_PATH);
        else lstrcpynW(buf, z->name, MAX_PATH);
        V_VT(v) = VT_BSTR;
        V_BSTR(v) = SysAllocString(buf);
        return V_BSTR(v) ? S_OK : E_OUTOFMEMORY;
    }
    return E_NOTIMPL;
}

static HRESULT WINAPI folder_GetDetailsOf(IShellFolder2 *iface, LPCITEMIDLIST pidl, UINT column, SHELLDETAILS *sd)
{
    static const UINT ids[] = { IDS_COL_NAME, IDS_COL_SIZE, IDS_COL_TYPE, IDS_COL_DATE };
    WCHAR buf[MAX_PATH];
    const struct zpidl *z;

    if (column >= COL_COUNT || !sd) return E_INVALIDARG;
    sd->fmt = column == COL_SIZE ? LVCFMT_RIGHT : LVCFMT_LEFT;
    sd->cxChar = column == COL_NAME ? 30 : 15;
    if (!pidl)
    {
        LoadStringW(zipfldr_instance, ids[column], buf, MAX_PATH);
    }
    else
    {
        if (!(z = zpidl(pidl))) return E_INVALIDARG;
        buf[0] = 0;
        switch (column)
        {
        case COL_NAME:
        {
            STRRET sr;
            if (SUCCEEDED(folder_GetDisplayNameOf(iface, pidl, SHGDN_INFOLDER, &sr)))
                StrRetToBufW(&sr, pidl, buf, MAX_PATH);
            break;
        }
        case COL_SIZE:
            if (!(z->flags & PIDL_DIR)) StrFormatKBSizeW(z->size, buf, MAX_PATH);
            break;
        case COL_TYPE:
            type_name(z, buf, MAX_PATH);
            break;
        case COL_DATE:
        {
            FILETIME local;
            SYSTEMTIME st;
            WCHAR t[64];
            if (FileTimeToLocalFileTime(&z->mtime, &local) && FileTimeToSystemTime(&local, &st))
            {
                GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &st, NULL, buf, 64);
                GetTimeFormatW(LOCALE_USER_DEFAULT, TIME_NOSECONDS, &st, NULL, t, 64);
                lstrcatW(buf, L" ");
                lstrcatW(buf, t);
            }
            break;
        }
        }
    }
    sd->str.uType = STRRET_WSTR;
    return SHStrDupW(buf, &sd->str.pOleStr);
}

static HRESULT WINAPI folder_MapColumnToSCID(IShellFolder2 *iface, UINT column, SHCOLUMNID *scid)
{
    const SHCOLUMNID *key = column_key(column);
    if (!key) return E_INVALIDARG;
    *scid = *key;
    return S_OK;
}

static const IShellFolder2Vtbl folder_vtbl =
{
    folder_QueryInterface,
    folder_AddRef,
    folder_Release,
    folder_ParseDisplayName,
    folder_EnumObjects,
    folder_BindToObject,
    folder_BindToStorage,
    folder_CompareIDs,
    folder_CreateViewObject,
    folder_GetAttributesOf,
    folder_GetUIObjectOf,
    folder_GetDisplayNameOf,
    folder_SetNameOf,
    folder_GetDefaultSearchGUID,
    folder_EnumSearches,
    folder_GetDefaultColumn,
    folder_GetDefaultColumnState,
    folder_GetDetailsEx,
    folder_GetDetailsOf,
    folder_MapColumnToSCID,
};

/* ---- IPersistFolder2 ---- */

static HRESULT WINAPI persist_QueryInterface(IPersistFolder2 *iface, REFIID riid, void **out)
{
    return IShellFolder2_QueryInterface(&impl_from_IPersistFolder2(iface)->IShellFolder2_iface, riid, out);
}

static ULONG WINAPI persist_AddRef(IPersistFolder2 *iface)
{
    return IShellFolder2_AddRef(&impl_from_IPersistFolder2(iface)->IShellFolder2_iface);
}

static ULONG WINAPI persist_Release(IPersistFolder2 *iface)
{
    return IShellFolder2_Release(&impl_from_IPersistFolder2(iface)->IShellFolder2_iface);
}

static HRESULT WINAPI persist_GetClassID(IPersistFolder2 *iface, CLSID *clsid)
{
    if (!clsid) return E_POINTER;
    *clsid = *folder_clsid(impl_from_IPersistFolder2(iface));
    return S_OK;
}

static HRESULT WINAPI persist_Initialize(IPersistFolder2 *iface, LPCITEMIDLIST pidl)
{
    struct folder *f = impl_from_IPersistFolder2(iface);

    TRACE("(%p, %p)\n", f, pidl);
    ILFree(f->pidl);
    archive_release(f->arc);
    f->arc = NULL;
    f->loaded = FALSE;
    f->sub[0] = 0;
    if (!(f->pidl = ILClone(pidl))) return E_OUTOFMEMORY;
    /* Only archives that are files can be opened; an archive inside an
     * archive shows as an empty folder. */
    if (!SHGetPathFromIDListW(pidl, f->arc_path)) f->arc_path[0] = 0;
    TRACE("archive %s\n", debugstr_w(f->arc_path));
    return S_OK;
}

static HRESULT WINAPI persist_GetCurFolder(IPersistFolder2 *iface, LPITEMIDLIST *pidl)
{
    struct folder *f = impl_from_IPersistFolder2(iface);
    if (!pidl) return E_POINTER;
    *pidl = ILClone(f->pidl);
    return *pidl ? S_OK : S_FALSE;
}

static const IPersistFolder2Vtbl persist_vtbl =
{
    persist_QueryInterface, persist_AddRef, persist_Release, persist_GetClassID, persist_Initialize,
    persist_GetCurFolder
};

/* ---- IStorage (zip only; the archive can't be changed through it yet) ---- */

static HRESULT WINAPI storage_QueryInterface(IStorage *iface, REFIID riid, void **out)
{
    return IShellFolder2_QueryInterface(&impl_from_IStorage(iface)->IShellFolder2_iface, riid, out);
}

static ULONG WINAPI storage_AddRef(IStorage *iface)
{
    return IShellFolder2_AddRef(&impl_from_IStorage(iface)->IShellFolder2_iface);
}

static ULONG WINAPI storage_Release(IStorage *iface)
{
    return IShellFolder2_Release(&impl_from_IStorage(iface)->IShellFolder2_iface);
}

static HRESULT WINAPI storage_CreateStream(IStorage *iface, LPCOLESTR name, DWORD mode, DWORD r1, DWORD r2, IStream **out)
{
    FIXME("writing into archives is not supported\n");
    *out = NULL;
    return STG_E_ACCESSDENIED;
}

static HRESULT WINAPI storage_OpenStream(IStorage *iface, LPCOLESTR name, void *r1, DWORD mode, DWORD r2, IStream **out)
{
    struct folder *f = impl_from_IStorage(iface);
    struct arc_node *dir = folder_node(f), *node;

    *out = NULL;
    if (!dir || !(node = archive_find(f->arc, dir, name)) || node->dir) return STG_E_FILENOTFOUND;
    if ((mode & 3) != STGM_READ) return STG_E_ACCESSDENIED;
    return open_stream(f, node, out);
}

static HRESULT WINAPI storage_CreateStorage(IStorage *iface, LPCOLESTR name, DWORD mode, DWORD r1, DWORD r2, IStorage **out)
{
    *out = NULL;
    return STG_E_ACCESSDENIED;
}

static HRESULT WINAPI storage_OpenStorage(IStorage *iface, LPCOLESTR name, IStorage *prio, DWORD mode,
                                          SNB exclude, DWORD reserved, IStorage **out)
{
    *out = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI storage_CopyTo(IStorage *iface, DWORD n, const IID *exclude, SNB snb, IStorage *dest)
{
    return E_NOTIMPL;
}

static HRESULT WINAPI storage_MoveElementTo(IStorage *iface, LPCOLESTR name, IStorage *dest, LPCOLESTR newname, DWORD flags)
{
    return E_NOTIMPL;
}

static HRESULT WINAPI storage_Commit(IStorage *iface, DWORD flags)
{
    return S_OK;
}

static HRESULT WINAPI storage_Revert(IStorage *iface)
{
    return S_OK;
}

static HRESULT WINAPI storage_EnumElements(IStorage *iface, DWORD r1, void *r2, DWORD r3, IEnumSTATSTG **out)
{
    *out = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI storage_DestroyElement(IStorage *iface, LPCOLESTR name)
{
    return STG_E_ACCESSDENIED;
}

static HRESULT WINAPI storage_RenameElement(IStorage *iface, LPCOLESTR from, LPCOLESTR to)
{
    return STG_E_ACCESSDENIED;
}

static HRESULT WINAPI storage_SetElementTimes(IStorage *iface, LPCOLESTR name, const FILETIME *c, const FILETIME *a,
                                              const FILETIME *m)
{
    return STG_E_ACCESSDENIED;
}

static HRESULT WINAPI storage_SetClass(IStorage *iface, REFCLSID clsid)
{
    return STG_E_ACCESSDENIED;
}

static HRESULT WINAPI storage_SetStateBits(IStorage *iface, DWORD bits, DWORD mask)
{
    return STG_E_ACCESSDENIED;
}

static HRESULT WINAPI storage_Stat(IStorage *iface, STATSTG *st, DWORD flags)
{
    struct folder *f = impl_from_IStorage(iface);
    WCHAR path[MAX_PATH];

    memset(st, 0, sizeof(*st));
    st->type = STGTY_STORAGE;
    st->clsid = CLSID_ZipFolder;
    st->grfMode = STGM_READ;
    if (!(flags & STATFLAG_NONAME))
    {
        folder_path(f, path, MAX_PATH);
        return SHStrDupW(PathFindFileNameW(path), &st->pwcsName);
    }
    return S_OK;
}

static const IStorageVtbl storage_vtbl =
{
    storage_QueryInterface, storage_AddRef, storage_Release, storage_CreateStream, storage_OpenStream,
    storage_CreateStorage, storage_OpenStorage, storage_CopyTo, storage_MoveElementTo, storage_Commit,
    storage_Revert, storage_EnumElements, storage_DestroyElement, storage_RenameElement,
    storage_SetElementTimes, storage_SetClass, storage_SetStateBits, storage_Stat
};

/* ---- creation ---- */

static struct folder *folder_alloc(BOOL zip, BOOL cab)
{
    struct folder *f = calloc(1, sizeof(*f));
    if (!f) return NULL;
    f->IShellFolder2_iface.lpVtbl = &folder_vtbl;
    f->IPersistFolder2_iface.lpVtbl = &persist_vtbl;
    f->IStorage_iface.lpVtbl = &storage_vtbl;
    f->ref = 1;
    f->zip = zip;
    f->cab = cab;
    InterlockedIncrement(&zipfldr_locks);
    return f;
}

static HRESULT folder_create_child(struct folder *parent, LPCITEMIDLIST child, struct folder **out)
{
    const struct zpidl *z = zpidl(child);
    struct folder *f;
    ITEMIDLIST *first;

    *out = NULL;
    if (!z) return E_INVALIDARG;
    if (!(f = folder_alloc(parent->zip, parent->cab))) return E_OUTOFMEMORY;
    if (!(first = ILCloneFirst(child)) || !(f->pidl = ILCombine(parent->pidl, first)))
    {
        ILFree(first);
        IShellFolder2_Release(&f->IShellFolder2_iface);
        return E_OUTOFMEMORY;
    }
    ILFree(first);
    lstrcpyW(f->arc_path, parent->arc_path);
    lstrcpynW(f->sub, parent->sub, MAX_PATH);
    if (*f->sub) lstrcatW(f->sub, L"\\");
    if (wcslen(f->sub) + wcslen(z->name) >= MAX_PATH)
    {
        IShellFolder2_Release(&f->IShellFolder2_iface);
        return HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE);
    }
    lstrcatW(f->sub, z->name);
    if ((f->arc = folder_archive(parent))) InterlockedIncrement(&f->arc->ref);
    f->loaded = TRUE;
    *out = f;
    return S_OK;
}

HRESULT zip_folder_create(const CLSID *clsid, REFIID riid, void **out)
{
    struct folder *f = folder_alloc(IsEqualCLSID(clsid, &CLSID_ZipFolder), IsEqualCLSID(clsid, &CLSID_CABFolder));
    HRESULT hr;

    if (!f) return E_OUTOFMEMORY;
    hr = IShellFolder2_QueryInterface(&f->IShellFolder2_iface, riid, out);
    IShellFolder2_Release(&f->IShellFolder2_iface);
    return hr;
}

/* For the data object and the transfer source: the node of a child, the
 * folder's archive, and the folder's path inside it. */
struct arc_node *folder_lookup(IShellFolder2 *iface, LPCITEMIDLIST pidl, struct archive **arc)
{
    struct folder *f = impl_from_IShellFolder2(iface);
    struct arc_node *node = pidl_node(f, pidl);
    *arc = f->arc;
    return node;
}
