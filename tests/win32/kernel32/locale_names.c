/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: locale names and identifiers: ResolveLocaleName,
 * LocaleNameToLCID, LCIDToLocaleName, IsValidLocaleName, IsValidLocale and
 * EnumSystemLocalesEx. Enumeration is checked by shape and by relationships
 * between the counts of the different flag sets, never by raw counts. The
 * user's own locale is only compared, never printed.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winnls/nf-winnls-resolvelocalename
 *       https://learn.microsoft.com/en-us/windows/win32/api/winnls/nf-winnls-localenametolcid
 *       https://learn.microsoft.com/en-us/windows/win32/api/winnls/nf-winnls-lcidtolocalename
 *       https://learn.microsoft.com/en-us/windows/win32/api/winnls/nf-winnls-isvalidlocalename
 *       https://learn.microsoft.com/en-us/windows/win32/api/winnls/nf-winnls-isvalidlocale
 *       https://learn.microsoft.com/en-us/windows/win32/api/winnls/nf-winnls-enumsystemlocalesex
 *       https://learn.microsoft.com/en-us/windows/win32/intl/locale-names
 */
#include "oracle.h"

#ifndef LOCALE_ALLOW_NEUTRAL_NAMES
#define LOCALE_ALLOW_NEUTRAL_NAMES 0x08000000
#endif

static void resolve(const char *name, const wchar_t *in, int size)
{
    wchar_t out[LOCALE_NAME_MAX_LENGTH];
    int r;
    wmemset(out, L'#', LOCALE_NAME_MAX_LENGTH);
    ORACLE_CALL(r, ResolveLocaleName(in, size ? out : NULL, size));
    oracle_case(name);
    o_i64("ret", r);
    o_lasterr();
    if (r > 0 && size) o_wstr("name", out);
    oracle_case_end();
}

static void n2l(const char *name, const wchar_t *in, DWORD flags)
{
    LCID r;
    ORACLE_CALL(r, LocaleNameToLCID(in, flags));
    oracle_case(name);
    o_hex32("lcid", r);
    o_lasterr();
    oracle_case_end();
}

static void l2n(const char *name, LCID lcid, DWORD flags, int size)
{
    wchar_t out[LOCALE_NAME_MAX_LENGTH];
    int r;
    wmemset(out, L'#', LOCALE_NAME_MAX_LENGTH);
    ORACLE_CALL(r, LCIDToLocaleName(lcid, size ? out : NULL, size, flags));
    oracle_case(name);
    o_i64("ret", r);
    o_lasterr();
    if (r > 0 && size) o_wstr("name", out);
    oracle_case_end();
}

static void valid(const char *name, const wchar_t *in)
{
    BOOL r;
    ORACLE_CALL(r, IsValidLocaleName(in));
    oracle_case(name);
    o_bool("ret", r);
    o_lasterr();
    oracle_case_end();
}

struct enum_state {
    int count, stop_after, bad_lparam;
    int has_en_us, has_en, has_invariant, has_phoneb, has_tradnl;
    int with_underscore, all_valid, all_have_lcid_or_custom;
    int with_dash;
};

static BOOL CALLBACK enum_cb(LPWSTR name, DWORD flags, LPARAM lp)
{
    struct enum_state *s = (struct enum_state *)lp;
    (void)flags;
    s->count++;
    if (!wcscmp(name, L"en-US")) s->has_en_us = 1;
    if (!wcscmp(name, L"en")) s->has_en = 1;
    if (!name[0]) s->has_invariant = 1;
    if (!wcscmp(name, L"de-DE_phoneb")) s->has_phoneb = 1;
    if (!wcscmp(name, L"es-ES_tradnl")) s->has_tradnl = 1;
    if (wcschr(name, L'_')) s->with_underscore++;
    if (wcschr(name, L'-')) s->with_dash++;
    if (!IsValidLocaleName(name)) s->all_valid = 0;
    if (s->stop_after && s->count >= s->stop_after) return FALSE;
    return TRUE;
}

