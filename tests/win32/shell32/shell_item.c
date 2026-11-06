/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: IShellItem, the object the Vista-and-later shell APIs
 * (file dialogs, IFileOperation, known folders) pass around.
 * SHCreateItemFromParsingName for files, folders, missing paths, drive
 * roots and virtual folders; IShellItem::GetDisplayName for the SIGDN_*
 * kinds; GetAttributes with SICHINT-free masks; GetParent up to the
 * Desktop; Compare (SICHINT_CANONICAL / DISPLAY); SHCreateItemFromIDList,
 * SHCreateItemFromRelativeName, SHCreateItemInKnownFolder,
 * SHGetKnownFolderItem, SHGetIDListFromObject and BindToHandler for
 * BHID_SFObject / BHID_EnumItems.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nn-shobjidl_core-ishellitem
 *       https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-shcreateitemfromparsingname
 *       https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/ne-shobjidl_core-sigdn
 *       https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ishellitem-compare
 *       https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-shcreateitemfromrelativename
 *       https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-shcreateiteminknownfolder
 *       https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-shgetknownfolderitem
 *       https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ishellitem-bindtohandler
 */
#define INITGUID
#include "oracle.h"
#include <shlobj.h>
#include <shobjidl.h>
#include <shlguid.h>

static HRESULT(WINAPI *pSHCreateItemFromParsingName)(PCWSTR, IBindCtx *, REFIID, void **);
static HRESULT(WINAPI *pSHCreateItemFromIDList)(PCIDLIST_ABSOLUTE, REFIID, void **);
static HRESULT(WINAPI *pSHCreateItemFromRelativeName)(IShellItem *, PCWSTR, IBindCtx *, REFIID, void **);
static HRESULT(WINAPI *pSHCreateItemInKnownFolder)(REFKNOWNFOLDERID, DWORD, PCWSTR, REFIID, void **);
static HRESULT(WINAPI *pSHGetKnownFolderItem)(REFKNOWNFOLDERID, KNOWN_FOLDER_FLAG, HANDLE, REFIID, void **);
static HRESULT(WINAPI *pSHGetIDListFromObject)(IUnknown *, PIDLIST_ABSOLUTE *);
static HRESULT(WINAPI *pSHGetSpecialFolderLocation)(HWND, int, PIDLIST_ABSOLUTE *);
static HRESULT(WINAPI *pCoInitializeEx)(void *, DWORD);
static void(WINAPI *pCoTaskMemFree)(void *);

#define LOAD(m, f) (*(FARPROC *)&p##f = GetProcAddress(m, #f))

#define ATTR_MASK (SFGAO_CANCOPY | SFGAO_CANMOVE | SFGAO_CANLINK | SFGAO_CANRENAME | SFGAO_CANDELETE | \
                   SFGAO_DROPTARGET | SFGAO_LINK | SFGAO_READONLY | SFGAO_HIDDEN | SFGAO_FOLDER | \
                   SFGAO_FILESYSTEM | SFGAO_HASSUBFOLDER | SFGAO_STREAM | SFGAO_FILESYSANCESTOR | SFGAO_STORAGE | \
                   SFGAO_STORAGEANCESTOR | SFGAO_BROWSABLE)

static wchar_t cwd[MAX_PATH], profile[MAX_PATH];

static void o_path(const char *k, const wchar_t *p)
{
    wchar_t out[2 * MAX_PATH];
    size_t n = wcslen(cwd), m = wcslen(profile);
    if (!_wcsnicmp(p, cwd, n)) swprintf(out, 2 * MAX_PATH, L"<cwd>%ls", p + n);
    else if (m && !_wcsnicmp(p, profile, m)) swprintf(out, 2 * MAX_PATH, L"<profile>%ls", p + m);
    else wcscpy(out, p);
    o_wstr(k, out);
}

/* The drive root's names include the volume label, which is the machine's. */
static int attrs_only;

