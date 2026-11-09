/*
 * Compressed folders: archives (zip, tar, gzip) as shell folders
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

#ifndef __ZIPFLDR_PRIVATE_H
#define __ZIPFLDR_PRIVATE_H

#define COBJMACROS
#include <stdarg.h>
#include "windef.h"
#include "winbase.h"
#include "winuser.h"
#include "ole2.h"
#include "shlobj.h"
#include "shobjidl.h"
#include "resource.h"

extern HINSTANCE zipfldr_instance;
extern LONG zipfldr_locks;

/* An entry of an archive, as a tree: directories hold their children. */
struct arc_node
{
    WCHAR *name;
    BOOL dir;
    BOOL explicit_dir;          /* the archive has an entry for the directory itself */
    DWORD attrs;                /* FILE_ATTRIBUTE_HIDDEN etc. from the archive */
    ULONGLONG size;
    FILETIME mtime;
    /* where the data is */
    ULONGLONG offset;           /* zip: local header; tar: start of the data */
    ULONGLONG csize;            /* zip: compressed size */
    WORD method;                /* zip: compression method */
    WORD flags;                 /* zip: general purpose flags */
    struct arc_node *parent, *child, *last_child, *next;
};

enum arc_format { ARC_NONE, ARC_ZIP, ARC_TAR, ARC_GZ };

struct archive
{
    LONG ref;
    WCHAR path[MAX_PATH];
    FILETIME stamp;             /* last write time when it was read */
    ULONGLONG file_size;
    enum arc_format format;
    BOOL valid;                 /* read without errors */
    BOOL sorted;                /* entries sorted by name (not archive order) */
    BYTE *data;                 /* tar inside gzip, or the gzip member: decompressed */
    SIZE_T data_len;
    struct arc_node root;
};

/* archive.c */
struct archive *archive_open(const WCHAR *path, BOOL sorted);
void archive_release(struct archive *arc);
struct arc_node *archive_find(struct archive *arc, struct arc_node *dir, const WCHAR *name);
HRESULT archive_read_entry(struct archive *arc, const struct arc_node *node, BYTE **data, SIZE_T *len);
BOOL node_has_subdirs(const struct arc_node *node);
void archive_sanitize_name(const WCHAR *in, WCHAR *out, int len);
HRESULT archive_extract_all(const WCHAR *path, const WCHAR *dest);

/* folder.c */
HRESULT zip_folder_create(const CLSID *clsid, REFIID riid, void **out);
struct arc_node *folder_lookup(IShellFolder2 *folder, LPCITEMIDLIST pidl, struct archive **arc);

/* dataobj.c */
HRESULT stream_create(const WCHAR *name, const FILETIME *mtime, BYTE *data, SIZE_T len, IStream **out);
HRESULT dataobject_create(IShellFolder2 *folder, BOOL zip, LPCITEMIDLIST parent, UINT cidl,
                          LPCITEMIDLIST *apidl, REFIID riid, void **out);
HRESULT transfer_source_create(IShellFolder2 *folder, LPCITEMIDLIST pidl, void **out);
HRESULT item_menu_create(IShellFolder2 *folder, LPCITEMIDLIST parent, UINT cidl,
                         LPCITEMIDLIST *apidl, REFIID riid, void **out);

/* extract.c */
HRESULT extract_menu_create(const CLSID *clsid, REFIID riid, void **out);

extern const CLSID CLSID_ZipFolder;         /* {E88DCCE0-B7B3-11d1-A9F0-00AA0060FA31} */
extern const CLSID CLSID_ArchiveFolder;     /* {0C1FD748-B888-443D-9EC3-AD7E22D48808} */
extern const CLSID CLSID_ZipExtractMenu;    /* {b8cdcb65-b1bf-4b42-9428-1dfdb7ee92af} */
extern const CLSID CLSID_ArchiveExtractMenu;/* {EE07CEF5-3441-4CFB-870A-4002C724783A} */

#endif
