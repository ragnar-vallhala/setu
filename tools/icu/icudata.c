/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * icu.dll's entry point (tools/build-icu.sh).
 *
 * The 64-bit icu.dll carries ICU's data in its .icudata section, under the
 * symbol ICU's static data packaging looks for, and needs nothing here.
 *
 * The 32-bit icu.dll has no copy of the data (it would be the same 31 MB
 * again: the data is the same for both). When it is loaded, it maps the
 * 64-bit icu.dll from the native system folder as a plain file (no code of
 * it runs), finds its .icudata section and gives it to ICU with
 * udata_setCommonData, before anything can use ICU. Mapping the file
 * directly instead of calling LoadLibraryEx keeps the loader out of
 * DllMain. The mapping stays for the life of the process.
 *
 * Docs: https://learn.microsoft.com/windows/win32/winprog64/file-system-redirector (Sysnative)
 *       https://learn.microsoft.com/windows/win32/debug/pe-format (section table)
 *       https://unicode-org.github.io/icu/userguide/icu_data/ (udata_setCommonData)
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wchar.h>
#include "unicode/udata.h"

#ifndef ICU_DATA_EMBEDDED
static const void *map_section(const wchar_t *path)
{
    HANDLE file, mapping;
    const BYTE *base;
    const IMAGE_DOS_HEADER *dos;
    const IMAGE_NT_HEADERS64 *nt;
    const IMAGE_SECTION_HEADER *sec;
    LARGE_INTEGER size;
    WORD i;

    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL,
                       OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE) return NULL;
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 4096) {
        CloseHandle(file);
        return NULL;
    }
    mapping = CreateFileMappingW(file, NULL, PAGE_READONLY, 0, 0, NULL);
    CloseHandle(file);
    if (!mapping) return NULL;
    base = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    CloseHandle(mapping);
    if (!base) return NULL;

    dos = (const IMAGE_DOS_HEADER *)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 ||
        dos->e_lfanew > size.QuadPart - (LONGLONG)sizeof(*nt)) goto fail;
    nt = (const IMAGE_NT_HEADERS64 *)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64) goto fail;
    sec = (const IMAGE_SECTION_HEADER *)((const BYTE *)&nt->OptionalHeader +
                                         nt->FileHeader.SizeOfOptionalHeader);
    if ((const BYTE *)(sec + nt->FileHeader.NumberOfSections) > base + size.QuadPart) goto fail;
    for (i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        if (memcmp(sec[i].Name, ".icudata", 8)) continue;
        if (sec[i].PointerToRawData % 16 || sec[i].SizeOfRawData < 32 ||
            (LONGLONG)sec[i].PointerToRawData + sec[i].SizeOfRawData > size.QuadPart) goto fail;
        return base + sec[i].PointerToRawData;
    }
fail:
    UnmapViewOfFile(base);
    return NULL;
}

static void load_data(void)
{
    static const wchar_t *const dirs[] = {L"Sysnative", L"System32"};
    wchar_t path[MAX_PATH + 32];
    UINT n = GetSystemWindowsDirectoryW(path, MAX_PATH);
    UErrorCode status = U_ZERO_ERROR;
    const void *data = NULL;

    if (!n || n >= MAX_PATH) return;
    for (size_t i = 0; !data && i < sizeof(dirs) / sizeof(dirs[0]); i++) {
        path[n] = L'\\';
        wcscpy(path + n + 1, dirs[i]);
        wcscat(path, L"\\icu.dll");
        data = map_section(path);
    }
    if (data) udata_setCommonData(data, &status);
}
#endif

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, void *reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
#ifndef ICU_DATA_EMBEDDED
        load_data();
#endif
    }
    return TRUE;
}
