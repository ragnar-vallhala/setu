/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: CSIDL special folders and PIDL helpers.
 *
 * For each CSIDL: SHGetSpecialFolderLocation (hr), SHGetPathFromIDListW
 * on the result (file-system folders only), SHGetFolderPathW with
 * SHGFP_TYPE_CURRENT and SHGFP_TYPE_DEFAULT, SHGetSpecialFolderPathW, and
 * whether SHGetFolderLocation gives an equal PIDL. Then the IL* helpers:
 * ILCreateFromPathW, ILGetSize (as a boolean), ILFindLastID,
 * ILRemoveLastID, ILCombine, ILIsParent, ILIsEqual, ILClone, ILGetNext,
 * SHGetPathFromIDListEx with a small buffer, SHGetNameFromIDList for the
 * SIGDN_* kinds, SHGetFolderPathW with CSIDL_FLAG_CREATE /
 * CSIDL_FLAG_DONT_VERIFY, and bad CSIDLs.
 *
 * Paths print with <profile>, <public>, <profiles>, <programdata>,
 * <programfiles>, <programfilesx86>, <windir> and <cwd> tokens.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/shell/csidl
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/nf-shlobj_core-shgetspecialfolderlocation
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/nf-shlobj_core-shgetfolderpathw
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/nf-shlobj_core-shgetpathfromidlistw
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/nf-shlobj_core-shgetpathfromidlistex
 *       https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-shgetnamefromidlist
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/nf-shlobj_core-ilcreatefrompathw
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/nf-shlobj_core-ilcombine
 */
#include "oracle.h"
#include <shlobj.h>

static HRESULT(WINAPI *pSHGetSpecialFolderLocation)(HWND, int, PIDLIST_ABSOLUTE *);
static HRESULT(WINAPI *pSHGetFolderLocation)(HWND, int, HANDLE, DWORD, PIDLIST_ABSOLUTE *);
static BOOL(WINAPI *pSHGetPathFromIDListW)(PCIDLIST_ABSOLUTE, LPWSTR);
static BOOL(WINAPI *pSHGetPathFromIDListEx)(PCIDLIST_ABSOLUTE, PWSTR, DWORD, GPFIDL_FLAGS);
static HRESULT(WINAPI *pSHGetFolderPathW)(HWND, int, HANDLE, DWORD, LPWSTR);
static BOOL(WINAPI *pSHGetSpecialFolderPathW)(HWND, LPWSTR, int, BOOL);
static HRESULT(WINAPI *pSHGetNameFromIDList)(PCIDLIST_ABSOLUTE, SIGDN, PWSTR *);
static PIDLIST_ABSOLUTE(WINAPI *pILCreateFromPathW)(PCWSTR);
static UINT(WINAPI *pILGetSize)(PCUIDLIST_RELATIVE);
static PUIDLIST_RELATIVE(WINAPI *pILFindLastID)(PCUIDLIST_RELATIVE);
static BOOL(WINAPI *pILRemoveLastID)(PUIDLIST_RELATIVE);
static PIDLIST_ABSOLUTE(WINAPI *pILCombine)(PCIDLIST_ABSOLUTE, PCUIDLIST_RELATIVE);
static BOOL(WINAPI *pILIsParent)(PCIDLIST_ABSOLUTE, PCIDLIST_ABSOLUTE, BOOL);
static BOOL(WINAPI *pILIsEqual)(PCIDLIST_ABSOLUTE, PCIDLIST_ABSOLUTE);
static PIDLIST_RELATIVE(WINAPI *pILClone)(PCUIDLIST_RELATIVE);
static PUIDLIST_RELATIVE(WINAPI *pILGetNext)(PCUIDLIST_RELATIVE);
static PUIDLIST_RELATIVE(WINAPI *pILFindChild)(PCIDLIST_ABSOLUTE, PCIDLIST_ABSOLUTE);
static void(WINAPI *pILFree)(PIDLIST_RELATIVE);
static void(WINAPI *pCoTaskMemFree)(void *);
static HRESULT(WINAPI *pCoInitializeEx)(void *, DWORD);

