/*
 * DirectComposition surfaces
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

/* dcomp/surface_draw: *_huge (the largest size isn't documented; virtual
 * surfaces of 2^20 pixels a side are common) */
#define MAX_SURFACE_SIZE 0x1000000

static const IDCompositionVirtualSurfaceVtbl surface_vtbl;

static struct composition_surface *impl_from_IDCompositionVirtualSurface(IDCompositionVirtualSurface *iface)
{
    return CONTAINING_RECORD(iface, struct composition_surface, IDCompositionVirtualSurface_iface);
}

struct composition_surface *unsafe_impl_from_IDCompositionSurface(IUnknown *iface)
{
    if (!iface || iface->lpVtbl != (const void *)&surface_vtbl) return NULL;
    return impl_from_IDCompositionVirtualSurface((IDCompositionVirtualSurface *)iface);
}

/* ---- tiles -------------------------------------------------------------- */

static unsigned int tile_bucket(int tx, int ty)
{
    return ((unsigned int)tx * 31u + (unsigned int)ty) % TILE_BUCKETS;
}

static struct surface_tile *surface_find_tile(struct composition_surface *surface, int tx, int ty, BOOL create)
{
    struct list *bucket = &surface->tiles[tile_bucket(tx, ty)];
    struct surface_tile *tile;

    if ((tile = surface->last_tile) && tile->tx == tx && tile->ty == ty) return tile;
    LIST_FOR_EACH_ENTRY(tile, bucket, struct surface_tile, entry)
    {
        if (tile->tx == tx && tile->ty == ty) return (surface->last_tile = tile);
    }
    if (!create || !(tile = calloc(1, sizeof(*tile)))) return NULL;
    tile->tx = tx;
    tile->ty = ty;
    list_add_head(bucket, &tile->entry);
    return (surface->last_tile = tile);
}

const DWORD *surface_get_pixel(struct composition_surface *surface, int x, int y)
{
    struct surface_tile *tile;

    if (x < 0 || y < 0 || x >= surface->width || y >= surface->height) return NULL;
    if (!(tile = surface_find_tile(surface, x / TILE_SIZE, y / TILE_SIZE, FALSE))) return NULL;
    return &tile->bits[(y % TILE_SIZE) * TILE_SIZE + x % TILE_SIZE];
}

static void surface_free_tile(struct composition_surface *surface, struct surface_tile *tile)
{
    if (surface->last_tile == tile) surface->last_tile = NULL;
    list_remove(&tile->entry);
    free(tile);
}

/* frees the tiles outside the rectangles; all of them without any */
static void surface_trim_tiles(struct composition_surface *surface, const RECT *rects, UINT count)
{
    struct surface_tile *tile, *next;

    for (unsigned int i = 0; i < TILE_BUCKETS; i++)
    {
        LIST_FOR_EACH_ENTRY_SAFE(tile, next, &surface->tiles[i], struct surface_tile, entry)
        {
            RECT r = { tile->tx * TILE_SIZE, tile->ty * TILE_SIZE, (tile->tx + 1) * TILE_SIZE, (tile->ty + 1) * TILE_SIZE };
            BOOL keep = FALSE;
            RECT tmp;

            for (UINT j = 0; j < count && !keep; j++) keep = IntersectRect(&tmp, &r, &rects[j]);
            if (!keep) surface_free_tile(surface, tile);
        }
    }
}

static inline DWORD half_to_byte(WORD h, BOOL clamp)
{
    unsigned int exponent = (h >> 10) & 0x1f, mantissa = h & 0x3ff;
    float value;

    if (h & 0x8000) return 0;
    if (exponent == 0x1f) return 255;
    if (!exponent) value = ldexpf((float)mantissa, -24);
    else value = ldexpf((float)(mantissa | 0x400), (int)exponent - 25);
    if (clamp && value > 1.0f) value = 1.0f;
    return (DWORD)(value * 255.0f + 0.5f);
}

