/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: DirectComposition targets and the visual tree.
 * CreateTargetForHwnd on own, hidden, child, layered, message-only,
 * destroyed and foreign windows (another thread, another process, the
 * desktop window), twice on one window and from two devices;
 * IDCompositionTarget::SetRoot; IDCompositionVisual AddVisual,
 * RemoveVisual and RemoveAllVisuals rules, reference counts and the
 * property setters' argument checks; the transform, clip, effect group,
 * animation and IDCompositionDevice3 effect objects with their
 * QueryInterface tables.
 *
 * Methods are called through the vtable by index in the order of the
 * documented interfaces (MinGW dcomp.h, C layout). Only HRESULTs,
 * reference counts and booleans are printed.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/dcomp/nf-dcomp-idcompositiondesktopdevice-createtargetforhwnd
 *       https://learn.microsoft.com/en-us/windows/win32/api/dcomp/nf-dcomp-idcompositiontarget-setroot
 *       https://learn.microsoft.com/en-us/windows/win32/api/dcomp/nf-dcomp-idcompositionvisual-addvisual
 *       https://learn.microsoft.com/en-us/windows/win32/api/dcomp/nf-dcomp-idcompositionvisual-removevisual
 *       https://learn.microsoft.com/en-us/windows/win32/api/dcomp/nf-dcomp-idcompositionvisual-setcontent
 *       https://learn.microsoft.com/en-us/windows/win32/api/dcomp/nn-dcomp-idcompositionvisual2
 *       https://learn.microsoft.com/en-us/windows/win32/api/dcomp/nn-dcomp-idcompositiondevice3
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

DEFINE_GUID(XIID_IDCompositionVisual2,       0xe8de1639, 0x4331, 0x4b26, 0xbc, 0x5f, 0x6a, 0x32, 0x1d, 0x34, 0x7a, 0x85);
DEFINE_GUID(XIID_IDCompositionVisual3,       0x2775f462, 0xb6c1, 0x4015, 0xb0, 0xbe, 0xb3, 0xe7, 0xd6, 0xa4, 0x97, 0x6d);
DEFINE_GUID(XIID_IDCompositionVisualDebug,   0xfed2b808, 0x5eb4, 0x43a0, 0xae, 0xa3, 0x35, 0xf6, 0x52, 0x80, 0xf9, 0x1b);
DEFINE_GUID(XIID_IDCompositionEffect,        0xec81b08f, 0xbfcb, 0x4e8d, 0xb1, 0x93, 0xa9, 0x15, 0x58, 0x79, 0x99, 0xe8);
DEFINE_GUID(XIID_IDCompositionTransform3D,   0x71185722, 0x246b, 0x41f2, 0xaa, 0xd1, 0x04, 0x43, 0xf7, 0xf4, 0xbf, 0xc2);
DEFINE_GUID(XIID_IDCompositionTransform,     0xfd55faa7, 0x37e0, 0x4c20, 0x95, 0xd2, 0x9b, 0xe4, 0x5b, 0xc3, 0x3f, 0x55);
DEFINE_GUID(XIID_IDCompositionTranslateTransform, 0x06791122, 0xc6f0, 0x417d, 0x83, 0x23, 0x26, 0x9e, 0x98, 0x7f, 0x59, 0x54);
DEFINE_GUID(XIID_IDCompositionScaleTransform, 0x71fde914, 0x40ef, 0x45ef, 0xbd, 0x51, 0x68, 0xb0, 0x37, 0xc3, 0x39, 0xf9);
DEFINE_GUID(XIID_IDCompositionRotateTransform, 0x641ed83c, 0xae96, 0x46c5, 0x90, 0xdc, 0x32, 0x77, 0x4c, 0xc5, 0xc6, 0xd5);
DEFINE_GUID(XIID_IDCompositionSkewTransform, 0xe57aa735, 0xdcdb, 0x4c72, 0x9c, 0x61, 0x05, 0x91, 0xf5, 0x88, 0x89, 0xee);
DEFINE_GUID(XIID_IDCompositionMatrixTransform, 0x16cdff07, 0xc503, 0x419c, 0x83, 0xf2, 0x09, 0x65, 0xc7, 0xaf, 0x1f, 0xa6);
DEFINE_GUID(XIID_IDCompositionTranslateTransform3D, 0x91636d4b, 0x9ba1, 0x4532, 0xaa, 0xf7, 0xe3, 0x34, 0x49, 0x94, 0xd7, 0x88);
DEFINE_GUID(XIID_IDCompositionScaleTransform3D, 0x2a9e9ead, 0x364b, 0x4b15, 0xa7, 0xc4, 0xa1, 0x99, 0x7f, 0x78, 0xb3, 0x89);
DEFINE_GUID(XIID_IDCompositionRotateTransform3D, 0xd8f5b23f, 0xd429, 0x4a91, 0xb5, 0x5a, 0xd2, 0xf4, 0x5f, 0xd7, 0x5b, 0x18);
DEFINE_GUID(XIID_IDCompositionMatrixTransform3D, 0x4b3363f0, 0x643b, 0x41b7, 0xb6, 0xe0, 0xcc, 0xf2, 0x2d, 0x34, 0x46, 0x7c);
DEFINE_GUID(XIID_IDCompositionEffectGroup,   0xa7929a74, 0xe6b2, 0x4bd6, 0x8b, 0x95, 0x40, 0x40, 0x11, 0x9c, 0xa3, 0x4d);
DEFINE_GUID(XIID_IDCompositionClip,          0x64ac3703, 0x9d3f, 0x45ec, 0xa1, 0x09, 0x7c, 0xac, 0x0e, 0x7a, 0x13, 0xa7);
DEFINE_GUID(XIID_IDCompositionRectangleClip, 0x9842ad7d, 0xd9cf, 0x4908, 0xae, 0xd7, 0x48, 0xb5, 0x1d, 0xa5, 0xe7, 0xc2);
DEFINE_GUID(XIID_IDCompositionAnimation,     0xcbfd91d9, 0x51b2, 0x45e4, 0xb3, 0xde, 0xd1, 0x9c, 0xcf, 0xb8, 0x63, 0xc5);
DEFINE_GUID(XIID_IDCompositionFilterEffect,  0x30c421d5, 0x8cb2, 0x4e9f, 0xb1, 0x33, 0x37, 0xbe, 0x27, 0x0d, 0x4a, 0xc2);
DEFINE_GUID(XIID_IDCompositionSaturationEffect, 0xa08debda, 0x3258, 0x4fa4, 0x9f, 0x16, 0x91, 0x74, 0xd3, 0xfe, 0x93, 0xb1);
DEFINE_GUID(XIID_IDCompositionGaussianBlurEffect, 0x45d4d0b7, 0x1bd4, 0x454e, 0x88, 0x94, 0x2b, 0xfa, 0x68, 0x44, 0x30, 0x33);

