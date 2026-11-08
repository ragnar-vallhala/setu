/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: time zone information. The machine's own zone is checked by
 * shape and relationships only (GetTimeZoneInformation vs
 * GetDynamicTimeZoneInformation, FileTimeToLocalFileTime using the current
 * bias). Named zones from EnumDynamicTimeZoneInformation are printed in full
 * (the zone data is the documented registry database), and
 * GetTimeZoneInformationForYear / SystemTimeToTzSpecificLocalTimeEx /
 * TzSpecificLocalTimeToSystemTimeEx are run against them with fixed times.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/timezoneapi/nf-timezoneapi-gettimezoneinformation
 *       https://learn.microsoft.com/en-us/windows/win32/api/timezoneapi/nf-timezoneapi-getdynamictimezoneinformation
 *       https://learn.microsoft.com/en-us/windows/win32/api/timezoneapi/nf-timezoneapi-enumdynamictimezoneinformation
 *       https://learn.microsoft.com/en-us/windows/win32/api/timezoneapi/nf-timezoneapi-gettimezoneinformationforyear
 *       https://learn.microsoft.com/en-us/windows/win32/api/timezoneapi/nf-timezoneapi-systemtimetotzspecificlocaltimeex
 *       https://learn.microsoft.com/en-us/windows/win32/api/timezoneapi/nf-timezoneapi-tzspecificlocaltimetosystemtimeex
 *       https://learn.microsoft.com/en-us/windows/win32/api/timezoneapi/nf-timezoneapi-getdynamictimezoneinformationeffectiveyears
 *       https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-filetimetolocalfiletime
 */
#include "oracle.h"

typedef DWORD (WINAPI *enum_fn)(DWORD, PDYNAMIC_TIME_ZONE_INFORMATION);
typedef DWORD (WINAPI *years_fn)(const DYNAMIC_TIME_ZONE_INFORMATION *, LPDWORD, LPDWORD);

static enum_fn pEnum;
static years_fn pYears;

static void st_str(const char *k, const SYSTEMTIME *s)
{
    char t[64];
    snprintf(t, sizeof t, "%u-%02u-%02u %02u:%02u:%02u.%03u dow%u", s->wYear, s->wMonth, s->wDay,
             s->wHour, s->wMinute, s->wSecond, s->wMilliseconds, s->wDayOfWeek);
    o_str(k, t);
}

static void tzi_fields(const TIME_ZONE_INFORMATION *t)
{
    o_i64("bias", t->Bias);
    o_wstr("std_name", t->StandardName);
    st_str("std_date", &t->StandardDate);
    o_i64("std_bias", t->StandardBias);
    o_wstr("dst_name", t->DaylightName);
    st_str("dst_date", &t->DaylightDate);
    o_i64("dst_bias", t->DaylightBias);
}

static int find_zone(const wchar_t *key, DYNAMIC_TIME_ZONE_INFORMATION *out)
{
    for (DWORD i = 0; i < 1000; i++) {
        DYNAMIC_TIME_ZONE_INFORMATION d;
        if (pEnum(i, &d)) return 0;
        if (!wcscmp(d.TimeZoneKeyName, key)) {
            *out = d;
            return 1;
        }
    }
    return 0;
}

static void zone_case(const char *name, const wchar_t *key)
{
    DYNAMIC_TIME_ZONE_INFORMATION d;
    int found = find_zone(key, &d);
    oracle_case(name);
    o_bool("found", found);
    if (found) {
        tzi_fields((TIME_ZONE_INFORMATION *)&d);
        o_wstr("key", d.TimeZoneKeyName);
        o_bool("dynamic_disabled", d.DynamicDaylightTimeDisabled);
        if (pYears) {
            DWORD first = 0, last = 0, r = pYears(&d, &first, &last);
            o_u32("years_ret", r);
            if (!r) {
                o_u32("first_year", first);
                o_bool("last_year_ge_first", last >= first);
            }
        }
    }
    oracle_case_end();
}

static void year_case(const char *name, const wchar_t *key, USHORT year)
{
    DYNAMIC_TIME_ZONE_INFORMATION d;
    TIME_ZONE_INFORMATION t;
    BOOL r = FALSE;
    oracle_case(name);
    if (find_zone(key, &d)) {
        memset(&t, 0xcc, sizeof t);
        SetLastError(ORACLE_SENTINEL);
        r = GetTimeZoneInformationForYear(year, &d, &t);
        oracle_last_error = GetLastError();
        o_bool("ret", r);
        o_lasterr();
        if (r) tzi_fields(&t);
    } else o_bool("found", 0);
    oracle_case_end();
}

