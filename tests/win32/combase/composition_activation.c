/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: activating Windows.UI.Composition.Compositor.
 * Where the class is registered (the public WindowsRuntime
 * ActivatableClassId keys: the DLL's file name only, the activation type
 * and threading model), RoGetActivationFactory and ActivateInstance on
 * threads without COM, in the MTA and an STA, with and without a
 * DispatcherQueue; the factory's and the compositor's QueryInterface
 * tables, their runtime class names and trust levels, and what a
 * DirectComposition device answers for the interfaces a compositor
 * client asks it for.
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
static ro_uninit_fn pRoUninitialize;
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
static ULONG addref(void *p) { return ((mref)VT(p, 1))(p); }
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

/* ---- the activation registry -------------------------------------------- */

static void registry_case(const WCHAR *cls, const char *name)
{
    WCHAR path[512], buf[MAX_PATH];
    HKEY key;
    LONG r;
    char k[64];
    swprintf(path, 512, L"SOFTWARE\\Microsoft\\WindowsRuntime\\ActivatableClassId\\%ls", cls);
    snprintf(k, sizeof k, "reg_%s", name);
    oracle_case(k);
    r = RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &key);
    o_bool("exists", r == ERROR_SUCCESS);
    if (r == ERROR_SUCCESS) {
        DWORD type, size = sizeof buf, v;
        if (!RegQueryValueExW(key, L"DllPath", NULL, &type, (BYTE *)buf, &size)) {
            WCHAR *base = wcsrchr(buf, L'\\');
            base = base ? base + 1 : buf;
            CharLowerW(base);
            o_wstr("dll", base);
        } else o_str("dll", NULL);
        size = sizeof v;
        if (!RegQueryValueExW(key, L"ActivationType", NULL, &type, (BYTE *)&v, &size)) o_u32("activation_type", v);
        size = sizeof v;
        if (!RegQueryValueExW(key, L"Threading", NULL, &type, (BYTE *)&v, &size)) o_u32("threading", v);
        size = sizeof v;
        if (!RegQueryValueExW(key, L"TrustLevel", NULL, &type, (BYTE *)&v, &size)) o_u32("trust_level", v);
        RegCloseKey(key);
    }
    oracle_case_end();
}

/* ---- activation on different threads ------------------------------------- */

enum { T_NONE, T_MTA, T_STA, T_MTA_DQ, T_STA_DQ };
struct act_result {
    int kind;
    HRESULT init, dq, factory, instance, instance2;
    int distinct;
};

static DWORD WINAPI act_thread(void *arg)
{
    struct act_result *r = arg;
    void *factory = NULL, *dqc = NULL, *comp = NULL, *comp2 = NULL;
    HSTRING cls = hs(L"Windows.UI.Composition.Compositor");
    r->init = r->dq = r->factory = r->instance = r->instance2 = 0x7fffffff;
    if (r->kind == T_MTA || r->kind == T_MTA_DQ) r->init = pRoInitialize(1);
    if (r->kind == T_STA || r->kind == T_STA_DQ) r->init = pCoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (r->kind == T_MTA_DQ || r->kind == T_STA_DQ) {
        dq_options o = { sizeof(dq_options), 2 /* DQTYPE_THREAD_CURRENT */, 0 /* DQTAT_COM_NONE */ };
        r->dq = pCreateDispatcherQueueController ? pCreateDispatcherQueueController(o, &dqc) : E_NOTIMPL;
    }
    r->factory = pRoGetActivationFactory(cls, &XIID_IActivationFactory, &factory);
    if (SUCCEEDED(r->factory) && factory) {
        r->instance = ((m1p)VT(factory, 6))(factory, &comp);
        r->instance2 = ((m1p)VT(factory, 6))(factory, &comp2);
        r->distinct = comp && comp2 && comp != comp2;
        rel(comp2);
        rel(comp);
        rel(factory);
    }
    rel(dqc);
    pWindowsDeleteString(cls);
    if (r->kind != T_NONE && SUCCEEDED(r->init)) pCoUninitialize();
    return 0;
}

