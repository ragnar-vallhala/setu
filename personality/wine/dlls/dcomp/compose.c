/*
 * DirectComposition compositor
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

/* The visual trees are composed in memory when a device commits, and the
 * result is blended into the target windows with GDI. Offsets, 2D
 * transforms, rectangle clips and opacity are applied; 3D transforms,
 * filter effects and the other content kinds are not. */

#include "dcomp_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(dcomp);

struct canvas
{
    DWORD *bits; /* premultiplied BGRA, top-down */
    int width, height;
};

static D2D_MATRIX_3X2_F multiply(const D2D_MATRIX_3X2_F *a, const D2D_MATRIX_3X2_F *b)
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

static void transform_point(const D2D_MATRIX_3X2_F *m, float x, float y, float *ox, float *oy)
{
    *ox = x * m->_11 + y * m->_21 + m->_31;
    *oy = x * m->_12 + y * m->_22 + m->_32;
}

/* the bounds of a local rectangle in canvas pixels */
static void transform_rect(const D2D_MATRIX_3X2_F *m, const D2D_RECT_F *r, RECT *out)
{
    float x[4], y[4], l, t, rt, b;

    transform_point(m, r->left, r->top, &x[0], &y[0]);
    transform_point(m, r->right, r->top, &x[1], &y[1]);
    transform_point(m, r->left, r->bottom, &x[2], &y[2]);
    transform_point(m, r->right, r->bottom, &x[3], &y[3]);
    l = rt = x[0];
    t = b = y[0];
    for (int i = 1; i < 4; i++)
    {
        l = min(l, x[i]); rt = max(rt, x[i]);
        t = min(t, y[i]); b = max(b, y[i]);
    }
    out->left = (LONG)max(floorf(l), -1e9f);
    out->top = (LONG)max(floorf(t), -1e9f);
    out->right = (LONG)min(ceilf(rt), 1e9f);
    out->bottom = (LONG)min(ceilf(b), 1e9f);
}

static BOOL invert(const D2D_MATRIX_3X2_F *m, D2D_MATRIX_3X2_F *r)
{
    float det = m->_11 * m->_22 - m->_12 * m->_21;

    if (fabsf(det) < 1e-12f) return FALSE;
    r->_11 = m->_22 / det;
    r->_12 = -m->_12 / det;
    r->_21 = -m->_21 / det;
    r->_22 = m->_11 / det;
    r->_31 = (m->_21 * m->_32 - m->_22 * m->_31) / det;
    r->_32 = (m->_12 * m->_31 - m->_11 * m->_32) / det;
    return TRUE;
}

static inline DWORD blend(DWORD dst, DWORD src, unsigned int opacity)
{
    unsigned int sa, inv;
    DWORD r = 0;

    if (opacity < 255)
    {
        DWORD scaled = 0;
        for (int i = 0; i < 32; i += 8)
            scaled |= ((((src >> i) & 0xff) * opacity + 127) / 255) << i;
        src = scaled;
    }
    sa = src >> 24;
    if (sa == 255) return src;
    if (!sa && !src) return dst;
    inv = 255 - sa;
    for (int i = 0; i < 32; i += 8)
    {
        unsigned int v = ((src >> i) & 0xff) + (((dst >> i) & 0xff) * inv + 127) / 255;
        r |= min(v, 255u) << i;
    }
    return r;
}

static void draw_surface(struct canvas *canvas, struct composition_surface *surface, const D2D_MATRIX_3X2_F *world,
        const RECT *clip, unsigned int opacity)
{
    D2D_RECT_F bounds = { 0.0f, 0.0f, surface->width, surface->height };
    D2D_MATRIX_3X2_F inverse;
    RECT area;

    if (!surface->initialized || !invert(world, &inverse)) return;
    transform_rect(world, &bounds, &area);
    if (!IntersectRect(&area, &area, clip)) return;

    if (world->_11 == 1.0f && world->_22 == 1.0f && !world->_12 && !world->_21)
    {
        /* translation only, the common case */
        int ox = (int)floorf(world->_31 + 0.5f), oy = (int)floorf(world->_32 + 0.5f);

        for (int y = area.top; y < area.bottom; y++)
        {
            DWORD *row = canvas->bits + (size_t)y * canvas->width;
            for (int x = area.left; x < area.right; x++)
            {
                const DWORD *p = surface_get_pixel(surface, x - ox, y - oy);
                if (p) row[x] = blend(row[x], *p, opacity);
            }
        }
        return;
    }

    for (int y = area.top; y < area.bottom; y++)
    {
        DWORD *row = canvas->bits + (size_t)y * canvas->width;
        for (int x = area.left; x < area.right; x++)
        {
            const DWORD *p;
            float sx, sy;

            transform_point(&inverse, x + 0.5f, y + 0.5f, &sx, &sy);
            if ((p = surface_get_pixel(surface, (int)floorf(sx), (int)floorf(sy))))
                row[x] = blend(row[x], *p, opacity);
        }
    }
}

