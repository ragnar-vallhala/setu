/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: IShellLinkW + IPersistFile round trips. A link is filled in
 * through IShellLinkW (path, arguments, working directory, icon location,
 * show command, hotkey, description, relative path), saved with
 * IPersistFile::Save, loaded into a fresh object and read back. Also: the
 * values a new, empty link returns; SetPath with environment strings, a
 * directory, a missing file and a relative name; GetPath flags
 * (SLGP_RAWPATH, SLGP_SHORTPATH) and the WIN32_FIND_DATAW it fills in;
 * IsDirty / GetCurFile / GetClassID; IShellLinkDataList GetFlags / SetFlags
 * and data blocks; Load errors.
 *
 * Paths print with the working directory as "<cwd>" (its 8.3 form as
 * "<shortcwd>"), the Windows directory as "<windir>" and the user's profile
 * as "<profile>" (SetPath resolves relative names against the Desktop). No time stamps are printed.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nn-shobjidl_core-ishelllinkw
 *       https://learn.microsoft.com/en-us/windows/win32/api/objidl/nn-objidl-ipersistfile
 *       https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ishelllinkw-getpath
 *       https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ishelllinkw-sethotkey
 *       https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nn-shobjidl_core-ishelllinkdatalist
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/ne-shlobj_core-shell_link_data_flags
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/ns-shlobj_core-exp_sz_link
 *       https://learn.microsoft.com/en-us/windows/win32/shell/links
 */
#define INITGUID
#include "oracle.h"
#include <shlobj.h>
#include <shobjidl.h>

static HRESULT(WINAPI *pCoInitializeEx)(void *, DWORD);
static HRESULT(WINAPI *pCoCreateInstance)(REFCLSID, IUnknown *, DWORD, REFIID, void **);
static void(WINAPI *pCoTaskMemFree)(void *);
static void *(WINAPI *pLocalFree)(void *);

#define LOAD(m, f) (*(FARPROC *)&p##f = GetProcAddress(m, #f))

static wchar_t cwd[MAX_PATH], windir[MAX_PATH], shortcwd[MAX_PATH], profile[MAX_PATH];

static int has_prefix_ci(const wchar_t *s, const wchar_t *p)
{
    size_t n = wcslen(p);
    return n && !_wcsnicmp(s, p, n) && (s[n] == 0 || s[n] == L'\\');
}

static void o_path(const char *k, const wchar_t *s)
{
    wchar_t out[4 * MAX_PATH];
    if (!s) { o_str(k, NULL); return; }
    if (has_prefix_ci(s, cwd)) swprintf(out, 4 * MAX_PATH, L"<cwd>%ls", s + wcslen(cwd));
    else if (shortcwd[0] && has_prefix_ci(s, shortcwd)) swprintf(out, 4 * MAX_PATH, L"<shortcwd>%ls", s + wcslen(shortcwd));
    else if (has_prefix_ci(s, windir)) swprintf(out, 4 * MAX_PATH, L"<windir>%ls", s + wcslen(windir));
    else if (has_prefix_ci(s, profile)) swprintf(out, 4 * MAX_PATH, L"<profile>%ls", s + wcslen(profile));
    else swprintf(out, 4 * MAX_PATH, L"%ls", s);
    o_wstr(k, out);
}

static IShellLinkW *new_link(void)
{
    IShellLinkW *sl = NULL;
    pCoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkW, (void **)&sl);
    return sl;
}

static HRESULT save_link(IShellLinkW *sl, const wchar_t *file)
{
    IPersistFile *pf = NULL;
    HRESULT hr = sl->lpVtbl->QueryInterface(sl, &IID_IPersistFile, (void **)&pf);
    if (FAILED(hr)) return hr;
    hr = pf->lpVtbl->Save(pf, file, TRUE);
    pf->lpVtbl->Release(pf);
    return hr;
}

static IShellLinkW *load_link(const wchar_t *file, HRESULT *phr)
{
    IShellLinkW *sl = new_link();
    IPersistFile *pf = NULL;
    *phr = E_FAIL;
    if (!sl) return NULL;
    sl->lpVtbl->QueryInterface(sl, &IID_IPersistFile, (void **)&pf);
    if (!pf) return sl;
    *phr = pf->lpVtbl->Load(pf, file, STGM_READ);
    pf->lpVtbl->Release(pf);
    return sl;
}

