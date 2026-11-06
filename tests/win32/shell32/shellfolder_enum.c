/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: IShellFolder::EnumObjects on a file-system folder the test
 * builds (files with and without extensions, hidden, system, hidden+system,
 * a .lnk, subfolders, an empty subfolder) with SHCONTF_FOLDERS,
 * SHCONTF_NONFOLDERS, SHCONTF_INCLUDEHIDDEN, SHCONTF_INCLUDESUPERHIDDEN and
 * combinations; IEnumIDList::Next with several items at once, Skip,
 * Reset and Clone; enumerating an empty folder; and which well-known
 * items the Desktop and This PC folders contain (as booleans: This PC,
 * Recycle Bin and drive C:, matched with CompareIDs).
 *
 * Names are SHGDN_INFOLDER | SHGDN_FORPARSING (the file names), sorted.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ishellfolder-enumobjects
 *       https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/ne-shobjidl_core-_shcontf
 *       https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nn-shobjidl_core-ienumidlist
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/nf-shlobj_core-shgetdesktopfolder
 *       https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ishellfolder-compareids
 */
#define INITGUID
#include "oracle.h"
#include <shlobj.h>
#include <shlwapi.h>

static HRESULT(WINAPI *pSHGetDesktopFolder)(IShellFolder **);
static HRESULT(WINAPI *pSHGetSpecialFolderLocation)(HWND, int, PIDLIST_ABSOLUTE *);
static HRESULT(WINAPI *pStrRetToBufW)(STRRET *, PCUITEMID_CHILD, LPWSTR, UINT);
static HRESULT(WINAPI *pCoInitializeEx)(void *, DWORD);
static void(WINAPI *pCoTaskMemFree)(void *);
static PUIDLIST_RELATIVE(WINAPI *pILFindLastID)(PCUIDLIST_RELATIVE);

#define LOAD(m, f) (*(FARPROC *)&p##f = GetProcAddress(m, #f))

static IShellFolder *desktop;

