/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: GetDateFormatEx and GetTimeFormatEx with named locales (the
 * invariant locale is covered by kernel32/time_conv): the locale's default
 * short, long, year-month and month-day formats, genitive month names, the
 * time marker position and the TIME_* flags, DATE_USE_ALT_CALENDAR for the
 * Japanese and Thai calendars, DATE_LTRREADING / DATE_RTLREADING marks, and
 * the A/W LCID forms. Every call uses LOCALE_NOUSEROVERRIDE or a locale
 * that is not the user's, with a fixed SYSTEMTIME.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/datetimeapi/nf-datetimeapi-getdateformatex
 *       https://learn.microsoft.com/en-us/windows/win32/api/datetimeapi/nf-datetimeapi-gettimeformatex
 *       https://learn.microsoft.com/en-us/windows/win32/intl/day--month--year--and-era-format-pictures
 *       https://learn.microsoft.com/en-us/windows/win32/intl/hour--minute--and-second-format-pictures
 *       https://learn.microsoft.com/en-us/windows/win32/intl/calendar-identifiers
 */
#include "oracle.h"

#define NO LOCALE_NOUSEROVERRIDE

static SYSTEMTIME when = { 2024, 3, 5, 14, 15, 4, 5, 6 }; /* Thu 2024-03-14 15:04:05.006 */
static SYSTEMTIME morning = { 2019, 5, 3, 1, 9, 7, 8, 0 }; /* Wed 2019-05-01 09:07:08 */

static void date(const char *name, const wchar_t *loc, DWORD flags, const SYSTEMTIME *st, const wchar_t *fmt)
{
    wchar_t out[128];
    int r;
    ORACLE_CALL(r, GetDateFormatEx(loc, flags, st, fmt, out, 128, NULL));
    oracle_case(name);
    o_i64("ret", r);
    o_lasterr();
    if (r > 0) o_wstr("out", out);
    oracle_case_end();
}

static void tm(const char *name, const wchar_t *loc, DWORD flags, const SYSTEMTIME *st, const wchar_t *fmt)
{
    wchar_t out[128];
    int r;
    ORACLE_CALL(r, GetTimeFormatEx(loc, flags, st, fmt, out, 128));
    oracle_case(name);
    o_i64("ret", r);
    o_lasterr();
    if (r > 0) o_wstr("out", out);
    oracle_case_end();
}

