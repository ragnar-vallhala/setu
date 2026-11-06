/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: the shlwapi string helpers a file manager uses to show
 * sizes and times, and to sort and parse names. StrFormatByteSizeW /
 * StrFormatByteSizeEx / StrFormatByteSize64A / StrFormatKBSizeW across
 * unit boundaries, StrFromTimeIntervalW with different digit counts,
 * StrToIntExW (decimal, hex, signs, junk), StrTrimW, StrStrIW,
 * StrCmpIW / StrCmpNW, StrChrW, StrRChrW, StrCSpnW, StrSpnW,
 * StrPBrkW, StrIsIntlEqualW, ChrCmpIW, StrDupW, PathMakePrettyW,
 * PathIsContentTypeW and AssocIsDangerous.
 *
 * Output depends on the user's locale for decimal separators; the oracle
 * VM and the Setu prefix both use en-US.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/nf-shlwapi-strformatbytesizew
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/nf-shlwapi-strformatbytesizeex
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/nf-shlwapi-strformatkbsizew
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/nf-shlwapi-strfromtimeintervalw
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/nf-shlwapi-strtointexw
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/nf-shlwapi-strtrimw
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/nf-shlwapi-pathmakeprettyw
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/nf-shlwapi-pathiscontenttypew
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/nf-shlwapi-associsdangerous
 */
#include "oracle.h"
#include <shlwapi.h>

static LPWSTR(WINAPI *pStrFormatByteSizeW)(LONGLONG, LPWSTR, UINT);
static HRESULT(WINAPI *pStrFormatByteSizeEx)(ULONGLONG, SFBS_FLAGS, LPWSTR, UINT);
static LPSTR(WINAPI *pStrFormatByteSize64A)(LONGLONG, LPSTR, UINT);
static LPWSTR(WINAPI *pStrFormatKBSizeW)(LONGLONG, LPWSTR, UINT);
static INT(WINAPI *pStrFromTimeIntervalW)(LPWSTR, UINT, DWORD, int);
static BOOL(WINAPI *pStrToIntExW)(LPCWSTR, STIF_FLAGS, int *);
static BOOL(WINAPI *pStrTrimW)(LPWSTR, LPCWSTR);
static LPWSTR(WINAPI *pStrStrIW)(LPCWSTR, LPCWSTR);
static int(WINAPI *pStrCmpIW)(LPCWSTR, LPCWSTR);
static int(WINAPI *pStrCmpNW)(LPCWSTR, LPCWSTR, int);
static LPWSTR(WINAPI *pStrChrW)(LPCWSTR, WCHAR);
static LPWSTR(WINAPI *pStrRChrW)(LPCWSTR, LPCWSTR, WCHAR);
static int(WINAPI *pStrCSpnW)(LPCWSTR, LPCWSTR);
static int(WINAPI *pStrSpnW)(LPCWSTR, LPCWSTR);
static LPWSTR(WINAPI *pStrPBrkW)(LPCWSTR, LPCWSTR);
static BOOL(WINAPI *pStrIsIntlEqualW)(BOOL, LPCWSTR, LPCWSTR, int);
static BOOL(WINAPI *pChrCmpIW)(WCHAR, WCHAR);
static LPWSTR(WINAPI *pStrDupW)(LPCWSTR);
static BOOL(WINAPI *pPathMakePrettyW)(LPWSTR);
static BOOL(WINAPI *pPathIsContentTypeW)(LPCWSTR, LPCWSTR);
static BOOL(WINAPI *pAssocIsDangerous)(LPCWSTR);
static HLOCAL(WINAPI *pLocalFree)(HLOCAL);

#define LOAD(m, f) (*(FARPROC *)&p##f = GetProcAddress(m, #f))

static int sgn(int v) { return v < 0 ? -1 : v > 0; }

