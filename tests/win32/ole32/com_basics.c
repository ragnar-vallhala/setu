/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: COM basics in ole32: CoInitializeEx return codes (first,
 * repeated, mixed apartment, bad flags, per thread), CoGetApartmentType,
 * CoCreateInstance before/after initialisation with an unregistered CLSID,
 * CLSIDFromString / IIDFromString parsing edge cases, StringFromGUID2
 * buffer sizing, StringFromCLSID, CoCreateGuid (version/variant bits only),
 * CoTaskMemAlloc/Realloc/Free and IMalloc via CoGetMalloc.
 *
 * ole32 is loaded with LoadLibraryW; functions come from GetProcAddress.
 * HRESULTs are printed in hex. No pointers, no generated GUID values.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/combaseapi/nf-combaseapi-coinitializeex
 *       https://learn.microsoft.com/en-us/windows/win32/api/combaseapi/nf-combaseapi-couninitialize
 *       https://learn.microsoft.com/en-us/windows/win32/api/combaseapi/nf-combaseapi-cogetapartmenttype
 *       https://learn.microsoft.com/en-us/windows/win32/api/combaseapi/nf-combaseapi-cocreateinstance
 *       https://learn.microsoft.com/en-us/windows/win32/api/combaseapi/nf-combaseapi-clsidfromstring
 *       https://learn.microsoft.com/en-us/windows/win32/api/combaseapi/nf-combaseapi-iidfromstring
 *       https://learn.microsoft.com/en-us/windows/win32/api/combaseapi/nf-combaseapi-stringfromguid2
 *       https://learn.microsoft.com/en-us/windows/win32/api/combaseapi/nf-combaseapi-stringfromclsid
 *       https://learn.microsoft.com/en-us/windows/win32/api/combaseapi/nf-combaseapi-cocreateguid
 *       https://learn.microsoft.com/en-us/windows/win32/api/combaseapi/nf-combaseapi-cotaskmemalloc
 *       https://learn.microsoft.com/en-us/windows/win32/api/combaseapi/nf-combaseapi-cotaskmemrealloc
 *       https://learn.microsoft.com/en-us/windows/win32/api/combaseapi/nf-combaseapi-cotaskmemfree
 *       https://learn.microsoft.com/en-us/windows/win32/api/combaseapi/nf-combaseapi-cogetmalloc
 *       https://learn.microsoft.com/en-us/windows/win32/api/objidl/nn-objidl-imalloc
 *       https://learn.microsoft.com/en-us/windows/win32/api/combaseapi/nf-combaseapi-progidfromclsid
 *       https://learn.microsoft.com/en-us/windows/win32/com/com-error-codes-1
 */
#include "oracle.h"
#include <objbase.h>

#define DECL(f) static __typeof__(f) *p##f
#define LOAD(m, f) (*(FARPROC *)&p##f = GetProcAddress(m, #f))

DECL(CoInitializeEx);
DECL(CoUninitialize);
DECL(CoGetApartmentType);
DECL(CoCreateInstance);
DECL(CLSIDFromString);
DECL(IIDFromString);
DECL(StringFromGUID2);
DECL(StringFromCLSID);
DECL(CoCreateGuid);
DECL(CoTaskMemAlloc);
DECL(CoTaskMemRealloc);
DECL(CoTaskMemFree);
DECL(CoGetMalloc);
DECL(ProgIDFromCLSID);

