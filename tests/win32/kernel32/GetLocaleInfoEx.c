/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: GetLocaleInfoEx for a fixed set of LCTYPEs across a fixed
 * set of named locales (one case per locale and group), plus the buffer
 * rules, LOCALE_RETURN_NUMBER, neutral and odd locale names, invalid
 * LCTYPEs and the LCID-based GetLocaleInfoW / GetLocaleInfoA.
 *
 * Every lookup uses LOCALE_NOUSEROVERRIDE or a locale that is not the user's,
 * so the machine's regional settings never show up. The user and system
 * default locales are only compared with each other.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winnls/nf-winnls-getlocaleinfoex
 *       https://learn.microsoft.com/en-us/windows/win32/intl/locale-information-constants
 *       https://learn.microsoft.com/en-us/windows/win32/intl/locale-names
 *       https://learn.microsoft.com/en-us/windows/win32/api/winnls/nf-winnls-getlocaleinfow
 */
#include "oracle.h"

struct lct { const char *name; LCTYPE type; };

#define T(x) { #x, x }
static const struct lct codes[] = {
    T(LOCALE_SNAME), T(LOCALE_ILANGUAGE), T(LOCALE_SISO639LANGNAME), T(LOCALE_SISO639LANGNAME2),
    T(LOCALE_SISO3166CTRYNAME), T(LOCALE_SISO3166CTRYNAME2), T(LOCALE_IDEFAULTANSICODEPAGE),
    T(LOCALE_IDEFAULTCODEPAGE), T(LOCALE_IDEFAULTMACCODEPAGE), T(LOCALE_IDEFAULTEBCDICCODEPAGE),
    T(LOCALE_SPARENT), T(LOCALE_SSCRIPTS), T(LOCALE_IREADINGLAYOUT), T(LOCALE_IGEOID),
    { NULL, 0 }
};
static const struct lct numbers[] = {
    T(LOCALE_SDECIMAL), T(LOCALE_STHOUSAND), T(LOCALE_SGROUPING), T(LOCALE_IDIGITS),
    T(LOCALE_ILZERO), T(LOCALE_INEGNUMBER), T(LOCALE_SNEGATIVESIGN), T(LOCALE_SPOSITIVESIGN),
    T(LOCALE_SNATIVEDIGITS), T(LOCALE_IDIGITSUBSTITUTION), T(LOCALE_IMEASURE), T(LOCALE_SLIST),
    T(LOCALE_SPERCENT), T(LOCALE_SNAN), T(LOCALE_SPOSINFINITY), T(LOCALE_SNEGINFINITY),
    { NULL, 0 }
};
static const struct lct money[] = {
    T(LOCALE_SCURRENCY), T(LOCALE_SINTLSYMBOL), T(LOCALE_ICURRDIGITS), T(LOCALE_IINTLCURRDIGITS),
    T(LOCALE_ICURRENCY), T(LOCALE_INEGCURR), T(LOCALE_SMONDECIMALSEP), T(LOCALE_SMONTHOUSANDSEP),
    T(LOCALE_SMONGROUPING), T(LOCALE_SENGCURRNAME), T(LOCALE_SNATIVECURRNAME),
    { NULL, 0 }
};
static const struct lct dates[] = {
    T(LOCALE_SSHORTDATE), T(LOCALE_SLONGDATE), T(LOCALE_SYEARMONTH), T(LOCALE_SMONTHDAY),
    T(LOCALE_STIMEFORMAT), T(LOCALE_SSHORTTIME), T(LOCALE_S1159), T(LOCALE_S2359),
    T(LOCALE_IFIRSTDAYOFWEEK), T(LOCALE_IFIRSTWEEKOFYEAR), T(LOCALE_ICALENDARTYPE),
    T(LOCALE_IOPTIONALCALENDAR), T(LOCALE_SDAYNAME1), T(LOCALE_SDAYNAME7), T(LOCALE_SABBREVDAYNAME1),
    T(LOCALE_SSHORTESTDAYNAME1), T(LOCALE_SMONTHNAME1), T(LOCALE_SMONTHNAME12), T(LOCALE_SABBREVMONTHNAME1),
    { NULL, 0 }
};
static const struct lct names[] = {
    T(LOCALE_SENGLISHLANGUAGENAME), T(LOCALE_SENGLISHCOUNTRYNAME), T(LOCALE_SENGLISHDISPLAYNAME),
    T(LOCALE_SNATIVELANGUAGENAME), T(LOCALE_SNATIVECOUNTRYNAME), T(LOCALE_SNATIVEDISPLAYNAME),
    { NULL, 0 }
};
#undef T

