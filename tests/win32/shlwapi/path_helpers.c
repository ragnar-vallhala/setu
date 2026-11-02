/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: shlwapi's pure path helpers (no file-system access):
 * PathCombineW, PathCanonicalizeW, PathAppendW, PathFindExtensionW,
 * PathFindFileNameW, PathFindNextComponentW, PathRemoveExtensionW,
 * PathRenameExtensionW, PathAddExtensionW, PathAddBackslashW,
 * PathRemoveBackslashW, PathRemoveFileSpecW, PathStripPathW,
 * PathStripToRootW, PathSkipRootW, PathIsRelativeW, PathIsRootW, PathIsUNCW,
 * PathIsUNCServerW, PathIsUNCServerShareW, PathGetDriveNumberW,
 * PathBuildRootW, PathIsURLW, PathMatchSpecW, PathCommonPrefixW,
 * PathRelativePathToW, PathQuoteSpacesW, PathUnquoteSpacesW,
 * PathRemoveBlanksW, PathGetArgsW, PathRemoveArgsW, PathIsSameRootW,
 * PathParseIconLocationW, PathIsPrefixW, PathCompactPathExW and
 * StrCmpLogicalW.
 *
 * Returned pointers into the input are printed as offsets.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/nf-shlwapi-pathcombinew
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/nf-shlwapi-pathcanonicalizew
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/nf-shlwapi-pathappendw
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/nf-shlwapi-pathfindextensionw
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/nf-shlwapi-pathfindfilenamew
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/nf-shlwapi-pathfindnextcomponentw
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/nf-shlwapi-pathremoveextensionw
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/nf-shlwapi-pathrenameextensionw
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/nf-shlwapi-pathaddextensionw
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/nf-shlwapi-pathaddbackslashw
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/nf-shlwapi-pathremovebackslashw
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/nf-shlwapi-pathremovefilespecw
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/nf-shlwapi-pathstrippathw
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/nf-shlwapi-pathstriptorootw
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/nf-shlwapi-pathskiprootw
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/nf-shlwapi-pathisrelativew
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/nf-shlwapi-pathisrootw
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/nf-shlwapi-pathisuncw
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/nf-shlwapi-pathisuncserverw
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/nf-shlwapi-pathisuncserversharew
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/nf-shlwapi-pathgetdrivenumberw
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/nf-shlwapi-pathbuildrootw
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/nf-shlwapi-pathisurlw
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/nf-shlwapi-pathmatchspecw
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/nf-shlwapi-pathcommonprefixw
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/nf-shlwapi-pathrelativepathtow
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/nf-shlwapi-pathquotespacesw
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/nf-shlwapi-pathunquotespacesw
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/nf-shlwapi-pathremoveblanksw
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/nf-shlwapi-pathgetargsw
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/nf-shlwapi-pathremoveargsw
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/nf-shlwapi-pathissamerootw
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/nf-shlwapi-pathparseiconlocationw
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/nf-shlwapi-pathisprefixw
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/nf-shlwapi-pathcompactpathexw
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/nf-shlwapi-strcmplogicalw
 */
#include "oracle.h"