/* converts one pixel of the surface format to premultiplied BGRA */
static DWORD convert_pixel(const BYTE *src, DXGI_FORMAT format, DXGI_ALPHA_MODE alpha_mode)
{
    DWORD pixel;

    switch (format)
    {
    case DXGI_FORMAT_R8G8B8A8_UNORM:
        pixel = src[2] | (src[1] << 8) | (src[0] << 16) | ((DWORD)src[3] << 24);
        break;
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
    {
        const WORD *h = (const WORD *)src;
        pixel = half_to_byte(h[2], TRUE) | (half_to_byte(h[1], TRUE) << 8) | (half_to_byte(h[0], TRUE) << 16)
                | (half_to_byte(h[3], TRUE) << 24);
        break;
    }
    case DXGI_FORMAT_A8_UNORM:
        pixel = (DWORD)src[0] << 24;
        break;
    default:
        pixel = *(const DWORD *)src;
        break;
    }
    if (alpha_mode == DXGI_ALPHA_MODE_IGNORE) pixel |= 0xff000000;
    return pixel;
}

static UINT format_size(DXGI_FORMAT format)
{
    switch (format)
    {
    case DXGI_FORMAT_R16G16B16A16_FLOAT: return 8;
    case DXGI_FORMAT_A8_UNORM: return 1;
    default: return 4;
    }
}

/* copies what was drawn into the update texture into the tiles */
static void surface_read_back(struct composition_surface *surface)
{
    UINT w = surface->update.right - surface->update.left, h = surface->update.bottom - surface->update.top;
    UINT bpp = format_size(surface->format);
    D3D11_MAPPED_SUBRESOURCE map;
    ID3D11DeviceContext *context;
    D3D11_TEXTURE2D_DESC desc;
    D3D11_BOX box = { 0, 0, 0, w, h, 1 };
    HRESULT hr;

    if (!surface->texture || !surface->d3d_device) return;

    if (surface->staging)
    {
        ID3D11Texture2D_GetDesc(surface->staging, &desc);
        if (desc.Width < w || desc.Height < h)
        {
            ID3D11Texture2D_Release(surface->staging);
            surface->staging = NULL;
        }
    }
    if (!surface->staging)
    {
        ID3D11Texture2D_GetDesc(surface->texture, &desc);
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.MiscFlags = 0;
        if (FAILED(hr = ID3D11Device_CreateTexture2D(surface->d3d_device, &desc, NULL, &surface->staging)))
        {
            ERR("Failed to create the staging texture, hr %#lx.\n", hr);
            return;
        }
    }

    ID3D11Device_GetImmediateContext(surface->d3d_device, &context);
    ID3D11DeviceContext_CopySubresourceRegion(context, (ID3D11Resource *)surface->staging, 0, 0, 0, 0,
            (ID3D11Resource *)surface->texture, 0, &box);
    if (SUCCEEDED(hr = ID3D11DeviceContext_Map(context, (ID3D11Resource *)surface->staging, 0,
            D3D11_MAP_READ, 0, &map)))
    {
        for (UINT y = 0; y < h; y++)
        {
            const BYTE *row = (const BYTE *)map.pData + y * map.RowPitch;
            int sy = surface->update.top + y;
            UINT x = 0;

            while (x < w)
            {
                int sx = surface->update.left + x;
                struct surface_tile *tile = surface_find_tile(surface, sx / TILE_SIZE, sy / TILE_SIZE, TRUE);
                UINT span = min(w - x, TILE_SIZE - sx % TILE_SIZE);
                DWORD *dst;

                if (!tile) break;
                dst = &tile->bits[(sy % TILE_SIZE) * TILE_SIZE + sx % TILE_SIZE];
                if (surface->format == DXGI_FORMAT_B8G8R8A8_UNORM && surface->alpha_mode != DXGI_ALPHA_MODE_IGNORE)
                    memcpy(dst, row + x * 4, span * 4);
                else
                    for (UINT i = 0; i < span; i++)
                        dst[i] = convert_pixel(row + (x + i) * bpp, surface->format, surface->alpha_mode);
                x += span;
            }
        }
        ID3D11DeviceContext_Unmap(context, (ID3D11Resource *)surface->staging, 0);
    }
    else ERR("Failed to map the staging texture, hr %#lx.\n", hr);
    ID3D11DeviceContext_Release(context);
}

/* ---- IDCompositionSurface ----------------------------------------------- */