int main(void)
{
    oracle_begin("shlwapi/str_format");
    HMODULE m = LoadLibraryW(L"shlwapi.dll");
    HMODULE k = GetModuleHandleW(L"kernel32.dll");
    LOAD(m, StrFormatByteSizeW);
    LOAD(m, StrFormatByteSizeEx);
    LOAD(m, StrFormatByteSize64A);
    LOAD(m, StrFormatKBSizeW);
    LOAD(m, StrFromTimeIntervalW);
    LOAD(m, StrToIntExW);
    LOAD(m, StrTrimW);
    LOAD(m, StrStrIW);
    LOAD(m, StrCmpIW);
    LOAD(m, StrCmpNW);
    LOAD(m, StrChrW);
    LOAD(m, StrRChrW);
    LOAD(m, StrCSpnW);
    LOAD(m, StrSpnW);
    LOAD(m, StrPBrkW);
    LOAD(m, StrIsIntlEqualW);
    LOAD(m, ChrCmpIW);
    LOAD(m, StrDupW);
    LOAD(m, PathMakePrettyW);
    LOAD(m, PathIsContentTypeW);
    LOAD(m, AssocIsDangerous);
    LOAD(k, LocalFree);
    oracle_case("setup");
    o_bool("has_StrFormatByteSizeEx", pStrFormatByteSizeEx != NULL);
    o_bool("has_AssocIsDangerous", pAssocIsDangerous != NULL);
    o_bool("loaded", pStrFormatByteSizeW && pStrFormatByteSize64A && pStrFormatKBSizeW && pStrFromTimeIntervalW &&
                         pStrToIntExW && pStrTrimW && pStrStrIW && pStrCmpIW && pStrCmpNW && pStrChrW && pStrRChrW &&
                         pStrCSpnW && pStrSpnW && pStrPBrkW && pStrIsIntlEqualW && pChrCmpIW && pStrDupW &&
                         pPathMakePrettyW && pPathIsContentTypeW && pLocalFree);
    oracle_case_end();

    static const LONGLONG sizes[] = {
        0, 1, 532, 999, 1000, 1023, 1024, 1025, 1536, 10239, 10240, 102400, 1023999, 1048575, 1048576,
        1572864, 10485760, 104857600, 1073741823, 1073741824, 1610612736, 1099511627776LL,
        1125899906842624LL, 1152921504606846976LL, -1,
    };
    wchar_t buf[128];
    char abuf[128];
    char nm[64];
    for (size_t i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        snprintf(nm, sizeof nm, "bytes_%lld", (long long)sizes[i]);
        oracle_case(nm);
        wcscpy(buf, L"#");
        LPWSTR r = pStrFormatByteSizeW(sizes[i], buf, 128);
        o_bool("ret_is_buf", r == buf);
        o_wstr("w", buf);
        strcpy(abuf, "#");
        pStrFormatByteSize64A(sizes[i], abuf, 128);
        o_str("a", abuf);
        wcscpy(buf, L"#");
        pStrFormatKBSizeW(sizes[i], buf, 128);
        o_wstr("kb", buf);
        if (pStrFormatByteSizeEx) {
            wcscpy(buf, L"#");
            o_hex32("ex_round_hr", (DWORD)pStrFormatByteSizeEx((ULONGLONG)sizes[i], SFBS_FLAGS_ROUND_TO_NEAREST_DISPLAYED_DIGIT, buf, 128));
            o_wstr("ex_round", buf);
            wcscpy(buf, L"#");
            pStrFormatByteSizeEx((ULONGLONG)sizes[i], SFBS_FLAGS_TRUNCATE_UNDISPLAYED_DECIMAL_DIGITS, buf, 128);
            o_wstr("ex_trunc", buf);
        }
        oracle_case_end();
    }
    oracle_case("bytes_small_buffers");
    wcscpy(buf, L"#####");
    LPWSTR r = pStrFormatByteSizeW(1048576, buf, 3);
    o_bool("w3_ret_null", r == NULL);
    o_wstr("w3", buf);
    wcscpy(buf, L"#####");
    r = pStrFormatKBSizeW(1048576, buf, 3);
    o_bool("kb3_ret_null", r == NULL);
    o_wstr("kb3", buf);
    if (pStrFormatByteSizeEx) {
        wcscpy(buf, L"#####");
        o_hex32("ex3_hr", (DWORD)pStrFormatByteSizeEx(1048576, SFBS_FLAGS_ROUND_TO_NEAREST_DISPLAYED_DIGIT, buf, 3));
        o_hex32("ex_badflags_hr", (DWORD)pStrFormatByteSizeEx(1048576, 0, buf, 128));
        o_hex32("ex_zero_buf_hr", (DWORD)pStrFormatByteSizeEx(1048576, SFBS_FLAGS_ROUND_TO_NEAREST_DISPLAYED_DIGIT, buf, 0));
    }
    oracle_case_end();

    static const DWORD ms[] = {0, 1, 999, 1000, 1499, 1500, 59999, 60000, 61000, 3599000, 3600000, 3661000, 86400000, 0xffffffff};
    for (size_t i = 0; i < sizeof ms / sizeof ms[0]; i++) {
        snprintf(nm, sizeof nm, "interval_%lu", (unsigned long)ms[i]);
        oracle_case(nm);
        for (int d = 1; d <= 6; d += (d < 3 ? 1 : 3)) {
            char key[16];
            wcscpy(buf, L"#");
            int n = pStrFromTimeIntervalW(buf, 128, ms[i], d);
            snprintf(key, sizeof key, "d%d", d);
            o_wstr(key, buf);
            snprintf(key, sizeof key, "d%d_len", d);
            o_i64(key, n);
        }
        oracle_case_end();
    }
    oracle_case("interval_small_buffer");
    wcscpy(buf, L"#########");
    o_i64("ret", pStrFromTimeIntervalW(buf, 4, 3661000, 6));
    o_wstr("out", buf);
    oracle_case_end();

    static const struct { const wchar_t *s; STIF_FLAGS f; } ints[] = {
        {L"123", STIF_DEFAULT}, {L"  42", STIF_DEFAULT}, {L"-17", STIF_DEFAULT}, {L"+5", STIF_DEFAULT},
        {L"12abc", STIF_DEFAULT}, {L"abc", STIF_DEFAULT}, {L"", STIF_DEFAULT}, {L"0x1F", STIF_DEFAULT},
        {L"0x1F", STIF_SUPPORT_HEX}, {L"0X1f", STIF_SUPPORT_HEX}, {L"-0x10", STIF_SUPPORT_HEX},
        {L"0x", STIF_SUPPORT_HEX}, {L"2147483648", STIF_DEFAULT}, {L"4294967295", STIF_DEFAULT},
        {L"0xFFFFFFFF", STIF_SUPPORT_HEX}, {L"\t7", STIF_DEFAULT}, {L"- 3", STIF_DEFAULT},
    };
    oracle_case("strtointex");
    for (size_t i = 0; i < sizeof ints / sizeof ints[0]; i++) {
        int v = 777;
        BOOL b = pStrToIntExW(ints[i].s, ints[i].f, &v);
        char key[24];
        snprintf(key, sizeof key, "r%zu", i);
        o_bool(key, b);
        snprintf(key, sizeof key, "v%zu", i);
        o_i64(key, v);
    }
    oracle_case_end();

    oracle_case("strtrim");
    {
        wchar_t s1[] = L"  hello world  ", s2[] = L"xxabcxx", s3[] = L"    ", s4[] = L"nothing";
        o_bool("r1", pStrTrimW(s1, L" "));
        o_wstr("s1", s1);
        o_bool("r2", pStrTrimW(s2, L"x"));
        o_wstr("s2", s2);
        o_bool("r3", pStrTrimW(s3, L" "));
        o_wstr("s3", s3);
        o_bool("r4", pStrTrimW(s4, L" "));
        o_wstr("s4", s4);
    }
    oracle_case_end();

    oracle_case("search_compare");
    {
        const wchar_t *hay = L"Program Files\\Setu Oracle\\App.EXE";
        LPWSTR p = pStrStrIW(hay, L"setu");
        o_i64("strstri_off", p ? (int)(p - hay) : -1);
        o_bool("strstri_missing_null", pStrStrIW(hay, L"zzz") == NULL);
        o_bool("strstri_empty_null", pStrStrIW(hay, L"") == NULL);
        o_i64("cmpi_eq", sgn(pStrCmpIW(L"ABC", L"abc")));
        o_i64("cmpi_lt", sgn(pStrCmpIW(L"abc", L"abd")));
        o_i64("cmpi_underscore", sgn(pStrCmpIW(L"a_b", L"aab")));
        o_i64("cmpn_prefix", sgn(pStrCmpNW(L"abcdef", L"abcxyz", 3)));
        o_i64("cmpn_case", sgn(pStrCmpNW(L"ABC", L"abc", 3)));
        p = pStrChrW(hay, L'\\');
        o_i64("strchr_off", p ? (int)(p - hay) : -1);
        p = pStrRChrW(hay, NULL, L'\\');
        o_i64("strrchr_off", p ? (int)(p - hay) : -1);
        o_i64("cspn", pStrCSpnW(hay, L"\\."));
        o_i64("spn", pStrSpnW(L"aabbcde", L"ab"));
        p = pStrPBrkW(hay, L".\\");
        o_i64("pbrk_off", p ? (int)(p - hay) : -1);
        o_bool("intl_eq_nocase", pStrIsIntlEqualW(FALSE, L"Hello", L"hELLo", 5));
        o_bool("intl_eq_case", pStrIsIntlEqualW(TRUE, L"Hello", L"hELLo", 5));
        o_bool("chrcmpi_same", pChrCmpIW(L'a', L'A'));
        o_bool("chrcmpi_diff", pChrCmpIW(L'a', L'b'));
        LPWSTR d = pStrDupW(L"dup me");
        o_wstr("strdup", d);
        pLocalFree(d);
        o_bool("strdup_null_nonnull", (d = pStrDupW(NULL)) != NULL);
        if (d) {
            o_wstr("strdup_null", d);
            pLocalFree(d);
        }
    }
    oracle_case_end();

    oracle_case("pathmakepretty");
    {
        wchar_t p1[] = L"C:\\PROGRAM FILES\\APP.EXE", p2[] = L"C:\\Program Files\\App.exe", p3[] = L"\\\\SERVER\\SHARE";
        wchar_t p4[] = L"ALLCAPS";
        o_bool("r1", pPathMakePrettyW(p1));
        o_wstr("p1", p1);
        o_bool("r2", pPathMakePrettyW(p2));
        o_wstr("p2", p2);
        o_bool("r3", pPathMakePrettyW(p3));
        o_wstr("p3", p3);
        o_bool("r4", pPathMakePrettyW(p4));
        o_wstr("p4", p4);
    }
    oracle_case_end();

    oracle_case("content_type");
    o_bool("txt_text_plain", pPathIsContentTypeW(L"a.txt", L"text/plain"));
    o_bool("txt_text_html", pPathIsContentTypeW(L"a.txt", L"text/html"));
    o_bool("htm_text_html", pPathIsContentTypeW(L"x\\page.htm", L"text/html"));
    o_bool("noext", pPathIsContentTypeW(L"noext", L"text/plain"));
    o_bool("unknown", pPathIsContentTypeW(L"a.qqqzz", L"text/plain"));
    oracle_case_end();

    if (pAssocIsDangerous) {
        static const wchar_t *ext[] = {L".exe", L".bat", L".cmd", L".com", L".scr", L".pif", L".vbs", L".js",
                                       L".lnk", L".msi", L".ps1", L".txt", L".jpg", L".pdf", L".zip", L".qqqzz",
                                       L"exe", L"C:\\a\\b.exe", L"b.TXT"};
        oracle_case("assoc_is_dangerous");
        for (size_t i = 0; i < sizeof ext / sizeof ext[0]; i++) {
            char key[48];
            int j = 0;
            for (const wchar_t *c = ext[i]; *c && j < 40; c++)
                key[j++] = (*c >= L'a' && *c <= L'z') || (*c >= L'A' && *c <= L'Z') ? (char)*c : '_';
            key[j] = 0;
            o_bool(key, pAssocIsDangerous(ext[i]));
        }
        oracle_case_end();
    }
    return oracle_end();
}
