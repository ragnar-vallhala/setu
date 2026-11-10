/*
 * DirectComposition visuals and targets
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

#include "dcomp_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(dcomp);

static struct list all_targets = LIST_INIT(all_targets);

static const IDCompositionVisual3Vtbl visual_vtbl;

/* with dcomp_cs held */
struct list *target_list(void)
{
    return &all_targets;
}

static struct composition_visual *impl_from_IDCompositionVisual3(IDCompositionVisual3 *iface)
{
    return CONTAINING_RECORD(iface, struct composition_visual, IDCompositionVisual3_iface);
}

struct composition_visual *unsafe_impl_from_IDCompositionVisual(IDCompositionVisual *iface)
{
    if (!iface || iface->lpVtbl != (const void *)&visual_vtbl) return NULL;
    return impl_from_IDCompositionVisual3((IDCompositionVisual3 *)iface);
}

/* dcomp/target_visual: visual_create, visual_create_v1_device */
static HRESULT STDMETHODCALLTYPE visual_QueryInterface(IDCompositionVisual3 *iface, REFIID iid, void **out)
{
    struct composition_visual *visual = impl_from_IDCompositionVisual3(iface);

    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IDCompositionVisual)
            || (!visual->v1 && (IsEqualGUID(iid, &IID_IDCompositionVisual2)
            || IsEqualGUID(iid, &IID_IDCompositionVisualDebug) || IsEqualGUID(iid, &IID_IDCompositionVisual3))))
    {
        IDCompositionVisual3_AddRef(iface);
        *out = iface;
        return S_OK;
    }
    WARN("%s not implemented.\n", debugstr_guid(iid));
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE visual_AddRef(IDCompositionVisual3 *iface)
{
    return dcomp_object_addref(&impl_from_IDCompositionVisual3(iface)->obj);
}

static ULONG STDMETHODCALLTYPE visual_Release(IDCompositionVisual3 *iface)
{
    return dcomp_object_release(&impl_from_IDCompositionVisual3(iface)->obj);
}

static HRESULT set_float(float *dst, float value)
{
    if (!dcomp_valid_float(value)) return E_INVALIDARG;
    EnterCriticalSection(&dcomp_cs);
    *dst = value;
    LeaveCriticalSection(&dcomp_cs);
    return S_OK;
}

static HRESULT set_float_animation(float *dst, IDCompositionAnimation *animation)
{
    float value;
    HRESULT hr;
    if (FAILED(hr = dcomp_animation_value(animation, &value))) return hr;
    return set_float(dst, value);
}

static HRESULT STDMETHODCALLTYPE visual_SetOffsetXAnimation(IDCompositionVisual3 *iface,
        IDCompositionAnimation *animation)
{
    TRACE("iface %p, animation %p.\n", iface, animation);
    return set_float_animation(&impl_from_IDCompositionVisual3(iface)->offset_x, animation);
}

static HRESULT STDMETHODCALLTYPE visual_SetOffsetX(IDCompositionVisual3 *iface, float offset)
{
    TRACE("iface %p, offset %.8e.\n", iface, offset);
    return set_float(&impl_from_IDCompositionVisual3(iface)->offset_x, offset);
}

static HRESULT STDMETHODCALLTYPE visual_SetOffsetYAnimation(IDCompositionVisual3 *iface,
        IDCompositionAnimation *animation)
{
    TRACE("iface %p, animation %p.\n", iface, animation);
    return set_float_animation(&impl_from_IDCompositionVisual3(iface)->offset_y, animation);
}

static HRESULT STDMETHODCALLTYPE visual_SetOffsetY(IDCompositionVisual3 *iface, float offset)
{
    TRACE("iface %p, offset %.8e.\n", iface, offset);
    return set_float(&impl_from_IDCompositionVisual3(iface)->offset_y, offset);
}

static void replace_unknown(IUnknown **dst, IUnknown *value)
{
    if (value) IUnknown_AddRef(value);
    if (*dst) IUnknown_Release(*dst);
    *dst = value;
}

