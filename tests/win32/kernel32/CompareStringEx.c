/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: CompareStringEx with named locales (language-specific sorting,
 * alternate sorts such as de-DE_phoneb and es-ES_tradnl), the NORM_* /
 * LINGUISTIC_* / SORT_* flags, the -1 and counted lengths, parameter
 * validation, plus CompareStringW / CompareStringA with LCIDs and a few
 * CompareStringOrdinal edges. The invariant locale is covered by
 * kernel32/string_format.
 *
 * Results are the documented CSTR_LESS_THAN (1), CSTR_EQUAL (2) and
 * CSTR_GREATER_THAN (3), or 0 with the last error.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/stringapiset/nf-stringapiset-comparestringex
 *       https://learn.microsoft.com/en-us/windows/win32/api/stringapiset/nf-stringapiset-comparestringw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winnls/nf-winnls-comparestringa
 *       https://learn.microsoft.com/en-us/windows/win32/api/stringapiset/nf-stringapiset-comparestringordinal
 *       https://learn.microsoft.com/en-us/windows/win32/intl/sort-order-identifiers
 *       https://learn.microsoft.com/en-us/windows/win32/intl/handling-sorting-in-your-applications
 */
#include "oracle.h"

static void cs(const char *name, const wchar_t *loc, DWORD flags, const wchar_t *a, int na,
               const wchar_t *b, int nb)
{
    int r;
    ORACLE_CALL(r, CompareStringEx(loc, flags, a, na, b, nb, NULL, NULL, 0));
    oracle_case(name);
    o_i64("ret", r);
    o_lasterr();
    oracle_case_end();
}

/* Both orders, so a symmetric result is checked as well. */
static void cs2(const char *name, const wchar_t *loc, DWORD flags, const wchar_t *a, const wchar_t *b)
{
    int r1, r2;
    DWORD e1;
    ORACLE_CALL(r1, CompareStringEx(loc, flags, a, -1, b, -1, NULL, NULL, 0));
    e1 = oracle_last_error;
    ORACLE_CALL(r2, CompareStringEx(loc, flags, b, -1, a, -1, NULL, NULL, 0));
    oracle_case(name);
    o_i64("ab", r1);
    o_i64("ba", r2);
    oracle_last_error = e1;
    o_lasterr();
    oracle_case_end();
}