static void draw_visual(struct canvas *canvas, struct composition_visual *visual, const D2D_MATRIX_3X2_F *parent,
        const RECT *parent_clip, float parent_opacity)
{
    D2D_MATRIX_3X2_F local, world, offset = { { { 1.0f, 0.0f, 0.0f, 1.0f, visual->offset_x, visual->offset_y } } };
    struct composition_visual *child;
    struct composition_surface *surface;
    D2D_RECT_F clip_rect;
    float opacity;
    RECT clip;

    if (visual->walking || !visual->visible) return;
    visual->walking = TRUE;

    /* the visual's transform applies before its offset */
    if (!visual->transform_object || !transform_get_matrix(visual->transform_object, &local))
        local = visual->transform;
    local = multiply(&local, &offset);
    world = multiply(&local, parent);

    clip = *parent_clip;
    if (visual->has_clip || (visual->clip_object && clip_get_rect(visual->clip_object, &clip_rect)))
    {
        RECT bounds;
        if (visual->has_clip) clip_rect = visual->clip;
        transform_rect(&world, &clip_rect, &bounds);
        IntersectRect(&clip, &clip, &bounds);
    }

    opacity = parent_opacity * min(max(visual->opacity, 0.0f), 1.0f) * effect_get_opacity(visual->effect);

    if (!IsRectEmpty(&clip) && opacity > 0.0f)
    {
        if ((surface = unsafe_impl_from_IDCompositionSurface(visual->content)))
            draw_surface(canvas, surface, &world, &clip, (unsigned int)(opacity * 255.0f + 0.5f));

        LIST_FOR_EACH_ENTRY(child, &visual->children, struct composition_visual, entry)
            draw_visual(canvas, child, &world, &clip, opacity);
    }
    visual->walking = FALSE;
}

/* with dcomp_cs held */
static void compose_window(HWND hwnd, struct list *targets)
{
    static const D2D_MATRIX_3X2_F identity = { { { 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f } } };
    BLENDFUNCTION func = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    struct composition_target *target;
    struct canvas canvas;
    BITMAPINFO info = { { sizeof(info.bmiHeader) } };
    HBITMAP bitmap;
    HDC hdc, mem;
    RECT rect;
    void *bits;

    if (!GetClientRect(hwnd, &rect) || IsRectEmpty(&rect)) return;
    canvas.width = rect.right;
    canvas.height = rect.bottom;

    info.bmiHeader.biWidth = canvas.width;
    info.bmiHeader.biHeight = -canvas.height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    if (!(bitmap = CreateDIBSection(NULL, &info, DIB_RGB_COLORS, &bits, NULL, 0))) return;
    canvas.bits = bits;
    memset(canvas.bits, 0, (size_t)canvas.width * canvas.height * 4);

    /* the window's visuals first, then those on top of its children */
    for (int topmost = 0; topmost < 2; topmost++)
    {
        LIST_FOR_EACH_ENTRY(target, targets, struct composition_target, entry)
        {
            if (target->hwnd != hwnd || target->topmost != topmost || !target->root) continue;
            draw_visual(&canvas, target->root, &identity, &rect, 1.0f);
        }
    }

    if ((hdc = GetDCEx(hwnd, 0, DCX_CACHE | DCX_CLIPSIBLINGS)))
    {
        mem = CreateCompatibleDC(hdc);
        SelectObject(mem, bitmap);
        GdiAlphaBlend(hdc, 0, 0, canvas.width, canvas.height, mem, 0, 0, canvas.width, canvas.height, func);
        DeleteDC(mem);
        ReleaseDC(hwnd, hdc);
    }
    DeleteObject(bitmap);
}

/* composes every window one of the device's targets is for; the trees
 * of other devices' targets on those windows are drawn too */
void compose_device(struct composition_device *device)
{
    struct composition_target *target, *other;
    BOOL done;

    EnterCriticalSection(&dcomp_cs);
    LIST_FOR_EACH_ENTRY(target, &device->targets, struct composition_target, device_entry)
    {
        done = FALSE;
        LIST_FOR_EACH_ENTRY(other, &device->targets, struct composition_target, device_entry)
        {
            if (other == target) break;
            if (other->hwnd == target->hwnd) done = TRUE;
        }
        if (done) continue;
        TRACE("Composing window %p.\n", target->hwnd);
        compose_window(target->hwnd, target_list());
    }
    LeaveCriticalSection(&dcomp_cs);
}
