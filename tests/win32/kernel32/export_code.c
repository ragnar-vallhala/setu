/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: kernel32's FlushInstructionCache, GetModuleHandleW,
 * GetSystemInfo and VirtualProtect are real code exports.
 *
 * Firefox's launcher records the export address table entries of these four
 * functions, hands them to the browser process it creates, and calls them
 * there while the process is still mapping kernel32's dependencies, before
 * kernel32's own import table is bound. That only works when the export is
 * not a forwarder and the code it points at is runnable on its own. This test
 * reads the export address table of the loaded kernel32 the way that solver
 * does (name lookup, ordinal index, export directory range check for
 * forwarders) and then calls each function through base + RVA.
 *
 * Addresses, RVAs and system values are never printed.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/debug/pe-format#export-directory-table
 *       https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-getmodulehandlew
 *       https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-getsysteminfo
 *       https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualprotect
 *       https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-flushinstructioncache
 */
#include "oracle.h"
#include <string.h>

static BYTE *k32;

/* returns the EAT RVA, or 0 when missing or a forwarder */
static DWORD find_export(const char *name, int *forwarded)
{
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)k32;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(k32 + dos->e_lfanew);
    IMAGE_DATA_DIRECTORY *dd = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    IMAGE_EXPORT_DIRECTORY *ed = (IMAGE_EXPORT_DIRECTORY *)(k32 + dd->VirtualAddress);
    const DWORD *names = (const DWORD *)(k32 + ed->AddressOfNames);
    const DWORD *funcs = (const DWORD *)(k32 + ed->AddressOfFunctions);
    const WORD *ords = (const WORD *)(k32 + ed->AddressOfNameOrdinals);
    DWORD i, rva;

    *forwarded = 0;
    for (i = 0; i < ed->NumberOfNames; i++) {
        if (strcmp((const char *)(k32 + names[i]), name)) continue;
        rva = funcs[ords[i]];
        if (rva >= dd->VirtualAddress && rva < dd->VirtualAddress + dd->Size) { *forwarded = 1; return 0; }
        return rva;
    }
    return 0;
}

typedef BOOL (WINAPI *flush_t)(HANDLE, LPCVOID, SIZE_T);
typedef HMODULE (WINAPI *gmh_t)(LPCWSTR);
typedef void (WINAPI *gsi_t)(SYSTEM_INFO *);
typedef BOOL (WINAPI *vp_t)(void *, SIZE_T, DWORD, DWORD *);

static void export_case(const char *name, const char *fn)
{
    int fwd;
    DWORD rva = find_export(fn, &fwd);

    oracle_case(name);
    o_bool("found", rva != 0 || fwd);
    o_bool("forwarded", fwd);
    o_bool("same_as_getprocaddress", rva && (FARPROC)(k32 + rva) == GetProcAddress((HMODULE)k32, fn));
    if (rva) {
        BYTE *code = k32 + rva;
        MEMORY_BASIC_INFORMATION mbi;
        o_bool("in_image", VirtualQuery(code, &mbi, sizeof mbi) && mbi.AllocationBase == k32);
        o_bool("executable", (mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                                              PAGE_EXECUTE_WRITECOPY)) != 0);
    }
    oracle_case_end();
}

int main(void)
{
    k32 = (BYTE *)GetModuleHandleW(L"kernel32.dll");

    oracle_begin("kernel32/export_code");

    export_case("flush_instruction_cache", "FlushInstructionCache");
    export_case("get_module_handle_w", "GetModuleHandleW");
    export_case("get_system_info", "GetSystemInfo");
    export_case("virtual_protect", "VirtualProtect");

    {
        int fwd;
        DWORD rva;
        BYTE *page = VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        SYSTEM_INFO via_export, direct;
        DWORD old = 0;
        BOOL ok;

        oracle_case("call_through_rva");
        rva = find_export("GetModuleHandleW", &fwd);
        o_bool("module_handle_null_is_exe", rva && ((gmh_t)(k32 + rva))(NULL) == GetModuleHandleW(NULL));
        o_bool("module_handle_named_is_kernel32", rva && ((gmh_t)(k32 + rva))(L"kernel32.dll") == (HMODULE)k32);
        rva = find_export("GetSystemInfo", &fwd);
        memset(&via_export, 0xcc, sizeof via_export);
        memset(&direct, 0, sizeof direct);
        if (rva) ((gsi_t)(k32 + rva))(&via_export);
        GetSystemInfo(&direct);
        o_bool("system_info_equal", rva && !memcmp(&via_export, &direct, sizeof direct));
        o_u32("page_size", via_export.dwPageSize);
        o_u32("allocation_granularity", via_export.dwAllocationGranularity);
        rva = find_export("VirtualProtect", &fwd);
        ok = rva && ((vp_t)(k32 + rva))(page, 4096, PAGE_READONLY, &old);
        o_bool("virtual_protect_ok", ok);
        o_u32("virtual_protect_old", old);
        o_bool("virtual_protect_applied", ok && VirtualProtect(page, 4096, PAGE_READWRITE, &old) && old == PAGE_READONLY);
        rva = find_export("FlushInstructionCache", &fwd);
        o_bool("flush_ok", rva && ((flush_t)(k32 + rva))(GetCurrentProcess(), page, 4096));
        VirtualFree(page, 0, MEM_RELEASE);
        oracle_case_end();
    }
    return oracle_end();
}