static HRESULT STDMETHODCALLTYPE surface_QueryInterface(IDCompositionVirtualSurface *iface, REFIID iid, void **out)
{
    struct composition_surface *surface = impl_from_IDCompositionVirtualSurface(iface);

    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IDCompositionSurface)
            || (surface->is_virtual && IsEqualGUID(iid, &IID_IDCompositionVirtualSurface)))
    {
        IDCompositionVirtualSurface_AddRef(iface);
        *out = iface;
        return S_OK;
    }
    WARN("%s not implemented.\n", debugstr_guid(iid));
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE surface_AddRef(IDCompositionVirtualSurface *iface)
{
    return dcomp_object_addref(&impl_from_IDCompositionVirtualSurface(iface)->obj);
}

static ULONG STDMETHODCALLTYPE surface_Release(IDCompositionVirtualSurface *iface)
{
    return dcomp_object_release(&impl_from_IDCompositionVirtualSurface(iface)->obj);
}

static HRESULT surface_prepare_texture(struct composition_surface *surface, UINT width, UINT height)
{
    D3D11_TEXTURE2D_DESC desc;
    HRESULT hr;

    if (surface->texture)
    {
        ID3D11Texture2D_GetDesc(surface->texture, &desc);
        if (desc.Width >= width && desc.Height >= height) return S_OK;
        ID3D11Texture2D_Release(surface->texture);
        surface->texture = NULL;
    }
    if (!surface->d3d_device) return E_ILLEGAL_METHOD_CALL;

    /* dcomp/surface_draw: *_begin_texture2d */
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = surface->format;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    desc.CPUAccessFlags = 0;
    desc.MiscFlags = 0;
    if (FAILED(hr = ID3D11Device_CreateTexture2D(surface->d3d_device, &desc, NULL, &surface->texture)))
        ERR("Failed to create a %ux%u texture, hr %#lx.\n", width, height, hr);
    return hr;
}

static HRESULT surface_begin_d2d(struct composition_surface *surface, UINT width, UINT height, void **out)
{
    D2D1_BITMAP_PROPERTIES1 props = { { surface->format, D2D1_ALPHA_MODE_PREMULTIPLIED }, 96.0f, 96.0f,
            D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW, NULL };
    IDXGISurface *dxgi_surface;
    ID2D1Bitmap1 *bitmap;
    HRESULT hr;

    if (surface->alpha_mode == DXGI_ALPHA_MODE_IGNORE) props.pixelFormat.alphaMode = D2D1_ALPHA_MODE_IGNORE;
    if (!surface->d2d_context && FAILED(hr = ID2D1Device_CreateDeviceContext(surface->d2d_device,
            D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &surface->d2d_context)))
        return hr;
    if (FAILED(hr = ID3D11Texture2D_QueryInterface(surface->texture, &IID_IDXGISurface, (void **)&dxgi_surface)))
        return hr;
    hr = ID2D1DeviceContext_CreateBitmapFromDxgiSurface(surface->d2d_context, dxgi_surface, &props, &bitmap);
    IDXGISurface_Release(dxgi_surface);
    if (FAILED(hr)) return hr;
    ID2D1DeviceContext_SetTarget(surface->d2d_context, (ID2D1Image *)bitmap);
    ID2D1Bitmap1_Release(bitmap);
    ID2D1DeviceContext_BeginDraw(surface->d2d_context);
    ID2D1DeviceContext_AddRef(surface->d2d_context);
    *out = surface->d2d_context;
    return S_OK;
}

