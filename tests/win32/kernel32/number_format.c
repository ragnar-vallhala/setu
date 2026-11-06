/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: GetNumberFormatEx and GetCurrencyFormatEx with named locales
 * (LOCALE_NOUSEROVERRIDE so the machine's settings never apply) and with
 * explicit NUMBERFMTW / CURRENCYFMTW structures: rounding, grouping
 * patterns, negative orders, leading zeros, input validation, buffer rules.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winnls/nf-winnls-getnumberformatex
 *       https://learn.microsoft.com/en-us/windows/win32/api/winnls/nf-winnls-getcurrencyformatex
 *       https://learn.microsoft.com/en-us/windows/win32/api/winnls/ns-winnls-numberfmtw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winnls/ns-winnls-currencyfmtw
 *       https://learn.microsoft.com/en-us/windows/win32/intl/locale-sgrouping
 */
#include "oracle.h"

static void num(const char *name, const wchar_t *loc, DWORD flags, const wchar_t *v, const NUMBERFMTW *f, int size)
{
    wchar_t out[128];
    int r;
    wmemset(out, L'#', 128);
    ORACLE_CALL(r, GetNumberFormatEx(loc, flags, v, f, size ? out : NULL, size));
    oracle_case(name);
    o_i64("ret", r);
    o_lasterr();
    if (r > 0 && size) o_wstr("out", out);
    oracle_case_end();
}

static void cur(const char *name, const wchar_t *loc, DWORD flags, const wchar_t *v, const CURRENCYFMTW *f, int size)
{
    wchar_t out[128];
    int r;
    wmemset(out, L'#', 128);
    ORACLE_CALL(r, GetCurrencyFormatEx(loc, flags, v, f, size ? out : NULL, size));
    oracle_case(name);
    o_i64("ret", r);
    o_lasterr();
    if (r > 0 && size) o_wstr("out", out);
    oracle_case_end();
}

#define NO LOCALE_NOUSEROVERRIDE

int main(void)
{
    oracle_begin("kernel32/number_format");

    /* Locale defaults. */
    num("en_basic", L"en-US", NO, L"1234567.891", NULL, 128);
    num("en_negative", L"en-US", NO, L"-1234.5", NULL, 128);
    num("en_round_half_up", L"en-US", NO, L"0.125", NULL, 128);
    num("en_round_carry", L"en-US", NO, L"9999.995", NULL, 128);
    num("en_leading_zero", L"en-US", NO, L".5", NULL, 128);
    num("en_integer", L"en-US", NO, L"42", NULL, 128);
    num("en_zero", L"en-US", NO, L"0", NULL, 128);
    num("en_negative_zero_rounds", L"en-US", NO, L"-0.001", NULL, 128);
    num("de_basic", L"de-DE", NO, L"1234567.891", NULL, 128);
    num("fr_basic", L"fr-FR", NO, L"1234567.891", NULL, 128);
    num("hi_indian_grouping", L"hi-IN", NO, L"123456789.5", NULL, 128);
    num("ja_basic", L"ja-JP", NO, L"-1234567.891", NULL, 128);
    num("ar_sa_basic", L"ar-SA", NO, L"-1234.5", NULL, 128);
    num("invariant_basic", L"", NO, L"-1234567.891", NULL, 128);

    /* Input validation. */
    num("bad_two_points", L"en-US", NO, L"1.2.3", NULL, 128);
    num("bad_letters", L"en-US", NO, L"12a", NULL, 128);
    num("bad_plus_sign", L"en-US", NO, L"+12", NULL, 128);
    num("bad_space", L"en-US", NO, L" 12", NULL, 128);
    num("bad_empty", L"en-US", NO, L"", NULL, 128);
    num("bad_minus_only", L"en-US", NO, L"-", NULL, 128);
    num("bad_grouped_input", L"en-US", NO, L"1,234", NULL, 128);
    num("trailing_point", L"en-US", NO, L"12.", NULL, 128);
    num("long_fraction", L"en-US", NO, L"1.99999999999999999999", NULL, 128);
    num("long_integer", L"en-US", NO, L"123456789012345678901234567890", NULL, 128);
    num("bad_locale", L"xx-YY", 0, L"1", NULL, 128);
    num("bad_flag", L"en-US", 0x80, L"1", NULL, 128);

    /* Buffers. */
    num("size_query", L"en-US", NO, L"1234.5", NULL, 0);
    num("buffer_too_small", L"en-US", NO, L"1234.5", NULL, 4);
    num("buffer_exact", L"en-US", NO, L"1234.5", NULL, 9);

    /* Explicit formats. */
    {
        NUMBERFMTW f = { 3, 1, 3, (LPWSTR)L".", (LPWSTR)L",", 1 };
        num("fmt_3_digits", L"en-US", 0, L"1234.56789", &f, 128);
        f.NumDigits = 0;
        num("fmt_0_digits_round", L"en-US", 0, L"2.5", &f, 128);
        f.NumDigits = 2; f.LeadingZero = 0;
        num("fmt_no_leading_zero", L"en-US", 0, L"0.5", &f, 128);
        num("fmt_no_leading_zero_zero", L"en-US", 0, L"0", &f, 128);
        f.NumDigits = 0;
        num("fmt_no_leading_zero_zero_0_digits", L"en-US", 0, L"0", &f, 128);
        num("fmt_no_leading_zero_zero_point_0_digits", L"en-US", 0, L"0.0", &f, 128);
        num("fmt_no_leading_zero_round_down_0_digits", L"en-US", 0, L"0.4", &f, 128);
        f.NumDigits = 2;
        f.LeadingZero = 1; f.Grouping = 0;
        num("fmt_no_grouping", L"en-US", 0, L"1234567", &f, 128);
        f.Grouping = 32;
        num("fmt_grouping_32", L"en-US", 0, L"123456789", &f, 128);
        f.Grouping = 320;
        num("fmt_grouping_320", L"en-US", 0, L"123456789", &f, 128);
        f.Grouping = 3;
        for (UINT o = 0; o <= 4; o++) {
            char n[32];
            f.NegativeOrder = o;
            snprintf(n, sizeof n, "fmt_negative_order_%u", o);
            num(n, L"en-US", 0, L"-12.5", &f, 128);
        }
        f.NegativeOrder = 5;
        num("fmt_negative_order_5_bad", L"en-US", 0, L"-12.5", &f, 128);
        f.NegativeOrder = 1;
        f.lpDecimalSep = (LPWSTR)L"<dec>";
        f.lpThousandSep = (LPWSTR)L"'";
        num("fmt_long_separators", L"en-US", 0, L"1234567.25", &f, 128);
        f.lpDecimalSep = NULL;
        num("fmt_null_separator", L"en-US", 0, L"1.5", &f, 128);
        f.lpDecimalSep = (LPWSTR)L".";
        f.NumDigits = 10;
        num("fmt_10_digits", L"en-US", 0, L"1.5", &f, 128);
        f.NumDigits = 2;
        num("fmt_with_nouseroverride_flag", L"en-US", NO, L"1.5", &f, 128);
    }

    /* Currency. */
    cur("cur_en", L"en-US", NO, L"-1234.567", NULL, 128);
    cur("cur_en_positive", L"en-US", NO, L"1234.567", NULL, 128);
    cur("cur_de", L"de-DE", NO, L"-1234.567", NULL, 128);
    cur("cur_fr", L"fr-FR", NO, L"1234.567", NULL, 128);
    cur("cur_ja", L"ja-JP", NO, L"-1234.567", NULL, 128);
    cur("cur_en_gb", L"en-GB", NO, L"-1234.5", NULL, 128);
    cur("cur_hi", L"hi-IN", NO, L"1234567.5", NULL, 128);
    cur("cur_invariant", L"", NO, L"-1234.5", NULL, 128);
    cur("cur_bad_input", L"en-US", NO, L"1e5", NULL, 128);
    cur("cur_size_query", L"en-US", NO, L"1.5", NULL, 0);
    cur("cur_buffer_too_small", L"en-US", NO, L"1.5", NULL, 3);
    {
        CURRENCYFMTW f = { 2, 1, 3, (LPWSTR)L".", (LPWSTR)L",", 0, 0, (LPWSTR)L"$" };
        for (UINT o = 0; o <= 15; o++) {
            char n[32];
            f.NegativeOrder = o;
            snprintf(n, sizeof n, "cur_fmt_neg_%u", o);
            cur(n, L"en-US", 0, L"-1.5", &f, 128);
        }
        f.NegativeOrder = 16;
        cur("cur_fmt_neg_16_bad", L"en-US", 0, L"-1.5", &f, 128);
        f.NegativeOrder = 0;
        for (UINT o = 0; o <= 3; o++) {
            char n[32];
            f.PositiveOrder = o;
            snprintf(n, sizeof n, "cur_fmt_pos_%u", o);
            cur(n, L"en-US", 0, L"1.5", &f, 128);
        }
        f.PositiveOrder = 4;
        cur("cur_fmt_pos_4_bad", L"en-US", 0, L"1.5", &f, 128);
        f.PositiveOrder = 0;
        f.lpCurrencySymbol = (LPWSTR)L"EUR";
        cur("cur_fmt_long_symbol", L"en-US", 0, L"1234.5", &f, 128);
        f.lpCurrencySymbol = NULL;
        cur("cur_fmt_null_symbol", L"en-US", 0, L"1.5", &f, 128);
    }

    return oracle_end();
}