static HRESULT STDMETHODCALLTYPE visual_SetTransformObject(IDCompositionVisual3 *iface,
        IDCompositionTransform *transform)
{
    struct composition_visual *visual = impl_from_IDCompositionVisual3(iface);
    IUnknown *old;

    TRACE("iface %p, transform %p.\n", iface, transform);

    if (transform) IDCompositionTransform_AddRef(transform);
    EnterCriticalSection(&dcomp_cs);
    old = (IUnknown *)visual->transform_object;
    visual->transform_object = transform;
    LeaveCriticalSection(&dcomp_cs);
    if (old) IUnknown_Release(old);
    return S_OK;
}

static HRESULT set_matrix(struct composition_visual *visual, const D2D_MATRIX_3X2_F *matrix)
{
    IUnknown *old;

    for (int i = 0; i < 6; i++) if (!dcomp_valid_float((&matrix->_11)[i])) return E_INVALIDARG;
    EnterCriticalSection(&dcomp_cs);
    visual->transform = *matrix;
    old = (IUnknown *)visual->transform_object;
    visual->transform_object = NULL;
    LeaveCriticalSection(&dcomp_cs);
    if (old) IUnknown_Release(old);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE visual_SetTransform(IDCompositionVisual3 *iface, const D2D_MATRIX_3X2_F *matrix)
{
    TRACE("iface %p, matrix %p.\n", iface, matrix);
    if (!matrix) return E_INVALIDARG;
    return set_matrix(impl_from_IDCompositionVisual3(iface), matrix);
}

/* dcomp/target_visual: visual_setters (a visual of another device is
 * refused; the visual itself or a child is accepted) */
static HRESULT STDMETHODCALLTYPE visual_SetTransformParent(IDCompositionVisual3 *iface, IDCompositionVisual *parent)
{
    struct composition_visual *visual = impl_from_IDCompositionVisual3(iface), *other;

    TRACE("iface %p, parent %p.\n", iface, parent);

    if (!parent) return S_OK;
    if (!(other = unsafe_impl_from_IDCompositionVisual(parent)) || other->obj.device != visual->obj.device)
        return E_INVALIDARG;
    FIXME("Transform parents are not composed.\n");
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE visual_SetEffect(IDCompositionVisual3 *iface, IDCompositionEffect *effect)
{
    struct composition_visual *visual = impl_from_IDCompositionVisual3(iface);
    IUnknown *old;

    TRACE("iface %p, effect %p.\n", iface, effect);

    if (effect) IDCompositionEffect_AddRef(effect);
    EnterCriticalSection(&dcomp_cs);
    old = (IUnknown *)visual->effect;
    visual->effect = effect;
    LeaveCriticalSection(&dcomp_cs);
    if (old) IUnknown_Release(old);
    return S_OK;
}

/* dcomp/target_visual: visual_setters (the enumerations accept their
 * values and INHERIT, -1) */
static BOOL valid_mode(UINT mode, UINT max)
{
    return mode == ~0u || mode <= max;
}

static HRESULT STDMETHODCALLTYPE visual_SetBitmapInterpolationMode(IDCompositionVisual3 *iface,
        enum DCOMPOSITION_BITMAP_INTERPOLATION_MODE mode)
{
    TRACE("iface %p, mode %#x.\n", iface, mode);
    if (!valid_mode(mode, 1)) return E_INVALIDARG;
    impl_from_IDCompositionVisual3(iface)->interpolation_mode = mode;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE visual_SetBorderMode(IDCompositionVisual3 *iface,
        enum DCOMPOSITION_BORDER_MODE mode)
{
    TRACE("iface %p, mode %#x.\n", iface, mode);
    if (!valid_mode(mode, 1)) return E_INVALIDARG;
    impl_from_IDCompositionVisual3(iface)->border_mode = mode;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE visual_SetClipObject(IDCompositionVisual3 *iface, IDCompositionClip *clip)
{
    struct composition_visual *visual = impl_from_IDCompositionVisual3(iface);
    IUnknown *old;

    TRACE("iface %p, clip %p.\n", iface, clip);

    if (clip) IDCompositionClip_AddRef(clip);
    EnterCriticalSection(&dcomp_cs);
    old = (IUnknown *)visual->clip_object;
    visual->clip_object = clip;
    visual->has_clip = FALSE;
    LeaveCriticalSection(&dcomp_cs);
    if (old) IUnknown_Release(old);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE visual_SetClip(IDCompositionVisual3 *iface, const D2D_RECT_F *rect)
{
    struct composition_visual *visual = impl_from_IDCompositionVisual3(iface);
    IUnknown *old;

    TRACE("iface %p, rect %p.\n", iface, rect);

    if (!rect) return E_INVALIDARG;
    EnterCriticalSection(&dcomp_cs);
    old = (IUnknown *)visual->clip_object;
    visual->clip_object = NULL;
    visual->clip = *rect;
    visual->has_clip = TRUE;
    LeaveCriticalSection(&dcomp_cs);
    if (old) IUnknown_Release(old);
    return S_OK;
}

/* dcomp/target_visual: visual_setters (a visual or a device is not
 * content); dcomp/surface_draw: content_* */
static HRESULT STDMETHODCALLTYPE visual_SetContent(IDCompositionVisual3 *iface, IUnknown *content)
{
    struct composition_visual *visual = impl_from_IDCompositionVisual3(iface);
    struct composition_surface *surface, *old_surface = NULL;
    IUnknown *old;
    HRESULT hr;

    TRACE("iface %p, content %p.\n", iface, content);

    if (content && FAILED(hr = surface_check_content(visual->obj.device, content))) return hr;

    surface = unsafe_impl_from_IDCompositionSurface(content);
    if (content && !surface) IUnknown_AddRef(content);
    EnterCriticalSection(&dcomp_cs);
    old = visual->content;
    if (old) old_surface = unsafe_impl_from_IDCompositionSurface(old);
    if (surface) dcomp_object_hold(&surface->obj);
    visual->content = content;
    if (old_surface) dcomp_object_unhold(&old_surface->obj);
    LeaveCriticalSection(&dcomp_cs);
    if (old && !old_surface) IUnknown_Release(old);
    return S_OK;
}

static BOOL is_ancestor(struct composition_visual *ancestor, struct composition_visual *visual)
{
    for (; visual; visual = visual->parent)
        if (visual == ancestor) return TRUE;
    return FALSE;
}

/* dcomp/target_visual: visual_tree */
static HRESULT STDMETHODCALLTYPE visual_AddVisual(IDCompositionVisual3 *iface, IDCompositionVisual *child_iface,
        BOOL insert_above, IDCompositionVisual *reference_iface)
{
    struct composition_visual *visual = impl_from_IDCompositionVisual3(iface), *child, *reference = NULL;
    HRESULT hr = S_OK;

    TRACE("iface %p, child %p, insert_above %d, reference %p.\n", iface, child_iface, insert_above, reference_iface);

    if (!(child = unsafe_impl_from_IDCompositionVisual(child_iface))) return E_INVALIDARG;
    if (reference_iface && !(reference = unsafe_impl_from_IDCompositionVisual(reference_iface))) return E_INVALIDARG;

    EnterCriticalSection(&dcomp_cs);
    if (reference && reference->parent != visual)
        hr = E_INVALIDARG;
    else if (child == visual)
        WARN("Ignoring adding visual %p to itself.\n", visual);
    else if (child->parent || child->target || is_ancestor(child, visual))
        hr = E_INVALIDARG;
    else
    {
        if (reference)
        {
            if (insert_above) list_add_after(&reference->entry, &child->entry);
            else list_add_before(&reference->entry, &child->entry);
        }
        else if (insert_above) list_add_tail(&visual->children, &child->entry);
        else list_add_head(&visual->children, &child->entry);
        child->parent = visual;
        dcomp_object_hold(&child->obj);
    }
    LeaveCriticalSection(&dcomp_cs);
    return hr;
}

static void remove_child(struct composition_visual *child)
{
    list_remove(&child->entry);
    child->parent = NULL;
    dcomp_object_unhold(&child->obj);
}

static HRESULT STDMETHODCALLTYPE visual_RemoveVisual(IDCompositionVisual3 *iface, IDCompositionVisual *child_iface)
{
    struct composition_visual *visual = impl_from_IDCompositionVisual3(iface), *child;
    HRESULT hr = S_OK;

    TRACE("iface %p, child %p.\n", iface, child_iface);

    if (!(child = unsafe_impl_from_IDCompositionVisual(child_iface))) return E_INVALIDARG;
    EnterCriticalSection(&dcomp_cs);
    if (child->parent != visual) hr = E_INVALIDARG;
    else remove_child(child);
    LeaveCriticalSection(&dcomp_cs);
    return hr;
}

static void remove_all_children(struct composition_visual *visual)
{
    struct list *head;
    while ((head = list_head(&visual->children)))
        remove_child(LIST_ENTRY(head, struct composition_visual, entry));
}

static HRESULT STDMETHODCALLTYPE visual_RemoveAllVisuals(IDCompositionVisual3 *iface)
{
    TRACE("iface %p.\n", iface);
    EnterCriticalSection(&dcomp_cs);
    remove_all_children(impl_from_IDCompositionVisual3(iface));
    LeaveCriticalSection(&dcomp_cs);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE visual_SetCompositeMode(IDCompositionVisual3 *iface,
        enum DCOMPOSITION_COMPOSITE_MODE mode)
{
    TRACE("iface %p, mode %#x.\n", iface, mode);
    if (!valid_mode(mode, 2)) return E_INVALIDARG;
    impl_from_IDCompositionVisual3(iface)->composite_mode = mode;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE visual_SetOpacityMode(IDCompositionVisual3 *iface,
        enum DCOMPOSITION_OPACITY_MODE mode)
{
    TRACE("iface %p, mode %#x.\n", iface, mode);
    if (!valid_mode(mode, 1)) return E_INVALIDARG;
    impl_from_IDCompositionVisual3(iface)->opacity_mode = mode;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE visual_SetBackFaceVisibility(IDCompositionVisual3 *iface,
        enum DCOMPOSITION_BACKFACE_VISIBILITY visibility)
{
    TRACE("iface %p, visibility %#x.\n", iface, visibility);
    if (!valid_mode(visibility, 1)) return E_INVALIDARG;
    impl_from_IDCompositionVisual3(iface)->backface_visibility = visibility;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE visual_EnableHeatMap(IDCompositionVisual3 *iface, const D2D1_COLOR_F *color)
{
    FIXME("iface %p, color %p stub!\n", iface, color);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE visual_DisableHeatMap(IDCompositionVisual3 *iface)
{
    TRACE("iface %p.\n", iface);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE visual_EnableRedrawRegions(IDCompositionVisual3 *iface)
{
    FIXME("iface %p stub!\n", iface);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE visual_DisableRedrawRegions(IDCompositionVisual3 *iface)
{
    TRACE("iface %p.\n", iface);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE visual_SetDepthMode(IDCompositionVisual3 *iface, enum DCOMPOSITION_DEPTH_MODE mode)
{
    TRACE("iface %p, mode %#x.\n", iface, mode);
    if (mode != DCOMPOSITION_DEPTH_MODE_TREE && mode != DCOMPOSITION_DEPTH_MODE_SPATIAL
            && mode != DCOMPOSITION_DEPTH_MODE_SORTED && mode != DCOMPOSITION_DEPTH_MODE_INHERIT)
        return E_INVALIDARG;
    impl_from_IDCompositionVisual3(iface)->depth_mode = mode;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE visual_SetOffsetZAnimation(IDCompositionVisual3 *iface,
        IDCompositionAnimation *animation)
{
    float value;
    TRACE("iface %p, animation %p.\n", iface, animation);
    return dcomp_animation_value(animation, &value);
}

static HRESULT STDMETHODCALLTYPE visual_SetOffsetZ(IDCompositionVisual3 *iface, float offset)
{
    TRACE("iface %p, offset %.8e.\n", iface, offset);
    return dcomp_valid_float(offset) ? S_OK : E_INVALIDARG;
}

static HRESULT STDMETHODCALLTYPE visual_SetOpacityAnimation(IDCompositionVisual3 *iface,
        IDCompositionAnimation *animation)
{
    TRACE("iface %p, animation %p.\n", iface, animation);
    return set_float_animation(&impl_from_IDCompositionVisual3(iface)->opacity, animation);
}

static HRESULT STDMETHODCALLTYPE visual_SetOpacity(IDCompositionVisual3 *iface, float opacity)
{
    TRACE("iface %p, opacity %.8e.\n", iface, opacity);
    return set_float(&impl_from_IDCompositionVisual3(iface)->opacity, opacity);
}

static HRESULT STDMETHODCALLTYPE visual_SetTransform3DObject(IDCompositionVisual3 *iface,
        IDCompositionTransform3D *transform)
{
    TRACE("iface %p, transform %p.\n", iface, transform);
    /* only the 2D transforms are composed */
    return visual_SetTransformObject(iface, (IDCompositionTransform *)transform);
}

static HRESULT STDMETHODCALLTYPE visual_SetTransform3D(IDCompositionVisual3 *iface, const D2D_MATRIX_4X4_F *matrix)
{
    D2D_MATRIX_3X2_F m;

    TRACE("iface %p, matrix %p.\n", iface, matrix);

    if (!matrix) return E_INVALIDARG;
    m._11 = matrix->_11; m._12 = matrix->_12;
    m._21 = matrix->_21; m._22 = matrix->_22;
    m._31 = matrix->_41; m._32 = matrix->_42;
    return set_matrix(impl_from_IDCompositionVisual3(iface), &m);
}

static HRESULT STDMETHODCALLTYPE visual_SetVisible(IDCompositionVisual3 *iface, BOOL visible)
{
    TRACE("iface %p, visible %d.\n", iface, visible);
    impl_from_IDCompositionVisual3(iface)->visible = !!visible;
    return S_OK;
}

static const IDCompositionVisual3Vtbl visual_vtbl =
{
    visual_QueryInterface,
    visual_AddRef,
    visual_Release,
    visual_SetOffsetXAnimation,
    visual_SetOffsetX,
    visual_SetOffsetYAnimation,
    visual_SetOffsetY,
    visual_SetTransformObject,
    visual_SetTransform,
    visual_SetTransformParent,
    visual_SetEffect,
    visual_SetBitmapInterpolationMode,
    visual_SetBorderMode,
    visual_SetClipObject,
    visual_SetClip,
    visual_SetContent,
    visual_AddVisual,
    visual_RemoveVisual,
    visual_RemoveAllVisuals,
    visual_SetCompositeMode,
    visual_SetOpacityMode,
    visual_SetBackFaceVisibility,
    visual_EnableHeatMap,
    visual_DisableHeatMap,
    visual_EnableRedrawRegions,
    visual_DisableRedrawRegions,
    visual_SetDepthMode,
    visual_SetOffsetZAnimation,
    visual_SetOffsetZ,
    visual_SetOpacityAnimation,
    visual_SetOpacity,
    visual_SetTransform3DObject,
    visual_SetTransform3D,
    visual_SetVisible,
};

/* with dcomp_cs held */
static void visual_destroy(struct dcomp_object *obj)
{
    struct composition_visual *visual = CONTAINING_RECORD(obj, struct composition_visual, obj);
    struct composition_surface *surface;

    TRACE("Destroying visual %p.\n", visual);

    remove_all_children(visual);
    if ((surface = unsafe_impl_from_IDCompositionSurface(visual->content)))
        dcomp_object_unhold(&surface->obj);
    else if (visual->content)
        IUnknown_Release(visual->content);
    if (visual->transform_object) IDCompositionTransform_Release(visual->transform_object);
    if (visual->effect) IDCompositionEffect_Release(visual->effect);
    if (visual->clip_object) IDCompositionClip_Release(visual->clip_object);
    free(visual);
}

HRESULT create_visual(struct composition_device *device, BOOL v1, IDCompositionVisual2 **out)
{
    struct composition_visual *visual;

    if (!out) return E_INVALIDARG;
    *out = NULL;
    if (!(visual = calloc(1, sizeof(*visual)))) return E_OUTOFMEMORY;
    visual->IDCompositionVisual3_iface.lpVtbl = &visual_vtbl;
    visual->v1 = v1;
    list_init(&visual->children);
    visual->transform._11 = visual->transform._22 = 1.0f;
    visual->opacity = 1.0f;
    visual->visible = TRUE;
    visual->interpolation_mode = visual->border_mode = visual->composite_mode = ~0u;
    visual->opacity_mode = visual->backface_visibility = visual->depth_mode = ~0u;
    dcomp_object_init(&visual->obj, device, visual_destroy);
    *out = (IDCompositionVisual2 *)&visual->IDCompositionVisual3_iface;
    TRACE("Created visual %p.\n", visual);
    return S_OK;
}

/* ---- targets ------------------------------------------------------------ */

static struct composition_target *impl_from_IDCompositionTarget(IDCompositionTarget *iface)
{
    return CONTAINING_RECORD(iface, struct composition_target, IDCompositionTarget_iface);
}

static HRESULT STDMETHODCALLTYPE target_QueryInterface(IDCompositionTarget *iface, REFIID iid, void **out)
{
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IDCompositionTarget))
    {
        IDCompositionTarget_AddRef(iface);
        *out = iface;
        return S_OK;
    }
    WARN("%s not implemented.\n", debugstr_guid(iid));
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE target_AddRef(IDCompositionTarget *iface)
{
    return dcomp_object_addref(&impl_from_IDCompositionTarget(iface)->obj);
}

static ULONG STDMETHODCALLTYPE target_Release(IDCompositionTarget *iface)
{
    return dcomp_object_release(&impl_from_IDCompositionTarget(iface)->obj);
}

static void target_set_root(struct composition_target *target, struct composition_visual *root)
{
    struct composition_visual *old = target->root;

    if (old == root) return;
    if (root)
    {
        root->target = target;
        dcomp_object_hold(&root->obj);
    }
    target->root = root;
    if (old)
    {
        old->target = NULL;
        dcomp_object_unhold(&old->obj);
    }
}

/* dcomp/target_visual: setroot */
static HRESULT STDMETHODCALLTYPE target_SetRoot(IDCompositionTarget *iface, IDCompositionVisual *visual)
{
    struct composition_target *target = impl_from_IDCompositionTarget(iface);
    struct composition_visual *root = NULL;
    HRESULT hr = S_OK;

    TRACE("iface %p, visual %p.\n", iface, visual);

    if (visual && (!(root = unsafe_impl_from_IDCompositionVisual(visual))
            || root->obj.device != target->obj.device))
        return E_INVALIDARG;

    EnterCriticalSection(&dcomp_cs);
    if (root && (root->parent || (root->target && root->target != target)))
        hr = E_INVALIDARG;
    else
        target_set_root(target, root);
    LeaveCriticalSection(&dcomp_cs);
    return hr;
}

static const IDCompositionTargetVtbl target_vtbl =
{
    target_QueryInterface,
    target_AddRef,
    target_Release,
    target_SetRoot,
};

static void target_destroy(struct dcomp_object *obj)
{
    struct composition_target *target = CONTAINING_RECORD(obj, struct composition_target, obj);

    TRACE("Destroying target %p.\n", target);

    target_set_root(target, NULL);
    list_remove(&target->entry);
    list_remove(&target->device_entry);
    free(target);
}

/* dcomp/target_visual: target_* */
HRESULT create_target(struct composition_device *device, HWND hwnd, BOOL topmost, IDCompositionTarget **out)
{
    struct composition_target *target, *other;
    DWORD pid;

    if (!out) return E_INVALIDARG;
    *out = NULL;
    if (!hwnd || !IsWindow(hwnd) || hwnd == GetDesktopWindow()) return E_INVALIDARG;
    if (!GetWindowThreadProcessId(hwnd, &pid) || pid != GetCurrentProcessId()) return E_ACCESSDENIED;
    topmost = !!topmost;

    if (!(target = calloc(1, sizeof(*target)))) return E_OUTOFMEMORY;
    target->IDCompositionTarget_iface.lpVtbl = &target_vtbl;
    target->hwnd = hwnd;
    target->topmost = topmost;

    EnterCriticalSection(&dcomp_cs);
    LIST_FOR_EACH_ENTRY(other, &all_targets, struct composition_target, entry)
    {
        if (other->hwnd == hwnd && other->topmost == topmost)
        {
            LeaveCriticalSection(&dcomp_cs);
            free(target);
            return DCOMPOSITION_ERROR_WINDOW_ALREADY_COMPOSED;
        }
    }
    dcomp_object_init(&target->obj, device, target_destroy);
    list_add_tail(&all_targets, &target->entry);
    list_add_tail(&device->targets, &target->device_entry);
    LeaveCriticalSection(&dcomp_cs);

    TRACE("Created target %p for window %p, topmost %d.\n", target, hwnd, topmost);
    *out = &target->IDCompositionTarget_iface;
    return S_OK;
}
