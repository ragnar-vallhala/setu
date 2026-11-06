/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: IShellFolder on a file-system folder the test builds, and
 * on the Desktop. GetDisplayNameOf with each SHGDN_* combination for a
 * file, a file without an extension, a folder and a .lnk;
 * ParseDisplayName (relative names, absolute paths through the Desktop,
 * missing items, bad characters, the eaten count and requested
 * attributes); GetAttributesOf with a mask for single items and several
 * at once; CompareIDs (by name, by column, folders before files,
 * case-insensitivity); BindToObject on a file and a folder; SetNameOf
 * to rename an item; GetUIObjectOf for IContextMenu / IExtractIconW
 * (success only).
 *
 * Paths print as "<cwd>"; attributes are masked to bits that describe
 * the item.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nn-shobjidl_core-ishellfolder
 *       https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/ne-shobjidl_core-_shgdnf
 *       https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ishellfolder-parsedisplayname
 *       https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ishellfolder-compareids
 *       https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ishellfolder-setnameof
 *       https://learn.microsoft.com/en-us/windows/win32/shell/sfgao
 */
#define INITGUID
#include "oracle.h"
#include <shlobj.h>
#include <shlwapi.h>

static HRESULT(WINAPI *pSHGetDesktopFolder)(IShellFolder **);
static HRESULT(WINAPI *pStrRetToBufW)(STRRET *, PCUITEMID_CHILD, LPWSTR, UINT);
static HRESULT(WINAPI *pCoInitializeEx)(void *, DWORD);
static void(WINAPI *pCoTaskMemFree)(void *);
static UINT(WINAPI *pILGetSize)(PCUIDLIST_RELATIVE);

#define LOAD(m, f) (*(FARPROC *)&p##f = GetProcAddress(m, #f))

/* IID_IExtractIconW, from shlobj.h's interface list (not exported by the import library). */
static const GUID iid_extract_icon_w = {0x000214fa, 0, 0, {0xc0, 0, 0, 0, 0, 0, 0, 0x46}};

#define ATTR_MASK (SFGAO_CANCOPY | SFGAO_CANMOVE | SFGAO_CANLINK | SFGAO_CANRENAME | SFGAO_CANDELETE | \
                   SFGAO_DROPTARGET | SFGAO_LINK | SFGAO_READONLY | SFGAO_HIDDEN | SFGAO_FOLDER | \
                   SFGAO_FILESYSTEM | SFGAO_HASSUBFOLDER | SFGAO_STREAM | SFGAO_FILESYSANCESTOR | SFGAO_STORAGE | \
                   SFGAO_STORAGEANCESTOR | SFGAO_BROWSABLE)

static IShellFolder *desktop, *sf;
static wchar_t cwd[MAX_PATH];

static void o_path(const char *k, const wchar_t *p)
{
    wchar_t out[2 * MAX_PATH];
    size_t n = wcslen(cwd);
    if (!_wcsnicmp(p, cwd, n)) swprintf(out, 2 * MAX_PATH, L"<cwd>%ls", p + n);
    else wcscpy(out, p);
    o_wstr(k, out);
}