static LPWSTR(WINAPI *pPathCombineW)(LPWSTR, LPCWSTR, LPCWSTR);
static BOOL(WINAPI *pPathCanonicalizeW)(LPWSTR, LPCWSTR);
static BOOL(WINAPI *pPathAppendW)(LPWSTR, LPCWSTR);
static LPWSTR(WINAPI *pPathFindExtensionW)(LPCWSTR);
static LPWSTR(WINAPI *pPathFindFileNameW)(LPCWSTR);
static LPWSTR(WINAPI *pPathFindNextComponentW)(LPCWSTR);
static void(WINAPI *pPathRemoveExtensionW)(LPWSTR);
static BOOL(WINAPI *pPathRenameExtensionW)(LPWSTR, LPCWSTR);
static BOOL(WINAPI *pPathAddExtensionW)(LPWSTR, LPCWSTR);
static LPWSTR(WINAPI *pPathAddBackslashW)(LPWSTR);
static LPWSTR(WINAPI *pPathRemoveBackslashW)(LPWSTR);
static BOOL(WINAPI *pPathRemoveFileSpecW)(LPWSTR);
static void(WINAPI *pPathStripPathW)(LPWSTR);
static BOOL(WINAPI *pPathStripToRootW)(LPWSTR);
static LPWSTR(WINAPI *pPathSkipRootW)(LPCWSTR);
static BOOL(WINAPI *pPathIsRelativeW)(LPCWSTR);
static BOOL(WINAPI *pPathIsRootW)(LPCWSTR);
static BOOL(WINAPI *pPathIsUNCW)(LPCWSTR);
static BOOL(WINAPI *pPathIsUNCServerW)(LPCWSTR);
static BOOL(WINAPI *pPathIsUNCServerShareW)(LPCWSTR);
static int(WINAPI *pPathGetDriveNumberW)(LPCWSTR);
static LPWSTR(WINAPI *pPathBuildRootW)(LPWSTR, int);
static BOOL(WINAPI *pPathIsURLW)(LPCWSTR);
static BOOL(WINAPI *pPathMatchSpecW)(LPCWSTR, LPCWSTR);
static int(WINAPI *pPathCommonPrefixW)(LPCWSTR, LPCWSTR, LPWSTR);
static BOOL(WINAPI *pPathRelativePathToW)(LPWSTR, LPCWSTR, DWORD, LPCWSTR, DWORD);
static BOOL(WINAPI *pPathQuoteSpacesW)(LPWSTR);
static void(WINAPI *pPathUnquoteSpacesW)(LPWSTR);
static void(WINAPI *pPathRemoveBlanksW)(LPWSTR);
static LPWSTR(WINAPI *pPathGetArgsW)(LPCWSTR);
static void(WINAPI *pPathRemoveArgsW)(LPWSTR);
static BOOL(WINAPI *pPathIsSameRootW)(LPCWSTR, LPCWSTR);
static int(WINAPI *pPathParseIconLocationW)(LPWSTR);
static BOOL(WINAPI *pPathIsPrefixW)(LPCWSTR, LPCWSTR);
static BOOL(WINAPI *pPathCompactPathExW)(LPWSTR, LPCWSTR, UINT, DWORD);
static int(WINAPI *pStrCmpLogicalW)(LPCWSTR, LPCWSTR);

#define LOAD(m, f) (*(FARPROC *)&p##f = GetProcAddress(m, #f))
#define B 600 /* buffers are larger than MAX_PATH so overruns are visible */

static void off_field(const char *k, const wchar_t *base, const wchar_t *p)
{
    if (!p) o_str(k, "null");
    else o_i64(k, p - base);
}

static void combine(const char *name, const wchar_t *d, const wchar_t *f)
{
    wchar_t buf[B];
    LPWSTR r;
    wmemset(buf, L'#', B);
    buf[B - 1] = 0;
    ORACLE_CALL(r, pPathCombineW(buf, d, f));
    oracle_case(name);
    o_str("ret", r == buf ? "buf" : r ? "other" : "null");
    o_lasterr();
    buf[B - 1] = 0;
    if (wcslen(buf) < 300) o_wstr("out", buf);
    else o_u64("out_len", wcslen(buf));
    oracle_case_end();
}

static void canon(const char *name, const wchar_t *in)
{
    wchar_t buf[B];
    BOOL r;
    wmemset(buf, L'#', B);
    buf[B - 1] = 0;
    ORACLE_CALL(r, pPathCanonicalizeW(buf, in));
    oracle_case(name);
    o_bool("ret", r);
    o_lasterr();
    if (wcslen(buf) < 300) o_wstr("out", buf);
    else o_u64("out_len", wcslen(buf));
    oracle_case_end();
}

static void append(const char *name, const wchar_t *a, const wchar_t *b)
{
    wchar_t buf[B];
    BOOL r;
    wcscpy(buf, a);
    ORACLE_CALL(r, pPathAppendW(buf, b));
    oracle_case(name);
    o_bool("ret", r);
    o_lasterr();
    o_wstr("out", buf);
    oracle_case_end();
}

