/*
 * Windows.UI.Composition.Compositor
 *
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

/*
 * On Windows the Compositor runtime class lives in dcomp.dll
 * (combase/composition_activation: reg_compositor). A compositor is
 * made either by the activation factory's ActivateInstance or by a
 * second, unnamed interface of the factory, {22118adf-...}, whose first
 * method takes (rendering device, flags, riid, out); a compositor made
 * that way is also a DirectComposition device: it answers the device
 * interfaces and shares the device's identity and reference count
 * (combase/composition_private: statics_*). Here a compositor is a
 * composition_device with the WinRT interfaces added.
 */

#include "dcomp_private.h"
#include "initguid.h"
#include "winstring.h"
#include "activation.h"
#include "roapi.h"

#define WIDL_using_Windows_Foundation
#include "windows.foundation.h"
#define WIDL_using_Windows_UI_Composition
#include "windows.ui.composition.h"
#include "windows.ui.composition.interop.h"

WINE_DEFAULT_DEBUG_CHANNEL(dcomp);

/* combase/composition_private: statics_qi (no public name) */
static const GUID IID_ICompositorPrivateStatics =
        { 0x22118adf, 0x23f1, 0x4801, { 0xbc, 0xfa, 0x66, 0xcb, 0xf4, 0x8c, 0xc5, 0x1b } };

typedef struct ICompositorPrivateStatics ICompositorPrivateStatics;
typedef struct ICompositorPrivateStaticsVtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(ICompositorPrivateStatics *iface, REFIID iid, void **out);
    ULONG (STDMETHODCALLTYPE *AddRef)(ICompositorPrivateStatics *iface);
    ULONG (STDMETHODCALLTYPE *Release)(ICompositorPrivateStatics *iface);
    HRESULT (STDMETHODCALLTYPE *GetIids)(ICompositorPrivateStatics *iface, ULONG *count, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(ICompositorPrivateStatics *iface, HSTRING *name);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(ICompositorPrivateStatics *iface, TrustLevel *level);
    HRESULT (STDMETHODCALLTYPE *CreateCompositor)(ICompositorPrivateStatics *iface, IUnknown *rendering_device,
            UINT_PTR flags, REFIID iid, void **out);
} ICompositorPrivateStaticsVtbl;
struct ICompositorPrivateStatics { const ICompositorPrivateStaticsVtbl *lpVtbl; };

struct compositor
{
    IInspectable IInspectable_iface; /* IUnknown, IInspectable and IAgileObject */
    ICompositor ICompositor_iface;
    IClosable IClosable_iface;
    ICompositorInterop ICompositorInterop_iface;
    struct composition_device *device;
};

static HRESULT copy_iids(const GUID *const *list, ULONG count, ULONG *out_count, IID **out)
{
    IID *iids;
    ULONG i;

    if (!out_count || !out) return E_INVALIDARG;
    if (!(iids = CoTaskMemAlloc(count * sizeof(*iids)))) return E_OUTOFMEMORY;
    for (i = 0; i < count; i++) iids[i] = *list[i];
    *out_count = count;
    *out = iids;
    return S_OK;
}

/* ---- the compositor ----------------------------------------------------- */

/* combase/composition_activation: compositor, compositor_interop;
 * combase/composition_private: statics_compositor. The IUnknown is its own
 * pointer, not the ICompositor one; IMarshal and the factory's interfaces
 * aren't answered. */
HRESULT compositor_query_interface(struct compositor *compositor, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable)
            || IsEqualGUID(iid, &IID_IAgileObject))
        *out = &compositor->IInspectable_iface;
    else if (IsEqualGUID(iid, &IID_ICompositor))
        *out = &compositor->ICompositor_iface;
    else if (IsEqualGUID(iid, &IID_IClosable))
        *out = &compositor->IClosable_iface;
    else if (IsEqualGUID(iid, &IID_ICompositorInterop))
        *out = &compositor->ICompositorInterop_iface;
    else
        return E_NOINTERFACE;
    IUnknown_AddRef((IUnknown *)*out);
    return S_OK;
}