/* dcomp/surface_draw: *_begin_*, *_rules, *_virtual */
static HRESULT STDMETHODCALLTYPE surface_BeginDraw(IDCompositionVirtualSurface *iface, const RECT *rect,
        REFIID iid, void **out, POINT *offset)
{
    struct composition_surface *surface = impl_from_IDCompositionVirtualSurface(iface);
    struct composition_device *device = surface->obj.device;
    RECT update;
    HRESULT hr;

    TRACE("iface %p, rect %s, iid %s, out %p, offset %p.\n", iface, wine_dbgstr_rect(rect),
            debugstr_guid(iid), out, offset);

    if (!out || !offset) return E_INVALIDARG;
    *out = NULL;
    offset->x = offset->y = 0;

    if (rect) update = *rect;
    else SetRect(&update, 0, 0, surface->width, surface->height);
    if (update.left < 0 || update.top < 0 || update.right > (LONG)surface->width
            || update.bottom > (LONG)surface->height || update.left >= update.right || update.top >= update.bottom)
        return E_INVALIDARG;
    if (!surface->is_virtual && !surface->initialized && (update.left || update.top
            || update.right != surface->width || update.bottom != surface->height))
        return E_INVALIDARG;

    EnterCriticalSection(&dcomp_cs);
    if (device->drawing && device->drawing != surface)
    {
        LeaveCriticalSection(&dcomp_cs);
        return DCOMPOSITION_ERROR_SURFACE_BEING_RENDERED;
    }
    if (surface->state == SURFACE_DRAWING && surface->d2d_context)
        ID2D1DeviceContext_EndDraw(surface->d2d_context, NULL, NULL);

    if (FAILED(hr = surface_prepare_texture(surface, update.right - update.left, update.bottom - update.top)))
    {
        LeaveCriticalSection(&dcomp_cs);
        return hr;
    }
    if (IsEqualGUID(iid, &IID_ID2D1DeviceContext))
        hr = surface->d2d_device ? surface_begin_d2d(surface, update.right - update.left,
                update.bottom - update.top, out) : E_NOINTERFACE;
    else
        hr = ID3D11Texture2D_QueryInterface(surface->texture, iid, out);
    if (FAILED(hr))
    {
        WARN("Failed to get %s, hr %#lx.\n", debugstr_guid(iid), hr);
        *out = NULL;
        LeaveCriticalSection(&dcomp_cs);
        return E_NOINTERFACE;
    }
    surface->update = update;
    surface->state = SURFACE_DRAWING;
    device->drawing = surface;
    LeaveCriticalSection(&dcomp_cs);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE surface_EndDraw(IDCompositionVirtualSurface *iface)
{
    struct composition_surface *surface = impl_from_IDCompositionVirtualSurface(iface);
    struct composition_device *device = surface->obj.device;

    TRACE("iface %p.\n", iface);

    EnterCriticalSection(&dcomp_cs);
    if (surface->state == SURFACE_IDLE)
    {
        LeaveCriticalSection(&dcomp_cs);
        return DCOMPOSITION_ERROR_SURFACE_NOT_BEING_RENDERED;
    }
    if (surface->d2d_context && surface->state == SURFACE_DRAWING)
        ID2D1DeviceContext_EndDraw(surface->d2d_context, NULL, NULL);
    if (surface->d2d_context) ID2D1DeviceContext_SetTarget(surface->d2d_context, NULL);
    surface_read_back(surface);
    surface->initialized = TRUE;
    surface->state = SURFACE_IDLE;
    if (device->drawing == surface) device->drawing = NULL;
    LeaveCriticalSection(&dcomp_cs);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE surface_SuspendDraw(IDCompositionVirtualSurface *iface)
{
    struct composition_surface *surface = impl_from_IDCompositionVirtualSurface(iface);
    struct composition_device *device = surface->obj.device;
    HRESULT hr = S_OK;

    TRACE("iface %p.\n", iface);

    EnterCriticalSection(&dcomp_cs);
    if (surface->state == SURFACE_IDLE) hr = DCOMPOSITION_ERROR_SURFACE_NOT_BEING_RENDERED;
    else if (surface->state == SURFACE_SUSPENDED) hr = DCOMPOSITION_ERROR_SURFACE_BEING_RENDERED;
    else
    {
        if (surface->d2d_context) ID2D1DeviceContext_EndDraw(surface->d2d_context, NULL, NULL);
        surface->state = SURFACE_SUSPENDED;
        if (device->drawing == surface) device->drawing = NULL;
    }
    LeaveCriticalSection(&dcomp_cs);
    return hr;
}

static HRESULT STDMETHODCALLTYPE surface_ResumeDraw(IDCompositionVirtualSurface *iface)
{
    struct composition_surface *surface = impl_from_IDCompositionVirtualSurface(iface);
    struct composition_device *device = surface->obj.device;
    HRESULT hr = S_OK;

    TRACE("iface %p.\n", iface);

    EnterCriticalSection(&dcomp_cs);
    if (surface->state != SURFACE_SUSPENDED) hr = DCOMPOSITION_ERROR_SURFACE_NOT_BEING_RENDERED;
    else if (device->drawing) hr = DCOMPOSITION_ERROR_SURFACE_BEING_RENDERED;
    else
    {
        if (surface->d2d_context) ID2D1DeviceContext_BeginDraw(surface->d2d_context);
        surface->state = SURFACE_DRAWING;
        device->drawing = surface;
    }
    LeaveCriticalSection(&dcomp_cs);
    return hr;
}

static BOOL rect_inside(const RECT *rect, UINT width, UINT height)
{
    return rect->left >= 0 && rect->top >= 0 && rect->right <= (LONG)width && rect->bottom <= (LONG)height
            && rect->left <= rect->right && rect->top <= rect->bottom;
}

/* dcomp/surface_draw: *_rules (scroll*) */
static HRESULT STDMETHODCALLTYPE surface_Scroll(IDCompositionVirtualSurface *iface, const RECT *scroll,
        const RECT *clip, int dx, int dy)
{
    struct composition_surface *surface = impl_from_IDCompositionVirtualSurface(iface);
    RECT full, src, dst, bounds;
    DWORD *copy;
    int w, h;

    TRACE("iface %p, scroll %s, clip %s, offset %d,%d.\n", iface, wine_dbgstr_rect(scroll),
            wine_dbgstr_rect(clip), dx, dy);

    SetRect(&full, 0, 0, surface->width, surface->height);
    if ((scroll && !rect_inside(scroll, surface->width, surface->height))
            || (clip && !rect_inside(clip, surface->width, surface->height)))
        return E_INVALIDARG;

    EnterCriticalSection(&dcomp_cs);
    if (surface->state != SURFACE_IDLE)
    {
        LeaveCriticalSection(&dcomp_cs);
        return E_INVALIDARG;
    }
    src = scroll ? *scroll : full;
    bounds = clip ? *clip : full;
    dst = src;
    OffsetRect(&dst, dx, dy);
    IntersectRect(&dst, &dst, &bounds);
    w = src.right - src.left;
    h = src.bottom - src.top;
    if (w > 0 && h > 0 && !IsRectEmpty(&dst) && (copy = malloc((size_t)w * h * sizeof(*copy))))
    {
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
            {
                const DWORD *p = surface_get_pixel(surface, src.left + x, src.top + y);
                copy[y * w + x] = p ? *p : 0;
            }
        for (int y = dst.top; y < dst.bottom; y++)
            for (int x = dst.left; x < dst.right; x++)
            {
                struct surface_tile *tile = surface_find_tile(surface, x / TILE_SIZE, y / TILE_SIZE, TRUE);
                if (tile) tile->bits[(y % TILE_SIZE) * TILE_SIZE + x % TILE_SIZE] = copy[(y - dy - src.top) * w + x - dx - src.left];
            }
        free(copy);
    }
    LeaveCriticalSection(&dcomp_cs);
    return S_OK;
}

static BOOL valid_size(UINT width, UINT height, BOOL is_virtual)
{
    if (width > MAX_SURFACE_SIZE || height > MAX_SURFACE_SIZE) return FALSE;
    return is_virtual || (width && height);
}

/* dcomp/surface_draw: *_virtual */
static HRESULT STDMETHODCALLTYPE surface_Resize(IDCompositionVirtualSurface *iface, UINT width, UINT height)
{
    struct composition_surface *surface = impl_from_IDCompositionVirtualSurface(iface);
    RECT rect = { 0, 0, width, height };
    HRESULT hr = S_OK;

    TRACE("iface %p, %ux%u.\n", iface, width, height);

    if (!surface->is_virtual) return E_NOTIMPL;
    if (!valid_size(width, height, TRUE)) return E_INVALIDARG;
    EnterCriticalSection(&dcomp_cs);
    if (surface->state != SURFACE_IDLE) hr = DCOMPOSITION_ERROR_SURFACE_BEING_RENDERED;
    else
    {
        if (width < surface->width || height < surface->height) surface_trim_tiles(surface, &rect, 1);
        surface->width = width;
        surface->height = height;
    }
    LeaveCriticalSection(&dcomp_cs);
    return hr;
}

static HRESULT STDMETHODCALLTYPE surface_Trim(IDCompositionVirtualSurface *iface, const RECT *rects, UINT count)
{
    struct composition_surface *surface = impl_from_IDCompositionVirtualSurface(iface);

    TRACE("iface %p, rects %p, count %u.\n", iface, rects, count);

    if (!surface->is_virtual) return E_NOTIMPL;
    if (count && !rects) return E_INVALIDARG;
    EnterCriticalSection(&dcomp_cs);
    surface_trim_tiles(surface, rects, count);
    LeaveCriticalSection(&dcomp_cs);
    return S_OK;
}

static const IDCompositionVirtualSurfaceVtbl surface_vtbl =
{
    surface_QueryInterface,
    surface_AddRef,
    surface_Release,
    surface_BeginDraw,
    surface_EndDraw,
    surface_SuspendDraw,
    surface_ResumeDraw,
    surface_Scroll,
    surface_Resize,
    surface_Trim,
};

static void surface_destroy(struct dcomp_object *obj)
{
    struct composition_surface *surface = CONTAINING_RECORD(obj, struct composition_surface, obj);

    TRACE("Destroying surface %p.\n", surface);

    if (obj->device->drawing == surface) obj->device->drawing = NULL;
    surface_trim_tiles(surface, NULL, 0);
    if (surface->d2d_context)
    {
        if (surface->state == SURFACE_DRAWING) ID2D1DeviceContext_EndDraw(surface->d2d_context, NULL, NULL);
        ID2D1DeviceContext_Release(surface->d2d_context);
    }
    if (surface->texture) ID3D11Texture2D_Release(surface->texture);
    if (surface->staging) ID3D11Texture2D_Release(surface->staging);
    if (surface->d3d_device) ID3D11Device_Release(surface->d3d_device);
    if (surface->d2d_device) ID2D1Device_Release(surface->d2d_device);
    free(surface);
}

/* dcomp/surface_draw: dxgi_*, v1_*, dxgi_virtual_* */
HRESULT create_surface(struct composition_device *device, ID3D11Device *d3d, ID2D1Device *d2d, UINT width,
        UINT height, DXGI_FORMAT format, DXGI_ALPHA_MODE alpha_mode, BOOL is_virtual, IDCompositionVirtualSurface **out)
{
    struct composition_surface *surface;

    if (!out) return E_INVALIDARG;
    *out = NULL;
    if (!valid_size(width, height, is_virtual)) return E_INVALIDARG;
    if (format != DXGI_FORMAT_B8G8R8A8_UNORM && format != DXGI_FORMAT_R8G8B8A8_UNORM
            && format != DXGI_FORMAT_R16G16B16A16_FLOAT && format != DXGI_FORMAT_A8_UNORM)
        return E_INVALIDARG;
    if (alpha_mode != DXGI_ALPHA_MODE_PREMULTIPLIED && alpha_mode != DXGI_ALPHA_MODE_IGNORE
            && alpha_mode != DXGI_ALPHA_MODE_UNSPECIFIED)
        return E_INVALIDARG;

    if (!(surface = calloc(1, sizeof(*surface)))) return E_OUTOFMEMORY;
    surface->IDCompositionVirtualSurface_iface.lpVtbl = &surface_vtbl;
    surface->is_virtual = is_virtual;
    surface->width = width;
    surface->height = height;
    surface->format = format;
    surface->alpha_mode = alpha_mode;
    if ((surface->d3d_device = d3d)) ID3D11Device_AddRef(d3d);
    if ((surface->d2d_device = d2d)) ID2D1Device_AddRef(d2d);
    for (unsigned int i = 0; i < TILE_BUCKETS; i++) list_init(&surface->tiles[i]);
    dcomp_object_init(&surface->obj, device, surface_destroy);
    *out = &surface->IDCompositionVirtualSurface_iface;
    TRACE("Created surface %p, %ux%u.\n", surface, width, height);
    return S_OK;
}

/* ---- IDCompositionSurfaceFactory ---------------------------------------- */

struct composition_surface_factory
{
    IDCompositionSurfaceFactory IDCompositionSurfaceFactory_iface;
    struct dcomp_object obj;
    ID3D11Device *d3d_device;
    ID2D1Device *d2d_device;
};

static struct composition_surface_factory *impl_from_IDCompositionSurfaceFactory(IDCompositionSurfaceFactory *iface)
{
    return CONTAINING_RECORD(iface, struct composition_surface_factory, IDCompositionSurfaceFactory_iface);
}

static HRESULT STDMETHODCALLTYPE factory_QueryInterface(IDCompositionSurfaceFactory *iface, REFIID iid, void **out)
{
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IDCompositionSurfaceFactory))
    {
        IDCompositionSurfaceFactory_AddRef(iface);
        *out = iface;
        return S_OK;
    }
    WARN("%s not implemented.\n", debugstr_guid(iid));
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE factory_AddRef(IDCompositionSurfaceFactory *iface)
{
    return dcomp_object_addref(&impl_from_IDCompositionSurfaceFactory(iface)->obj);
}

static ULONG STDMETHODCALLTYPE factory_Release(IDCompositionSurfaceFactory *iface)
{
    return dcomp_object_release(&impl_from_IDCompositionSurfaceFactory(iface)->obj);
}

static HRESULT STDMETHODCALLTYPE factory_CreateSurface(IDCompositionSurfaceFactory *iface, UINT width, UINT height,
        DXGI_FORMAT format, DXGI_ALPHA_MODE alpha_mode, IDCompositionSurface **out)
{
    struct composition_surface_factory *factory = impl_from_IDCompositionSurfaceFactory(iface);
    TRACE("iface %p, %ux%u, format %#x, alpha mode %#x, out %p.\n", iface, width, height, format, alpha_mode, out);
    return create_surface(factory->obj.device, factory->d3d_device, factory->d2d_device, width, height,
            format, alpha_mode, FALSE, (IDCompositionVirtualSurface **)out);
}

static HRESULT STDMETHODCALLTYPE factory_CreateVirtualSurface(IDCompositionSurfaceFactory *iface, UINT width,
        UINT height, DXGI_FORMAT format, DXGI_ALPHA_MODE alpha_mode, IDCompositionVirtualSurface **out)
{
    struct composition_surface_factory *factory = impl_from_IDCompositionSurfaceFactory(iface);
    TRACE("iface %p, %ux%u, format %#x, alpha mode %#x, out %p.\n", iface, width, height, format, alpha_mode, out);
    return create_surface(factory->obj.device, factory->d3d_device, factory->d2d_device, width, height,
            format, alpha_mode, TRUE, out);
}

static const IDCompositionSurfaceFactoryVtbl factory_vtbl =
{
    factory_QueryInterface,
    factory_AddRef,
    factory_Release,
    factory_CreateSurface,
    factory_CreateVirtualSurface,
};

static void factory_destroy(struct dcomp_object *obj)
{
    struct composition_surface_factory *factory = CONTAINING_RECORD(obj, struct composition_surface_factory, obj);
    if (factory->d3d_device) ID3D11Device_Release(factory->d3d_device);
    if (factory->d2d_device) ID2D1Device_Release(factory->d2d_device);
    free(factory);
}

/* dcomp/device_create: surface_factory_* */
HRESULT create_surface_factory(struct composition_device *device, IUnknown *rendering_device,
        IDCompositionSurfaceFactory **out)
{
    struct composition_surface_factory *factory;
    ID3D11Device *d3d;
    ID2D1Device *d2d;
    HRESULT hr;

    if (!out) return E_INVALIDARG;
    *out = NULL;
    if (!rendering_device) return E_INVALIDARG;
    if (FAILED(hr = get_rendering_device(rendering_device, &d3d, &d2d))) return hr;
    if (!(factory = calloc(1, sizeof(*factory))))
    {
        if (d3d) ID3D11Device_Release(d3d);
        if (d2d) ID2D1Device_Release(d2d);
        return E_OUTOFMEMORY;
    }
    factory->IDCompositionSurfaceFactory_iface.lpVtbl = &factory_vtbl;
    factory->d3d_device = d3d;
    factory->d2d_device = d2d;
    dcomp_object_init(&factory->obj, device, factory_destroy);
    *out = &factory->IDCompositionSurfaceFactory_iface;
    return S_OK;
}

/* ---- surfaces from handles and windows ---------------------------------- *
 * dcomp/surface_draw: from_handle. They are accepted as visual content;
 * the compositor doesn't draw them yet. */

struct composition_foreign_surface
{
    IUnknown IUnknown_iface;
    struct dcomp_object obj;
    HANDLE handle;
    HWND hwnd;
};

static const IUnknownVtbl foreign_surface_vtbl;

static struct composition_foreign_surface *impl_from_foreign_IUnknown(IUnknown *iface)
{
    return CONTAINING_RECORD(iface, struct composition_foreign_surface, IUnknown_iface);
}

static HRESULT STDMETHODCALLTYPE foreign_surface_QueryInterface(IUnknown *iface, REFIID iid, void **out)
{
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_IUnknown))
    {
        IUnknown_AddRef(iface);
        *out = iface;
        return S_OK;
    }
    /* dcomp/surface_draw: from_handle (a failed query leaves *out alone) */
    WARN("%s not implemented.\n", debugstr_guid(iid));
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE foreign_surface_AddRef(IUnknown *iface)
{
    return dcomp_object_addref(&impl_from_foreign_IUnknown(iface)->obj);
}

