/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: the undocumented parts of Windows.UI.Composition and
 * DirectComposition that the Windows App SDK windowing code
 * (Microsoft.UI.Windowing.Core, which Firefox loads as a Windows 11 client)
 * uses, observed black-box only:
 *  - the Compositor factory's second interface {22118adf-...}, whose
 *    method 6 takes (device, flags, riid, out) and makes a compositor;
 *    that compositor is also a DirectComposition device;
 *  - the device's {6b556968-...} interface, method 3 (Firefox calls it as
 *    (0x24, 0x18, buffer));
 *  - the visual's {2c4eef28-...} interface;
 *  - user32 ordinal 2557 (whether it exists, whether it is a named export,
 *    what it returns for the arguments Firefox passes).
 * Nothing here looks at code: QueryInterface results, pointer identity,
 * HRESULTs and whether a buffer was written are all that is printed.
 *
 * Only HRESULTs, booleans, names and reference counts are printed.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/roapi/nf-roapi-rogetactivationfactory
 *       https://learn.microsoft.com/en-us/windows/win32/api/activation/nf-activation-iactivationfactory-activateinstance
 *       https://learn.microsoft.com/en-us/windows/win32/api/inspectable/nn-inspectable-iinspectable
 *       https://learn.microsoft.com/en-us/uwp/api/windows.ui.composition.compositor
 *       https://learn.microsoft.com/en-us/windows/apps/desktop/modernize/using-the-visual-layer-with-win32
 *       https://learn.microsoft.com/en-us/windows/win32/api/dispatcherqueue/nf-dispatcherqueue-createdispatcherqueuecontroller
 *       https://learn.microsoft.com/en-us/windows/win32/winrt/creating-a-windows-runtime-activatable-class
 */
#define COBJMACROS
#include "oracle.h"
#include "oracle_interactive.h"
#include <initguid.h>
#include <d3d11.h>
#include <winstring.h>

typedef HRESULT (WINAPI *ro_init_fn)(int);
typedef void (WINAPI *ro_uninit_fn)(void);
typedef HRESULT (WINAPI *ro_factory_fn)(HSTRING, REFIID, void **);
typedef HRESULT (WINAPI *ws_create_fn)(const WCHAR *, UINT32, HSTRING *);
typedef HRESULT (WINAPI *ws_delete_fn)(HSTRING);
typedef const WCHAR *(WINAPI *ws_buffer_fn)(HSTRING, UINT32 *);
typedef HRESULT (WINAPI *co_init_fn)(void *, DWORD);
typedef void (WINAPI *co_uninit_fn)(void);
typedef void (WINAPI *co_free_fn)(void *);
typedef struct { DWORD size; int type; int apartment; } dq_options;
typedef HRESULT (WINAPI *dq_create_fn)(dq_options, void **);
typedef HRESULT (WINAPI *create_fn)(IUnknown *, REFIID, void **);
typedef HRESULT (WINAPI *d3d11_create_fn)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT,
                                          const D3D_FEATURE_LEVEL *, UINT, UINT, ID3D11Device **,
                                          D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);

static ro_init_fn pRoInitialize;
static ro_uninit_fn pRoUninitialize __attribute__((unused));
static ro_factory_fn pRoGetActivationFactory;
static ws_create_fn pWindowsCreateString;
static ws_delete_fn pWindowsDeleteString;
static ws_buffer_fn pWindowsGetStringRawBuffer;
static co_init_fn pCoInitializeEx;
static co_uninit_fn pCoUninitialize;
static co_free_fn pCoTaskMemFree;
static dq_create_fn pCreateDispatcherQueueController;
static create_fn pDCompositionCreateDevice3;
static d3d11_create_fn pD3D11CreateDevice;