static void make_file(const wchar_t *name, DWORD attrs)
{
    HANDLE h = CreateFileW(name, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    DWORD n;
    WriteFile(h, "x", 1, &n, NULL);
    CloseHandle(h);
    if (attrs) SetFileAttributesW(name, attrs);
}

static IShellFolder *bind_path(const wchar_t *path)
{
    PIDLIST_RELATIVE pidl = NULL;
    IShellFolder *sf = NULL;
    wchar_t buf[MAX_PATH];
    wcscpy(buf, path);
    if (FAILED(desktop->lpVtbl->ParseDisplayName(desktop, NULL, NULL, buf, NULL, &pidl, NULL))) return NULL;
    desktop->lpVtbl->BindToObject(desktop, pidl, NULL, &IID_IShellFolder, (void **)&sf);
    pCoTaskMemFree(pidl);
    return sf;
}

static int wcmp(const void *a, const void *b) { return _wcsicmp(*(wchar_t *const *)a, *(wchar_t *const *)b); }

static void name_of(IShellFolder *sf, PCUITEMID_CHILD child, wchar_t *out)
{
    STRRET sr;
    out[0] = 0;
    if (SUCCEEDED(sf->lpVtbl->GetDisplayNameOf(sf, child, SHGDN_INFOLDER | SHGDN_FORPARSING, &sr)))
        pStrRetToBufW(&sr, child, out, MAX_PATH);
}

static void enum_case(const char *name, IShellFolder *sf, SHCONTF flags)
{
    IEnumIDList *en = NULL;
    HRESULT hr = sf->lpVtbl->EnumObjects(sf, NULL, flags, &en);
    wchar_t *names[64];
    int n = 0;
    static wchar_t buf[4096];
    if (hr == S_OK && en) {
        PITEMID_CHILD c;
        ULONG got;
        while (n < 64 && en->lpVtbl->Next(en, 1, &c, &got) == S_OK) {
            names[n] = malloc(MAX_PATH * sizeof(wchar_t));
            name_of(sf, c, names[n]);
            SFGAOF a = SFGAO_FOLDER;
            sf->lpVtbl->GetAttributesOf(sf, 1, (PCUITEMID_CHILD_ARRAY)&c, &a);
            if (a & SFGAO_FOLDER) wcscat(names[n], L"/");
            n++;
            pCoTaskMemFree(c);
        }
        en->lpVtbl->Release(en);
    }
    qsort(names, n, sizeof names[0], wcmp);
    buf[0] = 0;
    for (int i = 0; i < n; i++) {
        if (i) wcscat(buf, L",");
        wcscat(buf, names[i]);
        free(names[i]);
    }
    oracle_case(name);
    o_hex32("hr", (DWORD)hr);
    o_bool("enum_null", en == NULL);
    o_u32("count", n);
    o_wstr("names", buf);
    oracle_case_end();
}

/* Does folder sf contain an item equal to the last id of special folder csidl? */
static int contains(IShellFolder *sf, PCUITEMID_CHILD want, SHCONTF flags)
{
    IEnumIDList *en = NULL;
    int found = 0;
    if (!want || sf->lpVtbl->EnumObjects(sf, NULL, flags, &en) != S_OK || !en) return 0;
    PITEMID_CHILD c;
    ULONG got;
    while (en->lpVtbl->Next(en, 1, &c, &got) == S_OK) {
        if (sf->lpVtbl->CompareIDs(sf, SHCIDS_CANONICALONLY, (PCUIDLIST_RELATIVE)c, (PCUIDLIST_RELATIVE)want) == 0) found = 1;
        pCoTaskMemFree(c);
    }
    en->lpVtbl->Release(en);
    return found;
}

int main(void)
{
    oracle_begin("shell32/shellfolder_enum");
    HMODULE sh = LoadLibraryW(L"shell32.dll");
    HMODULE ole = LoadLibraryW(L"ole32.dll");
    HMODULE shl = LoadLibraryW(L"shlwapi.dll");
    LOAD(sh, SHGetDesktopFolder);
    LOAD(sh, SHGetSpecialFolderLocation);
    LOAD(sh, ILFindLastID);
    LOAD(shl, StrRetToBufW);
    LOAD(ole, CoInitializeEx);
    LOAD(ole, CoTaskMemFree);
    int ok = pSHGetDesktopFolder && pSHGetSpecialFolderLocation && pILFindLastID && pStrRetToBufW &&
             pCoInitializeEx && pCoTaskMemFree;
    oracle_case("setup");
    o_bool("loaded", ok);
    oracle_case_end();
    if (!ok) return oracle_end();
    pCoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    pSHGetDesktopFolder(&desktop);

    CreateDirectoryW(L"t", NULL);
    make_file(L"t\\a.txt", 0);
    make_file(L"t\\b.dat", 0);
    make_file(L"t\\noext", 0);
    make_file(L"t\\hidden.txt", FILE_ATTRIBUTE_HIDDEN);
    make_file(L"t\\system.txt", FILE_ATTRIBUTE_SYSTEM);
    make_file(L"t\\superhidden.txt", FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
    make_file(L"t\\readonly.txt", FILE_ATTRIBUTE_READONLY);
    make_file(L"t\\shortcut.lnk", 0);
    CreateDirectoryW(L"t\\sub", NULL);
    make_file(L"t\\sub\\inner.txt", 0);
    CreateDirectoryW(L"t\\empty", NULL);
    CreateDirectoryW(L"t\\hiddendir", NULL);
    SetFileAttributesW(L"t\\hiddendir", FILE_ATTRIBUTE_HIDDEN);

    wchar_t cwd[MAX_PATH], tdir[MAX_PATH], edir[MAX_PATH];
    GetCurrentDirectoryW(MAX_PATH, cwd);
    swprintf(tdir, MAX_PATH, L"%ls\\t", cwd);
    swprintf(edir, MAX_PATH, L"%ls\\t\\empty", cwd);
    IShellFolder *sf = bind_path(tdir);
    oracle_case("bind_folder");
    o_bool("bound", sf != NULL);
    oracle_case_end();
    if (!sf) return oracle_end();

    enum_case("folders", sf, SHCONTF_FOLDERS);
    enum_case("nonfolders", sf, SHCONTF_NONFOLDERS);
    enum_case("both", sf, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS);
    enum_case("both_hidden", sf, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS | SHCONTF_INCLUDEHIDDEN);
    enum_case("both_superhidden", sf, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS | SHCONTF_INCLUDESUPERHIDDEN);
    enum_case("both_hidden_superhidden", sf,
              SHCONTF_FOLDERS | SHCONTF_NONFOLDERS | SHCONTF_INCLUDEHIDDEN | SHCONTF_INCLUDESUPERHIDDEN);
    enum_case("nonfolders_hidden", sf, SHCONTF_NONFOLDERS | SHCONTF_INCLUDEHIDDEN);
    enum_case("no_flags", sf, 0);
    enum_case("fastitems", sf, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS | SHCONTF_FASTITEMS);
    enum_case("flatlist", sf, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS | SHCONTF_FLATLIST);
    enum_case("storage", sf, SHCONTF_STORAGE);
    {
        IShellFolder *ef = bind_path(edir);
        if (ef) {
            enum_case("empty_folder", ef, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS | SHCONTF_INCLUDEHIDDEN);
            ef->lpVtbl->Release(ef);
        }
    }

    /* Next with celt > 1, Skip, Reset, Clone. */
    {
        IEnumIDList *en = NULL;
        sf->lpVtbl->EnumObjects(sf, NULL, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS, &en);
        if (en) {
            PITEMID_CHILD items[16];
            ULONG got = 77;
            HRESULT hr = en->lpVtbl->Next(en, 3, items, &got);
            oracle_case("next_three");
            o_hex32("hr", (DWORD)hr);
            o_u32("got", got);
            oracle_case_end();
            for (ULONG i = 0; i < got && i < 16; i++) pCoTaskMemFree(items[i]);
            hr = en->lpVtbl->Skip(en, 2);
            oracle_case("skip_two");
            o_hex32("hr", (DWORD)hr);
            oracle_case_end();
            got = 77;
            hr = en->lpVtbl->Next(en, 16, items, &got);
            oracle_case("next_rest");
            o_hex32("hr", (DWORD)hr);
            o_u32("got", got);
            oracle_case_end();
            for (ULONG i = 0; i < got && i < 16; i++) pCoTaskMemFree(items[i]);
            got = 77;
            hr = en->lpVtbl->Next(en, 1, items, &got);
            oracle_case("next_at_end");
            o_hex32("hr", (DWORD)hr);
            o_u32("got", got);
            oracle_case_end();
            hr = en->lpVtbl->Skip(en, 1);
            oracle_case("skip_at_end");
            o_hex32("hr", (DWORD)hr);
            oracle_case_end();
            hr = en->lpVtbl->Reset(en);
            got = 77;
            HRESULT h2 = en->lpVtbl->Next(en, 16, items, &got);
            oracle_case("reset_then_all");
            o_hex32("reset_hr", (DWORD)hr);
            o_hex32("hr", (DWORD)h2);
            o_u32("got", got);
            oracle_case_end();
            for (ULONG i = 0; i < got && i < 16; i++) pCoTaskMemFree(items[i]);
            IEnumIDList *cl = NULL;
            en->lpVtbl->Reset(en);
            hr = en->lpVtbl->Clone(en, &cl);
            oracle_case("clone");
            o_hex32("hr", (DWORD)hr);
            o_bool("clone_null", cl == NULL);
            oracle_case_end();
            if (cl) cl->lpVtbl->Release(cl);
            got = 77;
            hr = en->lpVtbl->Next(en, 1, items, NULL);
            oracle_case("next_one_null_fetched");
            o_hex32("hr", (DWORD)hr);
            oracle_case_end();
            if (hr == S_OK) pCoTaskMemFree(items[0]);
            hr = en->lpVtbl->Next(en, 2, items, NULL);
            oracle_case("next_two_null_fetched");
            o_hex32("hr", (DWORD)hr);
            oracle_case_end();
            if (hr == S_OK) { pCoTaskMemFree(items[0]); pCoTaskMemFree(items[1]); }
            en->lpVtbl->Release(en);
        }
    }

    /* Well-known items of the Desktop and This PC. */
    {
        PIDLIST_ABSOLUTE drives = NULL, bin = NULL, docs = NULL, cdrive = NULL;
        pSHGetSpecialFolderLocation(NULL, CSIDL_DRIVES, &drives);
        pSHGetSpecialFolderLocation(NULL, CSIDL_BITBUCKET, &bin);
        pSHGetSpecialFolderLocation(NULL, CSIDL_PERSONAL, &docs);
        wchar_t c[] = L"C:\\";
        desktop->lpVtbl->ParseDisplayName(desktop, NULL, NULL, c, NULL, (PIDLIST_RELATIVE *)&cdrive, NULL);
        SHCONTF all = SHCONTF_FOLDERS | SHCONTF_NONFOLDERS | SHCONTF_INCLUDEHIDDEN;
        oracle_case("desktop_items");
        o_bool("has_this_pc", contains(desktop, (PCUITEMID_CHILD)drives, all));
        o_bool("has_recycle_bin", contains(desktop, (PCUITEMID_CHILD)bin, all));
        oracle_case_end();
        IShellFolder *pc = NULL;
        if (drives) desktop->lpVtbl->BindToObject(desktop, drives, NULL, &IID_IShellFolder, (void **)&pc);
        oracle_case("this_pc_items");
        o_bool("bound", pc != NULL);
        if (pc && cdrive) o_bool("has_drive_c", contains(pc, (PCUITEMID_CHILD)pILFindLastID((PCUIDLIST_RELATIVE)cdrive), all));
        oracle_case_end();
        if (pc) pc->lpVtbl->Release(pc);
        if (drives) pCoTaskMemFree(drives);
        if (bin) pCoTaskMemFree(bin);
        if (docs) pCoTaskMemFree(docs);
        if (cdrive) pCoTaskMemFree(cdrive);
    }
    sf->lpVtbl->Release(sf);
    return oracle_end();
}
