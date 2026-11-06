/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: the Recycle Bin. SHFileOperationW FO_DELETE with
 * FOF_ALLOWUNDO (single file, several files, a directory tree, a missing
 * file), SHQueryRecycleBinW (item count and total size for C:\, for a path
 * on C:, for NULL = all drives, bad cbSize), enumerating the Recycle Bin
 * folder through IShellFolder (CSIDL_BITBUCKET) with in-folder display
 * names, and SHEmptyRecycleBinW (flags, empty bin, bad root).
 *
 * The test empties the Recycle Bin of C: first, so both runs start from
 * the same state (the harness gives Setu a private trash; see
 * oracle/setu_oracle/targets.py). No UI: FOF_NOCONFIRMATION | FOF_SILENT |
 * FOF_NOERRORUI and SHERB_NOCONFIRMATION | SHERB_NOPROGRESSUI |
 * SHERB_NOSOUND everywhere.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-shfileoperationw
 *       https://learn.microsoft.com/en-us/windows/win32/api/shellapi/ns-shellapi-shfileopstructw
 *       https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-shqueryrecyclebinw
 *       https://learn.microsoft.com/en-us/windows/win32/api/shellapi/ns-shellapi-shqueryrbinfo
 *       https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-shemptyrecyclebinw
 *       https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nn-shobjidl_core-ishellfolder
 *       https://learn.microsoft.com/en-us/windows/win32/shell/csidl
 */
#define INITGUID
#include "oracle.h"
#include <shlobj.h>
#include <shellapi.h>
#include <shlwapi.h>

static int(WINAPI *pSHFileOperationW)(SHFILEOPSTRUCTW *);
static HRESULT(WINAPI *pSHQueryRecycleBinW)(LPCWSTR, SHQUERYRBINFO *);
static HRESULT(WINAPI *pSHEmptyRecycleBinW)(HWND, LPCWSTR, DWORD);
static HRESULT(WINAPI *pSHGetDesktopFolder)(IShellFolder **);
static HRESULT(WINAPI *pSHGetSpecialFolderLocation)(HWND, int, PIDLIST_ABSOLUTE *);
static HRESULT(WINAPI *pStrRetToBufW)(STRRET *, PCUITEMID_CHILD, LPWSTR, UINT);
static HRESULT(WINAPI *pCoInitializeEx)(void *, DWORD);
static void(WINAPI *pCoTaskMemFree)(void *);

#define LOAD(m, f) (*(FARPROC *)&p##f = GetProcAddress(m, #f))
#define QUIET (FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI | FOF_NOCONFIRMMKDIR)
#define ERB (SHERB_NOCONFIRMATION | SHERB_NOPROGRESSUI | SHERB_NOSOUND)