#define VT(obj, idx) ((*(void ***)(obj))[idx])
typedef HRESULT (STDMETHODCALLTYPE *mqi)(void *, REFIID, void **);
typedef ULONG (STDMETHODCALLTYPE *mref)(void *);
typedef HRESULT (STDMETHODCALLTYPE *m1p)(void *, void *);
typedef HRESULT (STDMETHODCALLTYPE *miids)(void *, ULONG *, GUID **);
static inline ULONG addref(void *p) { return ((mref)VT(p, 1))(p); }
static ULONG rel(void *p) { return p ? ((mref)VT(p, 2))(p) : 0; }
static HRESULT qi(void *p, REFIID iid, void **out) { return ((mqi)VT(p, 0))(p, iid, out); }
static void o_hr(const char *k, HRESULT hr) { o_hex32(k, (DWORD)hr); }

DEFINE_GUID(XIID_IUnknown,            0x00000000, 0x0000, 0x0000, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
DEFINE_GUID(XIID_IMarshal,            0x00000003, 0x0000, 0x0000, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
DEFINE_GUID(XIID_IActivationFactory,  0x00000035, 0x0000, 0x0000, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
DEFINE_GUID(XIID_IWeakReferenceSource,0x00000038, 0x0000, 0x0000, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
DEFINE_GUID(XIID_IInspectable,        0xaf86e2e0, 0xb12d, 0x4c6a, 0x9c, 0x5a, 0xd7, 0xaa, 0x65, 0x10, 0x1e, 0x90);
DEFINE_GUID(XIID_IAgileObject,        0x94ea2b94, 0xe9cc, 0x49e0, 0xc0, 0xff, 0xee, 0x64, 0xca, 0x8f, 0x5b, 0x90);
DEFINE_GUID(XIID_IClosable,           0x30d5a829, 0x7fa4, 0x4026, 0x83, 0xbb, 0xd7, 0x5b, 0xae, 0x4e, 0xa9, 0x9e);
DEFINE_GUID(XIID_ICompositor,         0xb403ca50, 0x7f8c, 0x4e83, 0x98, 0x5f, 0xcc, 0x45, 0x06, 0x00, 0x36, 0xd8);
DEFINE_GUID(XIID_ICompositionObject,  0xbcb4ad45, 0x7609, 0x4550, 0x93, 0x4f, 0x16, 0x00, 0x2a, 0x68, 0xfd, 0xed);
DEFINE_GUID(XIID_ICompositorInterop,  0x25297d5c, 0x3ad4, 0x4c9c, 0xb5, 0xcf, 0xe3, 0x6a, 0x38, 0x51, 0x23, 0x30);
DEFINE_GUID(XIID_IContainerVisual,    0x02f6bc74, 0xed20, 0x4773, 0xaf, 0xe6, 0xd4, 0x9b, 0x4a, 0x93, 0xdb, 0x32);
DEFINE_GUID(XIID_IDCompositionDevice, 0xc37ea93a, 0xe7aa, 0x450d, 0xb1, 0x6f, 0x97, 0x46, 0xcb, 0x04, 0x07, 0xf3);
DEFINE_GUID(XIID_IDCompositionDevice2,0x75f6468d, 0x1b8e, 0x447c, 0x9b, 0xc6, 0x75, 0xfe, 0xa8, 0x0b, 0x5b, 0x25);
DEFINE_GUID(XIID_IDCompositionDesktopDevice, 0x5f4633fe, 0x1e08, 0x4cb8, 0x8c, 0x75, 0xce, 0x24, 0x33, 0x3f, 0x56, 0x02);
DEFINE_GUID(XIID_IDCompositionDevice3,0x0987cb06, 0xf916, 0x48bf, 0x8d, 0x35, 0xce, 0x76, 0x41, 0x78, 0x1b, 0xd9);
/* the two interfaces Microsoft.UI.Windowing.Core asks a dcomp device for
 * (seen in Setu's own debug output); no public name or documentation */
DEFINE_GUID(XIID_UnknownA,            0xca67b562, 0x1c32, 0x4017, 0x9d, 0xd9, 0x3d, 0x4b, 0x7e, 0x25, 0x10, 0xaa);
DEFINE_GUID(XIID_UnknownB,            0x6b556968, 0x2d25, 0x11e2, 0x99, 0xf6, 0x8b, 0xc3, 0x60, 0x88, 0x70, 0x9b);

static const struct { const char *n; const GUID *g; } iids[] = {
    { "IUnknown", &XIID_IUnknown },
    { "IInspectable", &XIID_IInspectable },
    { "IAgileObject", &XIID_IAgileObject },
    { "IMarshal", &XIID_IMarshal },
    { "IWeakReferenceSource", &XIID_IWeakReferenceSource },
    { "IActivationFactory", &XIID_IActivationFactory },
    { "IClosable", &XIID_IClosable },
    { "ICompositor", &XIID_ICompositor },
    { "ICompositionObject", &XIID_ICompositionObject },
    { "ICompositorInterop", &XIID_ICompositorInterop },
    { "IContainerVisual", &XIID_IContainerVisual },
    { "IDCompositionDevice", &XIID_IDCompositionDevice },
    { "IDCompositionDevice2", &XIID_IDCompositionDevice2 },
    { "IDCompositionDesktopDevice", &XIID_IDCompositionDesktopDevice },
    { "IDCompositionDevice3", &XIID_IDCompositionDevice3 },
    { "UnknownA", &XIID_UnknownA },
    { "UnknownB", &XIID_UnknownB },
};

static HSTRING hs(const WCHAR *s)
{
    HSTRING h = NULL;
    pWindowsCreateString(s, (UINT32)wcslen(s), &h);
    return h;
}

static void o_hstring(const char *k, HSTRING h)
{
    UINT32 len = 0;
    const WCHAR *s = h ? pWindowsGetStringRawBuffer(h, &len) : NULL;
    o_wstr(k, s);
}

static void o_guid(const char *k, const GUID *g)
{
    char buf[64];
    snprintf(buf, sizeof buf, "%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             (unsigned long)g->Data1, g->Data2, g->Data3, g->Data4[0], g->Data4[1], g->Data4[2],
             g->Data4[3], g->Data4[4], g->Data4[5], g->Data4[6], g->Data4[7]);
    o_str(k, buf);
}

static int same_object(void *a, void *b)
{
    void *ua = NULL, *ub = NULL;
    int r;
    if (FAILED(qi(a, &XIID_IUnknown, &ua))) return 0;
    if (FAILED(qi(b, &XIID_IUnknown, &ub))) { rel(ua); return 0; }
    r = ua == ub;
    rel(ua);
    rel(ub);
    return r;
}

/* QueryInterface table: hr per IID, plus whether the result is the same
 * pointer as the object and the same object (IUnknown identity) */
static void qi_table(const char *prefix, void *obj)
{
    char k[128];
    for (size_t i = 0; i < sizeof(iids) / sizeof(iids[0]); i++) {
        void *p = (void *)0xdeadbeef;
        HRESULT hr = qi(obj, iids[i].g, &p);
        snprintf(k, sizeof k, "%s%s", prefix, iids[i].n);
        o_hr(k, hr);
        if (SUCCEEDED(hr) && p) {
            snprintf(k, sizeof k, "%s%s_same_ptr", prefix, iids[i].n);
            o_bool(k, p == obj);
            snprintf(k, sizeof k, "%s%s_same_object", prefix, iids[i].n);
            o_bool(k, same_object(p, obj));
            rel(p);
        } else {
            snprintf(k, sizeof k, "%s%s_out_null", prefix, iids[i].n);
            o_bool(k, p == NULL);
        }
    }
}

static void inspectable_info(const char *prefix, void *obj)
{
    void *insp = NULL;
    char k[128];
    if (FAILED(qi(obj, &XIID_IInspectable, &insp))) return;
    {
        HSTRING name = NULL;
        HRESULT hr = ((m1p)VT(insp, 4))(insp, &name);
        snprintf(k, sizeof k, "%sclass_name_hr", prefix);
        o_hr(k, hr);
        snprintf(k, sizeof k, "%sclass_name", prefix);
        o_hstring(k, name);
        if (name) pWindowsDeleteString(name);
    }
    {
        int level = -1;
        HRESULT hr = ((m1p)VT(insp, 5))(insp, &level);
        snprintf(k, sizeof k, "%strust_hr", prefix);
        o_hr(k, hr);
        snprintf(k, sizeof k, "%strust", prefix);
        o_i64(k, level);
    }
    {
        ULONG n = 0;
        GUID *list = NULL;
        HRESULT hr = ((miids)VT(insp, 3))(insp, &n, &list);
        snprintf(k, sizeof k, "%siids_hr", prefix);
        o_hr(k, hr);
        snprintf(k, sizeof k, "%siids_count", prefix);
        o_u32(k, n);
        for (ULONG i = 0; SUCCEEDED(hr) && list && i < n; i++) {
            snprintf(k, sizeof k, "%siid_%lu", prefix, (unsigned long)i);
            o_guid(k, &list[i]);
        }
        if (list) pCoTaskMemFree(list);
    }
    rel(insp);
}

enum { T_NONE, T_MTA, T_STA, T_MTA_DQ, T_STA_DQ };
DEFINE_GUID(XIID_Statics22,           0x22118adf, 0x23f1, 0x4801, 0xbc, 0xfa, 0x66, 0xcb, 0xf4, 0x8c, 0xc5, 0x1b);
DEFINE_GUID(XIID_VisualX,             0x2c4eef28, 0x1bc0, 0x4736, 0xb7, 0xdd, 0xb6, 0x26, 0x92, 0xf9, 0xbd, 0x67);
DEFINE_GUID(XIID_Bogus,               0x12345678, 0x1234, 0x1234, 0x12, 0x34, 0x12, 0x34, 0x12, 0x34, 0x12, 0x34);
typedef HRESULT (STDMETHODCALLTYPE *m6_fn)(void *, void *, UINT_PTR, const GUID *, void **);
typedef HRESULT (STDMETHODCALLTYPE *m3_fn)(void *, UINT_PTR, UINT_PTR, void *);
typedef UINT_PTR (WINAPI *ord_fn)(void *, UINT_PTR, UINT_PTR, UINT_PTR);
typedef HRESULT (STDMETHODCALLTYPE *mcv_fn)(void *, void **);

static void o_buf(const char *k, const unsigned char *b, int n)
{
    char s[600];
    int i, m = 0;
    for (i = 0; i < n; i++) m += sprintf(s + m, "%02x", b[i]);
    o_str(k, s);
}

struct st_result { int kind; HRESULT init, dq, qi, create, create2; int distinct; char cls[100]; };

static DWORD WINAPI statics_thread(void *arg)
{
    struct st_result *r = arg;
    void *factory = NULL, *dqc = NULL, *st = NULL, *c1 = NULL, *c2 = NULL;
    HSTRING cls = hs(L"Windows.UI.Composition.Compositor");
    r->init = r->dq = r->qi = r->create = r->create2 = 0x7fffffff;
    if (r->kind == T_MTA || r->kind == T_MTA_DQ) r->init = pRoInitialize(1);
    if (r->kind == T_STA || r->kind == T_STA_DQ) r->init = pCoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (r->kind == T_MTA_DQ || r->kind == T_STA_DQ) {
        dq_options o = { sizeof(dq_options), 2, 0 };
        r->dq = pCreateDispatcherQueueController(o, &dqc);
    }
    if (SUCCEEDED(pRoGetActivationFactory(cls, &XIID_IActivationFactory, &factory)) && factory) {
        r->qi = qi(factory, &XIID_Statics22, &st);
        if (SUCCEEDED(r->qi) && st) {
            r->create = ((m6_fn)VT(st, 6))(st, NULL, 0, &XIID_ICompositor, &c1);
            r->create2 = ((m6_fn)VT(st, 6))(st, NULL, 0, &XIID_ICompositor, &c2);
            r->distinct = c1 && c2 && c1 != c2;
            if (c1) {
                void *insp = NULL;
                if (SUCCEEDED(qi(c1, &XIID_IInspectable, &insp))) {
                    HSTRING name = NULL;
                    ((m1p)VT(insp, 4))(insp, &name);
                    if (name) {
                        snprintf(r->cls, sizeof r->cls, "%s", oracle_utf8(pWindowsGetStringRawBuffer(name, NULL)));
                        pWindowsDeleteString(name);
                    }
                    rel(insp);
                }
            }
            rel(c1);
            rel(c2);
            rel(st);
        }
        rel(factory);
    }
    rel(dqc);
    pWindowsDeleteString(cls);
    return 0;
}

static void statics_cases(void)
{
    static const struct { int k; const char *n; } kinds[] = {
        { T_NONE, "no_com" }, { T_MTA, "mta" }, { T_STA, "sta" }, { T_MTA_DQ, "mta_queue" }, { T_STA_DQ, "sta_queue" } };
    for (size_t i = 0; i < 5; i++) {
        struct st_result r;
        char k[64];
        HANDLE t;
        memset(&r, 0, sizeof r);
        r.kind = kinds[i].k;
        t = CreateThread(NULL, 0, statics_thread, &r, 0, NULL);
        WaitForSingleObject(t, 30000);
        CloseHandle(t);
        snprintf(k, sizeof k, "statics_%s", kinds[i].n);
        oracle_case(k);
        o_hr("qi", r.qi);
        o_hr("create", r.create);
        o_hr("create2", r.create2);
        o_bool("distinct", r.distinct);
        o_str("class", r.cls);
        oracle_case_end();
    }
}

static void statics_detail(void)
{
    void *dqc = NULL, *factory = NULL, *st = NULL, *c = NULL;
    dq_options o = { sizeof(dq_options), 2, 0 };
    HSTRING cls = hs(L"Windows.UI.Composition.Compositor");
    HRESULT hr;
    pRoInitialize(0);
    pCreateDispatcherQueueController(o, &dqc);
    pRoGetActivationFactory(cls, &XIID_IActivationFactory, &factory);
    if (!factory) return;
    hr = qi(factory, &XIID_Statics22, &st);
    oracle_case("statics_qi");
    o_hr("hr", hr);
    if (st) {
        o_bool("same_ptr", st == factory);
        o_bool("same_object", same_object(st, factory));
        qi_table("qi_", st);
        inspectable_info("", st);
    }
    oracle_case_end();
    if (!st) return;
    {
        static const struct { const char *n; void *a; UINT_PTR b; const GUID *g; } v[] = {
            { "null_0_compositor", NULL, 0, &XIID_ICompositor },
            { "null_1_compositor", NULL, 1, &XIID_ICompositor },
            { "null_2_compositor", NULL, 2, &XIID_ICompositor },
            { "null_0_iunknown", NULL, 0, &XIID_IUnknown },
            { "null_0_iinspectable", NULL, 0, &XIID_IInspectable },
            { "null_0_interop", NULL, 0, &XIID_ICompositorInterop },
            { "null_0_bogus", NULL, 0, &XIID_Bogus },
            { "null_0_statics", NULL, 0, &XIID_Statics22 },
        };
        for (size_t i = 0; i < sizeof(v) / sizeof(v[0]); i++) {
            char k[80];
            void *out = (void *)0xdeadbeef;
            snprintf(k, sizeof k, "statics_create_%s", v[i].n);
            oracle_case(k);
            hr = ((m6_fn)VT(st, 6))(st, v[i].a, v[i].b, v[i].g, &out);
            o_hr("hr", hr);
            o_bool("out_null", out == NULL);
            o_bool("out_unchanged", out == (void *)0xdeadbeef);
            if (SUCCEEDED(hr) && out && out != (void *)0xdeadbeef) {
                inspectable_info("", out);
                rel(out);
            }
            oracle_case_end();
        }
        /* the first argument: a device? */
        {
            ID3D11Device *d3d = NULL;
            void *dev = NULL, *out = NULL;
            if (pD3D11CreateDevice) pD3D11CreateDevice(NULL, D3D_DRIVER_TYPE_WARP, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                                       NULL, 0, D3D11_SDK_VERSION, &d3d, NULL, NULL);
            if (d3d) pDCompositionCreateDevice3((IUnknown *)d3d, &XIID_IDCompositionDevice, &dev);
            oracle_case("statics_create_dcomp_device");
            hr = dev ? ((m6_fn)VT(st, 6))(st, dev, 0, &XIID_ICompositor, &out) : E_FAIL;
            o_hr("hr", hr);
            if (out) { inspectable_info("", out); rel(out); }
            oracle_case_end();
            oracle_case("statics_create_d3d_device");
            out = NULL;
            hr = d3d ? ((m6_fn)VT(st, 6))(st, d3d, 0, &XIID_ICompositor, &out) : E_FAIL;
            o_hr("hr", hr);
            if (out) { inspectable_info("", out); rel(out); }
            oracle_case_end();
            rel(dev);
            if (d3d) ID3D11Device_Release(d3d);
        }
        ((m6_fn)VT(st, 6))(st, NULL, 0, &XIID_ICompositor, &c);
    }
    if (c) {
        oracle_case("statics_compositor");
        qi_table("qi_", c);
        {
            void *p = NULL;
            o_hr("qi_Statics22", qi(c, &XIID_Statics22, &p));
            rel(p);
        }
        inspectable_info("", c);
        oracle_case_end();
        rel(c);
    }
    rel(st);
    rel(factory);
    rel(dqc);
}

static void device_b_cases(void)
{
    ID3D11Device *d3d = NULL;
    void *dev = NULL, *b = NULL, *vis = NULL, *dev3 = NULL;
    unsigned char buf[256];
    HRESULT hr;
    if (pD3D11CreateDevice) pD3D11CreateDevice(NULL, D3D_DRIVER_TYPE_WARP, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                               NULL, 0, D3D11_SDK_VERSION, &d3d, NULL, NULL);
    pDCompositionCreateDevice3((IUnknown *)d3d, &XIID_IDCompositionDevice, &dev);
    if (!dev) return;
    qi(dev, &XIID_UnknownB, &b);
    if (b) {
        static const struct { UINT_PTR id, size; } v[] = {
            { 0x24, 0x18 }, { 0x24, 0x10 }, { 0x24, 0x20 }, { 0x24, 0 }, { 0x24, 0x17 }, { 0x24, 0x19 },
        };
        for (size_t i = 0; i < sizeof(v) / sizeof(v[0]); i++) {
            char k[64];
            memset(buf, 0xcc, sizeof buf);
            snprintf(k, sizeof k, "b_slot3_%02x_%02x", (unsigned)v[i].id, (unsigned)v[i].size);
            oracle_case(k);
            hr = ((m3_fn)VT(b, 3))(b, v[i].id, v[i].size, buf);
            o_hr("hr", hr);
            o_buf("buf", buf, 40);
            oracle_case_end();
        }
        oracle_case("b_slot3_ids");
        for (UINT_PTR id = 0; id < 0x40; id++) {
            char k[32];
            memset(buf, 0xcc, sizeof buf);
            hr = ((m3_fn)VT(b, 3))(b, id, 0x18, buf);
            snprintf(k, sizeof k, "id_%02x", (unsigned)id);
            o_hr(k, hr);
            if (SUCCEEDED(hr)) {
                snprintf(k, sizeof k, "buf_%02x", (unsigned)id);
                o_buf(k, buf, 32);
            }
        }
        oracle_case_end();
        rel(b);
    }
    /* B on version 1 and 2 devices */
    {
        HMODULE m = GetModuleHandleW(L"dcomp.dll");
        create_fn c1 = (create_fn)(void (*)(void))GetProcAddress(m, "DCompositionCreateDevice");
        create_fn c2 = (create_fn)(void (*)(void))GetProcAddress(m, "DCompositionCreateDevice2");
        void *d = NULL, *p = NULL;
        IDXGIDevice *dxgi = NULL;
        if (d3d) ID3D11Device_QueryInterface(d3d, &IID_IDXGIDevice, (void **)&dxgi);
        oracle_case("b_versions");
        if (dxgi && SUCCEEDED(c1((IUnknown *)dxgi, &XIID_IDCompositionDevice, &d)) && d) {
            o_hr("v1_A", qi(d, &XIID_UnknownA, &p)); rel(p); p = NULL;
            o_hr("v1_B", qi(d, &XIID_UnknownB, &p)); rel(p); p = NULL;
            rel(d); d = NULL;
        }
        if (SUCCEEDED(c2(NULL, &XIID_IDCompositionDesktopDevice, &d)) && d) {
            o_hr("v2_A", qi(d, &XIID_UnknownA, &p)); rel(p); p = NULL;
            o_hr("v2_B", qi(d, &XIID_UnknownB, &p)); rel(p); p = NULL;
            rel(d); d = NULL;
        }
        oracle_case_end();
        if (dxgi) IDXGIDevice_Release(dxgi);
    }
    /* the visual interface */
    if (SUCCEEDED(qi(dev, &XIID_IDCompositionDevice3, &dev3)) && dev3) {
        /* IDCompositionDevice3::CreateVisual is slot 6 (after Commit, WaitForCommitCompletion, GetFrameStatistics) */
        hr = ((mcv_fn)VT(dev3, 6))(dev3, &vis);
        oracle_case("visual_x");
        o_hr("create", hr);
        if (vis) {
            void *p = NULL;
            hr = qi(vis, &XIID_VisualX, &p);
            o_hr("qi", hr);
            if (p) {
                o_bool("same_ptr", p == vis);
                o_bool("same_object", same_object(p, vis));
                qi_table("x_qi_", p);
                rel(p);
            }
            rel(vis);
        }
        oracle_case_end();
        rel(dev3);
    }
    rel(dev);
    if (d3d) ID3D11Device_Release(d3d);
}

/* the names Wine's user32.spec exports */
#include "user32_names.inc"
/* the call runs on its own thread: where the ordinal raises (a stub), the
 * handler ends that thread and the case records it */
struct ord_call { FARPROC f; UINT_PTR ret; DWORD err; int raised; unsigned char buf[256]; };
static DWORD ord_tid;
static struct ord_call *ord_cur;
static LONG CALLBACK ord_veh(EXCEPTION_POINTERS *ep)
{
    (void)ep;
    if (GetCurrentThreadId() != ord_tid) return EXCEPTION_CONTINUE_SEARCH;
    ord_cur->raised = 1;
    ExitThread(1);
}
static DWORD WINAPI ord_thread(void *arg)
{
    struct ord_call *c = arg;
    ord_cur = c;
    SetLastError(0xdeadbeef);
    c->ret = ((ord_fn)(void (*)(void))c->f)(c->buf, 0x12, (UINT_PTR)-4, 0x30001);
    c->err = GetLastError();
    return 0;
}

static void ord2557_cases(void)
{
    HMODULE u = GetModuleHandleW(L"user32.dll");
    FARPROC f = GetProcAddress(u, (LPCSTR)(ULONG_PTR)2557);
    oracle_case("ord2557");
    o_bool("exists", f != NULL);
    for (size_t i = 0; f && i < sizeof(user32_names) / sizeof(user32_names[0]); i++)
        if (GetProcAddress(u, user32_names[i]) == f) o_str("name", user32_names[i]);
    for (int o = 2550; o < 2565; o++) {
        FARPROC g = GetProcAddress(u, (LPCSTR)(ULONG_PTR)o);
        char k[32];
        snprintf(k, sizeof k, "ord_%d", o);
        o_bool(k, g != NULL);
        for (size_t i = 0; g && i < sizeof(user32_names) / sizeof(user32_names[0]); i++)
            if (GetProcAddress(u, user32_names[i]) == g) { snprintf(k, sizeof k, "ord_%d_name", o); o_str(k, user32_names[i]); }
    }
    oracle_case_end();
    if (!f) return;
    {
        struct ord_call c;
        HANDLE t;
        PVOID veh;
        memset(&c, 0, sizeof c);
        memset(c.buf, 0xcc, sizeof c.buf);
        c.f = f;
        veh = AddVectoredExceptionHandler(1, ord_veh);
        t = CreateThread(NULL, 0, ord_thread, &c, 0, &ord_tid);
        WaitForSingleObject(t, 10000);
        CloseHandle(t);
        RemoveVectoredExceptionHandler(veh);
        oracle_case("ord2557_call");
        o_bool("raised", c.raised);
        if (!c.raised) {
            o_u64("ret", c.ret);
            o_u64("last_error", c.err);
            o_buf("buf", c.buf, 48);
        }
        oracle_case_end();
    }
}

static int run_cases(void)
{
    HMODULE combase = LoadLibraryW(L"combase.dll");
    HMODULE cm = LoadLibraryW(L"CoreMessaging.dll");
    HMODULE dcomp = LoadLibraryW(L"dcomp.dll");
    HMODULE d3d11 = LoadLibraryW(L"d3d11.dll");
    LoadLibraryW(L"user32.dll");

    oracle_begin("combase/composition_private");
    pRoInitialize = (ro_init_fn)(void (*)(void))GetProcAddress(combase, "RoInitialize");
    pRoGetActivationFactory = (ro_factory_fn)(void (*)(void))GetProcAddress(combase, "RoGetActivationFactory");
    pWindowsCreateString = (ws_create_fn)(void (*)(void))GetProcAddress(combase, "WindowsCreateString");
    pWindowsDeleteString = (ws_delete_fn)(void (*)(void))GetProcAddress(combase, "WindowsDeleteString");
    pWindowsGetStringRawBuffer = (ws_buffer_fn)(void (*)(void))GetProcAddress(combase, "WindowsGetStringRawBuffer");
    pCoInitializeEx = (co_init_fn)(void (*)(void))GetProcAddress(combase, "CoInitializeEx");
    pCoUninitialize = (co_uninit_fn)(void (*)(void))GetProcAddress(combase, "CoUninitialize");
    pCoTaskMemFree = (co_free_fn)(void (*)(void))GetProcAddress(combase, "CoTaskMemFree");
    pCreateDispatcherQueueController = (dq_create_fn)(void (*)(void))GetProcAddress(cm, "CreateDispatcherQueueController");
    pDCompositionCreateDevice3 = (create_fn)(void (*)(void))GetProcAddress(dcomp, "DCompositionCreateDevice3");
    pD3D11CreateDevice = (d3d11_create_fn)(void (*)(void))GetProcAddress(d3d11, "D3D11CreateDevice");

    statics_cases();
    statics_detail();
    device_b_cases();
    ord2557_cases();
    return oracle_end();
}

int main(void)
{
    return oracle_interactive_main(L"SetuOracleCompositionPrivate", run_cases, oracle_dcomp_denied);
}