int main(void)
{
    oracle_begin("kernel32/CompareStringEx");

    /* Windows loads its tables on first use and may reset the last error then;
     * one call up front keeps that process-history detail out of the cases. */
    CompareStringEx(L"en-US", 0, L"a", -1, L"b", -1, NULL, NULL, 0);

    /* Plain linguistic order in en-US. */
    cs2("en_lower_vs_upper", L"en-US", 0, L"a", L"A");
    cs2("en_a_vs_B", L"en-US", 0, L"a", L"B");
    cs2("en_prefix", L"en-US", 0, L"abc", L"abcd");
    cs2("en_accent_after_plain", L"en-US", 0, L"resume", L"résumé");
    cs2("en_accent_vs_next_letter", L"en-US", 0, L"é", L"f");
    cs2("en_hyphen_word_sort", L"en-US", 0, L"co-op", L"coop");
    cs2("en_apostrophe_word_sort", L"en-US", 0, L"can't", L"cant");
    cs2("en_digit_vs_letter", L"en-US", 0, L"1", L"a");
    cs2("en_space_vs_letter", L"en-US", 0, L" a", L"a");
    cs2("en_ligature_ae", L"en-US", 0, L"æ", L"ae");
    cs2("en_sharp_s", L"en-US", 0, L"ß", L"ss");

    /* Flags. */
    cs2("en_ignorecase", L"en-US", NORM_IGNORECASE, L"Hello", L"hELLO");
    cs2("en_linguistic_ignorecase", L"en-US", LINGUISTIC_IGNORECASE, L"Hello", L"hELLO");
    cs2("en_ignorenonspace", L"en-US", NORM_IGNORENONSPACE, L"résumé", L"resume");
    cs2("en_linguistic_ignorediacritic", L"en-US", LINGUISTIC_IGNOREDIACRITIC, L"résumé", L"resume");
    cs2("en_ignorecase_not_accent", L"en-US", NORM_IGNORECASE, L"É", L"e");
    cs2("en_ignoresymbols", L"en-US", NORM_IGNORESYMBOLS, L"a.b-c!", L"abc");
    cs2("en_ignorewidth", L"en-US", NORM_IGNOREWIDTH, L"ＡＢＣ", L"ABC");
    cs2("en_width_differs", L"en-US", 0, L"ＡＢＣ", L"ABC");
    cs2("en_stringsort_hyphen", L"en-US", SORT_STRINGSORT, L"co-op", L"coop");
    cs2("en_digits_as_numbers", L"en-US", SORT_DIGITSASNUMBERS, L"file10", L"file9");
    cs2("en_digits_as_text", L"en-US", 0, L"file10", L"file9");
    cs2("en_digits_leading_zero", L"en-US", SORT_DIGITSASNUMBERS, L"a01", L"a1");
    cs2("en_digits_mixed_runs", L"en-US", SORT_DIGITSASNUMBERS, L"a2b10", L"a2b9");
    cs2("en_ignorecase_stringsort", L"en-US", NORM_IGNORECASE | SORT_STRINGSORT, L"A-B", L"a-b");
    cs2("en_ignorekanatype", L"ja-JP", NORM_IGNOREKANATYPE, L"あいう", L"アイウ");
    cs2("ja_kana_differs", L"ja-JP", 0, L"あ", L"ア");
    cs2("en_linguistic_casing_flag", L"en-US", NORM_LINGUISTIC_CASING, L"a", L"A");

    /* Language-specific orders. */
    cs2("sv_o_umlaut_vs_z", L"sv-SE", 0, L"ö", L"z");
    cs2("de_o_umlaut_vs_z", L"de-DE", 0, L"ö", L"z");
    cs2("da_aa_vs_z", L"da-DK", 0, L"aa", L"z");
    cs2("en_aa_vs_z", L"en-US", 0, L"aa", L"z");
    cs2("da_ae_ligature", L"da-DK", 0, L"æ", L"ae");
    cs2("es_modern_ch", L"es-ES", 0, L"ch", L"cz");
    cs2("es_tradnl_ch", L"es-ES_tradnl", 0, L"ch", L"cz");
    cs2("es_n_tilde_vs_o", L"es-ES", 0, L"ñ", L"o");
    cs2("de_phoneb_a_umlaut", L"de-DE_phoneb", 0, L"ä", L"ae");
    cs2("de_default_a_umlaut", L"de-DE", 0, L"ä", L"ae");
    cs2("lt_y_vs_j", L"lt-LT", 0, L"y", L"j");
    cs2("tr_dotted_ignorecase", L"tr-TR", NORM_IGNORECASE, L"i", L"İ");
    cs2("tr_dotless_ignorecase", L"tr-TR", NORM_IGNORECASE, L"ı", L"I");
    cs2("tr_linguistic_ignorecase", L"tr-TR", LINGUISTIC_IGNORECASE, L"i", L"I");
    cs2("en_dotted_ignorecase", L"en-US", NORM_IGNORECASE, L"i", L"İ");

    /* Lengths. */
    cs("len_counted_prefix", L"en-US", 0, L"abcdef", 3, L"abc", -1);
    cs("len_counted_both", L"en-US", 0, L"abX", 2, L"abY", 2);
    cs("len_embedded_nul", L"en-US", 0, L"ab\0c", 4, L"ab", 2);
    cs("len_zero_vs_empty", L"en-US", 0, L"x", 0, L"", -1);
    cs("len_zero_vs_text", L"en-US", 0, L"x", 0, L"a", -1);
    cs("len_minus_two", L"en-US", 0, L"a", -2, L"a", -1);

    /* Parameter validation. */
    cs("bad_flag_bit", L"en-US", 0x40000000, L"a", -1, L"a", -1);
    cs("bad_locale", L"xx-YY", 0, L"a", -1, L"a", -1);
    cs("null_first_string", L"en-US", 0, NULL, -1, L"a", -1);
    cs("null_both_zero_len", L"en-US", 0, NULL, 0, NULL, 0);
    {
        int r;
        ORACLE_CALL(r, CompareStringEx(L"en-US", 0, L"a", -1, L"b", -1, NULL, (void *)1, 0));
        oracle_case("reserved_not_null");
        o_i64("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, CompareStringEx(L"en-US", 0, L"a", -1, L"b", -1, NULL, NULL, 1));
        oracle_case("lparam_not_zero");
        o_i64("ret", r);
        o_lasterr();
        oracle_case_end();
    }

    /* LCID forms. */
    {
        int r;
        ORACLE_CALL(r, CompareStringW(MAKELCID(0x041d, SORT_DEFAULT), 0, L"ö", -1, L"z", -1));
        oracle_case("w_lcid_sv");
        o_i64("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, CompareStringW(MAKELCID(0x0407, SORT_GERMAN_PHONE_BOOK), 0, L"ä", -1, L"ae", -1));
        oracle_case("w_lcid_de_phonebook");
        o_i64("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, CompareStringW(0x7fff, 0, L"a", -1, L"b", -1));
        oracle_case("w_lcid_bogus");
        o_i64("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, CompareStringA(MAKELCID(0x0409, SORT_DEFAULT), NORM_IGNORECASE, "\xc9t\xe9", -1, "\xe9T\xc9", -1));
        oracle_case("a_lcid_1252_ignorecase");
        o_i64("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, CompareStringA(MAKELCID(0x0409, SORT_DEFAULT), 0, "abc", 2, "ab", -1));
        oracle_case("a_counted");
        o_i64("ret", r);
        o_lasterr();
        oracle_case_end();
    }

    /* CompareStringOrdinal edges not in string_format. */
    {
        int r;
        ORACLE_CALL(r, CompareStringOrdinal(L"abc", -1, L"ABC", -1, 2));
        oracle_case("ordinal_ignorecase_2");
        o_i64("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, CompareStringOrdinal(L"a", -2, L"a", -1, FALSE));
        oracle_case("ordinal_len_minus_two");
        o_i64("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, CompareStringOrdinal(L"\x00e9", -1, L"\x00c9", -1, TRUE));
        oracle_case("ordinal_ignorecase_e_acute");
        o_i64("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, CompareStringOrdinal(L"\xd801\xdc00", -1, L"\xd801\xdc28", -1, TRUE));
        oracle_case("ordinal_ignorecase_deseret_pair");
        o_i64("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, CompareStringOrdinal(L"\xff21", -1, L"\xff41", -1, TRUE));
        oracle_case("ordinal_ignorecase_fullwidth");
        o_i64("ret", r);
        o_lasterr();
        oracle_case_end();
    }

    return oracle_end();
}
