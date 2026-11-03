/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: WideCharToMultiByte edges that kernel32/MultiByteToWideChar
 * does not cover: WC_NO_BEST_FIT_CHARS, custom default characters, the
 * lpUsedDefaultChar flag, WC_COMPOSITECHECK with its WC_DEFAULTCHAR /
 * WC_DISCARDNS / WC_SEPCHARS companions, WC_ERR_INVALID_CHARS, invalid
 * surrogates, which flags each code page accepts (UTF-8, UTF-7, symbol,
 * DBCS), DBCS code pages (932, 936, 949, 950) and buffer rules.
 *
 * Only code pages that do not depend on the system locale are used.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/stringapiset/nf-stringapiset-widechartomultibyte
 *       https://learn.microsoft.com/en-us/windows/win32/intl/code-page-identifiers
 *       https://learn.microsoft.com/en-us/windows/win32/intl/security-considerations--international-features
 */
#include "oracle.h"

static void w2m(const char *name, UINT cp, DWORD flags, const wchar_t *src, int n,
                const char *defchar, int want_used, int size)
{
    char out[128];
    BOOL used = 7;
    int r;
    memset(out, '#', sizeof out);
    ORACLE_CALL(r, WideCharToMultiByte(cp, flags, src, n, size ? out : NULL, size, defchar,
                                       want_used ? &used : NULL));
    oracle_case(name);
    o_i64("ret", r);
    o_lasterr();
    if (r > 0 && size) o_bytes("out", out, (size_t)r);
    if (want_used) o_i64("used_default", used);
    oracle_case_end();
}

#define NOLEN -1

