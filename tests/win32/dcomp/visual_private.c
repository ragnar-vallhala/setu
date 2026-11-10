/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: the methods of the undocumented visual interface
 * {2c4eef28-1bc0-4736-b7dd-b62692f9bd67} (the visual's own pointer, see
 * combase/composition_private) that Microsoft.UI.Windowing.Core calls, as
 * Firefox running as Windows 11 shows: methods 36, 37, 38, 40 and 41, each
 * with one float argument (Wine +dcomp trace). Black box: a visual with a
 * 32x32 opaque red surface at offset (20, 20) is the root of a target on a
 * topmost window; one method is called with a value, the device commits,
 * and the case records the result and what the screen shows in the
 * window's client area: the bounding box and count of red pixels, of pixels
 * with some red blended in, and the colour in the middle of the surface.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/dcomp/nn-dcomp-idcompositionvisual3
 *       https://learn.microsoft.com/en-us/windows/win32/api/dcomp/nf-dcomp-idcompositiondesktopdevice-createtargetforhwnd
 *       https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-bitblt
 */
#define COBJMACROS
#include "oracle.h"
#include "oracle_interactive.h"
#include <initguid.h>
#include <d3d11.h>
#include <dxgi.h>
#include <dcomptypes.h>

DEFINE_GUID(XIID_IDCompositionDesktopDevice, 0x5f4633fe, 0x1e08, 0x4cb8, 0x8c, 0x75, 0xce, 0x24, 0x33, 0x3f, 0x56, 0x02);
DEFINE_GUID(XIID_IDCompositionDevice3,       0x0987cb06, 0xf916, 0x48bf, 0x8d, 0x35, 0xce, 0x76, 0x41, 0x78, 0x1b, 0xd9);
DEFINE_GUID(XIID_IDXGISurface1_,             0x4ae63092, 0x6327, 0x4c1b, 0x80, 0xae, 0xbf, 0xe1, 0x2e, 0xa3, 0x2b, 0x86);
DEFINE_GUID(XIID_VisualX,                    0x2c4eef28, 0x1bc0, 0x4736, 0xb7, 0xdd, 0xb6, 0x26, 0x92, 0xf9, 0xbd, 0x67);

typedef HRESULT (WINAPI *create_fn)(IUnknown *, REFIID, void **);
typedef HRESULT (WINAPI *d3d11_create_fn)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT,
                                          const D3D_FEATURE_LEVEL *, UINT, UINT, ID3D11Device **,
                                          D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);
typedef HBRUSH (WINAPI *create_brush_fn)(COLORREF);
typedef BOOL (WINAPI *delete_object_fn)(HGDIOBJ);
typedef HDC (WINAPI *create_dc_fn)(HDC);
typedef BOOL (WINAPI *delete_dc_fn)(HDC);
typedef HGDIOBJ (WINAPI *select_fn)(HDC, HGDIOBJ);
typedef HBITMAP (WINAPI *create_dib_fn)(HDC, const BITMAPINFO *, UINT, void **, HANDLE, DWORD);
typedef BOOL (WINAPI *bitblt_fn)(HDC, int, int, int, int, HDC, int, int, DWORD);

#define VT(obj, idx) ((*(void ***)(obj))[idx])
typedef HRESULT (STDMETHODCALLTYPE *m0)(void *);
typedef HRESULT (STDMETHODCALLTYPE *m1p)(void *, void *);
typedef HRESULT (STDMETHODCALLTYPE *mf)(void *, float);
typedef HRESULT (STDMETHODCALLTYPE *mqi)(void *, REFIID, void **);
typedef ULONG (STDMETHODCALLTYPE *mref)(void *);
typedef HRESULT (STDMETHODCALLTYPE *mtarget)(void *, HWND, BOOL, void **);
typedef HRESULT (STDMETHODCALLTYPE *mcreate_surface)(void *, UINT, UINT, DXGI_FORMAT, DXGI_ALPHA_MODE, void **);
typedef HRESULT (STDMETHODCALLTYPE *mbegin)(void *, const RECT *, REFIID, void **, POINT *);

/* IDCompositionDesktopDevice */
#define D_Commit 3
#define D_WaitForCommitCompletion 4
#define D_CreateSurface 8
#define D_CreateTargetForHwnd 24
/* IDCompositionDevice3 */
#define D3_CreateVisual 6
/* IDCompositionVisual */
#define V_SetOffsetX 4
#define V_SetOffsetY 6
#define V_SetContent 15
#define V_AddVisual 16
/* IDCompositionTarget */
#define T_SetRoot 3
/* IDCompositionSurface */
#define S_BeginDraw 3
#define S_EndDraw 4

#define WIN_W 160
#define WIN_H 120

