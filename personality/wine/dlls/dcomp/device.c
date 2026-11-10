/*
 * Copyright 2020 Nikolay Sivov for CodeWeavers
 * Copyright 2026 Setu contributors
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

#include "initguid.h"
#include "dcomp_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(dcomp);

static CRITICAL_SECTION_DEBUG dcomp_cs_debug =
{
    0, 0, &dcomp_cs,
    { &dcomp_cs_debug.ProcessLocksList, &dcomp_cs_debug.ProcessLocksList },
      0, 0, { (DWORD_PTR)(__FILE__ ": dcomp_cs") }
};
CRITICAL_SECTION dcomp_cs = { &dcomp_cs_debug, -1, 0, 0, 0, 0 };

static inline struct composition_device *impl_from_IDCompositionDevice(IDCompositionDevice *iface)
{
    return CONTAINING_RECORD(iface, struct composition_device, IDCompositionDevice_iface);
}

static inline struct composition_device *impl_from_IDCompositionDesktopDevice(IDCompositionDesktopDevice *iface)
{
    return CONTAINING_RECORD(iface, struct composition_device, IDCompositionDesktopDevice_iface);
}

static inline struct composition_device *impl_from_IDCompositionDevice3(IDCompositionDevice3 *iface)
{
    return CONTAINING_RECORD(iface, struct composition_device, IDCompositionDevice3_iface);
}

static inline struct composition_device *impl_from_IDCompositionDeviceDebug(IDCompositionDeviceDebug *iface)
{
    return CONTAINING_RECORD(iface, struct composition_device, IDCompositionDeviceDebug_iface);
}

/* dcomp/device_create: dev*_qi. A device made by DCompositionCreateDevice
 * only answers IUnknown and IDCompositionDevice; one made by
 * DCompositionCreateDevice2 or 3 also answers IDCompositionDevice2 and
 * IDCompositionDesktopDevice (one pointer, separate from the IUnknown) and
 * IDCompositionDeviceDebug, and IDCompositionDevice3 when made by
 * DCompositionCreateDevice3. */
static HRESULT device_query_interface(struct composition_device *device, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IDCompositionDevice))
        *out = &device->IDCompositionDevice_iface;
    else if (device->version >= 2 && (IsEqualGUID(iid, &IID_IDCompositionDevice2)
            || IsEqualGUID(iid, &IID_IDCompositionDesktopDevice)))
        *out = &device->IDCompositionDesktopDevice_iface;
    else if (device->version >= 3 && IsEqualGUID(iid, &IID_IDCompositionDevice3))
        *out = &device->IDCompositionDevice3_iface;
    else if (device->version >= 2 && IsEqualGUID(iid, &IID_IDCompositionDeviceDebug))
        *out = &device->IDCompositionDeviceDebug_iface;
    else
    {
        WARN("%s not implemented.\n", debugstr_guid(iid));
        *out = NULL;
        return E_NOINTERFACE;
    }
    InterlockedIncrement(&device->refcount);
    return S_OK;
}

static ULONG device_addref(struct composition_device *device)
{
    ULONG refcount = InterlockedIncrement(&device->refcount);
    TRACE("%p increasing refcount to %lu.\n", device, refcount);
    return refcount;
}

static ULONG device_release(struct composition_device *device)
{
    ULONG refcount = InterlockedDecrement(&device->refcount);

    TRACE("%p decreasing refcount to %lu.\n", device, refcount);

    if (!refcount)
    {
        if (device->d3d_device) ID3D11Device_Release(device->d3d_device);
        if (device->d2d_device) ID2D1Device_Release(device->d2d_device);
        free(device);
    }
    return refcount;
}

static HRESULT device_commit(struct composition_device *device)
{
    TRACE("device %p.\n", device);
    QueryPerformanceCounter(&device->last_commit);
    compose_device(device);
    return S_OK;
}

/* dcomp/device_create: methods_* (the times are QueryPerformanceCounter
 * times) */
static HRESULT device_get_frame_statistics(struct composition_device *device, DCOMPOSITION_FRAME_STATISTICS *stats)
{
    LARGE_INTEGER now, frequency;

    TRACE("device %p, stats %p.\n", device, stats);

    if (!stats) return E_INVALIDARG;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&frequency);
    stats->lastFrameTime = device->last_commit.QuadPart ? device->last_commit : now;
    stats->currentCompositionRate.Numerator = 60;
    stats->currentCompositionRate.Denominator = 1;
    stats->currentTime = now;
    stats->timeFrequency = frequency;
    stats->nextEstimatedFrameTime.QuadPart = now.QuadPart + frequency.QuadPart / 60;
    return S_OK;
}

