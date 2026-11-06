/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: the .lnk file format, checked against the public
 * [MS-SHLLINK] specification in both directions.
 *
 * Writing: links saved through IShellLinkW + IPersistFile are parsed here
 * per [MS-SHLLINK] 2.1-2.5: ShellLinkHeader (HeaderSize, LinkCLSID,
 * LinkFlags, FileAttributes, FileSize, IconIndex, ShowCommand, HotKey,
 * reserved fields), whether a LinkTargetIDList and LinkInfo are present
 * (LinkInfo flags and the local base path only: volume serial numbers and
 * labels are machine data), the StringData strings, and the signatures of
 * the ExtraData blocks. Time stamps are never printed.
 *
 * Reading: hand-built files that follow the spec (header only, StringData
 * only, ANSI strings, an EnvironmentVariableDataBlock, a truncated file,
 * a wrong HeaderSize or CLSID) are loaded with IPersistFile::Load and read
 * back with IShellLinkW.
 *
 * Paths print with the working directory as "<cwd>" and the Windows
 * directory as "<windir>".
 *
 * Docs: https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-shllink/16cb4ca1-9339-4d0c-a68d-bf1d6cc0f943
 *       https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-shllink/c3376b21-0931-45e4-b2fc-a48ac0e60d15
 *       https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-shllink/ae350202-3ba9-4790-9e9e-98935f4ee5af
 *       https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-shllink/17b69472-0f34-4bcf-b290-eccdb8de224b
 *       https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nn-shobjidl_core-ishelllinkw
 */
#define INITGUID
#include "oracle.h"
#include <shlobj.h>
#include <shobjidl.h>

static HRESULT(WINAPI *pCoInitializeEx)(void *, DWORD);
static HRESULT(WINAPI *pCoCreateInstance)(REFCLSID, IUnknown *, DWORD, REFIID, void **);

#define LOAD(m, f) (*(FARPROC *)&p##f = GetProcAddress(m, #f))

static wchar_t cwd[MAX_PATH];
static char cwda[MAX_PATH], windira[MAX_PATH];
static wchar_t windir[MAX_PATH];

static void o_wpath(const char *k, const wchar_t *s)
{
    wchar_t out[2 * MAX_PATH];
    size_t n = wcslen(cwd), w = wcslen(windir);
    if (!_wcsnicmp(s, cwd, n) && (s[n] == 0 || s[n] == L'\\')) swprintf(out, 2 * MAX_PATH, L"<cwd>%ls", s + n);
    else if (!_wcsnicmp(s, windir, w) && s[w] == L'\\') swprintf(out, 2 * MAX_PATH, L"<windir>%ls", s + w);
    else swprintf(out, 2 * MAX_PATH, L"%ls", s);
    o_wstr(k, out);
}