static ULONG rel(void *p) { return p ? ((mref)VT(p, 2))(p) : 0; }
static HRESULT qi(void *p, REFIID iid, void **out) { return ((mqi)VT(p, 0))(p, iid, out); }
static void o_hr(const char *k, HRESULT hr) { o_hex32(k, (DWORD)hr); }

static void *dev, *dev3;
static create_brush_fn pCreateSolidBrush;
static delete_object_fn pDeleteObject;
static create_dc_fn pCreateCompatibleDC;
static delete_dc_fn pDeleteDC;
static select_fn pSelectObject;
static create_dib_fn pCreateDIBSection;
static bitblt_fn pBitBlt;

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_ERASEBKGND) {
        RECT r;
        HBRUSH b = pCreateSolidBrush(RGB(255, 255, 255));
        GetClientRect(hwnd, &r);
        FillRect((HDC)wp, &r, b);
        pDeleteObject(b);
        return 1;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void pump(DWORD ms)
{
    DWORD end = GetTickCount() + ms;
    MSG msg;
    while ((LONG)(end - GetTickCount()) > 0) {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
        Sleep(10);
    }
}

/* the red surface: GDI fills it, alpha ignored */
static void *red_surface(void)
{
    void *s = NULL;
    IDXGISurface1 *s1 = NULL;
    POINT off;
    HDC hdc;
    if (FAILED(((mcreate_surface)VT(dev, D_CreateSurface))(dev, 32, 32, DXGI_FORMAT_B8G8R8A8_UNORM,
                                                          DXGI_ALPHA_MODE_IGNORE, &s))) return NULL;
    if (SUCCEEDED(((mbegin)VT(s, S_BeginDraw))(s, NULL, &XIID_IDXGISurface1_, (void **)&s1, &off))) {
        if (SUCCEEDED(IDXGISurface1_GetDC(s1, FALSE, &hdc))) {
            RECT r = { off.x, off.y, off.x + 32, off.y + 32 };
            HBRUSH b = pCreateSolidBrush(RGB(255, 0, 0));
            FillRect(hdc, &r, b);
            pDeleteObject(b);
            IDXGISurface1_ReleaseDC(s1, NULL);
        }
        rel(s1);
        ((m0)VT(s, S_EndDraw))(s);
    }
    return s;
}

static void o_screen(HWND hwnd)
{
    BITMAPINFO info;
    POINT org = { 0, 0 };
    HDC screen, mem;
    HBITMAP bmp;
    DWORD *bits = NULL;
    int rl = WIN_W, rt = WIN_H, rr = -1, rb = -1, rn = 0;
    int pl = WIN_W, pt = WIN_H, pr = -1, pb = -1, pn = 0;
    char buf[64];

    memset(&info, 0, sizeof info);
    info.bmiHeader.biSize = sizeof info.bmiHeader;
    info.bmiHeader.biWidth = WIN_W;
    info.bmiHeader.biHeight = -WIN_H;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    ClientToScreen(hwnd, &org);
    screen = GetDC(NULL);
    mem = pCreateCompatibleDC(screen);
    bmp = pCreateDIBSection(screen, &info, DIB_RGB_COLORS, (void **)&bits, NULL, 0);
    pSelectObject(mem, bmp);
    pBitBlt(mem, 0, 0, WIN_W, WIN_H, screen, org.x, org.y, SRCCOPY);
    for (int y = 0; bits && y < WIN_H; y++)
        for (int x = 0; x < WIN_W; x++) {
            DWORD p = bits[y * WIN_W + x];
            int r = (p >> 16) & 0xff, g = (p >> 8) & 0xff, b = p & 0xff;
            if (r > 200 && g < 60 && b < 60) {
                rn++;
                if (x < rl) rl = x;
                if (y < rt) rt = y;
                if (x > rr) rr = x;
                if (y > rb) rb = y;
            } else if (r > g + 30 && r > b + 30) {
                pn++;
                if (x < pl) pl = x;
                if (y < pt) pt = y;
                if (x > pr) pr = x;
                if (y > pb) pb = y;
            }
        }
    if (rn) snprintf(buf, sizeof buf, "%d,%d,%d,%d", rl, rt, rr + 1, rb + 1);
    o_str("red_box", rn ? buf : "none");
    o_u32("red_count", rn);
    if (pn) snprintf(buf, sizeof buf, "%d,%d,%d,%d", pl, pt, pr + 1, pb + 1);
    o_str("tinted_box", pn ? buf : "none");
    o_u32("tinted_count", pn);
    if (bits) o_hex32("center", bits[36 * WIN_W + 36] & 0xffffff);
    pDeleteDC(mem);
    pDeleteObject(bmp);
    ReleaseDC(NULL, screen);
}

/* slot < 0: no call */
static void do_case(const char *name, int slot, float value, int slot2, float value2)
{
    HWND hwnd = CreateWindowExW(WS_EX_TOPMOST, L"SetuVisualPrivate", L"v", WS_POPUP | WS_VISIBLE,
                                100, 100, WIN_W, WIN_H, NULL, NULL, NULL, NULL);
    void *target = NULL, *visual = NULL, *x = NULL, *s;
    HRESULT hr;

    pump(200);
    oracle_case(name);
    if (FAILED(hr = ((mtarget)VT(dev, D_CreateTargetForHwnd))(dev, hwnd, TRUE, &target)) ||
        FAILED(hr = ((m1p)VT(dev3, D3_CreateVisual))(dev3, &visual))) {
        o_hr("setup", hr);
    } else {
        s = red_surface();
        ((mf)VT(visual, V_SetOffsetX))(visual, 20.0f);
        ((mf)VT(visual, V_SetOffsetY))(visual, 20.0f);
        ((m1p)VT(visual, V_SetContent))(visual, s);
        ((m1p)VT(target, T_SetRoot))(target, visual);
        if (slot >= 0) {
            o_hr("qi", qi(visual, &XIID_VisualX, &x));
            if (x) {
                o_hr("hr", ((mf)VT(x, slot))(x, value));
                if (slot2 >= 0) o_hr("hr2", ((mf)VT(x, slot2))(x, value2));
                rel(x);
            }
        }
        o_hr("commit", ((m0)VT(dev, D_Commit))(dev));
        ((m0)VT(dev, D_WaitForCommitCompletion))(dev);
        pump(400);
        o_screen(hwnd);
        rel(s);
    }
    oracle_case_end();
    rel(visual);
    rel(target);
    DestroyWindow(hwnd);
    pump(50);
}

/* the red visual is a child of a contentless root at (0, 0); the root gets
 * root_slot(root_value), the child child_slot(child_value) */
static void do_child_case(const char *name, int root_slot, float root_value, int child_slot, float child_value)
{
    HWND hwnd = CreateWindowExW(WS_EX_TOPMOST, L"SetuVisualPrivate", L"v", WS_POPUP | WS_VISIBLE,
                                100, 100, WIN_W, WIN_H, NULL, NULL, NULL, NULL);
    void *target = NULL, *root = NULL, *visual = NULL, *x = NULL, *s;
    HRESULT hr;

    pump(200);
    oracle_case(name);
    if (FAILED(hr = ((mtarget)VT(dev, D_CreateTargetForHwnd))(dev, hwnd, TRUE, &target)) ||
        FAILED(hr = ((m1p)VT(dev3, D3_CreateVisual))(dev3, &root)) ||
        FAILED(hr = ((m1p)VT(dev3, D3_CreateVisual))(dev3, &visual))) {
        o_hr("setup", hr);
    } else {
        s = red_surface();
        ((mf)VT(visual, V_SetOffsetX))(visual, 20.0f);
        ((mf)VT(visual, V_SetOffsetY))(visual, 20.0f);
        ((m1p)VT(visual, V_SetContent))(visual, s);
        o_hr("add", ((HRESULT (STDMETHODCALLTYPE *)(void *, void *, BOOL, void *))VT(root, V_AddVisual))(root, visual, TRUE, NULL));
        ((m1p)VT(target, T_SetRoot))(target, root);
        if (root_slot >= 0 && SUCCEEDED(qi(root, &XIID_VisualX, &x))) {
            o_hr("hr_root", ((mf)VT(x, root_slot))(x, root_value));
            rel(x);
            x = NULL;
        }
        if (child_slot >= 0 && SUCCEEDED(qi(visual, &XIID_VisualX, &x))) {
            o_hr("hr_child", ((mf)VT(x, child_slot))(x, child_value));
            rel(x);
        }
        o_hr("commit", ((m0)VT(dev, D_Commit))(dev));
        ((m0)VT(dev, D_WaitForCommitCompletion))(dev);
        pump(400);
        o_screen(hwnd);
        rel(s);
    }
    oracle_case_end();
    rel(visual);
    rel(root);
    rel(target);
    DestroyWindow(hwnd);
    pump(50);
}

static int run_cases(void)
{
    HMODULE dcomp = LoadLibraryW(L"dcomp.dll");
    HMODULE d3d11 = LoadLibraryW(L"d3d11.dll");
    HMODULE gdi32 = LoadLibraryW(L"gdi32.dll");
    create_fn create3 = dcomp ? (create_fn)(void (*)(void))GetProcAddress(dcomp, "DCompositionCreateDevice3") : NULL;
    d3d11_create_fn pD3D11CreateDevice = d3d11 ? (d3d11_create_fn)(void (*)(void))GetProcAddress(d3d11, "D3D11CreateDevice") : NULL;
    ID3D11Device *d3d = NULL;
    IDXGIDevice *dxgi = NULL;
    WNDCLASSW wc;
    HRESULT hr = E_FAIL;
    char name[64];
    static const struct { int slot; float values[4]; int n; } calls[] = {
        { 36, { 0.0f, 8.0f, 64.0f }, 3 },
        { 37, { 0.0f, 8.0f, 64.0f }, 3 },
        { 38, { 0.0f, 0.5f, 1.0f, 8.0f }, 4 },
        { 40, { 0.0f, 0.5f, 8.0f, 64.0f }, 4 },
        { 41, { 0.0f, 0.5f, 8.0f }, 3 },
    };

    pCreateSolidBrush = (create_brush_fn)(void (*)(void))GetProcAddress(gdi32, "CreateSolidBrush");
    pDeleteObject = (delete_object_fn)(void (*)(void))GetProcAddress(gdi32, "DeleteObject");
    pCreateCompatibleDC = (create_dc_fn)(void (*)(void))GetProcAddress(gdi32, "CreateCompatibleDC");
    pDeleteDC = (delete_dc_fn)(void (*)(void))GetProcAddress(gdi32, "DeleteDC");
    pSelectObject = (select_fn)(void (*)(void))GetProcAddress(gdi32, "SelectObject");
    pCreateDIBSection = (create_dib_fn)(void (*)(void))GetProcAddress(gdi32, "CreateDIBSection");
    pBitBlt = (bitblt_fn)(void (*)(void))GetProcAddress(gdi32, "BitBlt");

    oracle_begin("dcomp/visual_private");
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = wndproc;
    wc.lpszClassName = L"SetuVisualPrivate";
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    RegisterClassW(&wc);
    if (pD3D11CreateDevice)
        pD3D11CreateDevice(NULL, D3D_DRIVER_TYPE_WARP, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                           NULL, 0, D3D11_SDK_VERSION, &d3d, NULL, NULL);
    if (d3d) ID3D11Device_QueryInterface(d3d, &IID_IDXGIDevice, (void **)&dxgi);
    if (create3 && dxgi) hr = create3((IUnknown *)dxgi, &XIID_IDCompositionDesktopDevice, &dev);
    oracle_case("setup");
    o_hr("create", hr);
    if (dev) o_hr("qi_device3", qi(dev, &XIID_IDCompositionDevice3, &dev3));
    oracle_case_end();
    if (!dev3) { rel(dev); return oracle_end(); }

    do_case("baseline", -1, 0.0f, -1, 0.0f);
    for (size_t i = 0; i < sizeof calls / sizeof calls[0]; i++)
        for (int j = 0; j < calls[i].n; j++) {
            snprintf(name, sizeof name, "s%d_%g", calls[i].slot, calls[i].values[j]);
            for (char *c = name; *c; c++) if (*c == '.') *c = 'p';
            do_case(name, calls[i].slot, calls[i].values[j], -1, 0.0f);
        }
    /* the pairs Microsoft.UI.Windowing.Core makes */
    do_case("s36_8_s37_8", 36, 8.0f, 37, 8.0f);
    do_case("s36_64_s37_64", 36, 64.0f, 37, 64.0f);
    do_case("s38_0_s36_8", 38, 0.0f, 36, 8.0f);
    do_case("s38_1_s36_8", 38, 1.0f, 36, 8.0f);
    do_case("s40_8_s41_8", 40, 8.0f, 41, 8.0f);
    do_case("s38_m0p25", 38, -0.25f, -1, 0.0f);
    do_case("s39_0p5", 39, 0.5f, -1, 0.0f);
    do_case("s39_0p25", 39, 0.25f, -1, 0.0f);
    /* what the child's relative offset is relative to */
    do_child_case("child_s38_0p5", -1, 0.0f, 38, 0.5f);
    do_child_case("child_s38_0p5_root_s36_40", 36, 40.0f, 38, 0.5f);
    do_child_case("child_s38_0p5_root_s40_0p25", 40, 0.25f, 38, 0.5f);
    do_child_case("child_s39_0p5_root_s37_40", 37, 40.0f, 39, 0.5f);
    do_child_case("child_s39_0p5_root_s41_0p25", 41, 0.25f, 39, 0.5f);

    rel(dev3);
    rel(dev);
    if (dxgi) IDXGIDevice_Release(dxgi);
    if (d3d) ID3D11Device_Release(d3d);
    return oracle_end();
}

int main(void)
{
    return oracle_interactive_main(L"SetuOracleDcompVisualPrivate", run_cases, oracle_dcomp_denied);
}