/* dcomp/surface_draw: surface_no_device (a device made without a rendering
 * device can't make surfaces) */
static HRESULT device_create_surface(struct composition_device *device, UINT width, UINT height,
        DXGI_FORMAT format, DXGI_ALPHA_MODE alpha_mode, BOOL is_virtual, IDCompositionVirtualSurface **out)
{
    TRACE("device %p, %ux%u, format %#x, alpha mode %#x, virtual %d, out %p.\n",
            device, width, height, format, alpha_mode, is_virtual, out);

    if (!out) return E_INVALIDARG;
    *out = NULL;
    if (!device->d3d_device && !device->d2d_device) return E_ILLEGAL_METHOD_CALL;
    return create_surface(device, device->d3d_device, device->d2d_device, width, height, format, alpha_mode,
            is_virtual, out);
}

/* ---- IDCompositionDevice ----------------------------------------------- */

static HRESULT STDMETHODCALLTYPE device_QueryInterface(IDCompositionDevice *iface, REFIID iid, void **out)
{
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);
    return device_query_interface(impl_from_IDCompositionDevice(iface), iid, out);
}

static ULONG STDMETHODCALLTYPE device_AddRef(IDCompositionDevice *iface)
{
    return device_addref(impl_from_IDCompositionDevice(iface));
}

static ULONG STDMETHODCALLTYPE device_Release(IDCompositionDevice *iface)
{
    return device_release(impl_from_IDCompositionDevice(iface));
}

static HRESULT STDMETHODCALLTYPE device_Commit(IDCompositionDevice *iface)
{
    return device_commit(impl_from_IDCompositionDevice(iface));
}

