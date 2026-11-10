/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: method 71 of the DirectComposition device's undocumented
 * {ca67b562-1c32-4017-9dd9-3d4b7e2510aa} interface (the IDCompositionDesktopDevice
 * pointer, see combase/composition_private). Microsoft.UI.Windowing.Core,
 * loaded by Firefox running as Windows 11, calls it once per top-level
 * window as (this, HWND, void *a, 0x5a, void *b) with a and b pointing at
 * zeroed memory (Wine +dcomp trace). Black box: the call is made with
 * zeroed buffers for a and b, on its own thread so that a fault ends only
 * that thread; the case records the result, which pointer-sized slots of
 * the buffers were written, and the QueryInterface table of what was
 * written into the first slot. Variations: another third argument, a NULL
 * and a destroyed window, a child window, the same window twice, and a
 * window of another thread.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/dcomp/nf-dcomp-dcompositioncreatedevice3
 *       https://learn.microsoft.com/en-us/windows/win32/api/dcomp/nf-dcomp-idcompositiondesktopdevice-createtargetforhwnd
 */
#define COBJMACROS
#include "oracle.h"
#include "oracle_interactive.h"
#include <initguid.h>
#include <d3d11.h>
#include <dxgi.h>

DEFINE_GUID(XIID_IUnknown,                   0x00000000, 0x0000, 0x0000, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
DEFINE_GUID(XIID_IMarshal,                   0x00000003, 0x0000, 0x0000, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
DEFINE_GUID(XIID_IAgileObject,               0x94ea2b94, 0xe9cc, 0x49e0, 0xc0, 0xff, 0xee, 0x64, 0xca, 0x8f, 0x5b, 0x90);
DEFINE_GUID(XIID_IInspectable,               0xaf86e2e0, 0xb12d, 0x4c6a, 0x9c, 0x5a, 0xd7, 0xaa, 0x65, 0x10, 0x1e, 0x90);
DEFINE_GUID(XIID_IDCompositionDevice,        0xc37ea93a, 0xe7aa, 0x450d, 0xb1, 0x6f, 0x97, 0x46, 0xcb, 0x04, 0x07, 0xf3);
DEFINE_GUID(XIID_IDCompositionDesktopDevice, 0x5f4633fe, 0x1e08, 0x4cb8, 0x8c, 0x75, 0xce, 0x24, 0x33, 0x3f, 0x56, 0x02);
DEFINE_GUID(XIID_IDCompositionTarget,        0xeacdd04c, 0x117e, 0x4e17, 0x88, 0xf4, 0xd1, 0xb1, 0x2b, 0x0e, 0x3d, 0x89);
DEFINE_GUID(XIID_IDCompositionVisual,        0x4d93059d, 0x097b, 0x4651, 0x9a, 0x60, 0xf0, 0xf2, 0x51, 0x16, 0xe2, 0xf3);
DEFINE_GUID(XIID_IDCompositionVisual2,       0xe8de1639, 0x4331, 0x4b26, 0xbc, 0x5f, 0x6a, 0x32, 0x1d, 0x34, 0x7a, 0x85);
DEFINE_GUID(XIID_IDCompositionVisual3,       0x2775f462, 0xb6c1, 0x4015, 0xb0, 0xbe, 0xb3, 0xe7, 0xd6, 0xa4, 0x97, 0x6d);
DEFINE_GUID(XIID_IDCompositionSurface,       0xbb8a4953, 0x2c99, 0x4f5a, 0x96, 0xf5, 0x48, 0x19, 0x02, 0x7f, 0xa3, 0xac);
DEFINE_GUID(XIID_VisualX,                    0x2c4eef28, 0x1bc0, 0x4736, 0xb7, 0xdd, 0xb6, 0x26, 0x92, 0xf9, 0xbd, 0x67);
DEFINE_GUID(XIID_UnknownA,                   0xca67b562, 0x1c32, 0x4017, 0x9d, 0xd9, 0x3d, 0x4b, 0x7e, 0x25, 0x10, 0xaa);
DEFINE_GUID(XIID_UnknownB,                   0x6b556968, 0x2d25, 0x11e2, 0x99, 0xf6, 0x8b, 0xc3, 0x60, 0x88, 0x70, 0x9b);
DEFINE_GUID(XIID_ICompositionTarget,         0xa1bea8ba, 0xd726, 0x4663, 0x81, 0x29, 0x6b, 0x5e, 0x79, 0x27, 0xff, 0xa6);
DEFINE_GUID(XIID_IVisual,                    0x117e202d, 0xa859, 0x4c89, 0x87, 0x3b, 0xc2, 0xaa, 0x56, 0x67, 0x88, 0xe3);
DEFINE_GUID(XIID_IContainerVisual,           0x02f6bc74, 0xed20, 0x4773, 0xaf, 0xe6, 0xd4, 0x9b, 0x4a, 0x93, 0xdb, 0x32);
DEFINE_GUID(XIID_ISpriteVisual,              0x08e05581, 0x1ad1, 0x4f97, 0x97, 0x57, 0x40, 0x2d, 0x76, 0xe4, 0x23, 0x3b);

typedef HRESULT (WINAPI *create_fn)(IUnknown *, REFIID, void **);
typedef HRESULT (WINAPI *d3d11_create_fn)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT,
                                          const D3D_FEATURE_LEVEL *, UINT, UINT, ID3D11Device **,
                                          D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);
#define VT(obj, idx) ((*(void ***)(obj))[idx])
typedef HRESULT (STDMETHODCALLTYPE *mqi)(void *, REFIID, void **);
typedef ULONG (STDMETHODCALLTYPE *mref)(void *);
typedef HRESULT (STDMETHODCALLTYPE *m71)(void *, HWND, void *, UINT_PTR, void *);

static ULONG rel(void *p) { return p ? ((mref)VT(p, 2))(p) : 0; }
static HRESULT qi(void *p, REFIID iid, void **out) { return ((mqi)VT(p, 0))(p, iid, out); }
static void o_hr(const char *k, HRESULT hr) { o_hex32(k, (DWORD)hr); }

static const struct { const char *name; const GUID *iid; } iids[] = {
    { "IUnknown", &XIID_IUnknown },
    { "IMarshal", &XIID_IMarshal },
    { "IAgileObject", &XIID_IAgileObject },
    { "IInspectable", &XIID_IInspectable },
    { "IDCompositionDevice", &XIID_IDCompositionDevice },
    { "IDCompositionDesktopDevice", &XIID_IDCompositionDesktopDevice },
    { "IDCompositionTarget", &XIID_IDCompositionTarget },
    { "IDCompositionVisual", &XIID_IDCompositionVisual },
    { "IDCompositionVisual2", &XIID_IDCompositionVisual2 },
    { "IDCompositionVisual3", &XIID_IDCompositionVisual3 },
    { "IDCompositionSurface", &XIID_IDCompositionSurface },
    { "VisualX", &XIID_VisualX },
    { "A", &XIID_UnknownA },
    { "B", &XIID_UnknownB },
    { "ICompositionTarget", &XIID_ICompositionTarget },
    { "IVisual", &XIID_IVisual },
    { "IContainerVisual", &XIID_IContainerVisual },
    { "ISpriteVisual", &XIID_ISpriteVisual },
};

#define NSLOTS 8
struct call
{
    void *a_iface;
    HWND hwnd;
    UINT_PTR arg3;
    HRESULT hr;
    int raised, qi_raised;
    UINT_PTR a[NSLOTS], b[NSLOTS];
    char qi_a[1024], qi_b[1024];
};
static DWORD call_tid;
static struct call *call_cur;
static int in_qi;

static LONG CALLBACK call_veh(EXCEPTION_POINTERS *ep)
{
    (void)ep;
    if (GetCurrentThreadId() != call_tid) return EXCEPTION_CONTINUE_SEARCH;
    if (in_qi) call_cur->qi_raised = 1;
    else call_cur->raised = 1;
    ExitThread(1);
}

static void qi_table(char *buf, size_t len, void *obj)
{
    for (size_t i = 0; i < sizeof iids / sizeof iids[0]; i++) {
        void *p = (void *)0x1;
        HRESULT hr = qi(obj, iids[i].iid, &p);
        char item[96];
        if (SUCCEEDED(hr)) {
            snprintf(item, sizeof item, "%s%s=%s", *buf ? " " : "", iids[i].name, p == obj ? "self" : "other");
            rel(p);
        } else if (hr != E_NOINTERFACE || p != NULL) {
            snprintf(item, sizeof item, "%s%s=%08lx", *buf ? " " : "", iids[i].name, (unsigned long)hr);
        } else continue;
        strncat(buf, item, len - strlen(buf) - 1);
    }
}

static DWORD WINAPI call_thread(void *arg)
{
    struct call *c = arg;
    call_cur = c;
    in_qi = 0;
    c->hr = ((m71)VT(c->a_iface, 71))(c->a_iface, c->hwnd, c->a, c->arg3, c->b);
    in_qi = 1;
    if (c->a[0]) qi_table(c->qi_a, sizeof c->qi_a, (void *)c->a[0]);
    if (c->b[0]) qi_table(c->qi_b, sizeof c->qi_b, (void *)c->b[0]);
    in_qi = 0;
    return 0;
}

static void o_slots(const char *k, const UINT_PTR *s)
{
    char buf[128] = "";
    for (int i = 0; i < NSLOTS; i++) {
        char item[16];
        snprintf(item, sizeof item, "%s%d", *buf ? "," : "", i);
        if (s[i]) strncat(buf, item, sizeof buf - strlen(buf) - 1);
    }
    o_str(k, *buf ? buf : "none");
}

static void do_case(const char *name, void *a_iface, HWND hwnd, UINT_PTR arg3)
{
    static struct call c;
    HANDLE t;
    PVOID veh;

    memset(&c, 0, sizeof c);
    c.a_iface = a_iface;
    c.hwnd = hwnd;
    c.arg3 = arg3;
    c.hr = 0x7fffffff;
    veh = AddVectoredExceptionHandler(1, call_veh);
    t = CreateThread(NULL, 0, call_thread, &c, 0, &call_tid);
    WaitForSingleObject(t, 10000);
    CloseHandle(t);
    RemoveVectoredExceptionHandler(veh);
    oracle_case(name);
    o_bool("raised", c.raised);
    if (!c.raised) {
        o_hr("hr", c.hr);
        o_slots("a_written", c.a);
        o_slots("b_written", c.b);
        if (c.qi_raised) o_bool("qi_raised", 1);
        if (c.a[0]) o_str("a0_qi", c.qi_a);
        if (c.b[0]) o_str("b0_qi", c.qi_b);
    }
    oracle_case_end();
    /* what the call handed out stays referenced: the test ends soon */
}

static DWORD WINAPI other_thread_window(void *arg)
{
    HWND *w = arg;
    MSG msg;
    *w = CreateWindowExW(0, L"STATIC", L"other", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 300, 0, 200, 150, NULL, NULL, NULL, NULL);
    while (GetMessageW(&msg, NULL, 0, 0)) DispatchMessageW(&msg);
    return 0;
}

static int run_cases(void)
{
    HMODULE dcomp = LoadLibraryW(L"dcomp.dll");
    HMODULE d3d11 = LoadLibraryW(L"d3d11.dll");
    create_fn create3 = dcomp ? (create_fn)(void (*)(void))GetProcAddress(dcomp, "DCompositionCreateDevice3") : NULL;
    d3d11_create_fn pD3D11CreateDevice = d3d11 ? (d3d11_create_fn)(void (*)(void))GetProcAddress(d3d11, "D3D11CreateDevice") : NULL;
    ID3D11Device *d3d = NULL;
    IDXGIDevice *dxgi = NULL;
    void *dev = NULL, *a = NULL;
    HWND w1, w2, w3, child, dead, other = NULL;
    HANDLE ot;
    DWORD otid;
    HRESULT hr = E_FAIL;

    oracle_begin("dcomp/desktop_private");
    if (pD3D11CreateDevice)
        pD3D11CreateDevice(NULL, D3D_DRIVER_TYPE_WARP, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                           NULL, 0, D3D11_SDK_VERSION, &d3d, NULL, NULL);
    if (d3d) ID3D11Device_QueryInterface(d3d, &IID_IDXGIDevice, (void **)&dxgi);
    if (create3 && dxgi) hr = create3((IUnknown *)dxgi, &XIID_IDCompositionDevice, &dev);
    oracle_case("setup");
    o_hr("create", hr);
    if (dev) o_hr("qi_a", qi(dev, &XIID_UnknownA, &a));
    oracle_case_end();
    if (!a) { rel(dev); return oracle_end(); }

    w1 = CreateWindowExW(0, L"STATIC", L"w1", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 200, 150, NULL, NULL, NULL, NULL);
    w2 = CreateWindowExW(0, L"STATIC", L"w2", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 200, 200, 150, NULL, NULL, NULL, NULL);
    w3 = CreateWindowExW(0, L"STATIC", L"w3", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 400, 200, 150, NULL, NULL, NULL, NULL);
    child = CreateWindowExW(0, L"STATIC", L"child", WS_CHILD | WS_VISIBLE, 0, 0, 50, 50, w3, NULL, NULL, NULL);
    dead = CreateWindowExW(0, L"STATIC", L"dead", WS_OVERLAPPEDWINDOW, 0, 0, 50, 50, NULL, NULL, NULL, NULL);
    DestroyWindow(dead);
    ot = CreateThread(NULL, 0, other_thread_window, &other, 0, &otid);
    for (int i = 0; i < 100 && !other; i++) Sleep(20);

    do_case("hwnd_5a", a, w1, 0x5a);
    do_case("hwnd_5a_again", a, w1, 0x5a);
    do_case("hwnd_0", a, w2, 0);
    do_case("hwnd_1", a, w3, 1);
    do_case("child_5a", a, child, 0x5a);
    do_case("null_5a", a, NULL, 0x5a);
    do_case("destroyed_5a", a, dead, 0x5a);
    if (other) do_case("other_thread_5a", a, other, 0x5a);

    if (other) PostMessageW(other, WM_QUIT, 0, 0);
    PostThreadMessageW(otid, WM_QUIT, 0, 0);
    WaitForSingleObject(ot, 2000);
    CloseHandle(ot);
    return oracle_end();
}

int main(void)
{
    return oracle_interactive_main(L"SetuOracleDcompDesktopPrivate", run_cases, oracle_dcomp_denied);
}
