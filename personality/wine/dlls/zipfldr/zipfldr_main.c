/*
 * Compressed folders: class factory and registration
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

#include "zipfldr_private.h"
#include "initguid.h"
#include "rpcproxy.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(zipfldr);

/* The class IDs Windows 11 registers for zipfldr.dll (registry, observed
 * through tests/win32/shell32/compressed_folders.c). */
DEFINE_GUID(CLSID_ZipFolder, 0xe88dcce0, 0xb7b3, 0x11d1, 0xa9, 0xf0, 0x00, 0xaa, 0x00, 0x60, 0xfa, 0x31);
DEFINE_GUID(CLSID_ArchiveFolder, 0x0c1fd748, 0xb888, 0x443d, 0x9e, 0xc3, 0xad, 0x7e, 0x22, 0xd4, 0x88, 0x08);
DEFINE_GUID(CLSID_CABFolder, 0x0cd7a5c0, 0x9f37, 0x11ce, 0xae, 0x65, 0x08, 0x00, 0x2b, 0x2e, 0x12, 0x62);
DEFINE_GUID(CLSID_ZipExtractMenu, 0xb8cdcb65, 0xb1bf, 0x4b42, 0x94, 0x28, 0x1d, 0xfd, 0xb7, 0xee, 0x92, 0xaf);
DEFINE_GUID(CLSID_ArchiveExtractMenu, 0xee07cef5, 0x3441, 0x4cfb, 0x87, 0x0a, 0x40, 0x02, 0xc7, 0x24, 0x78, 0x3a);

HINSTANCE zipfldr_instance;
LONG zipfldr_locks;

struct factory
{
    IClassFactory IClassFactory_iface;
    const CLSID *clsid;
    HRESULT (*create)(const CLSID *clsid, REFIID riid, void **out);
};

static inline struct factory *impl_from_IClassFactory(IClassFactory *iface)
{
    return CONTAINING_RECORD(iface, struct factory, IClassFactory_iface);
}

static HRESULT WINAPI factory_QueryInterface(IClassFactory *iface, REFIID riid, void **out)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IClassFactory))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI factory_AddRef(IClassFactory *iface)
{
    return 2;
}

static ULONG WINAPI factory_Release(IClassFactory *iface)
{
    return 1;
}

static HRESULT WINAPI factory_CreateInstance(IClassFactory *iface, IUnknown *outer, REFIID riid, void **out)
{
    struct factory *f = impl_from_IClassFactory(iface);

    TRACE("(%s, %s)\n", debugstr_guid(f->clsid), debugstr_guid(riid));
    if (!out) return E_POINTER;
    *out = NULL;
    if (outer) return CLASS_E_NOAGGREGATION;
    return f->create(f->clsid, riid, out);
}

static HRESULT WINAPI factory_LockServer(IClassFactory *iface, BOOL lock)
{
    if (lock) InterlockedIncrement(&zipfldr_locks);
    else InterlockedDecrement(&zipfldr_locks);
    return S_OK;
}

static const IClassFactoryVtbl factory_vtbl =
{
    factory_QueryInterface, factory_AddRef, factory_Release, factory_CreateInstance, factory_LockServer
};

static struct factory factories[] =
{
    { { &factory_vtbl }, &CLSID_ZipFolder, zip_folder_create },
    { { &factory_vtbl }, &CLSID_ArchiveFolder, zip_folder_create },
    { { &factory_vtbl }, &CLSID_CABFolder, zip_folder_create },
    { { &factory_vtbl }, &CLSID_ZipExtractMenu, extract_menu_create },
    { { &factory_vtbl }, &CLSID_ArchiveExtractMenu, extract_menu_create },
};

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        zipfldr_instance = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}

HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void **out)
{
    unsigned int i;

    TRACE("(%s, %s)\n", debugstr_guid(clsid), debugstr_guid(riid));
    *out = NULL;
    for (i = 0; i < ARRAY_SIZE(factories); i++)
        if (IsEqualCLSID(clsid, factories[i].clsid))
            return IClassFactory_QueryInterface(&factories[i].IClassFactory_iface, riid, out);
    return CLASS_E_CLASSNOTAVAILABLE;
}

HRESULT WINAPI DllCanUnloadNow(void)
{
    return zipfldr_locks ? S_FALSE : S_OK;
}

HRESULT WINAPI DllRegisterServer(void)
{
    return __wine_register_resources();
}

HRESULT WINAPI DllUnregisterServer(void)
{
    return __wine_unregister_resources();
}
