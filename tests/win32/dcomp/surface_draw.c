/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: DirectComposition surfaces. CreateSurface and
 * CreateVirtualSurface with and without a rendering device (none, an
 * IDXGIDevice, an ID2D1Device; the v1 device; a surface factory), across
 * pixel formats, alpha modes and sizes; BeginDraw with each interface ID
 * (IDXGISurface, ID3D11Texture2D, ID2D1DeviceContext, ...), what it hands
 * out (texture format, usage, bind flags; no sizes beyond "at least the
 * update rectangle", no offsets beyond "not negative"); the BeginDraw,
 * EndDraw, SuspendDraw, ResumeDraw and Scroll state rules; virtual surface
 * Resize and Trim; SetContent with surfaces of the same and of another
 * device; CreateSurfaceFromHandle and CreateSurfaceFromHwnd.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/dcomp/nf-dcomp-idcompositiondevice2-createsurface
 *       https://learn.microsoft.com/en-us/windows/win32/api/dcomp/nf-dcomp-idcompositionsurface-begindraw
 *       https://learn.microsoft.com/en-us/windows/win32/api/dcomp/nf-dcomp-idcompositionsurface-enddraw
 *       https://learn.microsoft.com/en-us/windows/win32/api/dcomp/nf-dcomp-idcompositionsurface-suspenddraw
 *       https://learn.microsoft.com/en-us/windows/win32/api/dcomp/nf-dcomp-idcompositionsurface-scroll
 *       https://learn.microsoft.com/en-us/windows/win32/api/dcomp/nn-dcomp-idcompositionvirtualsurface
 *       https://learn.microsoft.com/en-us/windows/win32/api/dcomp/nf-dcomp-idcompositiondevice2-createsurfacefactory
 *       https://learn.microsoft.com/en-us/windows/win32/directcomp/directcomposition-error-codes
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

DEFINE_GUID(XIID_IDCompositionSurface,       0xbb8a4953, 0x2c99, 0x4f5a, 0x96, 0xf5, 0x48, 0x19, 0x02, 0x7f, 0xa3, 0xac);
DEFINE_GUID(XIID_IDCompositionVirtualSurface,0xae471c51, 0x5f53, 0x4a24, 0x8d, 0x3e, 0xd0, 0xc3, 0x9c, 0x30, 0xb3, 0xf0);
DEFINE_GUID(XIID_IDXGISurface_,              0xcafcb56c, 0x6ac3, 0x4889, 0xbf, 0x47, 0x9e, 0x23, 0xbb, 0xd2, 0x60, 0xec);
DEFINE_GUID(XIID_IDXGISurface1_,             0x4ae63092, 0x6327, 0x4c1b, 0x80, 0xae, 0xbf, 0xe1, 0x2e, 0xa3, 0x2b, 0x86);
DEFINE_GUID(XIID_ID3D11Texture2D_,           0x6f15aaf2, 0xd208, 0x4e89, 0x9a, 0xb4, 0x48, 0x95, 0x35, 0xd3, 0x4f, 0x9c);
DEFINE_GUID(XIID_ID3D11Resource_,            0xdc8e63f3, 0xd12b, 0x4952, 0xb4, 0x7b, 0x5e, 0x45, 0x02, 0x6a, 0x86, 0x2d);
DEFINE_GUID(XIID_ID2D1DeviceContext_,        0xe8f7fe7a, 0x191c, 0x466d, 0xad, 0x95, 0x97, 0x56, 0x78, 0xbd, 0xa9, 0x98);
DEFINE_GUID(XIID_ID2D1Device_,               0x47dd575d, 0xac05, 0x4cdd, 0x80, 0x49, 0x9b, 0x02, 0xcd, 0x16, 0xf4, 0x4c);

typedef HRESULT (WINAPI *d2d1_create_device_fn)(IDXGIDevice *, const D2D1_CREATION_PROPERTIES *, ID2D1Device **);
typedef HRESULT (STDMETHODCALLTYPE *mcreate_surface)(void *, UINT, UINT, DXGI_FORMAT, DXGI_ALPHA_MODE, void **);
typedef HRESULT (STDMETHODCALLTYPE *mbegin)(void *, const RECT *, REFIID, void **, POINT *);
typedef HRESULT (STDMETHODCALLTYPE *mscroll)(void *, const RECT *, const RECT *, int, int);
typedef HRESULT (STDMETHODCALLTYPE *mresize)(void *, UINT, UINT);
typedef HRESULT (STDMETHODCALLTYPE *mtrim)(void *, const RECT *, UINT);
typedef HRESULT (STDMETHODCALLTYPE *mtarget)(void *, HWND, BOOL, void **);

static create_fn create1, create2;
static ID3D11Device *d3d;
static IDXGIDevice *dxgi;
static ID2D1Device *d2d;
static void o_hr(const char *k, HRESULT hr) { o_hex32(k, (DWORD)hr); }

static const struct { const char *name; const GUID *iid; } iids[] = {
    { "IUnknown", &XIID_IUnknown },
    { "Surface", &XIID_IDCompositionSurface },
    { "VirtualSurface", &XIID_IDCompositionVirtualSurface },
    { "SurfaceFactory", &XIID_IDCompositionSurfaceFactory },
    { "Visual", &XIID_IDCompositionVisual },
    { "IDXGISurface", &XIID_IDXGISurface_ },
    { "ID3D11Texture2D", &XIID_ID3D11Texture2D_ },
    { "IMarshal", &XIID_IMarshal },
    { "IAgileObject", &XIID_IAgileObject },
};
#define NIIDS (sizeof(iids) / sizeof(iids[0]))

static void o_qi_table(const char *k, void *obj)
{
    char buf[1024] = "";
    void *unk = NULL;
    qi(obj, &XIID_IUnknown, &unk);
    for (size_t i = 0; i < NIIDS; i++) {
        void *p = (void *)0x1;
        HRESULT hr = qi(obj, iids[i].iid, &p);
        char item[96];
        if (SUCCEEDED(hr)) {
            snprintf(item, sizeof item, "%s%s=%s", *buf ? " " : "", iids[i].name,
                     p == obj ? "self" : p == unk ? "unknown" : "other");
            rel(p);
        } else if (hr != E_NOINTERFACE || p != NULL) {
            snprintf(item, sizeof item, "%s%s=%08lx%s", *buf ? " " : "", iids[i].name,
                     (unsigned long)hr, p ? ":out_untouched" : "");
        } else continue;
        strncat(buf, item, sizeof buf - strlen(buf) - 1);
    }
    rel(unk);
    o_str(k, buf);
}

#define D_CreateSurface 8
#define D_CreateVirtualSurface 9
#define S_BeginDraw 3
#define S_EndDraw 4
#define S_SuspendDraw 5
#define S_ResumeDraw 6
#define S_Scroll 7
#define S_Resize 8
#define S_Trim 9

static void *new_device(IUnknown *rd)
{
    void *dev = NULL;
    if (FAILED(create2(rd, &XIID_IDCompositionDesktopDevice, &dev))) return NULL;
    return dev;
}

static HRESULT surface(void *dev, UINT w, UINT h, DXGI_FORMAT f, DXGI_ALPHA_MODE a, void **s)
{
    *s = (void *)0x1;
    return ((mcreate_surface)VT(dev, D_CreateSurface))(dev, w, h, f, a, s);
}

static void create_cases(const char *prefix, void *dev, int idx)
{
    static const struct { const char *n; UINT w, h; DXGI_FORMAT f; DXGI_ALPHA_MODE a; } c[] = {
        { "bgra_premul", 64, 32, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED },
        { "bgra_ignore", 64, 32, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_IGNORE },
        { "bgra_straight", 64, 32, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_STRAIGHT },
        { "bgra_unspecified", 64, 32, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_UNSPECIFIED },
        { "bgra_alpha_bad", 64, 32, DXGI_FORMAT_B8G8R8A8_UNORM, (DXGI_ALPHA_MODE)7 },
        { "rgba_premul", 64, 32, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED },
        { "rgba16f_premul", 64, 32, DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_ALPHA_MODE_PREMULTIPLIED },
        { "rgb10a2_premul", 64, 32, DXGI_FORMAT_R10G10B10A2_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED },
        { "bgrx_ignore", 64, 32, DXGI_FORMAT_B8G8R8X8_UNORM, DXGI_ALPHA_MODE_IGNORE },
        { "bgra_srgb", 64, 32, DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, DXGI_ALPHA_MODE_PREMULTIPLIED },
        { "a8_premul", 64, 32, DXGI_FORMAT_A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED },
        { "r32f", 64, 32, DXGI_FORMAT_R32_FLOAT, DXGI_ALPHA_MODE_IGNORE },
        { "unknown_format", 64, 32, DXGI_FORMAT_UNKNOWN, DXGI_ALPHA_MODE_PREMULTIPLIED },
        { "zero_width", 0, 32, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED },
        { "zero_both", 0, 0, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED },
        { "one", 1, 1, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED },
        { "w16384", 16384, 1, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED },
        { "w16385", 16385, 1, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED },
        { "huge", 0x7fffffff, 0x7fffffff, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED },
    };
    char name[96];
    for (size_t i = 0; i < sizeof c / sizeof c[0]; i++) {
        void *s = (void *)0x1;
        HRESULT hr = ((mcreate_surface)VT(dev, idx))(dev, c[i].w, c[i].h, c[i].f, c[i].a, &s);
        snprintf(name, sizeof name, "%s_%s", prefix, c[i].n);
        oracle_case(name);
        o_hr("hr", hr);
        o_str("out", s == NULL ? "null" : s == (void *)0x1 ? "untouched" : "set");
        if (SUCCEEDED(hr) && i == 0) o_qi_table("qi", s);
        oracle_case_end();
        if (SUCCEEDED(hr) && s && s != (void *)0x1) rel(s);
    }
}

/* what BeginDraw handed out: kind and description */
static void o_drawn(const char *k, REFIID iid, void *p)
{
    char key[64];
    if (IsEqualGUID(iid, &XIID_ID3D11Texture2D_)) {
        D3D11_TEXTURE2D_DESC d;
        ID3D11Texture2D_GetDesc((ID3D11Texture2D *)p, &d);
        snprintf(key, sizeof key, "%s.format", k); o_u32(key, d.Format);
        snprintf(key, sizeof key, "%s.usage", k); o_u32(key, d.Usage);
        snprintf(key, sizeof key, "%s.bind", k); o_hex32(key, d.BindFlags);
        snprintf(key, sizeof key, "%s.cpu", k); o_hex32(key, d.CPUAccessFlags);
        snprintf(key, sizeof key, "%s.mips", k); o_u32(key, d.MipLevels);
        snprintf(key, sizeof key, "%s.array", k); o_u32(key, d.ArraySize);
        snprintf(key, sizeof key, "%s.samples", k); o_u32(key, d.SampleDesc.Count);
        snprintf(key, sizeof key, "%s.w_ge64", k); o_bool(key, d.Width >= 64);
        snprintf(key, sizeof key, "%s.h_ge32", k); o_bool(key, d.Height >= 32);
    } else if (IsEqualGUID(iid, &XIID_IDXGISurface_)) {
        DXGI_SURFACE_DESC d;
        IDXGISurface_GetDesc((IDXGISurface *)p, &d);
        snprintf(key, sizeof key, "%s.format", k); o_u32(key, d.Format);
        snprintf(key, sizeof key, "%s.w_ge64", k); o_bool(key, d.Width >= 64);
        snprintf(key, sizeof key, "%s.h_ge32", k); o_bool(key, d.Height >= 32);
    }
}

static void draw_cases(const char *prefix, void *dev, int is_d2d)
{
    static const struct { const char *n; const GUID *iid; } kinds[] = {
        { "dxgi_surface", &XIID_IDXGISurface_ },
        { "dxgi_surface1", &XIID_IDXGISurface1_ },
        { "texture2d", &XIID_ID3D11Texture2D_ },
        { "resource", &XIID_ID3D11Resource_ },
        { "d2d_context", &XIID_ID2D1DeviceContext_ },
        { "unknown", &XIID_IUnknown },
        { "surface_iid", &XIID_IDCompositionSurface },
    };
    char name[96];
    void *s = NULL, *s2 = NULL, *p;
    POINT off;
    HRESULT hr;
    RECT full = { 0, 0, 64, 32 }, part = { 8, 8, 24, 16 }, out = { 60, 0, 70, 10 }, empty = { 5, 5, 5, 9 };
    RECT neg = { -1, 0, 10, 10 }, swapped = { 10, 10, 0, 0 };

    for (size_t i = 0; i < sizeof kinds / sizeof kinds[0]; i++) {
        if (FAILED(surface(dev, 64, 32, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED, &s))) return;
        p = (void *)0x1;
        off.x = off.y = -7;
        hr = ((mbegin)VT(s, S_BeginDraw))(s, NULL, kinds[i].iid, &p, &off);
        snprintf(name, sizeof name, "%s_begin_%s", prefix, kinds[i].n);
        oracle_case(name);
        o_hr("hr", hr);
        o_str("out", p == NULL ? "null" : p == (void *)0x1 ? "untouched" : "set");
        o_bool("offset_untouched", off.x == -7 && off.y == -7);
        o_bool("offset_nonneg", off.x >= 0 && off.y >= 0);
        if (SUCCEEDED(hr) && p && p != (void *)0x1) {
            o_drawn("drawn", kinds[i].iid, p);
            rel(p);
            o_hr("end", ((m0)VT(s, S_EndDraw))(s));
        }
        oracle_case_end();
        rel(s);
    }
    (void)is_d2d;

    /* the BeginDraw/EndDraw state rules, with IDXGISurface */
    if (FAILED(surface(dev, 64, 32, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED, &s))) return;
    surface(dev, 64, 32, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED, &s2);
    snprintf(name, sizeof name, "%s_rules", prefix);
    oracle_case(name);
    o_hr("end_not_drawing", ((m0)VT(s, S_EndDraw))(s));
    o_hr("suspend_not_drawing", ((m0)VT(s, S_SuspendDraw))(s));
    o_hr("resume_not_drawing", ((m0)VT(s, S_ResumeDraw))(s));
    p = (void *)0x1;
    o_hr("first_partial", ((mbegin)VT(s, S_BeginDraw))(s, &part, &XIID_IDXGISurface_, &p, &off));
    if (p && p != (void *)0x1) { rel(p); ((m0)VT(s, S_EndDraw))(s); }
    p = NULL;
    o_hr("begin_null_out", ((mbegin)VT(s, S_BeginDraw))(s, NULL, &XIID_IDXGISurface_, NULL, &off));
    o_hr("begin_null_offset", ((mbegin)VT(s, S_BeginDraw))(s, NULL, &XIID_IDXGISurface_, &p, NULL));
    if (p) { rel(p); ((m0)VT(s, S_EndDraw))(s); }
    p = NULL;
    o_hr("begin_outside", ((mbegin)VT(s, S_BeginDraw))(s, &out, &XIID_IDXGISurface_, &p, &off));
    if (p) { rel(p); ((m0)VT(s, S_EndDraw))(s); p = NULL; }
    o_hr("begin_empty", ((mbegin)VT(s, S_BeginDraw))(s, &empty, &XIID_IDXGISurface_, &p, &off));
    if (p) { rel(p); ((m0)VT(s, S_EndDraw))(s); p = NULL; }
    o_hr("begin_negative", ((mbegin)VT(s, S_BeginDraw))(s, &neg, &XIID_IDXGISurface_, &p, &off));
    if (p) { rel(p); ((m0)VT(s, S_EndDraw))(s); p = NULL; }
    o_hr("begin_swapped", ((mbegin)VT(s, S_BeginDraw))(s, &swapped, &XIID_IDXGISurface_, &p, &off));
    if (p) { rel(p); ((m0)VT(s, S_EndDraw))(s); p = NULL; }
    o_hr("begin_full", ((mbegin)VT(s, S_BeginDraw))(s, &full, &XIID_IDXGISurface_, &p, &off));
    if (p) { rel(p); p = NULL; }
    o_hr("begin_twice", ((mbegin)VT(s, S_BeginDraw))(s, &full, &XIID_IDXGISurface_, &p, &off));
    if (p) { rel(p); p = NULL; }
    o_hr("begin_other_surface", ((mbegin)VT(s2, S_BeginDraw))(s2, &full, &XIID_IDXGISurface_, &p, &off));
    if (p) { rel(p); p = NULL; ((m0)VT(s2, S_EndDraw))(s2); }
    o_hr("commit_while_drawing", ((m0)VT(dev, 3))(dev));
    o_hr("scroll_while_drawing", ((mscroll)VT(s, S_Scroll))(s, NULL, NULL, 1, 1));
    o_hr("suspend", ((m0)VT(s, S_SuspendDraw))(s));
    o_hr("suspend_again", ((m0)VT(s, S_SuspendDraw))(s));
    o_hr("begin_while_suspended", ((mbegin)VT(s2, S_BeginDraw))(s2, &full, &XIID_IDXGISurface_, &p, &off));
    if (p) { rel(p); p = NULL; ((m0)VT(s2, S_EndDraw))(s2); }
    o_hr("end_while_suspended", ((m0)VT(s, S_EndDraw))(s));
    o_hr("resume", ((m0)VT(s, S_ResumeDraw))(s));
    o_hr("resume_again", ((m0)VT(s, S_ResumeDraw))(s));
    o_hr("end", ((m0)VT(s, S_EndDraw))(s));
    o_hr("end_again", ((m0)VT(s, S_EndDraw))(s));
    o_hr("scroll", ((mscroll)VT(s, S_Scroll))(s, NULL, NULL, 1, 1));
    o_hr("scroll_rect", ((mscroll)VT(s, S_Scroll))(s, &part, NULL, -2, 3));
    o_hr("scroll_outside", ((mscroll)VT(s, S_Scroll))(s, &out, NULL, 1, 1));
    o_hr("commit", ((m0)VT(dev, 3))(dev));
    {
        void *t = NULL;
        HRESULT h2 = ((m1p)VT(dev, 6))(dev, &t);
        if (SUCCEEDED(h2)) {
            o_hr("visual_set_content", ((m1p)VT(t, 15))(t, s));
            o_hr("visual_set_content_null", ((m1p)VT(t, 15))(t, NULL));
            rel(t);
        }
    }
    oracle_case_end();
    rel(s2);
    rel(s);

    /* virtual surfaces (index 9) */
    s = (void *)0x1;
    hr = ((mcreate_surface)VT(dev, D_CreateVirtualSurface))(dev, 0, 0, DXGI_FORMAT_B8G8R8A8_UNORM,
                                                            DXGI_ALPHA_MODE_PREMULTIPLIED, &s);
    snprintf(name, sizeof name, "%s_virtual", prefix);
    oracle_case(name);
    o_hr("create_empty", hr);
    if (SUCCEEDED(hr)) {
        o_qi_table("qi", s);
        p = NULL;
        o_hr("begin_on_empty", ((mbegin)VT(s, S_BeginDraw))(s, NULL, &XIID_IDXGISurface_, &p, &off));
        if (p) { rel(p); p = NULL; ((m0)VT(s, S_EndDraw))(s); }
        o_hr("resize", ((mresize)VT(s, S_Resize))(s, 64, 32));
        o_hr("begin_partial", ((mbegin)VT(s, S_BeginDraw))(s, &part, &XIID_IDXGISurface_, &p, &off));
        if (p) { rel(p); p = NULL; }
        o_hr("resize_while_drawing", ((mresize)VT(s, S_Resize))(s, 128, 128));
        o_hr("end", ((m0)VT(s, S_EndDraw))(s));
        o_hr("begin_outside", ((mbegin)VT(s, S_BeginDraw))(s, &out, &XIID_IDXGISurface_, &p, &off));
        if (p) { rel(p); p = NULL; ((m0)VT(s, S_EndDraw))(s); }
        o_hr("trim_null", ((mtrim)VT(s, S_Trim))(s, NULL, 0));
        o_hr("trim_rect", ((mtrim)VT(s, S_Trim))(s, &part, 1));
        o_hr("resize_huge", ((mresize)VT(s, S_Resize))(s, 0x7fffffff, 0x7fffffff));
        o_hr("resize_zero", ((mresize)VT(s, S_Resize))(s, 0, 0));
        o_hr("commit", ((m0)VT(dev, 3))(dev));
        rel(s);
    }
    oracle_case_end();
}

static int run_cases(void)
{
    HMODULE dcomp, lib;
    d3d11_create_fn pD3D11CreateDevice;
    oracle_begin("dcomp/surface_draw");
    dcomp = LoadLibraryW(L"dcomp.dll");
    create1 = (create_fn)(void (*)(void))GetProcAddress(dcomp, "DCompositionCreateDevice");
    create2 = (create_fn)(void (*)(void))GetProcAddress(dcomp, "DCompositionCreateDevice2");
    lib = LoadLibraryW(L"d3d11.dll");
    pD3D11CreateDevice = (d3d11_create_fn)(void (*)(void))GetProcAddress(lib, "D3D11CreateDevice");
    if (pD3D11CreateDevice &&
        FAILED(pD3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                  NULL, 0, D3D11_SDK_VERSION, &d3d, NULL, NULL)))
        pD3D11CreateDevice(NULL, D3D_DRIVER_TYPE_WARP, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                           NULL, 0, D3D11_SDK_VERSION, &d3d, NULL, NULL);
    if (d3d) ID3D11Device_QueryInterface(d3d, &IID_IDXGIDevice, (void **)&dxgi);
    lib = LoadLibraryW(L"d2d1.dll");
    if (lib && dxgi) {
        d2d1_create_device_fn pD2D1CreateDevice = (d2d1_create_device_fn)(void (*)(void))GetProcAddress(lib, "D2D1CreateDevice");
        if (pD2D1CreateDevice) pD2D1CreateDevice(dxgi, NULL, &d2d);
    }
    oracle_case("setup");
    o_bool("create", create2 != NULL);
    o_bool("d3d11", d3d != NULL);
    o_bool("d2d", d2d != NULL);
    oracle_case_end();
    if (!create2 || !dxgi) return oracle_end();

    {   /* surfaces need a rendering device */
        void *dev = new_device(NULL);
        if (dev) {
            create_cases("nodev", dev, D_CreateSurface);
            rel(dev);
        }
    }
    {
        void *dev = new_device((IUnknown *)dxgi);
        if (dev) {
            create_cases("dxgi", dev, D_CreateSurface);
            create_cases("dxgi_virtual", dev, D_CreateVirtualSurface);
            draw_cases("dxgi", dev, 0);
            rel(dev);
        }
    }
    if (d2d) {
        void *dev = new_device((IUnknown *)d2d);
        oracle_case("d2d_device");
        o_bool("ok", dev != NULL);
        oracle_case_end();
        if (dev) {
            draw_cases("d2d", dev, 1);
            rel(dev);
        }
    }
    {   /* the v1 device (DCompositionCreateDevice with the IDXGIDevice) */
        void *dev = NULL;
        if (SUCCEEDED(create1((IUnknown *)dxgi, &XIID_IDCompositionDevice, &dev))) {
            create_cases("v1", dev, D_CreateSurface);
            rel(dev);
        }
    }
    {   /* a surface factory for another device, from a device without one */
        void *dev = new_device(NULL), *f = (void *)0x1, *s;
        if (dev) {
            HRESULT hr = ((m2p)VT(dev, 7))(dev, (IUnknown *)dxgi, &f);
            oracle_case("factory");
            o_hr("hr", hr);
            if (SUCCEEDED(hr)) {
                o_qi_table("qi", f);
                s = (void *)0x1;
                hr = ((mcreate_surface)VT(f, 3))(f, 64, 32, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED, &s);
                o_hr("surface", hr);
                if (SUCCEEDED(hr)) {
                    void *v = NULL, *p = NULL;
                    POINT off;
                    o_qi_table("surface.qi", s);
                    o_hr("begin", ((mbegin)VT(s, S_BeginDraw))(s, NULL, &XIID_IDXGISurface_, &p, &off));
                    if (p) { rel(p); o_hr("end", ((m0)VT(s, S_EndDraw))(s)); }
                    if (SUCCEEDED(((m1p)VT(dev, 6))(dev, &v))) {
                        o_hr("set_content", ((m1p)VT(v, 15))(v, s));
                        rel(v);
                    }
                    rel(s);
                }
                s = (void *)0x1;
                hr = ((mcreate_surface)VT(f, 4))(f, 64, 32, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED, &s);
                o_hr("virtual", hr);
                if (SUCCEEDED(hr)) rel(s);
                rel(f);
            }
            oracle_case_end();
            rel(dev);
        }
    }
    {   /* a surface of one device as content of another device's visual */
        void *da = new_device((IUnknown *)dxgi), *db = new_device((IUnknown *)dxgi), *s = NULL, *v = NULL;
        if (da && db && SUCCEEDED(surface(da, 16, 16, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED, &s)) &&
            SUCCEEDED(((m1p)VT(db, 6))(db, &v))) {
            oracle_case("content_other_device");
            o_hr("hr", ((m1p)VT(v, 15))(v, s));
            oracle_case_end();
        }
        rel(v); rel(s); rel(da); rel(db);
    }
    {   /* CreateSurfaceFromHandle / CreateSurfaceFromHwnd (DesktopDevice 25, 26) */
        void *dev = new_device((IUnknown *)dxgi), *s;
        HWND w = CreateWindowExW(0, L"STATIC", L"x", WS_POPUP, 0, 0, 50, 50, NULL, NULL, NULL, NULL);
        if (dev) {
            oracle_case("from_handle");
            s = (void *)0x1;
            o_hr("null_handle", ((m2p)VT(dev, 25))(dev, NULL, &s));
            o_bool("null_handle_out_null", s == NULL);
            s = (void *)0x1;
            o_hr("bogus_handle", ((m2p)VT(dev, 25))(dev, (void *)(ULONG_PTR)0x1234, &s));
            if (s && s != (void *)0x1) rel(s);
            s = (void *)0x1;
            o_hr("hwnd", ((m2p)VT(dev, 26))(dev, w, &s));
            if (s && s != (void *)0x1) { o_qi_table("hwnd.qi", s); rel(s); }
            s = (void *)0x1;
            o_hr("null_hwnd", ((m2p)VT(dev, 26))(dev, NULL, &s));
            if (s && s != (void *)0x1) rel(s);
            oracle_case_end();
            rel(dev);
        }
        DestroyWindow(w);
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
    if (probe_access() == E_ACCESSDENIED && relay_interactive(L"SetuOracleDcompSurfaceDraw"))
        return 0;
    return run_cases();
}
