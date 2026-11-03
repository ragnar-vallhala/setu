/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: GetCalendarInfoEx for a fixed set of CALTYPEs over the
 * Gregorian, Japanese, Thai, Hijri and Hebrew calendars in named locales
 * (with CAL_NOUSEROVERRIDE), CAL_RETURN_NUMBER, buffer rules and invalid
 * input; EnumCalendarInfoExEx, EnumDateFormatsExEx and EnumTimeFormatsEx
 * (values and order, which are the documented user-visible lists).
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winnls/nf-winnls-getcalendarinfoex
 *       https://learn.microsoft.com/en-us/windows/win32/intl/calendar-type-information
 *       https://learn.microsoft.com/en-us/windows/win32/intl/calendar-identifiers
 *       https://learn.microsoft.com/en-us/windows/win32/api/winnls/nf-winnls-enumcalendarinfoexex
 *       https://learn.microsoft.com/en-us/windows/win32/api/winnls/nf-winnls-enumdateformatsexex
 *       https://learn.microsoft.com/en-us/windows/win32/api/winnls/nf-winnls-enumtimeformatsex
 */
#include "oracle.h"

#ifndef CAL_SABBREVERASTRING
#define CAL_SABBREVERASTRING 0x00000039
#endif
#ifndef CAL_SMONTHDAY
#define CAL_SMONTHDAY 0x00000038
#endif
#ifndef CAL_SENGLISHERANAME
#define CAL_SENGLISHERANAME 0x0000003b
#endif
#ifndef CAL_SENGLISHABBREVERANAME
#define CAL_SENGLISHABBREVERANAME 0x0000003c
#endif

struct ct { const char *name; CALTYPE type; };
#define T(x) { #x, x }
static const struct ct types[] = {
    T(CAL_ICALINTVALUE), T(CAL_SCALNAME), T(CAL_IYEAROFFSETRANGE), T(CAL_SERASTRING),
    T(CAL_SABBREVERASTRING), T(CAL_SSHORTDATE), T(CAL_SLONGDATE), T(CAL_SYEARMONTH),
    T(CAL_SMONTHDAY), T(CAL_SDAYNAME1), T(CAL_SABBREVDAYNAME1), T(CAL_SSHORTESTDAYNAME1),
    T(CAL_SMONTHNAME1), T(CAL_SMONTHNAME13), T(CAL_SABBREVMONTHNAME1), T(CAL_ITWODIGITYEARMAX),
    { NULL, 0 }
};
#undef T

static void calgroup(const char *casename, const wchar_t *loc, CALID cal)
{
    oracle_case(casename);
    for (const struct ct *t = types; t->name; t++) {
        wchar_t buf[128];
        char k[64];
        int r = GetCalendarInfoEx(loc, cal, NULL, t->type | CAL_NOUSEROVERRIDE, buf, 128, NULL);
        if (r > 0) o_wstr(t->name + 4, buf);
        else {
            snprintf(k, sizeof k, "%s.error", t->name + 4);
            o_u32(k, GetLastError());
        }
    }
    oracle_case_end();
}

static void one(const char *name, const wchar_t *loc, CALID cal, CALTYPE type, int size)
{
    wchar_t buf[128];
    DWORD num = 0xdeadbeef;
    int r;
    wmemset(buf, L'#', 128);
    if (type & CAL_RETURN_NUMBER)
        ORACLE_CALL(r, GetCalendarInfoEx(loc, cal, NULL, type, NULL, size, &num));
    else
        ORACLE_CALL(r, GetCalendarInfoEx(loc, cal, NULL, type, size ? buf : NULL, size, NULL));
    oracle_case(name);
    o_i64("ret", r);
    o_lasterr();
    if (r > 0 && (type & CAL_RETURN_NUMBER)) o_u32("number", num);
    else if (r > 0 && size) o_wstr("value", buf);
    oracle_case_end();
}

static char listbuf[2048];
static int listcount;

static void add(const wchar_t *s)
{
    if (listcount++) strncat(listbuf, "|", sizeof listbuf - strlen(listbuf) - 1);
    strncat(listbuf, oracle_utf8(s), sizeof listbuf - strlen(listbuf) - 1);
}

static BOOL CALLBACK cal_cb(LPWSTR s, CALID cal, LPWSTR reserved, LPARAM lp)
{
    (void)reserved;
    (void)lp;
    wchar_t t[160];
    swprintf(t, 160, L"%u:%ls", (unsigned)cal, s);
    add(t);
    return TRUE;
}

static BOOL CALLBACK date_cb(LPWSTR s, CALID cal, LPARAM lp)
{
    (void)lp;
    wchar_t t[160];
    swprintf(t, 160, L"%u:%ls", (unsigned)cal, s);
    add(t);
    return TRUE;
}

static BOOL CALLBACK time_cb(LPWSTR s, LPARAM lp)
{
    (void)lp;
    add(s);
    return TRUE;
}

static void enum_cal(const char *name, const wchar_t *loc, CALID cal, CALTYPE type)
{
    BOOL r;
    listbuf[0] = 0;
    listcount = 0;
    ORACLE_CALL(r, EnumCalendarInfoExEx(cal_cb, loc, cal, NULL, type, 0));
    oracle_case(name);
    o_bool("ret", r);
    o_lasterr();
    o_i64("count", listcount);
    o_str("list", listbuf);
    oracle_case_end();
}