static DWORD link_flags(IShellLinkW *sl)
{
    IShellLinkDataList *dl = NULL;
    DWORD f = 0xdeadbeef;
    if (SUCCEEDED(sl->lpVtbl->QueryInterface(sl, &IID_IShellLinkDataList, (void **)&dl))) {
        dl->lpVtbl->GetFlags(dl, &f);
        dl->lpVtbl->Release(dl);
    }
    return f;
}

/* Print every IShellLinkW getter. */
static void o_link(IShellLinkW *sl, DWORD pathflags)
{
    wchar_t buf[4 * MAX_PATH];
    WIN32_FIND_DATAW fd;
    HRESULT hr;
    int i;
    WORD w;

    memset(&fd, 0xcc, sizeof fd);
    wmemset(buf, L'#', 8); buf[8] = 0;
    hr = sl->lpVtbl->GetPath(sl, buf, 4 * MAX_PATH, &fd, pathflags);
    o_hex32("path_hr", (DWORD)hr);
    o_path("path", buf[0] == L'#' ? L"(untouched)" : buf);
    o_bool("fd_untouched", fd.dwFileAttributes == 0xcccccccc);
    if (fd.dwFileAttributes != 0xcccccccc) {
        /* Directory sizes and the drive root's attributes depend on the file system. */
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            o_hex32("fd_attrs", fd.dwFileAttributes);
            o_u32("fd_size", fd.nFileSizeLow);
        } else {
            o_bool("fd_is_dir", 1);
        }
        o_wstr("fd_name", fd.cFileName);
    }
    wmemset(buf, L'#', 8); buf[8] = 0;
    hr = sl->lpVtbl->GetArguments(sl, buf, 4 * MAX_PATH);
    o_hex32("args_hr", (DWORD)hr);
    o_wstr("args", buf);
    wmemset(buf, L'#', 8); buf[8] = 0;
    hr = sl->lpVtbl->GetWorkingDirectory(sl, buf, 4 * MAX_PATH);
    o_hex32("workdir_hr", (DWORD)hr);
    o_path("workdir", buf);
    wmemset(buf, L'#', 8); buf[8] = 0;
    hr = sl->lpVtbl->GetDescription(sl, buf, 4 * MAX_PATH);
    o_hex32("desc_hr", (DWORD)hr);
    o_wstr("desc", buf);
    wmemset(buf, L'#', 8); buf[8] = 0;
    i = -77;
    hr = sl->lpVtbl->GetIconLocation(sl, buf, 4 * MAX_PATH, &i);
    o_hex32("icon_hr", (DWORD)hr);
    o_path("icon", buf);
    o_i64("icon_index", i);
    i = -77;
    hr = sl->lpVtbl->GetShowCmd(sl, &i);
    o_hex32("show_hr", (DWORD)hr);
    o_i64("show", i);
    w = 0xbeef;
    hr = sl->lpVtbl->GetHotkey(sl, &w);
    o_hex32("hotkey_hr", (DWORD)hr);
    o_hex32("hotkey", w);
    PIDLIST_ABSOLUTE pidl = NULL;
    hr = sl->lpVtbl->GetIDList(sl, &pidl);
    o_hex32("idlist_hr", (DWORD)hr);
    o_bool("idlist_null", pidl == NULL);
    if (pidl) pCoTaskMemFree(pidl);
    o_hex32("flags", link_flags(sl));
}

