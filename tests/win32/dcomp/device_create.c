/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: DirectComposition device creation (DCompositionCreateDevice,
 * DCompositionCreateDevice2, DCompositionCreateDevice3) with no rendering
 * device, a D3D11 device, its IDXGIDevice and objects that are not
 * rendering devices; which interface IDs each accepts; the QueryInterface
 * table of the devices; and the device methods that need no window
 * (Commit, WaitForCommitCompletion, CheckDeviceState, GetFrameStatistics,
 * CreateSurfaceFactory, IDCompositionDeviceDebug).
 *
 * dcomp.dll and d3d11.dll are loaded with LoadLibrary. MinGW's dcomp.h is
 * C++ only, so methods are called through the vtable by index, in the
 * order of the documented interfaces (MinGW dcomp.h, C layout). Results
 * are HRESULTs and booleans only; no pointers are printed.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/dcomp/nf-dcomp-dcompositioncreatedevice
 *       https://learn.microsoft.com/en-us/windows/win32/api/dcomp/nf-dcomp-dcompositioncreatedevice2
 *       https://learn.microsoft.com/en-us/windows/win32/api/dcomp/nf-dcomp-dcompositioncreatedevice3
 *       https://learn.microsoft.com/en-us/windows/win32/api/dcomp/nn-dcomp-idcompositiondevice
 *       https://learn.microsoft.com/en-us/windows/win32/api/dcomp/nn-dcomp-idcompositiondevice2
 *       https://learn.microsoft.com/en-us/windows/win32/api/dcomp/nn-dcomp-idcompositiondesktopdevice
 *       https://learn.microsoft.com/en-us/windows/win32/api/dcomp/nn-dcomp-idcompositiondevice3
 *       https://learn.microsoft.com/en-us/windows/win32/api/dcomp/nf-dcomp-idcompositiondevice-checkdevicestate
 *       https://learn.microsoft.com/en-us/windows/win32/api/dcomptypes/ns-dcomptypes-dcomposition_frame_statistics
 */
#define COBJMACROS
#include "oracle.h"
#include <initguid.h>
#include <d3d11.h>
#include <dxgi.h>
#include <d2d1_1.h>
#include <dcomptypes.h>

