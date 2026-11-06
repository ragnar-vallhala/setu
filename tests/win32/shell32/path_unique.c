/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: the shell32 path helpers file managers and installers use
 * to pick names and resolve paths. PathMakeUniqueName (short and long
 * templates, with and without existing files), PathYetAnotherMakeUniqueName,
 * PathCleanupSpec (invalid characters, spaces, long names, return flags),
 * PathIsExe, PathResolve (PRF_* flags, files found in the given dirs,
 * missing files), PathQualify, PathGetShortPath, PathIsSlowW and
 * GetFileNameFromBrowse is skipped (UI).
 *
 * Every path is relative to the working directory or printed as "<cwd>".
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/nf-shlobj_core-pathmakeuniquename
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/nf-shlobj_core-pathyetanothermakeuniquename
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/nf-shlobj_core-pathcleanupspec
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/nf-shlobj_core-pathisexe
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/nf-shlobj_core-pathresolve
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/nf-shlobj_core-pathqualify
 */
#include "oracle.h"
#include <shlobj.h>

static BOOL(WINAPI *pPathMakeUniqueName)(PWSTR, UINT, PCWSTR, PCWSTR, PCWSTR);
static BOOL(WINAPI *pPathYetAnotherMakeUniqueName)(PWSTR, PCWSTR, PCWSTR, PCWSTR);
static int(WINAPI *pPathCleanupSpec)(PCWSTR, PWSTR);
static BOOL(WINAPI *pPathIsExe)(PCWSTR);
static int(WINAPI *pPathResolve)(PWSTR, PZPCWSTR, UINT);
static void(WINAPI *pPathQualify)(PWSTR);

#define LOAD(m, f) (*(FARPROC *)&p##f = GetProcAddress(m, #f))
#define LOADO(m, f, o) (*(FARPROC *)&p##f = GetProcAddress(m, (LPCSTR)(o)))

static wchar_t cwd[MAX_PATH];

static void o_path(const char *k, const wchar_t *p)
{
    wchar_t out[2 * MAX_PATH];
    wchar_t win[MAX_PATH];
    size_t n = wcslen(cwd), w = GetWindowsDirectoryW(win, MAX_PATH);
    if (!_wcsnicmp(p, cwd, n)) swprintf(out, 2 * MAX_PATH, L"<cwd>%ls", p + n);
    else if (w && !_wcsnicmp(p, win, w)) swprintf(out, 2 * MAX_PATH, L"<windir>%ls", p + w);
    else wcscpy(out, p);
    o_wstr(k, out);
}