#define LOAD(m, f) (*(FARPROC *)&p##f = GetProcAddress(m, #f))

static struct { const wchar_t *path; const wchar_t *tok; } toks[8];
static int ntoks;
static wchar_t cwd[MAX_PATH];

static void add_tok(const wchar_t *env, const wchar_t *tok)
{
    static wchar_t store[8][MAX_PATH];
    if (!GetEnvironmentVariableW(env, store[ntoks], MAX_PATH)) return;
    toks[ntoks].path = store[ntoks];
    toks[ntoks].tok = tok;
    ntoks++;
}

static void o_path(const char *k, const wchar_t *p)
{
    static wchar_t out[2 * MAX_PATH];
    const wchar_t *s = p;
    if (!_wcsnicmp(s, cwd, wcslen(cwd))) {
        swprintf(out, 2 * MAX_PATH, L"<cwd>%ls", s + wcslen(cwd));
        o_wstr(k, out);
        return;
    }
    for (int i = 0; i < ntoks; i++) {
        size_t n = wcslen(toks[i].path);
        if (!_wcsnicmp(s, toks[i].path, n) && (s[n] == 0 || s[n] == L'\\')) {
            swprintf(out, 2 * MAX_PATH, L"%ls%ls", toks[i].tok, s + n);
            o_wstr(k, out);
            return;
        }
    }
    o_wstr(k, p);
}

static const struct { int csidl; const char *name; } csidls[] = {
    {CSIDL_DESKTOP, "DESKTOP"}, {CSIDL_INTERNET, "INTERNET"}, {CSIDL_PROGRAMS, "PROGRAMS"},
    {CSIDL_CONTROLS, "CONTROLS"}, {CSIDL_PRINTERS, "PRINTERS"}, {CSIDL_PERSONAL, "PERSONAL"},
    {CSIDL_FAVORITES, "FAVORITES"}, {CSIDL_STARTUP, "STARTUP"}, {CSIDL_RECENT, "RECENT"},
    {CSIDL_SENDTO, "SENDTO"}, {CSIDL_BITBUCKET, "BITBUCKET"}, {CSIDL_STARTMENU, "STARTMENU"},
    {CSIDL_MYMUSIC, "MYMUSIC"}, {CSIDL_MYVIDEO, "MYVIDEO"}, {CSIDL_DESKTOPDIRECTORY, "DESKTOPDIRECTORY"},
    {CSIDL_DRIVES, "DRIVES"}, {CSIDL_NETWORK, "NETWORK"}, {CSIDL_NETHOOD, "NETHOOD"}, {CSIDL_FONTS, "FONTS"},
    {CSIDL_TEMPLATES, "TEMPLATES"}, {CSIDL_COMMON_STARTMENU, "COMMON_STARTMENU"},
    {CSIDL_COMMON_PROGRAMS, "COMMON_PROGRAMS"}, {CSIDL_COMMON_STARTUP, "COMMON_STARTUP"},
    {CSIDL_COMMON_DESKTOPDIRECTORY, "COMMON_DESKTOPDIRECTORY"}, {CSIDL_APPDATA, "APPDATA"},
    {CSIDL_PRINTHOOD, "PRINTHOOD"}, {CSIDL_LOCAL_APPDATA, "LOCAL_APPDATA"}, {CSIDL_ALTSTARTUP, "ALTSTARTUP"},
    {CSIDL_COMMON_ALTSTARTUP, "COMMON_ALTSTARTUP"}, {CSIDL_COMMON_FAVORITES, "COMMON_FAVORITES"},
    {CSIDL_INTERNET_CACHE, "INTERNET_CACHE"}, {CSIDL_COOKIES, "COOKIES"}, {CSIDL_HISTORY, "HISTORY"},
    {CSIDL_COMMON_APPDATA, "COMMON_APPDATA"}, {CSIDL_WINDOWS, "WINDOWS"}, {CSIDL_SYSTEM, "SYSTEM"},
    {CSIDL_PROGRAM_FILES, "PROGRAM_FILES"}, {CSIDL_MYPICTURES, "MYPICTURES"}, {CSIDL_PROFILE, "PROFILE"},
    {CSIDL_SYSTEMX86, "SYSTEMX86"}, {CSIDL_PROGRAM_FILESX86, "PROGRAM_FILESX86"},
    {CSIDL_PROGRAM_FILES_COMMON, "PROGRAM_FILES_COMMON"}, {CSIDL_PROGRAM_FILES_COMMONX86, "PROGRAM_FILES_COMMONX86"},
    {CSIDL_COMMON_TEMPLATES, "COMMON_TEMPLATES"}, {CSIDL_COMMON_DOCUMENTS, "COMMON_DOCUMENTS"},
    {CSIDL_COMMON_ADMINTOOLS, "COMMON_ADMINTOOLS"}, {CSIDL_ADMINTOOLS, "ADMINTOOLS"},
    {CSIDL_CONNECTIONS, "CONNECTIONS"}, {CSIDL_COMMON_MUSIC, "COMMON_MUSIC"},
    {CSIDL_COMMON_PICTURES, "COMMON_PICTURES"}, {CSIDL_COMMON_VIDEO, "COMMON_VIDEO"},
    {CSIDL_RESOURCES, "RESOURCES"}, {CSIDL_RESOURCES_LOCALIZED, "RESOURCES_LOCALIZED"},
    {CSIDL_COMMON_OEM_LINKS, "COMMON_OEM_LINKS"}, {CSIDL_CDBURN_AREA, "CDBURN_AREA"},
    {CSIDL_COMPUTERSNEARME, "COMPUTERSNEARME"}, {0x3c, "csidl_0x3c"}, {0x7f, "csidl_0x7f"},
};