static create_fn create2, create3, create1;
static void o_hr(const char *k, HRESULT hr) { o_hex32(k, (DWORD)hr); }
typedef HRESULT (STDMETHODCALLTYPE *mf)(void *, float);
typedef HRESULT (STDMETHODCALLTYPE *mu)(void *, UINT);
typedef HRESULT (STDMETHODCALLTYPE *mtarget)(void *, HWND, BOOL, void **);
typedef HRESULT (STDMETHODCALLTYPE *madd)(void *, void *, BOOL, void *);
typedef HRESULT (STDMETHODCALLTYPE *mgroup)(void *, void **, UINT, void **);
static ULONG addref(void *p) { return ((mref)VT(p, 1))(p); }
/* the reference count, seen through AddRef/Release */
static ULONG refs(void *p) { addref(p); return rel(p); }

static const struct { const char *name; const GUID *iid; } iids[] = {
    { "IUnknown", &XIID_IUnknown },
    { "Visual", &XIID_IDCompositionVisual },
    { "Visual2", &XIID_IDCompositionVisual2 },
    { "Visual3", &XIID_IDCompositionVisual3 },
    { "VisualDebug", &XIID_IDCompositionVisualDebug },
    { "Target", &XIID_IDCompositionTarget },
    { "Effect", &XIID_IDCompositionEffect },
    { "Transform3D", &XIID_IDCompositionTransform3D },
    { "Transform", &XIID_IDCompositionTransform },
    { "Translate", &XIID_IDCompositionTranslateTransform },
    { "Scale", &XIID_IDCompositionScaleTransform },
    { "Rotate", &XIID_IDCompositionRotateTransform },
    { "Skew", &XIID_IDCompositionSkewTransform },
    { "Matrix", &XIID_IDCompositionMatrixTransform },
    { "Translate3D", &XIID_IDCompositionTranslateTransform3D },
    { "Scale3D", &XIID_IDCompositionScaleTransform3D },
    { "Rotate3D", &XIID_IDCompositionRotateTransform3D },
    { "Matrix3D", &XIID_IDCompositionMatrixTransform3D },
    { "EffectGroup", &XIID_IDCompositionEffectGroup },
    { "Clip", &XIID_IDCompositionClip },
    { "RectangleClip", &XIID_IDCompositionRectangleClip },
    { "Animation", &XIID_IDCompositionAnimation },
    { "FilterEffect", &XIID_IDCompositionFilterEffect },
    { "Saturation", &XIID_IDCompositionSaturationEffect },
    { "GaussianBlur", &XIID_IDCompositionGaussianBlurEffect },
    { "Device", &XIID_IDCompositionDevice },
    { "Device2", &XIID_IDCompositionDevice2 },
    { "IMarshal", &XIID_IMarshal },
    { "IAgileObject", &XIID_IAgileObject },
};
#define NIIDS (sizeof(iids) / sizeof(iids[0]))