static void group(const char *casename, const wchar_t *loc, const struct lct *t)
{
    oracle_case(casename);
    for (; t->name; t++) {
        wchar_t buf[256];
        int r = GetLocaleInfoEx(loc, t->type | LOCALE_NOUSEROVERRIDE, buf, 256);
        if (r > 0) o_wstr(t->name + 7, buf); /* drop "LOCALE_" */
        else {
            char k[64];
            snprintf(k, sizeof k, "%s.error", t->name + 7);
            o_u32(k, GetLastError());
        }
    }
    oracle_case_end();
}

static void one(const char *name, const wchar_t *loc, LCTYPE type, int size)
{
    wchar_t buf[256];
    int r;
    wmemset(buf, L'#', 256);
    ORACLE_CALL(r, GetLocaleInfoEx(loc, type, size ? buf : NULL, size));
    oracle_case(name);
    o_i64("ret", r);
    o_lasterr();
    if (r > 0 && size) {
        if (type & LOCALE_RETURN_NUMBER) o_u32("number", *(DWORD *)buf);
        else o_wstr("value", buf);
    }
    oracle_case_end();
}

int main(void)
{
    oracle_begin("kernel32/GetLocaleInfoEx");

    static const struct { const char *tag; const wchar_t *name; } locs[] = {
        { "en_us", L"en-US" }, { "de_de", L"de-DE" }, { "fr_fr", L"fr-FR" }, { "ja_jp", L"ja-JP" },
        { "ar_sa", L"ar-SA" }, { "hi_in", L"hi-IN" }, { "invariant", L"" },
    };
    for (size_t i = 0; i < sizeof locs / sizeof locs[0]; i++) {
        char c[64];
        snprintf(c, sizeof c, "%s_codes", locs[i].tag);
        group(c, locs[i].name, codes);
        snprintf(c, sizeof c, "%s_numbers", locs[i].tag);
        group(c, locs[i].name, numbers);
        snprintf(c, sizeof c, "%s_money", locs[i].tag);
        group(c, locs[i].name, money);
        snprintf(c, sizeof c, "%s_dates", locs[i].tag);
        group(c, locs[i].name, dates);
        if (locs[i].name[0]) {
            snprintf(c, sizeof c, "%s_names", locs[i].tag);
            group(c, locs[i].name, names);
        }
    }

    /* Buffer rules. */
    one("size_query", L"en-US", LOCALE_SENGLISHLANGUAGENAME, 0);
    one("buffer_exact", L"en-US", LOCALE_SISO639LANGNAME, 3);
    one("buffer_too_small", L"en-US", LOCALE_SENGLISHLANGUAGENAME, 3);
    one("return_number_idigits", L"en-US", LOCALE_IDIGITS | LOCALE_RETURN_NUMBER, 2);
    one("return_number_size_query", L"en-US", LOCALE_IDIGITS | LOCALE_RETURN_NUMBER, 0);
    one("return_number_buffer_1", L"en-US", LOCALE_IDIGITS | LOCALE_RETURN_NUMBER, 1);
    one("return_number_ansi_cp", L"ja-JP", LOCALE_IDEFAULTANSICODEPAGE | LOCALE_RETURN_NUMBER, 2);
    one("return_number_string_type", L"en-US", LOCALE_SDECIMAL | LOCALE_RETURN_NUMBER, 2);
    one("return_number_ilanguage", L"de-DE", LOCALE_ILANGUAGE | LOCALE_RETURN_NUMBER, 2);

    /* Names. */
    one("lowercase_name", L"en-us", LOCALE_SNAME, 64);
    one("underscore_name", L"en_US", LOCALE_SNAME, 64);
    one("neutral_en", L"en", LOCALE_SNAME, 64);
    one("neutral_en_country", L"en", LOCALE_SISO3166CTRYNAME, 64);
    one("neutral_allow_flag", L"en", LOCALE_SNAME | LOCALE_ALLOW_NEUTRAL_NAMES, 64);
    one("script_name_zh_hans", L"zh-Hans", LOCALE_SNAME, 64);
    one("script_name_sr_latn_rs", L"sr-Latn-RS", LOCALE_SENGLISHDISPLAYNAME, 64);
    one("alt_sort_name", L"de-DE_phoneb", LOCALE_SNAME, 64);
    one("bad_locale", L"xx-YY", LOCALE_SNAME, 64);
    one("bad_lctype", L"en-US", 0x7fff, 64);
    one("lctype_zero", L"en-US", 0, 64);
    one("invariant_constant", LOCALE_NAME_INVARIANT, LOCALE_SENGLISHDISPLAYNAME, 64);

    /* Defaults, compared only. */
    {
        wchar_t a[LOCALE_NAME_MAX_LENGTH], b[LOCALE_NAME_MAX_LENGTH], c[LOCALE_NAME_MAX_LENGTH];
        int ra = GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_SNAME, a, LOCALE_NAME_MAX_LENGTH);
        int rb = GetUserDefaultLocaleName(b, LOCALE_NAME_MAX_LENGTH);
        int rc = GetLocaleInfoEx(LOCALE_NAME_SYSTEM_DEFAULT, LOCALE_SNAME, c, LOCALE_NAME_MAX_LENGTH);
        wchar_t d[LOCALE_NAME_MAX_LENGTH];
        int rd = GetSystemDefaultLocaleName(d, LOCALE_NAME_MAX_LENGTH);
        oracle_case("defaults_consistent");
        o_bool("user_ok", ra > 0 && rb > 0);
        o_bool("user_same", ra > 0 && rb > 0 && !wcscmp(a, b));
        o_bool("user_len_matches", ra == rb);
        o_bool("system_ok", rc > 0 && rd > 0);
        o_bool("system_same", rc > 0 && rd > 0 && !wcscmp(c, d));
        oracle_case_end();
    }

    /* LCID forms. */
    {
        wchar_t w[64];
        char a[64];
        int r;
        ORACLE_CALL(r, GetLocaleInfoW(MAKELCID(0x0407, SORT_DEFAULT), LOCALE_SNAME, w, 64));
        oracle_case("w_lcid_sname");
        o_i64("ret", r);
        o_lasterr();
        if (r > 0) o_wstr("value", w);
        oracle_case_end();
        ORACLE_CALL(r, GetLocaleInfoW(MAKELCID(0x0407, SORT_GERMAN_PHONE_BOOK), LOCALE_SNAME, w, 64));
        oracle_case("w_lcid_phonebook_sname");
        o_i64("ret", r);
        o_lasterr();
        if (r > 0) o_wstr("value", w);
        oracle_case_end();
        ORACLE_CALL(r, GetLocaleInfoA(MAKELCID(0x040c, SORT_DEFAULT), LOCALE_SMONTHNAME2 | LOCALE_NOUSEROVERRIDE, a, 64));
        oracle_case("a_lcid_fr_month2");
        o_i64("ret", r);
        o_lasterr();
        if (r > 0) o_bytes("value", a, (size_t)r);
        oracle_case_end();
        ORACLE_CALL(r, GetLocaleInfoA(MAKELCID(0x0409, SORT_DEFAULT), LOCALE_SENGLISHLANGUAGENAME, a, 3));
        oracle_case("a_buffer_too_small");
        o_i64("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, GetLocaleInfoW(0x7fff, LOCALE_SNAME, w, 64));
        oracle_case("w_lcid_bogus");
        o_i64("ret", r);
        o_lasterr();
        oracle_case_end();
    }

    return oracle_end();
}