static void make_file(const wchar_t *name)
{
    HANDLE h = CreateFileW(name, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    CloseHandle(h);
}

static void unique(const char *name, const wchar_t *shortt, const wchar_t *longt, const wchar_t *dir)
{
    wchar_t buf[MAX_PATH];
    wcscpy(buf, L"#");
    BOOL r = pPathMakeUniqueName(buf, MAX_PATH, shortt, longt, dir);
    oracle_case(name);
    o_bool("ret", r);
    o_path("out", buf);
    oracle_case_end();
}

static void yet(const char *name, const wchar_t *path, const wchar_t *shortt, const wchar_t *longt)
{
    wchar_t buf[MAX_PATH];
    wcscpy(buf, L"#");
    BOOL r = pPathYetAnotherMakeUniqueName(buf, path, shortt, longt);
    oracle_case(name);
    o_bool("ret", r);
    o_path("out", buf);
    oracle_case_end();
}

static void cleanup_spec(const char *name, const wchar_t *dir, const wchar_t *spec)
{
    wchar_t buf[MAX_PATH];
    wcscpy(buf, spec);
    int r = pPathCleanupSpec(dir, buf);
    oracle_case(name);
    o_hex32("ret", (DWORD)r);
    o_wstr("out", buf);
    oracle_case_end();
}

static void resolve(const char *name, const wchar_t *in, const wchar_t *dir, UINT flags)
{
    wchar_t buf[MAX_PATH];
    const wchar_t *dirs[2] = {dir, NULL};
    wcscpy(buf, in);
    SetLastError(0xdeadbeef);
    int r = pPathResolve(buf, dir ? dirs : NULL, flags);
    DWORD e = GetLastError();
    oracle_case(name);
    o_bool("ret", r);
    if (e == 0xdeadbeef) o_str("last_error", "unchanged");
    else o_u32("last_error", e);
    o_path("out", buf);
    oracle_case_end();
}

int main(void)
{
    oracle_begin("shell32/path_unique");
    HMODULE sh = LoadLibraryW(L"shell32.dll");
    LOAD(sh, PathMakeUniqueName);
    LOAD(sh, PathYetAnotherMakeUniqueName);
    LOAD(sh, PathCleanupSpec);
    LOAD(sh, PathIsExe);
    LOAD(sh, PathResolve);
    LOAD(sh, PathQualify);
    /* Older shell32 exported some of these by ordinal only. */
    if (!pPathIsExe) LOADO(sh, PathIsExe, 43);
    if (!pPathResolve) LOADO(sh, PathResolve, 51);
    if (!pPathQualify) LOADO(sh, PathQualify, 49);
    oracle_case("setup");
    o_bool("loaded", pPathMakeUniqueName && pPathYetAnotherMakeUniqueName && pPathCleanupSpec && pPathIsExe &&
                         pPathResolve && pPathQualify);
    oracle_case_end();
    if (!pPathMakeUniqueName || !pPathYetAnotherMakeUniqueName || !pPathCleanupSpec || !pPathIsExe ||
        !pPathResolve || !pPathQualify)
        return oracle_end();

    GetCurrentDirectoryW(MAX_PATH, cwd);
    CreateDirectoryW(L"u", NULL);
    CreateDirectoryW(L"y", NULL);
    CreateDirectoryW(L"r", NULL);
    wchar_t udir[MAX_PATH], ydir[MAX_PATH], rdir[MAX_PATH];
    swprintf(udir, MAX_PATH, L"%ls\\u", cwd);
    swprintf(ydir, MAX_PATH, L"%ls\\y", cwd);
    swprintf(rdir, MAX_PATH, L"%ls\\r", cwd);

    /* PathMakeUniqueName */
    unique("unique_long_empty_dir", L"short.txt", L"New Text Document.txt", udir);
    unique("unique_short_only", L"short.txt", NULL, udir);
    unique("unique_long_noext", NULL, L"New Folder", udir);
    unique("unique_short_paren", L"name (1).txt", NULL, udir);
    make_file(L"u\\New Text Document (1).txt");
    make_file(L"u\\New Text Document (2).txt");
    unique("unique_long_with_existing", NULL, L"New Text Document.txt", udir);
    make_file(L"u\\short1.txt");
    unique("unique_short_with_existing", L"short.txt", NULL, udir);
    unique("unique_null_dir", NULL, L"New Text Document.txt", NULL);
    unique("unique_long_template_paren", NULL, L"Copy (1) of x.txt", udir);
    {
        wchar_t buf[4];
        wcscpy(buf, L"#");
        BOOL r = pPathMakeUniqueName(buf, 4, NULL, L"New Text Document.txt", udir);
        oracle_case("unique_small_buffer");
        o_bool("ret", r);
        o_wstr("out", buf);
        oracle_case_end();
    }

    /* PathYetAnotherMakeUniqueName */
    yet("yet_nothing_exists", ydir, NULL, L"Doc.txt");
    make_file(L"y\\Doc.txt");
    yet("yet_exists", ydir, NULL, L"Doc.txt");
    make_file(L"y\\Doc (2).txt");
    yet("yet_exists_2", ydir, NULL, L"Doc.txt");
    make_file(L"y\\Doc (1).txt");
    yet("yet_exists_paren_template", ydir, NULL, L"Doc (1).txt");
    yet("yet_short", ydir, L"Doc.txt", NULL);
    CreateDirectoryW(L"y\\Folder", NULL);
    yet("yet_folder", ydir, NULL, L"Folder");
    yet("yet_noext_missing", ydir, NULL, L"Brand new");

    /* PathCleanupSpec */
    cleanup_spec("cleanup_plain", NULL, L"report.txt");
    cleanup_spec("cleanup_invalid_chars", NULL, L"a<b>c:d\"e|f?g*h.txt");
    cleanup_spec("cleanup_slashes", NULL, L"a/b\\c.txt");
    cleanup_spec("cleanup_spaces", NULL, L"  spaced name .txt");
    cleanup_spec("cleanup_dir_given", udir, L"a:b.txt");
    cleanup_spec("cleanup_empty", NULL, L"");
    cleanup_spec("cleanup_only_invalid", NULL, L"???");
    cleanup_spec("cleanup_dots", NULL, L"..");
    cleanup_spec("cleanup_trailing_dot", NULL, L"name.");
    cleanup_spec("cleanup_control", NULL, L"a\tb.txt");
    {
        wchar_t lng[MAX_PATH];
        for (int i = 0; i < 250; i++) lng[i] = L'a' + i % 26;
        wcscpy(lng + 250, L".txt");
        cleanup_spec("cleanup_long_name", udir, lng);
    }

    /* PathIsExe */
    static const wchar_t *exes[] = {L"a.exe", L"a.EXE", L"a.com", L"a.bat", L"a.cmd", L"a.pif", L"a.lnk",
                                    L"a.scr", L"a.msi", L"a.txt", L"a", L"C:\\dir.exe\\file", L"a.exe.txt"};
    oracle_case("pathisexe");
    for (size_t i = 0; i < sizeof exes / sizeof exes[0]; i++) {
        char key[16];
        snprintf(key, sizeof key, "e%zu", i);
        o_bool(key, pPathIsExe(exes[i]));
    }
    oracle_case_end();

    /* PathResolve */
    make_file(L"r\\found.txt");
    make_file(L"r\\tool.exe");
    make_file(L"r\\script.bat");
    resolve("resolve_found_in_dir", L"found.txt", rdir, 0);
    resolve("resolve_missing_in_dir", L"missing.txt", rdir, 0);
    resolve("resolve_noext_tryexts", L"tool", rdir, PRF_TRYPROGRAMEXTENSIONS);
    resolve("resolve_noext_bat", L"script", rdir, PRF_TRYPROGRAMEXTENSIONS);
    resolve("resolve_noext_noflag", L"tool", rdir, 0);
    resolve("resolve_verify_missing", L"missing.txt", rdir, PRF_VERIFYEXISTS);
    resolve("resolve_verify_found", L"found.txt", rdir, PRF_VERIFYEXISTS);
    resolve("resolve_relative_first", L"r\\found.txt", NULL, PRF_VERIFYEXISTS | PRF_FIRSTDIRDEF);
    resolve("resolve_absolute", L"C:\\Windows", NULL, PRF_VERIFYEXISTS);
    resolve("resolve_cmd_on_path", L"cmd", NULL, PRF_TRYPROGRAMEXTENSIONS | PRF_VERIFYEXISTS);
    resolve("resolve_qualify_dot", L".\\r\\found.txt", NULL, 0);
    resolve("resolve_dotdot", L"r\\..\\r\\found.txt", NULL, PRF_VERIFYEXISTS);

    /* PathQualify */
    static const struct { const char *name; const wchar_t *p; } q[] = {
        {"qualify_relative", L"r\\found.txt"}, {"qualify_dots", L"r\\..\\u"}, {"qualify_rooted", L"\\Windows"},
        {"qualify_drive_relative", L"C:x"}, {"qualify_absolute", L"C:\\a\\.\\b\\..\\c"}, {"qualify_empty", L""},
    };
    for (size_t i = 0; i < sizeof q / sizeof q[0]; i++) {
        wchar_t buf[MAX_PATH];
        wcscpy(buf, q[i].p);
        pPathQualify(buf);
        oracle_case(q[i].name);
        /* \Windows and C:x depend on the current drive / directory: print relative to cwd. */
        o_path("out", buf);
        oracle_case_end();
    }
    return oracle_end();
}