/* the IIDs obj answers, as one string: "Name=self|unknown|other ..." */
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

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static HWND make_window(DWORD style, DWORD exstyle, HWND parent)
{
    static int registered;
    if (!registered) {
        WNDCLASSW wc;
        memset(&wc, 0, sizeof wc);
        wc.lpfnWndProc = wndproc;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.lpszClassName = L"SetuDcompTest";
        RegisterClassW(&wc);
        registered = 1;
    }
    return CreateWindowExW(exstyle, L"SetuDcompTest", L"dcomp", style, 0, 0, 200, 150, parent, NULL,
                           GetModuleHandleW(NULL), NULL);
}

/* desktop device (DCompositionCreateDevice2 with no rendering device) */
static void *new_desktop(void)
{
    void *dev = NULL;
    if (FAILED(create2(NULL, &XIID_IDCompositionDesktopDevice, &dev))) return NULL;
    return dev;
}

#define DD_CreateVisual 6
#define DD_CreateTranslateTransform 10
#define DD_CreateTransformGroup 15
#define DD_CreateTransform3DGroup 20
#define DD_CreateEffectGroup 21
#define DD_CreateRectangleClip 22
#define DD_CreateAnimation 23
#define DD_CreateTargetForHwnd 24
#define V_SetOffsetXAnim 3
#define V_SetOffsetX 4
#define V_SetTransformObj 7
#define V_SetTransform 8
#define V_SetTransformParent 9
#define V_SetEffect 10
#define V_SetBitmapInterpolationMode 11
#define V_SetBorderMode 12
#define V_SetClipObj 13
#define V_SetClip 14
#define V_SetContent 15
#define V_AddVisual 16
#define V_RemoveVisual 17
#define V_RemoveAllVisuals 18
#define V_SetCompositeMode 19
#define V_SetOpacityMode 20
#define V_SetBackFaceVisibility 21

static void *visual(void *dev)
{
    void *v = NULL;
    if (FAILED(((m1p)VT(dev, DD_CreateVisual))(dev, &v))) return NULL;
    return v;
}

static HRESULT target(void *dev, HWND hwnd, BOOL topmost, void **t)
{
    *t = (void *)0x1;
    return ((mtarget)VT(dev, DD_CreateTargetForHwnd))(dev, hwnd, topmost, t);
}

static void target_case(const char *name, void *dev, HWND hwnd, BOOL topmost)
{
    void *t;
    HRESULT hr = target(dev, hwnd, topmost, &t);
    oracle_case(name);
    o_hr("hr", hr);
    o_str("out", t == NULL ? "null" : t == (void *)0x1 ? "untouched" : "set");
    oracle_case_end();
    if (SUCCEEDED(hr)) rel(t);
}

/* a window in another process: this test started with " holdwnd <name>" */
static int hold_window(const wchar_t *name)
{
    HWND w = make_window(WS_OVERLAPPEDWINDOW, 0, NULL);
    HANDLE ev = OpenEventW(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, name);
    MSG msg;
    SetWindowTextW(w, name);
    SetEvent(ev);
    /* until the parent kills us */
    while (GetMessageW(&msg, NULL, 0, 0)) DispatchMessageW(&msg);
    return 0;
}