static HRESULT STDMETHODCALLTYPE device_WaitForCommitCompletion(IDCompositionDevice *iface)
{
    TRACE("iface %p.\n", iface);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE device_GetFrameStatistics(IDCompositionDevice *iface,
        DCOMPOSITION_FRAME_STATISTICS *stats)
{
    return device_get_frame_statistics(impl_from_IDCompositionDevice(iface), stats);
}

static HRESULT STDMETHODCALLTYPE device_CreateTargetForHwnd(IDCompositionDevice *iface, HWND hwnd,
        BOOL topmost, IDCompositionTarget **target)
{
    TRACE("iface %p, hwnd %p, topmost %d, target %p.\n", iface, hwnd, topmost, target);
    return create_target(impl_from_IDCompositionDevice(iface), hwnd, topmost, target);
}

static HRESULT STDMETHODCALLTYPE device_CreateVisual(IDCompositionDevice *iface, IDCompositionVisual **visual)
{
    struct composition_device *device = impl_from_IDCompositionDevice(iface);
    TRACE("iface %p, visual %p.\n", iface, visual);
    return create_visual(device, device->version == 1, (IDCompositionVisual2 **)visual);
}

static HRESULT STDMETHODCALLTYPE device_CreateSurface(IDCompositionDevice *iface, UINT width, UINT height,
        DXGI_FORMAT format, DXGI_ALPHA_MODE alpha_mode, IDCompositionSurface **surface)
{
    return device_create_surface(impl_from_IDCompositionDevice(iface), width, height, format, alpha_mode,
            FALSE, (IDCompositionVirtualSurface **)surface);
}

static HRESULT STDMETHODCALLTYPE device_CreateVirtualSurface(IDCompositionDevice *iface, UINT width, UINT height,
        DXGI_FORMAT format, DXGI_ALPHA_MODE alpha_mode, IDCompositionVirtualSurface **surface)
{
    return device_create_surface(impl_from_IDCompositionDevice(iface), width, height, format, alpha_mode,
            TRUE, surface);
}

static HRESULT STDMETHODCALLTYPE device_CreateSurfaceFromHandle(IDCompositionDevice *iface, HANDLE handle,
        IUnknown **surface)
{
    TRACE("iface %p, handle %p, surface %p.\n", iface, handle, surface);
    return create_surface_from_handle(impl_from_IDCompositionDevice(iface), handle, surface);
}

static HRESULT STDMETHODCALLTYPE device_CreateSurfaceFromHwnd(IDCompositionDevice *iface, HWND hwnd,
        IUnknown **surface)
{
    TRACE("iface %p, hwnd %p, surface %p.\n", iface, hwnd, surface);
    return create_surface_from_hwnd(impl_from_IDCompositionDevice(iface), hwnd, surface);
}

#define DEVICE_FORWARD(prefix, type, impl, name, ctor, out_type) \
static HRESULT STDMETHODCALLTYPE prefix##_##name(type *iface, out_type **out) \
{ \
    TRACE("iface %p, out %p.\n", iface, out); \
    return ctor(impl(iface), out); \
}

#define DEVICE_OBJECT_METHODS(prefix, type, impl) \
DEVICE_FORWARD(prefix, type, impl, CreateTranslateTransform, create_translate_transform, IDCompositionTranslateTransform) \
DEVICE_FORWARD(prefix, type, impl, CreateScaleTransform, create_scale_transform, IDCompositionScaleTransform) \
DEVICE_FORWARD(prefix, type, impl, CreateRotateTransform, create_rotate_transform, IDCompositionRotateTransform) \
DEVICE_FORWARD(prefix, type, impl, CreateSkewTransform, create_skew_transform, IDCompositionSkewTransform) \
DEVICE_FORWARD(prefix, type, impl, CreateMatrixTransform, create_matrix_transform, IDCompositionMatrixTransform) \
static HRESULT STDMETHODCALLTYPE prefix##_CreateTransformGroup(type *iface, IDCompositionTransform **transforms, \
        UINT count, IDCompositionTransform **out) \
{ \
    TRACE("iface %p, transforms %p, count %u, out %p.\n", iface, transforms, count, out); \
    return create_transform_group(impl(iface), transforms, count, out); \
} \
DEVICE_FORWARD(prefix, type, impl, CreateTranslateTransform3D, create_translate_transform_3d, IDCompositionTranslateTransform3D) \
DEVICE_FORWARD(prefix, type, impl, CreateScaleTransform3D, create_scale_transform_3d, IDCompositionScaleTransform3D) \
DEVICE_FORWARD(prefix, type, impl, CreateRotateTransform3D, create_rotate_transform_3d, IDCompositionRotateTransform3D) \
DEVICE_FORWARD(prefix, type, impl, CreateMatrixTransform3D, create_matrix_transform_3d, IDCompositionMatrixTransform3D) \
static HRESULT STDMETHODCALLTYPE prefix##_CreateTransform3DGroup(type *iface, IDCompositionTransform3D **transforms, \
        UINT count, IDCompositionTransform3D **out) \
{ \
    TRACE("iface %p, transforms %p, count %u, out %p.\n", iface, transforms, count, out); \
    return create_transform_3d_group(impl(iface), transforms, count, out); \
} \
DEVICE_FORWARD(prefix, type, impl, CreateEffectGroup, create_effect_group, IDCompositionEffectGroup) \
DEVICE_FORWARD(prefix, type, impl, CreateRectangleClip, create_rectangle_clip, IDCompositionRectangleClip) \
DEVICE_FORWARD(prefix, type, impl, CreateAnimation, create_animation, IDCompositionAnimation)

DEVICE_OBJECT_METHODS(device, IDCompositionDevice, impl_from_IDCompositionDevice)

static HRESULT STDMETHODCALLTYPE device_CheckDeviceState(IDCompositionDevice *iface, BOOL *valid)
{
    TRACE("iface %p, valid %p.\n", iface, valid);
    if (!valid) return E_INVALIDARG;
    *valid = TRUE;
    return S_OK;
}

#define DEVICE_OBJECT_VTBL(prefix) \
    prefix##_CreateTranslateTransform, \
    prefix##_CreateScaleTransform, \
    prefix##_CreateRotateTransform, \
    prefix##_CreateSkewTransform, \
    prefix##_CreateMatrixTransform, \
    prefix##_CreateTransformGroup, \
    prefix##_CreateTranslateTransform3D, \
    prefix##_CreateScaleTransform3D, \
    prefix##_CreateRotateTransform3D, \
    prefix##_CreateMatrixTransform3D, \
    prefix##_CreateTransform3DGroup, \
    prefix##_CreateEffectGroup, \
    prefix##_CreateRectangleClip, \
    prefix##_CreateAnimation