static struct enum_state do_enum(DWORD flags, int stop_after, BOOL *ret, DWORD *err)
{
    struct enum_state s;
    memset(&s, 0, sizeof s);
    s.all_valid = 1;
    s.stop_after = stop_after;
    SetLastError(ORACLE_SENTINEL);
    *ret = EnumSystemLocalesEx(enum_cb, flags, (LPARAM)&s, NULL);
    *err = GetLastError();
    return s;
}

int main(void)
{
    oracle_begin("kernel32/locale_names");

    resolve("resolve_en", L"en", LOCALE_NAME_MAX_LENGTH);
    resolve("resolve_de", L"de", LOCALE_NAME_MAX_LENGTH);
    resolve("resolve_fr", L"fr", LOCALE_NAME_MAX_LENGTH);
    resolve("resolve_zh", L"zh", LOCALE_NAME_MAX_LENGTH);
    resolve("resolve_zh_hant", L"zh-Hant", LOCALE_NAME_MAX_LENGTH);
    resolve("resolve_zh_hans", L"zh-Hans", LOCALE_NAME_MAX_LENGTH);
    resolve("resolve_sr", L"sr", LOCALE_NAME_MAX_LENGTH);
    resolve("resolve_pt", L"pt", LOCALE_NAME_MAX_LENGTH);
    resolve("resolve_en_us_lower", L"en-us", LOCALE_NAME_MAX_LENGTH);
    resolve("resolve_en_gb", L"en-GB", LOCALE_NAME_MAX_LENGTH);
    resolve("resolve_en_unknown_region", L"en-ZZ", LOCALE_NAME_MAX_LENGTH);
    resolve("resolve_unknown_language", L"xx", LOCALE_NAME_MAX_LENGTH);
    resolve("resolve_phonebook", L"de-DE_phoneb", LOCALE_NAME_MAX_LENGTH);
    resolve("resolve_invariant", L"", LOCALE_NAME_MAX_LENGTH);
    resolve("resolve_size_query", L"en", 0);
    resolve("resolve_buffer_too_small", L"en", 3);
    resolve("resolve_buffer_exact", L"en", 6);
    {
        wchar_t a[LOCALE_NAME_MAX_LENGTH], b[LOCALE_NAME_MAX_LENGTH];
        int ra = ResolveLocaleName(NULL, a, LOCALE_NAME_MAX_LENGTH);
        int rb = GetUserDefaultLocaleName(b, LOCALE_NAME_MAX_LENGTH);
        oracle_case("resolve_null_is_user_default");
        o_bool("ok", ra > 0 && rb > 0);
        o_bool("same", ra > 0 && rb > 0 && !wcscmp(a, b));
        oracle_case_end();
    }

    n2l("n2l_en_us", L"en-US", 0);
    n2l("n2l_en_us_lower", L"en-us", 0);
    n2l("n2l_de_phoneb", L"de-DE_phoneb", 0);
    n2l("n2l_es_tradnl", L"es-ES_tradnl", 0);
    n2l("n2l_ja_radstr", L"ja-JP_radstr", 0);
    n2l("n2l_neutral_en", L"en", 0);
    n2l("n2l_neutral_en_allow", L"en", LOCALE_ALLOW_NEUTRAL_NAMES);
    n2l("n2l_neutral_zh_hans_allow", L"zh-Hans", LOCALE_ALLOW_NEUTRAL_NAMES);
    n2l("n2l_invariant", L"", 0);
    n2l("n2l_unknown", L"xx-YY", 0);
    n2l("n2l_bad_flag", L"en-US", 1);
    n2l("n2l_sr_latn_rs", L"sr-Latn-RS", 0);
    n2l("n2l_en_001", L"en-001", 0);
    {
        LCID a = LocaleNameToLCID(LOCALE_NAME_USER_DEFAULT, 0);
        LCID b = GetUserDefaultLCID();
        LCID c = LocaleNameToLCID(LOCALE_NAME_SYSTEM_DEFAULT, 0);
        LCID d = GetSystemDefaultLCID();
        oracle_case("n2l_defaults");
        o_bool("user_matches", a == b);
        o_bool("system_matches", c == d);
        oracle_case_end();
    }

    l2n("l2n_0409", 0x0409, 0, LOCALE_NAME_MAX_LENGTH);
    l2n("l2n_0407", 0x0407, 0, LOCALE_NAME_MAX_LENGTH);
    l2n("l2n_10407", 0x10407, 0, LOCALE_NAME_MAX_LENGTH);
    l2n("l2n_040a", 0x040a, 0, LOCALE_NAME_MAX_LENGTH);
    l2n("l2n_0c0a", 0x0c0a, 0, LOCALE_NAME_MAX_LENGTH);
    l2n("l2n_0804", 0x0804, 0, LOCALE_NAME_MAX_LENGTH);
    l2n("l2n_0404", 0x0404, 0, LOCALE_NAME_MAX_LENGTH);
    l2n("l2n_241a", 0x241a, 0, LOCALE_NAME_MAX_LENGTH);
    l2n("l2n_neutral_0009", 0x0009, 0, LOCALE_NAME_MAX_LENGTH);
    l2n("l2n_neutral_0009_allow", 0x0009, LOCALE_ALLOW_NEUTRAL_NAMES, LOCALE_NAME_MAX_LENGTH);
    l2n("l2n_invariant", LOCALE_INVARIANT, 0, LOCALE_NAME_MAX_LENGTH);
    l2n("l2n_custom_unspecified", 0x1000, 0, LOCALE_NAME_MAX_LENGTH);
    l2n("l2n_bogus", 0x7fff, 0, LOCALE_NAME_MAX_LENGTH);
    l2n("l2n_size_query", 0x0409, 0, 0);
    l2n("l2n_buffer_too_small", 0x0409, 0, 3);
    l2n("l2n_buffer_exact", 0x0409, 0, 6);
    l2n("l2n_bad_flag", 0x0409, 1, LOCALE_NAME_MAX_LENGTH);
    {
        wchar_t a[LOCALE_NAME_MAX_LENGTH], b[LOCALE_NAME_MAX_LENGTH], c[LOCALE_NAME_MAX_LENGTH];
        int ra = LCIDToLocaleName(LOCALE_USER_DEFAULT, a, LOCALE_NAME_MAX_LENGTH, 0);
        int rb = GetUserDefaultLocaleName(b, LOCALE_NAME_MAX_LENGTH);
        int rc = LCIDToLocaleName(LOCALE_SYSTEM_DEFAULT, c, LOCALE_NAME_MAX_LENGTH, 0);
        wchar_t d[LOCALE_NAME_MAX_LENGTH];
        int rd = GetSystemDefaultLocaleName(d, LOCALE_NAME_MAX_LENGTH);
        oracle_case("l2n_defaults");
        o_bool("user_same", ra > 0 && rb > 0 && !wcscmp(a, b));
        o_bool("system_same", rc > 0 && rd > 0 && !wcscmp(c, d));
        oracle_case_end();
    }

    valid("valid_en_us", L"en-US");
    valid("valid_en_us_lower", L"en-us");
    valid("valid_neutral_en", L"en");
    valid("valid_invariant", L"");
    valid("valid_phonebook", L"de-DE_phoneb");
    valid("valid_underscore", L"en_US");
    valid("valid_unknown", L"xx-YY");
    valid("valid_trailing_dash", L"en-");
    {
        BOOL r;
        ORACLE_CALL(r, IsValidLocale(0x0409, LCID_SUPPORTED));
        oracle_case("isvalid_0409_supported");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, IsValidLocale(0x0409, LCID_INSTALLED));
        oracle_case("isvalid_0409_installed");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, IsValidLocale(0x7fff, LCID_SUPPORTED));
        oracle_case("isvalid_bogus");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, IsValidLocale(0x10407, LCID_SUPPORTED));
        oracle_case("isvalid_phonebook_lcid");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
    }

    {
        BOOL r;
        DWORD e;
        struct enum_state all = do_enum(LOCALE_ALL, 0, &r, &e);
        oracle_case("enum_all");
        o_bool("ret", r);
        o_bool("nonzero", all.count > 0);
        o_bool("has_en_us", all.has_en_us);
        o_bool("has_en", all.has_en);
        o_bool("has_invariant", all.has_invariant);
        o_bool("has_phoneb", all.has_phoneb);
        o_bool("all_valid", all.all_valid);
        oracle_case_end();

        struct enum_state win = do_enum(LOCALE_WINDOWS, 0, &r, &e);
        oracle_case("enum_windows");
        o_bool("ret", r);
        o_bool("has_en_us", win.has_en_us);
        o_bool("has_phoneb", win.has_phoneb);
        o_bool("le_all", win.count <= all.count);
        o_bool("all_valid", win.all_valid);
        oracle_case_end();

        struct enum_state alt = do_enum(LOCALE_ALTERNATE_SORTS, 0, &r, &e);
        oracle_case("enum_alternate_sorts");
        o_bool("ret", r);
        o_bool("nonzero", alt.count > 0);
        o_bool("has_phoneb", alt.has_phoneb);
        o_bool("has_tradnl", alt.has_tradnl);
        o_bool("has_en_us", alt.has_en_us);
        o_bool("all_have_underscore", alt.with_underscore == alt.count);
        oracle_case_end();

        struct enum_state neu = do_enum(LOCALE_NEUTRALDATA, 0, &r, &e);
        struct enum_state spec = do_enum(LOCALE_SPECIFICDATA, 0, &r, &e);
        oracle_case("enum_neutral_specific");
        o_bool("neutral_has_en", neu.has_en);
        o_bool("neutral_has_en_us", neu.has_en_us);
        o_bool("specific_has_en_us", spec.has_en_us);
        o_bool("specific_has_en", spec.has_en);
        o_bool("specific_all_have_dash", spec.with_dash == spec.count);
        o_bool("neutral_lt_specific", neu.count < spec.count);
        o_bool("sum_le_all", neu.count + spec.count <= all.count);
        o_bool("neutral_specific_no_alt", !neu.has_phoneb && !spec.has_phoneb);
        oracle_case_end();

        struct enum_state both = do_enum(LOCALE_WINDOWS | LOCALE_ALTERNATE_SORTS, 0, &r, &e);
        oracle_case("enum_windows_plus_alternate");
        o_bool("ret", r);
        o_bool("sum", both.count == win.count + alt.count);
        oracle_case_end();

        struct enum_state stop = do_enum(LOCALE_ALL, 3, &r, &e);
        oracle_case("enum_callback_stops");
        o_bool("ret", r);
        o_i64("count", stop.count);
        oracle_last_error = e;
        o_lasterr();
        oracle_case_end();

        struct enum_state bad = do_enum(0x80, 0, &r, &e);
        oracle_case("enum_bad_flag");
        o_bool("ret", r);
        o_i64("count", bad.count);
        oracle_last_error = e;
        o_lasterr();
        oracle_case_end();

        {
            struct enum_state s;
            memset(&s, 0, sizeof s);
            SetLastError(ORACLE_SENTINEL);
            r = EnumSystemLocalesEx(enum_cb, LOCALE_ALL, (LPARAM)&s, (void *)1);
            e = GetLastError();
            oracle_case("enum_reserved_not_null");
            o_bool("ret", r);
            o_i64("count", s.count);
            oracle_last_error = e;
            o_lasterr();
            oracle_case_end();
        }
        {
            SetLastError(ORACLE_SENTINEL);
            r = EnumSystemLocalesEx(NULL, LOCALE_ALL, 0, NULL);
            e = GetLastError();
            oracle_case("enum_null_callback");
            o_bool("ret", r);
            oracle_last_error = e;
            o_lasterr();
            oracle_case_end();
        }
    }

    return oracle_end();
}
