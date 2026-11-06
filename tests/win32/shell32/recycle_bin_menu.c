/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: the context menu of an item in the Recycle Bin, as a file
 * manager uses it. The test recycles files, gets IContextMenu for one
 * item of the Recycle Bin folder (CSIDL_BITBUCKET) with GetUIObjectOf,
 * fills a menu with QueryContextMenu, and asks GetCommandString for the
 * verbs of its commands. It then runs commands by canonical verb (ANSI and
 * Unicode lpVerb) and by menu offset: "undelete" puts the file back where it
 * was, and an unknown verb fails. ("delete" is only looked up, not run:
 * on Windows it did not return within the harness time limit, even with
 * CMIC_MASK_FLAG_NO_UI.)
 *
 * The test empties the Recycle Bin of C: first, so both runs start from
 * the same state. No UI: FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI
 * for the deletes, CMIC_MASK_FLAG_NO_UI for every InvokeCommand.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nn-shobjidl_core-icontextmenu
 *       https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-icontextmenu-querycontextmenu
 *       https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-icontextmenu-getcommandstring
 *       https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-icontextmenu-invokecommand
 *       https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/ns-shobjidl_core-cminvokecommandinfoex
 *       https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ishellfolder-getuiobjectof
 *       https://learn.microsoft.com/en-us/windows/win32/shell/csidl
 */
#define INITGUID
#include "oracle.h"
#include <shlobj.h>
#include <shellapi.h>
#include <shlwapi.h>

static int(WINAPI *pSHFileOperationW)(SHFILEOPSTRUCTW *);
static HRESULT(WINAPI *pSHEmptyRecycleBinW)(HWND, LPCWSTR, DWORD);
static HRESULT(WINAPI *pSHGetDesktopFolder)(IShellFolder **);
static HRESULT(WINAPI *pSHGetSpecialFolderLocation)(HWND, int, PIDLIST_ABSOLUTE *);
static HRESULT(WINAPI *pStrRetToBufW)(STRRET *, PCUITEMID_CHILD, LPWSTR, UINT);
static HRESULT(WINAPI *pCoInitializeEx)(void *, DWORD);
static void(WINAPI *pCoTaskMemFree)(void *);

#define LOAD(m, f) (*(FARPROC *)&p##f = GetProcAddress(m, #f))
#define QUIET (FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI | FOF_NOCONFIRMMKDIR)
#define ERB (SHERB_NOCONFIRMATION | SHERB_NOPROGRESSUI | SHERB_NOSOUND)
#define FIRST 1

static IShellFolder *bin;