static const IDCompositionDeviceVtbl device_vtbl =
{
    device_QueryInterface,
    device_AddRef,
    device_Release,
    device_Commit,
    device_WaitForCommitCompletion,
    device_GetFrameStatistics,
    device_CreateTargetForHwnd,
    device_CreateVisual,
    device_CreateSurface,
    device_CreateVirtualSurface,
    device_CreateSurfaceFromHandle,
    device_CreateSurfaceFromHwnd,
    DEVICE_OBJECT_VTBL(device),
    device_CheckDeviceState,
};

/* ---- IDCompositionDesktopDevice / IDCompositionDevice2 ------------------ */

static HRESULT STDMETHODCALLTYPE desktop_QueryInterface(IDCompositionDesktopDevice *iface, REFIID iid, void **out)
{
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);
    return device_query_interface(impl_from_IDCompositionDesktopDevice(iface), iid, out);
}

static ULONG STDMETHODCALLTYPE desktop_AddRef(IDCompositionDesktopDevice *iface)
{
    return device_addref(impl_from_IDCompositionDesktopDevice(iface));
}

static ULONG STDMETHODCALLTYPE desktop_Release(IDCompositionDesktopDevice *iface)
{
    return device_release(impl_from_IDCompositionDesktopDevice(iface));
}

static HRESULT STDMETHODCALLTYPE desktop_Commit(IDCompositionDesktopDevice *iface)
{
    return device_commit(impl_from_IDCompositionDesktopDevice(iface));
}

static HRESULT STDMETHODCALLTYPE desktop_WaitForCommitCompletion(IDCompositionDesktopDevice *iface)
{
    TRACE("iface %p.\n", iface);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE desktop_GetFrameStatistics(IDCompositionDesktopDevice *iface,
        DCOMPOSITION_FRAME_STATISTICS *stats)
{
    return device_get_frame_statistics(impl_from_IDCompositionDesktopDevice(iface), stats);
}

static HRESULT STDMETHODCALLTYPE desktop_CreateVisual(IDCompositionDesktopDevice *iface, IDCompositionVisual2 **visual)
{
    TRACE("iface %p, visual %p.\n", iface, visual);
    return create_visual(impl_from_IDCompositionDesktopDevice(iface), FALSE, visual);
}

static HRESULT STDMETHODCALLTYPE desktop_CreateSurfaceFactory(IDCompositionDesktopDevice *iface,
        IUnknown *rendering_device, IDCompositionSurfaceFactory **factory)
{
    TRACE("iface %p, rendering_device %p, factory %p.\n", iface, rendering_device, factory);
    return create_surface_factory(impl_from_IDCompositionDesktopDevice(iface), rendering_device, factory);
}

static HRESULT STDMETHODCALLTYPE desktop_CreateSurface(IDCompositionDesktopDevice *iface, UINT width, UINT height,
        DXGI_FORMAT format, DXGI_ALPHA_MODE alpha_mode, IDCompositionSurface **surface)
{
    return device_create_surface(impl_from_IDCompositionDesktopDevice(iface), width, height, format, alpha_mode,
            FALSE, (IDCompositionVirtualSurface **)surface);
}

static HRESULT STDMETHODCALLTYPE desktop_CreateVirtualSurface(IDCompositionDesktopDevice *iface, UINT width,
        UINT height, DXGI_FORMAT format, DXGI_ALPHA_MODE alpha_mode, IDCompositionVirtualSurface **surface)
{
    return device_create_surface(impl_from_IDCompositionDesktopDevice(iface), width, height, format, alpha_mode,
            TRUE, surface);
}

DEVICE_OBJECT_METHODS(desktop, IDCompositionDesktopDevice, impl_from_IDCompositionDesktopDevice)

static HRESULT STDMETHODCALLTYPE desktop_CreateTargetForHwnd(IDCompositionDesktopDevice *iface, HWND hwnd,
        BOOL topmost, IDCompositionTarget **target)
{
    TRACE("iface %p, hwnd %p, topmost %d, target %p.\n", iface, hwnd, topmost, target);
    return create_target(impl_from_IDCompositionDesktopDevice(iface), hwnd, topmost, target);
}

static HRESULT STDMETHODCALLTYPE desktop_CreateSurfaceFromHandle(IDCompositionDesktopDevice *iface, HANDLE handle,
        IUnknown **surface)
{
    TRACE("iface %p, handle %p, surface %p.\n", iface, handle, surface);
    return create_surface_from_handle(impl_from_IDCompositionDesktopDevice(iface), handle, surface);
}

static HRESULT STDMETHODCALLTYPE desktop_CreateSurfaceFromHwnd(IDCompositionDesktopDevice *iface, HWND hwnd,
        IUnknown **surface)
{
    TRACE("iface %p, hwnd %p, surface %p.\n", iface, hwnd, surface);
    return create_surface_from_hwnd(impl_from_IDCompositionDesktopDevice(iface), hwnd, surface);
}

static const IDCompositionDesktopDeviceVtbl desktop_vtbl =
{
    desktop_QueryInterface,
    desktop_AddRef,
    desktop_Release,
    desktop_Commit,
    desktop_WaitForCommitCompletion,
    desktop_GetFrameStatistics,
    desktop_CreateVisual,
    desktop_CreateSurfaceFactory,
    desktop_CreateSurface,
    desktop_CreateVirtualSurface,
    DEVICE_OBJECT_VTBL(desktop),
    desktop_CreateTargetForHwnd,
    desktop_CreateSurfaceFromHandle,
    desktop_CreateSurfaceFromHwnd,
};

/* ---- IDCompositionDevice3 ----------------------------------------------- */

static HRESULT STDMETHODCALLTYPE device3_QueryInterface(IDCompositionDevice3 *iface, REFIID iid, void **out)
{
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);
    return device_query_interface(impl_from_IDCompositionDevice3(iface), iid, out);
}