static ULONG STDMETHODCALLTYPE foreign_surface_Release(IUnknown *iface)
{
    return dcomp_object_release(&impl_from_foreign_IUnknown(iface)->obj);
}

static const IUnknownVtbl foreign_surface_vtbl =
{
    foreign_surface_QueryInterface,
    foreign_surface_AddRef,
    foreign_surface_Release,
};

static void foreign_surface_destroy(struct dcomp_object *obj)
{
    free(CONTAINING_RECORD(obj, struct composition_foreign_surface, obj));
}

static HRESULT create_foreign_surface(struct composition_device *device, HANDLE handle, HWND hwnd, IUnknown **out)
{
    struct composition_foreign_surface *surface;

    if (!(surface = calloc(1, sizeof(*surface)))) return E_OUTOFMEMORY;
    surface->IUnknown_iface.lpVtbl = &foreign_surface_vtbl;
    surface->handle = handle;
    surface->hwnd = hwnd;
    dcomp_object_init(&surface->obj, device, foreign_surface_destroy);
    *out = &surface->IUnknown_iface;
    return S_OK;
}

HRESULT create_surface_from_handle(struct composition_device *device, HANDLE handle, IUnknown **out)
{
    DWORD flags;

    if (!out) return E_INVALIDARG;
    *out = NULL;
    if (handle && !GetHandleInformation(handle, &flags)) return E_HANDLE;
    if (handle) FIXME("Surface handle %p is not composed.\n", handle);
    return create_foreign_surface(device, handle, NULL, out);
}