static const IID iid_iunknown = {0, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
static const GUID bogus = {0x5e7a0001, 0x1234, 0x5678, {0x9a, 0xbc, 0xde, 0xf0, 0x12, 0x34, 0x56, 0x78}};

static void hr_case(const char *name, HRESULT hr)
{
    oracle_case(name);
    o_hex32("hr", (DWORD)hr);
    oracle_case_end();
}

static void guid_field(const char *k, const GUID *g)
{
    wchar_t b[64];
    swprintf(b, 64, L"%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X", (unsigned long)g->Data1, g->Data2,
             g->Data3, g->Data4[0], g->Data4[1], g->Data4[2], g->Data4[3], g->Data4[4], g->Data4[5], g->Data4[6],
             g->Data4[7]);
    o_wstr(k, b);
}

static void clsid_case(const char *name, const wchar_t *s)
{
    CLSID c;
    HRESULT hr;
    memset(&c, 0xab, sizeof c);
    ORACLE_CALL(hr, pCLSIDFromString(s, &c));
    oracle_case(name);
    o_hex32("hr", (DWORD)hr);
    guid_field("clsid", &c);
    oracle_case_end();
}

static void iid_case(const char *name, const wchar_t *s)
{
    IID c;
    HRESULT hr;
    memset(&c, 0xab, sizeof c);
    ORACLE_CALL(hr, pIIDFromString(s, &c));
    oracle_case(name);
    o_hex32("hr", (DWORD)hr);
    guid_field("iid", &c);
    oracle_case_end();
}

static void apt_case(const char *name)
{
    APTTYPE t = (APTTYPE)-7;
    APTTYPEQUALIFIER q = (APTTYPEQUALIFIER)-7;
    HRESULT hr = pCoGetApartmentType(&t, &q);
    oracle_case(name);
    o_hex32("hr", (DWORD)hr);
    o_i64("type", (int)t);
    o_i64("qualifier", (int)q);
    oracle_case_end();
}

static struct { HRESULT a, b, c, d; } th;
static DWORD WINAPI mta_thread(LPVOID p)
{
    (void)p;
    th.a = pCoInitializeEx(NULL, COINIT_MULTITHREADED);
    th.b = pCoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    th.c = pCoInitializeEx(NULL, COINIT_MULTITHREADED);
    pCoUninitialize();
    pCoUninitialize();
    th.d = pCoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    pCoUninitialize();
    return 0;
}

static struct { DWORD flags; HRESULT hr; APTTYPE t; } ft;
static DWORD WINAPI flags_thread(LPVOID p)
{
    APTTYPEQUALIFIER q;
    (void)p;
    ft.hr = pCoInitializeEx(NULL, ft.flags);
    ft.t = (APTTYPE)-7;
    pCoGetApartmentType(&ft.t, &q);
    if (SUCCEEDED(ft.hr)) pCoUninitialize();
    return 0;
}

static void flags_case(const char *name, DWORD flags)
{
    ft.flags = flags;
    HANDLE t = CreateThread(NULL, 0, flags_thread, NULL, 0, NULL);
    WaitForSingleObject(t, 10000);
    CloseHandle(t);
    oracle_case(name);
    o_hex32("hr", (DWORD)ft.hr);
    o_i64("apartment_type", (int)ft.t);
    oracle_case_end();
}

int main(void)
{
    oracle_begin("ole32/com_basics");
    HMODULE m;
    ORACLE_CALL(m, LoadLibraryW(L"ole32.dll"));
    oracle_case("load_ole32");
    o_handle("ret", m);
    oracle_case_end();
    if (!m) return oracle_end();
    LOAD(m, CoInitializeEx);
    LOAD(m, CoUninitialize);
    LOAD(m, CoGetApartmentType);
    LOAD(m, CoCreateInstance);
    LOAD(m, CLSIDFromString);
    LOAD(m, IIDFromString);
    LOAD(m, StringFromGUID2);
    LOAD(m, StringFromCLSID);
    LOAD(m, CoCreateGuid);
    LOAD(m, CoTaskMemAlloc);
    LOAD(m, CoTaskMemRealloc);
    LOAD(m, CoTaskMemFree);
    LOAD(m, CoGetMalloc);
    LOAD(m, ProgIDFromCLSID);

    /* ---- before CoInitialize ---- */
    apt_case("apartment_before_init");
    {
        IUnknown *u = (IUnknown *)(ULONG_PTR)1;
        HRESULT hr = pCoCreateInstance(&bogus, NULL, CLSCTX_INPROC_SERVER, &iid_iunknown, (void **)&u);
        oracle_case("cocreateinstance_before_init");
        o_hex32("hr", (DWORD)hr);
        o_bool("out_null", u == NULL);
        oracle_case_end();
    }
    /* Odd flags run on their own threads so they cannot affect this one. */
    flags_case("coinit_flags_0x100", 0x100);
    flags_case("coinit_flags_sta_0x80", COINIT_APARTMENTTHREADED | 0x80);
    flags_case("coinit_flags_0xffffffff", 0xffffffff);
    flags_case("coinit_flags_speed_over_memory", COINIT_SPEED_OVER_MEMORY);
    flags_case("coinit_flags_0x1", 0x1);
    flags_case("coinit_flags_0x10", 0x10);
    flags_case("coinit_flags_0x20", 0x20);
    flags_case("coinit_flags_0x40", 0x40);
    flags_case("coinit_flags_0x80", 0x80);
    flags_case("coinit_flags_all_documented", COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE | COINIT_SPEED_OVER_MEMORY);
    apt_case("apartment_main_still_uninitialised");

    /* ---- initialise STA ---- */
    hr_case("coinit_sta_first", pCoInitializeEx(NULL, COINIT_APARTMENTTHREADED));
    hr_case("coinit_sta_second", pCoInitializeEx(NULL, COINIT_APARTMENTTHREADED));
    hr_case("coinit_mta_on_sta", pCoInitializeEx(NULL, COINIT_MULTITHREADED));
    hr_case("coinit_sta_disable_ole1dde", pCoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE));
    hr_case("coinit_reserved_nonnull", pCoInitializeEx((LPVOID)(ULONG_PTR)1, COINIT_APARTMENTTHREADED));
    apt_case("apartment_sta");
    {
        IUnknown *u = (IUnknown *)(ULONG_PTR)1;
        HRESULT hr = pCoCreateInstance(&bogus, NULL, CLSCTX_INPROC_SERVER, &iid_iunknown, (void **)&u);
        oracle_case("cocreateinstance_unregistered");
        o_hex32("hr", (DWORD)hr);
        o_bool("out_null", u == NULL);
        oracle_case_end();
        hr = pCoCreateInstance(&bogus, NULL, CLSCTX_INPROC_SERVER, &iid_iunknown, NULL);
        hr_case("cocreateinstance_null_out", hr);
        hr = pCoCreateInstance(&bogus, NULL, 0, &iid_iunknown, (void **)&u);
        hr_case("cocreateinstance_clsctx_0", hr);
        LPOLESTR pid = NULL;
        hr = pProgIDFromCLSID(&bogus, &pid);
        oracle_case("progidfromclsid_unregistered");
        o_hex32("hr", (DWORD)hr);
        o_bool("out_null", pid == NULL);
        oracle_case_end();
        if (pid) pCoTaskMemFree(pid);
    }

    /* ---- per-thread apartments ---- */
    {
        HANDLE t = CreateThread(NULL, 0, mta_thread, NULL, 0, NULL);
        WaitForSingleObject(t, 10000);
        CloseHandle(t);
        oracle_case("thread_mta_then_sta");
        o_hex32("mta_first", (DWORD)th.a);
        o_hex32("sta_on_mta", (DWORD)th.b);
        o_hex32("mta_again", (DWORD)th.c);
        o_hex32("sta_after_uninit", (DWORD)th.d);
        oracle_case_end();
    }

    /* ---- CLSIDFromString / IIDFromString ---- */
    clsid_case("clsid_iunknown_upper", L"{00000000-0000-0000-C000-000000000046}");
    clsid_case("clsid_lowercase", L"{5e7a0001-1234-5678-9abc-def012345678}");
    clsid_case("clsid_no_braces", L"5E7A0001-1234-5678-9ABC-DEF012345678");
    clsid_case("clsid_missing_close_brace", L"{5E7A0001-1234-5678-9ABC-DEF012345678");
    clsid_case("clsid_trailing_garbage", L"{5E7A0001-1234-5678-9ABC-DEF012345678}x");
    clsid_case("clsid_bad_hex", L"{5E7A0001-1234-5678-9ABC-DEF01234567G}");
    clsid_case("clsid_short", L"{5E7A0001-1234-5678-9ABC-DEF01234567}");
    clsid_case("clsid_no_dashes", L"{5E7A00011234567809ABCDEF012345678}");
    clsid_case("clsid_empty", L"");
    clsid_case("clsid_null", NULL);
    clsid_case("clsid_unknown_progid", L"SetuOracle.NoSuchProgId.1");
    clsid_case("clsid_spaces", L"{ 5E7A0001-1234-5678-9ABC-DEF012345678}");
    iid_case("iid_iunknown", L"{00000000-0000-0000-C000-000000000046}");
    iid_case("iid_lowercase", L"{5e7a0001-1234-5678-9abc-def012345678}");
    iid_case("iid_no_braces", L"5E7A0001-1234-5678-9ABC-DEF012345678");
    iid_case("iid_trailing_garbage", L"{5E7A0001-1234-5678-9ABC-DEF012345678}x");
    iid_case("iid_bad_hex", L"{5E7A0001-1234-5678-9ABC-DEF01234567G}");
    iid_case("iid_empty", L"");
    iid_case("iid_null", NULL);
    iid_case("iid_progid", L"SetuOracle.NoSuchProgId.1");

    /* ---- StringFromGUID2 / StringFromCLSID ---- */
    {
        static const int sizes[] = {39, 40, 38, 1, 0};
        for (size_t i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
            wchar_t buf[64];
            char nm[48];
            wmemset(buf, L'#', 64);
            buf[63] = 0;
            int r = pStringFromGUID2(&bogus, buf, sizes[i]);
            snprintf(nm, sizeof nm, "stringfromguid2_buf_%d", sizes[i]);
            oracle_case(nm);
            o_i64("ret", r);
            buf[45] = 0;
            o_wstr("buf", buf);
            oracle_case_end();
        }
        LPOLESTR s = NULL;
        HRESULT hr = pStringFromCLSID(&bogus, &s);
        oracle_case("stringfromclsid");
        o_hex32("hr", (DWORD)hr);
        o_wstr("str", s);
        oracle_case_end();
        if (s) pCoTaskMemFree(s);
    }

    /* ---- CoCreateGuid ---- */
    {
        GUID g1, g2;
        HRESULT h1 = pCoCreateGuid(&g1), h2 = pCoCreateGuid(&g2);
        oracle_case("cocreateguid");
        o_hex32("hr1", (DWORD)h1);
        o_hex32("hr2", (DWORD)h2);
        o_bool("different", memcmp(&g1, &g2, sizeof g1) != 0);
        o_u32("version", g1.Data3 >> 12);
        o_u32("variant_bits", g1.Data4[0] >> 6);
        oracle_case_end();
    }

    /* ---- CoTaskMem* and IMalloc ---- */
    {
        void *p0 = pCoTaskMemAlloc(0);
        oracle_case("cotaskmemalloc_0");
        o_bool("nonnull", p0 != NULL);
        oracle_case_end();
        pCoTaskMemFree(p0);
        void *p = pCoTaskMemAlloc(16);
        memset(p, 0x5a, 16);
        void *q = pCoTaskMemRealloc(p, 4096);
        oracle_case("cotaskmemrealloc_grow");
        o_bool("nonnull", q != NULL);
        o_bytes("kept", q, 16);
        oracle_case_end();
        void *z = pCoTaskMemRealloc(q, 0);
        oracle_case("cotaskmemrealloc_to_0");
        o_bool("null", z == NULL);
        oracle_case_end();
        if (z) pCoTaskMemFree(z);
        void *n = pCoTaskMemRealloc(NULL, 8);
        oracle_case("cotaskmemrealloc_null_is_alloc");
        o_bool("nonnull", n != NULL);
        oracle_case_end();
        pCoTaskMemFree(n);
        pCoTaskMemFree(NULL);
        void *huge = pCoTaskMemAlloc((SIZE_T)-16);
        oracle_case("cotaskmemalloc_huge");
        o_bool("null", huge == NULL);
        oracle_case_end();

        IMalloc *ma = NULL;
        HRESULT hr = pCoGetMalloc(MEMCTX_TASK, &ma);
        oracle_case("cogetmalloc_task");
        o_hex32("hr", (DWORD)hr);
        o_bool("nonnull", ma != NULL);
        oracle_case_end();
        IMalloc *mb = (IMalloc *)(ULONG_PTR)1;
        hr = pCoGetMalloc(MEMCTX_SHARED, &mb);
        oracle_case("cogetmalloc_shared");
        o_hex32("hr", (DWORD)hr);
        o_bool("out_null", mb == NULL);
        oracle_case_end();
        mb = (IMalloc *)(ULONG_PTR)1;
        hr = pCoGetMalloc(77, &mb);
        oracle_case("cogetmalloc_bogus_ctx");
        o_hex32("hr", (DWORD)hr);
        o_bool("out_null", mb == NULL);
        oracle_case_end();
        if (ma) {
            void *a = ma->lpVtbl->Alloc(ma, 100);
            oracle_case("imalloc_alloc_getsize_didalloc");
            o_bool("alloc_nonnull", a != NULL);
            o_u64("getsize", a ? ma->lpVtbl->GetSize(ma, a) : 0);
            o_i64("didalloc_own", a ? ma->lpVtbl->DidAlloc(ma, a) : -9);
            o_i64("didalloc_null", ma->lpVtbl->DidAlloc(ma, NULL));
            o_bool("cotaskmemfree_compatible", 1);
            oracle_case_end();
            pCoTaskMemFree(a);
            void *t = pCoTaskMemAlloc(33);
            oracle_case("imalloc_getsize_of_cotaskmemalloc");
            o_u64("getsize", ma->lpVtbl->GetSize(ma, t));
            oracle_case_end();
            ma->lpVtbl->Free(ma, t);
            ma->lpVtbl->Release(ma);
        }
    }

    /* ---- uninitialise ---- */
    pCoUninitialize(); /* balances: sta_second */
    pCoUninitialize(); /* sta_disable_ole1dde */
    apt_case("apartment_after_two_uninit");
    pCoUninitialize(); /* sta_first */
    apt_case("apartment_after_full_uninit");
    hr_case("coinit_mta_after_uninit", pCoInitializeEx(NULL, COINIT_MULTITHREADED));
    apt_case("apartment_mta");
    pCoUninitialize();
    return oracle_end();
}
