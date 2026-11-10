/*
 * DirectComposition private definitions
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

#ifndef __WINE_DCOMP_PRIVATE_H
#define __WINE_DCOMP_PRIVATE_H

#include <stdarg.h>
#include <float.h>
#include <math.h>

#define COBJMACROS
#include "windef.h"
#include "winbase.h"
#include "wingdi.h"
#include "winuser.h"
#include "objidl.h"
#include "d3d11.h"
#include "dxgi1_6.h"
#include "d2d1_1.h"
#include "dcomp.h"
#include "wine/debug.h"
#include "wine/list.h"

/* Every DirectComposition object has two counts: the public one that
 * AddRef and Release return, and the references the composition tree
 * holds (a visual that is a child or a target's root, a surface that is
 * a visual's content, ...). Windows doesn't count those in the public
 * reference count (dcomp/target_visual: visual_tree, setroot), and an
 * object only goes away when both are zero. Visuals of different devices
 * can be put in one tree (dcomp/target_visual: visual_tree), so all tree
 * changes happen with the one process-wide lock held. */
struct dcomp_object
{
    LONG refcount;
    LONG internal;
    struct composition_device *device;
    void (*destroy)(struct dcomp_object *object);
};

struct composition_device;
struct composition_visual;
struct composition_surface;

enum surface_state
{
    SURFACE_IDLE,
    SURFACE_DRAWING,
    SURFACE_SUSPENDED,
};

#define TILE_SIZE 256
#define TILE_BUCKETS 256

/* surface contents are kept in memory, in tiles of TILE_SIZE pixels made
 * when first drawn; virtual surfaces can be very large and sparse */
struct surface_tile
{
    struct list entry;
    int tx, ty; /* tile column and row */
    DWORD bits[TILE_SIZE * TILE_SIZE]; /* premultiplied BGRA */
};

struct composition_surface
{
    IDCompositionVirtualSurface IDCompositionVirtualSurface_iface;
    struct dcomp_object obj;
    BOOL is_virtual;
    BOOL initialized; /* drawn at least once */
    UINT width, height;
    DXGI_FORMAT format;
    DXGI_ALPHA_MODE alpha_mode;
    ID3D11Device *d3d_device; /* the rendering device the surface was made with */
    ID2D1Device *d2d_device;
    enum surface_state state;
    RECT update; /* the rectangle being drawn */
    ID3D11Texture2D *texture; /* the update texture BeginDraw hands out */
    ID3D11Texture2D *staging;
    ID2D1DeviceContext *d2d_context; /* drawing with Direct2D */
    struct list tiles[TILE_BUCKETS]; /* struct surface_tile */
    struct surface_tile *last_tile; /* lookup cache */
};

struct composition_target
{
    IDCompositionTarget IDCompositionTarget_iface;
    struct dcomp_object obj;
    struct list entry; /* in the process-wide target list */
    struct list device_entry;
    HWND hwnd;
    BOOL topmost;
    struct composition_visual *root;
};

struct composition_visual
{
    IDCompositionVisual3 IDCompositionVisual3_iface;
    struct dcomp_object obj;
    BOOL v1; /* made by an IDCompositionDevice: only IDCompositionVisual */
    struct composition_visual *parent;
    struct composition_target *target; /* this is the target's root */
    struct list children; /* bottom to top */
    struct list entry;
    float offset_x, offset_y;
    float size_x, size_y; /* the {2c4eef28-...} interface's properties */
    float relative_offset_x, relative_offset_y, relative_size_x, relative_size_y;
    D2D_MATRIX_3X2_F transform;
    IDCompositionTransform *transform_object;
    IDCompositionEffect *effect;
    BOOL has_clip;
    D2D_RECT_F clip;
    IDCompositionClip *clip_object;
    IUnknown *content;
    float opacity;
    BOOL visible;
    UINT interpolation_mode, border_mode, composite_mode, opacity_mode, backface_visibility, depth_mode;
    BOOL walking; /* compositor cycle guard */
};

struct compositor;

/* the device's {6b556968-...} interface, which has no public name or
 * documentation (combase/composition_private: b_*) */
struct device_private
{
    const struct device_private_vtbl *lpVtbl;
};

struct composition_device
{
    IDCompositionDevice IDCompositionDevice_iface; /* also the IUnknown */
    IDCompositionDesktopDevice IDCompositionDesktopDevice_iface; /* also IDCompositionDevice2 */
    IDCompositionDevice3 IDCompositionDevice3_iface;
    IDCompositionDeviceDebug IDCompositionDeviceDebug_iface;
    struct device_private device_private_iface;
    LONG refcount;
    int version; /* 1, 2 or 3: which DCompositionCreateDevice made it; 0 for a
                  * compositor that is not a device */
    struct compositor *compositor; /* a Windows.UI.Composition compositor */
    ID3D11Device *d3d_device;
    ID2D1Device *d2d_device;
    struct composition_surface *drawing; /* the surface being drawn, if any */
    struct list targets;
    LARGE_INTEGER last_commit;
};

