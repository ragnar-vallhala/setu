/*
 * DirectComposition transforms, clips, effects and animations
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

/* ---- reference counting ------------------------------------------------ */

void dcomp_object_init(struct dcomp_object *obj, struct composition_device *device,
        void (*destroy)(struct dcomp_object *object))
{
    obj->refcount = 1;
    obj->internal = 0;
    obj->device = device;
    obj->destroy = destroy;
    IDCompositionDevice_AddRef(&device->IDCompositionDevice_iface);
}

ULONG dcomp_object_addref(struct dcomp_object *obj)
{
    return InterlockedIncrement(&obj->refcount);
}

/* with dcomp_cs held; the caller releases the device afterwards */
static BOOL object_maybe_destroy(struct dcomp_object *obj)
{
    if (obj->refcount || obj->internal) return FALSE;
    obj->destroy(obj);
    return TRUE;
}

ULONG dcomp_object_release(struct dcomp_object *obj)
{
    struct composition_device *device = obj->device;
    BOOL destroyed;
    ULONG ref;

    EnterCriticalSection(&dcomp_cs);
    ref = InterlockedDecrement(&obj->refcount);
    destroyed = !ref && object_maybe_destroy(obj);
    LeaveCriticalSection(&dcomp_cs);
    if (destroyed) IDCompositionDevice_Release(&device->IDCompositionDevice_iface);
    return ref;
}

/* tree references; with dcomp_cs held */
void dcomp_object_hold(struct dcomp_object *obj)
{
    obj->internal++;
}

void dcomp_object_unhold(struct dcomp_object *obj)
{
    struct composition_device *device = obj->device;
    if (!--obj->internal && object_maybe_destroy(obj))
        IDCompositionDevice_Release(&device->IDCompositionDevice_iface);
}

/* dcomp/target_visual: visual_setters (NaN and infinite offsets are invalid) */
BOOL dcomp_valid_float(float value)
{
    return isfinite(value);
}

/* ---- animations ---------------------------------------------------------- */

struct animation_segment
{
    double offset;
    float c[4]; /* cubic: constant, linear, quadratic, cubic */
};

struct composition_animation
{
    IDCompositionAnimation IDCompositionAnimation_iface;
    struct dcomp_object obj;
    struct animation_segment *segments;
    UINT count, capacity;
    BOOL ended;
    LARGE_INTEGER begin_time;
};

static const IDCompositionAnimationVtbl animation_vtbl;

static struct composition_animation *impl_from_IDCompositionAnimation(IDCompositionAnimation *iface)
{
    return CONTAINING_RECORD(iface, struct composition_animation, IDCompositionAnimation_iface);
}

/* the value an animation gives at its start: DirectComposition evaluates
 * animations at composition time; this composes static frames, so the
 * first segment's value is used. An animation without segments is
 * rejected (dcomp/target_visual: objects, visual_offset_anim_empty). */
HRESULT dcomp_animation_value(IDCompositionAnimation *iface, float *value)
{
    struct composition_animation *animation;

    if (!iface || iface->lpVtbl != &animation_vtbl) return E_INVALIDARG;
    animation = impl_from_IDCompositionAnimation(iface);
    if (!animation->count) return E_INVALIDARG;
    *value = animation->segments[0].c[0];
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE animation_QueryInterface(IDCompositionAnimation *iface, REFIID iid, void **out)
{
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IDCompositionAnimation))
    {
        IDCompositionAnimation_AddRef(iface);
        *out = iface;
        return S_OK;
    }
    WARN("%s not implemented.\n", debugstr_guid(iid));
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE animation_AddRef(IDCompositionAnimation *iface)
{
    return dcomp_object_addref(&impl_from_IDCompositionAnimation(iface)->obj);
}

static ULONG STDMETHODCALLTYPE animation_Release(IDCompositionAnimation *iface)
{
    return dcomp_object_release(&impl_from_IDCompositionAnimation(iface)->obj);
}