static void make_file(const wchar_t *name, DWORD size)
{
    HANDLE h = CreateFileW(name, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    static char b[4096];
    DWORD done;
    memset(b, 'r', sizeof b);
    if (h == INVALID_HANDLE_VALUE) return;
    WriteFile(h, b, size, &done, NULL);
    CloseHandle(h);
}

static void query(const char *k, const wchar_t *root)
{
    SHQUERYRBINFO qi;
    char key[64];
    memset(&qi, 0xcc, sizeof qi);
    qi.cbSize = sizeof qi;
    HRESULT hr = pSHQueryRecycleBinW(root, &qi);
    snprintf(key, sizeof key, "%s.hr", k);
    o_hex32(key, (DWORD)hr);
    if (SUCCEEDED(hr)) {
        snprintf(key, sizeof key, "%s.size", k);
        o_i64(key, qi.i64Size);
        snprintf(key, sizeof key, "%s.items", k);
        o_i64(key, qi.i64NumItems);
    }
}

static int del(const wchar_t *from, FILEOP_FLAGS flags)
{
    SHFILEOPSTRUCTW op;
    memset(&op, 0, sizeof op);
    op.wFunc = FO_DELETE;
    op.pFrom = from;
    op.fFlags = flags;
    return pSHFileOperationW(&op);
}

static int wcmp(const void *a, const void *b) { return wcscmp(*(wchar_t *const *)a, *(wchar_t *const *)b); }

/* List the Recycle Bin folder's items by SHGDN_INFOLDER name, sorted. */
static void o_bin_items(const char *k)
{
    IShellFolder *desk = NULL, *bin = NULL;
    PIDLIST_ABSOLUTE pidl = NULL;
    IEnumIDList *en = NULL;
    static wchar_t buf[4096];
    wchar_t *names[64];
    int n = 0;
    char key[64];
    buf[0] = 0;
    HRESULT hr = pSHGetDesktopFolder(&desk);
    if (SUCCEEDED(hr)) hr = pSHGetSpecialFolderLocation(NULL, CSIDL_BITBUCKET, &pidl);
    if (SUCCEEDED(hr)) hr = desk->lpVtbl->BindToObject(desk, pidl, NULL, &IID_IShellFolder, (void **)&bin);
    if (SUCCEEDED(hr)) hr = bin->lpVtbl->EnumObjects(bin, NULL, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS | SHCONTF_INCLUDEHIDDEN, &en);
    snprintf(key, sizeof key, "%s.enum_hr", k);
    o_hex32(key, (DWORD)hr);
    if (hr == S_OK && en) {
        PITEMID_CHILD child;
        ULONG got;
        while (n < 64 && en->lpVtbl->Next(en, 1, &child, &got) == S_OK && got == 1) {
            STRRET sr;
            names[n] = malloc(MAX_PATH * sizeof(wchar_t));
            names[n][0] = 0;
            if (SUCCEEDED(bin->lpVtbl->GetDisplayNameOf(bin, child, SHGDN_INFOLDER, &sr)))
                pStrRetToBufW(&sr, child, names[n], MAX_PATH);
            SFGAOF attrs = SFGAO_FOLDER | SFGAO_CANDELETE | SFGAO_FILESYSTEM;
            bin->lpVtbl->GetAttributesOf(bin, 1, (PCUITEMID_CHILD_ARRAY)&child, &attrs);
            if (attrs & SFGAO_FOLDER) wcscat(names[n], L"/");
            n++;
            pCoTaskMemFree(child);
        }
        en->lpVtbl->Release(en);
    }
    qsort(names, n, sizeof names[0], wcmp);
    for (int i = 0; i < n; i++) {
        if (i) wcscat(buf, L",");
        wcscat(buf, names[i]);
        free(names[i]);
    }
    snprintf(key, sizeof key, "%s.count", k);
    o_u32(key, n);
    snprintf(key, sizeof key, "%s.names", k);
    o_wstr(key, buf);
    if (bin) bin->lpVtbl->Release(bin);
    if (desk) desk->lpVtbl->Release(desk);
    if (pidl) pCoTaskMemFree(pidl);
}

int main(void)
{
    oracle_begin("shell32/recycle_bin");
    HMODULE sh = LoadLibraryW(L"shell32.dll");
    HMODULE ole = LoadLibraryW(L"ole32.dll");
    HMODULE shl = LoadLibraryW(L"shlwapi.dll");
    LOAD(sh, SHFileOperationW);
    LOAD(sh, SHQueryRecycleBinW);
    LOAD(sh, SHEmptyRecycleBinW);
    LOAD(sh, SHGetDesktopFolder);
    LOAD(sh, SHGetSpecialFolderLocation);
    LOAD(shl, StrRetToBufW);
    LOAD(ole, CoInitializeEx);
    LOAD(ole, CoTaskMemFree);
    oracle_case("setup");
    o_bool("loaded", pSHFileOperationW && pSHQueryRecycleBinW && pSHEmptyRecycleBinW && pSHGetDesktopFolder &&
                     pSHGetSpecialFolderLocation && pStrRetToBufW && pCoInitializeEx && pCoTaskMemFree);
    oracle_case_end();
    if (!pSHFileOperationW || !pSHQueryRecycleBinW || !pSHEmptyRecycleBinW || !pSHGetDesktopFolder ||
        !pSHGetSpecialFolderLocation || !pStrRetToBufW || !pCoInitializeEx || !pCoTaskMemFree)
        return oracle_end();
    pCoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    /* Start empty (result not printed: it depends on what was there). */
    pSHEmptyRecycleBinW(NULL, L"C:\\", ERB);
    HRESULT hr = pSHEmptyRecycleBinW(NULL, L"C:\\", ERB);
    oracle_case("start_empty");
    o_hex32("empty_again_hr", (DWORD)hr);
    query("q", L"C:\\");
    o_bin_items("bin");
    oracle_case_end();

    make_file(L"rb_one.txt", 100);
    int r = del(L"rb_one.txt\0", QUIET | FOF_ALLOWUNDO);
    oracle_case("delete_one_file");
    o_hex32("ret", (DWORD)r);
    o_bool("gone", GetFileAttributesW(L"rb_one.txt") == INVALID_FILE_ATTRIBUTES);
    query("q", L"C:\\");
    o_bin_items("bin");
    oracle_case_end();

    make_file(L"rb_two.txt", 200);
    make_file(L"rb_three.dat", 300);
    r = del(L"rb_two.txt\0rb_three.dat\0", QUIET | FOF_ALLOWUNDO);
    oracle_case("delete_two_files");
    o_hex32("ret", (DWORD)r);
    o_bool("gone", GetFileAttributesW(L"rb_two.txt") == INVALID_FILE_ATTRIBUTES &&
                   GetFileAttributesW(L"rb_three.dat") == INVALID_FILE_ATTRIBUTES);
    query("q", L"C:\\");
    oracle_case_end();

    CreateDirectoryW(L"rb_dir", NULL);
    make_file(L"rb_dir\\x.txt", 10);
    CreateDirectoryW(L"rb_dir\\sub", NULL);
    make_file(L"rb_dir\\sub\\y.txt", 20);
    r = del(L"rb_dir\0", QUIET | FOF_ALLOWUNDO);
    oracle_case("delete_dir_tree");
    o_hex32("ret", (DWORD)r);
    o_bool("gone", GetFileAttributesW(L"rb_dir") == INVALID_FILE_ATTRIBUTES);
    query("q", L"C:\\");
    o_bin_items("bin");
    oracle_case_end();

    /* The same name twice: both copies stay in the bin. */
    make_file(L"rb_one.txt", 7);
    r = del(L"rb_one.txt\0", QUIET | FOF_ALLOWUNDO);
    oracle_case("delete_same_name_again");
    o_hex32("ret", (DWORD)r);
    query("q", L"C:\\");
    o_bin_items("bin");
    oracle_case_end();

    r = del(L"rb_missing.txt\0", QUIET | FOF_ALLOWUNDO);
    oracle_case("delete_missing");
    o_hex32("ret", (DWORD)r);
    query("q", L"C:\\");
    oracle_case_end();

    make_file(L"rb_ro.txt", 5);
    SetFileAttributesW(L"rb_ro.txt", FILE_ATTRIBUTE_READONLY);
    r = del(L"rb_ro.txt\0", QUIET | FOF_ALLOWUNDO);
    oracle_case("delete_readonly");
    o_hex32("ret", (DWORD)r);
    o_bool("gone", GetFileAttributesW(L"rb_ro.txt") == INVALID_FILE_ATTRIBUTES);
    query("q", L"C:\\");
    oracle_case_end();
    SetFileAttributesW(L"rb_ro.txt", 0);

    /* ---- SHQueryRecycleBinW variants ---- */
    {
        wchar_t cwd[MAX_PATH];
        GetCurrentDirectoryW(MAX_PATH, cwd);
        oracle_case("query_variants");
        query("cwd", cwd);
        query("drive_no_backslash", L"C:");
        query("all_drives_null", NULL);
        query("all_drives_empty", L"");
        query("missing_drive", L"Q:\\");
        SHQUERYRBINFO qi;
        memset(&qi, 0, sizeof qi);
        qi.cbSize = 0;
        o_hex32("cbsize_zero_hr", (DWORD)pSHQueryRecycleBinW(L"C:\\", &qi));
        qi.cbSize = sizeof qi + 8;
        o_hex32("cbsize_big_hr", (DWORD)pSHQueryRecycleBinW(L"C:\\", &qi));
        oracle_case_end();
    }

    /* ---- SHEmptyRecycleBinW ---- */
    hr = pSHEmptyRecycleBinW(NULL, L"Q:\\", ERB);
    oracle_case("empty_missing_drive");
    o_hex32("hr", (DWORD)hr);
    query("q", L"C:\\");
    oracle_case_end();
    hr = pSHEmptyRecycleBinW(NULL, L"C:\\", ERB);
    oracle_case("empty_c");
    o_hex32("hr", (DWORD)hr);
    query("q", L"C:\\");
    o_bin_items("bin");
    oracle_case_end();
    hr = pSHEmptyRecycleBinW(NULL, L"C:\\", ERB);
    oracle_case("empty_c_already_empty");
    o_hex32("hr", (DWORD)hr);
    oracle_case_end();
    hr = pSHEmptyRecycleBinW(NULL, NULL, ERB);
    oracle_case("empty_all_already_empty");
    o_hex32("hr", (DWORD)hr);
    oracle_case_end();
    return oracle_end();
}
