/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: module APIs: GetModuleHandleW/ExW, GetModuleFileNameW/A
 * buffer sizing, LoadLibraryW/ExW error codes, GetProcAddress (names,
 * case, ordinals, forwarders), FreeLibrary and the DLL directory APIs.
 *
 * Module paths are printed as "<sysdir>\name", "<exe-dir>\name" or "<self>"; module
 * handles and addresses are only compared, never printed.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-getmodulehandlew
 *       https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-getmodulehandleexw
 *       https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-getmodulefilenamew
 *       https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-getmodulefilenamea
 *       https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-loadlibraryw
 *       https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-loadlibraryexw
 *       https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-getprocaddress
 *       https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-freelibrary
 *       https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-setdlldirectoryw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-getdlldirectoryw
 *       https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-adddlldirectory
 *       https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-search-order
 */
#include "oracle.h"

static wchar_t sysdir[MAX_PATH], exedir[MAX_PATH], selfpath[MAX_PATH];

#define BOOL_CASE(name, expr)     \
    do {                          \
        BOOL ok_;                 \
        ORACLE_CALL(ok_, (expr)); \
        oracle_case(name);        \
        o_bool("ret", ok_);       \
        o_lasterr();              \
        oracle_case_end();        \
    } while (0)

static void path_field(const char *k, const wchar_t *p)
{
    wchar_t out[MAX_PATH * 2];
    const wchar_t *slash = wcsrchr(p, L'\\');
    if (!slash) { o_wstr(k, p); return; }
    size_t dl = (size_t)(slash - p);
    if (dl == wcslen(sysdir) && !_wcsnicmp(p, sysdir, dl)) swprintf(out, MAX_PATH * 2, L"<sysdir>%ls", slash);
    else if (!_wcsicmp(p, selfpath)) swprintf(out, MAX_PATH * 2, L"<self>"); /* the harness renames the .exe */
    else if (dl == wcslen(exedir) && !_wcsnicmp(p, exedir, dl)) swprintf(out, MAX_PATH * 2, L"<exe-dir>%ls", slash);
    else swprintf(out, MAX_PATH * 2, L"<other>%ls", slash);
    o_wstr(k, out);
}

static HMODULE gmh_case(const char *name, const wchar_t *mod)
{
    HMODULE h;
    ORACLE_CALL(h, GetModuleHandleW(mod));
    oracle_case(name);
    o_handle("ret", h);
    o_lasterr();
    oracle_case_end();
    return h;
}

static HMODULE load_case(const char *name, const wchar_t *mod, DWORD flags)
{
    HMODULE h;
    ORACLE_CALL(h, LoadLibraryExW(mod, NULL, flags));
    oracle_case(name);
    o_handle("ret", h);
    o_lasterr();
    if (h) o_u32("low_bits", (DWORD)((ULONG_PTR)h & 3));
    oracle_case_end();
    return h;
}

static void gpa_case(const char *name, HMODULE m, const char *proc)
{
    FARPROC p;
    ORACLE_CALL(p, GetProcAddress(m, proc));
    oracle_case(name);
    o_bool("ret", p != NULL);
    o_lasterr();
    oracle_case_end();
}

static void gmfn_case(const char *name, HMODULE m, DWORD size)
{
    wchar_t buf[MAX_PATH + 8];
    DWORD r;
    wmemset(buf, L'#', MAX_PATH + 8);
    ORACLE_CALL(r, GetModuleFileNameW(m, buf, size));
    oracle_case(name);
    o_lasterr();
    if (size >= MAX_PATH) {
        o_bool("ret_is_len", r == wcslen(buf));
        if (r) path_field("path", buf);
        else o_u32("ret", r);
    } else {
        o_u32("ret", r);
        o_bool("terminated_at_end", size && buf[size - 1] == 0);
        o_bool("untouched_after", buf[size] == L'#');
        o_u32("strlen", size ? (DWORD)wcsnlen(buf, size) : 0);
    }
    oracle_case_end();
}

static int main_marker;

