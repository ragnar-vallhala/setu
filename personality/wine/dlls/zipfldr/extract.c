/*
 * Compressed folders: the "Extract All..." verb
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
#include "zipfldr_private.h"
#include "shlwapi.h"
#include "shellapi.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(zipfldr);

struct extract_menu
{
    IShellExtInit IShellExtInit_iface;
    IContextMenu IContextMenu_iface;
    LONG ref;
    WCHAR path[MAX_PATH];       /* the archive */
};

static inline struct extract_menu *impl_from_IShellExtInit(IShellExtInit *iface)
{
    return CONTAINING_RECORD(iface, struct extract_menu, IShellExtInit_iface);
}

static inline struct extract_menu *impl_from_IContextMenu(IContextMenu *iface)
{
    return CONTAINING_RECORD(iface, struct extract_menu, IContextMenu_iface);
}

static HRESULT WINAPI init_QueryInterface(IShellExtInit *iface, REFIID riid, void **out)
{
    struct extract_menu *m = impl_from_IShellExtInit(iface);

    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IShellExtInit))
        *out = &m->IShellExtInit_iface;
    else if (IsEqualIID(riid, &IID_IContextMenu))
        *out = &m->IContextMenu_iface;
    else
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    IUnknown_AddRef((IUnknown *)*out);
    return S_OK;
}

static ULONG WINAPI init_AddRef(IShellExtInit *iface)
{
    return InterlockedIncrement(&impl_from_IShellExtInit(iface)->ref);
}

static ULONG WINAPI init_Release(IShellExtInit *iface)
{
    struct extract_menu *m = impl_from_IShellExtInit(iface);
    ULONG ref = InterlockedDecrement(&m->ref);
    if (!ref)
    {
        free(m);
        InterlockedDecrement(&zipfldr_locks);
    }
    return ref;
}

static HRESULT WINAPI init_Initialize(IShellExtInit *iface, LPCITEMIDLIST folder, IDataObject *obj, HKEY key)
{
    struct extract_menu *m = impl_from_IShellExtInit(iface);
    FORMATETC fmt = { CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    STGMEDIUM medium;
    HRESULT hr;

    TRACE("(%p, %p, %p)\n", m, folder, obj);
    m->path[0] = 0;
    if (!obj) return E_INVALIDARG;
    if (SUCCEEDED(hr = IDataObject_GetData(obj, &fmt, &medium)))
    {
        DragQueryFileW(medium.hGlobal, 0, m->path, MAX_PATH);
        ReleaseStgMedium(&medium);
    }
    else
    {
        fmt.cfFormat = RegisterClipboardFormatW(CFSTR_SHELLIDLISTW);
        if (FAILED(hr = IDataObject_GetData(obj, &fmt, &medium))) return hr;
        {
            CIDA *cida = GlobalLock(medium.hGlobal);
            LPITEMIDLIST pidl = cida && cida->cidl ?
                ILCombine((LPCITEMIDLIST)((BYTE *)cida + cida->aoffset[0]),
                          (LPCITEMIDLIST)((BYTE *)cida + cida->aoffset[1])) : NULL;
            if (!pidl || !SHGetPathFromIDListW(pidl, m->path)) m->path[0] = 0;
            ILFree(pidl);
            GlobalUnlock(medium.hGlobal);
        }
        ReleaseStgMedium(&medium);
    }
    TRACE("archive %s\n", debugstr_w(m->path));
    return *m->path ? S_OK : E_FAIL;
}

static const IShellExtInitVtbl init_vtbl =
{
    init_QueryInterface, init_AddRef, init_Release, init_Initialize
};

static HRESULT WINAPI menu_QueryInterface(IContextMenu *iface, REFIID riid, void **out)
{
    return init_QueryInterface(&impl_from_IContextMenu(iface)->IShellExtInit_iface, riid, out);
}

static ULONG WINAPI menu_AddRef(IContextMenu *iface)
{
    return init_AddRef(&impl_from_IContextMenu(iface)->IShellExtInit_iface);
}

static ULONG WINAPI menu_Release(IContextMenu *iface)
{
    return init_Release(&impl_from_IContextMenu(iface)->IShellExtInit_iface);
}

static HRESULT WINAPI menu_QueryContextMenu(IContextMenu *iface, HMENU menu, UINT index, UINT first, UINT last, UINT flags)
{
    WCHAR text[64];

    if ((flags & CMF_DEFAULTONLY) || first > last) return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 0);
    LoadStringW(zipfldr_instance, IDS_EXTRACT_MENU, text, ARRAY_SIZE(text));
    InsertMenuW(menu, index, MF_BYPOSITION | MF_STRING, first, text);
    return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 1);
}

/* "C:\dir\name.tar.gz" -> "C:\dir\name" */
static void default_destination(const WCHAR *archive, WCHAR *dest)
{
    lstrcpynW(dest, archive, MAX_PATH);
    PathRemoveExtensionW(dest);
    if (!lstrcmpiW(PathFindExtensionW(dest), L".tar")) PathRemoveExtensionW(dest);
    if (!lstrcmpW(dest, archive)) lstrcatW(dest, L"_files");
}

