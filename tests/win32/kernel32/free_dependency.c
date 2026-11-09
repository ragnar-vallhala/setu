/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: FreeLibrary on a DLL that is loaded only because another
 * dynamically loaded DLL imports it (a static import, never LoadLibrary'd
 * itself), with its handle taken from GetModuleHandleW: whether the
 * dependency stays loaded while its importer is loaded, after a second
 * FreeLibrary, and whether it goes away with its importer.
 *
 * The DLL pair is found at run time, so it need not be the same on every
 * system: for each candidate system DLL A (not yet loaded), the modules
 * that LoadLibraryW(A) brings in are listed with a toolhelp snapshot; the
 * first such dependency B is used. Only booleans are printed, never names.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-freelibrary
 *       https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-getmodulehandlew
 *       https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-search-order
 *       https://learn.microsoft.com/en-us/windows/win32/api/tlhelp32/nf-tlhelp32-createtoolhelp32snapshot
 */
#include "oracle.h"
#include <tlhelp32.h>

#define MAX_MODS 512

static int list_modules(WCHAR names[][MAX_MODULE_NAME32 + 1], HMODULE *bases)
{
    MODULEENTRY32W me;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, 0);
    int n = 0;
    if (snap == INVALID_HANDLE_VALUE) return 0;
    me.dwSize = sizeof(me);
    if (Module32FirstW(snap, &me)) {
        do {
            if (n < MAX_MODS) {
                lstrcpynW(names[n], me.szModule, MAX_MODULE_NAME32 + 1);
                bases[n] = me.hModule;
                n++;
            }
        } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);
    return n;
}

static WCHAR before[MAX_MODS][MAX_MODULE_NAME32 + 1], after[MAX_MODS][MAX_MODULE_NAME32 + 1];
static HMODULE before_base[MAX_MODS], after_base[MAX_MODS];

static int is_mapped(HMODULE base)
{
    MEMORY_BASIC_INFORMATION mbi;
    return VirtualQuery(base, &mbi, sizeof(mbi)) && mbi.State == MEM_COMMIT && mbi.Type == MEM_IMAGE;
}

int main(void)
{
    static const WCHAR *candidates[] = {
        L"dbghelp.dll", L"winhttp.dll", L"wininet.dll", L"urlmon.dll", L"msi.dll",
        L"d3d11.dll", L"dxgi.dll", L"wtsapi32.dll", L"netapi32.dll", L"crypt32.dll",
        L"setupapi.dll", L"comdlg32.dll", L"shell32.dll", L"oleaut32.dll", L"winmm.dll",
    };
    HMODULE a = NULL, b = NULL;
    WCHAR bname[MAX_MODULE_NAME32 + 1] = {0};
    unsigned int c;
    int nb, na, i, j;
    BOOL ok;

    oracle_begin("kernel32/free_dependency");

    for (c = 0; c < sizeof(candidates) / sizeof(candidates[0]) && !b; c++) {
        if (GetModuleHandleW(candidates[c])) continue;
        nb = list_modules(before, before_base);
        if (!(a = LoadLibraryW(candidates[c]))) continue;
        na = list_modules(after, after_base);
        for (i = 0; i < na && !b; i++) {
            int seen = 0;
            if (after_base[i] == a) continue;
            for (j = 0; j < nb; j++) if (!lstrcmpiW(after[i], before[j])) { seen = 1; break; }
            if (!seen) { b = after_base[i]; lstrcpyW(bname, after[i]); }
        }
        if (!b) { FreeLibrary(a); a = NULL; }
    }

    oracle_case("pair_found");
    o_bool("found", a && b);
    oracle_case_end();
    if (!(a && b)) return oracle_end();

    {
        HMODULE h = GetModuleHandleW(bname);
        oracle_case("handle_from_getmodulehandle");
        o_bool("same_base", h == b);
        oracle_case_end();

        ORACLE_CALL(ok, FreeLibrary(h));
        oracle_case("free_dependency_once");
        o_bool("ret", ok);
        o_lasterr();
        o_bool("still_loaded", GetModuleHandleW(bname) != NULL);
        o_bool("still_mapped", is_mapped(b));
        oracle_case_end();

        ORACLE_CALL(ok, FreeLibrary(h));
        oracle_case("free_dependency_twice");
        o_bool("ret", ok);
        o_lasterr();
        o_bool("still_loaded", GetModuleHandleW(bname) != NULL);
        o_bool("still_mapped", is_mapped(b));
        oracle_case_end();

        ok = FreeLibrary(a);
        oracle_case("free_importer");
        o_bool("ret", ok);
        o_bool("importer_unloaded", !is_mapped(a));
        o_bool("dependency_unloaded", GetModuleHandleW(bname) == NULL);
        oracle_case_end();
    }

    /* a dependency that was also loaded explicitly once */
    a = NULL; b = NULL;
    for (c = 0; c < sizeof(candidates) / sizeof(candidates[0]) && !b; c++) {
        if (GetModuleHandleW(candidates[c])) continue;
        nb = list_modules(before, before_base);
        if (!(a = LoadLibraryW(candidates[c]))) continue;
        na = list_modules(after, after_base);
        for (i = 0; i < na && !b; i++) {
            int seen = 0;
            if (after_base[i] == a) continue;
            for (j = 0; j < nb; j++) if (!lstrcmpiW(after[i], before[j])) { seen = 1; break; }
            if (!seen) { b = after_base[i]; lstrcpyW(bname, after[i]); }
        }
        if (!b) { FreeLibrary(a); a = NULL; }
    }
    oracle_case("explicit_load_too");
    o_bool("found", a && b);
    if (a && b) {
        HMODULE h = LoadLibraryW(bname);
        o_bool("same_base", h == b);
        FreeLibrary(h);
        o_bool("after_one_free", GetModuleHandleW(bname) != NULL);
        FreeLibrary(h);
        o_bool("after_two_frees", GetModuleHandleW(bname) != NULL);
        FreeLibrary(a);
    }
    oracle_case_end();

    return oracle_end();
}