DEFINE_GUID(XIID_IDCompositionDevice,        0xc37ea93a, 0xe7aa, 0x450d, 0xb1, 0x6f, 0x97, 0x46, 0xcb, 0x04, 0x07, 0xf3);
DEFINE_GUID(XIID_IDCompositionDevice2,       0x75f6468d, 0x1b8e, 0x447c, 0x9b, 0xc6, 0x75, 0xfe, 0xa8, 0x0b, 0x5b, 0x25);
DEFINE_GUID(XIID_IDCompositionDesktopDevice, 0x5f4633fe, 0x1e08, 0x4cb8, 0x8c, 0x75, 0xce, 0x24, 0x33, 0x3f, 0x56, 0x02);
DEFINE_GUID(XIID_IDCompositionDevice3,       0x0987cb06, 0xf916, 0x48bf, 0x8d, 0x35, 0xce, 0x76, 0x41, 0x78, 0x1b, 0xd9);
DEFINE_GUID(XIID_IDCompositionDeviceDebug,   0xa1a3c64a, 0x224f, 0x4a81, 0x97, 0x73, 0x4f, 0x03, 0xa8, 0x9d, 0x3c, 0x6c);
DEFINE_GUID(XIID_IDCompositionSurfaceFactory,0xe334bc12, 0x3937, 0x4e02, 0x85, 0xeb, 0xfc, 0xf4, 0xeb, 0x30, 0xd2, 0xc8);
DEFINE_GUID(XIID_IDCompositionVisual,        0x4d93059d, 0x097b, 0x4651, 0x9a, 0x60, 0xf0, 0xf2, 0x51, 0x16, 0xe2, 0xf3);
DEFINE_GUID(XIID_IDCompositionTarget,        0xeacdd04c, 0x117e, 0x4e17, 0x88, 0xf4, 0xd1, 0xb1, 0x2b, 0x0e, 0x3d, 0x89);
DEFINE_GUID(XIID_IUnknown,                   0x00000000, 0x0000, 0x0000, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
DEFINE_GUID(XIID_IMarshal,                   0x00000003, 0x0000, 0x0000, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
DEFINE_GUID(XIID_IAgileObject,               0x94ea2b94, 0xe9cc, 0x49e0, 0xc0, 0xff, 0xee, 0x64, 0xca, 0x8f, 0x5b, 0x90);
DEFINE_GUID(XIID_IInspectable,               0xaf86e2e0, 0xb12d, 0x4c6a, 0x9c, 0x5a, 0xd7, 0xaa, 0x65, 0x10, 0x1e, 0x90);
DEFINE_GUID(XIID_ID3D11Device_,              0xdb6f6ddb, 0xac77, 0x4e88, 0x82, 0x53, 0x81, 0x9d, 0xf9, 0xbb, 0xf1, 0x40);
DEFINE_GUID(XIID_IDXGIDevice_,               0x54ec77fa, 0x1377, 0x44e6, 0x8c, 0x32, 0x88, 0xfd, 0x5f, 0x44, 0xc8, 0x4c);

typedef HRESULT (WINAPI *create_fn)(IUnknown *, REFIID, void **);
typedef HRESULT (WINAPI *d3d11_create_fn)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT,
                                          const D3D_FEATURE_LEVEL *, UINT, UINT, ID3D11Device **,
                                          D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);

/* vtable call: object, index, function type, arguments */
#define VT(obj, idx) ((*(void ***)(obj))[idx])
typedef HRESULT (STDMETHODCALLTYPE *m0)(void *);
typedef HRESULT (STDMETHODCALLTYPE *m1p)(void *, void *);
typedef HRESULT (STDMETHODCALLTYPE *m2p)(void *, void *, void *);
typedef HRESULT (STDMETHODCALLTYPE *mqi)(void *, REFIID, void **);
typedef ULONG (STDMETHODCALLTYPE *mref)(void *);

static ULONG rel(void *p) { return p ? ((mref)VT(p, 2))(p) : 0; }
static HRESULT qi(void *p, REFIID iid, void **out) { return ((mqi)VT(p, 0))(p, iid, out); }

static create_fn create[3];
static const char *create_name[3] = { "dev1", "dev2", "dev3" };
static ID3D11Device *d3d;
static IDXGIDevice *dxgi;

static void o_hr(const char *k, HRESULT hr) { o_hex32(k, (DWORD)hr); }

static const struct { const char *name; const GUID *iid; } iids[] = {
    { "IUnknown", &XIID_IUnknown },
    { "Device", &XIID_IDCompositionDevice },
    { "Device2", &XIID_IDCompositionDevice2 },
    { "DesktopDevice", &XIID_IDCompositionDesktopDevice },
    { "Device3", &XIID_IDCompositionDevice3 },
    { "DeviceDebug", &XIID_IDCompositionDeviceDebug },
    { "SurfaceFactory", &XIID_IDCompositionSurfaceFactory },
    { "Visual", &XIID_IDCompositionVisual },
    { "Target", &XIID_IDCompositionTarget },
    { "IMarshal", &XIID_IMarshal },
    { "IAgileObject", &XIID_IAgileObject },
    { "IInspectable", &XIID_IInspectable },
    { "ID3D11Device", &XIID_ID3D11Device_ },
    { "IDXGIDevice", &XIID_IDXGIDevice_ },
};
#define NIIDS (sizeof(iids) / sizeof(iids[0]))

/* QueryInterface table of obj: S_OK/E_NOINTERFACE for each IID, and
 * which of the returned interfaces are the same pointer as obj or as
 * the IUnknown. */
static void o_qi_table(void *obj)
{
    char k[64];
    void *unk = NULL;
    qi(obj, &XIID_IUnknown, &unk);
    for (size_t i = 0; i < NIIDS; i++) {
        void *p = (void *)0x1;
        HRESULT hr = qi(obj, iids[i].iid, &p);
        snprintf(k, sizeof k, "qi.%s", iids[i].name);
        if (SUCCEEDED(hr)) {
            o_str(k, p == obj ? "ok:self" : p == unk ? "ok:unknown" : "ok:other");
            rel(p);
        } else {
            char v[32];
            snprintf(v, sizeof v, "%08lx%s", (unsigned long)hr, p == NULL ? "" : ":out_untouched");
            o_str(k, v);
        }
    }
    rel(unk);
}

static const char *dev_arg_name[] = { "null", "d3d11", "dxgi", "d3dctx", "dxgifactory" };

static IUnknown *dev_arg(int a)
{
    static IUnknown *ctx, *factory;
    switch (a) {
    case 0: return NULL;
    case 1: return (IUnknown *)d3d;
    case 2: return (IUnknown *)dxgi;
    case 3:
        if (!ctx && d3d) ID3D11Device_GetImmediateContext(d3d, (ID3D11DeviceContext **)&ctx);
        return ctx;
    case 4:
        if (!factory && dxgi) {
            IDXGIAdapter *ad = NULL;
            if (SUCCEEDED(IDXGIDevice_GetAdapter(dxgi, &ad))) {
                IDXGIAdapter_GetParent(ad, &IID_IDXGIFactory, (void **)&factory);
                IDXGIAdapter_Release(ad);
            }
        }
        return factory;
    }
    return NULL;
}

/* Index of Commit etc. is the same (3, 4, 5) in IDCompositionDevice and
 * IDCompositionDevice2 (and so DesktopDevice and Device3). */
static void device_methods(void *dev, int is_v1)
{
    HRESULT hr;
    DCOMPOSITION_FRAME_STATISTICS st;
    LARGE_INTEGER freq;

    hr = ((m0)VT(dev, 3))(dev);
    o_hr("commit", hr);
    hr = ((m0)VT(dev, 3))(dev);
    o_hr("commit_again", hr);
    hr = ((m0)VT(dev, 4))(dev);
    o_hr("wait", hr);

    memset(&st, 0xcc, sizeof st);
    hr = ((m1p)VT(dev, 5))(dev, &st);
    o_hr("stats", hr);
    if (SUCCEEDED(hr)) {
        QueryPerformanceFrequency(&freq);
        o_bool("stats.freq_is_qpc", st.timeFrequency.QuadPart == freq.QuadPart);
        o_bool("stats.current_nonzero", st.currentTime.QuadPart != 0);
        o_bool("stats.rate_nonzero", st.currentCompositionRate.Numerator != 0 && st.currentCompositionRate.Denominator != 0);
    }
    if (is_v1) {
        BOOL valid = 0xcc;
        hr = ((m1p)VT(dev, 26))(dev, &valid);
        o_hr("check_state", hr);
        o_u32("check_state.valid", valid);
    }
}


/* ---- running in the interactive session ---------------------------------
 * DirectComposition needs the desktop window manager, which a
 * non-interactive logon (the oracle's SSH session) doesn't have: there,
 * every device creation fails with E_ACCESSDENIED. When that happens, the
 * test runs itself again in the interactive session of the same user, as a
 * scheduled task with an interactive token (schtasks /IT), and copies that
 * run's output. Where device creation doesn't fail that way, the cases run
 * in this process. The output is the same either way. */

static int run_cmd(const wchar_t *cmd)
{
    STARTUPINFOW si;
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    PROCESS_INFORMATION pi;
    DWORD code = 1;
    wchar_t buf[2048];
    wcsncpy(buf, cmd, 2047);
    buf[2047] = 0;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = si.hStdOutput = si.hStdError = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE, 3, NULL, OPEN_EXISTING, 0, NULL);
    if (!CreateProcessW(NULL, buf, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) return -1;
    WaitForSingleObject(pi.hProcess, 60000);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(si.hStdInput);
    return (int)code;
}

static int relay_interactive(const wchar_t *task)
{
    wchar_t exe[MAX_PATH], dir[MAX_PATH], out[MAX_PATH], done[MAX_PATH], cmd[2048];
    char buf[4096];
    DWORD n;
    HANDLE f;
    int ok = 0;

    GetModuleFileNameW(NULL, exe, MAX_PATH);
    GetCurrentDirectoryW(MAX_PATH, dir);
    swprintf(out, MAX_PATH, L"%ls\\interactive.out", dir);
    swprintf(done, MAX_PATH, L"%ls\\interactive.done", dir);
    DeleteFileW(out);
    DeleteFileW(done);
    swprintf(cmd, 2048, L"schtasks.exe /create /f /it /rl highest /sc once /st 00:00 /tn %ls /tr \"%ls child %ls\"", task, exe, dir);
    if (run_cmd(cmd)) return 0;
    /* the interactive session can still be starting right after the VM
     * is restored: try a few times */
    for (int tries = 0; tries < 5 && !ok; tries++) {
        swprintf(cmd, 2048, L"schtasks.exe /run /tn %ls", task);
        if (!run_cmd(cmd)) {
            for (int i = 0; i < 300 && GetFileAttributesW(done) == INVALID_FILE_ATTRIBUTES; i++) Sleep(100);
            ok = GetFileAttributesW(done) != INVALID_FILE_ATTRIBUTES;
        }
        if (!ok) Sleep(3000);
    }
    swprintf(cmd, 2048, L"schtasks.exe /delete /f /tn %ls", task);
    run_cmd(cmd);
    if (!ok) return 0;
    f = CreateFileW(out, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    while (ReadFile(f, buf, sizeof buf, &n, NULL) && n) fwrite(buf, 1, n, stdout);
    fflush(stdout);
    CloseHandle(f);
    return 1;
}

/* in the scheduled task: the output goes to <dir>\interactive.out, and
 * <dir>\interactive.done marks the end (also after a crash) */
static wchar_t child_done[MAX_PATH];
static LONG WINAPI child_crash(EXCEPTION_POINTERS *ep)
{
    (void)ep;
    fflush(stdout);
    CloseHandle(CreateFileW(child_done, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL));
    return EXCEPTION_EXECUTE_HANDLER;
}
static void child_setup(const wchar_t *dir)
{
    wchar_t out[MAX_PATH];
    swprintf(out, MAX_PATH, L"%ls\\interactive.out", dir);
    swprintf(child_done, MAX_PATH, L"%ls\\interactive.done", dir);
    SetCurrentDirectoryW(dir);
    _wfreopen(out, L"wb", stdout);
    SetUnhandledExceptionFilter(child_crash);
}
static void child_finish(void)
{
    fflush(stdout);
    fclose(stdout);
    CloseHandle(CreateFileW(child_done, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL));
}

static HRESULT probe_access(void)
{
    HMODULE m = LoadLibraryW(L"dcomp.dll");
    create_fn f = m ? (create_fn)(void (*)(void))GetProcAddress(m, "DCompositionCreateDevice2") : NULL;
    void *dev = NULL;
    HRESULT hr;
    if (!f) return E_FAIL;
    hr = f(NULL, &XIID_IDCompositionDesktopDevice, &dev);
    if (SUCCEEDED(hr) && dev) rel(dev);
    return hr;
}

static void create_case(int fn, int arg, const char *iidname, const GUID *iid)
{
    char name[96];
    void *dev = (void *)0x1;
    HRESULT hr;
    IUnknown *a = dev_arg(arg);

    snprintf(name, sizeof name, "%s_%s_%s", create_name[fn], dev_arg_name[arg], iidname);
    if (arg && !a) return; /* no D3D device: skipped on both sides alike */
    hr = create[fn](a, iid, &dev);
    oracle_case(name);
    o_hr("hr", hr);
    o_bool("out_null", dev == NULL);
    if (SUCCEEDED(hr) && dev && dev != (void *)0x1) {
        o_qi_table(dev);
        rel(dev);
    }
    oracle_case_end();
}

static int run_cases(void)
{
    HMODULE dcomp, d3d11lib;
    oracle_begin("dcomp/device_create");

    dcomp = LoadLibraryW(L"dcomp.dll");
    oracle_case("load");
    o_bool("dcomp", dcomp != NULL);
    create[0] = (create_fn)(void (*)(void))GetProcAddress(dcomp, "DCompositionCreateDevice");
    create[1] = (create_fn)(void (*)(void))GetProcAddress(dcomp, "DCompositionCreateDevice2");
    create[2] = (create_fn)(void (*)(void))GetProcAddress(dcomp, "DCompositionCreateDevice3");
    o_bool("create1", create[0] != NULL);
    o_bool("create2", create[1] != NULL);
    o_bool("create3", create[2] != NULL);
    d3d11lib = LoadLibraryW(L"d3d11.dll");
    if (d3d11lib) {
        d3d11_create_fn pD3D11CreateDevice = (d3d11_create_fn)(void (*)(void))GetProcAddress(d3d11lib, "D3D11CreateDevice");
        HRESULT hr = E_FAIL;
        if (pD3D11CreateDevice) {
            hr = pD3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                    NULL, 0, D3D11_SDK_VERSION, &d3d, NULL, NULL);
            if (FAILED(hr))
                hr = pD3D11CreateDevice(NULL, D3D_DRIVER_TYPE_WARP, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                        NULL, 0, D3D11_SDK_VERSION, &d3d, NULL, NULL);
        }
        if (SUCCEEDED(hr)) ID3D11Device_QueryInterface(d3d, &IID_IDXGIDevice, (void **)&dxgi);
    }
    o_bool("d3d11_device", d3d != NULL);
    oracle_case_end();
    if (!create[0] || !create[1] || !create[2]) return oracle_end();

    {
        static const struct { const char *n; const GUID *g; } ciids[] = {
            { "Device", &XIID_IDCompositionDevice },
            { "Device2", &XIID_IDCompositionDevice2 },
            { "DesktopDevice", &XIID_IDCompositionDesktopDevice },
            { "Device3", &XIID_IDCompositionDevice3 },
            { "IUnknown", &XIID_IUnknown },
            { "DeviceDebug", &XIID_IDCompositionDeviceDebug },
            { "Visual", &XIID_IDCompositionVisual },
        };
        for (int fn = 0; fn < 3; fn++)
            for (int arg = 0; arg < 5; arg++)
                for (size_t i = 0; i < sizeof(ciids) / sizeof(ciids[0]); i++)
                    create_case(fn, arg, ciids[i].n, ciids[i].g);
    }

    /* methods on each kind of device */
    for (int fn = 0; fn < 3; fn++) {
        for (int arg = 0; arg < 3; arg++) {
            const GUID *iid = fn == 0 ? &XIID_IDCompositionDevice : &XIID_IDCompositionDesktopDevice;
            char name[64];
            void *dev = NULL;
            IUnknown *a = dev_arg(arg);
            if (arg && !a) continue;
            if (FAILED(create[fn](a, iid, &dev)) || !dev) continue;
            snprintf(name, sizeof name, "methods_%s_%s", create_name[fn], dev_arg_name[arg]);
            oracle_case(name);
            device_methods(dev, fn == 0);
            oracle_case_end();
            rel(dev);
        }
    }

    /* the v1 interface from CreateDevice2/3 (Windows App SDK asks
     * DCompositionCreateDevice3 for IDCompositionDevice) */
    for (int fn = 1; fn < 3; fn++) {
        char name[64];
        void *dev = NULL;
        if (FAILED(create[fn]((IUnknown *)dxgi, &XIID_IDCompositionDevice, &dev)) || !dev) continue;
        snprintf(name, sizeof name, "methods_v1_from_%s", create_name[fn]);
        oracle_case(name);
        device_methods(dev, 1);
        oracle_case_end();
        rel(dev);
    }

    /* CreateSurfaceFactory (IDCompositionDevice2 index 7) */
    {
        void *dev = NULL;
        if (SUCCEEDED(create[1](NULL, &XIID_IDCompositionDesktopDevice, &dev)) && dev) {
            for (int arg = 0; arg < 5; arg++) {
                char name[64];
                void *f = (void *)0x1;
                IUnknown *a = dev_arg(arg);
                HRESULT hr;
                if (arg && !a) continue;
                hr = ((m2p)VT(dev, 7))(dev, a, &f);
                snprintf(name, sizeof name, "surface_factory_%s", dev_arg_name[arg]);
                oracle_case(name);
                o_hr("hr", hr);
                o_bool("out_null", f == NULL);
                if (SUCCEEDED(hr) && f) { o_qi_table(f); rel(f); }
                oracle_case_end();
            }
            rel(dev);
        }
    }

    /* IDCompositionDeviceDebug: EnableDebugCounters (3), DisableDebugCounters (4) */
    {
        void *dev = NULL, *dbg = NULL;
        if (SUCCEEDED(create[1](NULL, &XIID_IDCompositionDesktopDevice, &dev)) && dev) {
            HRESULT hr = qi(dev, &XIID_IDCompositionDeviceDebug, &dbg);
            oracle_case("device_debug");
            o_hr("qi", hr);
            if (SUCCEEDED(hr)) {
                o_hr("enable", ((m0)VT(dbg, 3))(dbg));
                o_hr("enable_again", ((m0)VT(dbg, 3))(dbg));
                o_hr("disable", ((m0)VT(dbg, 4))(dbg));
                o_hr("disable_again", ((m0)VT(dbg, 4))(dbg));
                rel(dbg);
            }
            oracle_case_end();
            rel(dev);
        }
    }

    /* the QueryInterface tables seen from IDCompositionDevice3 and
     * IDCompositionDeviceDebug of a DCompositionCreateDevice3 device */
    {
        void *dev = NULL, *d3 = NULL, *dbg = NULL;
        if (SUCCEEDED(create[2](NULL, &XIID_IDCompositionDesktopDevice, &dev)) && dev) {
            if (SUCCEEDED(qi(dev, &XIID_IDCompositionDevice3, &d3))) {
                oracle_case("qi_from_device3");
                o_qi_table(d3);
                oracle_case_end();
                rel(d3);
            }
            if (SUCCEEDED(qi(dev, &XIID_IDCompositionDeviceDebug, &dbg))) {
                oracle_case("qi_from_debug");
                o_qi_table(dbg);
                oracle_case_end();
                rel(dbg);
            }
            rel(dev);
        }
    }

    /* reference counting: AddRef/Release values (COM docs say they're for
     * testing only, but apps print them) */
    {
        void *dev = NULL;
        if (SUCCEEDED(create[1](NULL, &XIID_IDCompositionDesktopDevice, &dev)) && dev) {
            void *d2 = NULL;
            oracle_case("refcount");
            o_u32("addref", ((mref)VT(dev, 1))(dev));
            o_u32("release", rel(dev));
            qi(dev, &XIID_IDCompositionDevice2, &d2);
            o_u32("after_qi_release", rel(d2));
            o_u32("final_release", rel(dev));
            oracle_case_end();
        }
    }

    /* bad arguments last: a NULL out pointer and a NULL iid */
    for (int fn = 0; fn < 3; fn++) {
        char name[64];
        HRESULT hr = create[fn](NULL, fn == 0 ? &XIID_IDCompositionDevice : &XIID_IDCompositionDevice2, NULL);
        snprintf(name, sizeof name, "%s_null_out", create_name[fn]);
        oracle_case(name);
        o_hr("hr", hr);
        oracle_case_end();
    }
    return oracle_end();
}

int main(void)
{
    const wchar_t *child = wcsstr(GetCommandLineW(), L" child ");
    if (child) {
        child_setup(child + 7);
        run_cases();
        child_finish();
        return 0;
    }
    if (probe_access() == E_ACCESSDENIED && relay_interactive(L"SetuOracleDcompDeviceCreate"))
        return 0;
    return run_cases();
}