int main(void)
{
    oracle_begin("kernel32/datetime_named");

    date("en_short", L"en-US", NO | DATE_SHORTDATE, &when, NULL);
    date("en_long", L"en-US", NO | DATE_LONGDATE, &when, NULL);
    date("en_yearmonth", L"en-US", NO | DATE_YEARMONTH, &when, NULL);
    date("en_monthday", L"en-US", NO | DATE_MONTHDAY, &when, NULL);
    date("en_gb_short", L"en-GB", NO | DATE_SHORTDATE, &when, NULL);
    date("en_gb_long", L"en-GB", NO | DATE_LONGDATE, &when, NULL);
    date("de_short", L"de-DE", NO | DATE_SHORTDATE, &when, NULL);
    date("de_long", L"de-DE", NO | DATE_LONGDATE, &when, NULL);
    date("fr_long", L"fr-FR", NO | DATE_LONGDATE, &when, NULL);
    date("es_long", L"es-ES", NO | DATE_LONGDATE, &when, NULL);
    date("ru_long_genitive", L"ru-RU", NO | DATE_LONGDATE, &when, NULL);
    date("ru_month_alone", L"ru-RU", NO, &when, L"MMMM");
    date("ru_day_month", L"ru-RU", NO, &when, L"d MMMM");
    date("pl_day_month", L"pl-PL", NO, &when, L"d MMMM yyyy");
    date("ja_short", L"ja-JP", NO | DATE_SHORTDATE, &when, NULL);
    date("ja_long", L"ja-JP", NO | DATE_LONGDATE, &when, NULL);
    date("ja_alt_calendar_long", L"ja-JP", NO | DATE_LONGDATE | DATE_USE_ALT_CALENDAR, &when, NULL);
    date("ja_alt_calendar_heisei", L"ja-JP", NO | DATE_USE_ALT_CALENDAR, &morning, L"gg y");
    date("ko_long", L"ko-KR", NO | DATE_LONGDATE, &when, NULL);
    date("zh_cn_long", L"zh-CN", NO | DATE_LONGDATE, &when, NULL);
    date("ar_sa_short", L"ar-SA", NO | DATE_SHORTDATE, &when, NULL);
    date("ar_sa_gregorian_pattern", L"ar-SA", NO, &when, L"yyyy-MM-dd");
    date("th_short", L"th-TH", NO | DATE_SHORTDATE, &when, NULL);
    date("he_rtl_reading", L"he-IL", NO | DATE_SHORTDATE | DATE_RTLREADING, &when, NULL);
    date("en_ltr_reading", L"en-US", NO | DATE_SHORTDATE | DATE_LTRREADING, &when, NULL);
    date("ltr_and_rtl_rejected", L"en-US", NO | DATE_LTRREADING | DATE_RTLREADING, &when, NULL);
    date("short_and_long_rejected", L"en-US", NO | DATE_SHORTDATE | DATE_LONGDATE, &when, NULL);
    date("de_custom_names", L"de-DE", NO, &when, L"dddd, d. MMMM yyyy (ddd, MMM)");

    tm("en_default", L"en-US", NO, &when, NULL);
    tm("en_morning", L"en-US", NO, &morning, NULL);
    tm("en_no_seconds", L"en-US", NO | TIME_NOSECONDS, &when, NULL);
    tm("en_no_minutes", L"en-US", NO | TIME_NOMINUTESORSECONDS, &when, NULL);
    tm("en_no_marker", L"en-US", NO | TIME_NOTIMEMARKER, &when, NULL);
    tm("en_force24", L"en-US", NO | TIME_FORCE24HOURFORMAT, &when, NULL);
    tm("de_default", L"de-DE", NO, &when, NULL);
    tm("fr_default", L"fr-FR", NO, &when, NULL);
    tm("ja_default", L"ja-JP", NO, &when, NULL);
    tm("ko_default", L"ko-KR", NO, &when, NULL);
    tm("ko_morning", L"ko-KR", NO, &morning, NULL);
    tm("zh_cn_default", L"zh-CN", NO, &when, NULL);
    tm("ar_sa_default", L"ar-SA", NO, &when, NULL);
    tm("hi_default", L"hi-IN", NO, &when, NULL);
    tm("en_gb_tt", L"en-GB", NO, &when, L"h:mm tt");
    tm("de_tt_empty_marker", L"de-DE", NO, &when, L"h:mm tt|");
    tm("ja_t_single", L"ja-JP", NO, &morning, L"t h");
    tm("en_seconds_kept_literal", L"en-US", NO | TIME_NOSECONDS, &when, L"hh 'ss' mm:ss");

    {
        char a[64];
        int r;
        ORACLE_CALL(r, GetDateFormatA(MAKELCID(0x0407, SORT_DEFAULT), NO, &when, "dddd d. MMMM", a, 64));
        oracle_case("a_lcid_de");
        o_i64("ret", r);
        o_lasterr();
        if (r > 0) o_bytes("out", a, (size_t)r);
        oracle_case_end();
        wchar_t w[64];
        ORACLE_CALL(r, GetTimeFormatW(MAKELCID(0x0411, SORT_DEFAULT), NO, &when, NULL, w, 64));
        oracle_case("w_lcid_ja");
        o_i64("ret", r);
        o_lasterr();
        if (r > 0) o_wstr("out", w);
        oracle_case_end();
    }

    return oracle_end();
}