/* Pointer-returning finders: extension, file name, next component, skip root, args. */
static void finders(const char *name, const wchar_t *p)
{
    oracle_case(name);
    off_field("ext", p, pPathFindExtensionW(p));
    off_field("filename", p, pPathFindFileNameW(p));
    off_field("next_component", p, pPathFindNextComponentW(p));
    off_field("skip_root", p, pPathSkipRootW(p));
    off_field("args", p, pPathGetArgsW(p));
    o_bool("is_relative", pPathIsRelativeW(p));
    o_bool("is_root", pPathIsRootW(p));
    o_bool("is_unc", pPathIsUNCW(p));
    o_bool("is_unc_server", pPathIsUNCServerW(p));
    o_bool("is_unc_server_share", pPathIsUNCServerShareW(p));
    o_i64("drive_number", pPathGetDriveNumberW(p));
    o_bool("is_url", pPathIsURLW(p));
    oracle_case_end();
}

/* In-place modifiers, each on a fresh copy. */
static void modifiers(const char *name, const wchar_t *p)
{
    wchar_t b[B];
    oracle_case(name);
    wcscpy(b, p); pPathRemoveExtensionW(b); o_wstr("remove_extension", b);
    wcscpy(b, p); o_bool("rename_ext_ret", pPathRenameExtensionW(b, L".new")); o_wstr("rename_ext", b);
    wcscpy(b, p); o_bool("add_ext_ret", pPathAddExtensionW(b, NULL)); o_wstr("add_ext_default", b);
    wcscpy(b, p); { LPWSTR r = pPathAddBackslashW(b); off_field("add_backslash_ret", b, r); } o_wstr("add_backslash", b);
    wcscpy(b, p); { LPWSTR r = pPathRemoveBackslashW(b); off_field("remove_backslash_ret", b, r); } o_wstr("remove_backslash", b);
    wcscpy(b, p); o_bool("remove_filespec_ret", pPathRemoveFileSpecW(b)); o_wstr("remove_filespec", b);
    wcscpy(b, p); pPathStripPathW(b); o_wstr("strip_path", b);
    wcscpy(b, p); o_bool("strip_to_root_ret", pPathStripToRootW(b)); o_wstr("strip_to_root", b);
    wcscpy(b, p); pPathRemoveArgsW(b); o_wstr("remove_args", b);
    wcscpy(b, p); pPathRemoveBlanksW(b); o_wstr("remove_blanks", b);
    wcscpy(b, p); o_bool("quote_spaces_ret", pPathQuoteSpacesW(b)); o_wstr("quote_spaces", b);
    wcscpy(b, p); pPathUnquoteSpacesW(b); o_wstr("unquote_spaces", b);
    oracle_case_end();
}

static void match(const char *name, const wchar_t *f, const wchar_t *spec)
{
    oracle_case(name);
    o_bool("ret", pPathMatchSpecW(f, spec));
    oracle_case_end();
}

static void relpath(const char *name, const wchar_t *from, DWORD fa, const wchar_t *to, DWORD ta)
{
    wchar_t buf[B];
    BOOL r;
    wmemset(buf, L'#', B);
    buf[B - 1] = 0;
    ORACLE_CALL(r, pPathRelativePathToW(buf, from, fa, to, ta));
    oracle_case(name);
    o_bool("ret", r);
    o_lasterr();
    buf[40] = 0;
    o_wstr("out", buf);
    oracle_case_end();
}

static void prefix(const char *name, const wchar_t *a, const wchar_t *b)
{
    wchar_t buf[B];
    wmemset(buf, L'#', B);
    buf[B - 1] = 0;
    int r = pPathCommonPrefixW(a, b, buf);
    oracle_case(name);
    o_i64("ret", r);
    buf[40] = 0;
    o_wstr("out", buf);
    o_bool("is_prefix_a_of_b", pPathIsPrefixW(a, b));
    o_bool("same_root", pPathIsSameRootW(a, b));
    oracle_case_end();
}

static void icon(const char *name, const wchar_t *p)
{
    wchar_t b[B];
    wcscpy(b, p);
    int r = pPathParseIconLocationW(b);
    oracle_case(name);
    o_i64("index", r);
    o_wstr("path", b);
    oracle_case_end();
}

static void compact(const char *name, const wchar_t *p, UINT n)
{
    wchar_t b[B];
    BOOL r;
    wmemset(b, L'#', B);
    b[B - 1] = 0;
    ORACLE_CALL(r, pPathCompactPathExW(b, p, n, 0));
    oracle_case(name);
    o_bool("ret", r);
    b[60] = 0;
    o_wstr("out", b);
    oracle_case_end();
}