IUnknown *compositor_identity(struct compositor *compositor)
{
    return (IUnknown *)&compositor->IInspectable_iface;
}

void compositor_destroy(struct compositor *compositor)
{
    free(compositor);
}

static HRESULT compositor_get_iids(struct compositor *compositor, ULONG *count, IID **iids)
{
    /* the order Windows lists them in: the device interfaces first, the
     * WinRT ones last */
    static const GUID *const with_device[] =
    {
        &IID_IDCompositionDevice, &IID_IDCompositionDevice2, &IID_IDCompositionDevice3,
        &IID_IDCompositionDesktopDevice, &IID_IDCompositionDeviceDebug,
        &IID_ICompositor, &IID_ICompositorInterop, &IID_IClosable, &IID_IUnknown, &IID_IInspectable,
    };
    static const GUID *const without_device[] =
    {
        &IID_ICompositor, &IID_ICompositorInterop, &IID_IClosable, &IID_IUnknown, &IID_IInspectable,
    };

    if (compositor->device->version)
        return copy_iids(with_device, ARRAY_SIZE(with_device), count, iids);
    return copy_iids(without_device, ARRAY_SIZE(without_device), count, iids);
}

static HRESULT compositor_get_class_name(HSTRING *name)
{
    static const WCHAR class_name[] = L"Windows.UI.Composition.Compositor";
    if (!name) return E_INVALIDARG;
    return WindowsCreateString(class_name, ARRAY_SIZE(class_name) - 1, name);
}

static HRESULT compositor_get_trust_level(TrustLevel *level)
{
    if (!level) return E_INVALIDARG;
    *level = BaseTrust;
    return S_OK;
}

static inline struct compositor *impl_from_IInspectable(IInspectable *iface)
{
    return CONTAINING_RECORD(iface, struct compositor, IInspectable_iface);
}

static HRESULT STDMETHODCALLTYPE identity_QueryInterface(IInspectable *iface, REFIID iid, void **out)
{
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);
    return device_query_interface(impl_from_IInspectable(iface)->device, iid, out);
}

static ULONG STDMETHODCALLTYPE identity_AddRef(IInspectable *iface)
{
    return device_addref(impl_from_IInspectable(iface)->device);
}

static ULONG STDMETHODCALLTYPE identity_Release(IInspectable *iface)
{
    return device_release(impl_from_IInspectable(iface)->device);
}

static HRESULT STDMETHODCALLTYPE identity_GetIids(IInspectable *iface, ULONG *count, IID **iids)
{
    TRACE("iface %p, count %p, iids %p.\n", iface, count, iids);
    return compositor_get_iids(impl_from_IInspectable(iface), count, iids);
}

static HRESULT STDMETHODCALLTYPE identity_GetRuntimeClassName(IInspectable *iface, HSTRING *name)
{
    TRACE("iface %p, name %p.\n", iface, name);
    return compositor_get_class_name(name);
}

static HRESULT STDMETHODCALLTYPE identity_GetTrustLevel(IInspectable *iface, TrustLevel *level)
{
    TRACE("iface %p, level %p.\n", iface, level);
    return compositor_get_trust_level(level);
}

static const IInspectableVtbl identity_vtbl =
{
    identity_QueryInterface,
    identity_AddRef,
    identity_Release,
    identity_GetIids,
    identity_GetRuntimeClassName,
    identity_GetTrustLevel,
};