static void o_apath(const char *k, const char *s)
{
    char out[2 * MAX_PATH];
    size_t n = strlen(cwda), w = strlen(windira);
    if (!_strnicmp(s, cwda, n) && (s[n] == 0 || s[n] == '\\')) snprintf(out, sizeof out, "<cwd>%s", s + n);
    else if (!_strnicmp(s, windira, w) && s[w] == '\\') snprintf(out, sizeof out, "<windir>%s", s + w);
    else snprintf(out, sizeof out, "%s", s);
    o_str(k, out);
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

static BYTE filebuf[65536];

static DWORD read_file(const wchar_t *name)
{
    HANDLE h = CreateFileW(name, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    DWORD n = 0;
    if (h == INVALID_HANDLE_VALUE) return 0;
    ReadFile(h, filebuf, sizeof filebuf, &n, NULL);
    CloseHandle(h);
    return n;
}

static void write_file(const wchar_t *name, const void *data, DWORD n)
{
    HANDLE h = CreateFileW(name, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    DWORD done;
    if (h == INVALID_HANDLE_VALUE) return;
    WriteFile(h, data, n, &done, NULL);
    CloseHandle(h);
}

static DWORD rd32(const BYTE *p) { return p[0] | p[1] << 8 | p[2] << 16 | (DWORD)p[3] << 24; }
static WORD rd16(const BYTE *p) { return (WORD)(p[0] | p[1] << 8); }

/* [MS-SHLLINK] LinkFlags bits. */
#define HasLinkTargetIDList 0x1
#define HasLinkInfo 0x2
#define HasName 0x4
#define HasRelativePath 0x8
#define HasWorkingDir 0x10
#define HasArguments 0x20
#define HasIconLocation 0x40
#define IsUnicode 0x80

/* Parse a saved link and print its structure. */
static void o_parse(const wchar_t *file)
{
    DWORD n = read_file(file), off;
    o_u32("file_size_ge_header", n >= 0x4c);
    if (n < 0x4c) return;
    const BYTE *p = filebuf;
    static const BYTE clsid[16] = {0x01, 0x14, 0x02, 0, 0, 0, 0, 0, 0xc0, 0, 0, 0, 0, 0, 0, 0x46};
    DWORD flags = rd32(p + 0x14);
    o_hex32("header_size", rd32(p));
    o_bool("clsid_ok", !memcmp(p + 4, clsid, 16));
    o_hex32("link_flags", flags);
    o_hex32("file_attributes", rd32(p + 0x18));
    o_bool("creation_time_zero", !rd32(p + 0x1c) && !rd32(p + 0x20));
    o_u32("file_size", rd32(p + 0x34));
    o_i64("icon_index", (int)rd32(p + 0x38));
    o_u32("show_command", rd32(p + 0x3c));
    o_hex32("hotkey", rd16(p + 0x40));
    o_bytes("reserved", p + 0x42, 10);
    off = 0x4c;
    if (flags & HasLinkTargetIDList) {
        if (off + 2 > n) { o_bool("truncated", 1); return; }
        WORD idsize = rd16(p + off);
        o_bool("idlist_nonempty", idsize > 2);
        off += 2 + idsize;
    }
    if (flags & HasLinkInfo) {
        if (off + 0x1c > n) { o_bool("truncated", 1); return; }
        DWORD size = rd32(p + off), hsize = rd32(p + off + 4), liflags = rd32(p + off + 8);
        o_hex32("linkinfo_header_size", hsize);
        o_hex32("linkinfo_flags", liflags);
        if (liflags & 1) {
            DWORD vol = rd32(p + off + 0x0c), base = rd32(p + off + 0x10);
            if (off + vol + 8 <= n) o_u32("linkinfo_drive_type", rd32(p + off + vol + 4));
            if (off + base < n) o_apath("linkinfo_local_base_path", (const char *)p + off + base);
            if (hsize >= 0x24 && off + rd32(p + off + 0x1c) + 2 <= n)
                o_wpath("linkinfo_local_base_path_unicode", (const wchar_t *)(p + off + rd32(p + off + 0x1c)));
        }
        off += size;
    }
    static const struct { DWORD bit; const char *name; } strs[] = {
        {HasName, "sd_name"}, {HasRelativePath, "sd_relative_path"}, {HasWorkingDir, "sd_working_dir"},
        {HasArguments, "sd_arguments"}, {HasIconLocation, "sd_icon_location"},
    };
    for (int i = 0; i < 5; i++) {
        if (!(flags & strs[i].bit)) continue;
        if (off + 2 > n) { o_bool("truncated", 1); return; }
        WORD count = rd16(p + off);
        off += 2;
        if (flags & IsUnicode) {
            wchar_t s[1024];
            WORD c = count < 1023 ? count : 1023;
            memcpy(s, p + off, c * 2);
            s[c] = 0;
            o_wpath(strs[i].name, s);
            off += count * 2;
        } else {
            char s[1024];
            WORD c = count < 1023 ? count : 1023;
            memcpy(s, p + off, c);
            s[c] = 0;
            o_apath(strs[i].name, s);
            off += count;
        }
    }
    /* ExtraData: a list of blocks ending with a terminal block (size < 4). */
    char sigs[512] = "";
    int nb = 0;
    while (off + 4 <= n && nb < 32) {
        DWORD size = rd32(p + off);
        if (size < 4) break;
        if (off + 8 > n) break;
        char one[16];
        snprintf(one, sizeof one, "%s%08lx", nb ? "," : "", (unsigned long)rd32(p + off + 4));
        strcat(sigs, one);
        nb++;
        off += size;
    }
    o_str("extra_signatures", sigs);
    o_bool("ends_with_terminal_block", off + 4 == n && rd32(p + off) == 0);
}

static void o_readback(IShellLinkW *sl)
{
    wchar_t buf[1024];
    int i = -9;
    WORD w = 0xbeef;
    buf[0] = L'#'; buf[1] = 0;
    o_hex32("path_hr", (DWORD)sl->lpVtbl->GetPath(sl, buf, 1024, NULL, SLGP_RAWPATH));
    o_wpath("path", buf);
    buf[0] = L'#'; buf[1] = 0;
    sl->lpVtbl->GetArguments(sl, buf, 1024);
    o_wstr("args", buf);
    buf[0] = L'#'; buf[1] = 0;
    sl->lpVtbl->GetWorkingDirectory(sl, buf, 1024);
    o_wpath("workdir", buf);
    buf[0] = L'#'; buf[1] = 0;
    sl->lpVtbl->GetDescription(sl, buf, 1024);
    o_wstr("desc", buf);
    buf[0] = L'#'; buf[1] = 0;
    sl->lpVtbl->GetIconLocation(sl, buf, 1024, &i);
    o_wpath("icon", buf);
    o_i64("icon_index", i);
    i = -9;
    sl->lpVtbl->GetShowCmd(sl, &i);
    o_i64("show", i);
    sl->lpVtbl->GetHotkey(sl, &w);
    o_hex32("hotkey", w);
}

/* Build a link file by hand. */
static DWORD put_header(BYTE *b, DWORD flags, DWORD attrs, DWORD size, int icon, DWORD show, WORD hotkey)
{
    static const BYTE clsid[16] = {0x01, 0x14, 0x02, 0, 0, 0, 0, 0, 0xc0, 0, 0, 0, 0, 0, 0, 0x46};
    memset(b, 0, 0x4c);
    b[0] = 0x4c;
    memcpy(b + 4, clsid, 16);
    memcpy(b + 0x14, &flags, 4);
    memcpy(b + 0x18, &attrs, 4);
    memcpy(b + 0x34, &size, 4);
    memcpy(b + 0x38, &icon, 4);
    memcpy(b + 0x3c, &show, 4);
    memcpy(b + 0x40, &hotkey, 2);
    return 0x4c;
}

static DWORD put_wstr(BYTE *b, const wchar_t *s)
{
    WORD n = (WORD)wcslen(s);
    memcpy(b, &n, 2);
    memcpy(b + 2, s, n * 2);
    return 2 + n * 2;
}

static DWORD put_astr(BYTE *b, const char *s)
{
    WORD n = (WORD)strlen(s);
    memcpy(b, &n, 2);
    memcpy(b + 2, s, n);
    return 2 + n;
}

static void load_case(const char *name, const BYTE *data, DWORD n)
{
    write_file(L"hand.lnk", data, n);
    IShellLinkW *sl = new_link();
    HRESULT hr = persist(sl, L"hand.lnk", 0);
    oracle_case(name);
    o_hex32("load_hr", (DWORD)hr);
    if (SUCCEEDED(hr)) o_readback(sl);
    oracle_case_end();
    sl->lpVtbl->Release(sl);
}

int main(void)
{
    oracle_begin("shell32/shelllink_format");
    HMODULE ole = LoadLibraryW(L"ole32.dll");
    LoadLibraryW(L"shell32.dll");
    LOAD(ole, CoInitializeEx);
    LOAD(ole, CoCreateInstance);
    pCoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    GetCurrentDirectoryW(MAX_PATH, cwd);
    GetCurrentDirectoryA(MAX_PATH, cwda);
    GetWindowsDirectoryW(windir, MAX_PATH);
    GetWindowsDirectoryA(windira, MAX_PATH);

    IShellLinkW *sl = new_link();
    oracle_case("create");
    o_bool("created", sl != NULL);
    oracle_case_end();
    if (!sl) return oracle_end();

    /* ---- files written by IPersistFile::Save ---- */
    persist(sl, L"w_empty.lnk", 1);
    oracle_case("write_empty");
    o_parse(L"w_empty.lnk");
    oracle_case_end();
    sl->lpVtbl->Release(sl);

    write_file(L"target.txt", "0123456789abcdefghij", 20);
    CreateDirectoryW(L"wd", NULL);
    wchar_t target[MAX_PATH], wd[MAX_PATH];
    swprintf(target, MAX_PATH, L"%ls\\target.txt", cwd);
    swprintf(wd, MAX_PATH, L"%ls\\wd", cwd);

    sl = new_link();
    sl->lpVtbl->SetPath(sl, target);
    persist(sl, L"w_path.lnk", 1);
    oracle_case("write_path_only");
    o_parse(L"w_path.lnk");
    oracle_case_end();
    sl->lpVtbl->SetArguments(sl, L"/x \"y z\"");
    sl->lpVtbl->SetWorkingDirectory(sl, wd);
    sl->lpVtbl->SetDescription(sl, L"Desc \x00e9");
    sl->lpVtbl->SetIconLocation(sl, L"C:\\setu-icons\\app.ico", -5);
    sl->lpVtbl->SetShowCmd(sl, SW_SHOWMAXIMIZED);
    sl->lpVtbl->SetHotkey(sl, MAKEWORD(VK_F5, HOTKEYF_SHIFT));
    persist(sl, L"w_full.lnk", 1);
    oracle_case("write_full");
    o_parse(L"w_full.lnk");
    oracle_case_end();
    sl->lpVtbl->Release(sl);

    sl = new_link();
    sl->lpVtbl->SetPath(sl, L"C:\\setu-no-such-dir\\missing.exe");
    persist(sl, L"w_missing.lnk", 1);
    oracle_case("write_missing_target");
    o_parse(L"w_missing.lnk");
    oracle_case_end();
    sl->lpVtbl->Release(sl);

    sl = new_link();
    sl->lpVtbl->SetPath(sl, wd);
    persist(sl, L"w_dir.lnk", 1);
    oracle_case("write_directory_target");
    o_parse(L"w_dir.lnk");
    oracle_case_end();
    sl->lpVtbl->Release(sl);

    sl = new_link();
    sl->lpVtbl->SetPath(sl, L"%SystemRoot%\\system32\\cmd.exe");
    persist(sl, L"w_env.lnk", 1);
    oracle_case("write_env_target");
    o_parse(L"w_env.lnk");
    oracle_case_end();
    sl->lpVtbl->Release(sl);

    sl = new_link();
    sl->lpVtbl->SetPath(sl, target);
    sl->lpVtbl->SetRelativePath(sl, L"w_rel.lnk", 0);
    persist(sl, L"w_rel.lnk", 1);
    oracle_case("write_relative_path");
    o_parse(L"w_rel.lnk");
    oracle_case_end();
    sl->lpVtbl->Release(sl);

    /* ---- files built by hand from the spec ---- */
    BYTE b[4096];
    DWORD n;

    n = put_header(b, 0, 0, 0, 0, SW_SHOWNORMAL, 0);
    memset(b + n, 0, 4); n += 4;
    load_case("read_header_only", b, n);

    n = put_header(b, IsUnicode | HasName | HasRelativePath | HasWorkingDir | HasArguments | HasIconLocation,
                   FILE_ATTRIBUTE_ARCHIVE, 1234, 2, SW_SHOWMINNOACTIVE, MAKEWORD('Q', HOTKEYF_CONTROL));
    n += put_wstr(b + n, L"hand made");
    n += put_wstr(b + n, L".\\target.txt");
    n += put_wstr(b + n, L"C:\\setu-wd");
    n += put_wstr(b + n, L"-flag value");
    n += put_wstr(b + n, L"C:\\setu-icons\\i.ico");
    memset(b + n, 0, 4); n += 4;
    load_case("read_unicode_strings", b, n);

    n = put_header(b, HasName | HasRelativePath | HasArguments, 0, 0, 0, SW_SHOWMAXIMIZED, 0);
    n += put_astr(b + n, "ansi name");
    n += put_astr(b + n, ".\\target.txt");
    n += put_astr(b + n, "ansi args");
    memset(b + n, 0, 4); n += 4;
    load_case("read_ansi_strings", b, n);

    /* EnvironmentVariableDataBlock (0xA0000001), 0x314 bytes. */
    n = put_header(b, IsUnicode | 0x200 /* HasExpString */, 0, 0, 0, SW_SHOWNORMAL, 0);
    {
        DWORD size = 0x314, sig = 0xA0000001;
        memset(b + n, 0, size);
        memcpy(b + n, &size, 4);
        memcpy(b + n + 4, &sig, 4);
        strcpy((char *)b + n + 8, "%SystemRoot%\\system32\\cmd.exe");
        wcscpy((wchar_t *)(b + n + 8 + 260), L"%SystemRoot%\\system32\\cmd.exe");
        n += size;
    }
    memset(b + n, 0, 4); n += 4;
    load_case("read_env_block", b, n);

    /* No terminal block at all. */
    n = put_header(b, IsUnicode | HasArguments, 0, 0, 0, SW_SHOWNORMAL, 0);
    n += put_wstr(b + n, L"no terminal");
    load_case("read_no_terminal_block", b, n);

    /* Truncated in the middle of StringData. */
    n = put_header(b, IsUnicode | HasArguments, 0, 0, 0, SW_SHOWNORMAL, 0);
    n += put_wstr(b + n, L"truncated arguments");
    load_case("read_truncated_string", b, n - 10);

    /* Header only, cut short. */
    n = put_header(b, 0, 0, 0, 0, SW_SHOWNORMAL, 0);
    load_case("read_short_header", b, 0x30);

    /* Wrong HeaderSize. */
    n = put_header(b, 0, 0, 0, 0, SW_SHOWNORMAL, 0);
    b[0] = 0x50;
    memset(b + n, 0, 4); n += 4;
    load_case("read_bad_header_size", b, n);

    /* Wrong CLSID. */
    n = put_header(b, 0, 0, 0, 0, SW_SHOWNORMAL, 0);
    b[4] ^= 0xff;
    memset(b + n, 0, 4); n += 4;
    load_case("read_bad_clsid", b, n);

    /* ShowCommand values other than 1, 3 and 7 must be read as SW_SHOWNORMAL. */
    n = put_header(b, 0, 0, 0, 0, SW_HIDE, 0);
    memset(b + n, 0, 4); n += 4;
    load_case("read_show_hide", b, n);
    n = put_header(b, 0, 0, 0, 0, 99, 0);
    memset(b + n, 0, 4); n += 4;
    load_case("read_show_bogus", b, n);
    return oracle_end();
}
