/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: IdnToAscii, IdnToUnicode and IdnToNameprepUnicode: punycode
 * encoding and decoding, IDN_ALLOW_UNASSIGNED, IDN_USE_STD3_ASCII_RULES,
 * label and name length limits, mapping (case folding, width), prohibited
 * code points, malformed punycode, and buffer rules.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winnls/nf-winnls-idntoascii
 *       https://learn.microsoft.com/en-us/windows/win32/api/winnls/nf-winnls-idntounicode
 *       https://learn.microsoft.com/en-us/windows/win32/api/winnls/nf-winnls-idntonameprepunicode
 *       https://learn.microsoft.com/en-us/windows/win32/intl/handling-internationalized-domain-names--idns
 */
#include "oracle.h"

enum which { TO_ASCII, TO_UNICODE, NAMEPREP };

static void idn(const char *name, enum which w, DWORD flags, const wchar_t *src, int n, int size)
{
    wchar_t out[300];
    int r;
    wmemset(out, L'#', 300);
    switch (w) {
    case TO_ASCII: ORACLE_CALL(r, IdnToAscii(flags, src, n, size ? out : NULL, size)); break;
    case TO_UNICODE: ORACLE_CALL(r, IdnToUnicode(flags, src, n, size ? out : NULL, size)); break;
    default: ORACLE_CALL(r, IdnToNameprepUnicode(flags, src, n, size ? out : NULL, size)); break;
    }
    oracle_case(name);
    o_i64("ret", r);
    o_lasterr();
    if (r > 0 && size && r <= size) {
        o_bytes("out", out, (size_t)(r < 120 ? r : 120) * 2);
        if (r < 300) {
            wchar_t tmp[300];
            memcpy(tmp, out, (size_t)r * 2);
            tmp[r] = 0;
            o_wstr("str", tmp);
        }
    }
    oracle_case_end();
}

int main(void)
{
    oracle_begin("kernel32/idn");

    wchar_t label63[80], label64[80], long_name[300];
    for (int i = 0; i < 63; i++) label63[i] = L'a';
    wcscpy(label63 + 63, L".com");
    for (int i = 0; i < 64; i++) label64[i] = L'a';
    wcscpy(label64 + 64, L".com");
    long_name[0] = 0;
    for (int i = 0; i < 4; i++) wcscat(long_name, L"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.");
    long_name[255] = 0; /* 255 characters: just over the limit with the label dots */

    idn("ascii_plain", TO_ASCII, 0, L"example.com", -1, 300);
    idn("ascii_upper_folded", TO_ASCII, 0, L"EXAMPLE.Com", -1, 300);
    idn("ascii_umlaut", TO_ASCII, 0, L"b\x00fc" L"cher.de", -1, 300);
    idn("ascii_chinese", TO_ASCII, 0, L"\x4f8b\x5b50.\x6d4b\x8bd5", -1, 300);
    idn("ascii_mixed_case_unicode", TO_ASCII, 0, L"B\x00dc" L"CHER.de", -1, 300);
    idn("ascii_sharp_s", TO_ASCII, 0, L"stra\x00df" L"e.de", -1, 300);
    idn("ascii_fullwidth_dot", TO_ASCII, 0, L"a\xff0e" L"b", -1, 300);
    idn("ascii_ideographic_dot", TO_ASCII, 0, L"a\x3002" L"b", -1, 300);
    idn("ascii_fullwidth_letters", TO_ASCII, 0, L"\xff41\xff42", -1, 300);
    idn("ascii_soft_hyphen_removed", TO_ASCII, 0, L"ab\x00ad" L"c", -1, 300);
    idn("ascii_trailing_dot", TO_ASCII, 0, L"example.com.", -1, 300);
    idn("ascii_empty_label", TO_ASCII, 0, L"a..b", -1, 300);
    idn("ascii_label_63", TO_ASCII, 0, label63, -1, 300);
    idn("ascii_label_64", TO_ASCII, 0, label64, -1, 300);
    idn("ascii_name_too_long", TO_ASCII, 0, long_name, -1, 300);
    idn("ascii_underscore", TO_ASCII, 0, L"a_b.com", -1, 300);
    idn("ascii_underscore_std3", TO_ASCII, IDN_USE_STD3_ASCII_RULES, L"a_b.com", -1, 300);
    idn("ascii_leading_hyphen_std3", TO_ASCII, IDN_USE_STD3_ASCII_RULES, L"-ab.com", -1, 300);
    idn("ascii_leading_hyphen", TO_ASCII, 0, L"-ab.com", -1, 300);
    idn("ascii_unassigned", TO_ASCII, 0, L"a\x0378", -1, 300);
    idn("ascii_unassigned_allowed", TO_ASCII, IDN_ALLOW_UNASSIGNED, L"a\x0378", -1, 300);
    idn("ascii_prohibited_space", TO_ASCII, 0, L"a b", -1, 300);
    idn("ascii_prohibited_ideographic_space", TO_ASCII, 0, L"a\x3000" L"b", -1, 300);
    idn("ascii_bidi_mixed", TO_ASCII, 0, L"\x05d0" L"a", -1, 300);
    idn("ascii_bidi_rtl_only", TO_ASCII, 0, L"\x05d0\x05d1", -1, 300);
    idn("ascii_already_punycode", TO_ASCII, 0, L"xn--bcher-kva.de", -1, 300);
    idn("ascii_size_query", TO_ASCII, 0, L"b\x00fc" L"cher.de", -1, 0);
    idn("ascii_buffer_too_small", TO_ASCII, 0, L"b\x00fc" L"cher.de", -1, 5);
    idn("ascii_counted", TO_ASCII, 0, L"b\x00fc" L"cher.dexyz", 9, 300);
    idn("ascii_zero_length", TO_ASCII, 0, L"a", 0, 300);
    idn("ascii_bad_flag", TO_ASCII, 0x100, L"a", -1, 300);

    idn("unicode_punycode", TO_UNICODE, 0, L"xn--bcher-kva.de", -1, 300);
    idn("unicode_punycode_upper_prefix", TO_UNICODE, 0, L"XN--bcher-kva.de", -1, 300);
    idn("unicode_chinese", TO_UNICODE, 0, L"xn--fsqu00a.xn--0zwm56d", -1, 300);
    idn("unicode_plain", TO_UNICODE, 0, L"Example.COM", -1, 300);
    idn("unicode_bad_punycode", TO_UNICODE, 0, L"xn--a-.com", -1, 300);
    idn("unicode_bad_punycode_digits", TO_UNICODE, 0, L"xn--99999999999.com", -1, 300);
    idn("unicode_non_ascii_input", TO_UNICODE, 0, L"b\x00fc" L"cher.de", -1, 300);
    idn("unicode_punycode_of_ascii", TO_UNICODE, 0, L"xn--abc-.com", -1, 300);
    idn("unicode_size_query", TO_UNICODE, 0, L"xn--bcher-kva.de", -1, 0);
    idn("unicode_buffer_too_small", TO_UNICODE, 0, L"xn--bcher-kva.de", -1, 3);

    idn("nameprep_case_fold", NAMEPREP, 0, L"B\x00dc" L"CHER.De", -1, 300);
    idn("nameprep_sharp_s", NAMEPREP, 0, L"stra\x00df" L"e", -1, 300);
    idn("nameprep_compat", NAMEPREP, 0, L"\xfb01\x2126", -1, 300);
    idn("nameprep_prohibited", NAMEPREP, 0, L"a\x0000" L"b", 3, 300);
    idn("nameprep_ascii", NAMEPREP, 0, L"abc", -1, 300);

    return oracle_end();
}
