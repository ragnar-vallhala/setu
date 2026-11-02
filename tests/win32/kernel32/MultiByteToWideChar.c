/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: MultiByteToWideChar / WideCharToMultiByte for code pages that
 * do not depend on the system locale (UTF-8, UTF-7, 1252, 437), plus
 * CompareStringOrdinal and CompareStringEx with the invariant locale.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/stringapiset/nf-stringapiset-multibytetowidechar
 *       https://learn.microsoft.com/en-us/windows/win32/api/stringapiset/nf-stringapiset-widechartomultibyte
 *       https://learn.microsoft.com/en-us/windows/win32/intl/code-page-identifiers
 *       https://learn.microsoft.com/en-us/windows/win32/api/stringapiset/nf-stringapiset-comparestringordinal
 *       https://learn.microsoft.com/en-us/windows/win32/api/stringapiset/nf-stringapiset-comparestringex
 */
#include "oracle.h"

static void mb2wc(const char *name, UINT cp, DWORD flags, const char *in, int inlen, int outsize)
{
    wchar_t out[64];
    int r;
    wmemset(out, 0xfffe, 64);
    ORACLE_CALL(r, MultiByteToWideChar(cp, flags, in, inlen, outsize ? out : NULL, outsize));
    oracle_case(name);
    o_i64("ret", r);
    o_lasterr();
    if (outsize && r > 0) o_bytes("out", out, (size_t)r * sizeof(wchar_t));
    oracle_case_end();
}

static void wc2mb(const char *name, UINT cp, DWORD flags, const wchar_t *in, int inlen, int outsize,
                  BOOL want_default)
{
    char out[64];
    int r;
    BOOL used = 77;
    memset(out, 0x7e, 64);
    ORACLE_CALL(r, WideCharToMultiByte(cp, flags, in, inlen, outsize ? out : NULL, outsize, NULL,
                                       want_default ? &used : NULL));
    oracle_case(name);
    o_i64("ret", r);
    o_lasterr();
    if (outsize && r > 0) o_bytes("out", out, (size_t)r);
    if (want_default) o_i64("used_default", used);
    oracle_case_end();
}

static void cmp_ord(const char *name, const wchar_t *a, const wchar_t *b, BOOL ic)
{
    int r;
    ORACLE_CALL(r, CompareStringOrdinal(a, -1, b, -1, ic));
    oracle_case(name);
    o_i64("ret", r);
    o_lasterr();
    oracle_case_end();
}

static void cmp_inv(const char *name, DWORD flags, const wchar_t *a, const wchar_t *b)
{
    int r;
    ORACLE_CALL(r, CompareStringEx(LOCALE_NAME_INVARIANT, flags, a, -1, b, -1, NULL, NULL, 0));
    oracle_case(name);
    o_i64("ret", r);
    o_lasterr();
    oracle_case_end();
}

