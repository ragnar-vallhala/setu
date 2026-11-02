/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: time conversions with fixed inputs only: FileTimeToSystemTime,
 * SystemTimeToFileTime (invalid dates, limits, ignored day of week),
 * CompareFileTime, FileTimeToDosDateTime / DosDateTimeToFileTime,
 * SystemTimeToTzSpecificLocalTime / TzSpecificLocalTimeToSystemTime with an
 * explicit TIME_ZONE_INFORMATION, a FileTimeToLocalFileTime round trip, and
 * GetDateFormatEx / GetTimeFormatEx with the invariant locale.
 *
 * The machine's own time zone is never printed: the local-time APIs are only
 * checked for round trips.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/timezoneapi/nf-timezoneapi-filetimetosystemtime
 *       https://learn.microsoft.com/en-us/windows/win32/api/timezoneapi/nf-timezoneapi-systemtimetofiletime
 *       https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-comparefiletime
 *       https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-filetimetodosdatetime
 *       https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-dosdatetimetofiletime
 *       https://learn.microsoft.com/en-us/windows/win32/api/timezoneapi/nf-timezoneapi-systemtimetotzspecificlocaltime
 *       https://learn.microsoft.com/en-us/windows/win32/api/timezoneapi/nf-timezoneapi-tzspecificlocaltimetosystemtime
 *       https://learn.microsoft.com/en-us/windows/win32/api/timezoneapi/ns-timezoneapi-time_zone_information
 *       https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-filetimetolocalfiletime
 *       https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-localfiletimetofiletime
 *       https://learn.microsoft.com/en-us/windows/win32/api/datetimeapi/nf-datetimeapi-getdateformatex
 *       https://learn.microsoft.com/en-us/windows/win32/api/datetimeapi/nf-datetimeapi-gettimeformatex
 *       https://learn.microsoft.com/en-us/windows/win32/api/minwinbase/ns-minwinbase-systemtime
 */
#include "oracle.h"

static FILETIME ft_of(unsigned long long v)
{
    FILETIME f;
    f.dwLowDateTime = (DWORD)v;
    f.dwHighDateTime = (DWORD)(v >> 32);
    return f;
}
static unsigned long long u64_of(FILETIME f) { return ((unsigned long long)f.dwHighDateTime << 32) | f.dwLowDateTime; }

static void st_fields(const SYSTEMTIME *st)
{
    char t[64];
    snprintf(t, sizeof t, "%04u-%02u-%02u %02u:%02u:%02u.%03u dow%u", st->wYear, st->wMonth, st->wDay, st->wHour,
             st->wMinute, st->wSecond, st->wMilliseconds, st->wDayOfWeek);
    o_str("st", t);
}

static void ft2st_case(const char *name, unsigned long long v)
{
    FILETIME f = ft_of(v);
    SYSTEMTIME st;
    BOOL ok;
    memset(&st, 0xAA, sizeof st);
    ORACLE_CALL(ok, FileTimeToSystemTime(&f, &st));
    oracle_case(name);
    o_bool("ret", ok);
    o_lasterr();
    if (ok) st_fields(&st);
    oracle_case_end();
}

static void st2ft_case(const char *name, WORD y, WORD mo, WORD dow, WORD d, WORD h, WORD mi, WORD s, WORD ms)
{
    SYSTEMTIME st = {y, mo, dow, d, h, mi, s, ms};
    FILETIME f = ft_of(0x1111111111111111ULL);
    BOOL ok;
    ORACLE_CALL(ok, SystemTimeToFileTime(&st, &f));
    oracle_case(name);
    o_bool("ret", ok);
    o_lasterr();
    o_u64("ft", u64_of(f));
    oracle_case_end();
}

static void dos_case(const char *name, unsigned long long v)
{
    FILETIME f = ft_of(v);
    WORD date = 0xAAAA, time = 0xAAAA;
    BOOL ok;
    ORACLE_CALL(ok, FileTimeToDosDateTime(&f, &date, &time));
    oracle_case(name);
    o_bool("ret", ok);
    o_lasterr();
    o_hex32("date", date);
    o_hex32("time", time);
    oracle_case_end();
}

static void dos2ft_case(const char *name, WORD date, WORD time)
{
    FILETIME f = ft_of(0x1111111111111111ULL);
    BOOL ok;
    ORACLE_CALL(ok, DosDateTimeToFileTime(date, time, &f));
    oracle_case(name);
    o_bool("ret", ok);
    o_lasterr();
    o_u64("ft", u64_of(f));
    oracle_case_end();
}