static void o_names(IShellItem *it)
{
    static const struct { SIGDN k; const char *n; } kinds[] = {
        {SIGDN_NORMALDISPLAY, "normal"}, {SIGDN_PARENTRELATIVEPARSING, "parent_parsing"},
        {SIGDN_DESKTOPABSOLUTEPARSING, "desktop_parsing"}, {SIGDN_PARENTRELATIVEEDITING, "parent_editing"},
        {SIGDN_FILESYSPATH, "filesys"}, {SIGDN_PARENTRELATIVE, "parent_relative"},
    };
    for (size_t i = 0; i < sizeof kinds / sizeof kinds[0] && !attrs_only; i++) {
        PWSTR s = NULL;
        HRESULT hr = it->lpVtbl->GetDisplayName(it, kinds[i].k, &s);
        if (SUCCEEDED(hr) && s) {
            o_path(kinds[i].n, s);
            pCoTaskMemFree(s);
        } else {
            char key[48];
            snprintf(key, sizeof key, "%s_hr", kinds[i].n);
            o_hex32(key, (DWORD)hr);
        }
    }
    SFGAOF a = 0;
    HRESULT hr = it->lpVtbl->GetAttributes(it, ATTR_MASK, &a);
    o_hex32("attrs_hr", (DWORD)hr);
    o_hex32("attrs", a);
}

static void item_case(const char *name, const wchar_t *path)
{
    IShellItem *it = NULL;
    HRESULT hr = pSHCreateItemFromParsingName(path, NULL, &IID_IShellItem, (void **)&it);
    oracle_case(name);
    o_hex32("hr", (DWORD)hr);
    o_bool("null_on_failure", FAILED(hr) ? it == NULL : 1);
    if (it) {
        o_names(it);
        it->lpVtbl->Release(it);
    }
    oracle_case_end();
}