static HRESULT STDMETHODCALLTYPE animation_Reset(IDCompositionAnimation *iface)
{
    struct composition_animation *animation = impl_from_IDCompositionAnimation(iface);
    TRACE("iface %p.\n", iface);
    animation->count = 0;
    animation->ended = FALSE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE animation_SetAbsoluteBeginTime(IDCompositionAnimation *iface, LARGE_INTEGER time)
{
    TRACE("iface %p, time %s.\n", iface, wine_dbgstr_longlong(time.QuadPart));
    impl_from_IDCompositionAnimation(iface)->begin_time = time;
    return S_OK;
}

static HRESULT add_segment(struct composition_animation *animation, double offset, const float *c)
{
    if (!isfinite(offset) || offset < 0.0) return E_INVALIDARG;
    if (animation->count && offset < animation->segments[animation->count - 1].offset) return E_INVALIDARG;
    if (animation->count == animation->capacity)
    {
        UINT capacity = max(4, animation->capacity * 2);
        struct animation_segment *segments = realloc(animation->segments, capacity * sizeof(*segments));
        if (!segments) return E_OUTOFMEMORY;
        animation->segments = segments;
        animation->capacity = capacity;
    }
    animation->segments[animation->count].offset = offset;
    memcpy(animation->segments[animation->count].c, c, sizeof(animation->segments[0].c));
    animation->count++;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE animation_AddCubic(IDCompositionAnimation *iface, double offset,
        float constant, float linear, float quadratic, float cubic)
{
    float c[4] = { constant, linear, quadratic, cubic };
    TRACE("iface %p, offset %.8e, %.8e, %.8e, %.8e, %.8e.\n", iface, offset, constant, linear, quadratic, cubic);
    if (!dcomp_valid_float(constant) || !dcomp_valid_float(linear) || !dcomp_valid_float(quadratic)
            || !dcomp_valid_float(cubic))
        return E_INVALIDARG;
    return add_segment(impl_from_IDCompositionAnimation(iface), offset, c);
}

static HRESULT STDMETHODCALLTYPE animation_AddSinusoidal(IDCompositionAnimation *iface, double offset,
        float bias, float amplitude, float frequency, float phase)
{
    float c[4] = { bias + amplitude * sinf(phase * (float)M_PI / 180.0f), 0.0f, 0.0f, 0.0f };
    TRACE("iface %p, offset %.8e, %.8e, %.8e, %.8e, %.8e.\n", iface, offset, bias, amplitude, frequency, phase);
    if (!dcomp_valid_float(bias) || !dcomp_valid_float(amplitude) || !dcomp_valid_float(frequency)
            || !dcomp_valid_float(phase))
        return E_INVALIDARG;
    return add_segment(impl_from_IDCompositionAnimation(iface), offset, c);
}

static HRESULT STDMETHODCALLTYPE animation_AddRepeat(IDCompositionAnimation *iface, double offset, double duration)
{
    struct composition_animation *animation = impl_from_IDCompositionAnimation(iface);
    TRACE("iface %p, offset %.8e, duration %.8e.\n", iface, offset, duration);
    if (!animation->count || !isfinite(duration) || duration <= 0.0) return E_INVALIDARG;
    return add_segment(animation, offset, animation->segments[animation->count - 1].c);
}

static HRESULT STDMETHODCALLTYPE animation_End(IDCompositionAnimation *iface, double offset, float value)
{
    struct composition_animation *animation = impl_from_IDCompositionAnimation(iface);
    float c[4] = { value, 0.0f, 0.0f, 0.0f };
    HRESULT hr;
    TRACE("iface %p, offset %.8e, value %.8e.\n", iface, offset, value);
    if (!dcomp_valid_float(value)) return E_INVALIDARG;
    if (SUCCEEDED(hr = add_segment(animation, offset, c))) animation->ended = TRUE;
    return hr;
}

static const IDCompositionAnimationVtbl animation_vtbl =
{
    animation_QueryInterface,
    animation_AddRef,
    animation_Release,
    animation_Reset,
    animation_SetAbsoluteBeginTime,
    animation_AddCubic,
    animation_AddSinusoidal,
    animation_AddRepeat,
    animation_End,
};

static void animation_destroy(struct dcomp_object *obj)
{
    struct composition_animation *animation = CONTAINING_RECORD(obj, struct composition_animation, obj);
    free(animation->segments);
    free(animation);
}

HRESULT create_animation(struct composition_device *device, IDCompositionAnimation **out)
{
    struct composition_animation *animation;

    if (!out) return E_INVALIDARG;
    if (!(animation = calloc(1, sizeof(*animation)))) return E_OUTOFMEMORY;
    animation->IDCompositionAnimation_iface.lpVtbl = &animation_vtbl;
    dcomp_object_init(&animation->obj, device, animation_destroy);
    *out = &animation->IDCompositionAnimation_iface;
    return S_OK;
}

/* ---- transforms ---------------------------------------------------------- */

enum transform_kind
{
    TRANSFORM_TRANSLATE,
    TRANSFORM_SCALE,
    TRANSFORM_ROTATE,
    TRANSFORM_SKEW,
    TRANSFORM_MATRIX,
    TRANSFORM_GROUP,
    TRANSFORM_TRANSLATE_3D,
    TRANSFORM_SCALE_3D,
    TRANSFORM_ROTATE_3D,
    TRANSFORM_MATRIX_3D,
    TRANSFORM_GROUP_3D,
};

struct composition_transform
{
    IDCompositionTransform3D iface; /* the vtable depends on the kind */
    struct dcomp_object obj;
    enum transform_kind kind;
    float v[16];
    IUnknown **group;
    UINT group_count;
};

static struct composition_transform *impl_from_transform(void *iface)
{
    return CONTAINING_RECORD(iface, struct composition_transform, iface);
}

static const GUID *transform_iids[] =
{
    &IID_IDCompositionTranslateTransform,
    &IID_IDCompositionScaleTransform,
    &IID_IDCompositionRotateTransform,
    &IID_IDCompositionSkewTransform,
    &IID_IDCompositionMatrixTransform,
    NULL,
    &IID_IDCompositionTranslateTransform3D,
    &IID_IDCompositionScaleTransform3D,
    &IID_IDCompositionRotateTransform3D,
    &IID_IDCompositionMatrixTransform3D,
    NULL,
};

/* dcomp/target_visual: objects (each transform answers IUnknown,
 * IDCompositionEffect, IDCompositionTransform3D, its own interface and,
 * for the 2D ones and the 2D group, IDCompositionTransform) */
static HRESULT STDMETHODCALLTYPE transform_QueryInterface(IDCompositionTransform3D *iface, REFIID iid, void **out)
{
    struct composition_transform *transform = impl_from_transform(iface);
    const GUID *own = transform_iids[transform->kind];
    BOOL is_2d = transform->kind <= TRANSFORM_GROUP;

    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IDCompositionEffect)
            || IsEqualGUID(iid, &IID_IDCompositionTransform3D)
            || (is_2d && IsEqualGUID(iid, &IID_IDCompositionTransform))
            || (own && IsEqualGUID(iid, own)))
    {
        IDCompositionTransform3D_AddRef(iface);
        *out = iface;
        return S_OK;
    }
    /* dcomp/target_visual: objects (a failed query leaves *out alone) */
    WARN("%s not implemented.\n", debugstr_guid(iid));
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE transform_AddRef(IDCompositionTransform3D *iface)
{
    return dcomp_object_addref(&impl_from_transform(iface)->obj);
}

static ULONG STDMETHODCALLTYPE transform_Release(IDCompositionTransform3D *iface)
{
    return dcomp_object_release(&impl_from_transform(iface)->obj);
}

static HRESULT set_value(void *iface, int index, float value)
{
    if (!dcomp_valid_float(value)) return E_INVALIDARG;
    impl_from_transform(iface)->v[index] = value;
    return S_OK;
}

static HRESULT set_animation(void *iface, int index, IDCompositionAnimation *animation)
{
    float value;
    HRESULT hr;
    if (FAILED(hr = dcomp_animation_value(animation, &value))) return hr;
    impl_from_transform(iface)->v[index] = value;
    return S_OK;
}

/* a float setter and its animation form */
#define TRANSFORM_SETTER(prefix, type, name, index) \
static HRESULT STDMETHODCALLTYPE prefix##_##name##Animation(type *iface, IDCompositionAnimation *animation) \
{ \
    TRACE("iface %p, animation %p.\n", iface, animation); \
    return set_animation(iface, index, animation); \
} \
static HRESULT STDMETHODCALLTYPE prefix##_##name(type *iface, float value) \
{ \
    TRACE("iface %p, value %.8e.\n", iface, value); \
    return set_value(iface, index, value); \
}

#define TRANSFORM_IUNKNOWN(type) \
    (void *)transform_QueryInterface, (void *)transform_AddRef, (void *)transform_Release

TRANSFORM_SETTER(translate, IDCompositionTranslateTransform, SetOffsetX, 0)
TRANSFORM_SETTER(translate, IDCompositionTranslateTransform, SetOffsetY, 1)

static const IDCompositionTranslateTransformVtbl translate_vtbl =
{
    TRANSFORM_IUNKNOWN(IDCompositionTranslateTransform),
    translate_SetOffsetXAnimation, translate_SetOffsetX,
    translate_SetOffsetYAnimation, translate_SetOffsetY,
};

TRANSFORM_SETTER(scale, IDCompositionScaleTransform, SetScaleX, 0)
TRANSFORM_SETTER(scale, IDCompositionScaleTransform, SetScaleY, 1)
TRANSFORM_SETTER(scale, IDCompositionScaleTransform, SetCenterX, 2)
TRANSFORM_SETTER(scale, IDCompositionScaleTransform, SetCenterY, 3)

static const IDCompositionScaleTransformVtbl scale_vtbl =
{
    TRANSFORM_IUNKNOWN(IDCompositionScaleTransform),
    scale_SetScaleXAnimation, scale_SetScaleX,
    scale_SetScaleYAnimation, scale_SetScaleY,
    scale_SetCenterXAnimation, scale_SetCenterX,
    scale_SetCenterYAnimation, scale_SetCenterY,
};

TRANSFORM_SETTER(rotate, IDCompositionRotateTransform, SetAngle, 0)
TRANSFORM_SETTER(rotate, IDCompositionRotateTransform, SetCenterX, 1)
TRANSFORM_SETTER(rotate, IDCompositionRotateTransform, SetCenterY, 2)

static const IDCompositionRotateTransformVtbl rotate_vtbl =
{
    TRANSFORM_IUNKNOWN(IDCompositionRotateTransform),
    rotate_SetAngleAnimation, rotate_SetAngle,
    rotate_SetCenterXAnimation, rotate_SetCenterX,
    rotate_SetCenterYAnimation, rotate_SetCenterY,
};

TRANSFORM_SETTER(skew, IDCompositionSkewTransform, SetAngleX, 0)
TRANSFORM_SETTER(skew, IDCompositionSkewTransform, SetAngleY, 1)
TRANSFORM_SETTER(skew, IDCompositionSkewTransform, SetCenterX, 2)
TRANSFORM_SETTER(skew, IDCompositionSkewTransform, SetCenterY, 3)

static const IDCompositionSkewTransformVtbl skew_vtbl =
{
    TRANSFORM_IUNKNOWN(IDCompositionSkewTransform),
    skew_SetAngleXAnimation, skew_SetAngleX,
    skew_SetAngleYAnimation, skew_SetAngleY,
    skew_SetCenterXAnimation, skew_SetCenterX,
    skew_SetCenterYAnimation, skew_SetCenterY,
};

static HRESULT STDMETHODCALLTYPE matrix_SetMatrix(IDCompositionMatrixTransform *iface, const D2D_MATRIX_3X2_F *matrix)
{
    struct composition_transform *transform = impl_from_transform(iface);
    TRACE("iface %p, matrix %p.\n", iface, matrix);
    if (!matrix) return E_INVALIDARG;
    for (int i = 0; i < 6; i++) if (!dcomp_valid_float((&matrix->_11)[i])) return E_INVALIDARG;
    memcpy(transform->v, &matrix->_11, 6 * sizeof(float));
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE matrix_SetMatrixElementAnimation(IDCompositionMatrixTransform *iface,
        int row, int column, IDCompositionAnimation *animation)
{
    TRACE("iface %p, row %d, column %d, animation %p.\n", iface, row, column, animation);
    if (row < 0 || row > 2 || column < 0 || column > 1) return E_INVALIDARG;
    return set_animation(iface, row * 2 + column, animation);
}

static HRESULT STDMETHODCALLTYPE matrix_SetMatrixElement(IDCompositionMatrixTransform *iface,
        int row, int column, float value)
{
    TRACE("iface %p, row %d, column %d, value %.8e.\n", iface, row, column, value);
    if (row < 0 || row > 2 || column < 0 || column > 1) return E_INVALIDARG;
    return set_value(iface, row * 2 + column, value);
}

static const IDCompositionMatrixTransformVtbl matrix_vtbl =
{
    TRANSFORM_IUNKNOWN(IDCompositionMatrixTransform),
    matrix_SetMatrix,
    matrix_SetMatrixElementAnimation,
    matrix_SetMatrixElement,
};

static const IDCompositionTransformVtbl group_vtbl =
{
    TRANSFORM_IUNKNOWN(IDCompositionTransform),
};

TRANSFORM_SETTER(translate3d, IDCompositionTranslateTransform3D, SetOffsetX, 0)
TRANSFORM_SETTER(translate3d, IDCompositionTranslateTransform3D, SetOffsetY, 1)
TRANSFORM_SETTER(translate3d, IDCompositionTranslateTransform3D, SetOffsetZ, 2)

static const IDCompositionTranslateTransform3DVtbl translate3d_vtbl =
{
    TRANSFORM_IUNKNOWN(IDCompositionTranslateTransform3D),
    translate3d_SetOffsetXAnimation, translate3d_SetOffsetX,
    translate3d_SetOffsetYAnimation, translate3d_SetOffsetY,
    translate3d_SetOffsetZAnimation, translate3d_SetOffsetZ,
};

TRANSFORM_SETTER(scale3d, IDCompositionScaleTransform3D, SetScaleX, 0)
TRANSFORM_SETTER(scale3d, IDCompositionScaleTransform3D, SetScaleY, 1)
TRANSFORM_SETTER(scale3d, IDCompositionScaleTransform3D, SetScaleZ, 2)
TRANSFORM_SETTER(scale3d, IDCompositionScaleTransform3D, SetCenterX, 3)
TRANSFORM_SETTER(scale3d, IDCompositionScaleTransform3D, SetCenterY, 4)
TRANSFORM_SETTER(scale3d, IDCompositionScaleTransform3D, SetCenterZ, 5)

static const IDCompositionScaleTransform3DVtbl scale3d_vtbl =
{
    TRANSFORM_IUNKNOWN(IDCompositionScaleTransform3D),
    scale3d_SetScaleXAnimation, scale3d_SetScaleX,
    scale3d_SetScaleYAnimation, scale3d_SetScaleY,
    scale3d_SetScaleZAnimation, scale3d_SetScaleZ,
    scale3d_SetCenterXAnimation, scale3d_SetCenterX,
    scale3d_SetCenterYAnimation, scale3d_SetCenterY,
    scale3d_SetCenterZAnimation, scale3d_SetCenterZ,
};

TRANSFORM_SETTER(rotate3d, IDCompositionRotateTransform3D, SetAngle, 0)
TRANSFORM_SETTER(rotate3d, IDCompositionRotateTransform3D, SetAxisX, 1)
TRANSFORM_SETTER(rotate3d, IDCompositionRotateTransform3D, SetAxisY, 2)
TRANSFORM_SETTER(rotate3d, IDCompositionRotateTransform3D, SetAxisZ, 3)
TRANSFORM_SETTER(rotate3d, IDCompositionRotateTransform3D, SetCenterX, 4)
TRANSFORM_SETTER(rotate3d, IDCompositionRotateTransform3D, SetCenterY, 5)
TRANSFORM_SETTER(rotate3d, IDCompositionRotateTransform3D, SetCenterZ, 6)

static const IDCompositionRotateTransform3DVtbl rotate3d_vtbl =
{
    TRANSFORM_IUNKNOWN(IDCompositionRotateTransform3D),
    rotate3d_SetAngleAnimation, rotate3d_SetAngle,
    rotate3d_SetAxisXAnimation, rotate3d_SetAxisX,
    rotate3d_SetAxisYAnimation, rotate3d_SetAxisY,
    rotate3d_SetAxisZAnimation, rotate3d_SetAxisZ,
    rotate3d_SetCenterXAnimation, rotate3d_SetCenterX,
    rotate3d_SetCenterYAnimation, rotate3d_SetCenterY,
    rotate3d_SetCenterZAnimation, rotate3d_SetCenterZ,
};

static HRESULT STDMETHODCALLTYPE matrix3d_SetMatrix(IDCompositionMatrixTransform3D *iface, const D3DMATRIX *matrix)
{
    struct composition_transform *transform = impl_from_transform(iface);
    TRACE("iface %p, matrix %p.\n", iface, matrix);
    if (!matrix) return E_INVALIDARG;
    for (int i = 0; i < 16; i++) if (!dcomp_valid_float((&matrix->m[0][0])[i])) return E_INVALIDARG;
    memcpy(transform->v, matrix->m, sizeof(transform->v));
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE matrix3d_SetMatrixElementAnimation(IDCompositionMatrixTransform3D *iface,
        int row, int column, IDCompositionAnimation *animation)
{
    TRACE("iface %p, row %d, column %d, animation %p.\n", iface, row, column, animation);
    if (row < 0 || row > 3 || column < 0 || column > 3) return E_INVALIDARG;
    return set_animation(iface, row * 4 + column, animation);
}

static HRESULT STDMETHODCALLTYPE matrix3d_SetMatrixElement(IDCompositionMatrixTransform3D *iface,
        int row, int column, float value)
{
    TRACE("iface %p, row %d, column %d, value %.8e.\n", iface, row, column, value);
    if (row < 0 || row > 3 || column < 0 || column > 3) return E_INVALIDARG;
    return set_value(iface, row * 4 + column, value);
}

static const IDCompositionMatrixTransform3DVtbl matrix3d_vtbl =
{
    TRANSFORM_IUNKNOWN(IDCompositionMatrixTransform3D),
    matrix3d_SetMatrix,
    matrix3d_SetMatrixElementAnimation,
    matrix3d_SetMatrixElement,
};

static const IDCompositionTransform3DVtbl group3d_vtbl =
{
    TRANSFORM_IUNKNOWN(IDCompositionTransform3D),
};

static const void *transform_vtbls[] =
{
    &translate_vtbl, &scale_vtbl, &rotate_vtbl, &skew_vtbl, &matrix_vtbl, &group_vtbl,
    &translate3d_vtbl, &scale3d_vtbl, &rotate3d_vtbl, &matrix3d_vtbl, &group3d_vtbl,
};

static void transform_destroy(struct dcomp_object *obj)
{
    struct composition_transform *transform = CONTAINING_RECORD(obj, struct composition_transform, obj);
    for (UINT i = 0; i < transform->group_count; i++) IUnknown_Release(transform->group[i]);
    free(transform->group);
    free(transform);
}

static HRESULT create_transform(struct composition_device *device, enum transform_kind kind, void **out)
{
    struct composition_transform *transform;

    if (!out) return E_INVALIDARG;
    *out = NULL;
    if (!(transform = calloc(1, sizeof(*transform)))) return E_OUTOFMEMORY;
    transform->iface.lpVtbl = transform_vtbls[kind];
    transform->kind = kind;
    switch (kind)
    {
    case TRANSFORM_SCALE: transform->v[0] = transform->v[1] = 1.0f; break;
    case TRANSFORM_SCALE_3D: transform->v[0] = transform->v[1] = transform->v[2] = 1.0f; break;
    case TRANSFORM_ROTATE_3D: transform->v[3] = 1.0f; break;
    case TRANSFORM_MATRIX: transform->v[0] = transform->v[3] = 1.0f; break;
    case TRANSFORM_MATRIX_3D: for (int i = 0; i < 4; i++) transform->v[i * 5] = 1.0f; break;
    default: break;
    }
    dcomp_object_init(&transform->obj, device, transform_destroy);
    *out = &transform->iface;
    return S_OK;
}

HRESULT create_translate_transform(struct composition_device *device, IDCompositionTranslateTransform **out)
{
    return create_transform(device, TRANSFORM_TRANSLATE, (void **)out);
}

HRESULT create_scale_transform(struct composition_device *device, IDCompositionScaleTransform **out)
{
    return create_transform(device, TRANSFORM_SCALE, (void **)out);
}

HRESULT create_rotate_transform(struct composition_device *device, IDCompositionRotateTransform **out)
{
    return create_transform(device, TRANSFORM_ROTATE, (void **)out);
}

HRESULT create_skew_transform(struct composition_device *device, IDCompositionSkewTransform **out)
{
    return create_transform(device, TRANSFORM_SKEW, (void **)out);
}

HRESULT create_matrix_transform(struct composition_device *device, IDCompositionMatrixTransform **out)
{
    return create_transform(device, TRANSFORM_MATRIX, (void **)out);
}

HRESULT create_translate_transform_3d(struct composition_device *device, IDCompositionTranslateTransform3D **out)
{
    return create_transform(device, TRANSFORM_TRANSLATE_3D, (void **)out);
}

HRESULT create_scale_transform_3d(struct composition_device *device, IDCompositionScaleTransform3D **out)
{
    return create_transform(device, TRANSFORM_SCALE_3D, (void **)out);
}

HRESULT create_rotate_transform_3d(struct composition_device *device, IDCompositionRotateTransform3D **out)
{
    return create_transform(device, TRANSFORM_ROTATE_3D, (void **)out);
}

HRESULT create_matrix_transform_3d(struct composition_device *device, IDCompositionMatrixTransform3D **out)
{
    return create_transform(device, TRANSFORM_MATRIX_3D, (void **)out);
}

static HRESULT create_group(struct composition_device *device, enum transform_kind kind, IUnknown **transforms,
        UINT count, void **out)
{
    struct composition_transform *group;
    HRESULT hr;

    if (!out || (count && !transforms)) return E_INVALIDARG;
    for (UINT i = 0; i < count; i++) if (!transforms[i]) return E_INVALIDARG;
    if (FAILED(hr = create_transform(device, kind, out))) return hr;
    group = impl_from_transform(*out);
    if (count && !(group->group = calloc(count, sizeof(*group->group))))
    {
        IUnknown_Release((IUnknown *)*out);
        *out = NULL;
        return E_OUTOFMEMORY;
    }
    for (UINT i = 0; i < count; i++)
    {
        group->group[i] = transforms[i];
        IUnknown_AddRef(group->group[i]);
    }
    group->group_count = count;
    return S_OK;
}

/* dcomp/target_visual: objects (an empty group is allowed) */
HRESULT create_transform_group(struct composition_device *device, IDCompositionTransform **transforms,
        UINT count, IDCompositionTransform **out)
{
    return create_group(device, TRANSFORM_GROUP, (IUnknown **)transforms, count, (void **)out);
}

HRESULT create_transform_3d_group(struct composition_device *device, IDCompositionTransform3D **transforms,
        UINT count, IDCompositionTransform3D **out)
{
    return create_group(device, TRANSFORM_GROUP_3D, (IUnknown **)transforms, count, (void **)out);
}

static D2D_MATRIX_3X2_F matrix_multiply(const D2D_MATRIX_3X2_F *a, const D2D_MATRIX_3X2_F *b)
{
    D2D_MATRIX_3X2_F r;
    r._11 = a->_11 * b->_11 + a->_12 * b->_21;
    r._12 = a->_11 * b->_12 + a->_12 * b->_22;
    r._21 = a->_21 * b->_11 + a->_22 * b->_21;
    r._22 = a->_21 * b->_12 + a->_22 * b->_22;
    r._31 = a->_31 * b->_11 + a->_32 * b->_21 + b->_31;
    r._32 = a->_31 * b->_12 + a->_32 * b->_22 + b->_32;
    return r;
}

static void matrix_about(D2D_MATRIX_3X2_F *m, float cx, float cy)
{
    /* m applied around (cx, cy): translate(-c) * m * translate(c) */
    D2D_MATRIX_3X2_F t = { { { 1, 0, 0, 1, -cx, -cy } } }, back = { { { 1, 0, 0, 1, cx, cy } } }, r;
    r = matrix_multiply(&t, m);
    *m = matrix_multiply(&r, &back);
}

/* the 2D matrix of a transform object (row vectors, as D2D) */
BOOL transform_get_matrix(IDCompositionTransform *iface, D2D_MATRIX_3X2_F *m)
{
    struct composition_transform *transform;
    const float *v;
    float s, c;

    if (!iface || (iface->lpVtbl != (void *)&translate_vtbl && iface->lpVtbl != (void *)&scale_vtbl
            && iface->lpVtbl != (void *)&rotate_vtbl && iface->lpVtbl != (void *)&skew_vtbl
            && iface->lpVtbl != (void *)&matrix_vtbl && iface->lpVtbl != (void *)&group_vtbl))
        return FALSE;
    transform = impl_from_transform(iface);
    v = transform->v;
    m->_11 = 1; m->_12 = 0; m->_21 = 0; m->_22 = 1; m->_31 = 0; m->_32 = 0;
    switch (transform->kind)
    {
    case TRANSFORM_TRANSLATE:
        m->_31 = v[0]; m->_32 = v[1];
        break;
    case TRANSFORM_SCALE:
        m->_11 = v[0]; m->_22 = v[1];
        matrix_about(m, v[2], v[3]);
        break;
    case TRANSFORM_ROTATE:
        s = sinf(v[0] * (float)M_PI / 180.0f);
        c = cosf(v[0] * (float)M_PI / 180.0f);
        m->_11 = c; m->_12 = s; m->_21 = -s; m->_22 = c;
        matrix_about(m, v[1], v[2]);
        break;
    case TRANSFORM_SKEW:
        m->_12 = tanf(v[1] * (float)M_PI / 180.0f);
        m->_21 = tanf(v[0] * (float)M_PI / 180.0f);
        matrix_about(m, v[2], v[3]);
        break;
    case TRANSFORM_MATRIX:
        memcpy(&m->_11, v, 6 * sizeof(float));
        break;
    case TRANSFORM_GROUP:
        for (UINT i = 0; i < transform->group_count; i++)
        {
            D2D_MATRIX_3X2_F part;
            if (transform_get_matrix((IDCompositionTransform *)transform->group[i], &part))
                *m = matrix_multiply(m, &part);
        }
        break;
    default:
        break;
    }
    return TRUE;
}

/* ---- effect groups ------------------------------------------------------- */

struct composition_effect_group
{
    IDCompositionEffectGroup IDCompositionEffectGroup_iface;
    struct dcomp_object obj;
    float opacity;
    IDCompositionTransform3D *transform;
};

static struct composition_effect_group *impl_from_IDCompositionEffectGroup(IDCompositionEffectGroup *iface)
{
    return CONTAINING_RECORD(iface, struct composition_effect_group, IDCompositionEffectGroup_iface);
}

static HRESULT STDMETHODCALLTYPE effect_group_QueryInterface(IDCompositionEffectGroup *iface, REFIID iid, void **out)
{
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IDCompositionEffect)
            || IsEqualGUID(iid, &IID_IDCompositionEffectGroup))
    {
        IDCompositionEffectGroup_AddRef(iface);
        *out = iface;
        return S_OK;
    }
    /* dcomp/target_visual: objects (a failed query leaves *out alone) */
    WARN("%s not implemented.\n", debugstr_guid(iid));
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE effect_group_AddRef(IDCompositionEffectGroup *iface)
{
    return dcomp_object_addref(&impl_from_IDCompositionEffectGroup(iface)->obj);
}

static ULONG STDMETHODCALLTYPE effect_group_Release(IDCompositionEffectGroup *iface)
{
    return dcomp_object_release(&impl_from_IDCompositionEffectGroup(iface)->obj);
}

static HRESULT STDMETHODCALLTYPE effect_group_SetOpacityAnimation(IDCompositionEffectGroup *iface,
        IDCompositionAnimation *animation)
{
    float value;
    HRESULT hr;
    TRACE("iface %p, animation %p.\n", iface, animation);
    if (FAILED(hr = dcomp_animation_value(animation, &value))) return hr;
    impl_from_IDCompositionEffectGroup(iface)->opacity = value;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE effect_group_SetOpacity(IDCompositionEffectGroup *iface, float opacity)
{
    TRACE("iface %p, opacity %.8e.\n", iface, opacity);
    if (!dcomp_valid_float(opacity)) return E_INVALIDARG;
    impl_from_IDCompositionEffectGroup(iface)->opacity = opacity;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE effect_group_SetTransform3D(IDCompositionEffectGroup *iface,
        IDCompositionTransform3D *transform)
{
    struct composition_effect_group *group = impl_from_IDCompositionEffectGroup(iface);
    TRACE("iface %p, transform %p.\n", iface, transform);
    if (transform) IDCompositionTransform3D_AddRef(transform);
    if (group->transform) IDCompositionTransform3D_Release(group->transform);
    group->transform = transform;
    return S_OK;
}

static const IDCompositionEffectGroupVtbl effect_group_vtbl =
{
    effect_group_QueryInterface,
    effect_group_AddRef,
    effect_group_Release,
    effect_group_SetOpacityAnimation,
    effect_group_SetOpacity,
    effect_group_SetTransform3D,
};

static void effect_group_destroy(struct dcomp_object *obj)
{
    struct composition_effect_group *group = CONTAINING_RECORD(obj, struct composition_effect_group, obj);
    if (group->transform) IDCompositionTransform3D_Release(group->transform);
    free(group);
}

HRESULT create_effect_group(struct composition_device *device, IDCompositionEffectGroup **out)
{
    struct composition_effect_group *group;

    if (!out) return E_INVALIDARG;
    if (!(group = calloc(1, sizeof(*group)))) return E_OUTOFMEMORY;
    group->IDCompositionEffectGroup_iface.lpVtbl = &effect_group_vtbl;
    group->opacity = 1.0f;
    dcomp_object_init(&group->obj, device, effect_group_destroy);
    *out = &group->IDCompositionEffectGroup_iface;
    return S_OK;
}

float effect_get_opacity(IDCompositionEffect *effect)
{
    float opacity;
    if (!effect || effect->lpVtbl != (void *)&effect_group_vtbl) return 1.0f;
    opacity = impl_from_IDCompositionEffectGroup((IDCompositionEffectGroup *)effect)->opacity;
    return min(max(opacity, 0.0f), 1.0f);
}

/* ---- rectangle clips ----------------------------------------------------- */

struct composition_clip
{
    IDCompositionRectangleClip IDCompositionRectangleClip_iface;
    struct dcomp_object obj;
    float v[12]; /* left, top, right, bottom, then the corner radii */
};

static struct composition_clip *impl_from_IDCompositionRectangleClip(IDCompositionRectangleClip *iface)
{
    return CONTAINING_RECORD(iface, struct composition_clip, IDCompositionRectangleClip_iface);
}

static HRESULT STDMETHODCALLTYPE clip_QueryInterface(IDCompositionRectangleClip *iface, REFIID iid, void **out)
{
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IDCompositionClip)
            || IsEqualGUID(iid, &IID_IDCompositionRectangleClip))
    {
        IDCompositionRectangleClip_AddRef(iface);
        *out = iface;
        return S_OK;
    }
    /* dcomp/target_visual: objects (a failed query leaves *out alone) */
    WARN("%s not implemented.\n", debugstr_guid(iid));
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE clip_AddRef(IDCompositionRectangleClip *iface)
{
    return dcomp_object_addref(&impl_from_IDCompositionRectangleClip(iface)->obj);
}

static ULONG STDMETHODCALLTYPE clip_Release(IDCompositionRectangleClip *iface)
{
    return dcomp_object_release(&impl_from_IDCompositionRectangleClip(iface)->obj);
}

#define CLIP_SETTER(name, index) \
static HRESULT STDMETHODCALLTYPE clip_##name##Animation(IDCompositionRectangleClip *iface, \
        IDCompositionAnimation *animation) \
{ \
    float value; \
    HRESULT hr; \
    TRACE("iface %p, animation %p.\n", iface, animation); \
    if (FAILED(hr = dcomp_animation_value(animation, &value))) return hr; \
    impl_from_IDCompositionRectangleClip(iface)->v[index] = value; \
    return S_OK; \
} \
static HRESULT STDMETHODCALLTYPE clip_##name(IDCompositionRectangleClip *iface, float value) \
{ \
    TRACE("iface %p, value %.8e.\n", iface, value); \
    if (!dcomp_valid_float(value)) return E_INVALIDARG; \
    impl_from_IDCompositionRectangleClip(iface)->v[index] = value; \
    return S_OK; \
}

CLIP_SETTER(SetLeft, 0)
CLIP_SETTER(SetTop, 1)
CLIP_SETTER(SetRight, 2)
CLIP_SETTER(SetBottom, 3)
CLIP_SETTER(SetTopLeftRadiusX, 4)
CLIP_SETTER(SetTopLeftRadiusY, 5)
CLIP_SETTER(SetTopRightRadiusX, 6)
CLIP_SETTER(SetTopRightRadiusY, 7)
CLIP_SETTER(SetBottomLeftRadiusX, 8)
CLIP_SETTER(SetBottomLeftRadiusY, 9)
CLIP_SETTER(SetBottomRightRadiusX, 10)
CLIP_SETTER(SetBottomRightRadiusY, 11)

static const IDCompositionRectangleClipVtbl clip_vtbl =
{
    clip_QueryInterface,
    clip_AddRef,
    clip_Release,
    clip_SetLeftAnimation, clip_SetLeft,
    clip_SetTopAnimation, clip_SetTop,
    clip_SetRightAnimation, clip_SetRight,
    clip_SetBottomAnimation, clip_SetBottom,
    clip_SetTopLeftRadiusXAnimation, clip_SetTopLeftRadiusX,
    clip_SetTopLeftRadiusYAnimation, clip_SetTopLeftRadiusY,
    clip_SetTopRightRadiusXAnimation, clip_SetTopRightRadiusX,
    clip_SetTopRightRadiusYAnimation, clip_SetTopRightRadiusY,
    clip_SetBottomLeftRadiusXAnimation, clip_SetBottomLeftRadiusX,
    clip_SetBottomLeftRadiusYAnimation, clip_SetBottomLeftRadiusY,
    clip_SetBottomRightRadiusXAnimation, clip_SetBottomRightRadiusX,
    clip_SetBottomRightRadiusYAnimation, clip_SetBottomRightRadiusY,
};

static void clip_destroy(struct dcomp_object *obj)
{
    free(CONTAINING_RECORD(obj, struct composition_clip, obj));
}

HRESULT create_rectangle_clip(struct composition_device *device, IDCompositionRectangleClip **out)
{
    struct composition_clip *clip;

    if (!out) return E_INVALIDARG;
    if (!(clip = calloc(1, sizeof(*clip)))) return E_OUTOFMEMORY;
    clip->IDCompositionRectangleClip_iface.lpVtbl = &clip_vtbl;
    /* an unset edge doesn't clip */
    clip->v[0] = clip->v[1] = -FLT_MAX;
    clip->v[2] = clip->v[3] = FLT_MAX;
    dcomp_object_init(&clip->obj, device, clip_destroy);
    *out = &clip->IDCompositionRectangleClip_iface;
    return S_OK;
}

BOOL clip_get_rect(IDCompositionClip *iface, D2D_RECT_F *rect)
{
    struct composition_clip *clip;
    if (!iface || iface->lpVtbl != (void *)&clip_vtbl) return FALSE;
    clip = impl_from_IDCompositionRectangleClip((IDCompositionRectangleClip *)iface);
    rect->left = clip->v[0];
    rect->top = clip->v[1];
    rect->right = clip->v[2];
    rect->bottom = clip->v[3];
    return TRUE;
}

/* ---- IDCompositionDevice3 filter effects --------------------------------- *
 * dcomp/target_visual: effects. Each effect answers IDCompositionEffect,
 * IDCompositionFilterEffect and its own interface on one pointer, and
 * IUnknown on another. The compositor doesn't apply filter effects yet:
 * their properties are accepted and kept out of the picture. */

enum
{
    EFFECT_GAUSSIAN_BLUR,
    EFFECT_BRIGHTNESS,
    EFFECT_COLOR_MATRIX,
    EFFECT_SHADOW,
    EFFECT_HUE_ROTATION,
    EFFECT_SATURATION,
    EFFECT_TURBULENCE,
    EFFECT_LINEAR_TRANSFER,
    EFFECT_TABLE_TRANSFER,
    EFFECT_COMPOSITE,
    EFFECT_BLEND,
    EFFECT_ARITHMETIC_COMPOSITE,
    EFFECT_AFFINE_TRANSFORM_2D,
    EFFECT_COUNT
};

struct composition_filter_effect
{
    IDCompositionFilterEffect IDCompositionFilterEffect_iface;
    IUnknown IUnknown_iface;
    struct dcomp_object obj;
    int kind;
    IUnknown *inputs[8];
    float amount; /* standard deviation or saturation */
};

static struct composition_filter_effect *impl_from_IDCompositionFilterEffect(IDCompositionFilterEffect *iface)
{
    return CONTAINING_RECORD(iface, struct composition_filter_effect, IDCompositionFilterEffect_iface);
}

static struct composition_filter_effect *filter_effect_from_IUnknown(IUnknown *iface)
{
    return CONTAINING_RECORD(iface, struct composition_filter_effect, IUnknown_iface);
}

static HRESULT filter_effect_query(struct composition_filter_effect *effect, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_IUnknown))
        *out = &effect->IUnknown_iface;
    else if (IsEqualGUID(iid, &IID_IDCompositionEffect) || IsEqualGUID(iid, &IID_IDCompositionFilterEffect)
            || (effect->kind == EFFECT_GAUSSIAN_BLUR && IsEqualGUID(iid, &IID_IDCompositionGaussianBlurEffect))
            || (effect->kind == EFFECT_SATURATION && IsEqualGUID(iid, &IID_IDCompositionSaturationEffect)))
        *out = &effect->IDCompositionFilterEffect_iface;
    else
    {
        WARN("%s not implemented.\n", debugstr_guid(iid));
        *out = NULL;
        return E_NOINTERFACE;
    }
    dcomp_object_addref(&effect->obj);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE filter_unknown_QueryInterface(IUnknown *iface, REFIID iid, void **out)
{
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);
    return filter_effect_query(filter_effect_from_IUnknown(iface), iid, out);
}