HRESULT create_surface_from_hwnd(struct composition_device *device, HWND hwnd, IUnknown **out)
{
    if (!out) return E_INVALIDARG;
    *out = NULL;
    if (!hwnd || !IsWindow(hwnd)) return E_INVALIDARG;
    FIXME("Window %p is not composed.\n", hwnd);
    return create_foreign_surface(device, NULL, hwnd, out);
}

/* dcomp/target_visual: visual_setters (content_*); dcomp/surface_draw:
 * content_other_device, factory (a surface is content for the visuals of
 * the device that made it, or whose surface factory made it) */
HRESULT surface_check_content(struct composition_device *device, IUnknown *content)
{
    struct composition_surface *surface;
    IUnknown *swapchain;

    if ((surface = unsafe_impl_from_IDCompositionSurface(content)))
        return surface->obj.device == device ? S_OK : E_INVALIDARG;
    if (content->lpVtbl == &foreign_surface_vtbl)
        return impl_from_foreign_IUnknown(content)->obj.device == device ? S_OK : E_INVALIDARG;
    if (SUCCEEDED(IUnknown_QueryInterface(content, &IID_IDXGISwapChain1, (void **)&swapchain)))
    {
        FIXME("Swap chain %p is not composed.\n", content);
        IUnknown_Release(swapchain);
        return S_OK;
    }
    return E_INVALIDARG;
}