/* A fixed zone: UTC-5 standard, UTC-4 daylight from 2nd Sunday of March 02:00
 * to 1st Sunday of November 02:00 (US Eastern rules). */
static TIME_ZONE_INFORMATION eastern(void)
{
    TIME_ZONE_INFORMATION tz;
    memset(&tz, 0, sizeof tz);
    tz.Bias = 300;
    tz.StandardBias = 0;
    tz.DaylightBias = -60;
    tz.StandardDate.wMonth = 11;
    tz.StandardDate.wDayOfWeek = 0;
    tz.StandardDate.wDay = 1;
    tz.StandardDate.wHour = 2;
    tz.DaylightDate.wMonth = 3;
    tz.DaylightDate.wDayOfWeek = 0;
    tz.DaylightDate.wDay = 2;
    tz.DaylightDate.wHour = 2;
    return tz;
}

static void tz_case(const char *name, const TIME_ZONE_INFORMATION *tz, int to_local, WORD y, WORD mo, WORD d, WORD h,
                    WORD mi)
{
    SYSTEMTIME in = {y, mo, 0, d, h, mi, 0, 0}, out;
    BOOL ok;
    memset(&out, 0, sizeof out);
    if (to_local) ORACLE_CALL(ok, SystemTimeToTzSpecificLocalTime(tz, &in, &out));
    else ORACLE_CALL(ok, TzSpecificLocalTimeToSystemTime(tz, &in, &out));
    oracle_case(name);
    o_bool("ret", ok);
    o_lasterr();
    if (ok) st_fields(&out);
    oracle_case_end();
}

static void datefmt_case(const char *name, DWORD flags, const wchar_t *fmt, WORD y, WORD mo, WORD dow, WORD d)
{
    SYSTEMTIME st = {y, mo, dow, d, 13, 5, 9, 7};
    wchar_t buf[128];
    int r;
    ORACLE_CALL(r, GetDateFormatEx(LOCALE_NAME_INVARIANT, flags, &st, fmt, buf, 128, NULL));
    oracle_case(name);
    o_i64("ret", r);
    o_lasterr();
    if (r > 0) o_wstr("out", buf);
    oracle_case_end();
}

static void timefmt_case(const char *name, DWORD flags, const wchar_t *fmt, WORD h, WORD mi, WORD s)
{
    SYSTEMTIME st = {2024, 2, 4, 29, h, mi, s, 0};
    wchar_t buf[128];
    int r;
    ORACLE_CALL(r, GetTimeFormatEx(LOCALE_NAME_INVARIANT, flags, &st, fmt, buf, 128));
    oracle_case(name);
    o_i64("ret", r);
    o_lasterr();
    if (r > 0) o_wstr("out", buf);
    oracle_case_end();
}

#define EPOCH_1970 116444736000000000ULL

