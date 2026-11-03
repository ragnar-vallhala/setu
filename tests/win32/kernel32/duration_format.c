/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: GetDurationFormatEx and GetDurationFormat: the duration
 * pictures (d, h/hh, H, m/mm, s/ss, f... up to 9), values given as a
 * SYSTEMTIME or as 100-ns units, carrying into the largest picture present,
 * literals and quotes, the locale default format (NULL), flags, buffer rules
 * and invalid input.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winnls/nf-winnls-getdurationformatex
 *       https://learn.microsoft.com/en-us/windows/win32/api/winnls/nf-winnls-getdurationformat
 */
#include "oracle.h"

#define SEC 10000000ULL
#define MIN (60 * SEC)
#define HOUR (60 * MIN)
#define DAY (24 * HOUR)

static void dur(const char *name, const wchar_t *loc, DWORD flags, const SYSTEMTIME *st,
                ULONGLONG v, const wchar_t *fmt, int size)
{
    wchar_t out[128];
    int r;
    wmemset(out, L'#', 128);
    ORACLE_CALL(r, GetDurationFormatEx(loc, flags, st, v, fmt, size ? out : NULL, size));
    oracle_case(name);
    o_i64("ret", r);
    o_lasterr();
    if (r > 0 && size) o_wstr("out", out);
    oracle_case_end();
}

int main(void)
{
    oracle_begin("kernel32/duration_format");

    ULONGLONG v = 1 * DAY + 2 * HOUR + 3 * MIN + 4 * SEC + 5678901; /* 1d 02:03:04.5678901 */

    dur("hh_mm_ss", L"en-US", 0, NULL, v, L"hh:mm:ss", 128);
    dur("d_hh_mm_ss", L"en-US", 0, NULL, v, L"d hh:mm:ss", 128);
    dur("h_m_s_single", L"en-US", 0, NULL, 2 * HOUR + 3 * MIN + 4 * SEC, L"h:m:s", 128);
    dur("capital_H", L"en-US", 0, NULL, v, L"H:mm", 128);
    dur("minutes_only_carry", L"en-US", 0, NULL, v, L"m", 128);
    dur("seconds_only_carry", L"en-US", 0, NULL, v, L"s", 128);
    dur("days_only", L"en-US", 0, NULL, v, L"d", 128);
    dur("fraction_1", L"en-US", 0, NULL, v, L"s.f", 128);
    dur("fraction_3", L"en-US", 0, NULL, v, L"ss.fff", 128);
    dur("fraction_7", L"en-US", 0, NULL, v, L"ss.fffffff", 128);
    dur("fraction_9", L"en-US", 0, NULL, v, L"ss.fffffffff", 128);
    dur("fraction_capital_F", L"en-US", 0, NULL, v, L"ss.FFF", 128);
    dur("quoted_literal", L"en-US", 0, NULL, v, L"h 'hours' m 'min'", 128);
    dur("quoted_apostrophe", L"en-US", 0, NULL, v, L"h''m", 128);
    dur("unterminated_quote", L"en-US", 0, NULL, v, L"h 'x", 128);
    dur("other_letters_literal", L"en-US", 0, NULL, v, L"hh:mm xyz t", 128);
    dur("triple_h", L"en-US", 0, NULL, v, L"hhh", 128);
    dur("zero", L"en-US", 0, NULL, 0, L"hh:mm:ss.fff", 128);
    dur("large_hours", L"en-US", 0, NULL, 1000 * HOUR, L"hh:mm", 128);
    dur("default_format_en", L"en-US", 0, NULL, v, NULL, 128);
    dur("default_format_de", L"de-DE", 0, NULL, v, NULL, 128);
    dur("default_format_ja", L"ja-JP", 0, NULL, v, NULL, 128);
    dur("default_format_invariant", L"", 0, NULL, v, NULL, 128);
    dur("nouseroverride", L"en-US", LOCALE_NOUSEROVERRIDE, NULL, v, L"hh:mm", 128);
    dur("bad_flag", L"en-US", TIME_NOSECONDS, NULL, v, L"hh:mm", 128);
    dur("bad_locale", L"xx-YY", 0, NULL, v, L"hh:mm", 128);
    dur("size_query", L"en-US", 0, NULL, v, L"hh:mm:ss", 0);
    dur("buffer_too_small", L"en-US", 0, NULL, v, L"hh:mm:ss", 4);
    dur("buffer_exact", L"en-US", 0, NULL, v, L"hh:mm:ss", 9);
    dur("empty_format", L"en-US", 0, NULL, v, L"", 128);

    {
        SYSTEMTIME st = { 0 };
        st.wHour = 25; st.wMinute = 61; st.wSecond = 5; st.wMilliseconds = 250;
        dur("systemtime_overflowing_fields", L"en-US", 0, &st, 0, L"hh:mm:ss.fff", 128);
        st.wHour = 1; st.wMinute = 2; st.wSecond = 3; st.wMilliseconds = 4;
        st.wDay = 9; st.wMonth = 9; st.wYear = 2000;
        dur("systemtime_ignores_date", L"en-US", 0, &st, 0, L"d hh:mm:ss.fff", 128);
        dur("systemtime_wins_over_value", L"en-US", 0, &st, v, L"hh:mm:ss", 128);
        st.wHour = 0; st.wMinute = 0; st.wSecond = 0; st.wMilliseconds = 1000;
        dur("systemtime_ms_1000", L"en-US", 0, &st, 0, L"ss.fff", 128);
    }

    {
        wchar_t out[64];
        int r;
        ORACLE_CALL(r, GetDurationFormat(MAKELCID(0x0409, SORT_DEFAULT), 0, NULL, v, L"hh:mm:ss", out, 64));
        oracle_case("lcid_form");
        o_i64("ret", r);
        o_lasterr();
        if (r > 0) o_wstr("out", out);
        oracle_case_end();
    }

    return oracle_end();
}