#define COMPOSITOR_IINSPECTABLE(pfx, type, member) \
static inline struct compositor *impl_from_##type(type *iface) \
{ \
    return CONTAINING_RECORD(iface, struct compositor, member); \
} \
static HRESULT STDMETHODCALLTYPE pfx##_QueryInterface(type *iface, REFIID iid, void **out) \
{ \
    return identity_QueryInterface(&impl_from_##type(iface)->IInspectable_iface, iid, out); \
} \
static ULONG STDMETHODCALLTYPE pfx##_AddRef(type *iface) \
{ \
    return identity_AddRef(&impl_from_##type(iface)->IInspectable_iface); \
} \
static ULONG STDMETHODCALLTYPE pfx##_Release(type *iface) \
{ \
    return identity_Release(&impl_from_##type(iface)->IInspectable_iface); \
}

COMPOSITOR_IINSPECTABLE(compositor, ICompositor, ICompositor_iface)

static HRESULT STDMETHODCALLTYPE compositor_GetIids(ICompositor *iface, ULONG *count, IID **iids)
{
    return identity_GetIids(&impl_from_ICompositor(iface)->IInspectable_iface, count, iids);
}

static HRESULT STDMETHODCALLTYPE compositor_GetRuntimeClassName(ICompositor *iface, HSTRING *name)
{
    return compositor_get_class_name(name);
}

static HRESULT STDMETHODCALLTYPE compositor_GetTrustLevel(ICompositor *iface, TrustLevel *level)
{
    return compositor_get_trust_level(level);
}

#define COMPOSITOR_STUB(name, ...) \
static HRESULT STDMETHODCALLTYPE compositor_##name(ICompositor *iface, __VA_ARGS__) \
{ \
    FIXME("iface %p stub!\n", iface); \
    return E_NOTIMPL; \
}

COMPOSITOR_STUB(CreateColorKeyFrameAnimation, IColorKeyFrameAnimation **result)
COMPOSITOR_STUB(CreateColorBrush, ICompositionColorBrush **result)
COMPOSITOR_STUB(CreateColorBrushWithColor, __x_ABI_CWindows_CUI_CColor color, ICompositionColorBrush **result)
COMPOSITOR_STUB(CreateContainerVisual, IContainerVisual **result)
COMPOSITOR_STUB(CreateCubicBezierEasingFunction, __x_ABI_CWindows_CFoundation_CNumerics_CVector2 point1,
        __x_ABI_CWindows_CFoundation_CNumerics_CVector2 point2, ICubicBezierEasingFunction **result)
COMPOSITOR_STUB(CreateEffectFactory, __x_ABI_CWindows_CGraphics_CEffects_CIGraphicsEffect *effect,
        ICompositionEffectFactory **result)
COMPOSITOR_STUB(CreateEffectFactoryWithProperties, __x_ABI_CWindows_CGraphics_CEffects_CIGraphicsEffect *effect,
        __FIIterable_1_HSTRING *animatable, ICompositionEffectFactory **result)
COMPOSITOR_STUB(CreateExpressionAnimation, IExpressionAnimation **result)
COMPOSITOR_STUB(CreateExpressionAnimationWithExpression, HSTRING expression, IExpressionAnimation **result)
COMPOSITOR_STUB(CreateInsetClip, IInsetClip **result)
COMPOSITOR_STUB(CreateInsetClipWithInsets, FLOAT left, FLOAT top, FLOAT right, FLOAT bottom, IInsetClip **result)
COMPOSITOR_STUB(CreateLinearEasingFunction, ILinearEasingFunction **result)
COMPOSITOR_STUB(CreatePropertySet, ICompositionPropertySet **result)
COMPOSITOR_STUB(CreateQuaternionKeyFrameAnimation, IQuaternionKeyFrameAnimation **result)
COMPOSITOR_STUB(CreateScalarKeyFrameAnimation, IScalarKeyFrameAnimation **result)
COMPOSITOR_STUB(CreateScopedBatch, CompositionBatchTypes type, ICompositionScopedBatch **result)
COMPOSITOR_STUB(CreateSpriteVisual, ISpriteVisual **result)
COMPOSITOR_STUB(CreateSurfaceBrush, ICompositionSurfaceBrush **result)
COMPOSITOR_STUB(CreateSurfaceBrushWithSurface, ICompositionSurface *surface, ICompositionSurfaceBrush **result)
COMPOSITOR_STUB(CreateTargetForCurrentView, ICompositionTarget **result)
COMPOSITOR_STUB(CreateVector2KeyFrameAnimation, IVector2KeyFrameAnimation **result)
COMPOSITOR_STUB(CreateVector3KeyFrameAnimation, IVector3KeyFrameAnimation **result)
COMPOSITOR_STUB(CreateVector4KeyFrameAnimation, IVector4KeyFrameAnimation **result)
COMPOSITOR_STUB(GetCommitBatch, CompositionBatchTypes type, ICompositionCommitBatch **result)

static const ICompositorVtbl compositor_vtbl =
{
    compositor_QueryInterface,
    compositor_AddRef,
    compositor_Release,
    compositor_GetIids,
    compositor_GetRuntimeClassName,
    compositor_GetTrustLevel,
    compositor_CreateColorKeyFrameAnimation,
    compositor_CreateColorBrush,
    compositor_CreateColorBrushWithColor,
    compositor_CreateContainerVisual,
    compositor_CreateCubicBezierEasingFunction,
    compositor_CreateEffectFactory,
    compositor_CreateEffectFactoryWithProperties,
    compositor_CreateExpressionAnimation,
    compositor_CreateExpressionAnimationWithExpression,
    compositor_CreateInsetClip,
    compositor_CreateInsetClipWithInsets,
    compositor_CreateLinearEasingFunction,
    compositor_CreatePropertySet,
    compositor_CreateQuaternionKeyFrameAnimation,
    compositor_CreateScalarKeyFrameAnimation,
    compositor_CreateScopedBatch,
    compositor_CreateSpriteVisual,
    compositor_CreateSurfaceBrush,
    compositor_CreateSurfaceBrushWithSurface,
    compositor_CreateTargetForCurrentView,
    compositor_CreateVector2KeyFrameAnimation,
    compositor_CreateVector3KeyFrameAnimation,
    compositor_CreateVector4KeyFrameAnimation,
    compositor_GetCommitBatch,
};

COMPOSITOR_IINSPECTABLE(closable, IClosable, IClosable_iface)

static HRESULT STDMETHODCALLTYPE closable_GetIids(IClosable *iface, ULONG *count, IID **iids)
{
    return identity_GetIids(&impl_from_IClosable(iface)->IInspectable_iface, count, iids);
}

static HRESULT STDMETHODCALLTYPE closable_GetRuntimeClassName(IClosable *iface, HSTRING *name)
{
    return compositor_get_class_name(name);
}

static HRESULT STDMETHODCALLTYPE closable_GetTrustLevel(IClosable *iface, TrustLevel *level)
{
    return compositor_get_trust_level(level);
}

static HRESULT STDMETHODCALLTYPE closable_Close(IClosable *iface)
{
    TRACE("iface %p.\n", iface);
    return S_OK;
}

static const IClosableVtbl closable_vtbl =
{
    closable_QueryInterface,
    closable_AddRef,
    closable_Release,
    closable_GetIids,
    closable_GetRuntimeClassName,
    closable_GetTrustLevel,
    closable_Close,
};

COMPOSITOR_IINSPECTABLE(interop, ICompositorInterop, ICompositorInterop_iface)

static HRESULT STDMETHODCALLTYPE interop_CreateCompositionSurfaceForHandle(ICompositorInterop *iface,
        HANDLE handle, ICompositionSurface **result)
{
    FIXME("iface %p, handle %p, result %p stub!\n", iface, handle, result);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE interop_CreateCompositionSurfaceForSwapChain(ICompositorInterop *iface,
        IUnknown *swapchain, ICompositionSurface **result)
{
    FIXME("iface %p, swapchain %p, result %p stub!\n", iface, swapchain, result);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE interop_CreateGraphicsDevice(ICompositorInterop *iface, IUnknown *device,
        ICompositionGraphicsDevice **result)
{
    FIXME("iface %p, device %p, result %p stub!\n", iface, device, result);
    return E_NOTIMPL;
}

static const ICompositorInteropVtbl interop_vtbl =
{
    interop_QueryInterface,
    interop_AddRef,
    interop_Release,
    interop_CreateCompositionSurfaceForHandle,
    interop_CreateCompositionSurfaceForSwapChain,
    interop_CreateGraphicsDevice,
};

/* A compositor made by ActivateInstance is not a DirectComposition device
 * (combase/composition_activation: compositor); one made by the private
 * statics is a version 3 device (combase/composition_private:
 * statics_compositor). */
static HRESULT create_compositor(IUnknown *rendering_device, BOOL is_device, REFIID iid, void **out)
{
    struct composition_device *device;
    struct compositor *compositor;
    HRESULT hr;

    if (!(compositor = calloc(1, sizeof(*compositor)))) return E_OUTOFMEMORY;
    compositor->IInspectable_iface.lpVtbl = &identity_vtbl;
    compositor->ICompositor_iface.lpVtbl = &compositor_vtbl;
    compositor->IClosable_iface.lpVtbl = &closable_vtbl;
    compositor->ICompositorInterop_iface.lpVtbl = &interop_vtbl;

    if (FAILED(hr = device_create(is_device ? 3 : 0, rendering_device, compositor, &device)))
    {
        free(compositor);
        return hr;
    }
    compositor->device = device;
    hr = device_query_interface(device, iid, out);
    device_release(device);
    TRACE("Created compositor %p, device %p, hr %#lx.\n", compositor, device, hr);
    return hr;
}

/* ---- the activation factory --------------------------------------------- */

struct compositor_factory
{
    IActivationFactory IActivationFactory_iface;
    ICompositorPrivateStatics ICompositorPrivateStatics_iface;
    IAgileObject IAgileObject_iface;
    IUnknown *marshal; /* the free-threaded marshaler, aggregated */
};

static struct compositor_factory compositor_factory;

static HRESULT factory_query_interface(REFIID iid, void **out)
{
    struct compositor_factory *factory = &compositor_factory;

    TRACE("iid %s, out %p.\n", debugstr_guid(iid), out);

    if (!out) return E_POINTER;
    /* combase/composition_activation: factory */
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable)
            || IsEqualGUID(iid, &IID_IActivationFactory))
        *out = &factory->IActivationFactory_iface;
    else if (IsEqualGUID(iid, &IID_IAgileObject))
        *out = &factory->IAgileObject_iface;
    else if (IsEqualGUID(iid, &IID_ICompositorPrivateStatics))
        *out = &factory->ICompositorPrivateStatics_iface;
    else if (IsEqualGUID(iid, &IID_IMarshal))
    {
        EnterCriticalSection(&dcomp_cs);
        if (!factory->marshal)
            CoCreateFreeThreadedMarshaler((IUnknown *)&factory->IActivationFactory_iface, &factory->marshal);
        LeaveCriticalSection(&dcomp_cs);
        if (!factory->marshal)
        {
            *out = NULL;
            return E_NOINTERFACE;
        }
        return IUnknown_QueryInterface(factory->marshal, iid, out);
    }
    else
    {
        WARN("%s not implemented.\n", debugstr_guid(iid));
        *out = NULL;
        return E_NOINTERFACE;
    }
    return S_OK;
}

static HRESULT factory_get_iids(ULONG *count, IID **iids)
{
    static const GUID *const list[] = { &IID_IActivationFactory, &IID_ICompositorPrivateStatics };
    return copy_iids(list, ARRAY_SIZE(list), count, iids);
}

/* combase/composition_activation: factory (the factory has no class name) */
static HRESULT factory_get_class_name(HSTRING *name)
{
    if (!name) return E_INVALIDARG;
    *name = NULL;
    return E_ILLEGAL_METHOD_CALL;
}

/* The factory is a static object: AddRef and Release return fixed counts. */
static HRESULT STDMETHODCALLTYPE factory_QueryInterface(IActivationFactory *iface, REFIID iid, void **out)
{
    return factory_query_interface(iid, out);
}

static ULONG STDMETHODCALLTYPE factory_AddRef(IActivationFactory *iface)
{
    return 2;
}

static ULONG STDMETHODCALLTYPE factory_Release(IActivationFactory *iface)
{
    return 1;
}

static HRESULT STDMETHODCALLTYPE factory_GetIids(IActivationFactory *iface, ULONG *count, IID **iids)
{
    return factory_get_iids(count, iids);
}

static HRESULT STDMETHODCALLTYPE factory_GetRuntimeClassName(IActivationFactory *iface, HSTRING *name)
{
    return factory_get_class_name(name);
}

static HRESULT STDMETHODCALLTYPE factory_GetTrustLevel(IActivationFactory *iface, TrustLevel *level)
{
    return compositor_get_trust_level(level);
}

/* combase/composition_activation: activate_* */
static HRESULT STDMETHODCALLTYPE factory_ActivateInstance(IActivationFactory *iface, IInspectable **instance)
{
    TRACE("iface %p, instance %p.\n", iface, instance);
    if (!instance) return E_POINTER;
    *instance = NULL;
    return create_compositor(NULL, FALSE, &IID_IInspectable, (void **)instance);
}

static const IActivationFactoryVtbl factory_vtbl =
{
    factory_QueryInterface,
    factory_AddRef,
    factory_Release,
    factory_GetIids,
    factory_GetRuntimeClassName,
    factory_GetTrustLevel,
    factory_ActivateInstance,
};

static HRESULT STDMETHODCALLTYPE statics_QueryInterface(ICompositorPrivateStatics *iface, REFIID iid, void **out)
{
    return factory_query_interface(iid, out);
}

static ULONG STDMETHODCALLTYPE statics_AddRef(ICompositorPrivateStatics *iface)
{
    return 2;
}

static ULONG STDMETHODCALLTYPE statics_Release(ICompositorPrivateStatics *iface)
{
    return 1;
}

static HRESULT STDMETHODCALLTYPE statics_GetIids(ICompositorPrivateStatics *iface, ULONG *count, IID **iids)
{
    return factory_get_iids(count, iids);
}

static HRESULT STDMETHODCALLTYPE statics_GetRuntimeClassName(ICompositorPrivateStatics *iface, HSTRING *name)
{
    return factory_get_class_name(name);
}

static HRESULT STDMETHODCALLTYPE statics_GetTrustLevel(ICompositorPrivateStatics *iface, TrustLevel *level)
{
    return compositor_get_trust_level(level);
}

/* combase/composition_private: statics_create_*. The rendering device may
 * be NULL or a Direct3D device (a DirectComposition device is refused);
 * the flags seen (0, 1, 2) all work; any interface of the compositor can
 * be asked for, and the out pointer is cleared on failure. */
static HRESULT STDMETHODCALLTYPE statics_CreateCompositor(ICompositorPrivateStatics *iface,
        IUnknown *rendering_device, UINT_PTR flags, REFIID iid, void **out)
{
    TRACE("iface %p, rendering_device %p, flags %#Ix, iid %s, out %p.\n",
            iface, rendering_device, flags, debugstr_guid(iid), out);

    if (!out) return E_POINTER;
    *out = NULL;
    if (!iid) return E_INVALIDARG;
    return create_compositor(rendering_device, TRUE, iid, out);
}

static const ICompositorPrivateStaticsVtbl statics_vtbl =
{
    statics_QueryInterface,
    statics_AddRef,
    statics_Release,
    statics_GetIids,
    statics_GetRuntimeClassName,
    statics_GetTrustLevel,
    statics_CreateCompositor,
};

static HRESULT STDMETHODCALLTYPE factory_agile_QueryInterface(IAgileObject *iface, REFIID iid, void **out)
{
    return factory_query_interface(iid, out);
}

static ULONG STDMETHODCALLTYPE factory_agile_AddRef(IAgileObject *iface)
{
    return 2;
}

static ULONG STDMETHODCALLTYPE factory_agile_Release(IAgileObject *iface)
{
    return 1;
}

static const IAgileObjectVtbl factory_agile_vtbl =
{
    factory_agile_QueryInterface,
    factory_agile_AddRef,
    factory_agile_Release,
};

static struct compositor_factory compositor_factory =
{
    { &factory_vtbl },
    { &statics_vtbl },
    { &factory_agile_vtbl },
};

HRESULT WINAPI DllGetActivationFactory(HSTRING classid, IActivationFactory **factory)
{
    const WCHAR *name = WindowsGetStringRawBuffer(classid, NULL);

    TRACE("classid %s, factory %p.\n", debugstr_w(name), factory);

    if (!factory) return E_POINTER;
    *factory = NULL;
    if (wcscmp(name, L"Windows.UI.Composition.Compositor")) return CLASS_E_CLASSNOTAVAILABLE;
    *factory = &compositor_factory.IActivationFactory_iface;
    return S_OK;
}
