/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: drawing into a DirectComposition surface with GDI. BeginDraw
 * with IDXGISurface1, then IDXGISurface1::GetDC (with and without discard,
 * for a full and a partial update rectangle, across pixel formats), a GDI
 * fill read back with GetPixel, ReleaseDC and EndDraw; GetDC twice, EndDraw
 * with the DC still out; and the resource flags of the texture BeginDraw
 * hands out for ID3D11Texture2D (whether it is GDI compatible).
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/dcomp/nf-dcomp-idcompositionsurface-begindraw
 *       https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgisurface1-getdc
 *       https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgisurface1-releasedc
 *       https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_resource_misc_flag
 */
#define COBJMACROS
#include "oracle.h"
#include "oracle_interactive.h"
#include <initguid.h>
#include <d3d11.h>
#include <dxgi.h>
#include <dcomptypes.h>

DEFINE_GUID(XIID_IDCompositionDesktopDevice, 0x5f4633fe, 0x1e08, 0x4cb8, 0x8c, 0x75, 0xce, 0x24, 0x33, 0x3f, 0x56, 0x02);
DEFINE_GUID(XIID_IDXGISurface1_,             0x4ae63092, 0x6327, 0x4c1b, 0x80, 0xae, 0xbf, 0xe1, 0x2e, 0xa3, 0x2b, 0x86);
DEFINE_GUID(XIID_ID3D11Texture2D_,           0x6f15aaf2, 0xd208, 0x4e89, 0x9a, 0xb4, 0x48, 0x95, 0x35, 0xd3, 0x4f, 0x9c);

typedef HRESULT (WINAPI *create_fn)(IUnknown *, REFIID, void **);
typedef HRESULT (WINAPI *d3d11_create_fn)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT,
                                          const D3D_FEATURE_LEVEL *, UINT, UINT, ID3D11Device **,
                                          D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);
typedef HBRUSH (WINAPI *create_brush_fn)(COLORREF);
typedef COLORREF (WINAPI *get_pixel_fn)(HDC, int, int);
typedef BOOL (WINAPI *delete_object_fn)(HGDIOBJ);

#define VT(obj, idx) ((*(void ***)(obj))[idx])
typedef HRESULT (STDMETHODCALLTYPE *m0)(void *);
typedef ULONG (STDMETHODCALLTYPE *mref)(void *);
typedef HRESULT (STDMETHODCALLTYPE *mcreate_surface)(void *, UINT, UINT, DXGI_FORMAT, DXGI_ALPHA_MODE, void **);
typedef HRESULT (STDMETHODCALLTYPE *mbegin)(void *, const RECT *, REFIID, void **, POINT *);

#define D_CreateSurface 8
#define S_BeginDraw 3
#define S_EndDraw 4

static ULONG rel(void *p) { return p ? ((mref)VT(p, 2))(p) : 0; }
static void o_hr(const char *k, HRESULT hr) { o_hex32(k, (DWORD)hr); }

static create_fn create2;
static IDXGIDevice *dxgi;
static create_brush_fn pCreateSolidBrush;
static get_pixel_fn pGetPixel;
static delete_object_fn pDeleteObject;

static void *new_surface(void *dev, DXGI_FORMAT f, DXGI_ALPHA_MODE a, HRESULT *hr)
{
    void *s = NULL;
    *hr = ((mcreate_surface)VT(dev, D_CreateSurface))(dev, 64, 32, f, a, &s);
    return SUCCEEDED(*hr) ? s : NULL;
}

/* fill a 4x4 square at the update offset and read its middle back */
static void o_fill(HDC hdc, POINT off)
{
    RECT r = { off.x, off.y, off.x + 4, off.y + 4 };
    HBRUSH b = pCreateSolidBrush ? pCreateSolidBrush(RGB(0x12, 0x34, 0x56)) : NULL;
    o_bool("fill", b && FillRect(hdc, &r, b));
    if (pGetPixel) o_hex32("pixel", pGetPixel(hdc, off.x + 1, off.y + 1) & 0xffffff);
    if (b) pDeleteObject(b);
}

