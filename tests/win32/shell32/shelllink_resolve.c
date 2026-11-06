/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: IShellLinkW::Resolve without UI. Links are saved to a target
 * in the working directory, then the target is left alone, deleted, turned
 * into a directory, or the link is pointed at a missing drive, and Resolve
 * is called with SLR_NO_UI plus SLR_NOSEARCH / SLR_NOTRACK / SLR_NOUPDATE /
 * SLR_UPDATE combinations. Prints the HRESULT, the path afterwards, whether
 * the link became dirty and whether the .lnk file was rewritten (by size
 * class only). Also IShellLinkW::Resolve on a link that was never loaded.
 *
 * SLR_NO_UI is always set, with a 1 ms timeout in the high word, so no
 * dialog can appear. Paths print with the working directory as "<cwd>".
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ishelllinkw-resolve
 *       https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/ne-shobjidl_core-slr_flags
 *       https://learn.microsoft.com/en-us/windows/win32/shell/links
 */
#define INITGUID
#include "oracle.h"
#include <shlobj.h>
#include <shobjidl.h>

static HRESULT(WINAPI *pCoInitializeEx)(void *, DWORD);
static HRESULT(WINAPI *pCoCreateInstance)(REFCLSID, IUnknown *, DWORD, REFIID, void **);

#define LOAD(m, f) (*(FARPROC *)&p##f = GetProcAddress(m, #f))

static wchar_t cwd[MAX_PATH];

static void o_path(const char *k, const wchar_t *s)
{
    wchar_t out[2 * MAX_PATH];
    size_t n = wcslen(cwd);
    if (!_wcsnicmp(s, cwd, n) && (s[n] == 0 || s[n] == L'\\')) swprintf(out, 2 * MAX_PATH, L"<cwd>%ls", s + n);
    else swprintf(out, 2 * MAX_PATH, L"%ls", s);
    o_wstr(k, out);
}

static IShellLinkW *new_link(void)
{
    IShellLinkW *sl = NULL;
    pCoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkW, (void **)&sl);
    return sl;
}

static HRESULT persist(IShellLinkW *sl, const wchar_t *file, int save)
{
    IPersistFile *pf = NULL;
    HRESULT hr = sl->lpVtbl->QueryInterface(sl, &IID_IPersistFile, (void **)&pf);
    if (FAILED(hr)) return hr;
    hr = save ? pf->lpVtbl->Save(pf, file, TRUE) : pf->lpVtbl->Load(pf, file, STGM_READ);
    pf->lpVtbl->Release(pf);
    return hr;
}

static HRESULT is_dirty(IShellLinkW *sl)
{
    IPersistFile *pf = NULL;
    HRESULT hr = sl->lpVtbl->QueryInterface(sl, &IID_IPersistFile, (void **)&pf);
    if (FAILED(hr)) return hr;
    hr = pf->lpVtbl->IsDirty(pf);
    pf->lpVtbl->Release(pf);
    return hr;
}