static ULONG STDMETHODCALLTYPE filter_unknown_AddRef(IUnknown *iface)
{
    return dcomp_object_addref(&filter_effect_from_IUnknown(iface)->obj);
}

static ULONG STDMETHODCALLTYPE filter_unknown_Release(IUnknown *iface)
{
    return dcomp_object_release(&filter_effect_from_IUnknown(iface)->obj);
}

static const IUnknownVtbl filter_unknown_vtbl =
{
    filter_unknown_QueryInterface,
    filter_unknown_AddRef,
    filter_unknown_Release,
};

static HRESULT STDMETHODCALLTYPE filter_effect_QueryInterface(IDCompositionFilterEffect *iface, REFIID iid, void **out)
{
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);
    return filter_effect_query(impl_from_IDCompositionFilterEffect(iface), iid, out);
}

static ULONG STDMETHODCALLTYPE filter_effect_AddRef(IDCompositionFilterEffect *iface)
{
    return dcomp_object_addref(&impl_from_IDCompositionFilterEffect(iface)->obj);
}

static ULONG STDMETHODCALLTYPE filter_effect_Release(IDCompositionFilterEffect *iface)
{
    return dcomp_object_release(&impl_from_IDCompositionFilterEffect(iface)->obj);
}

static HRESULT STDMETHODCALLTYPE filter_effect_SetInput(IDCompositionFilterEffect *iface, UINT index,
        IUnknown *input, UINT flags)
{
    struct composition_filter_effect *effect = impl_from_IDCompositionFilterEffect(iface);

    TRACE("iface %p, index %u, input %p, flags %#x.\n", iface, index, input, flags);

    if (index >= ARRAY_SIZE(effect->inputs)) return E_INVALIDARG;
    if (input) IUnknown_AddRef(input);
    if (effect->inputs[index]) IUnknown_Release(effect->inputs[index]);
    effect->inputs[index] = input;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE filter_effect_SetAmountAnimation(IDCompositionFilterEffect *iface,
        IDCompositionAnimation *animation)
{
    float value;
    HRESULT hr;
    TRACE("iface %p, animation %p.\n", iface, animation);
    if (FAILED(hr = dcomp_animation_value(animation, &value))) return hr;
    impl_from_IDCompositionFilterEffect(iface)->amount = value;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE filter_effect_SetAmount(IDCompositionFilterEffect *iface, float value)
{
    TRACE("iface %p, value %.8e.\n", iface, value);
    if (!dcomp_valid_float(value)) return E_INVALIDARG;
    impl_from_IDCompositionFilterEffect(iface)->amount = value;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE blur_SetBorderMode(IDCompositionFilterEffect *iface, D2D1_BORDER_MODE mode)
{
    TRACE("iface %p, mode %#x.\n", iface, mode);
    if (mode != D2D1_BORDER_MODE_SOFT && mode != D2D1_BORDER_MODE_HARD) return E_INVALIDARG;
    return S_OK;
}

static const struct
{
    IDCompositionFilterEffectVtbl filter;
    void *methods[3];
}
blur_vtbl =
{
    {
        filter_effect_QueryInterface,
        filter_effect_AddRef,
        filter_effect_Release,
        filter_effect_SetInput,
    },
    { filter_effect_SetAmountAnimation, filter_effect_SetAmount, blur_SetBorderMode },
},
saturation_vtbl =
{
    {
        filter_effect_QueryInterface,
        filter_effect_AddRef,
        filter_effect_Release,
        filter_effect_SetInput,
    },
    { filter_effect_SetAmountAnimation, filter_effect_SetAmount, NULL },
};

#ifdef __x86_64__
/* The other effect interfaces (brightness, color matrix, shadow, ...) are
 * documented, but their interface IDs and method order aren't in the
 * public headers this is built from. Their methods only set properties
 * the compositor doesn't use yet; on x86-64 the caller removes the
 * arguments, so one method that accepts and ignores them serves every
 * slot. */
static HRESULT STDMETHODCALLTYPE filter_effect_set_property(IDCompositionFilterEffect *iface)
{
    TRACE("iface %p.\n", iface);
    return S_OK;
}

#define P filter_effect_set_property
static const struct
{
    IDCompositionFilterEffectVtbl filter;
    void *methods[40];
}
generic_filter_vtbl =
{
    {
        filter_effect_QueryInterface,
        filter_effect_AddRef,
        filter_effect_Release,
        filter_effect_SetInput,
    },
    { P, P, P, P, P, P, P, P, P, P, P, P, P, P, P, P, P, P, P, P,
      P, P, P, P, P, P, P, P, P, P, P, P, P, P, P, P, P, P, P, P },
};
#undef P
#endif

static void filter_effect_destroy(struct dcomp_object *obj)
{
    struct composition_filter_effect *effect = CONTAINING_RECORD(obj, struct composition_filter_effect, obj);
    for (UINT i = 0; i < ARRAY_SIZE(effect->inputs); i++)
        if (effect->inputs[i]) IUnknown_Release(effect->inputs[i]);
    free(effect);
}

HRESULT create_filter_effect(struct composition_device *device, int kind, void **out)
{
    struct composition_filter_effect *effect;
    const void *vtbl;

    if (!out) return E_INVALIDARG;
    *out = NULL;
    if (kind == EFFECT_GAUSSIAN_BLUR) vtbl = &blur_vtbl;
    else if (kind == EFFECT_SATURATION) vtbl = &saturation_vtbl;
    else
    {
#ifdef __x86_64__
        vtbl = &generic_filter_vtbl;
#else
        FIXME("effect %d not implemented.\n", kind);
        return E_NOTIMPL;
#endif
    }
    if (!(effect = calloc(1, sizeof(*effect)))) return E_OUTOFMEMORY;
    effect->IDCompositionFilterEffect_iface.lpVtbl = (void *)vtbl;
    effect->IUnknown_iface.lpVtbl = &filter_unknown_vtbl;
    effect->kind = kind;
    effect->amount = kind == EFFECT_SATURATION ? 1.0f : 0.0f;
    dcomp_object_init(&effect->obj, device, filter_effect_destroy);
    *out = &effect->IDCompositionFilterEffect_iface;
    return S_OK;
}
