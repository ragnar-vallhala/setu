/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: shlwapi's URL helpers as a file manager uses them for
 * file: URLs, typed locations and drag-and-drop text: UrlCreateFromPathW,
 * PathCreateFromUrlW, UrlIsFileUrlW / UrlIsW, UrlEscapeW, UrlUnescapeW,
 * UrlCanonicalizeW, UrlCombineW, UrlGetPartW and UrlGetLocationW. All of
 * them are pure string functions; nothing touches the file system.
 *
 * Every output records the HRESULT, the returned length and the string.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/nf-shlwapi-urlcreatefrompathw
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/nf-shlwapi-pathcreatefromurlw
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/nf-shlwapi-urlisw
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/nf-shlwapi-urlescapew
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/nf-shlwapi-urlunescapew
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/nf-shlwapi-urlcanonicalizew
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/nf-shlwapi-urlcombinew
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/nf-shlwapi-urlgetpartw
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/nf-shlwapi-urlgetlocationw
 */
#include "oracle.h"
#include <shlwapi.h>

static HRESULT(WINAPI *pUrlCreateFromPathW)(LPCWSTR, LPWSTR, LPDWORD, DWORD);
static HRESULT(WINAPI *pPathCreateFromUrlW)(LPCWSTR, LPWSTR, LPDWORD, DWORD);
static BOOL(WINAPI *pUrlIsW)(LPCWSTR, URLIS);
static HRESULT(WINAPI *pUrlEscapeW)(LPCWSTR, LPWSTR, LPDWORD, DWORD);
static HRESULT(WINAPI *pUrlUnescapeW)(LPWSTR, LPWSTR, LPDWORD, DWORD);
static HRESULT(WINAPI *pUrlCanonicalizeW)(LPCWSTR, LPWSTR, LPDWORD, DWORD);
static HRESULT(WINAPI *pUrlCombineW)(LPCWSTR, LPCWSTR, LPWSTR, LPDWORD, DWORD);
static HRESULT(WINAPI *pUrlGetPartW)(LPCWSTR, LPWSTR, LPDWORD, DWORD, DWORD);
static LPCWSTR(WINAPI *pUrlGetLocationW)(LPCWSTR);

#define LOAD(m, f) (*(FARPROC *)&p##f = GetProcAddress(m, #f))
#define BUF 512

static void out(HRESULT hr, DWORD len, const wchar_t *buf)
{
    o_hex32("hr", (DWORD)hr);
    o_u32("len", len);
    o_wstr("out", SUCCEEDED(hr) ? buf : NULL);
}

static void create_from_path(const char *name, const wchar_t *path, DWORD size)
{
    wchar_t buf[BUF];
    DWORD len = size;
    wmemset(buf, L'#', BUF);
    buf[BUF - 1] = 0;
    HRESULT hr = pUrlCreateFromPathW(path, buf, &len, 0);
    oracle_case(name);
    out(hr, len, buf);
    oracle_case_end();
}

static void path_from_url(const char *name, const wchar_t *url, DWORD size)
{
    wchar_t buf[BUF];
    DWORD len = size;
    wmemset(buf, L'#', BUF);
    buf[BUF - 1] = 0;
    HRESULT hr = pPathCreateFromUrlW(url, buf, &len, 0);
    oracle_case(name);
    out(hr, len, buf);
    oracle_case_end();
}

static void escape(const char *name, const wchar_t *url, DWORD flags)
{
    wchar_t buf[BUF];
    DWORD len = BUF;
    HRESULT hr = pUrlEscapeW(url, buf, &len, flags);
    oracle_case(name);
    out(hr, len, buf);
    oracle_case_end();
}

static void unescape(const char *name, const wchar_t *url, DWORD flags)
{
    wchar_t in[BUF], buf[BUF];
    DWORD len = BUF;
    wcscpy(in, url);
    HRESULT hr = pUrlUnescapeW(in, buf, &len, flags);
    oracle_case(name);
    out(hr, len, buf);
    oracle_case_end();
}

static void canon(const char *name, const wchar_t *url, DWORD flags)
{
    wchar_t buf[BUF];
    DWORD len = BUF;
    HRESULT hr = pUrlCanonicalizeW(url, buf, &len, flags);
    oracle_case(name);
    out(hr, len, buf);
    oracle_case_end();
}