static ULONG STDMETHODCALLTYPE device3_AddRef(IDCompositionDevice3 *iface)
{
    return device_addref(impl_from_IDCompositionDevice3(iface));
}

static ULONG STDMETHODCALLTYPE device3_Release(IDCompositionDevice3 *iface)
{
    return device_release(impl_from_IDCompositionDevice3(iface));
}

static HRESULT STDMETHODCALLTYPE device3_Commit(IDCompositionDevice3 *iface)
{
    return device_commit(impl_from_IDCompositionDevice3(iface));
}

static HRESULT STDMETHODCALLTYPE device3_WaitForCommitCompletion(IDCompositionDevice3 *iface)
{
    TRACE("iface %p.\n", iface);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE device3_GetFrameStatistics(IDCompositionDevice3 *iface,
        DCOMPOSITION_FRAME_STATISTICS *stats)
{
    return device_get_frame_statistics(impl_from_IDCompositionDevice3(iface), stats);
}

static HRESULT STDMETHODCALLTYPE device3_CreateVisual(IDCompositionDevice3 *iface, IDCompositionVisual2 **visual)
{
    TRACE("iface %p, visual %p.\n", iface, visual);
    return create_visual(impl_from_IDCompositionDevice3(iface), FALSE, visual);
}

static HRESULT STDMETHODCALLTYPE device3_CreateSurfaceFactory(IDCompositionDevice3 *iface,
        IUnknown *rendering_device, IDCompositionSurfaceFactory **factory)
{
    TRACE("iface %p, rendering_device %p, factory %p.\n", iface, rendering_device, factory);
    return create_surface_factory(impl_from_IDCompositionDevice3(iface), rendering_device, factory);
}

static HRESULT STDMETHODCALLTYPE device3_CreateSurface(IDCompositionDevice3 *iface, UINT width, UINT height,
        DXGI_FORMAT format, DXGI_ALPHA_MODE alpha_mode, IDCompositionSurface **surface)
{
    return device_create_surface(impl_from_IDCompositionDevice3(iface), width, height, format, alpha_mode,
            FALSE, (IDCompositionVirtualSurface **)surface);
}

static HRESULT STDMETHODCALLTYPE device3_CreateVirtualSurface(IDCompositionDevice3 *iface, UINT width,
        UINT height, DXGI_FORMAT format, DXGI_ALPHA_MODE alpha_mode, IDCompositionVirtualSurface **surface)
{
    return device_create_surface(impl_from_IDCompositionDevice3(iface), width, height, format, alpha_mode,
            TRUE, surface);
}

DEVICE_OBJECT_METHODS(device3, IDCompositionDevice3, impl_from_IDCompositionDevice3)

/* dcomp/target_visual: effects (each of the thirteen effects can be made) */
#define DEVICE3_EFFECT(name, kind, out_type) \
static HRESULT STDMETHODCALLTYPE device3_##name(IDCompositionDevice3 *iface, out_type **out) \
{ \
    TRACE("iface %p, out %p.\n", iface, out); \
    return create_filter_effect(impl_from_IDCompositionDevice3(iface), kind, (void **)out); \
}

DEVICE3_EFFECT(CreateGaussianBlurEffect, 0, IDCompositionGaussianBlurEffect)
DEVICE3_EFFECT(CreateBrightnessEffect, 1, void)
DEVICE3_EFFECT(CreateColorMatrixEffect, 2, void)
DEVICE3_EFFECT(CreateShadowEffect, 3, void)
DEVICE3_EFFECT(CreateHueRotationEffect, 4, void)
DEVICE3_EFFECT(CreateSaturationEffect, 5, IDCompositionSaturationEffect)
DEVICE3_EFFECT(CreateTurbulenceEffect, 6, void)
DEVICE3_EFFECT(CreateLinearTransferEffect, 7, void)
DEVICE3_EFFECT(CreateTableTransferEffect, 8, void)
DEVICE3_EFFECT(CreateCompositeEffect, 9, void)
DEVICE3_EFFECT(CreateBlendEffect, 10, void)
DEVICE3_EFFECT(CreateArithmeticCompositeEffect, 11, void)
DEVICE3_EFFECT(CreateAffineTransform2DEffect, 12, void)

static const IDCompositionDevice3Vtbl device3_vtbl =
{
    device3_QueryInterface,
    device3_AddRef,
    device3_Release,
    device3_Commit,
    device3_WaitForCommitCompletion,
    device3_GetFrameStatistics,
    device3_CreateVisual,
    device3_CreateSurfaceFactory,
    device3_CreateSurface,
    device3_CreateVirtualSurface,
    DEVICE_OBJECT_VTBL(device3),
    device3_CreateGaussianBlurEffect,
    device3_CreateBrightnessEffect,
    device3_CreateColorMatrixEffect,
    device3_CreateShadowEffect,
    device3_CreateHueRotationEffect,
    device3_CreateSaturationEffect,
    device3_CreateTurbulenceEffect,
    device3_CreateLinearTransferEffect,
    device3_CreateTableTransferEffect,
    device3_CreateCompositeEffect,
    device3_CreateBlendEffect,
    device3_CreateArithmeticCompositeEffect,
    device3_CreateAffineTransform2DEffect,
};

/* ---- IDCompositionDeviceDebug ------------------------------------------- */

static HRESULT STDMETHODCALLTYPE debug_QueryInterface(IDCompositionDeviceDebug *iface, REFIID iid, void **out)
{
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);
    return device_query_interface(impl_from_IDCompositionDeviceDebug(iface), iid, out);
}