int main(void)
{
    oracle_begin("kernel32/MultiByteToWideChar");

    /* UTF-8 decoding. */
    mb2wc("utf8_ascii", CP_UTF8, 0, "abc", -1, 64);
    mb2wc("utf8_explicit_len_no_nul", CP_UTF8, 0, "abc", 3, 64);
    mb2wc("utf8_two_byte", CP_UTF8, 0, "\xc3\xa9", -1, 64);
    mb2wc("utf8_three_byte", CP_UTF8, 0, "\xe6\x97\xa5", -1, 64);
    mb2wc("utf8_four_byte_surrogates", CP_UTF8, 0, "\xf0\x9f\x98\x80", -1, 64);
    mb2wc("utf8_invalid_lead", CP_UTF8, 0, "a\xffz", -1, 64);
    mb2wc("utf8_invalid_lead_strict", CP_UTF8, MB_ERR_INVALID_CHARS, "a\xffz", -1, 64);
    mb2wc("utf8_truncated_sequence", CP_UTF8, 0, "a\xe6\x97", -1, 64);
    mb2wc("utf8_overlong", CP_UTF8, 0, "\xc0\xaf", -1, 64);
    mb2wc("utf8_overlong_strict", CP_UTF8, MB_ERR_INVALID_CHARS, "\xc0\xaf", -1, 64);
    mb2wc("utf8_encoded_surrogate", CP_UTF8, 0, "\xed\xa0\x80", -1, 64);
    mb2wc("utf8_above_max", CP_UTF8, 0, "\xf4\x90\x80\x80", -1, 64);
    mb2wc("utf8_lone_continuation", CP_UTF8, 0, "\x80", -1, 64);
    mb2wc("utf8_bom_kept", CP_UTF8, 0, "\xef\xbb\xbfx", -1, 64);
    mb2wc("utf8_embedded_nul", CP_UTF8, 0, "a\0b", 3, 64);
    mb2wc("utf8_size_query", CP_UTF8, 0, "\xf0\x9f\x98\x80z", -1, 0);
    mb2wc("utf8_buffer_too_small", CP_UTF8, 0, "abcdef", -1, 3);
    mb2wc("utf8_surrogate_pair_split_by_buffer", CP_UTF8, 0, "\xf0\x9f\x98\x80", 4, 1);
    mb2wc("utf8_bad_flag", CP_UTF8, MB_PRECOMPOSED, "abc", -1, 64);
    mb2wc("utf8_zero_length", CP_UTF8, 0, "abc", 0, 64);
    mb2wc("bad_code_page", 12345, 0, "abc", -1, 64);

    /* Single-byte code pages. */
    mb2wc("cp1252_high", 1252, 0, "\x80\x9f\xe9", 3, 64);
    mb2wc("cp1252_undefined_0x81", 1252, 0, "\x81", 1, 64);
    mb2wc("cp1252_undefined_strict", 1252, MB_ERR_INVALID_CHARS, "\x81", 1, 64);
    mb2wc("cp437_box", 437, 0, "\xc9\xcd\xbb", 3, 64);
    mb2wc("cp437_control_glyphs", 437, MB_USEGLYPHCHARS, "\x01\x02", 2, 64);
    mb2wc("utf7_basic", CP_UTF7, 0, "Hi +AOk-!", -1, 64);

    /* Encoding. */
    wc2mb("to_utf8_bmp", CP_UTF8, 0, L"é日", -1, 64, FALSE);
    wc2mb("to_utf8_surrogate_pair", CP_UTF8, 0, L"\xd83d\xde00", -1, 64, FALSE);
    wc2mb("to_utf8_lone_high_surrogate", CP_UTF8, 0, L"a\xd800z", -1, 64, FALSE);
    wc2mb("to_utf8_lone_low_surrogate_strict", CP_UTF8, WC_ERR_INVALID_CHARS, L"a\xdc00z", -1, 64, FALSE);
    wc2mb("to_utf8_default_char_ptr_rejected", CP_UTF8, 0, L"a", -1, 64, TRUE);
    wc2mb("to_utf8_size_query", CP_UTF8, 0, L"é日", -1, 0, FALSE);
    wc2mb("to_utf8_buffer_splits_char", CP_UTF8, 0, L"a日", 2, 2, FALSE);
    wc2mb("to_1252_unmappable", 1252, 0, L"a日b", -1, 64, TRUE);
    wc2mb("to_1252_best_fit", 1252, 0, L"Ā", -1, 64, TRUE);
    wc2mb("to_1252_no_best_fit", 1252, WC_NO_BEST_FIT_CHARS, L"Ā", -1, 64, TRUE);
    wc2mb("to_1252_euro", 1252, 0, L"€", -1, 64, TRUE);
    wc2mb("to_437_box", 437, 0, L"╔═", -1, 64, TRUE);
    wc2mb("to_utf7_basic", CP_UTF7, 0, L"Hi é!", -1, 64, FALSE);

    /* Comparison. */
    cmp_ord("ordinal_equal", L"abc", L"abc", FALSE);
    cmp_ord("ordinal_case_differs", L"abc", L"ABC", FALSE);
    cmp_ord("ordinal_ignore_case", L"abc", L"ABC", TRUE);
    cmp_ord("ordinal_ignore_case_nonascii", L"été", L"ÉTÉ", TRUE);
    cmp_ord("ordinal_ignore_case_greek_sigma", L"σ", L"Σ", TRUE);
    cmp_ord("ordinal_ignore_case_final_sigma", L"ς", L"Σ", TRUE);
    cmp_ord("ordinal_ignore_case_dotless_i", L"ı", L"I", TRUE);
    cmp_ord("ordinal_ignore_case_sharp_s", L"ß", L"ẞ", TRUE);
    cmp_ord("ordinal_prefix_shorter", L"ab", L"abc", FALSE);
    cmp_ord("ordinal_underscore_vs_letter_ic", L"_", L"a", TRUE);
    cmp_inv("invariant_equal", 0, L"abc", L"abc");
    cmp_inv("invariant_case", 0, L"abc", L"ABC");
    cmp_inv("invariant_ignore_case", NORM_IGNORECASE, L"abc", L"ABC");
    cmp_inv("invariant_hyphen_vs_letter", 0, L"co-op", L"coop");
    cmp_inv("invariant_string_sort", SORT_STRINGSORT, L"co-op", L"coop");
    cmp_inv("invariant_digits_as_numbers", SORT_DIGITSASNUMBERS, L"a10", L"a9");
    cmp_inv("invariant_ignore_nonspace", NORM_IGNORENONSPACE, L"é", L"e");
    cmp_inv("invariant_accent", 0, L"é", L"e");
    cmp_inv("invariant_ignore_width", NORM_IGNOREWIDTH, L"Ａ", L"A");
    cmp_inv("invariant_bad_flag", 0x80000000, L"a", L"a");

    return oracle_end();
}