static void make_file(const wchar_t *name, DWORD size)
{
    HANDLE h = CreateFileW(name, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    char b[256];
    DWORD done;
    memset(b, 'y', sizeof b);
    if (h == INVALID_HANDLE_VALUE) return;
    WriteFile(h, b, size, &done, NULL);
    CloseHandle(h);
}

static void make_link(const wchar_t *lnk, const wchar_t *target)
{
    IShellLinkW *sl = new_link();
    sl->lpVtbl->SetPath(sl, target);
    sl->lpVtbl->SetArguments(sl, L"--resolve-test");
    persist(sl, lnk, 1);
    sl->lpVtbl->Release(sl);
}

static FILETIME write_time(const wchar_t *f)
{
    WIN32_FILE_ATTRIBUTE_DATA d;
    memset(&d, 0, sizeof d);
    GetFileAttributesExW(f, GetFileExInfoStandard, &d);
    return d.ftLastWriteTime;
}

static void resolve_case(const char *name, const wchar_t *lnk, DWORD flags)
{
    IShellLinkW *sl = new_link();
    HRESULT lhr = persist(sl, lnk, 0);
    FILETIME before = write_time(lnk);
    Sleep(20);
    HRESULT hr = sl->lpVtbl->Resolve(sl, NULL, flags | SLR_NO_UI | (1u << 16));
    wchar_t buf[MAX_PATH] = L"#";
    sl->lpVtbl->GetPath(sl, buf, MAX_PATH, NULL, SLGP_RAWPATH);
    wchar_t args[64] = L"#";
    sl->lpVtbl->GetArguments(sl, args, 64);
    oracle_case(name);
    o_hex32("load_hr", (DWORD)lhr);
    o_hex32("hr", (DWORD)hr);
    o_path("path", buf);
    o_wstr("args", args);
    o_hex32("dirty", (DWORD)is_dirty(sl));
    FILETIME after = write_time(lnk);
    o_bool("lnk_rewritten", CompareFileTime(&before, &after) != 0);
    oracle_case_end();
    sl->lpVtbl->Release(sl);
}

int main(void)
{
    oracle_begin("shell32/shelllink_resolve");
    HMODULE ole = LoadLibraryW(L"ole32.dll");
    LoadLibraryW(L"shell32.dll");
    LOAD(ole, CoInitializeEx);
    LOAD(ole, CoCreateInstance);
    pCoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    GetCurrentDirectoryW(MAX_PATH, cwd);
    IShellLinkW *probe = new_link();
    oracle_case("create");
    o_bool("created", probe != NULL);
    oracle_case_end();
    if (!probe) return oracle_end();

    /* A link that was never loaded or given a path. */
    {
        HRESULT hr = probe->lpVtbl->Resolve(probe, NULL, SLR_NO_UI | SLR_NOSEARCH | SLR_NOTRACK);
        oracle_case("resolve_empty_link");
        o_hex32("hr", (DWORD)hr);
        oracle_case_end();
        probe->lpVtbl->SetPath(probe, L"C:\\setu-no-such-dir\\x.exe");
        hr = probe->lpVtbl->Resolve(probe, NULL, SLR_NO_UI | SLR_NOSEARCH | SLR_NOTRACK);
        oracle_case("resolve_unsaved_missing");
        o_hex32("hr", (DWORD)hr);
        oracle_case_end();
        probe->lpVtbl->Release(probe);
    }

    wchar_t t1[MAX_PATH], t2[MAX_PATH], t3[MAX_PATH], t4[MAX_PATH];
    swprintf(t1, MAX_PATH, L"%ls\\setu_resolve_present.exe", cwd);
    swprintf(t2, MAX_PATH, L"%ls\\setu_resolve_gone.exe", cwd);
    swprintf(t3, MAX_PATH, L"%ls\\setu_resolve_dir_now", cwd);
    swprintf(t4, MAX_PATH, L"%ls\\sub\\setu_resolve_sub.exe", cwd);
    make_file(t1, 10);
    make_file(t2, 11);
    make_file(t3, 12);
    CreateDirectoryW(L"sub", NULL);
    make_file(t4, 14);
    make_link(L"present.lnk", t1);
    make_link(L"gone.lnk", t2);
    make_link(L"dirnow.lnk", t3);
    make_link(L"subgone.lnk", t4);
    make_link(L"drive.lnk", L"Q:\\setu\\missing_drive.exe");
    make_link(L"unc.lnk", L"\\\\setu-no-such-host-name\\share\\x.exe");

    resolve_case("present_nosearch_notrack", L"present.lnk", SLR_NOSEARCH | SLR_NOTRACK);
    resolve_case("present_update", L"present.lnk", SLR_UPDATE);
    resolve_case("present_noupdate", L"present.lnk", SLR_NOUPDATE);
    resolve_case("present_plain", L"present.lnk", 0);
    /* The target grows: SLR_UPDATE may write the new size back. */
    make_file(t1, 200);
    resolve_case("present_changed_noupdate", L"present.lnk", SLR_NOUPDATE | SLR_NOSEARCH | SLR_NOTRACK);
    resolve_case("present_changed_update", L"present.lnk", SLR_UPDATE | SLR_NOSEARCH | SLR_NOTRACK);
    resolve_case("present_changed_after_update", L"present.lnk", SLR_NOUPDATE | SLR_NOSEARCH | SLR_NOTRACK);

    DeleteFileW(t2);
    resolve_case("gone_nosearch_notrack", L"gone.lnk", SLR_NOSEARCH | SLR_NOTRACK);
    resolve_case("gone_nosearch_notrack_noupdate", L"gone.lnk", SLR_NOSEARCH | SLR_NOTRACK | SLR_NOUPDATE);
    resolve_case("gone_nolinkinfo", L"gone.lnk", SLR_NOSEARCH | SLR_NOTRACK | SLR_NOLINKINFO);
    resolve_case("gone_update", L"gone.lnk", SLR_NOSEARCH | SLR_NOTRACK | SLR_UPDATE);

    DeleteFileW(t3);
    CreateDirectoryW(t3, NULL);
    resolve_case("file_became_dir", L"dirnow.lnk", SLR_NOSEARCH | SLR_NOTRACK);

    DeleteFileW(t4);
    RemoveDirectoryW(L"sub");
    resolve_case("parent_dir_gone", L"subgone.lnk", SLR_NOSEARCH | SLR_NOTRACK);

    resolve_case("missing_drive", L"drive.lnk", SLR_NOSEARCH | SLR_NOTRACK);
    resolve_case("missing_unc_host", L"unc.lnk", SLR_NOSEARCH | SLR_NOTRACK);
    return oracle_end();
}