static void make_file(const wchar_t *name)
{
    HANDLE h = CreateFileW(name, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    DWORD done;
    if (h == INVALID_HANDLE_VALUE) return;
    WriteFile(h, "recycle", 7, &done, NULL);
    CloseHandle(h);
}

static int recycle(const wchar_t *from)
{
    SHFILEOPSTRUCTW op;
    memset(&op, 0, sizeof op);
    op.wFunc = FO_DELETE;
    op.pFrom = from;
    op.fFlags = QUIET | FOF_ALLOWUNDO;
    return pSHFileOperationW(&op);
}

static BOOL exists(const wchar_t *name) { return GetFileAttributesW(name) != INVALID_FILE_ATTRIBUTES; }

/* The Recycle Bin item for the file NAME, or NULL. */
static PITEMID_CHILD find_item(const wchar_t *name)
{
    IEnumIDList *en = NULL;
    PITEMID_CHILD child, found = NULL;
    ULONG got;
    if (bin->lpVtbl->EnumObjects(bin, NULL, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS | SHCONTF_INCLUDEHIDDEN, &en) != S_OK || !en)
        return NULL;
    while (!found && en->lpVtbl->Next(en, 1, &child, &got) == S_OK && got == 1) {
        STRRET sr;
        wchar_t buf[MAX_PATH] = L"";
        if (SUCCEEDED(bin->lpVtbl->GetDisplayNameOf(bin, child, SHGDN_INFOLDER, &sr)))
            pStrRetToBufW(&sr, child, buf, MAX_PATH);
        /* the in-folder name may hide a registered extension */
        size_t len = wcslen(buf);
        if (!wcscmp(buf, name) || (len && !wcsncmp(buf, name, len) && name[len] == '.')) found = child;
        else pCoTaskMemFree(child);
    }
    en->lpVtbl->Release(en);
    return found;
}

static ULONG bin_count(void)
{
    IEnumIDList *en = NULL;
    PITEMID_CHILD child;
    ULONG got, n = 0;
    if (bin->lpVtbl->EnumObjects(bin, NULL, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS | SHCONTF_INCLUDEHIDDEN, &en) != S_OK || !en)
        return ~0u;
    while (en->lpVtbl->Next(en, 1, &child, &got) == S_OK && got == 1) {
        n++;
        pCoTaskMemFree(child);
    }
    en->lpVtbl->Release(en);
    return n;
}

static IContextMenu *item_menu(const wchar_t *name, HRESULT *hr)
{
    IContextMenu *cm = NULL;
    PITEMID_CHILD child = find_item(name);
    *hr = E_FAIL;
    if (!child) return NULL;
    *hr = bin->lpVtbl->GetUIObjectOf(bin, NULL, 1, (PCUITEMID_CHILD_ARRAY)&child, &IID_IContextMenu, NULL, (void **)&cm);
    pCoTaskMemFree(child);
    return SUCCEEDED(*hr) ? cm : NULL;
}

/* The menu offset of the command whose GCS_VERBW verb is VERB, or -1. */
static int find_verb(IContextMenu *cm, HMENU menu, const wchar_t *verb)
{
    int n = GetMenuItemCount(menu);
    for (int i = 0; i < n; i++) {
        UINT id = GetMenuItemID(menu, i);
        wchar_t buf[64] = L"";
        if (id == (UINT)-1 || id < FIRST) continue;
        if (SUCCEEDED(cm->lpVtbl->GetCommandString(cm, id - FIRST, GCS_VERBW, NULL, (char *)buf, 64)) && !wcscmp(buf, verb))
            return id - FIRST;
    }
    return -1;
}

static HRESULT invoke(IContextMenu *cm, const char *verb, const wchar_t *verbw)
{
    CMINVOKECOMMANDINFOEX ici;
    memset(&ici, 0, sizeof ici);
    ici.cbSize = sizeof ici;
    ici.fMask = CMIC_MASK_FLAG_NO_UI | (verbw ? CMIC_MASK_UNICODE : 0);
    ici.lpVerb = verb;
    ici.lpVerbW = verbw;
    ici.nShow = SW_HIDE;
    return cm->lpVtbl->InvokeCommand(cm, (CMINVOKECOMMANDINFO *)&ici);
}

/* Recycle NAME and open its item's menu; reports the setup in the case. */
static IContextMenu *recycled_menu(const wchar_t *name, HMENU *menu)
{
    wchar_t from[MAX_PATH + 1];
    HRESULT hr;
    make_file(name);
    wcscpy(from, name);
    from[wcslen(from) + 1] = 0;
    o_hex32("recycle_ret", (DWORD)recycle(from));
    IContextMenu *cm = item_menu(name, &hr);
    o_hex32("menu_hr", (DWORD)hr);
    *menu = CreatePopupMenu();
    if (cm) {
        hr = cm->lpVtbl->QueryContextMenu(cm, *menu, 0, FIRST, 0x7fff, CMF_NORMAL);
        o_bool("query_ok", SUCCEEDED(hr));
    }
    return cm;
}

int main(void)
{
    oracle_begin("shell32/recycle_bin_menu");
    HMODULE sh = LoadLibraryW(L"shell32.dll");
    HMODULE ole = LoadLibraryW(L"ole32.dll");
    HMODULE shl = LoadLibraryW(L"shlwapi.dll");
    LOAD(sh, SHFileOperationW);
    LOAD(sh, SHEmptyRecycleBinW);
    LOAD(sh, SHGetDesktopFolder);
    LOAD(sh, SHGetSpecialFolderLocation);
    LOAD(shl, StrRetToBufW);
    LOAD(ole, CoInitializeEx);
    LOAD(ole, CoTaskMemFree);
    BOOL loaded = pSHFileOperationW && pSHEmptyRecycleBinW && pSHGetDesktopFolder && pSHGetSpecialFolderLocation &&
                  pStrRetToBufW && pCoInitializeEx && pCoTaskMemFree;
    oracle_case("setup");
    o_bool("loaded", loaded);
    if (!loaded) {
        oracle_case_end();
        return oracle_end();
    }
    pCoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    pSHEmptyRecycleBinW(NULL, L"C:\\", ERB);
    IShellFolder *desk = NULL;
    PIDLIST_ABSOLUTE pidl = NULL;
    HRESULT hr = pSHGetDesktopFolder(&desk);
    if (SUCCEEDED(hr)) hr = pSHGetSpecialFolderLocation(NULL, CSIDL_BITBUCKET, &pidl);
    if (SUCCEEDED(hr)) hr = desk->lpVtbl->BindToObject(desk, pidl, NULL, &IID_IShellFolder, (void **)&bin);
    o_hex32("bind_hr", (DWORD)hr);
    o_u32("bin_count", bin ? bin_count() : ~0u);
    oracle_case_end();
    if (!bin) return oracle_end();

    HMENU menu;
    IContextMenu *cm;
    int off;
    char a[64];

    /* GetCommandString for the "undelete" and "delete" commands. */
    oracle_case("command_strings");
    cm = recycled_menu(L"rbm_strings.txt", &menu);
    if (cm) {
        off = find_verb(cm, menu, L"undelete");
        o_bool("has_undelete", off >= 0);
        if (off >= 0) {
            memset(a, 0, sizeof a);
            o_hex32("undelete_verba_hr", (DWORD)cm->lpVtbl->GetCommandString(cm, off, GCS_VERBA, NULL, a, sizeof a));
            o_str("undelete_verba", a);
            o_hex32("undelete_validate_hr", (DWORD)cm->lpVtbl->GetCommandString(cm, off, GCS_VALIDATEW, NULL, NULL, 0));
        }
        o_bool("has_delete", find_verb(cm, menu, L"delete") >= 0);
        wchar_t w[64] = L"";
        o_hex32("bad_id_hr", (DWORD)cm->lpVtbl->GetCommandString(cm, 0x7000, GCS_VERBW, NULL, (char *)w, 64));
        cm->lpVtbl->Release(cm);
    }
    DestroyMenu(menu);
    oracle_case_end();
    pSHEmptyRecycleBinW(NULL, L"C:\\", ERB);

    /* An ANSI canonical verb. */
    oracle_case("invoke_undelete_ansi");
    cm = recycled_menu(L"rbm_ansi.txt", &menu);
    if (cm) {
        o_hex32("hr", (DWORD)invoke(cm, "undelete", NULL));
        cm->lpVtbl->Release(cm);
    }
    DestroyMenu(menu);
    o_bool("restored", exists(L"rbm_ansi.txt"));
    o_u32("bin_count", bin_count());
    oracle_case_end();
    DeleteFileW(L"rbm_ansi.txt");

    /* A Unicode canonical verb, in another case. */
    oracle_case("invoke_undelete_unicode");
    cm = recycled_menu(L"rbm_unicode.txt", &menu);
    if (cm) {
        o_hex32("hr", (DWORD)invoke(cm, "UNDELETE", L"UnDelete"));
        cm->lpVtbl->Release(cm);
    }
    DestroyMenu(menu);
    o_bool("restored", exists(L"rbm_unicode.txt"));
    o_u32("bin_count", bin_count());
    oracle_case_end();
    DeleteFileW(L"rbm_unicode.txt");

    /* The menu offset of "undelete". */
    oracle_case("invoke_undelete_offset");
    cm = recycled_menu(L"rbm_offset.txt", &menu);
    if (cm) {
        off = find_verb(cm, menu, L"undelete");
        o_hex32("hr", off >= 0 ? (DWORD)invoke(cm, MAKEINTRESOURCEA(off), NULL) : 0xdeadu);
        cm->lpVtbl->Release(cm);
    }
    DestroyMenu(menu);
    o_bool("restored", exists(L"rbm_offset.txt"));
    o_u32("bin_count", bin_count());
    oracle_case_end();
    DeleteFileW(L"rbm_offset.txt");

    /* A verb the menu doesn't have: nothing happens. */
    oracle_case("invoke_unknown_verb");
    cm = recycled_menu(L"rbm_unknown.txt", &menu);
    if (cm) {
        o_bool("hr_failed", FAILED(invoke(cm, "nosuchverb", NULL)));
        cm->lpVtbl->Release(cm);
    }
    DestroyMenu(menu);
    o_bool("restored", exists(L"rbm_unknown.txt"));
    o_u32("bin_count", bin_count());
    oracle_case_end();

    pSHEmptyRecycleBinW(NULL, L"C:\\", ERB);
    bin->lpVtbl->Release(bin);
    desk->lpVtbl->Release(desk);
    pCoTaskMemFree(pidl);
    return oracle_end();
}
