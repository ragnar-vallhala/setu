/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: NormalizeString and IsNormalizedString for the four Unicode
 * normalization forms: composition, decomposition, compatibility mappings,
 * canonical ordering of combining marks, Hangul, the size estimate returned
 * for a NULL buffer, ERROR_INSUFFICIENT_BUFFER, invalid input
 * (ERROR_NO_UNICODE_TRANSLATION with a negated position) and bad forms.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winnls/nf-winnls-normalizestring
 *       https://learn.microsoft.com/en-us/windows/win32/api/winnls/nf-winnls-isnormalizedstring
 *       https://learn.microsoft.com/en-us/windows/win32/intl/using-unicode-normalization-to-represent-strings
 */
#include "oracle.h"

static void norm(const char *name, NORM_FORM form, const wchar_t *src, int n, int size)
{
    wchar_t out[64];
    int r;
    wmemset(out, L'#', 64);
    ORACLE_CALL(r, NormalizeString(form, src, n, size ? out : NULL, size));
    oracle_case(name);
    o_i64("ret", r);
    o_lasterr();
    if (r > 0 && size && r <= size) o_bytes("out", out, (size_t)r * 2);
    oracle_case_end();
}

static void isn(const char *name, NORM_FORM form, const wchar_t *src, int n)
{
    BOOL r;
    ORACLE_CALL(r, IsNormalizedString(form, src, n));
    oracle_case(name);
    o_bool("ret", r);
    o_lasterr();
    oracle_case_end();
}

/* All four forms of one input in one case. */
static void all4(const char *name, const wchar_t *src)
{
    static const struct { const char *k; NORM_FORM f; } forms[] = {
        { "nfc", NormalizationC }, { "nfd", NormalizationD },
        { "nfkc", NormalizationKC }, { "nfkd", NormalizationKD },
    };
    oracle_case(name);
    for (int i = 0; i < 4; i++) {
        wchar_t out[64];
        char k[16];
        int r = NormalizeString(forms[i].f, src, -1, out, 64);
        if (r > 0) o_bytes(forms[i].k, out, (size_t)r * 2);
        else {
            snprintf(k, sizeof k, "%s.error", forms[i].k);
            o_u32(k, GetLastError());
        }
        snprintf(k, sizeof k, "%s.is", forms[i].k);
        o_bool(k, IsNormalizedString(forms[i].f, src, -1));
    }
    oracle_case_end();
}

int main(void)
{
    oracle_begin("kernel32/NormalizeString");

    all4("ascii", L"abc");
    all4("e_acute_precomposed", L"\x00e9");
    all4("e_acute_decomposed", L"e\x0301");
    all4("mark_order", L"a\x0301\x0323");
    all4("mark_order_canonical", L"a\x0323\x0301");
    all4("hangul_syllable", L"\xd55c");
    all4("hangul_jamo", L"\x1112\x1161\x11ab");
    all4("ligature_fi", L"\xfb01");
    all4("fullwidth_a", L"\xff21");
    all4("superscript_two", L"\x00b2");
    all4("ohm_sign", L"\x2126");
    all4("angstrom_sign", L"\x212b");
    all4("composition_exclusion", L"\x0958");
    all4("singleton_kelvin", L"\x212a");
    all4("surrogate_pair_math_a", L"\xd835\xdc00");
    all4("long_s_dot", L"\x1e9b\x0323");
    all4("vietnamese_stack", L"\x1ec7");
    all4("ellipsis", L"\x2026");

    norm("size_estimate", NormalizationD, L"\x00e9\x00e9", -1, 0);
    norm("size_estimate_counted", NormalizationD, L"\x00e9\x00e9", 2, 0);
    norm("size_estimate_kd_ligature", NormalizationKD, L"\xfdfa", -1, 0);
    norm("buffer_too_small", NormalizationD, L"\x00e9\x00e9", -1, 2);
    norm("buffer_exact", NormalizationD, L"\x00e9", -1, 3);
    norm("counted_no_nul", NormalizationC, L"e\x0301xyz", 2, 64);
    norm("zero_length", NormalizationC, L"abc", 0, 64);
    norm("lone_high_surrogate", NormalizationC, L"ab\xd800" L"c", -1, 64);
    norm("lone_low_surrogate", NormalizationD, L"\xdc00", -1, 64);
    norm("noncharacter_fffe", NormalizationC, L"a\xfffe", -1, 64);
    norm("unassigned_code_point", NormalizationC, L"a\x0378", -1, 64);
    norm("bad_form_0", (NORM_FORM)0, L"a", -1, 64);
    norm("bad_form_3", (NORM_FORM)3, L"a", -1, 64);
    norm("bad_form_other", (NORM_FORM)13, L"a", -1, 64);

    isn("is_nfc_decomposed", NormalizationC, L"e\x0301", -1);
    isn("is_nfd_precomposed", NormalizationD, L"\x00e9", -1);
    isn("is_counted_prefix", NormalizationC, L"ae\x0301", 1);
    isn("is_lone_surrogate", NormalizationC, L"\xd800", -1);
    isn("is_bad_form", (NORM_FORM)0, L"a", -1);
    isn("is_zero_length", NormalizationC, L"a", 0);

    return oracle_end();
}