static void enum_dates(const char *name, const wchar_t *loc, DWORD flags)
{
    BOOL r;
    listbuf[0] = 0;
    listcount = 0;
    ORACLE_CALL(r, EnumDateFormatsExEx(date_cb, loc, flags, 0));
    oracle_case(name);
    o_bool("ret", r);
    o_lasterr();
    o_i64("count", listcount);
    o_str("list", listbuf);
    oracle_case_end();
}

static void enum_times(const char *name, const wchar_t *loc, DWORD flags)
{
    BOOL r;
    listbuf[0] = 0;
    listcount = 0;
    ORACLE_CALL(r, EnumTimeFormatsEx(time_cb, loc, flags, 0));
    oracle_case(name);
    o_bool("ret", r);
    o_lasterr();
    o_i64("count", listcount);
    o_str("list", listbuf);
    oracle_case_end();
}

int main(void)
{
    oracle_begin("kernel32/calendar_info");

    calgroup("en_us_gregorian", L"en-US", CAL_GREGORIAN);
    calgroup("de_de_gregorian", L"de-DE", CAL_GREGORIAN);
    calgroup("ja_jp_gregorian", L"ja-JP", CAL_GREGORIAN);
    calgroup("ja_jp_japanese", L"ja-JP", CAL_JAPAN);
    calgroup("th_th_thai", L"th-TH", CAL_THAI);
    calgroup("ar_sa_hijri", L"ar-SA", CAL_HIJRI);
    calgroup("he_il_hebrew", L"he-IL", CAL_HEBREW);
    calgroup("en_us_us_english", L"en-US", CAL_GREGORIAN_US);
    calgroup("invariant_gregorian", L"", CAL_GREGORIAN);

    one("return_number_twodigityearmax", L"en-US", CAL_GREGORIAN, CAL_ITWODIGITYEARMAX | CAL_RETURN_NUMBER | CAL_NOUSEROVERRIDE, 0);
    one("return_number_calint", L"ja-JP", CAL_JAPAN, CAL_ICALINTVALUE | CAL_RETURN_NUMBER, 0);
    one("return_number_string_type", L"en-US", CAL_GREGORIAN, CAL_SDAYNAME1 | CAL_RETURN_NUMBER, 0);
    one("japanese_era_list_current", L"ja-JP", CAL_JAPAN, CAL_SERASTRING | CAL_NOUSEROVERRIDE, 128);
    one("size_query", L"en-US", CAL_GREGORIAN, CAL_SMONTHNAME9 | CAL_NOUSEROVERRIDE, 0);
    one("buffer_too_small", L"en-US", CAL_GREGORIAN, CAL_SMONTHNAME9 | CAL_NOUSEROVERRIDE, 3);
    one("calendar_not_for_locale", L"en-US", CAL_JAPAN, CAL_SCALNAME, 128);
    one("calendar_bogus", L"en-US", 99, CAL_SCALNAME, 128);
    one("caltype_bogus", L"en-US", CAL_GREGORIAN, 0x7f, 128);
    one("locale_bogus", L"xx-YY", CAL_GREGORIAN, CAL_SCALNAME, 128);
    one("english_era_name", L"en-US", CAL_GREGORIAN, CAL_SENGLISHERANAME, 128);
    one("english_abbrev_era_name", L"ja-JP", CAL_JAPAN, CAL_SENGLISHABBREVERANAME, 128);

    enum_cal("enum_en_calnames_all", L"en-US", ENUM_ALL_CALENDARS, CAL_SCALNAME);
    enum_cal("enum_ja_calnames_all", L"ja-JP", ENUM_ALL_CALENDARS, CAL_SCALNAME);
    enum_cal("enum_ja_eras", L"ja-JP", CAL_JAPAN, CAL_SERASTRING);
    enum_cal("enum_en_shortdates", L"en-US", CAL_GREGORIAN, CAL_SSHORTDATE);
    enum_cal("enum_bad_locale", L"xx-YY", CAL_GREGORIAN, CAL_SSHORTDATE);
    enum_dates("enum_dates_en_short", L"en-US", DATE_SHORTDATE);
    enum_dates("enum_dates_en_long", L"en-US", DATE_LONGDATE);
    enum_dates("enum_dates_en_yearmonth", L"en-US", DATE_YEARMONTH);
    enum_dates("enum_dates_de_short", L"de-DE", DATE_SHORTDATE);
    enum_dates("enum_dates_ja_long", L"ja-JP", DATE_LONGDATE);
    enum_dates("enum_dates_bad_flags", L"en-US", DATE_SHORTDATE | DATE_LONGDATE);
    enum_times("enum_times_en", L"en-US", 0);
    enum_times("enum_times_en_short", L"en-US", TIME_NOSECONDS);
    enum_times("enum_times_de", L"de-DE", 0);
    enum_times("enum_times_bad_flag", L"en-US", TIME_FORCE24HOURFORMAT);

    return oracle_end();
}