static void make_file(const wchar_t *name, DWORD attrs)
{
    HANDLE h = CreateFileW(name, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    DWORD n;
    WriteFile(h, "xy", 2, &n, NULL);
    CloseHandle(h);
    if (attrs) SetFileAttributesW(name, attrs);
}

static PITEMID_CHILD parse(IShellFolder *f, const wchar_t *name, HRESULT *phr, ULONG *eaten, SFGAOF *attrs)
{
    wchar_t buf[MAX_PATH];
    PIDLIST_RELATIVE pidl = NULL;
    wcscpy(buf, name);
    HRESULT hr = f->lpVtbl->ParseDisplayName(f, NULL, NULL, buf, eaten, &pidl, attrs);
    if (phr) *phr = hr;
    return (PITEMID_CHILD)pidl;
}

static void names_of(const char *name, const wchar_t *item)
{
    static const struct { SHGDNF f; const char *k; } kinds[] = {
        {SHGDN_NORMAL, "normal"},
        {SHGDN_INFOLDER, "infolder"},
        {SHGDN_FOREDITING, "forediting"},
        {SHGDN_INFOLDER | SHGDN_FOREDITING, "infolder_editing"},
        {SHGDN_FORADDRESSBAR, "foraddressbar"},
        {SHGDN_INFOLDER | SHGDN_FORADDRESSBAR, "infolder_addressbar"},
        {SHGDN_FORPARSING, "forparsing"},
        {SHGDN_INFOLDER | SHGDN_FORPARSING, "infolder_parsing"},
        {SHGDN_FORPARSING | SHGDN_FORADDRESSBAR, "parsing_addressbar"},
    };
    HRESULT hr;
    PITEMID_CHILD pidl = parse(sf, item, &hr, NULL, NULL);
    oracle_case(name);
    o_hex32("parse_hr", (DWORD)hr);
    if (pidl) {
        for (size_t i = 0; i < sizeof kinds / sizeof kinds[0]; i++) {
            STRRET sr;
            wchar_t buf[MAX_PATH];
            char key[48];
            HRESULT h = sf->lpVtbl->GetDisplayNameOf(sf, pidl, kinds[i].f, &sr);
            if (SUCCEEDED(h)) {
                buf[0] = 0;
                pStrRetToBufW(&sr, pidl, buf, MAX_PATH);
                o_path(kinds[i].k, buf);
            } else {
                snprintf(key, sizeof key, "%s_hr", kinds[i].k);
                o_hex32(key, (DWORD)h);
            }
        }
        pCoTaskMemFree(pidl);
    }
    oracle_case_end();
}

static void attrs_of(const char *name, const wchar_t *item, SFGAOF mask)
{
    HRESULT hr;
    PITEMID_CHILD pidl = parse(sf, item, &hr, NULL, NULL);
    oracle_case(name);
    o_hex32("parse_hr", (DWORD)hr);
    if (pidl) {
        SFGAOF a = mask;
        hr = sf->lpVtbl->GetAttributesOf(sf, 1, (PCUITEMID_CHILD_ARRAY)&pidl, &a);
        o_hex32("hr", (DWORD)hr);
        o_hex32("attrs", a & ATTR_MASK);
        pCoTaskMemFree(pidl);
    }
    oracle_case_end();
}

static int sgn(HRESULT hr) { short v = (short)HRESULT_CODE(hr); return v < 0 ? -1 : v > 0; }

static void compare(const char *name, const wchar_t *a, const wchar_t *b, LPARAM col)
{
    PITEMID_CHILD pa = parse(sf, a, NULL, NULL, NULL), pb = parse(sf, b, NULL, NULL, NULL);
    oracle_case(name);
    if (pa && pb) {
        HRESULT hr = sf->lpVtbl->CompareIDs(sf, col, (PCUIDLIST_RELATIVE)pa, (PCUIDLIST_RELATIVE)pb);
        o_bool("succeeded", SUCCEEDED(hr));
        o_i64("order", sgn(hr));
        hr = sf->lpVtbl->CompareIDs(sf, col, (PCUIDLIST_RELATIVE)pb, (PCUIDLIST_RELATIVE)pa);
        o_i64("reverse_order", sgn(hr));
    } else o_bool("parsed", 0);
    oracle_case_end();
    if (pa) pCoTaskMemFree(pa);
    if (pb) pCoTaskMemFree(pb);
}

int main(void)
{
    oracle_begin("shell32/shellfolder_names");
    HMODULE sh = LoadLibraryW(L"shell32.dll");
    HMODULE ole = LoadLibraryW(L"ole32.dll");
    HMODULE shl = LoadLibraryW(L"shlwapi.dll");
    LOAD(sh, SHGetDesktopFolder);
    LOAD(sh, ILGetSize);
    LOAD(shl, StrRetToBufW);
    LOAD(ole, CoInitializeEx);
    LOAD(ole, CoTaskMemFree);
    int ok = pSHGetDesktopFolder && pILGetSize && pStrRetToBufW && pCoInitializeEx && pCoTaskMemFree;
    oracle_case("setup");
    o_bool("loaded", ok);
    oracle_case_end();
    if (!ok) return oracle_end();
    pCoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    pSHGetDesktopFolder(&desktop);
    GetCurrentDirectoryW(MAX_PATH, cwd);

    CreateDirectoryW(L"t", NULL);
    make_file(L"t\\Report.txt", 0);
    make_file(L"t\\archive.tar.gz", 0);
    make_file(L"t\\noext", 0);
    make_file(L"t\\data.qqqzz", 0);
    make_file(L"t\\ro.txt", FILE_ATTRIBUTE_READONLY);
    make_file(L"t\\hid.txt", FILE_ATTRIBUTE_HIDDEN);
    make_file(L"t\\b.txt", 0);
    make_file(L"t\\B2.txt", 0);
    make_file(L"t\\a.dat", 0);
    make_file(L"t\\rename_me.txt", 0);
    CreateDirectoryW(L"t\\Folder", NULL);
    CreateDirectoryW(L"t\\Folder\\inner", NULL);
    CreateDirectoryW(L"t\\zfolder", NULL);

    wchar_t tdir[MAX_PATH];
    swprintf(tdir, MAX_PATH, L"%ls\\t", cwd);
    {
        HRESULT hr;
        ULONG eaten = 0;
        SFGAOF a = SFGAO_FOLDER | SFGAO_FILESYSTEM;
        PIDLIST_RELATIVE p = (PIDLIST_RELATIVE)parse(desktop, tdir, &hr, &eaten, &a);
        oracle_case("desktop_parse_abs");
        o_hex32("hr", (DWORD)hr);
        o_u32("eaten", eaten);
        o_u32("eaten_is_len", eaten == wcslen(tdir));
        o_hex32("attrs", a);
        if (p) desktop->lpVtbl->BindToObject(desktop, p, NULL, &IID_IShellFolder, (void **)&sf);
        o_bool("bound", sf != NULL);
        oracle_case_end();
        if (p) pCoTaskMemFree(p);
    }
    if (!sf) return oracle_end();

    names_of("names_file", L"Report.txt");
    names_of("names_multi_dot", L"archive.tar.gz");
    names_of("names_noext", L"noext");
    names_of("names_unknown_ext", L"data.qqqzz");
    names_of("names_folder", L"Folder");
    names_of("names_case_differs", L"REPORT.TXT");

    /* ParseDisplayName edge cases. */
    static const struct { const char *name; const wchar_t *s; } pd[] = {
        {"parse_missing", L"nope.txt"},
        {"parse_nested", L"Folder\\inner"},
        {"parse_nested_missing", L"Folder\\nope"},
        {"parse_trailing_backslash", L"Folder\\"},
        {"parse_bad_chars", L"a<b>.txt"},
        {"parse_empty", L""},
        {"parse_dot", L"."},
        {"parse_dotdot", L".."},
        {"parse_forward_slash", L"Folder/inner"},
        {"parse_absolute_in_child", L"C:\\Windows"},
    };
    for (size_t i = 0; i < sizeof pd / sizeof pd[0]; i++) {
        HRESULT hr;
        ULONG eaten = 999;
        SFGAOF a = SFGAO_FOLDER;
        PITEMID_CHILD p = parse(sf, pd[i].s, &hr, &eaten, &a);
        oracle_case(pd[i].name);
        o_hex32("hr", (DWORD)hr);
        o_bool("pidl", p != NULL);
        if (SUCCEEDED(hr)) {
            o_u32("eaten", eaten);
            o_hex32("attrs", a & ATTR_MASK);
        }
        oracle_case_end();
        if (p) pCoTaskMemFree(p);
    }
    {
        HRESULT hr;
        wchar_t fp[MAX_PATH];
        swprintf(fp, MAX_PATH, L"%ls\\t\\nope.txt", cwd);
        PIDLIST_RELATIVE p = (PIDLIST_RELATIVE)parse(desktop, fp, &hr, NULL, NULL);
        oracle_case("desktop_parse_missing");
        o_hex32("hr", (DWORD)hr);
        oracle_case_end();
        if (p) pCoTaskMemFree(p);
        p = (PIDLIST_RELATIVE)parse(desktop, L"::{20D04FE0-3AEA-1069-A2D8-08002B30309D}", &hr, NULL, NULL);
        oracle_case("desktop_parse_this_pc");
        o_hex32("hr", (DWORD)hr);
        oracle_case_end();
        if (p) pCoTaskMemFree(p);
        p = (PIDLIST_RELATIVE)parse(desktop, L"C:\\", &hr, NULL, NULL);
        oracle_case("desktop_parse_drive");
        o_hex32("hr", (DWORD)hr);
        if (p) {
            STRRET sr;
            wchar_t buf[MAX_PATH];
            if (SUCCEEDED(desktop->lpVtbl->GetDisplayNameOf(desktop, (PCUITEMID_CHILD)p, SHGDN_FORPARSING, &sr))) {
                pStrRetToBufW(&sr, (PCUITEMID_CHILD)p, buf, MAX_PATH);
                o_wstr("parsing", buf);
            }
            pCoTaskMemFree(p);
        }
        oracle_case_end();
    }

    /* GetAttributesOf */
    attrs_of("attrs_file_all", L"Report.txt", 0xffffffff);
    attrs_of("attrs_file_mask_folder", L"Report.txt", SFGAO_FOLDER);
    attrs_of("attrs_readonly", L"ro.txt", 0xffffffff);
    attrs_of("attrs_hidden", L"hid.txt", 0xffffffff);
    attrs_of("attrs_folder_all", L"Folder", 0xffffffff);
    attrs_of("attrs_folder_subfolder", L"Folder", SFGAO_HASSUBFOLDER);
    attrs_of("attrs_empty_folder_subfolder", L"zfolder", SFGAO_HASSUBFOLDER);
    {
        PITEMID_CHILD two[2] = {parse(sf, L"Report.txt", NULL, NULL, NULL), parse(sf, L"Folder", NULL, NULL, NULL)};
        SFGAOF a = 0xffffffff;
        HRESULT hr = sf->lpVtbl->GetAttributesOf(sf, 2, (PCUITEMID_CHILD_ARRAY)two, &a);
        oracle_case("attrs_file_and_folder");
        o_hex32("hr", (DWORD)hr);
        o_hex32("attrs", a & ATTR_MASK);
        oracle_case_end();
        a = SFGAO_FOLDER;
        hr = sf->lpVtbl->GetAttributesOf(sf, 0, NULL, &a);
        oracle_case("attrs_zero_items");
        o_hex32("hr", (DWORD)hr);
        o_hex32("attrs", a & ATTR_MASK);
        oracle_case_end();
        for (int i = 0; i < 2; i++)
            if (two[i]) pCoTaskMemFree(two[i]);
    }

    /* CompareIDs */
    compare("cmp_name_lt", L"a.dat", L"b.txt", 0);
    compare("cmp_name_case", L"b.txt", L"B2.txt", 0);
    compare("cmp_folder_vs_file", L"zfolder", L"a.dat", 0);
    compare("cmp_same", L"Report.txt", L"Report.txt", 0);
    compare("cmp_size_col", L"Report.txt", L"noext", 1);
    compare("cmp_type_col", L"a.dat", L"b.txt", 2);
    compare("cmp_canonical", L"a.dat", L"b.txt", SHCIDS_CANONICALONLY);
    compare("cmp_allfields_same", L"Report.txt", L"Report.txt", SHCIDS_ALLFIELDS);

    /* BindToObject */
    {
        PITEMID_CHILD pf = parse(sf, L"Report.txt", NULL, NULL, NULL), pd2 = parse(sf, L"Folder", NULL, NULL, NULL);
        IShellFolder *sub = NULL;
        oracle_case("bind");
        if (pf) {
            HRESULT hr = sf->lpVtbl->BindToObject(sf, (PCUIDLIST_RELATIVE)pf, NULL, &IID_IShellFolder, (void **)&sub);
            o_bool("file_failed", FAILED(hr));
            if (sub) { sub->lpVtbl->Release(sub); sub = NULL; }
            IStream *st = NULL;
            hr = sf->lpVtbl->BindToObject(sf, (PCUIDLIST_RELATIVE)pf, NULL, &IID_IStream, (void **)&st);
            o_hex32("file_stream_hr", (DWORD)hr);
            if (st) {
                char b[8] = {0};
                ULONG got = 0;
                st->lpVtbl->Read(st, b, 8, &got);
                o_u32("stream_read", got);
                st->lpVtbl->Release(st);
            }
        }
        if (pd2) {
            HRESULT hr = sf->lpVtbl->BindToObject(sf, (PCUIDLIST_RELATIVE)pd2, NULL, &IID_IShellFolder, (void **)&sub);
            o_hex32("folder_hr", (DWORD)hr);
            if (sub) {
                IEnumIDList *en = NULL;
                int n = 0;
                if (sub->lpVtbl->EnumObjects(sub, NULL, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS, &en) == S_OK && en) {
                    PITEMID_CHILD c;
                    while (en->lpVtbl->Next(en, 1, &c, NULL) == S_OK) { n++; pCoTaskMemFree(c); }
                    en->lpVtbl->Release(en);
                }
                o_u32("folder_items", n);
                sub->lpVtbl->Release(sub);
            }
        }
        oracle_case_end();
        if (pf) pCoTaskMemFree(pf);
        if (pd2) pCoTaskMemFree(pd2);
    }

    /* GetUIObjectOf */
    {
        PITEMID_CHILD pf = parse(sf, L"Report.txt", NULL, NULL, NULL);
        oracle_case("ui_objects");
        if (pf) {
            IUnknown *u = NULL;
            HRESULT hr = sf->lpVtbl->GetUIObjectOf(sf, NULL, 1, (PCUITEMID_CHILD_ARRAY)&pf, &IID_IContextMenu, NULL, (void **)&u);
            o_hex32("contextmenu_hr", (DWORD)hr);
            if (u) { u->lpVtbl->Release(u); u = NULL; }
            hr = sf->lpVtbl->GetUIObjectOf(sf, NULL, 1, (PCUITEMID_CHILD_ARRAY)&pf, &iid_extract_icon_w, NULL, (void **)&u);
            o_hex32("extracticon_hr", (DWORD)hr);
            if (u) { u->lpVtbl->Release(u); u = NULL; }
            hr = sf->lpVtbl->GetUIObjectOf(sf, NULL, 1, (PCUITEMID_CHILD_ARRAY)&pf, &IID_IDataObject, NULL, (void **)&u);
            o_hex32("dataobject_hr", (DWORD)hr);
            if (u) { u->lpVtbl->Release(u); u = NULL; }
            hr = sf->lpVtbl->GetUIObjectOf(sf, NULL, 1, (PCUITEMID_CHILD_ARRAY)&pf, &IID_IShellFolder, NULL, (void **)&u);
            o_hex32("shellfolder_hr", (DWORD)hr);
            if (u) u->lpVtbl->Release(u);
            pCoTaskMemFree(pf);
        }
        oracle_case_end();
    }

    /* SetNameOf */
    {
        PITEMID_CHILD pf = parse(sf, L"rename_me.txt", NULL, NULL, NULL);
        oracle_case("setnameof");
        if (pf) {
            PITEMID_CHILD out = NULL;
            HRESULT hr = sf->lpVtbl->SetNameOf(sf, NULL, pf, L"renamed.txt", SHGDN_INFOLDER | SHGDN_FORPARSING, &out);
            o_hex32("hr", (DWORD)hr);
            o_bool("new_pidl", out != NULL);
            o_bool("old_gone", GetFileAttributesW(L"t\\rename_me.txt") == INVALID_FILE_ATTRIBUTES);
            o_bool("new_exists", GetFileAttributesW(L"t\\renamed.txt") != INVALID_FILE_ATTRIBUTES);
            if (out) {
                STRRET sr;
                wchar_t buf[MAX_PATH];
                if (SUCCEEDED(sf->lpVtbl->GetDisplayNameOf(sf, out, SHGDN_INFOLDER | SHGDN_FORPARSING, &sr))) {
                    pStrRetToBufW(&sr, out, buf, MAX_PATH);
                    o_wstr("new_name", buf);
                }
                pCoTaskMemFree(out);
            }
            PITEMID_CHILD pr = parse(sf, L"renamed.txt", NULL, NULL, NULL);
            if (pr) {
                hr = sf->lpVtbl->SetNameOf(sf, NULL, pr, L"b.txt", SHGDN_INFOLDER | SHGDN_FORPARSING, NULL);
                o_hex32("onto_existing_hr", (DWORD)hr);
                hr = sf->lpVtbl->SetNameOf(sf, NULL, pr, L"bad:name.txt", SHGDN_INFOLDER | SHGDN_FORPARSING, NULL);
                o_hex32("bad_name_hr", (DWORD)hr);
                /* Display-name rename of a file whose extension is hidden keeps the extension. */
                hr = sf->lpVtbl->SetNameOf(sf, NULL, pr, L"shown", SHGDN_INFOLDER, &out);
                o_hex32("display_rename_hr", (DWORD)hr);
                o_bool("display_rename_kept_ext", GetFileAttributesW(L"t\\shown.txt") != INVALID_FILE_ATTRIBUTES);
                o_bool("display_rename_no_ext", GetFileAttributesW(L"t\\shown") != INVALID_FILE_ATTRIBUTES);
                if (SUCCEEDED(hr) && out) pCoTaskMemFree(out);
                pCoTaskMemFree(pr);
            }
            pCoTaskMemFree(pf);
        }
        oracle_case_end();
    }
    sf->lpVtbl->Release(sf);
    desktop->lpVtbl->Release(desktop);
    return oracle_end();
}