int main(void)
{
    oracle_begin("kernel32/time_conv");

    /* ---- FileTimeToSystemTime ---- */
    ft2st_case("ft_zero", 0);
    ft2st_case("ft_one_tick", 1);
    ft2st_case("ft_unix_epoch", EPOCH_1970);
    ft2st_case("ft_y2k_plus_ms", 125911584000000000ULL + 1234567ULL);
    ft2st_case("ft_2024_03_01_0800", 133537536000000000ULL);
    ft2st_case("ft_max_int64", 0x7FFFFFFFFFFFFFFFULL);
    ft2st_case("ft_high_bit", 0x8000000000000000ULL);
    ft2st_case("ft_all_ones", 0xFFFFFFFFFFFFFFFFULL);
    ft2st_case("ft_year_30827_end", 0x7FFF35F4F06C7FFFULL);
    ft2st_case("ft_year_30828", 0x7FFF35F4F06C8000ULL);

    /* ---- SystemTimeToFileTime ---- */
    st2ft_case("st_epoch_1601", 1601, 1, 0, 1, 0, 0, 0, 0);
    st2ft_case("st_1600", 1600, 12, 0, 31, 23, 59, 59, 999);
    st2ft_case("st_unix_epoch", 1970, 1, 4, 1, 0, 0, 0, 0);
    st2ft_case("st_dow_ignored", 1970, 1, 6, 1, 0, 0, 0, 0);
    st2ft_case("st_dow_out_of_range", 1970, 1, 9, 1, 0, 0, 0, 0);
    st2ft_case("st_leap_day_2024", 2024, 2, 4, 29, 0, 0, 0, 0);
    st2ft_case("st_feb_29_non_leap", 2023, 2, 0, 29, 0, 0, 0, 0);
    st2ft_case("st_feb_29_1900", 1900, 2, 0, 29, 0, 0, 0, 0);
    st2ft_case("st_feb_29_2000", 2000, 2, 0, 29, 0, 0, 0, 0);
    st2ft_case("st_feb_30", 2024, 2, 0, 30, 0, 0, 0, 0);
    st2ft_case("st_apr_31", 2024, 4, 0, 31, 0, 0, 0, 0);
    st2ft_case("st_month_0", 2024, 0, 0, 1, 0, 0, 0, 0);
    st2ft_case("st_month_13", 2024, 13, 0, 1, 0, 0, 0, 0);
    st2ft_case("st_day_0", 2024, 1, 0, 0, 0, 0, 0, 0);
    st2ft_case("st_hour_24", 2024, 1, 0, 1, 24, 0, 0, 0);
    st2ft_case("st_minute_60", 2024, 1, 0, 1, 0, 60, 0, 0);
    st2ft_case("st_second_60", 2024, 1, 0, 1, 0, 0, 60, 0);
    st2ft_case("st_ms_999", 2024, 1, 0, 1, 0, 0, 0, 999);
    st2ft_case("st_ms_1000", 2024, 1, 0, 1, 0, 0, 0, 1000);
    st2ft_case("st_year_30827", 30827, 12, 0, 31, 23, 59, 59, 999);
    st2ft_case("st_year_30828", 30828, 1, 0, 1, 0, 0, 0, 0);
    st2ft_case("st_year_60056", 60056, 5, 0, 28, 5, 36, 10, 955);
    st2ft_case("st_year_0", 0, 1, 0, 1, 0, 0, 0, 0);

    /* ---- CompareFileTime ---- */
    {
        FILETIME a = ft_of(5), b = ft_of(6), c = ft_of(0x100000000ULL), d = ft_of(0x8000000000000000ULL);
        oracle_case("compare_file_time");
        o_i64("lt", CompareFileTime(&a, &b));
        o_i64("gt", CompareFileTime(&b, &a));
        o_i64("eq", CompareFileTime(&a, &a));
        o_i64("high_part", CompareFileTime(&c, &b));
        o_i64("high_bit_unsigned", CompareFileTime(&d, &c));
        oracle_case_end();
    }

    /* ---- DOS date/time ---- */
    dos_case("dos_1980", 119600064000000000ULL);
    dos_case("dos_odd_second", 119600064000000000ULL + 10000000ULL);
    dos_case("dos_2024_03_01_130509", 133537536000000000ULL + 13ULL * 36000000000ULL + 5ULL * 600000000ULL + 9ULL * 10000000ULL);
    dos_case("dos_before_1980", EPOCH_1970);
    dos_case("dos_2107_end", 159992927980000000ULL);
    dos_case("dos_2108", 159992928000000000ULL + 864000000000ULL * 2);
    dos2ft_case("dos2ft_1980", 0x0021, 0x0000);
    dos2ft_case("dos2ft_2024_02_29", (44 << 9) | (2 << 5) | 29, (13 << 11) | (5 << 5) | 4);
    dos2ft_case("dos2ft_month_0", (44 << 9) | (0 << 5) | 1, 0);
    dos2ft_case("dos2ft_month_13", (44 << 9) | (13 << 5) | 1, 0);
    dos2ft_case("dos2ft_day_0", (44 << 9) | (1 << 5) | 0, 0);
    dos2ft_case("dos2ft_feb_31", (44 << 9) | (2 << 5) | 31, 0);
    dos2ft_case("dos2ft_hour_24", 0x0021, 24 << 11);
    dos2ft_case("dos2ft_minute_60", 0x0021, 60 << 5);
    dos2ft_case("dos2ft_seconds_31", 0x0021, 31);
    dos2ft_case("dos2ft_all_ones", 0xFFFF, 0xFFFF);

    /* ---- explicit time zone ---- */
    {
        TIME_ZONE_INFORMATION tz = eastern();
        tz_case("tz_winter_to_local", &tz, 1, 2024, 1, 15, 12, 0);
        tz_case("tz_summer_to_local", &tz, 1, 2024, 7, 15, 12, 0);
        tz_case("tz_dst_start_utc_0659", &tz, 1, 2024, 3, 10, 6, 59);
        tz_case("tz_dst_start_utc_0700", &tz, 1, 2024, 3, 10, 7, 0);
        tz_case("tz_dst_end_utc_0559", &tz, 1, 2024, 11, 3, 5, 59);
        tz_case("tz_dst_end_utc_0600", &tz, 1, 2024, 11, 3, 6, 0);
        tz_case("tz_new_year_crossing", &tz, 1, 2024, 1, 1, 2, 0);
        tz_case("tz_winter_to_utc", &tz, 0, 2024, 1, 15, 7, 0);
        tz_case("tz_summer_to_utc", &tz, 0, 2024, 7, 15, 8, 0);
        tz_case("tz_gap_local_0230", &tz, 0, 2024, 3, 10, 2, 30);
        tz_case("tz_overlap_local_0130", &tz, 0, 2024, 11, 3, 1, 30);
        tz_case("tz_invalid_input_month_13", &tz, 1, 2024, 13, 1, 0, 0);
        tz_case("tz_invalid_input_feb_30", &tz, 0, 2024, 2, 30, 0, 0);

        /* Absolute-date rule (wYear != 0). */
        TIME_ZONE_INFORMATION abs = tz;
        abs.DaylightDate.wYear = 2024;
        abs.DaylightDate.wMonth = 4;
        abs.DaylightDate.wDay = 1;
        abs.StandardDate.wYear = 2024;
        abs.StandardDate.wMonth = 10;
        abs.StandardDate.wDay = 1;
        tz_case("tz_absolute_rule_in_dst", &abs, 1, 2024, 6, 1, 12, 0);
        tz_case("tz_absolute_rule_before_dst", &abs, 1, 2024, 3, 20, 12, 0);

        /* No DST at all: StandardDate.wMonth == 0. */
        TIME_ZONE_INFORMATION nodst;
        memset(&nodst, 0, sizeof nodst);
        nodst.Bias = -330;
        tz_case("tz_no_dst_plus_0530", &nodst, 1, 2024, 7, 15, 20, 0);
        tz_case("tz_no_dst_plus_0530_to_utc", &nodst, 0, 2024, 7, 16, 1, 30);

        /* Southern hemisphere: DST from October to April. */
        TIME_ZONE_INFORMATION south;
        memset(&south, 0, sizeof south);
        south.Bias = -600;
        south.DaylightBias = -60;
        south.StandardDate.wMonth = 4;
        south.StandardDate.wDay = 1;
        south.StandardDate.wHour = 3;
        south.DaylightDate.wMonth = 10;
        south.DaylightDate.wDay = 1;
        south.DaylightDate.wHour = 2;
        tz_case("tz_south_january", &south, 1, 2024, 1, 15, 0, 0);
        tz_case("tz_south_july", &south, 1, 2024, 7, 15, 0, 0);
        /* wDay 5 means "last". */
        TIME_ZONE_INFORMATION last = tz;
        last.DaylightDate.wDay = 5;
        last.DaylightDate.wMonth = 3;
        tz_case("tz_last_sunday_march_before", &last, 1, 2024, 3, 31, 6, 0);
        tz_case("tz_last_sunday_march_after", &last, 1, 2024, 3, 31, 7, 0);
        /* Invalid rule: month 13. */
        TIME_ZONE_INFORMATION bad = tz;
        bad.DaylightDate.wMonth = 13;
        tz_case("tz_bad_rule_month_13", &bad, 1, 2024, 7, 15, 12, 0);
    }

    /* ---- local file time round trip (zone-independent) ---- */
    {
        FILETIME utc = ft_of(133537536000000000ULL), loc, back;
        BOOL ok1, ok2;
        ok1 = FileTimeToLocalFileTime(&utc, &loc);
        ok2 = LocalFileTimeToFileTime(&loc, &back);
        oracle_case("local_round_trip");
        o_bool("to_local", ok1);
        o_bool("to_utc", ok2);
        o_bool("same", u64_of(back) == u64_of(utc));
        long long diff = (long long)(u64_of(loc) - u64_of(utc));
        o_bool("whole_quarter_hours", diff % 9000000000LL == 0);
        oracle_case_end();
        FILETIME big = ft_of(0xFFFFFFFFFFFFFFFFULL);
        ORACLE_CALL(ok1, FileTimeToLocalFileTime(&big, &loc));
        oracle_case("local_all_ones");
        o_bool("ret", ok1);
        oracle_case_end();
    }

    /* ---- invariant date/time formatting ---- */
    datefmt_case("date_default", 0, NULL, 2024, 2, 4, 29);
    datefmt_case("date_long", DATE_LONGDATE, NULL, 2024, 2, 4, 29);
    datefmt_case("date_custom", 0, L"yyyy-MM-dd ddd dddd MMM MMMM", 2024, 2, 4, 29);
    datefmt_case("date_short_forms", 0, L"y yy d M dd MM", 2009, 3, 0, 5);
    datefmt_case("date_quoted", 0, L"'Year:' yyyy 'it''s'", 2024, 2, 4, 29);
    datefmt_case("date_era", 0, L"gg yyyy", 2024, 2, 4, 29);
    datefmt_case("date_wrong_dow_ignored", 0, L"dddd", 2024, 2, 0, 29);
    datefmt_case("date_invalid_day", 0, L"yyyy-MM-dd", 2024, 2, 4, 30);
    datefmt_case("date_year_1600", 0, L"yyyy-MM-dd", 1600, 1, 0, 1);
    datefmt_case("date_year_10000", 0, L"yyyy", 10000, 1, 0, 1);
    datefmt_case("date_bad_flags", DATE_LONGDATE | DATE_SHORTDATE, NULL, 2024, 2, 4, 29);
    datefmt_case("date_format_with_flag", DATE_LONGDATE, L"yyyy", 2024, 2, 4, 29);
    timefmt_case("time_default", 0, NULL, 13, 5, 9);
    timefmt_case("time_custom_24", 0, L"HH:mm:ss", 7, 5, 9);
    timefmt_case("time_custom_12", 0, L"h:m:s tt t", 13, 5, 9);
    timefmt_case("time_midnight_12h", 0, L"hh tt", 0, 0, 0);
    timefmt_case("time_no_seconds", TIME_NOSECONDS, NULL, 13, 5, 9);
    timefmt_case("time_no_minutes", TIME_NOMINUTESORSECONDS, NULL, 13, 5, 9);
    timefmt_case("time_force_24h", TIME_FORCE24HOURFORMAT, NULL, 13, 5, 9);
    timefmt_case("time_no_marker", TIME_NOTIMEMARKER, NULL, 13, 5, 9);
    timefmt_case("time_hour_24_invalid", 0, L"HH:mm", 24, 0, 0);
    timefmt_case("time_quoted", 0, L"'at' HH 'o''clock'", 9, 0, 0);
    {
        int r;
        SYSTEMTIME st = {2024, 2, 4, 29, 13, 5, 9, 7};
        wchar_t small[4];
        ORACLE_CALL(r, GetDateFormatEx(LOCALE_NAME_INVARIANT, 0, &st, L"yyyy-MM-dd", small, 4, NULL));
        oracle_case("date_buffer_too_small");
        o_i64("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, GetDateFormatEx(LOCALE_NAME_INVARIANT, 0, &st, L"yyyy-MM-dd", NULL, 0, NULL));
        oracle_case("date_size_query");
        o_i64("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, GetTimeFormatEx(L"xx-INVALID", 0, &st, NULL, NULL, 0));
        oracle_case("time_invalid_locale");
        o_i64("ret", r);
        o_lasterr();
        oracle_case_end();
    }

    /* ---- probes ---- */
    dos_case("probe_dos_one_tick", 119600064000000000ULL + 1ULL);
    dos_case("probe_dos_half_second", 119600064000000000ULL + 5000000ULL);
    dos_case("probe_dos_59_seconds", 119600064000000000ULL + 590000000ULL);
    dos_case("probe_dos_2107_last_second", 159992927990000000ULL);
    dos2ft_case("probe_dos2ft_feb_29_2023", (43 << 9) | (2 << 5) | 29, 0);
    dos2ft_case("probe_dos2ft_seconds_30", 0x0021, 30);
    st2ft_case("probe_st_feb_29_2100", 2100, 2, 0, 29, 0, 0, 0, 0);
    st2ft_case("probe_st_year_30827_jan", 30827, 1, 0, 1, 0, 0, 0, 0);
    datefmt_case("probe_date_yyy", 0, L"yyy|yyyyy", 2009, 3, 0, 5);
    datefmt_case("probe_date_y_2024", 0, L"y", 2024, 3, 0, 5);
    datefmt_case("probe_date_g", 0, L"g|ggg", 2024, 2, 4, 29);
    {
        TIME_ZONE_INFORMATION tz = eastern();
        tz_case("probe_tz_winter_to_local_again", &tz, 1, 2024, 1, 15, 12, 0);
        TIME_ZONE_INFORMATION bad = tz;
        bad.StandardDate.wMonth = 13;
        tz_case("probe_tz_bad_std_month_13", &bad, 1, 2024, 7, 15, 12, 0);
        tz_case("probe_tz_bad_rule_to_utc", &bad, 0, 2024, 7, 15, 12, 0);
        bad = tz;
        bad.DaylightDate.wDay = 6;
        tz_case("probe_tz_bad_rule_day_6", &bad, 1, 2024, 7, 15, 12, 0);
    }
    datefmt_case("probe_date_default_again", 0, NULL, 2024, 2, 4, 29);

    return oracle_end();
}