static void gdi_cases(void *dev)
{
    static const struct { const char *n; DXGI_FORMAT f; DXGI_ALPHA_MODE a; } c[] = {
        { "bgra_premul", DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED },
        { "bgra_ignore", DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_IGNORE },
        { "bgrx_ignore", DXGI_FORMAT_B8G8R8X8_UNORM, DXGI_ALPHA_MODE_IGNORE },
        { "bgra_srgb", DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, DXGI_ALPHA_MODE_PREMULTIPLIED },
        { "rgba_premul", DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED },
        { "rgba16f_premul", DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_ALPHA_MODE_PREMULTIPLIED },
    };
    char name[96];

    for (size_t i = 0; i < sizeof c / sizeof c[0]; i++) {
        for (int partial = 0; partial < 2; partial++) {
            RECT part = { 8, 8, 24, 16 };
            IDXGISurface1 *s1 = NULL;
            POINT off = { -7, -7 };
            HDC hdc = (HDC)0x1;
            HRESULT hr;
            void *s = new_surface(dev, c[i].f, c[i].a, &hr);

            snprintf(name, sizeof name, "%s%s", c[i].n, partial ? "_partial" : "");
            oracle_case(name);
            o_hr("create", hr);
            if (s) {
                hr = ((mbegin)VT(s, S_BeginDraw))(s, partial ? &part : NULL, &XIID_IDXGISurface1_, (void **)&s1, &off);
                o_hr("begin", hr);
                if (SUCCEEDED(hr)) {
                    hr = IDXGISurface1_GetDC(s1, FALSE, &hdc);
                    o_hr("getdc", hr);
                    o_str("hdc", hdc == NULL ? "null" : hdc == (HDC)0x1 ? "untouched" : "set");
                    if (SUCCEEDED(hr)) {
                        o_fill(hdc, off);
                        o_hr("releasedc", IDXGISurface1_ReleaseDC(s1, NULL));
                    }
                    rel(s1);
                    o_hr("end", ((m0)VT(s, S_EndDraw))(s));
                }
                rel(s);
            }
            oracle_case_end();
        }
    }

    {   /* GetDC with discard */
        IDXGISurface1 *s1 = NULL;
        POINT off;
        HDC hdc = (HDC)0x1;
        HRESULT hr;
        void *s = new_surface(dev, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED, &hr);
        oracle_case("getdc_discard");
        if (s && SUCCEEDED(((mbegin)VT(s, S_BeginDraw))(s, NULL, &XIID_IDXGISurface1_, (void **)&s1, &off))) {
            hr = IDXGISurface1_GetDC(s1, TRUE, &hdc);
            o_hr("getdc", hr);
            if (SUCCEEDED(hr)) {
                o_fill(hdc, off);
                o_hr("releasedc", IDXGISurface1_ReleaseDC(s1, NULL));
            }
            rel(s1);
            o_hr("end", ((m0)VT(s, S_EndDraw))(s));
        }
        oracle_case_end();
        rel(s);
    }
    {   /* GetDC twice; then EndDraw with the DC still out */
        IDXGISurface1 *s1 = NULL;
        POINT off;
        HDC hdc = (HDC)0x1, hdc2 = (HDC)0x1;
        HRESULT hr;
        void *s = new_surface(dev, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED, &hr);
        oracle_case("getdc_twice");
        if (s && SUCCEEDED(((mbegin)VT(s, S_BeginDraw))(s, NULL, &XIID_IDXGISurface1_, (void **)&s1, &off))) {
            o_hr("first", IDXGISurface1_GetDC(s1, FALSE, &hdc));
            hr = IDXGISurface1_GetDC(s1, FALSE, &hdc2);
            o_hr("second", hr);
            o_str("second_hdc", hdc2 == NULL ? "null" : hdc2 == (HDC)0x1 ? "untouched" : hdc2 == hdc ? "same" : "other");
            o_hr("end_with_dc", ((m0)VT(s, S_EndDraw))(s));
            o_hr("releasedc_after_end", IDXGISurface1_ReleaseDC(s1, NULL));
            rel(s1);
        }
        oracle_case_end();
        rel(s);
    }
    {   /* the texture BeginDraw hands out: bind and misc flags */
        static const struct { const char *n; DXGI_FORMAT f; DXGI_ALPHA_MODE a; } t[] = {
            { "texture_bgra", DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED },
            { "texture_bgrx", DXGI_FORMAT_B8G8R8X8_UNORM, DXGI_ALPHA_MODE_IGNORE },
            { "texture_rgba", DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED },
            { "texture_rgba16f", DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_ALPHA_MODE_PREMULTIPLIED },
        };
        for (size_t i = 0; i < sizeof t / sizeof t[0]; i++) {
            ID3D11Texture2D *tex = NULL;
            POINT off;
            HRESULT hr;
            void *s = new_surface(dev, t[i].f, t[i].a, &hr);
            oracle_case(t[i].n);
            o_hr("create", hr);
            if (s && SUCCEEDED(hr = ((mbegin)VT(s, S_BeginDraw))(s, NULL, &XIID_ID3D11Texture2D_, (void **)&tex, &off))) {
                D3D11_TEXTURE2D_DESC d;
                ID3D11Texture2D_GetDesc(tex, &d);
                o_hex32("bind", d.BindFlags);
                o_bool("gdi_compatible", !!(d.MiscFlags & D3D11_RESOURCE_MISC_GDI_COMPATIBLE));
                o_hex32("misc", d.MiscFlags);
                rel(tex);
                o_hr("end", ((m0)VT(s, S_EndDraw))(s));
            } else o_hr("begin", hr);
            oracle_case_end();
            rel(s);
        }
    }
}