static DWORD WINAPI thread_window(void *arg)
{
    HANDLE *ev = arg;
    MSG msg;
    HWND w = make_window(WS_OVERLAPPEDWINDOW, 0, NULL);
    SetWindowTextW(w, L"SetuDcompThreadWindow");
    SetEvent(ev[0]);
    while (MsgWaitForMultipleObjects(1, &ev[1], FALSE, INFINITE, QS_ALLINPUT) != WAIT_OBJECT_0)
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
    DestroyWindow(w);
    return 0;
}

static void targets(void)
{
    void *dev = new_desktop(), *dev2 = new_desktop(), *v1 = NULL, *t, *t2;
    HWND w = make_window(WS_OVERLAPPEDWINDOW, 0, NULL);
    HRESULT hr;
    if (!dev || !dev2) return;

    hr = target(dev, w, TRUE, &t);
    oracle_case("target_own_topmost");
    o_hr("hr", hr);
    if (SUCCEEDED(hr)) { o_qi_table("qi", t); o_u32("refs", refs(t)); }
    oracle_case_end();
    target_case("target_own_topmost_again", dev, w, TRUE);
    target_case("target_own_topmost_other_device", dev2, w, TRUE);
    hr = target(dev, w, FALSE, &t2);
    oracle_case("target_own_bottom");
    o_hr("hr", hr);
    oracle_case_end();
    target_case("target_own_bottom_again", dev, w, FALSE);
    if (SUCCEEDED(hr)) rel(t2);
    target_case("target_own_bottom_after_release", dev, w, FALSE);
    target_case("target_own_bottom_after_release_other_device", dev2, w, FALSE);
    if (t && t != (void *)0x1) rel(t);
    target_case("target_own_topmost_after_release", dev2, w, TRUE);

    target_case("target_null_hwnd", dev, NULL, TRUE);
    target_case("target_bogus_hwnd", dev, (HWND)(ULONG_PTR)0x1234, TRUE);
    target_case("target_desktop_window", dev, GetDesktopWindow(), TRUE);
    {
        HWND hidden = make_window(WS_OVERLAPPED, 0, NULL);
        target_case("target_hidden_window", dev, hidden, TRUE);
        HWND child = make_window(WS_CHILD | WS_VISIBLE, 0, hidden);
        target_case("target_child_window", dev, child, TRUE);
        HWND msgonly = make_window(0, 0, HWND_MESSAGE);
        target_case("target_message_only", dev, msgonly, TRUE);
        HWND layered = make_window(WS_POPUP, WS_EX_LAYERED, NULL);
        target_case("target_layered", dev, layered, TRUE);
        HWND noredir = make_window(WS_POPUP, WS_EX_NOREDIRECTIONBITMAP, NULL);
        target_case("target_noredirection", dev, noredir, TRUE);
        HWND gone = make_window(WS_POPUP, 0, NULL);
        DestroyWindow(gone);
        target_case("target_destroyed_window", dev, gone, TRUE);
    }
    {   /* a window of another thread */
        HANDLE ev[2] = { CreateEventW(NULL, FALSE, FALSE, NULL), CreateEventW(NULL, FALSE, FALSE, NULL) };
        HANDLE th = CreateThread(NULL, 0, thread_window, ev, 0, NULL);
        WaitForSingleObject(ev[0], 10000);
        target_case("target_other_thread", dev, FindWindowW(L"SetuDcompTest", L"SetuDcompThreadWindow"), TRUE);
        SetEvent(ev[1]);
        WaitForSingleObject(th, 10000);
        CloseHandle(th);
    }
    {   /* a window of another process */
        wchar_t exe[MAX_PATH], cmd[MAX_PATH + 64];
        const wchar_t *name = L"SetuDcompHoldWindow";
        HANDLE ev = CreateEventW(NULL, TRUE, FALSE, name);
        STARTUPINFOW si;
        PROCESS_INFORMATION pi;
        memset(&si, 0, sizeof si);
        si.cb = sizeof si;
        GetModuleFileNameW(NULL, exe, MAX_PATH);
        swprintf(cmd, MAX_PATH + 64, L"\"%ls\" holdwnd %ls", exe, name);
        if (CreateProcessW(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
            WaitForSingleObject(ev, 10000);
            target_case("target_other_process", dev, FindWindowW(L"SetuDcompTest", name), TRUE);
            TerminateProcess(pi.hProcess, 0);
            WaitForSingleObject(pi.hProcess, 10000);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
        CloseHandle(ev);
    }
    {   /* the v1 device's CreateTargetForHwnd (index 6) */
        void *t1 = (void *)0x1;
        HWND w1 = make_window(WS_OVERLAPPEDWINDOW, 0, NULL);
        if (SUCCEEDED(create1(NULL, &XIID_IDCompositionDevice, &v1))) {
            hr = ((mtarget)VT(v1, 6))(v1, w1, TRUE, &t1);
            oracle_case("target_v1_device");
            o_hr("hr", hr);
            oracle_case_end();
            target_case("target_v1_then_desktop_same_window", dev, w1, TRUE);
            if (SUCCEEDED(hr)) rel(t1);
            rel(v1);
        }
    }
    {   /* SetRoot */
        HWND wa = make_window(WS_OVERLAPPEDWINDOW, 0, NULL), wb = make_window(WS_OVERLAPPEDWINDOW, 0, NULL);
        void *ta = NULL, *tb = NULL, *va = visual(dev), *vb = visual(dev), *vc = visual(dev), *vo = visual(dev2);
        target(dev, wa, TRUE, &ta);
        target(dev, wb, TRUE, &tb);
        oracle_case("setroot");
        o_hr("null_first", ((m1p)VT(ta, 3))(ta, NULL));
        o_u32("va_refs_before", refs(va));
        o_hr("visual", ((m1p)VT(ta, 3))(ta, va));
        o_u32("va_refs_after", refs(va));
        o_hr("same_again", ((m1p)VT(ta, 3))(ta, va));
        o_hr("root_of_other_target", ((m1p)VT(tb, 3))(tb, va));
        o_hr("add_root_as_child", ((madd)VT(vb, V_AddVisual))(vb, va, TRUE, NULL));
        o_hr("child_add", ((madd)VT(vb, V_AddVisual))(vb, vc, TRUE, NULL));
        o_hr("child_as_root", ((m1p)VT(tb, 3))(tb, vc));
        o_hr("other_device_visual", ((m1p)VT(tb, 3))(tb, vo));
        o_hr("replace", ((m1p)VT(ta, 3))(ta, vb));
        o_u32("va_refs_replaced", refs(va));
        o_hr("old_root_elsewhere", ((m1p)VT(tb, 3))(tb, va));
        o_hr("null", ((m1p)VT(tb, 3))(tb, NULL));
        o_hr("commit", ((m0)VT(dev, 3))(dev));
        o_u32("vb_refs", refs(vb));
        rel(ta);
        o_u32("vb_refs_target_released", refs(vb));
        oracle_case_end();
        rel(tb); rel(va); rel(vb); rel(vc); rel(vo);
    }
    rel(dev);
    rel(dev2);
}

static void visuals(void)
{
    void *dev = new_desktop(), *dev2 = new_desktop();
    void *p, *c, *c2, *c3, *other, *v1dev = NULL;
    D2D1_MATRIX_3X2_F m = { { { 1, 0, 0, 1, 10, 20 } } };
    D2D1_RECT_F r = { 0, 0, 10, 10 };
    if (!dev || !dev2) return;

    p = visual(dev);
    oracle_case("visual_create");
    o_bool("ok", p != NULL);
    if (p) { o_qi_table("qi", p); o_u32("refs", refs(p)); }
    oracle_case_end();
    if (!p) return;
    if (SUCCEEDED(create1(NULL, &XIID_IDCompositionDevice, &v1dev))) {
        void *vv = NULL;
        HRESULT hr = ((m1p)VT(v1dev, 7))(v1dev, &vv);
        oracle_case("visual_create_v1_device");
        o_hr("hr", hr);
        if (SUCCEEDED(hr)) { o_qi_table("qi", vv); rel(vv); }
        oracle_case_end();
    }

    c = visual(dev); c2 = visual(dev); c3 = visual(dev); other = visual(dev2);
    oracle_case("visual_tree");
    o_u32("c_refs", refs(c));
    o_hr("add", ((madd)VT(p, V_AddVisual))(p, c, TRUE, NULL));
    o_u32("c_refs_added", refs(c));
    o_hr("add_again", ((madd)VT(p, V_AddVisual))(p, c, TRUE, NULL));
    o_hr("add_self", ((madd)VT(p, V_AddVisual))(p, p, TRUE, NULL));
    o_hr("add_null", ((madd)VT(p, V_AddVisual))(p, NULL, TRUE, NULL));
    o_hr("add_parent_to_child", ((madd)VT(c, V_AddVisual))(c, p, TRUE, NULL));
    o_hr("add_other_device", ((madd)VT(p, V_AddVisual))(p, other, TRUE, NULL));
    o_hr("add_ref_not_child", ((madd)VT(p, V_AddVisual))(p, c2, TRUE, c3));
    o_hr("add_above_ref", ((madd)VT(p, V_AddVisual))(p, c2, TRUE, c));
    o_hr("add_below_ref", ((madd)VT(p, V_AddVisual))(p, c3, FALSE, c));
    o_hr("add_child_of_other_parent", ((madd)VT(c, V_AddVisual))(c, c2, TRUE, NULL));
    o_hr("add_ref_self", ((madd)VT(c, V_AddVisual))(c, c, FALSE, c));
    o_hr("remove_not_child", ((m1p)VT(c, V_RemoveVisual))(c, c3));
    o_hr("remove_null", ((m1p)VT(p, V_RemoveVisual))(p, NULL));
    o_hr("remove", ((m1p)VT(p, V_RemoveVisual))(p, c));
    o_u32("c_refs_removed", refs(c));
    o_hr("remove_again", ((m1p)VT(p, V_RemoveVisual))(p, c));
    o_hr("readd_to_child", ((madd)VT(c2, V_AddVisual))(c2, c, FALSE, NULL));
    o_hr("remove_all", ((m0)VT(p, V_RemoveAllVisuals))(p));
    o_u32("c2_refs_removed_all", refs(c2));
    o_u32("c_refs_grandchild", refs(c));
    o_hr("remove_all_empty", ((m0)VT(p, V_RemoveAllVisuals))(p));
    o_hr("add_after_remove_all", ((madd)VT(p, V_AddVisual))(p, c2, TRUE, NULL));
    o_hr("commit", ((m0)VT(dev, 3))(dev));
    oracle_case_end();

    oracle_case("visual_setters");
    o_hr("offset_x", ((mf)VT(p, V_SetOffsetX))(p, 5.5f));
    o_hr("offset_x_nan", ((mf)VT(p, V_SetOffsetX))(p, __builtin_nanf("")));
    o_hr("offset_x_inf", ((mf)VT(p, V_SetOffsetX))(p, __builtin_inff()));
    o_hr("offset_x_null_anim", ((m1p)VT(p, V_SetOffsetXAnim))(p, NULL));
    o_hr("transform_matrix", ((m1p)VT(p, V_SetTransform))(p, &m));
    o_hr("transform_null_obj", ((m1p)VT(p, V_SetTransformObj))(p, NULL));
    o_hr("transform_parent_null", ((m1p)VT(p, V_SetTransformParent))(p, NULL));
    o_hr("transform_parent_child", ((m1p)VT(p, V_SetTransformParent))(p, c2));
    o_hr("transform_parent_self", ((m1p)VT(p, V_SetTransformParent))(p, p));
    o_hr("transform_parent_other_device", ((m1p)VT(p, V_SetTransformParent))(p, other));
    o_hr("effect_null", ((m1p)VT(p, V_SetEffect))(p, NULL));
    for (int i = 0; i < 4; i++) {
        char k[48];
        snprintf(k, sizeof k, "interpolation_%d", i - 1);
        o_hr(k, ((mu)VT(p, V_SetBitmapInterpolationMode))(p, (UINT)(i - 1)));
        snprintf(k, sizeof k, "border_%d", i - 1);
        o_hr(k, ((mu)VT(p, V_SetBorderMode))(p, (UINT)(i - 1)));
    }
    for (int i = 0; i < 5; i++) {
        char k[48];
        snprintf(k, sizeof k, "composite_%d", i - 1);
        o_hr(k, ((mu)VT(p, V_SetCompositeMode))(p, (UINT)(i - 1)));
        snprintf(k, sizeof k, "opacity_mode_%d", i - 1);
        o_hr(k, ((mu)VT(p, V_SetOpacityMode))(p, (UINT)(i - 1)));
        snprintf(k, sizeof k, "backface_%d", i - 1);
        o_hr(k, ((mu)VT(p, V_SetBackFaceVisibility))(p, (UINT)(i - 1)));
    }
    o_hr("clip_rect", ((m1p)VT(p, V_SetClip))(p, &r));
    o_hr("clip_null_obj", ((m1p)VT(p, V_SetClipObj))(p, NULL));
    o_hr("content_null", ((m1p)VT(p, V_SetContent))(p, NULL));
    o_hr("content_visual", ((m1p)VT(p, V_SetContent))(p, c3));
    o_hr("content_device", ((m1p)VT(p, V_SetContent))(p, dev));
    o_hr("commit", ((m0)VT(dev, 3))(dev));
    oracle_case_end();

    /* transform, clip, effect and animation objects */
    {
        static const char *names[] = { "translate", "scale", "rotate", "skew", "matrix" };
        static const char *names3d[] = { "translate3d", "scale3d", "rotate3d", "matrix3d" };
        void *objs[5] = { 0 }, *objs3d[4] = { 0 }, *g, *o;
        HRESULT hr;
        char k[48];
        oracle_case("objects");
        for (int i = 0; i < 5; i++) {
            hr = ((m1p)VT(dev, DD_CreateTranslateTransform + i))(dev, &objs[i]);
            snprintf(k, sizeof k, "%s", names[i]);
            o_hr(k, hr);
            snprintf(k, sizeof k, "%s.qi", names[i]);
            if (SUCCEEDED(hr)) o_qi_table(k, objs[i]);
        }
        for (int i = 0; i < 4; i++) {
            hr = ((m1p)VT(dev, DD_CreateTranslateTransform + 6 + i))(dev, &objs3d[i]);
            o_hr(names3d[i], hr);
            snprintf(k, sizeof k, "%s.qi", names3d[i]);
            if (SUCCEEDED(hr)) o_qi_table(k, objs3d[i]);
        }
        g = (void *)0x1;
        hr = ((mgroup)VT(dev, DD_CreateTransformGroup))(dev, objs, 5, &g);
        o_hr("group", hr);
        if (SUCCEEDED(hr)) { o_qi_table("group.qi", g); o_hr("visual_set_group", ((m1p)VT(p, V_SetTransformObj))(p, g)); rel(g); }
        g = (void *)0x1;
        hr = ((mgroup)VT(dev, DD_CreateTransformGroup))(dev, objs, 0, &g);
        o_hr("group_empty", hr);
        o_bool("group_empty_out_null", g == NULL);
        if (SUCCEEDED(hr) && g && g != (void *)0x1) rel(g);
        g = (void *)0x1;
        hr = ((mgroup)VT(dev, DD_CreateTransform3DGroup))(dev, objs3d, 4, &g);
        o_hr("group3d", hr);
        if (SUCCEEDED(hr)) { o_qi_table("group3d.qi", g); rel(g); }
        o_hr("visual_set_transform", ((m1p)VT(p, V_SetTransformObj))(p, objs[0]));
        o_hr("visual_set_effect_3d", ((m1p)VT(p, V_SetEffect))(p, objs3d[0]));
        o_hr("visual_set_effect_2d", ((m1p)VT(p, V_SetEffect))(p, objs[0]));
        o = NULL;
        hr = ((m1p)VT(dev, DD_CreateEffectGroup))(dev, &o);
        o_hr("effect_group", hr);
        if (SUCCEEDED(hr)) { o_qi_table("effect_group.qi", o); o_hr("visual_set_effect_group", ((m1p)VT(p, V_SetEffect))(p, o)); rel(o); }
        o = NULL;
        hr = ((m1p)VT(dev, DD_CreateRectangleClip))(dev, &o);
        o_hr("clip", hr);
        if (SUCCEEDED(hr)) { o_qi_table("clip.qi", o); o_hr("visual_set_clip", ((m1p)VT(p, V_SetClipObj))(p, o)); rel(o); }
        o = NULL;
        hr = ((m1p)VT(dev, DD_CreateAnimation))(dev, &o);
        o_hr("animation", hr);
        if (SUCCEEDED(hr)) {
            o_qi_table("animation.qi", o);
            o_hr("visual_offset_anim_empty", ((m1p)VT(p, V_SetOffsetXAnim))(p, o));
            rel(o);
        }
        for (int i = 0; i < 5; i++) rel(objs[i]);
        for (int i = 0; i < 4; i++) rel(objs3d[i]);
        o_hr("commit", ((m0)VT(dev, 3))(dev));
        oracle_case_end();
    }

    /* IDCompositionDevice3's effect factories (indices 24..36) */
    {
        void *d3dev = NULL, *dev3 = NULL;
        static const char *names[] = { "gaussian_blur", "brightness", "color_matrix", "shadow", "hue_rotation",
                                       "saturation", "turbulence", "linear_transfer", "table_transfer",
                                       "composite", "blend", "arithmetic_composite", "affine_transform_2d" };
        if (SUCCEEDED(create3(NULL, &XIID_IDCompositionDesktopDevice, &d3dev)) &&
            SUCCEEDED(qi(d3dev, &XIID_IDCompositionDevice3, &dev3))) {
            void *pv = visual(d3dev);
            oracle_case("effects");
            for (int i = 0; i < 13; i++) {
                void *e = NULL;
                char k[64];
                HRESULT hr = ((m1p)VT(dev3, 24 + i))(dev3, &e);
                o_hr(names[i], hr);
                if (SUCCEEDED(hr) && e) {
                    snprintf(k, sizeof k, "%s.qi", names[i]);
                    o_qi_table(k, e);
                    if (pv) {
                        snprintf(k, sizeof k, "%s.set_effect", names[i]);
                        o_hr(k, ((m1p)VT(pv, V_SetEffect))(pv, e));
                    }
                    rel(e);
                }
            }
            oracle_case_end();
            rel(pv);
            rel(dev3);
        }
        rel(d3dev);
    }
    {   /* releasing a parent: what happens to its children's reference */
        void *a = visual(dev), *b = visual(dev);
        oracle_case("release_parent");
        ((madd)VT(a, V_AddVisual))(a, b, TRUE, NULL);
        o_u32("child_refs", refs(b));
        o_u32("parent_final", rel(a));
        o_u32("child_refs_after", refs(b));
        o_hr("add_to_new_parent", ((madd)VT(p, V_AddVisual))(p, b, TRUE, NULL));
        oracle_case_end();
        rel(b);
    }
    oracle_case("release_device_first");
    o_u32("device_final", rel(dev2));
    o_u32("visual_refs", refs(other));
    o_hr("visual_offset", ((mf)VT(other, V_SetOffsetX))(other, 1.0f));
    oracle_case_end();
    rel(other);
    rel(c); rel(c2); rel(c3); rel(p);
    if (v1dev) rel(v1dev);
    rel(dev);
}

static int run_cases(void)
{
    HMODULE dcomp;
    oracle_begin("dcomp/target_visual");
    dcomp = LoadLibraryW(L"dcomp.dll");
    create1 = (create_fn)(void (*)(void))GetProcAddress(dcomp, "DCompositionCreateDevice");
    create2 = (create_fn)(void (*)(void))GetProcAddress(dcomp, "DCompositionCreateDevice2");
    create3 = (create_fn)(void (*)(void))GetProcAddress(dcomp, "DCompositionCreateDevice3");
    {
        void *dev = NULL;
        HRESULT hr = create2 ? create2(NULL, &XIID_IDCompositionDesktopDevice, &dev) : E_FAIL;
        oracle_case("device");
        o_hr("hr", hr);
        oracle_case_end();
        if (FAILED(hr)) return oracle_end();
        rel(dev);
    }
    visuals();
    targets();
    return oracle_end();
}

int main(void)
{
    const wchar_t *child = wcsstr(GetCommandLineW(), L" child ");
    const wchar_t *hold = wcsstr(GetCommandLineW(), L" holdwnd ");
    if (hold) return hold_window(hold + 9);
    if (child) {
        child_setup(child + 7);
        run_cases();
        child_finish();
        return 0;
    }
    if (probe_access() == E_ACCESSDENIED && relay_interactive(L"SetuOracleDcompTargetVisual"))
        return 0;
    return run_cases();
}