static void combine(const char *name, const wchar_t *base, const wchar_t *rel, DWORD flags)
{
    wchar_t buf[BUF];
    DWORD len = BUF;
    HRESULT hr = pUrlCombineW(base, rel, buf, &len, flags);
    oracle_case(name);
    out(hr, len, buf);
    oracle_case_end();
}

static void part(const char *name, const wchar_t *url, DWORD which, DWORD flags)
{
    wchar_t buf[BUF];
    DWORD len = BUF;
    HRESULT hr = pUrlGetPartW(url, buf, &len, which, flags);
    oracle_case(name);
    out(hr, len, buf);
    oracle_case_end();
}

int main(void)
{
    oracle_begin("shlwapi/url_path");
    HMODULE m = LoadLibraryW(L"shlwapi.dll");
    LOAD(m, UrlCreateFromPathW);
    LOAD(m, PathCreateFromUrlW);
    LOAD(m, UrlIsW);
    LOAD(m, UrlEscapeW);
    LOAD(m, UrlUnescapeW);
    LOAD(m, UrlCanonicalizeW);
    LOAD(m, UrlCombineW);
    LOAD(m, UrlGetPartW);
    LOAD(m, UrlGetLocationW);
    BOOL loaded = pUrlCreateFromPathW && pPathCreateFromUrlW && pUrlIsW && pUrlEscapeW && pUrlUnescapeW &&
                  pUrlCanonicalizeW && pUrlCombineW && pUrlGetPartW && pUrlGetLocationW;
    oracle_case("setup");
    o_bool("loaded", loaded);
    oracle_case_end();
    if (!loaded) return oracle_end();

    /* paths to file: URLs */
    create_from_path("from_path_simple", L"C:\\Users\\Public\\a.txt", BUF);
    create_from_path("from_path_spaces", L"C:\\My Files\\b c.txt", BUF);
    create_from_path("from_path_specials", L"C:\\dir\\x#y%z&w;q=1.txt", BUF);
    create_from_path("from_path_unicode", L"C:\\d\\\x00e9t\x00e9 \x65e5.txt", BUF);
    create_from_path("from_path_unc", L"\\\\server\\share\\f.txt", BUF);
    create_from_path("from_path_dir", L"C:\\dir\\", BUF);
    create_from_path("from_path_relative", L"dir\\f.txt", BUF);
    create_from_path("from_path_already_url", L"file:///C:/x.txt", BUF);
    create_from_path("from_path_http", L"http://example.com/a b", BUF);
    create_from_path("from_path_small_buffer", L"C:\\Users\\Public\\a.txt", 5);

    /* file: URLs to paths */
    path_from_url("to_path_simple", L"file:///C:/Users/Public/a.txt", BUF);
    path_from_url("to_path_escaped", L"file:///C:/My%20Files/b%20c.txt", BUF);
    path_from_url("to_path_two_slashes", L"file://C:/x.txt", BUF);
    path_from_url("to_path_pipe_drive", L"file:///C|/x.txt", BUF);
    path_from_url("to_path_unc", L"file://server/share/f.txt", BUF);
    path_from_url("to_path_localhost", L"file://localhost/C:/x.txt", BUF);
    path_from_url("to_path_backslashes", L"file:///C:\\dir\\x.txt", BUF);
    path_from_url("to_path_utf8_escapes", L"file:///C:/d/%C3%A9t%C3%A9.txt", BUF);
    path_from_url("to_path_hash", L"file:///C:/dir/x%23y.txt", BUF);
    path_from_url("to_path_http", L"http://example.com/x", BUF);
    path_from_url("to_path_small_buffer", L"file:///C:/Users/Public/a.txt", 5);

    oracle_case("url_is");
    o_bool("file_url_file", pUrlIsW(L"file:///C:/x.txt", URLIS_FILEURL));
    o_bool("file_url_http", pUrlIsW(L"http://e.com/", URLIS_FILEURL));
    o_bool("file_url_path", pUrlIsW(L"C:\\x.txt", URLIS_FILEURL));
    o_bool("url_http", pUrlIsW(L"http://e.com/", URLIS_URL));
    o_bool("url_path", pUrlIsW(L"C:\\x.txt", URLIS_URL));
    o_bool("opaque_mailto", pUrlIsW(L"mailto:a@b.c", URLIS_OPAQUE));
    o_bool("opaque_http", pUrlIsW(L"http://e.com/", URLIS_OPAQUE));
    o_bool("hasquery", pUrlIsW(L"http://e.com/?q=1", URLIS_HASQUERY));
    o_bool("hasquery_none", pUrlIsW(L"http://e.com/a#b", URLIS_HASQUERY));
    o_bool("hasquery_file", pUrlIsW(L"file:///C:/a?b.txt", URLIS_HASQUERY));
    o_bool("appliable_www", pUrlIsW(L"www.example.com", URLIS_APPLIABLE));
    oracle_case_end();

    escape("escape_default", L"http://e.com/a b/c#d?e f", 0);
    escape("escape_spaces_only", L"http://e.com/a b/c#d?e f", URL_ESCAPE_SPACES_ONLY);
    escape("escape_percent", L"http://e.com/100%/x", URL_ESCAPE_PERCENT);
    escape("escape_segment_only", L"a b/c?d#e", URL_ESCAPE_SEGMENT_ONLY);
    escape("escape_file", L"file:///C:/a b/c#d.txt", 0);

    unescape("unescape_default", L"http://e.com/a%20b/c%23d?e%20f", 0);
    unescape("unescape_until_query", L"http://e.com/a%20b?e%20f", URL_DONT_UNESCAPE_EXTRA_INFO);
    unescape("unescape_bad_escape", L"a%2xb%%c%", 0);

    canon("canon_dots", L"http://e.com/a/./b/../c", 0);
    canon("canon_spaces", L"http://e.com/a b", 0);
    canon("canon_no_encode", L"http://e.com/a b", URL_DONT_ESCAPE_EXTRA_INFO);
    canon("canon_file_path", L"C:\\dir\\..\\x y.txt", 0);
    canon("canon_file_url", L"file:///C:/dir/../x y.txt", 0);
    canon("canon_file_url_to_path", L"file:///C:/dir/x%20y.txt", URL_FILE_USE_PATHURL);
    canon("canon_unescape", L"http://e.com/a%20b", URL_UNESCAPE);

    combine("combine_relative", L"http://e.com/a/b/c.html", L"../d.html", 0);
    combine("combine_absolute", L"http://e.com/a/b", L"/x/y", 0);
    combine("combine_other_scheme", L"http://e.com/a/b", L"ftp://f.org/z", 0);
    combine("combine_file", L"file:///C:/dir/a.txt", L"b.txt", 0);
    combine("combine_query", L"http://e.com/a/b?x=1", L"?y=2", 0);

    part("part_scheme", L"http://user:pw@e.com:8080/p/q?x=1#f", URL_PART_SCHEME, 0);
    part("part_host", L"http://user:pw@e.com:8080/p/q?x=1#f", URL_PART_HOSTNAME, 0);
    part("part_user", L"http://user:pw@e.com:8080/p/q?x=1#f", URL_PART_USERNAME, 0);
    part("part_port", L"http://user:pw@e.com:8080/p/q?x=1#f", URL_PART_PORT, 0);
    part("part_query", L"http://user:pw@e.com:8080/p/q?x=1#f", URL_PART_QUERY, 0);
    part("part_host_with_scheme", L"http://e.com/p", URL_PART_HOSTNAME, URL_PARTFLAG_KEEPSCHEME);
    part("part_file_host", L"file:///C:/x.txt", URL_PART_HOSTNAME, 0);

    oracle_case("get_location");
    const wchar_t *u = L"http://e.com/p?x=1#frag";
    const wchar_t *loc = pUrlGetLocationW(u);
    o_i64("offset", loc ? (long long)(loc - u) : -1);
    u = L"file:///C:/x.txt#a";
    loc = pUrlGetLocationW(u);
    o_i64("file_offset", loc ? (long long)(loc - u) : -1);
    oracle_case_end();
    return oracle_end();
}