int main(void)
{
    oracle_begin("kernel32/WideCharToMultiByte");

    /* Best fit and defaults in 1252. */
    w2m("cp1252_best_fit_default", 1252, 0, L"\x0100\x2260\x221e", NOLEN, NULL, 1, 64);
    w2m("cp1252_no_best_fit", 1252, WC_NO_BEST_FIT_CHARS, L"\x0100\x2260\x221e", NOLEN, NULL, 1, 64);
    w2m("cp1252_custom_default", 1252, WC_NO_BEST_FIT_CHARS, L"a\x4e00z", NOLEN, "*", 1, 64);
    w2m("cp1252_custom_default_multibyte_str", 1252, WC_NO_BEST_FIT_CHARS, L"a\x4e00z", NOLEN, "XY", 1, 64);
    w2m("cp1252_used_default_false", 1252, 0, L"abc\x00e9", NOLEN, NULL, 1, 64);
    w2m("cp1252_used_default_null_ok", 1252, 0, L"\x4e00", NOLEN, NULL, 0, 64);
    w2m("cp1252_fullwidth_best_fit", 1252, 0, L"\xff21\xff42", NOLEN, NULL, 1, 64);
    w2m("cp1252_fullwidth_no_best_fit", 1252, WC_NO_BEST_FIT_CHARS, L"\xff21\xff42", NOLEN, NULL, 1, 64);
    w2m("cp1252_lone_surrogate", 1252, 0, L"a\xd800z", NOLEN, NULL, 1, 64);
    w2m("cp1252_surrogate_pair", 1252, 0, L"\xd83d\xde00", NOLEN, NULL, 1, 64);
    w2m("cp1252_err_invalid_chars_rejected", 1252, WC_ERR_INVALID_CHARS, L"a", NOLEN, NULL, 0, 64);
    w2m("cp1252_c1_controls", 1252, 0, L"\x0081\x008d\x0080\x20ac", NOLEN, NULL, 1, 64);
    w2m("cp437_best_fit", 437, 0, L"\x00c0\x00e3", NOLEN, NULL, 1, 64);
    w2m("cp437_no_best_fit", 437, WC_NO_BEST_FIT_CHARS, L"\x00c0\x00e3", NOLEN, NULL, 1, 64);

    /* Composite handling. */
    w2m("composite_check_precompose", 1252, WC_COMPOSITECHECK, L"e\x0301", NOLEN, NULL, 1, 64);
    w2m("composite_check_defaultchar", 1252, WC_COMPOSITECHECK | WC_DEFAULTCHAR, L"x\x0301", NOLEN, NULL, 1, 64);
    w2m("composite_check_discardns", 1252, WC_COMPOSITECHECK | WC_DISCARDNS, L"x\x0301", NOLEN, NULL, 1, 64);
    w2m("composite_check_sepchars", 1252, WC_COMPOSITECHECK | WC_SEPCHARS, L"x\x0301", NOLEN, NULL, 1, 64);
    w2m("no_composite_check", 1252, 0, L"e\x0301", NOLEN, NULL, 1, 64);
    w2m("discardns_without_compositecheck", 1252, WC_DISCARDNS, L"x\x0301", NOLEN, NULL, 1, 64);
    w2m("two_composite_companions", 1252, WC_COMPOSITECHECK | WC_DISCARDNS | WC_SEPCHARS, L"a", NOLEN, NULL, 0, 64);

    /* UTF-8 and UTF-7 flag rules. */
    w2m("utf8_err_invalid_lone_high", CP_UTF8, WC_ERR_INVALID_CHARS, L"a\xd800z", NOLEN, NULL, 0, 64);
    w2m("utf8_err_invalid_lone_low", CP_UTF8, WC_ERR_INVALID_CHARS, L"a\xdc00z", NOLEN, NULL, 0, 64);
    w2m("utf8_err_invalid_reversed_pair", CP_UTF8, WC_ERR_INVALID_CHARS, L"\xdc00\xd800", NOLEN, NULL, 0, 64);
    w2m("utf8_lone_high_at_end_counted", CP_UTF8, 0, L"a\xd800", 2, NULL, 0, 64);
    w2m("utf8_reversed_pair", CP_UTF8, 0, L"\xdc00\xd800", 2, NULL, 0, 64);
    w2m("utf8_noncharacter", CP_UTF8, WC_ERR_INVALID_CHARS, L"\xfffe\xffff", NOLEN, NULL, 0, 64);
    w2m("utf8_no_best_fit_ok", CP_UTF8, WC_NO_BEST_FIT_CHARS, L"\x00e9", NOLEN, NULL, 0, 64);
    w2m("utf8_compositecheck_rejected", CP_UTF8, WC_COMPOSITECHECK, L"a", NOLEN, NULL, 0, 64);
    w2m("utf8_used_default_rejected", CP_UTF8, 0, L"a", NOLEN, NULL, 1, 64);
    w2m("utf8_size_query_err_invalid", CP_UTF8, WC_ERR_INVALID_CHARS, L"a\xd800", NOLEN, NULL, 0, 0);
    w2m("utf7_flags_rejected", CP_UTF7, WC_NO_BEST_FIT_CHARS, L"a", NOLEN, NULL, 0, 64);
    w2m("utf7_default_char_rejected", CP_UTF7, 0, L"a", NOLEN, "?", 0, 64);
    w2m("utf7_plus_and_specials", CP_UTF7, 0, L"1+1=2 ~\\!", NOLEN, NULL, 0, 64);
    w2m("utf7_lone_surrogate", CP_UTF7, 0, L"\xd800", NOLEN, NULL, 0, 64);
    w2m("symbol_cp_42", CP_SYMBOL, 0, L"\xf041\x0041\xf0ff", NOLEN, NULL, 0, 64);
    w2m("symbol_cp_flags_rejected", CP_SYMBOL, WC_NO_BEST_FIT_CHARS, L"a", NOLEN, NULL, 0, 64);

    /* DBCS. */
    w2m("cp932_kanji_kana", 932, 0, L"\x65e5\x672c\xff76\x3042" L"A", NOLEN, NULL, 1, 64);
    w2m("cp932_yen_backslash", 932, 0, L"\\\x00a5", NOLEN, NULL, 1, 64);
    w2m("cp932_unmappable", 932, WC_NO_BEST_FIT_CHARS, L"\x0100", NOLEN, NULL, 1, 64);
    w2m("cp932_buffer_splits_dbcs", 932, 0, L"a\x65e5", NOLEN, NULL, 0, 2);
    w2m("cp936_hanzi", 936, 0, L"\x4e2d\x6587", NOLEN, NULL, 1, 64);
    w2m("cp949_hangul", 949, 0, L"\xd55c\xae00", NOLEN, NULL, 1, 64);
    w2m("cp950_hanzi", 950, 0, L"\x4e2d\x6587", NOLEN, NULL, 1, 64);
    w2m("cp932_default_char_dbcs", 932, WC_NO_BEST_FIT_CHARS, L"\x0100", NOLEN, "\x81\x48", 1, 64);

    /* Buffers and invalid parameters. */
    w2m("size_query", 1252, 0, L"hello", NOLEN, NULL, 0, 0);
    w2m("size_query_counted", 1252, 0, L"hello", 3, NULL, 0, 0);
    w2m("buffer_too_small", 1252, 0, L"hello", NOLEN, NULL, 0, 3);
    w2m("buffer_exact_no_nul", 1252, 0, L"hello", 5, NULL, 0, 5);
    w2m("zero_length", 1252, 0, L"hello", 0, NULL, 0, 64);
    w2m("null_source", 1252, 0, NULL, NOLEN, NULL, 0, 64);
    w2m("bad_code_page", 12345, 0, L"a", NOLEN, NULL, 0, 64);
    w2m("bad_flag_bit", 1252, 0x100000, L"a", NOLEN, NULL, 0, 64);
    {
        char buf[16] = { 'a', 0, 0, 0 };
        int r;
        ORACLE_CALL(r, WideCharToMultiByte(1252, 0, (LPCWSTR)buf, -1, buf, 16, NULL, NULL));
        oracle_case("same_buffer_rejected");
        o_i64("ret", r);
        o_lasterr();
        oracle_case_end();
    }

    return oracle_end();
}