/* device.c */
extern CRITICAL_SECTION dcomp_cs;
HRESULT device_create(int version, IUnknown *rendering_device, struct compositor *compositor,
        struct composition_device **out);
HRESULT device_query_interface(struct composition_device *device, REFIID iid, void **out);
ULONG device_addref(struct composition_device *device);
ULONG device_release(struct composition_device *device);

/* compositor.c */
HRESULT compositor_query_interface(struct compositor *compositor, REFIID iid, void **out);
IUnknown *compositor_identity(struct compositor *compositor);
void compositor_destroy(struct compositor *compositor);

/* objects.c */
void dcomp_object_init(struct dcomp_object *obj, struct composition_device *device,
        void (*destroy)(struct dcomp_object *object));
ULONG dcomp_object_addref(struct dcomp_object *obj);
ULONG dcomp_object_release(struct dcomp_object *obj);
void dcomp_object_hold(struct dcomp_object *obj);
void dcomp_object_unhold(struct dcomp_object *obj);
BOOL dcomp_valid_float(float value);
HRESULT dcomp_animation_value(IDCompositionAnimation *animation, float *value);

HRESULT create_translate_transform(struct composition_device *device, IDCompositionTranslateTransform **out);
HRESULT create_scale_transform(struct composition_device *device, IDCompositionScaleTransform **out);
HRESULT create_rotate_transform(struct composition_device *device, IDCompositionRotateTransform **out);
HRESULT create_skew_transform(struct composition_device *device, IDCompositionSkewTransform **out);
HRESULT create_matrix_transform(struct composition_device *device, IDCompositionMatrixTransform **out);
HRESULT create_transform_group(struct composition_device *device, IDCompositionTransform **transforms,
        UINT count, IDCompositionTransform **out);
HRESULT create_translate_transform_3d(struct composition_device *device, IDCompositionTranslateTransform3D **out);
HRESULT create_scale_transform_3d(struct composition_device *device, IDCompositionScaleTransform3D **out);
HRESULT create_rotate_transform_3d(struct composition_device *device, IDCompositionRotateTransform3D **out);
HRESULT create_matrix_transform_3d(struct composition_device *device, IDCompositionMatrixTransform3D **out);
HRESULT create_transform_3d_group(struct composition_device *device, IDCompositionTransform3D **transforms,
        UINT count, IDCompositionTransform3D **out);
HRESULT create_effect_group(struct composition_device *device, IDCompositionEffectGroup **out);
HRESULT create_rectangle_clip(struct composition_device *device, IDCompositionRectangleClip **out);
HRESULT create_animation(struct composition_device *device, IDCompositionAnimation **out);
HRESULT create_filter_effect(struct composition_device *device, int kind, void **out);
BOOL transform_get_matrix(IDCompositionTransform *transform, D2D_MATRIX_3X2_F *matrix);
BOOL clip_get_rect(IDCompositionClip *clip, D2D_RECT_F *rect);
float effect_get_opacity(IDCompositionEffect *effect);

/* visual.c */
HRESULT create_visual(struct composition_device *device, BOOL v1, IDCompositionVisual2 **out);
HRESULT create_target(struct composition_device *device, HWND hwnd, BOOL topmost, IDCompositionTarget **out);
struct composition_visual *unsafe_impl_from_IDCompositionVisual(IDCompositionVisual *iface);
struct list *target_list(void);

/* surface.c */
HRESULT create_surface(struct composition_device *device, ID3D11Device *d3d, ID2D1Device *d2d, UINT width,
        UINT height, DXGI_FORMAT format, DXGI_ALPHA_MODE alpha_mode, BOOL is_virtual, IDCompositionVirtualSurface **out);
HRESULT surface_check_content(struct composition_device *device, IUnknown *content);
const DWORD *surface_get_pixel(struct composition_surface *surface, int x, int y);
HRESULT create_surface_factory(struct composition_device *device, IUnknown *rendering_device,
        IDCompositionSurfaceFactory **out);
HRESULT create_surface_from_handle(struct composition_device *device, HANDLE handle, IUnknown **out);
HRESULT create_surface_from_hwnd(struct composition_device *device, HWND hwnd, IUnknown **out);
struct composition_surface *unsafe_impl_from_IDCompositionSurface(IUnknown *iface);
HRESULT get_rendering_device(IUnknown *rendering_device, ID3D11Device **d3d, ID2D1Device **d2d);

/* compose.c */
void compose_device(struct composition_device *device);

#endif /* __WINE_DCOMP_PRIVATE_H */