static int sgn(int v) { return v < 0 ? -1 : v > 0; }

int main(void)
{
    oracle_begin("shlwapi/path_helpers");
    HMODULE m;
    ORACLE_CALL(m, LoadLibraryW(L"shlwapi.dll"));
    oracle_case("load_shlwapi");
    o_handle("ret", m);
    oracle_case_end();
    if (!m) return oracle_end();
    LOAD(m, PathCombineW); LOAD(m, PathCanonicalizeW); LOAD(m, PathAppendW);
    LOAD(m, PathFindExtensionW); LOAD(m, PathFindFileNameW); LOAD(m, PathFindNextComponentW);
    LOAD(m, PathRemoveExtensionW); LOAD(m, PathRenameExtensionW); LOAD(m, PathAddExtensionW);
    LOAD(m, PathAddBackslashW); LOAD(m, PathRemoveBackslashW); LOAD(m, PathRemoveFileSpecW);
    LOAD(m, PathStripPathW); LOAD(m, PathStripToRootW); LOAD(m, PathSkipRootW);
    LOAD(m, PathIsRelativeW); LOAD(m, PathIsRootW); LOAD(m, PathIsUNCW); LOAD(m, PathIsUNCServerW);
    LOAD(m, PathIsUNCServerShareW); LOAD(m, PathGetDriveNumberW); LOAD(m, PathBuildRootW);
    LOAD(m, PathIsURLW); LOAD(m, PathMatchSpecW); LOAD(m, PathCommonPrefixW);
    LOAD(m, PathRelativePathToW); LOAD(m, PathQuoteSpacesW); LOAD(m, PathUnquoteSpacesW);
    LOAD(m, PathRemoveBlanksW); LOAD(m, PathGetArgsW); LOAD(m, PathRemoveArgsW);
    LOAD(m, PathIsSameRootW); LOAD(m, PathParseIconLocationW); LOAD(m, PathIsPrefixW);
    LOAD(m, PathCompactPathExW); LOAD(m, StrCmpLogicalW);

    /* ---- PathCombineW ---- */
    combine("combine_simple", L"C:\\dir", L"file.txt");
    combine("combine_dir_trailing_slash", L"C:\\dir\\", L"file.txt");
    combine("combine_file_absolute", L"C:\\dir", L"D:\\other\\f");
    combine("combine_file_rooted", L"C:\\dir\\sub", L"\\f");
    combine("combine_dotdot", L"C:\\a\\b\\c", L"..\\..\\x");
    combine("combine_dotdot_past_root", L"C:\\a", L"..\\..\\..\\x");
    combine("combine_dot", L"C:\\a\\.\\b", L".\\c");
    combine("combine_null_dir", NULL, L"rel\\f");
    combine("combine_null_file", L"C:\\a\\..\\b", NULL);
    combine("combine_both_null", NULL, NULL);
    combine("combine_empty_dir", L"", L"f");
    combine("combine_empty_file", L"C:\\a", L"");
    combine("combine_unc", L"\\\\server\\share", L"dir\\f");
    combine("combine_unc_dotdot", L"\\\\server\\share\\a", L"..\\..\\..\\f");
    combine("combine_forward_slashes", L"C:/a/b", L"c/d");
    combine("combine_relative_dir", L"a\\b", L"c");
    combine("combine_drive_relative", L"C:", L"f");
    combine("combine_trailing_dots", L"C:\\a", L"b...");
    {
        wchar_t lng[400];
        for (int i = 0; i < 300; i++) lng[i] = L'x';
        lng[300] = 0;
        combine("combine_result_too_long", L"C:\\dir", lng);
    }

    /* ---- PathCanonicalizeW ---- */
    canon("canon_dotdot", L"C:\\a\\b\\..\\c");
    canon("canon_dot", L"C:\\a\\.\\b");
    canon("canon_past_root", L"C:\\..\\..\\a");
    canon("canon_trailing_dotdot", L"C:\\a\\b\\..");
    canon("canon_trailing_dot", L"C:\\a\\.");
    canon("canon_relative", L"a\\..\\..\\b");
    canon("canon_leading_dot", L".\\a");
    canon("canon_double_backslash", L"C:\\a\\\\b");
    canon("canon_forward_slash", L"C:/a/../b");
    canon("canon_unc", L"\\\\server\\share\\..\\x");
    canon("canon_empty", L"");
    canon("canon_root_only", L"C:\\");
    canon("canon_drive_only", L"C:");
    canon("canon_triple_dots", L"C:\\a\\...\\b");
    canon("canon_dots_in_name", L"C:\\a.b\\..\\c.d");
    canon("canon_trailing_backslash", L"C:\\a\\b\\");

    /* ---- PathAppendW ---- */
    append("append_simple", L"C:\\a", L"b");
    append("append_leading_backslash", L"C:\\a", L"\\b");
    append("append_absolute", L"C:\\a", L"D:\\b");
    append("append_dotdot", L"C:\\a\\b", L"..\\c");
    append("append_empty", L"C:\\a", L"");
    append("append_to_empty", L"", L"b");
    append("append_unc_more", L"\\\\s\\sh", L"x");

    /* ---- finders & predicates ---- */
    finders("find_plain_file", L"C:\\dir\\file.txt");
    finders("find_double_ext", L"C:\\dir\\archive.tar.gz");
    finders("find_dot_in_dir", L"C:\\di.r\\file");
    finders("find_trailing_dot", L"file.");
    finders("find_dotfile", L"C:\\.hidden");
    finders("find_space_in_ext", L"a.b c");
    finders("find_long_ext", L"a.verylongextension");
    finders("find_trailing_backslash", L"C:\\dir\\");
    finders("find_root", L"C:\\");
    finders("find_drive_relative", L"C:file");
    finders("find_drive_only", L"C:");
    finders("find_unc_server", L"\\\\server");
    finders("find_unc_share", L"\\\\server\\share");
    finders("find_unc_share_dir", L"\\\\server\\share\\dir");
    finders("find_unc_trailing", L"\\\\server\\share\\");
    finders("find_rooted_no_drive", L"\\dir\\f");
    finders("find_relative", L"dir\\f.c");
    finders("find_empty", L"");
    finders("find_device_prefix", L"\\\\?\\C:\\x\\y.z");
    finders("find_url_http", L"http://example.com/a.b");
    finders("find_url_file", L"file:///C:/x.txt");
    finders("find_args", L"C:\\prog.exe -a -b");
    finders("find_quoted_args", L"\"C:\\my prog.exe\" -a");
    finders("find_lowercase_drive", L"z:\\x");
    finders("find_forward_slash", L"C:/dir/file.txt");

    /* ---- modifiers ---- */
    modifiers("mod_plain_file", L"C:\\dir\\file.txt");
    modifiers("mod_no_ext", L"C:\\dir\\file");
    modifiers("mod_trailing_backslash", L"C:\\dir\\");
    modifiers("mod_root", L"C:\\");
    modifiers("mod_drive", L"C:");
    modifiers("mod_unc_share_dir", L"\\\\server\\share\\dir\\f.x");
    modifiers("mod_unc_share", L"\\\\server\\share");
    modifiers("mod_relative", L"a\\b.c");
    modifiers("mod_spaces", L"  C:\\my dir\\a b.txt  ");
    modifiers("mod_quoted", L"\"C:\\my dir\\a.txt\"");
    modifiers("mod_args", L"C:\\p.exe /x y");
    modifiers("mod_empty", L"");
    modifiers("mod_rooted", L"\\file.txt");

    /* ---- PathBuildRootW ---- */
    {
        static const int drives[] = {0, 2, 25, 26, -1};
        for (size_t i = 0; i < sizeof drives / sizeof drives[0]; i++) {
            wchar_t b[8];
            char nm[32];
            wmemset(b, L'#', 8);
            b[7] = 0;
            LPWSTR r = pPathBuildRootW(b, drives[i]);
            snprintf(nm, sizeof nm, "buildroot_%d", drives[i]);
            oracle_case(nm);
            off_field("ret", b, r);
            o_wstr("out", b);
            oracle_case_end();
        }
    }

    /* ---- PathMatchSpecW ---- */
    match("match_star_ext", L"file.txt", L"*.txt");
    match("match_star_ext_case", L"FILE.TXT", L"*.txt");
    match("match_star_ext_no", L"file.txt", L"*.doc");
    match("match_multi_spec", L"file.doc", L"*.txt;*.doc");
    match("match_multi_spec_space", L"file.doc", L"*.txt; *.doc");
    match("match_question", L"a1.c", L"a?.c");
    match("match_question_too_short", L"a.c", L"a?.c");
    match("match_star_dot_star_no_ext", L"file", L"*.*");
    match("match_star", L"anything", L"*");
    match("match_path_with_dir", L"C:\\dir\\file.txt", L"*.txt");
    match("match_empty_spec", L"file", L"");
    match("match_dot_spec", L"file", L"*.");

    /* ---- common prefix / relative paths / same root ---- */
    prefix("prefix_shared_dir", L"C:\\a\\b\\c", L"C:\\a\\b\\d");
    prefix("prefix_case", L"C:\\A\\b", L"c:\\a\\B");
    prefix("prefix_partial_name", L"C:\\abc", L"C:\\abd");
    prefix("prefix_root_only", L"C:\\x", L"C:\\y");
    prefix("prefix_different_drives", L"C:\\x", L"D:\\x");
    prefix("prefix_unc", L"\\\\s\\sh\\a", L"\\\\s\\sh\\b");
    prefix("prefix_identical", L"C:\\a\\b", L"C:\\a\\b");
    relpath("rel_sibling_file", L"C:\\a\\b", FILE_ATTRIBUTE_DIRECTORY, L"C:\\a\\c\\f.txt", 0);
    relpath("rel_from_file", L"C:\\a\\b.txt", 0, L"C:\\a\\c\\f.txt", 0);
    relpath("rel_same", L"C:\\a", FILE_ATTRIBUTE_DIRECTORY, L"C:\\a", FILE_ATTRIBUTE_DIRECTORY);
    relpath("rel_child", L"C:\\a", FILE_ATTRIBUTE_DIRECTORY, L"C:\\a\\b\\c", 0);
    relpath("rel_parent", L"C:\\a\\b\\c", FILE_ATTRIBUTE_DIRECTORY, L"C:\\a", FILE_ATTRIBUTE_DIRECTORY);
    relpath("rel_different_drive", L"C:\\a", FILE_ATTRIBUTE_DIRECTORY, L"D:\\a", 0);
    relpath("rel_case_insensitive", L"C:\\A\\B", FILE_ATTRIBUTE_DIRECTORY, L"c:\\a\\b\\f", 0);

    /* ---- PathParseIconLocationW ---- */
    icon("icon_index", L"C:\\x\\shell.dll,3");
    icon("icon_negative", L"C:\\x\\shell.dll,-12");
    icon("icon_none", L"C:\\x\\shell.dll");
    icon("icon_spaces", L"C:\\x\\a.dll , 5");
    icon("icon_quoted", L"\"C:\\x y\\a.dll\",2");

    /* ---- PathCompactPathExW ---- */
    compact("compact_fits", L"C:\\a\\b.txt", 20);
    compact("compact_short", L"C:\\long\\directory\\name\\file.txt", 20);
    compact("compact_very_short", L"C:\\long\\directory\\name\\file.txt", 6);
    compact("compact_filename_only", L"averyveryverylongfilename.txt", 10);
    compact("compact_max_1", L"C:\\a\\b.txt", 1);
    compact("compact_max_0", L"C:\\a\\b.txt", 0);

    /* ---- StrCmpLogicalW ---- */
    {
        static const wchar_t *pairs[][2] = {
            {L"file2", L"file10"}, {L"file10", L"file2"}, {L"a", L"A"}, {L"file02", L"file2"},
            {L"x1y2", L"x1y10"},   {L"", L"a"},           {L"abc", L"abc"}, {L"1.5", L"1.10"},
            {L"a-1", L"a1"},       {L"Z", L"a"},
        };
        oracle_case("strcmplogical");
        for (size_t i = 0; i < sizeof pairs / sizeof pairs[0]; i++) {
            char k[16];
            snprintf(k, sizeof k, "p%zu", i);
            o_i64(k, sgn(pStrCmpLogicalW(pairs[i][0], pairs[i][1])));
        }
        oracle_case_end();
    }
    return oracle_end();
}
