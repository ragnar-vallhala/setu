/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: character classification and script APIs: GetStringTypeW /
 * GetStringTypeExW / GetStringTypeA (CT_CTYPE1/2/3 for characters outside
 * kernel32/string_format's set, ANSI conversion by locale code page),
 * GetStringScripts, VerifyScripts, IsNLSDefinedString and GetNLSVersionEx
 * (by shape only; version numbers depend on the Windows build).
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/stringapiset/nf-stringapiset-getstringtypew
 *       https://learn.microsoft.com/en-us/windows/win32/api/stringapiset/nf-stringapiset-getstringtypeexw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winnls/nf-winnls-getstringtypea
 *       https://learn.microsoft.com/en-us/windows/win32/api/winnls/nf-winnls-getstringscripts
 *       https://learn.microsoft.com/en-us/windows/win32/api/winnls/nf-winnls-verifyscripts
 *       https://learn.microsoft.com/en-us/windows/win32/api/winnls/nf-winnls-isnlsdefinedstring
 *       https://learn.microsoft.com/en-us/windows/win32/api/winnls/nf-winnls-getnlsversionex
 */
#include "oracle.h"

typedef int (WINAPI *scripts_fn)(DWORD, LPCWSTR, int, LPWSTR, int);
typedef BOOL (WINAPI *verify_fn)(DWORD, LPCWSTR, int, LPCWSTR, int);
static scripts_fn pGetStringScripts;
static verify_fn pVerifyScripts;

static void types(const char *name, const wchar_t *s)
{
    WORD t1[32], t2[32], t3[32];
    memset(t1, 0xff, sizeof t1);
    memset(t2, 0xff, sizeof t2);
    memset(t3, 0xff, sizeof t3);
    int n = (int)wcslen(s);
    BOOL r1 = GetStringTypeW(CT_CTYPE1, s, n, t1);
    BOOL r2 = GetStringTypeW(CT_CTYPE2, s, n, t2);
    BOOL r3 = GetStringTypeW(CT_CTYPE3, s, n, t3);
    char k1[256] = "", k2[256] = "", k3[256] = "";
    for (int i = 0; i < n; i++) {
        char t[16];
        snprintf(t, sizeof t, "%s%04x", i ? "," : "", t1[i]);
        strcat(k1, t);
        snprintf(t, sizeof t, "%s%x", i ? "," : "", t2[i]);
        strcat(k2, t);
        snprintf(t, sizeof t, "%s%04x", i ? "," : "", t3[i]);
        strcat(k3, t);
    }
    oracle_case(name);
    o_bool("ok", r1 && r2 && r3);
    o_str("ctype1", k1);
    o_str("ctype2", k2);
    o_str("ctype3", k3);
    oracle_case_end();
}

static void scripts(const char *name, DWORD flags, const wchar_t *s, int n, int size)
{
    wchar_t out[128];
    int r;
    wmemset(out, L'#', 128);
    ORACLE_CALL(r, pGetStringScripts(flags, s, n, size ? out : NULL, size));
    oracle_case(name);
    o_i64("ret", r);
    o_lasterr();
    if (r > 0 && size) o_wstr("scripts", out);
    oracle_case_end();
}

static void verify(const char *name, DWORD flags, const wchar_t *loc, const wchar_t *test)
{
    BOOL r;
    ORACLE_CALL(r, pVerifyScripts(flags, loc, -1, test, -1));
    oracle_case(name);
    o_bool("ret", r);
    o_lasterr();
    oracle_case_end();
}

int main(void)
{
    oracle_begin("kernel32/string_type");

    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    pGetStringScripts = (scripts_fn)(void *)GetProcAddress(k32, "GetStringScripts");
    pVerifyScripts = (verify_fn)(void *)GetProcAddress(k32, "VerifyScripts");

    types("arabic_letters_digits", L"\x0627\x0628\x0660\x0669\x064b");
    types("hebrew", L"\x05d0\x05b0\x05be");
    types("devanagari", L"\x0915\x093f\x094d\x0966");
    types("cjk_and_kana", L"\x4e00\x3042\x30a2\xff71\x3005");
    types("hangul", L"\xac00\x1100\x3131");
    types("fullwidth_ascii", L"\xff21\xff41\xff10\xff01");
    types("math_and_currency", L"\x221e\x20ac\x00a3\x00b5");
    types("spaces", L"\x00a0\x2002\x200b\x3000\x2028");
    types("controls", L"\x0001\x007f\x0085\x00ad\xfeff");
    types("surrogates_private", L"\xd800\xdc00\xe000\xfffd\xffff");
    types("greek_cyrillic", L"\x03a3\x03c3\x0416\x0436\x0401");
    types("titlecase_digraph", L"\x01c5\x01c6\x01c4");

    {
        WORD t[4];
        BOOL r;
        memset(t, 0xff, sizeof t);
        ORACLE_CALL(r, GetStringTypeExW(MAKELCID(0x0411, SORT_DEFAULT), CT_CTYPE3, L"\x3042\xff71", 2, t));
        oracle_case("ex_w_ignores_locale");
        o_bool("ret", r);
        o_lasterr();
        o_hex32("t0", t[0]);
        o_hex32("t1", t[1]);
        oracle_case_end();
        memset(t, 0xff, sizeof t);
        ORACLE_CALL(r, GetStringTypeA(MAKELCID(0x0411, SORT_DEFAULT), CT_CTYPE1, "\x82\xa0" "a", 3, t));
        oracle_case("a_cp932_dbcs");
        o_bool("ret", r);
        o_lasterr();
        o_hex32("t0", t[0]);
        o_hex32("t1", t[1]);
        o_hex32("t2", t[2]);
        oracle_case_end();
        memset(t, 0xff, sizeof t);
        ORACLE_CALL(r, GetStringTypeA(MAKELCID(0x0419, SORT_DEFAULT), CT_CTYPE1, "\xc0\xe0", 2, t));
        oracle_case("a_cp1251_cyrillic");
        o_bool("ret", r);
        o_lasterr();
        o_hex32("t0", t[0]);
        o_hex32("t1", t[1]);
        oracle_case_end();
        memset(t, 0xff, sizeof t);
        ORACLE_CALL(r, GetStringTypeExA(MAKELCID(0x0409, SORT_DEFAULT), CT_CTYPE2, "a1 ", -1, t));
        oracle_case("exa_minus_one");
        o_bool("ret", r);
        o_lasterr();
        o_hex32("t0", t[0]);
        o_hex32("t1", t[1]);
        o_hex32("t2", t[2]);
        o_hex32("t3_nul", t[3]);
        oracle_case_end();
        memset(t, 0xff, sizeof t);
        ORACLE_CALL(r, GetStringTypeA(0x7fff, CT_CTYPE1, "a", 1, t));
        oracle_case("a_bad_locale");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
    }

    oracle_case("have_script_fns");
    o_bool("get_string_scripts", pGetStringScripts != NULL);
    o_bool("verify_scripts", pVerifyScripts != NULL);
    oracle_case_end();
    if (pGetStringScripts) {
        scripts("scripts_latin", 0, L"abc", -1, 128);
        scripts("scripts_mixed", 0, L"a\x0416\x4e00\x3042", -1, 128);
        scripts("scripts_common_only", 0, L"123 !", -1, 128);
        scripts("scripts_allow_latin", GSS_ALLOW_INHERITED_COMMON, L"1a\x0301", -1, 128);
        scripts("scripts_inherited_plain", 0, L"a\x0301", -1, 128);
        scripts("scripts_size_query", 0, L"a\x0416", -1, 0);
        scripts("scripts_buffer_too_small", 0, L"a\x0416", -1, 3);
        scripts("scripts_bad_flag", 0x10, L"a", -1, 128);
        scripts("scripts_counted", 0, L"a\x0416", 1, 128);
        scripts("scripts_greek_arabic", 0, L"\x03b1\x0627", -1, 128);
    }
    if (pVerifyScripts) {
        verify("verify_latin_in_latin", 0, L"Latn;", L"Latn;");
        verify("verify_cyrl_not_in_latin", 0, L"Latn;", L"Cyrl;");
        verify("verify_subset", 0, L"Cyrl;Latn;", L"Latn;");
        verify("verify_empty_test", 0, L"Latn;", L"");
        verify("verify_zyyy_ignored", 0, L"Latn;", L"Latn;Zyyy;");
        verify("verify_allow_latin", VS_ALLOW_LATIN, L"Cyrl;", L"Latn;");
        verify("verify_bad_input_format", 0, L"Latn", L"Latn;");
        verify("verify_bad_flag", 0x10, L"Latn;", L"Latn;");
    }

    {
        NLSVERSIONINFOEX v;
        BOOL r;
        memset(&v, 0, sizeof v);
        v.dwNLSVersionInfoSize = sizeof v;
        ORACLE_CALL(r, GetNLSVersionEx(COMPARE_STRING, L"en-US", &v));
        oracle_case("nls_version_en_us");
        o_bool("ret", r);
        o_lasterr();
        o_bool("version_nonzero", v.dwNLSVersion != 0);
        o_bool("defined_version_nonzero", v.dwDefinedVersion != 0);
        o_hex32("effective_id", v.dwEffectiveId);
        oracle_case_end();
        memset(&v, 0, sizeof v);
        v.dwNLSVersionInfoSize = sizeof(NLSVERSIONINFO);
        ORACLE_CALL(r, GetNLSVersionEx(COMPARE_STRING, L"en-US", &v));
        oracle_case("nls_version_small_struct");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        memset(&v, 0, sizeof v);
        v.dwNLSVersionInfoSize = sizeof v;
        ORACLE_CALL(r, GetNLSVersionEx(2, L"en-US", &v));
        oracle_case("nls_version_bad_function");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, GetNLSVersionEx(COMPARE_STRING, L"xx-YY", &v));
        oracle_case("nls_version_bad_locale");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, IsNLSDefinedString(COMPARE_STRING, 0, NULL, L"abc", -1));
        oracle_case("defined_ascii");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, IsNLSDefinedString(COMPARE_STRING, 0, NULL, L"a\xffff", -1));
        oracle_case("defined_noncharacter");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, IsNLSDefinedString(COMPARE_STRING, 0, NULL, L"a\xd800", -1));
        oracle_case("defined_lone_surrogate");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, IsNLSDefinedString(COMPARE_STRING, 0, NULL, L"\xd83d\xde00", -1));
        oracle_case("defined_emoji_pair");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, IsNLSDefinedString(2, 0, NULL, L"abc", -1));
        oracle_case("defined_bad_function");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
    }

    return oracle_end();
}