int main(void)
{
    oracle_begin("shell32/folder_pidls");
    HMODULE sh = LoadLibraryW(L"shell32.dll");
    HMODULE ole = LoadLibraryW(L"ole32.dll");
    LOAD(sh, SHGetSpecialFolderLocation);
    LOAD(sh, SHGetFolderLocation);
    LOAD(sh, SHGetPathFromIDListW);
    LOAD(sh, SHGetPathFromIDListEx);
    LOAD(sh, SHGetFolderPathW);
    LOAD(sh, SHGetSpecialFolderPathW);
    LOAD(sh, SHGetNameFromIDList);
    LOAD(sh, ILCreateFromPathW);
    LOAD(sh, ILGetSize);
    LOAD(sh, ILFindLastID);
    LOAD(sh, ILRemoveLastID);
    LOAD(sh, ILCombine);
    LOAD(sh, ILIsParent);
    LOAD(sh, ILIsEqual);
    LOAD(sh, ILClone);
    LOAD(sh, ILGetNext);
    LOAD(sh, ILFindChild);
    LOAD(sh, ILFree);
    LOAD(ole, CoTaskMemFree);
    LOAD(ole, CoInitializeEx);
    int ok = pSHGetSpecialFolderLocation && pSHGetFolderLocation && pSHGetPathFromIDListW && pSHGetPathFromIDListEx &&
             pSHGetFolderPathW && pSHGetSpecialFolderPathW && pSHGetNameFromIDList && pILCreateFromPathW &&
             pILGetSize && pILFindLastID && pILRemoveLastID && pILCombine && pILIsParent && pILIsEqual && pILClone &&
             pILGetNext && pILFindChild && pILFree && pCoTaskMemFree && pCoInitializeEx;
    oracle_case("setup");
    o_bool("loaded", ok);
    oracle_case_end();
    if (!ok) return oracle_end();
    pCoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    GetCurrentDirectoryW(MAX_PATH, cwd);
    add_tok(L"USERPROFILE", L"<profile>");
    add_tok(L"PUBLIC", L"<public>");
    add_tok(L"ProgramData", L"<programdata>");
    add_tok(L"ProgramFiles(x86)", L"<programfilesx86>");
    add_tok(L"ProgramFiles", L"<programfiles>");
    add_tok(L"SystemRoot", L"<windir>");
    {
        static wchar_t profiles[MAX_PATH];
        if (GetEnvironmentVariableW(L"USERPROFILE", profiles, MAX_PATH)) {
            wchar_t *b = wcsrchr(profiles, L'\\');
            if (b) {
                *b = 0;
                toks[ntoks].path = profiles;
                toks[ntoks].tok = L"<profiles>";
                ntoks++;
            }
        }
    }

    wchar_t buf[MAX_PATH];
    char nm[64];
    for (size_t i = 0; i < sizeof csidls / sizeof csidls[0]; i++) {
        int c = csidls[i].csidl;
        snprintf(nm, sizeof nm, "csidl_%s", csidls[i].name);
        oracle_case(nm);
        PIDLIST_ABSOLUTE pidl = NULL, pidl2 = NULL;
        HRESULT hr = pSHGetSpecialFolderLocation(NULL, c, &pidl);
        o_hex32("loc_hr", (DWORD)hr);
        if (pidl) {
            buf[0] = L'#';
            buf[1] = 0;
            BOOL r = pSHGetPathFromIDListW(pidl, buf);
            o_bool("pidl_path_ok", r);
            if (r) o_path("pidl_path", buf);
            else o_wstr("pidl_path_buf", buf);
            HRESULT h2 = pSHGetFolderLocation(NULL, c, NULL, 0, &pidl2);
            o_hex32("folderloc_hr", (DWORD)h2);
            if (pidl2) o_bool("folderloc_equal", pILIsEqual(pidl, pidl2));
            o_bool("is_desktop_pidl", pILGetSize(pidl) == 2);
        }
        buf[0] = 0;
        hr = pSHGetFolderPathW(NULL, c, NULL, SHGFP_TYPE_CURRENT, buf);
        o_hex32("path_hr", (DWORD)hr);
        if (hr == S_OK) o_path("path", buf);
        buf[0] = 0;
        hr = pSHGetFolderPathW(NULL, c, NULL, SHGFP_TYPE_DEFAULT, buf);
        o_hex32("default_hr", (DWORD)hr);
        if (hr == S_OK) o_path("default", buf);
        buf[0] = 0;
        BOOL r = pSHGetSpecialFolderPathW(NULL, buf, c, FALSE);
        o_bool("special_ok", r);
        oracle_case_end();
        if (pidl) pCoTaskMemFree(pidl);
        if (pidl2) pCoTaskMemFree(pidl2);
    }

    /* Flags. */
    oracle_case("folderpath_flags");
    buf[0] = 0;
    o_hex32("dont_verify_hr", (DWORD)pSHGetFolderPathW(NULL, CSIDL_PERSONAL | CSIDL_FLAG_DONT_VERIFY, NULL, 0, buf));
    o_path("dont_verify", buf);
    o_hex32("create_hr", (DWORD)pSHGetFolderPathW(NULL, CSIDL_PERSONAL | CSIDL_FLAG_CREATE, NULL, 0, buf));
    o_hex32("token_minus1_hr", (DWORD)pSHGetFolderPathW(NULL, CSIDL_PERSONAL, (HANDLE)-1, 0, buf));
    o_path("token_minus1", buf);
    o_hex32("bad_type_hr", (DWORD)pSHGetFolderPathW(NULL, CSIDL_PERSONAL, NULL, 7, buf));
    o_hex32("null_buf_hr", (DWORD)pSHGetFolderPathW(NULL, CSIDL_PERSONAL, NULL, 0, NULL));
    o_hex32("virtual_hr", (DWORD)pSHGetFolderPathW(NULL, CSIDL_DRIVES, NULL, 0, buf));
    o_hex32("bad_csidl_hr", (DWORD)pSHGetFolderPathW(NULL, 0x7f, NULL, 0, buf));
    {
        PIDLIST_ABSOLUTE p = (PIDLIST_ABSOLUTE)0x1;
        o_hex32("bad_loc_hr", (DWORD)pSHGetSpecialFolderLocation(NULL, 0x7f, &p));
        o_bool("bad_loc_null", p == NULL);
        o_hex32("null_out_hr", (DWORD)pSHGetSpecialFolderLocation(NULL, CSIDL_PERSONAL, NULL));
    }
    oracle_case_end();

    /* IL helpers on a path the test owns. */
    CreateDirectoryW(L"p", NULL);
    CreateDirectoryW(L"p\\q", NULL);
    HANDLE h = CreateFileW(L"p\\q\\f.txt", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    CloseHandle(h);
    wchar_t pdir[MAX_PATH], qdir[MAX_PATH], fpath[MAX_PATH];
    swprintf(pdir, MAX_PATH, L"%ls\\p", cwd);
    swprintf(qdir, MAX_PATH, L"%ls\\p\\q", cwd);
    swprintf(fpath, MAX_PATH, L"%ls\\p\\q\\f.txt", cwd);
    PIDLIST_ABSOLUTE pp = pILCreateFromPathW(pdir), pq = pILCreateFromPathW(qdir), pf = pILCreateFromPathW(fpath);
    oracle_case("il_create");
    o_bool("p", pp != NULL);
    o_bool("q", pq != NULL);
    o_bool("f", pf != NULL);
    o_bool("missing_null", pILCreateFromPathW(L"C:\\setu-no-such\\x") == NULL);
    o_bool("relative_null", pILCreateFromPathW(L"p\\q") == NULL);
    oracle_case_end();
    if (pp && pq && pf) {
        oracle_case("il_ops");
        o_bool("size_grows", pILGetSize(pq) > pILGetSize(pp) && pILGetSize(pf) > pILGetSize(pq));
        o_bool("parent_immediate", pILIsParent(pq, pf, TRUE));
        o_bool("parent_grand_immediate", pILIsParent(pp, pf, TRUE));
        o_bool("parent_grand", pILIsParent(pp, pf, FALSE));
        o_bool("parent_self", pILIsParent(pf, pf, FALSE));
        o_bool("parent_reversed", pILIsParent(pf, pp, FALSE));
        PUIDLIST_RELATIVE child = pILFindChild(pp, pf);
        o_bool("findchild_nonnull", child != NULL);
        PIDLIST_ABSOLUTE comb = child ? pILCombine(pp, child) : NULL;
        o_bool("combine_equal", comb && pILIsEqual(comb, pf));
        if (comb) pILFree((PIDLIST_RELATIVE)comb);
        PIDLIST_RELATIVE clone = pILClone((PCUIDLIST_RELATIVE)pf);
        o_bool("clone_equal", clone && pILIsEqual((PCIDLIST_ABSOLUTE)clone, pf));
        o_bool("removelast", clone && pILRemoveLastID(clone));
        o_bool("removelast_is_parent", clone && pILIsEqual((PCIDLIST_ABSOLUTE)clone, pq));
        if (clone) {
            buf[0] = 0;
            pSHGetPathFromIDListW((PCIDLIST_ABSOLUTE)clone, buf);
            o_path("removelast_path", buf);
            pILFree(clone);
        }
        PUIDLIST_RELATIVE last = pILFindLastID((PCUIDLIST_RELATIVE)pf);
        o_bool("lastid_next_is_end", last && pILGetNext(last) == NULL);
        o_bool("lastid_size", last && pILGetSize(last) == pILGetSize(pf) - pILGetSize(pq) + 2);
        o_bool("equal_null_null", pILIsEqual(NULL, NULL));
        o_bool("size_null_zero", pILGetSize(NULL) == 0);
        oracle_case_end();

        oracle_case("path_from_idlist");
        buf[0] = 0;
        o_bool("file_ok", pSHGetPathFromIDListW(pf, buf));
        o_path("file", buf);
        wchar_t small[8];
        wcscpy(small, L"#");
        o_bool("ex_small_ok", pSHGetPathFromIDListEx(pf, small, 8, GPFIDL_DEFAULT));
        o_wstr("ex_small", small);
        buf[0] = 0;
        o_bool("ex_full_ok", pSHGetPathFromIDListEx(pf, buf, MAX_PATH, GPFIDL_DEFAULT));
        o_path("ex_full", buf);
        oracle_case_end();

        static const struct { SIGDN k; const char *name; } sig[] = {
            {SIGDN_NORMALDISPLAY, "normal"}, {SIGDN_PARENTRELATIVEPARSING, "parent_parsing"},
            {SIGDN_DESKTOPABSOLUTEPARSING, "desktop_parsing"}, {SIGDN_PARENTRELATIVEEDITING, "parent_editing"},
            {SIGDN_DESKTOPABSOLUTEEDITING, "desktop_editing"}, {SIGDN_FILESYSPATH, "filesyspath"},
            {SIGDN_URL, "url"}, {SIGDN_PARENTRELATIVEFORADDRESSBAR, "parent_addressbar"},
            {SIGDN_PARENTRELATIVE, "parent_relative"},
        };
        oracle_case("name_from_idlist_file");
        for (size_t i = 0; i < sizeof sig / sizeof sig[0]; i++) {
            PWSTR s = NULL;
            char key[48];
            HRESULT hr = pSHGetNameFromIDList(pf, sig[i].k, &s);
            snprintf(key, sizeof key, "%s_hr", sig[i].name);
            o_hex32(key, (DWORD)hr);
            if (SUCCEEDED(hr) && s) {
                if (sig[i].k == SIGDN_URL) {
                    /* file:///C:/... - strip the cwd part. */
                    const wchar_t *f = wcsstr(s, L"/p/q/");
                    o_wstr(sig[i].name, f ? f : s);
                } else o_path(sig[i].name, s);
                pCoTaskMemFree(s);
            }
        }
        oracle_case_end();
        pILFree((PIDLIST_RELATIVE)pp);
        pILFree((PIDLIST_RELATIVE)pq);
        pILFree((PIDLIST_RELATIVE)pf);
    }

    /* Names of virtual folders. */
    static const struct { int c; const char *name; } virt[] = {
        {CSIDL_DESKTOP, "desktop"}, {CSIDL_DRIVES, "drives"}, {CSIDL_BITBUCKET, "bitbucket"},
        {CSIDL_PERSONAL, "personal"}, {CSIDL_CONTROLS, "controls"}, {CSIDL_NETWORK, "network"},
    };
    for (size_t i = 0; i < sizeof virt / sizeof virt[0]; i++) {
        PIDLIST_ABSOLUTE p = NULL;
        pSHGetSpecialFolderLocation(NULL, virt[i].c, &p);
        snprintf(nm, sizeof nm, "name_%s", virt[i].name);
        oracle_case(nm);
        if (p) {
            PWSTR s = NULL;
            HRESULT hr = pSHGetNameFromIDList(p, SIGDN_NORMALDISPLAY, &s);
            o_hex32("normal_hr", (DWORD)hr);
            if (SUCCEEDED(hr) && s) { o_wstr("normal", s); pCoTaskMemFree(s); }
            s = NULL;
            hr = pSHGetNameFromIDList(p, SIGDN_DESKTOPABSOLUTEPARSING, &s);
            o_hex32("parsing_hr", (DWORD)hr);
            if (SUCCEEDED(hr) && s) { o_path("parsing", s); pCoTaskMemFree(s); }
            s = NULL;
            hr = pSHGetNameFromIDList(p, SIGDN_FILESYSPATH, &s);
            o_hex32("filesys_hr", (DWORD)hr);
            if (SUCCEEDED(hr) && s) { o_path("filesys", s); pCoTaskMemFree(s); }
            pCoTaskMemFree(p);
        }
        oracle_case_end();
    }
    return oracle_end();
}