struct dialog_data
{
    struct extract_menu *menu;
    WCHAR dest[MAX_PATH];
    BOOL show;
};

static INT_PTR CALLBACK extract_dlg_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    struct dialog_data *data = (struct dialog_data *)GetWindowLongPtrW(hwnd, DWLP_USER);

    switch (msg)
    {
    case WM_INITDIALOG:
        SetWindowLongPtrW(hwnd, DWLP_USER, lparam);
        data = (struct dialog_data *)lparam;
        SetDlgItemTextW(hwnd, IDC_DEST, data->dest);
        CheckDlgButton(hwnd, IDC_SHOW, BST_CHECKED);
        return TRUE;
    case WM_COMMAND:
        switch (LOWORD(wparam))
        {
        case IDC_BROWSE:
        {
            BROWSEINFOW bi = { hwnd };
            WCHAR title[128], path[MAX_PATH];
            LPITEMIDLIST pidl;

            LoadStringW(zipfldr_instance, IDS_EXTRACT_TITLE, title, ARRAY_SIZE(title));
            bi.lpszTitle = title;
            bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
            if ((pidl = SHBrowseForFolderW(&bi)))
            {
                if (SHGetPathFromIDListW(pidl, path)) SetDlgItemTextW(hwnd, IDC_DEST, path);
                ILFree(pidl);
            }
            return TRUE;
        }
        case IDOK:
            GetDlgItemTextW(hwnd, IDC_DEST, data->dest, MAX_PATH);
            data->show = IsDlgButtonChecked(hwnd, IDC_SHOW) == BST_CHECKED;
            EndDialog(hwnd, IDOK);
            return TRUE;
        case IDCANCEL:
            EndDialog(hwnd, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

static HRESULT WINAPI menu_InvokeCommand(IContextMenu *iface, CMINVOKECOMMANDINFO *info)
{
    struct extract_menu *m = impl_from_IContextMenu(iface);
    struct dialog_data data = { m };
    HRESULT hr;

    if (IS_INTRESOURCE(info->lpVerb) ? LOWORD(info->lpVerb) != 0 : lstrcmpiA(info->lpVerb, "extract"))
        return E_INVALIDARG;
    if (!*m->path) return E_FAIL;
    default_destination(m->path, data.dest);
    if (!(info->fMask & CMIC_MASK_FLAG_NO_UI))
    {
        if (DialogBoxParamW(zipfldr_instance, MAKEINTRESOURCEW(IDD_EXTRACT), info->hwnd, extract_dlg_proc,
                            (LPARAM)&data) != IDOK)
            return S_OK;
    }
    TRACE("extracting %s to %s\n", debugstr_w(m->path), debugstr_w(data.dest));
    hr = archive_extract_all(m->path, data.dest);
    if (FAILED(hr))
    {
        if (!(info->fMask & CMIC_MASK_FLAG_NO_UI))
        {
            WCHAR text[256], title[128];
            LoadStringW(zipfldr_instance, IDS_EXTRACT_FAILED, text, ARRAY_SIZE(text));
            LoadStringW(zipfldr_instance, IDS_EXTRACT_TITLE, title, ARRAY_SIZE(title));
            MessageBoxW(info->hwnd, text, title, MB_OK | MB_ICONERROR);
        }
        return hr;
    }
    if (data.show) ShellExecuteW(info->hwnd, NULL, data.dest, NULL, NULL, SW_SHOWNORMAL);
    return S_OK;
}

static HRESULT WINAPI menu_GetCommandString(IContextMenu *iface, UINT_PTR cmd, UINT type, UINT *reserved,
                                            LPSTR name, UINT len)
{
    if (cmd != 0) return E_INVALIDARG;
    switch (type)
    {
    case GCS_VERBA:
        lstrcpynA(name, "extract", len);
        return S_OK;
    case GCS_VERBW:
        lstrcpynW((WCHAR *)name, L"extract", len);
        return S_OK;
    case GCS_HELPTEXTW:
        LoadStringW(zipfldr_instance, IDS_EXTRACT_HELP, (WCHAR *)name, len);
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

HRESULT extract_menu_create(const CLSID *clsid, REFIID riid, void **out)
{
    struct extract_menu *m = calloc(1, sizeof(*m));
    HRESULT hr;

    *out = NULL;
    if (!m) return E_OUTOFMEMORY;
    m->IShellExtInit_iface.lpVtbl = &init_vtbl;
    m->IContextMenu_iface.lpVtbl = &menu_vtbl;
    m->ref = 1;
    InterlockedIncrement(&zipfldr_locks);
    hr = init_QueryInterface(&m->IShellExtInit_iface, riid, out);
    init_Release(&m->IShellExtInit_iface);
    return hr;
}