static void conv_case(const char *name, const wchar_t *key, int to_local, SYSTEMTIME in)
{
    DYNAMIC_TIME_ZONE_INFORMATION d;
    SYSTEMTIME out;
    BOOL r;
    oracle_case(name);
    if (find_zone(key, &d)) {
        memset(&out, 0, sizeof out);
        SetLastError(ORACLE_SENTINEL);
        r = to_local ? SystemTimeToTzSpecificLocalTimeEx(&d, &in, &out)
                     : TzSpecificLocalTimeToSystemTimeEx(&d, &in, &out);
        oracle_last_error = GetLastError();
        o_bool("ret", r);
        o_lasterr();
        if (r) st_str("out", &out);
    } else o_bool("found", 0);
    oracle_case_end();
}

static SYSTEMTIME mk(WORD y, WORD m, WORD d, WORD h, WORD mi)
{
    SYSTEMTIME s = { y, m, 0, d, h, mi, 0, 0 };
    return s;
}

int main(void)
{
    oracle_begin("kernel32/time_zones");

    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    HMODULE adv = LoadLibraryW(L"advapi32.dll");
    pEnum = (enum_fn)(void *)GetProcAddress(adv, "EnumDynamicTimeZoneInformation");
    pYears = (years_fn)(void *)GetProcAddress(adv, "GetDynamicTimeZoneInformationEffectiveYears");
    if (!pYears) pYears = (years_fn)(void *)GetProcAddress(k32, "GetDynamicTimeZoneInformationEffectiveYears");

    /* The machine's zone: shape only. */
    {
        TIME_ZONE_INFORMATION t;
        DYNAMIC_TIME_ZONE_INFORMATION d;
        DWORD r1, r2;
        ORACLE_CALL(r1, GetTimeZoneInformation(&t));
        DWORD e1 = oracle_last_error;
        r2 = GetDynamicTimeZoneInformation(&d);
        oracle_case("current_shape");
        o_bool("ret_valid", r1 <= TIME_ZONE_ID_DAYLIGHT);
        oracle_last_error = e1;
        o_lasterr();
        o_bool("dynamic_same_ret", r1 == r2);
        o_bool("dynamic_same_bias", t.Bias == d.Bias && t.StandardBias == d.StandardBias &&
                                    t.DaylightBias == d.DaylightBias);
        o_bool("dynamic_same_names", !wcscmp(t.StandardName, d.StandardName) &&
                                     !wcscmp(t.DaylightName, d.DaylightName));
        o_bool("dynamic_same_dates", !memcmp(&t.StandardDate, &d.StandardDate, sizeof(SYSTEMTIME)) &&
                                     !memcmp(&t.DaylightDate, &d.DaylightDate, sizeof(SYSTEMTIME)));
        o_bool("std_name_nonempty", t.StandardName[0] != 0);
        o_bool("key_name_nonempty", d.TimeZoneKeyName[0] != 0);
        o_bool("bias_in_range", t.Bias >= -14 * 60 && t.Bias <= 12 * 60);
        o_bool("unknown_iff_no_dst_date", (r1 == TIME_ZONE_ID_UNKNOWN) == (t.DaylightDate.wMonth == 0));
        DYNAMIC_TIME_ZONE_INFORMATION e;
        o_bool("key_enumerable", pEnum && find_zone(d.TimeZoneKeyName, &e));
        oracle_case_end();

        /* FileTimeToLocalFileTime applies the bias in effect now. */
        LONG cur = t.Bias + (r1 == TIME_ZONE_ID_DAYLIGHT ? t.DaylightBias :
                             r1 == TIME_ZONE_ID_STANDARD ? t.StandardBias : 0);
        FILETIME ft = { 0x2a5a8000, 0x01da4c1d }, lft, back; /* a fixed instant in 2024 */
        BOOL ok1 = FileTimeToLocalFileTime(&ft, &lft);
        BOOL ok2 = LocalFileTimeToFileTime(&lft, &back);
        LONGLONG diff = (LONGLONG)(((ULONGLONG)lft.dwHighDateTime << 32 | lft.dwLowDateTime) -
                                   ((ULONGLONG)ft.dwHighDateTime << 32 | ft.dwLowDateTime));
        oracle_case("filetime_local_uses_current_bias");
        o_bool("ok", ok1 && ok2);
        o_bool("diff_is_current_bias", diff == -(LONGLONG)cur * 60 * 10000000);
        o_bool("round_trip", back.dwLowDateTime == ft.dwLowDateTime && back.dwHighDateTime == ft.dwHighDateTime);
        oracle_case_end();
    }

    /* Enumeration shape. */
    {
        DYNAMIC_TIME_ZONE_INFORMATION d;
        DWORD r, n = 0, last = 0;
        int prev_bias_order = 1;
        LONG prev_bias = 0x7fffffff;
        oracle_case("enum_shape");
        o_bool("have_enum", pEnum != NULL);
        if (pEnum) {
            for (n = 0; n < 1000; n++) {
                SetLastError(ORACLE_SENTINEL);
                r = pEnum(n, &d);
                if (r) {
                    last = r;
                    break;
                }
                if (d.Bias > prev_bias) prev_bias_order = 0;
                prev_bias = d.Bias;
            }
            o_bool("many", n > 100);
            o_u32("end_ret", last);
            o_bool("sorted_by_descending_bias", prev_bias_order);
            SetLastError(ORACLE_SENTINEL);
            r = pEnum(100000, &d);
            o_u32("far_index_ret", r);
        }
        oracle_case_end();
    }

    zone_case("zone_pacific", L"Pacific Standard Time");
    zone_case("zone_utc", L"UTC");
    zone_case("zone_gmt", L"GMT Standard Time");
    zone_case("zone_central_europe", L"W. Europe Standard Time");
    zone_case("zone_india", L"India Standard Time");
    zone_case("zone_aus_eastern", L"AUS Eastern Standard Time");
    zone_case("zone_tokyo", L"Tokyo Standard Time");
    zone_case("zone_nepal", L"Nepal Standard Time");
    zone_case("zone_dateline", L"Dateline Standard Time");
    zone_case("zone_line_islands", L"Line Islands Standard Time");
    zone_case("zone_morocco", L"Morocco Standard Time");
    zone_case("zone_russian", L"Russian Standard Time");
    zone_case("zone_eastern", L"Eastern Standard Time");

    year_case("year_pacific_2006", L"Pacific Standard Time", 2006);
    year_case("year_pacific_2024", L"Pacific Standard Time", 2024);
    year_case("year_russian_2010", L"Russian Standard Time", 2010);
    year_case("year_russian_2013", L"Russian Standard Time", 2013);
    year_case("year_utc_2024", L"UTC", 2024);

    /* Probe: which year's rule Windows' Morocco TZI is, and where its
     * dynamic data ends (tzdata models Ramadan as a yearly change). */
    {
        DYNAMIC_TIME_ZONE_INFORMATION d;
        if (pYears && find_zone(L"Morocco Standard Time", &d)) {
            DWORD first = 0, last = 0, r = pYears(&d, &first, &last);
            oracle_case("probe_morocco_years");
            o_u32("ret", r);
            o_u32("last_year", last);
            oracle_case_end();
        }
        static const USHORT years[] = {2019, 2025, 2026, 2027, 2028, 2030, 2035, 2040};
        for (unsigned i = 0; i < sizeof years / sizeof years[0]; i++) {
            char nm[48];
            snprintf(nm, sizeof nm, "probe_morocco_%u", years[i]);
            year_case(nm, L"Morocco Standard Time", years[i]);
        }
    }

    conv_case("pacific_summer_to_local", L"Pacific Standard Time", 1, mk(2024, 7, 1, 12, 0));
    conv_case("pacific_winter_to_local", L"Pacific Standard Time", 1, mk(2024, 1, 15, 12, 0));
    conv_case("pacific_2006_rule_to_local", L"Pacific Standard Time", 1, mk(2006, 3, 20, 12, 0));
    conv_case("pacific_2007_rule_to_local", L"Pacific Standard Time", 1, mk(2007, 3, 20, 12, 0));
    conv_case("pacific_local_to_utc", L"Pacific Standard Time", 0, mk(2024, 7, 1, 5, 0));
    conv_case("russian_2012_to_local", L"Russian Standard Time", 1, mk(2012, 1, 1, 0, 0));
    conv_case("russian_2015_to_local", L"Russian Standard Time", 1, mk(2015, 1, 1, 0, 0));
    conv_case("aus_january_to_local", L"AUS Eastern Standard Time", 1, mk(2024, 1, 1, 0, 0));
    conv_case("nepal_to_local", L"Nepal Standard Time", 1, mk(2024, 1, 1, 0, 0));
    conv_case("india_local_to_utc", L"India Standard Time", 0, mk(2024, 1, 1, 0, 0));

    return oracle_end();
}