static ULONG STDMETHODCALLTYPE debug_AddRef(IDCompositionDeviceDebug *iface)
{
    return device_addref(impl_from_IDCompositionDeviceDebug(iface));
}

static ULONG STDMETHODCALLTYPE debug_Release(IDCompositionDeviceDebug *iface)
{
    return device_release(impl_from_IDCompositionDeviceDebug(iface));
}

/* dcomp/device_create: device_debug */
static HRESULT STDMETHODCALLTYPE debug_EnableDebugCounters(IDCompositionDeviceDebug *iface)
{
    TRACE("iface %p.\n", iface);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE debug_DisableDebugCounters(IDCompositionDeviceDebug *iface)
{
    TRACE("iface %p.\n", iface);
    return S_OK;
}

static const IDCompositionDeviceDebugVtbl debug_vtbl =
{
    debug_QueryInterface,
    debug_AddRef,
    debug_Release,
    debug_EnableDebugCounters,
    debug_DisableDebugCounters,
};

/* ---- creation ----------------------------------------------------------- */

/* The Direct3D device under a Direct2D device: the surface of a bitmap
 * made to be a target belongs to it. Surface textures are made on it. */
static ID3D11Device *d3d_device_from_d2d(ID2D1Device *d2d)
{
    D2D1_BITMAP_PROPERTIES1 props = { { DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED }, 96.0f, 96.0f,
            D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW, NULL };
    D2D1_SIZE_U size = { 1, 1 };
    ID3D11Device *d3d = NULL;
    ID2D1DeviceContext *context;
    IDXGISurface *surface;
    ID2D1Bitmap1 *bitmap;

    if (FAILED(ID2D1Device_CreateDeviceContext(d2d, D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &context))) return NULL;
    if (SUCCEEDED(ID2D1DeviceContext_CreateBitmap(context, size, NULL, 0, &props, &bitmap)))
    {
        if (SUCCEEDED(ID2D1Bitmap1_GetSurface(bitmap, &surface)))
        {
            IDXGISurface_GetDevice(surface, &IID_ID3D11Device, (void **)&d3d);
            IDXGISurface_Release(surface);
        }
        ID2D1Bitmap1_Release(bitmap);
    }
    ID2D1DeviceContext_Release(context);
    if (!d3d) WARN("No Direct3D device for Direct2D device %p.\n", d2d);
    return d3d;
}

/* dcomp/device_create: dev*_d3dctx, dev*_dxgifactory. The rendering device
 * is a Direct3D 11 device, a DXGI device or a Direct2D device. */
HRESULT get_rendering_device(IUnknown *rendering_device, ID3D11Device **d3d, ID2D1Device **d2d)
{
    IDXGIDevice *dxgi;

    *d3d = NULL;
    *d2d = NULL;
    if (!rendering_device) return S_OK;
    if (SUCCEEDED(IUnknown_QueryInterface(rendering_device, &IID_ID3D11Device, (void **)d3d)))
        return S_OK;
    if (SUCCEEDED(IUnknown_QueryInterface(rendering_device, &IID_IDXGIDevice, (void **)&dxgi)))
    {
        HRESULT hr = IDXGIDevice_QueryInterface(dxgi, &IID_ID3D11Device, (void **)d3d);
        IDXGIDevice_Release(dxgi);
        if (SUCCEEDED(hr)) return S_OK;
        WARN("DXGI device %p is not a Direct3D 11 device.\n", rendering_device);
        return E_NOINTERFACE;
    }
    if (SUCCEEDED(IUnknown_QueryInterface(rendering_device, &IID_ID2D1Device, (void **)d2d)))
    {
        *d3d = d3d_device_from_d2d(*d2d);
        return S_OK;
    }
    return E_NOINTERFACE;
}

static HRESULT create_device(int version, IUnknown *rendering_device, REFIID iid, void **out)
{
    struct composition_device *device;
    ID3D11Device *d3d;
    ID2D1Device *d2d;
    HRESULT hr;

    if (!out) return E_INVALIDARG;
    *out = NULL;
    if (!iid) return E_INVALIDARG;

    /* dcomp/device_create: dev*_null_* (each function only makes the
     * device interfaces of its own version) */
    if (version == 1 ? !IsEqualGUID(iid, &IID_IDCompositionDevice)
            : !IsEqualGUID(iid, &IID_IDCompositionDevice) && !IsEqualGUID(iid, &IID_IDCompositionDesktopDevice))
    {
        WARN("%s not supported.\n", debugstr_guid(iid));
        return E_NOINTERFACE;
    }

    if (FAILED(hr = get_rendering_device(rendering_device, &d3d, &d2d))) return hr;

    if (!(device = calloc(1, sizeof(*device))))
    {
        if (d3d) ID3D11Device_Release(d3d);
        if (d2d) ID2D1Device_Release(d2d);
        return E_OUTOFMEMORY;
    }
    device->IDCompositionDevice_iface.lpVtbl = &device_vtbl;
    device->IDCompositionDesktopDevice_iface.lpVtbl = &desktop_vtbl;
    device->IDCompositionDevice3_iface.lpVtbl = &device3_vtbl;
    device->IDCompositionDeviceDebug_iface.lpVtbl = &debug_vtbl;
    device->refcount = 1;
    device->version = version;
    device->d3d_device = d3d;
    device->d2d_device = d2d;
    list_init(&device->targets);

    hr = device_query_interface(device, iid, out);
    device_release(device);
    TRACE("Created device %p, version %d.\n", device, version);
    return hr;
}

HRESULT WINAPI DCompositionCreateDevice(IDXGIDevice *dxgi_device, REFIID iid, void **device)
{
    TRACE("%p, %s, %p.\n", dxgi_device, debugstr_guid(iid), device);
    return create_device(1, (IUnknown *)dxgi_device, iid, device);
}

HRESULT WINAPI DCompositionCreateDevice2(IUnknown *rendering_device, REFIID iid, void **device)
{
    TRACE("%p, %s, %p.\n", rendering_device, debugstr_guid(iid), device);
    return create_device(2, rendering_device, iid, device);
}

HRESULT WINAPI DCompositionCreateDevice3(IUnknown *rendering_device, REFIID iid, void **device)
{
    TRACE("%p, %s, %p.\n", rendering_device, debugstr_guid(iid), device);
    return create_device(3, rendering_device, iid, device);
}