static void activation_case(int kind, const char *name)
{
    struct act_result r;
    HANDLE t;
    char k[64];
    memset(&r, 0, sizeof r);
    r.kind = kind;
    t = CreateThread(NULL, 0, act_thread, &r, 0, NULL);
    WaitForSingleObject(t, 30000);
    CloseHandle(t);
    snprintf(k, sizeof k, "activate_%s", name);
    oracle_case(k);
    if (kind != T_NONE) o_hr("init", r.init);
    if (kind == T_MTA_DQ || kind == T_STA_DQ) o_hr("dispatcher_queue", r.dq);
    o_hr("factory", r.factory);
    o_hr("instance", r.instance);
    o_hr("instance2", r.instance2);
    o_bool("distinct", r.distinct);
    oracle_case_end();
}

/* ---- the factory and the compositor -------------------------------------- */

static void compositor_cases(void)
{
    void *dqc = NULL, *factory = NULL, *comp = NULL;
    dq_options o = { sizeof(dq_options), 2, 0 };
    HSTRING cls = hs(L"Windows.UI.Composition.Compositor");
    HRESULT hr;

    pRoInitialize(0);
    if (pCreateDispatcherQueueController) pCreateDispatcherQueueController(o, &dqc);

    hr = pRoGetActivationFactory(cls, &XIID_IActivationFactory, &factory);
    oracle_case("factory");
    o_hr("hr", hr);
    if (SUCCEEDED(hr) && factory) {
        qi_table("qi_", factory);
        inspectable_info("", factory);
    }
    oracle_case_end();

    if (factory) {
        void *f2 = NULL;
        HRESULT hr2 = pRoGetActivationFactory(cls, &XIID_IActivationFactory, &f2);
        oracle_case("factory_again");
        o_hr("hr", hr2);
        o_bool("same_ptr", f2 == factory);
        rel(f2);
        oracle_case_end();

        hr = ((m1p)VT(factory, 6))(factory, &comp);
        oracle_case("compositor");
        o_hr("hr", hr);
        if (SUCCEEDED(hr) && comp) {
            o_u32("addref", addref(comp));
            o_u32("release", rel(comp));
            qi_table("qi_", comp);
            inspectable_info("", comp);
        }
        oracle_case_end();
    }

    /* the interop interface's own QueryInterface */
    if (comp) {
        void *interop = NULL;
        hr = qi(comp, &XIID_ICompositorInterop, &interop);
        oracle_case("compositor_interop");
        o_hr("hr", hr);
        if (SUCCEEDED(hr) && interop) {
            o_bool("same_ptr", interop == comp);
            qi_table("qi_", interop);
            rel(interop);
        }
        oracle_case_end();
    }

    /* a compositor created on this thread: QI of ICompositor to unknown A/B */
    rel(comp);
    rel(factory);
    pWindowsDeleteString(cls);
    rel(dqc);
}

/* ---- the DirectComposition device ---------------------------------------- */

static void device_cases(void)
{
    ID3D11Device *d3d = NULL;
    void *dev = NULL;
    HRESULT hr;
    if (pD3D11CreateDevice) {
        hr = pD3D11CreateDevice(NULL, D3D_DRIVER_TYPE_WARP, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                NULL, 0, D3D11_SDK_VERSION, &d3d, NULL, NULL);
        if (FAILED(hr)) d3d = NULL;
    }
    for (int arg = 0; arg < 2; arg++) {
        const GUID *ask[2] = { &XIID_IDCompositionDevice, &XIID_IDCompositionDevice3 };
        for (int a = 0; a < 2; a++) {
            char k[64];
            dev = NULL;
            if (arg && !d3d) continue;
            hr = pDCompositionCreateDevice3(arg ? (IUnknown *)d3d : NULL, ask[a], &dev);
            snprintf(k, sizeof k, "device3_%s_%s", arg ? "d3d11" : "null", a ? "device3" : "device");
            oracle_case(k);
            o_hr("hr", hr);
            if (SUCCEEDED(hr) && dev) {
                qi_table("qi_", dev);
                for (int u = 0; u < 2; u++) {
                    void *p = NULL;
                    const char *un = u ? "B_" : "A_";
                    char kk[64];
                    if (FAILED(qi(dev, u ? &XIID_UnknownB : &XIID_UnknownA, &p)) || !p) continue;
                    snprintf(kk, sizeof kk, "%saddref", un);
                    o_u32(kk, addref(p));
                    snprintf(kk, sizeof kk, "%srelease", un);
                    o_u32(kk, rel(p));
                    snprintf(kk, sizeof kk, "%sqi_", un);
                    qi_table(kk, p);
                    rel(p);
                }
                inspectable_info("", dev);
                rel(dev);
            }
            oracle_case_end();
        }
    }
    if (d3d) ID3D11Device_Release(d3d);
}