static int run_cases(void)
{
    HMODULE dcomp = LoadLibraryW(L"dcomp.dll");
    HMODULE d3d11 = LoadLibraryW(L"d3d11.dll");
    HMODULE gdi32 = LoadLibraryW(L"gdi32.dll");
    d3d11_create_fn pD3D11CreateDevice;
    ID3D11Device *d3d = NULL;
    void *dev = NULL;

    oracle_begin("dcomp/surface_gdi");
    create2 = dcomp ? (create_fn)(void (*)(void))GetProcAddress(dcomp, "DCompositionCreateDevice2") : NULL;
    pD3D11CreateDevice = d3d11 ? (d3d11_create_fn)(void (*)(void))GetProcAddress(d3d11, "D3D11CreateDevice") : NULL;
    pCreateSolidBrush = (create_brush_fn)(void (*)(void))GetProcAddress(gdi32, "CreateSolidBrush");
    pGetPixel = (get_pixel_fn)(void (*)(void))GetProcAddress(gdi32, "GetPixel");
    pDeleteObject = (delete_object_fn)(void (*)(void))GetProcAddress(gdi32, "DeleteObject");
    /* WARP, as Microsoft.UI.Windowing.Core uses */
    if (pD3D11CreateDevice)
        pD3D11CreateDevice(NULL, D3D_DRIVER_TYPE_WARP, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                           NULL, 0, D3D11_SDK_VERSION, &d3d, NULL, NULL);
    if (d3d) ID3D11Device_QueryInterface(d3d, &IID_IDXGIDevice, (void **)&dxgi);
    oracle_case("setup");
    o_bool("device", dxgi != NULL);
    if (create2 && dxgi) o_hr("create", create2((IUnknown *)dxgi, &XIID_IDCompositionDesktopDevice, &dev));
    oracle_case_end();
    if (dev) gdi_cases(dev);
    rel(dev);
    if (dxgi) IDXGIDevice_Release(dxgi);
    if (d3d) ID3D11Device_Release(d3d);
    return oracle_end();
}

int main(void)
{
    return oracle_interactive_main(L"SetuOracleDcompSurfaceGdi", run_cases, oracle_dcomp_denied);
}