int main(void)
{
    oracle_begin("kernel32/modules");

    GetSystemDirectoryW(sysdir, MAX_PATH);
    GetModuleFileNameW(NULL, exedir, MAX_PATH);
    wcscpy(selfpath, exedir);
    *wcsrchr(exedir, L'\\') = 0;

    HMODULE exe, k32, ntdll, h, h2;
    BOOL ok;

    /* ---- GetModuleHandleW ---- */
    exe = gmh_case("gmh_null_is_exe", NULL);
    k32 = gmh_case("gmh_kernel32", L"kernel32.dll");
    h = gmh_case("gmh_kernel32_upper_no_ext", L"KERNEL32");
    oracle_case("gmh_kernel32_same");
    o_bool("same", h == k32);
    oracle_case_end();
    gmh_case("gmh_kernel32_trailing_dot", L"kernel32.");
    gmh_case("gmh_kernel32_full_path_style", L"C:\\no\\such\\dir\\kernel32.dll");
    gmh_case("gmh_kernel32_wrong_ext", L"kernel32.exe");
    gmh_case("gmh_kernel32_trailing_space", L"kernel32.dll ");
    ntdll = gmh_case("gmh_ntdll", L"ntdll");
    gmh_case("gmh_missing", L"setu_oracle_missing.dll");
    gmh_case("gmh_empty", L"");
    gmh_case("gmh_not_loaded_system_dll", L"mshtml.dll");

    /* ---- GetModuleHandleExW ---- */
    ORACLE_CALL(ok, GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                       (LPCWSTR)&main_marker, &h));
    oracle_case("gmhex_from_address_exe");
    o_bool("ret", ok);
    o_lasterr();
    o_bool("is_exe", h == exe);
    oracle_case_end();
    ORACLE_CALL(ok, GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                       (LPCWSTR)(void *)GetProcAddress(k32, "Sleep"), &h));
    oracle_case("gmhex_from_address_kernel32");
    o_bool("ret", ok);
    o_lasterr();
    o_bool("is_kernel32", h == k32);
    oracle_case_end();
    {
        void *heap = HeapAlloc(GetProcessHeap(), 0, 16);
        h = (HMODULE)1;
        ORACLE_CALL(ok, GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                           (LPCWSTR)heap, &h));
        oracle_case("gmhex_from_heap_address");
        o_bool("ret", ok);
        o_lasterr();
        o_bool("out_null", h == NULL);
        oracle_case_end();
        HeapFree(GetProcessHeap(), 0, heap);
    }
    h = (HMODULE)1;
    ORACLE_CALL(ok, GetModuleHandleExW(0, L"setu_oracle_missing.dll", &h));
    oracle_case("gmhex_missing");
    o_bool("ret", ok);
    o_lasterr();
    o_bool("out_null", h == NULL);
    oracle_case_end();
    ORACLE_CALL(ok, GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                       L"kernel32.dll", &h));
    oracle_case("gmhex_pin_and_unchanged");
    o_bool("ret", ok);
    o_lasterr();
    oracle_case_end();
    ORACLE_CALL(ok, GetModuleHandleExW(0, L"kernel32.dll", NULL));
    oracle_case("gmhex_null_out");
    o_bool("ret", ok);
    o_lasterr();
    oracle_case_end();
    ORACLE_CALL(ok, GetModuleHandleExW(0x100, L"kernel32.dll", &h));
    oracle_case("gmhex_unknown_flag");
    o_bool("ret", ok);
    o_lasterr();
    oracle_case_end();
    ORACLE_CALL(ok, GetModuleHandleExW(0, NULL, &h));
    oracle_case("gmhex_null_name_is_exe");
    o_bool("ret", ok);
    o_lasterr();
    o_bool("is_exe", h == exe);
    oracle_case_end();
    if (ok) FreeLibrary(h);
    ORACLE_CALL(ok, GetModuleHandleExW(0, L"kernel32.dll", &h));
    if (ok) FreeLibrary(h);

    /* ---- GetModuleFileNameW ---- */
    gmfn_case("gmfn_exe", NULL, MAX_PATH);
    gmfn_case("gmfn_exe_handle", exe, MAX_PATH);
    gmfn_case("gmfn_kernel32", k32, MAX_PATH);
    gmfn_case("gmfn_ntdll", ntdll, MAX_PATH);
    gmfn_case("gmfn_size_5", NULL, 5);
    gmfn_case("gmfn_size_1", NULL, 1);
    gmfn_case("gmfn_size_0", NULL, 0);
    gmfn_case("gmfn_bogus_module", (HMODULE)(ULONG_PTR)0x12340000, MAX_PATH);
    {
        char a[8];
        DWORD r;
        memset(a, '#', sizeof a);
        ORACLE_CALL(r, GetModuleFileNameA(k32, a, 5));
        oracle_case("gmfn_ansi_size_5");
        o_u32("ret", r);
        o_lasterr();
        o_bool("terminated_at_end", a[4] == 0);
        o_bool("untouched_after", a[5] == '#');
        oracle_case_end();
    }

    /* ---- LoadLibrary ---- */
    {
        HANDLE f = CreateFileW(L"notpe.dll", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        DWORD w;
        WriteFile(f, "this is not a PE file at all\r\n", 30, &w, NULL);
        CloseHandle(f);
        f = CreateFileW(L"mz.dll", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        char mz[256] = "MZ";
        WriteFile(f, mz, sizeof mz, &w, NULL);
        CloseHandle(f);
        f = CreateFileW(L"empty.dll", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        CloseHandle(f);
        CreateDirectoryW(L"dir.dll", NULL);
    }
    load_case("load_missing", L"setu_oracle_missing.dll", 0);
    load_case("load_missing_relative_path", L".\\setu_oracle_missing.dll", 0);
    load_case("load_missing_dir", L"nodir\\x.dll", 0);
    load_case("load_not_pe", L".\\notpe.dll", 0);
    load_case("load_mz_only", L".\\mz.dll", 0);
    load_case("load_empty_file", L".\\empty.dll", 0);
    load_case("load_directory", L".\\dir.dll", 0);
    load_case("load_empty_name", L"", 0);
    load_case("load_bad_chars", L"a|b.dll", 0);
    load_case("load_not_pe_as_datafile", L".\\notpe.dll", LOAD_LIBRARY_AS_DATAFILE);
    load_case("load_not_pe_as_image_resource", L".\\notpe.dll", LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    load_case("load_empty_as_datafile", L".\\empty.dll", LOAD_LIBRARY_AS_DATAFILE);
    h = load_case("load_kernel32", L"kernel32.dll", 0);
    oracle_case("load_kernel32_same_as_gmh");
    o_bool("same", h == k32);
    oracle_case_end();
    if (h) FreeLibrary(h);
    h = load_case("load_kernel32_no_ext", L"kernel32", 0);
    if (h) FreeLibrary(h);
    h = load_case("load_kernel32_trailing_dot", L"kernel32.", 0);
    if (h) FreeLibrary(h);
    h = load_case("load_kernel32_as_datafile", L"kernel32.dll", LOAD_LIBRARY_AS_DATAFILE);
    if (h) {
        oracle_case("datafile_handle_differs_from_module");
        o_bool("differs", h != k32);
        oracle_case_end();
        FreeLibrary(h);
    }
    h = load_case("load_kernel32_as_datafile_exclusive", L"kernel32.dll", LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE);
    if (h) FreeLibrary(h);
    h = load_case("load_kernel32_as_image_resource", L"kernel32.dll", LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (h) FreeLibrary(h);
    h = load_case("load_search_system32", L"kernel32.dll", LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (h) FreeLibrary(h);
    h = load_case("load_search_flags_relative_path", L".\\notpe.dll", LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR);
    if (h) FreeLibrary(h);
    h = load_case("load_unknown_flag", L"kernel32.dll", 0x80000000u);
    if (h) FreeLibrary(h);
    {
        HMODULE r;
        ORACLE_CALL(r, LoadLibraryExW(L"kernel32.dll", (HANDLE)1, 0));
        oracle_case("load_reserved_file_handle");
        o_handle("ret", r);
        o_lasterr();
        oracle_case_end();
        if (r) FreeLibrary(r);
    }

    /* Loading a DLL not yet loaded, then unloading it. */
    h = load_case("load_version_dll", L"version.dll", 0);
    gmh_case("gmh_version_after_load", L"version.dll");
    gmfn_case("gmfn_version_dll", h, MAX_PATH);
    h2 = load_case("load_version_dll_again", L"VERSION.DLL", 0);
    oracle_case("load_twice_same_handle");
    o_bool("same", h == h2);
    oracle_case_end();
    BOOL_CASE("free_version_1", FreeLibrary(h2));
    gmh_case("gmh_version_after_one_free", L"version.dll");
    BOOL_CASE("free_version_2", FreeLibrary(h));
    gmh_case("gmh_version_after_last_free", L"version.dll");

    /* ---- GetProcAddress ---- */
    gpa_case("gpa_exists", k32, "GetProcAddress");
    gpa_case("gpa_wrong_case", k32, "getprocaddress");
    gpa_case("gpa_missing", k32, "SetuOracleNoSuchFunction");
    gpa_case("gpa_empty_name", k32, "");
    gpa_case("gpa_ordinal_ffff", k32, (const char *)(ULONG_PTR)0xFFFF);
    gpa_case("gpa_ordinal_0", k32, (const char *)(ULONG_PTR)0);
    gpa_case("gpa_exe_no_exports", exe, "main");
    gpa_case("gpa_null_module", NULL, "main");
    gpa_case("gpa_bogus_module", (HMODULE)(ULONG_PTR)0x12340000, "Sleep");
    gpa_case("gpa_ntdll_function", ntdll, "NtClose");
    gpa_case("gpa_ntdll_data_export", ntdll, "NlsAnsiCodePage");
    {
        FARPROC a = GetProcAddress(k32, "HeapAlloc");
        FARPROC b = GetProcAddress(ntdll, "RtlAllocateHeap");
        oracle_case("gpa_forwarder_heapalloc");
        o_bool("resolved", a != NULL);
        o_bool("same_as_ntdll", a == b);
        oracle_case_end();
        a = GetProcAddress(k32, "EnterCriticalSection");
        b = GetProcAddress(ntdll, "RtlEnterCriticalSection");
        oracle_case("gpa_forwarder_enter_cs");
        o_bool("resolved", a != NULL);
        o_bool("same_as_ntdll", a == b);
        oracle_case_end();
        a = GetProcAddress(k32, "Sleep");
        HMODULE kb = GetModuleHandleW(L"kernelbase.dll");
        b = kb ? GetProcAddress(kb, "Sleep") : NULL;
        oracle_case("gpa_sleep_lives_in_kernelbase");
        o_bool("kernelbase_loaded", kb != NULL);
        o_bool("same", a == b);
        oracle_case_end();
        h = LoadLibraryExW(L"kernel32.dll", NULL, LOAD_LIBRARY_AS_DATAFILE);
        gpa_case("gpa_on_datafile_module", h, "Sleep");
        if (h) FreeLibrary(h);
    }

    /* ---- DLL directory ---- */
    {
        wchar_t d[MAX_PATH];
        DWORD r;
        wmemset(d, L'#', MAX_PATH);
        ORACLE_CALL(r, GetDllDirectoryW(MAX_PATH, d));
        oracle_case("get_dll_directory_default");
        o_u32("ret", r);
        o_lasterr();
        o_bool("empty_string", d[0] == 0);
        oracle_case_end();
        BOOL_CASE("set_dll_directory_relative", SetDllDirectoryW(L"sub"));
        wmemset(d, L'#', MAX_PATH);
        ORACLE_CALL(r, GetDllDirectoryW(MAX_PATH, d));
        oracle_case("get_dll_directory_after_set");
        o_lasterr();
        o_bool("ret_is_len", r == wcslen(d));
        const wchar_t *base = wcsrchr(d, L'\\');
        o_wstr("tail", base ? base : d);
        o_bool("absolute", d[0] && d[1] == L':');
        oracle_case_end();
        ORACLE_CALL(r, GetDllDirectoryW(2, d));
        oracle_case("get_dll_directory_small_buffer");
        o_bool("ret_gt_2", r > 2);
        o_lasterr();
        oracle_case_end();
        BOOL_CASE("set_dll_directory_empty", SetDllDirectoryW(L""));
        wmemset(d, L'#', MAX_PATH);
        ORACLE_CALL(r, GetDllDirectoryW(MAX_PATH, d));
        oracle_case("get_dll_directory_after_empty");
        o_u32("ret", r);
        o_lasterr();
        o_bool("empty_string", d[0] == 0);
        oracle_case_end();
        BOOL_CASE("set_dll_directory_null", SetDllDirectoryW(NULL));
        DLL_DIRECTORY_COOKIE c;
        ORACLE_CALL(c, AddDllDirectory(L"relative\\dir"));
        oracle_case("add_dll_directory_relative");
        o_bool("ret", c != NULL);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(c, AddDllDirectory(L"C:\\setu_oracle_missing_dir"));
        oracle_case("add_dll_directory_missing");
        o_bool("ret", c != NULL);
        o_lasterr();
        oracle_case_end();
        if (c) BOOL_CASE("remove_dll_directory", RemoveDllDirectory(c));
        if (c) BOOL_CASE("remove_dll_directory_again", RemoveDllDirectory(c));
        BOOL_CASE("set_default_dll_directories_zero", SetDefaultDllDirectories(0));
        BOOL_CASE("set_default_dll_directories_bad", SetDefaultDllDirectories(0x10));
    }

    /* ---- case of the module file name, flag validation ---- */
    gmfn_case("gmfn_kernelbase", GetModuleHandleW(L"kernelbase.dll"), MAX_PATH);
    h = load_case("load_mixed_case_name", L"VeRsIoN.DlL", 0);
    if (h) {
        gmfn_case("gmfn_mixed_case_name", h, MAX_PATH);
        FreeLibrary(h);
    }
    h = load_case("load_flag_0x10000", L"kernel32.dll", 0x10000);
    if (h) FreeLibrary(h);
    h = load_case("load_empty_name_as_datafile", L"", LOAD_LIBRARY_AS_DATAFILE);
    if (h) FreeLibrary(h);

    /* ---- risky last: FreeLibrary on bogus values ---- */
    BOOL_CASE("free_null", FreeLibrary(NULL));
    BOOL_CASE("free_bogus", FreeLibrary((HMODULE)(ULONG_PTR)0x12340000));

    return oracle_end();
}