static void make_file(const wchar_t *name, DWORD size)
{
    HANDLE h = CreateFileW(name, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    char b[64];
    memset(b, 'x', sizeof b);
    DWORD done;
    if (h == INVALID_HANDLE_VALUE) return;
    WriteFile(h, b, size, &done, NULL);
    CloseHandle(h);
}

int main(void)
{
    oracle_begin("shell32/shelllink_roundtrip");
    HMODULE ole = LoadLibraryW(L"ole32.dll");
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    LoadLibraryW(L"shell32.dll");
    LOAD(ole, CoInitializeEx);
    LOAD(ole, CoCreateInstance);
    LOAD(ole, CoTaskMemFree);
    LOAD(k32, LocalFree);
    HRESULT hr = pCoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    GetCurrentDirectoryW(MAX_PATH, cwd);
    GetWindowsDirectoryW(windir, MAX_PATH);
    GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH);
    if (!GetShortPathNameW(cwd, shortcwd, MAX_PATH) || !_wcsicmp(shortcwd, cwd)) shortcwd[0] = 0;

    IShellLinkW *sl = new_link();
    oracle_case("create");
    o_hex32("coinit_hr", (DWORD)hr);
    o_bool("created", sl != NULL);
    oracle_case_end();
    if (!sl) return oracle_end();

    /* ---- defaults of an empty link ---- */
    oracle_case("empty_defaults");
    o_link(sl, 0);
    {
        IPersistFile *pf = NULL;
        sl->lpVtbl->QueryInterface(sl, &IID_IPersistFile, (void **)&pf);
        o_bool("has_ipersistfile", pf != NULL);
        if (pf) {
            o_hex32("isdirty", (DWORD)pf->lpVtbl->IsDirty(pf));
            LPOLESTR cur = (LPOLESTR)(ULONG_PTR)1;
            HRESULT h2 = pf->lpVtbl->GetCurFile(pf, &cur);
            o_hex32("curfile_hr", (DWORD)h2);
            o_bool("curfile_null", cur == NULL);
            if (SUCCEEDED(h2) && cur && cur != (LPOLESTR)(ULONG_PTR)1) pCoTaskMemFree(cur);
            CLSID c;
            h2 = pf->lpVtbl->GetClassID(pf, &c);
            o_hex32("classid_hr", (DWORD)h2);
            o_bool("classid_is_shelllink", IsEqualGUID(&c, &CLSID_ShellLink));
            pf->lpVtbl->Release(pf);
        }
    }
    oracle_case_end();

    /* ---- save the empty link, load it back ---- */
    hr = save_link(sl, L"empty.lnk");
    oracle_case("empty_save");
    o_hex32("hr", (DWORD)hr);
    o_bool("file_exists", GetFileAttributesW(L"empty.lnk") != INVALID_FILE_ATTRIBUTES);
    oracle_case_end();
    sl->lpVtbl->Release(sl);
    {
        IShellLinkW *l2 = load_link(L"empty.lnk", &hr);
        oracle_case("empty_load");
        o_hex32("hr", (DWORD)hr);
        if (l2) o_link(l2, 0);
        oracle_case_end();
        if (l2) l2->lpVtbl->Release(l2);
    }

    /* ---- full round trip ---- */
    CreateDirectoryW(L"wd", NULL);
    make_file(L"target.exe", 13);
    wchar_t target[MAX_PATH], wd[MAX_PATH], icon[MAX_PATH], lnk[MAX_PATH];
    swprintf(target, MAX_PATH, L"%ls\\target.exe", cwd);
    swprintf(wd, MAX_PATH, L"%ls\\wd", cwd);
    swprintf(icon, MAX_PATH, L"%ls\\target.exe", cwd);
    swprintf(lnk, MAX_PATH, L"%ls\\full.lnk", cwd);
    sl = new_link();
    oracle_case("full_set");
    o_hex32("path_hr", (DWORD)sl->lpVtbl->SetPath(sl, target));
    o_hex32("args_hr", (DWORD)sl->lpVtbl->SetArguments(sl, L"-a \"b c\" %TEMP%"));
    o_hex32("workdir_hr", (DWORD)sl->lpVtbl->SetWorkingDirectory(sl, wd));
    o_hex32("icon_hr", (DWORD)sl->lpVtbl->SetIconLocation(sl, icon, 3));
    o_hex32("show_hr", (DWORD)sl->lpVtbl->SetShowCmd(sl, SW_SHOWMINNOACTIVE));
    o_hex32("hotkey_hr", (DWORD)sl->lpVtbl->SetHotkey(sl, MAKEWORD('K', HOTKEYF_CONTROL | HOTKEYF_ALT)));
    o_hex32("desc_hr", (DWORD)sl->lpVtbl->SetDescription(sl, L"A link \x00e9\x20ac"));
    {
        IPersistFile *pf = NULL;
        sl->lpVtbl->QueryInterface(sl, &IID_IPersistFile, (void **)&pf);
        if (pf) { o_hex32("isdirty", (DWORD)pf->lpVtbl->IsDirty(pf)); pf->lpVtbl->Release(pf); }
    }
    oracle_case_end();
    oracle_case("full_before_save");
    o_link(sl, 0);
    oracle_case_end();
    hr = save_link(sl, lnk);
    oracle_case("full_save");
    o_hex32("hr", (DWORD)hr);
    {
        IPersistFile *pf = NULL;
        sl->lpVtbl->QueryInterface(sl, &IID_IPersistFile, (void **)&pf);
        if (pf) {
            o_hex32("isdirty", (DWORD)pf->lpVtbl->IsDirty(pf));
            LPOLESTR cur = NULL;
            HRESULT h2 = pf->lpVtbl->GetCurFile(pf, &cur);
            o_hex32("curfile_hr", (DWORD)h2);
            o_path("curfile", cur);
            if (cur) pCoTaskMemFree(cur);
            pf->lpVtbl->Release(pf);
        }
    }
    oracle_case_end();
    sl->lpVtbl->Release(sl);
    {
        IShellLinkW *l2 = load_link(lnk, &hr);
        oracle_case("full_load");
        o_hex32("hr", (DWORD)hr);
        if (l2) o_link(l2, 0);
        oracle_case_end();
        if (l2) {
            oracle_case("full_load_rawpath");
            o_link(l2, SLGP_RAWPATH);
            oracle_case_end();
            oracle_case("full_load_shortpath");
            o_link(l2, SLGP_SHORTPATH);
            oracle_case_end();
            wchar_t small[8];
            wmemset(small, L'#', 8);
            HRESULT h2 = l2->lpVtbl->GetPath(l2, small, 5, NULL, 0);
            oracle_case("getpath_small_buffer");
            o_hex32("hr", (DWORD)h2);
            o_bool("terminated", wmemchr(small, 0, 5) != NULL);
            o_bool("untouched", small[0] == L'#');
            oracle_case_end();
            wmemset(small, L'#', 8);
            h2 = l2->lpVtbl->GetArguments(l2, small, 5);
            oracle_case("getargs_small_buffer");
            o_hex32("hr", (DWORD)h2);
            o_bool("terminated", wmemchr(small, 0, 5) != NULL);
            if (wmemchr(small, 0, 5)) o_wstr("args", small);
            oracle_case_end();
            l2->lpVtbl->Release(l2);
        }
    }
    /* Loading through a relative file name. */
    {
        IShellLinkW *l2 = load_link(L"full.lnk", &hr);
        oracle_case("load_relative_name");
        o_hex32("hr", (DWORD)hr);
        oracle_case_end();
        if (l2) l2->lpVtbl->Release(l2);
    }

    /* ---- SetPath variants ---- */
    static const struct { const char *name; const wchar_t *path; } paths[] = {
        {"setpath_env_string", L"%WINDIR%\\system32\\notepad.exe"},
        {"setpath_directory", L"wd"},
        {"setpath_missing_absolute", L"C:\\setu-no-such-dir\\nothing.exe"},
        {"setpath_missing_relative", L"nothing_here.exe"},
        {"setpath_relative_existing", L"target.exe"},
        {"setpath_empty", L""},
        {"setpath_quoted", L"\"C:\\setu-no-such-dir\\q.exe\""},
        {"setpath_forward_slashes", L"C:/setu-no-such-dir/f.exe"},
        {"setpath_trailing_backslash", L"C:\\setu-no-such-dir\\"},
        {"setpath_drive_root", L"C:\\"},
    };
    for (size_t i = 0; i < sizeof paths / sizeof paths[0]; i++) {
        sl = new_link();
        hr = sl->lpVtbl->SetPath(sl, paths[i].path);
        oracle_case(paths[i].name);
        o_hex32("set_hr", (DWORD)hr);
        o_link(sl, 0);
        oracle_case_end();
        char nm[96];
        snprintf(nm, sizeof nm, "%s_saved", paths[i].name);
        HRESULT h2 = save_link(sl, L"variant.lnk");
        sl->lpVtbl->Release(sl);
        IShellLinkW *l2 = load_link(L"variant.lnk", &hr);
        oracle_case(nm);
        o_hex32("save_hr", (DWORD)h2);
        o_hex32("load_hr", (DWORD)hr);
        if (l2) o_link(l2, SLGP_RAWPATH);
        oracle_case_end();
        if (l2) l2->lpVtbl->Release(l2);
        DeleteFileW(L"variant.lnk");
    }

    /* ---- other setters ---- */
    sl = new_link();
    oracle_case("set_misc");
    o_hex32("show_bogus_hr", (DWORD)sl->lpVtbl->SetShowCmd(sl, 77));
    o_hex32("hotkey_bogus_hr", (DWORD)sl->lpVtbl->SetHotkey(sl, 0xffff));
    o_hex32("icon_null_hr", (DWORD)sl->lpVtbl->SetIconLocation(sl, NULL, 0));
    o_hex32("args_null_hr", (DWORD)sl->lpVtbl->SetArguments(sl, NULL));
    o_hex32("desc_null_hr", (DWORD)sl->lpVtbl->SetDescription(sl, NULL));
    o_hex32("workdir_null_hr", (DWORD)sl->lpVtbl->SetWorkingDirectory(sl, NULL));
    o_hex32("relpath_hr", (DWORD)sl->lpVtbl->SetRelativePath(sl, L"..\\x\\y.lnk", 0));
    o_link(sl, 0);
    oracle_case_end();
    sl->lpVtbl->Release(sl);
    {
        /* 1000-character arguments survive a round trip. */
        static wchar_t big[1001];
        for (int i = 0; i < 1000; i++) big[i] = (wchar_t)(L'a' + i % 26);
        big[1000] = 0;
        sl = new_link();
        sl->lpVtbl->SetPath(sl, target);
        hr = sl->lpVtbl->SetArguments(sl, big);
        save_link(sl, L"big.lnk");
        sl->lpVtbl->Release(sl);
        IShellLinkW *l2 = load_link(L"big.lnk", &hr);
        static wchar_t back[2048];
        back[0] = 0;
        HRESULT h2 = l2 ? l2->lpVtbl->GetArguments(l2, back, 2048) : E_FAIL;
        oracle_case("long_arguments");
        o_hex32("load_hr", (DWORD)hr);
        o_hex32("get_hr", (DWORD)h2);
        o_u32("len", (DWORD)wcslen(back));
        o_bool("same", !wcscmp(back, big));
        oracle_case_end();
        if (l2) l2->lpVtbl->Release(l2);
    }

    /* ---- IShellLinkDataList ---- */
    sl = new_link();
    {
        IShellLinkDataList *dl = NULL;
        hr = sl->lpVtbl->QueryInterface(sl, &IID_IShellLinkDataList, (void **)&dl);
        oracle_case("datalist_qi");
        o_hex32("hr", (DWORD)hr);
        oracle_case_end();
        if (dl) {
            DWORD f = 0;
            dl->lpVtbl->GetFlags(dl, &f);
            oracle_case("datalist_flags_new");
            o_hex32("flags", f);
            oracle_case_end();
            hr = dl->lpVtbl->SetFlags(dl, SLDF_RUNAS_USER | SLDF_HAS_NAME);
            f = 0;
            dl->lpVtbl->GetFlags(dl, &f);
            oracle_case("datalist_set_runas");
            o_hex32("hr", (DWORD)hr);
            o_hex32("flags", f);
            oracle_case_end();
            sl->lpVtbl->SetPath(sl, target);
            sl->lpVtbl->SetDescription(sl, L"d");
            sl->lpVtbl->SetArguments(sl, L"x");
            sl->lpVtbl->SetWorkingDirectory(sl, wd);
            sl->lpVtbl->SetIconLocation(sl, icon, 0);
            f = 0;
            dl->lpVtbl->GetFlags(dl, &f);
            oracle_case("datalist_flags_after_setters");
            o_hex32("flags", f);
            oracle_case_end();
            void *blk = (void *)(ULONG_PTR)1;
            hr = dl->lpVtbl->CopyDataBlock(dl, EXP_SZ_LINK_SIG, &blk);
            oracle_case("datalist_copy_missing_block");
            o_hex32("hr", (DWORD)hr);
            o_bool("out_null", blk == NULL);
            oracle_case_end();
            if (SUCCEEDED(hr) && blk && blk != (void *)(ULONG_PTR)1) pLocalFree(blk);
            /* An icon environment block. */
            EXP_SZ_LINK exp;
            memset(&exp, 0, sizeof exp);
            exp.cbSize = sizeof exp;
            exp.dwSignature = EXP_SZ_ICON_SIG;
            strcpy(exp.szTarget, "%WINDIR%\\i.ico");
            wcscpy(exp.swzTarget, L"%WINDIR%\\i.ico");
            hr = dl->lpVtbl->AddDataBlock(dl, &exp);
            f = 0;
            dl->lpVtbl->GetFlags(dl, &f);
            oracle_case("datalist_add_icon_block");
            o_hex32("hr", (DWORD)hr);
            o_hex32("flags", f);
            oracle_case_end();
            blk = NULL;
            hr = dl->lpVtbl->CopyDataBlock(dl, EXP_SZ_ICON_SIG, &blk);
            oracle_case("datalist_copy_icon_block");
            o_hex32("hr", (DWORD)hr);
            if (SUCCEEDED(hr) && blk) {
                EXP_SZ_LINK *e = blk;
                o_u32("size", e->cbSize);
                o_hex32("sig", e->dwSignature);
                o_wstr("wtarget", e->swzTarget);
                pLocalFree(blk);
            }
            oracle_case_end();
            hr = save_link(sl, L"dl.lnk");
            dl->lpVtbl->Release(dl);
            sl->lpVtbl->Release(sl);
            IShellLinkW *l2 = load_link(L"dl.lnk", &hr);
            oracle_case("datalist_reloaded");
            o_hex32("load_hr", (DWORD)hr);
            if (l2) o_link(l2, 0);
            oracle_case_end();
            if (l2) {
                IShellLinkDataList *d2 = NULL;
                l2->lpVtbl->QueryInterface(l2, &IID_IShellLinkDataList, (void **)&d2);
                if (d2) {
                    hr = d2->lpVtbl->RemoveDataBlock(d2, EXP_SZ_ICON_SIG);
                    f = 0;
                    d2->lpVtbl->GetFlags(d2, &f);
                    oracle_case("datalist_remove_icon_block");
                    o_hex32("hr", (DWORD)hr);
                    o_hex32("flags", f);
                    oracle_case_end();
                    hr = d2->lpVtbl->RemoveDataBlock(d2, EXP_SZ_ICON_SIG);
                    oracle_case("datalist_remove_again");
                    o_hex32("hr", (DWORD)hr);
                    oracle_case_end();
                    d2->lpVtbl->Release(d2);
                }
                l2->lpVtbl->Release(l2);
            }
        } else {
            sl->lpVtbl->Release(sl);
        }
    }

    /* ---- Load errors ---- */
    {
        make_file(L"notalink.lnk", 40);
        static const struct { const char *name; const wchar_t *file; } bad[] = {
            {"load_missing_file", L"no_such.lnk"},
            {"load_not_a_link", L"notalink.lnk"},
            {"load_directory", L"wd"},
        };
        for (int i = 0; i < 3; i++) {
            IShellLinkW *l2 = load_link(bad[i].file, &hr);
            oracle_case(bad[i].name);
            o_hex32("hr", (DWORD)hr);
            oracle_case_end();
            if (l2) l2->lpVtbl->Release(l2);
        }
    }
    {
        HANDLE h = CreateFileW(L"zero.lnk", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        CloseHandle(h);
        IShellLinkW *l2 = load_link(L"zero.lnk", &hr);
        oracle_case("load_zero_length");
        o_hex32("hr", (DWORD)hr);
        oracle_case_end();
        if (l2) l2->lpVtbl->Release(l2);
    }
    return oracle_end();
}