int main(void)
{
    oracle_begin("shell32/shell_item");
    HMODULE sh = LoadLibraryW(L"shell32.dll");
    HMODULE ole = LoadLibraryW(L"ole32.dll");
    LOAD(sh, SHCreateItemFromParsingName);
    LOAD(sh, SHCreateItemFromIDList);
    LOAD(sh, SHCreateItemFromRelativeName);
    LOAD(sh, SHCreateItemInKnownFolder);
    LOAD(sh, SHGetKnownFolderItem);
    LOAD(sh, SHGetIDListFromObject);
    LOAD(sh, SHGetSpecialFolderLocation);
    LOAD(ole, CoInitializeEx);
    LOAD(ole, CoTaskMemFree);
    int ok = pSHCreateItemFromParsingName && pSHCreateItemFromIDList && pSHCreateItemFromRelativeName &&
             pSHCreateItemInKnownFolder && pSHGetKnownFolderItem && pSHGetIDListFromObject &&
             pSHGetSpecialFolderLocation && pCoInitializeEx && pCoTaskMemFree;
    oracle_case("setup");
    o_bool("loaded", ok);
    oracle_case_end();
    if (!ok) return oracle_end();
    pCoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    GetCurrentDirectoryW(MAX_PATH, cwd);
    if (!GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH)) profile[0] = 0;

    CreateDirectoryW(L"d", NULL);
    CreateDirectoryW(L"d\\sub", NULL);
    HANDLE h = CreateFileW(L"d\\file.txt", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    CloseHandle(h);
    h = CreateFileW(L"d\\sub\\x.bin", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    CloseHandle(h);
    wchar_t p[MAX_PATH];

    swprintf(p, MAX_PATH, L"%ls\\d\\file.txt", cwd);
    item_case("file", p);
    swprintf(p, MAX_PATH, L"%ls\\d", cwd);
    item_case("folder", p);
    swprintf(p, MAX_PATH, L"%ls\\d\\", cwd);
    item_case("folder_trailing_backslash", p);
    swprintf(p, MAX_PATH, L"%ls\\d\\missing.txt", cwd);
    item_case("missing", p);
    item_case("relative", L"d\\file.txt");
    attrs_only = 1;
    item_case("drive_root_attrs", L"C:\\");
    attrs_only = 0;
    item_case("this_pc", L"::{20D04FE0-3AEA-1069-A2D8-08002B30309D}");
    item_case("recycle_bin", L"::{645FF040-5081-101B-9F08-00AA002F954E}");
    item_case("empty", L"");
    item_case("bogus_guid", L"::{00000000-1111-2222-3333-444444444444}");
    item_case("shell_documents", L"shell:Personal");

    /* GetParent chain and Compare. */
    {
        IShellItem *it = NULL, *it2 = NULL, *it3 = NULL;
        swprintf(p, MAX_PATH, L"%ls\\d\\sub\\x.bin", cwd);
        pSHCreateItemFromParsingName(p, NULL, &IID_IShellItem, (void **)&it);
        pSHCreateItemFromParsingName(p, NULL, &IID_IShellItem, (void **)&it2);
        swprintf(p, MAX_PATH, L"%ls\\d\\file.txt", cwd);
        pSHCreateItemFromParsingName(p, NULL, &IID_IShellItem, (void **)&it3);
        oracle_case("compare");
        if (it && it2 && it3) {
            int order = 77;
            o_hex32("same_hr", (DWORD)it->lpVtbl->Compare(it, it2, SICHINT_CANONICAL, &order));
            o_i64("same_order", order);
            order = 77;
            HRESULT hr = it->lpVtbl->Compare(it, it3, SICHINT_CANONICAL, &order);
            o_hex32("diff_hr", (DWORD)hr);
            o_i64("diff_order_sign", order < 0 ? -1 : order > 0);
            order = 77;
            hr = it->lpVtbl->Compare(it, it2, SICHINT_DISPLAY, &order);
            o_hex32("display_hr", (DWORD)hr);
            o_i64("display_order", order);
        }
        oracle_case_end();
        oracle_case("parent_chain");
        if (it) {
            IShellItem *cur = it;
            cur->lpVtbl->AddRef(cur);
            wchar_t chain[2048] = L"";
            int depth = 0;
            HRESULT hr = S_OK;
            while (depth < 20) {
                IShellItem *par = NULL;
                hr = cur->lpVtbl->GetParent(cur, &par);
                cur->lpVtbl->Release(cur);
                if (FAILED(hr) || !par) break;
                PWSTR s = NULL;
                par->lpVtbl->GetDisplayName(par, SIGDN_PARENTRELATIVEPARSING, &s);
                if (s) {
                    /* Only the levels under the cwd and the top: the middle depends on the cwd depth. */
                    if (depth < 3) {
                        if (*chain) wcscat(chain, L"|");
                        wcscat(chain, s);
                    }
                    pCoTaskMemFree(s);
                }
                cur = par;
                depth++;
            }
            o_wstr("first_three", chain);
            o_hex32("top_hr", (DWORD)hr);
            /* The Desktop is the last parent. */
            o_bool("ends_with_failure", FAILED(hr));
        }
        oracle_case_end();
        if (it) it->lpVtbl->Release(it);
        if (it2) it2->lpVtbl->Release(it2);
        if (it3) it3->lpVtbl->Release(it3);
    }

    /* SHCreateItemFromRelativeName, BindToHandler. */
    {
        IShellItem *dir = NULL, *rel = NULL;
        swprintf(p, MAX_PATH, L"%ls\\d", cwd);
        pSHCreateItemFromParsingName(p, NULL, &IID_IShellItem, (void **)&dir);
        oracle_case("relative_name");
        if (dir) {
            HRESULT hr = pSHCreateItemFromRelativeName(dir, L"sub\\x.bin", NULL, &IID_IShellItem, (void **)&rel);
            o_hex32("hr", (DWORD)hr);
            if (rel) {
                PWSTR s = NULL;
                if (SUCCEEDED(rel->lpVtbl->GetDisplayName(rel, SIGDN_FILESYSPATH, &s)) && s) {
                    o_path("path", s);
                    pCoTaskMemFree(s);
                }
                rel->lpVtbl->Release(rel);
                rel = NULL;
            }
            hr = pSHCreateItemFromRelativeName(dir, L"nope.txt", NULL, &IID_IShellItem, (void **)&rel);
            o_hex32("missing_hr", (DWORD)hr);
            if (rel) rel->lpVtbl->Release(rel);
        }
        oracle_case_end();
        oracle_case("bind_to_handler");
        if (dir) {
            IShellFolder *sf = NULL;
            HRESULT hr = dir->lpVtbl->BindToHandler(dir, NULL, &BHID_SFObject, &IID_IShellFolder, (void **)&sf);
            o_hex32("sfobject_hr", (DWORD)hr);
            if (sf) sf->lpVtbl->Release(sf);
            IEnumShellItems *en = NULL;
            hr = dir->lpVtbl->BindToHandler(dir, NULL, &BHID_EnumItems, &IID_IEnumShellItems, (void **)&en);
            o_hex32("enumitems_hr", (DWORD)hr);
            if (en) {
                IShellItem *c;
                ULONG got;
                wchar_t names[512] = L"";
                int n = 0;
                while (en->lpVtbl->Next(en, 1, &c, &got) == S_OK && n < 10) {
                    PWSTR s = NULL;
                    c->lpVtbl->GetDisplayName(c, SIGDN_PARENTRELATIVEPARSING, &s);
                    if (s) {
                        /* Enumeration order is the file system's: sort two names by hand. */
                        if (*names && _wcsicmp(s, names) < 0) {
                            wchar_t tmp[512];
                            swprintf(tmp, 512, L"%ls,%ls", s, names);
                            wcscpy(names, tmp);
                        } else {
                            if (*names) wcscat(names, L",");
                            wcscat(names, s);
                        }
                        pCoTaskMemFree(s);
                    }
                    c->lpVtbl->Release(c);
                    n++;
                }
                o_u32("count", n);
                o_wstr("names", names);
                en->lpVtbl->Release(en);
            }
            IStream *st = NULL;
            hr = dir->lpVtbl->BindToHandler(dir, NULL, &BHID_Stream, &IID_IStream, (void **)&st);
            o_hex32("folder_stream_hr", (DWORD)hr);
            if (st) st->lpVtbl->Release(st);
            dir->lpVtbl->Release(dir);
        }
        oracle_case_end();
    }

    /* IDList round trip. */
    {
        PIDLIST_ABSOLUTE pidl = NULL;
        IShellItem *it = NULL;
        pSHGetSpecialFolderLocation(NULL, CSIDL_DRIVES, &pidl);
        HRESULT hr = pidl ? pSHCreateItemFromIDList(pidl, &IID_IShellItem, (void **)&it) : E_FAIL;
        oracle_case("from_idlist_drives");
        o_hex32("hr", (DWORD)hr);
        if (it) {
            o_names(it);
            PIDLIST_ABSOLUTE back = NULL;
            o_hex32("idlist_hr", (DWORD)pSHGetIDListFromObject((IUnknown *)it, &back));
            o_bool("idlist_nonnull", back != NULL);
            if (back) pCoTaskMemFree(back);
            it->lpVtbl->Release(it);
        }
        oracle_case_end();
        if (pidl) pCoTaskMemFree(pidl);
    }

    /* Known folders. */
    {
        IShellItem *it = NULL;
        HRESULT hr = pSHGetKnownFolderItem(&FOLDERID_Documents, KF_FLAG_DEFAULT, NULL, &IID_IShellItem, (void **)&it);
        oracle_case("known_documents");
        o_hex32("hr", (DWORD)hr);
        if (it) {
            o_names(it);
            it->lpVtbl->Release(it);
            it = NULL;
        }
        oracle_case_end();
        hr = pSHGetKnownFolderItem(&FOLDERID_ComputerFolder, KF_FLAG_DEFAULT, NULL, &IID_IShellItem, (void **)&it);
        oracle_case("known_computer");
        o_hex32("hr", (DWORD)hr);
        if (it) {
            o_names(it);
            it->lpVtbl->Release(it);
            it = NULL;
        }
        oracle_case_end();
        hr = pSHCreateItemInKnownFolder(&FOLDERID_Profile, 0, L"setu-no-such-item.txt", &IID_IShellItem, (void **)&it);
        oracle_case("in_known_folder_missing");
        o_hex32("hr", (DWORD)hr);
        if (it) it->lpVtbl->Release(it);
        it = NULL;
        oracle_case_end();
        hr = pSHCreateItemInKnownFolder(&FOLDERID_Profile, 0, L"Documents", &IID_IShellItem, (void **)&it);
        oracle_case("in_known_folder_documents");
        o_hex32("hr", (DWORD)hr);
        if (it) {
            PWSTR s = NULL;
            if (SUCCEEDED(it->lpVtbl->GetDisplayName(it, SIGDN_FILESYSPATH, &s)) && s) {
                o_path("path", s);
                pCoTaskMemFree(s);
            }
            it->lpVtbl->Release(it);
        }
        oracle_case_end();
    }
    return oracle_end();
}