static int run_cases(void)
{
    HMODULE combase = LoadLibraryW(L"combase.dll");
    HMODULE cm = LoadLibraryW(L"CoreMessaging.dll");
    HMODULE dcomp = LoadLibraryW(L"dcomp.dll");
    HMODULE d3d11 = LoadLibraryW(L"d3d11.dll");

    oracle_begin("combase/composition_activation");
    pRoInitialize = (ro_init_fn)(void (*)(void))GetProcAddress(combase, "RoInitialize");
    pRoUninitialize = (ro_uninit_fn)(void (*)(void))GetProcAddress(combase, "RoUninitialize");
    pRoGetActivationFactory = (ro_factory_fn)(void (*)(void))GetProcAddress(combase, "RoGetActivationFactory");
    pWindowsCreateString = (ws_create_fn)(void (*)(void))GetProcAddress(combase, "WindowsCreateString");
    pWindowsDeleteString = (ws_delete_fn)(void (*)(void))GetProcAddress(combase, "WindowsDeleteString");
    pWindowsGetStringRawBuffer = (ws_buffer_fn)(void (*)(void))GetProcAddress(combase, "WindowsGetStringRawBuffer");
    pCoInitializeEx = (co_init_fn)(void (*)(void))GetProcAddress(combase, "CoInitializeEx");
    pCoUninitialize = (co_uninit_fn)(void (*)(void))GetProcAddress(combase, "CoUninitialize");
    pCoTaskMemFree = (co_free_fn)(void (*)(void))GetProcAddress(combase, "CoTaskMemFree");
    pCreateDispatcherQueueController = cm ? (dq_create_fn)(void (*)(void))GetProcAddress(cm, "CreateDispatcherQueueController") : NULL;
    pDCompositionCreateDevice3 = dcomp ? (create_fn)(void (*)(void))GetProcAddress(dcomp, "DCompositionCreateDevice3") : NULL;
    pD3D11CreateDevice = d3d11 ? (d3d11_create_fn)(void (*)(void))GetProcAddress(d3d11, "D3D11CreateDevice") : NULL;

    oracle_case("load");
    o_bool("combase", pRoGetActivationFactory && pWindowsCreateString);
    o_bool("dispatcher_queue", pCreateDispatcherQueueController != NULL);
    o_bool("dcomp", pDCompositionCreateDevice3 != NULL);
    oracle_case_end();
    if (!pRoGetActivationFactory || !pWindowsCreateString) return oracle_end();

    registry_case(L"Windows.UI.Composition.Compositor", "compositor");
    registry_case(L"Windows.UI.Composition.Core.CompositorController", "compositor_controller");
    registry_case(L"Windows.UI.Composition.CompositionEffectSourceParameter", "effect_source_parameter");
    registry_case(L"Windows.UI.Composition.Desktop.DesktopWindowTarget", "desktop_window_target");
    registry_case(L"Windows.UI.Composition.ContainerVisual", "container_visual");
    registry_case(L"Windows.UI.ViewManagement.UISettings", "uisettings");

    activation_case(T_NONE, "no_com");
    activation_case(T_MTA, "mta");
    activation_case(T_STA, "sta");
    activation_case(T_MTA_DQ, "mta_queue");
    activation_case(T_STA_DQ, "sta_queue");

    compositor_cases();
    if (pDCompositionCreateDevice3) device_cases();
    return oracle_end();
}

int main(void)
{
    return oracle_interactive_main(L"SetuOracleCompositionActivation", run_cases, oracle_dcomp_denied);
}
