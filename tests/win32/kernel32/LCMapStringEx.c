/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: LCMapStringEx with named locales: Turkish/Azeri casing with
 * and without LCMAP_LINGUISTIC_CASING, LCMAP_TITLECASE, Chinese
 * simplified/traditional mapping, kana and width mapping in ja-JP,
 * LCMAP_BYTEREV, NORM_* stripping, flag combinations that are rejected,
 * buffer rules, and sort keys. Sort keys are checked by relationship
 * (memcmp order agrees with CompareStringEx, equal strings give equal keys)
 * plus the raw key of a few plain ASCII strings. LCMAP_HASH is checked only
 * by relationships.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winnls/nf-winnls-lcmapstringex
 *       https://learn.microsoft.com/en-us/windows/win32/api/winnls/nf-winnls-lcmapstringw
 *       https://learn.microsoft.com/en-us/windows/win32/intl/handling-sorting-in-your-applications
 */
#include "oracle.h"

#ifndef LCMAP_TITLECASE
#define LCMAP_TITLECASE 0x00000300
#endif
#ifndef LCMAP_HASH
#define LCMAP_HASH 0x00040000
#endif

static void map(const char *name, const wchar_t *loc, DWORD flags, const wchar_t *src, int n, int outsize)
{
    wchar_t out[128];
    int r;
    wmemset(out, L'#', 128);
    ORACLE_CALL(r, LCMapStringEx(loc, flags, src, n, outsize ? out : NULL, outsize, NULL, NULL, 0));
    oracle_case(name);
    o_i64("ret", r);
    o_lasterr();
    if (r > 0 && outsize && r <= outsize) o_bytes("out", out, (size_t)r * 2);
    if (r > 0 && outsize && r < outsize) {
        out[r] = 0;
        o_wstr("str", out);
    }
    oracle_case_end();
}

static int key(const wchar_t *loc, DWORD flags, const wchar_t *s, BYTE *out, int size)
{
    return LCMapStringEx(loc, flags | LCMAP_SORTKEY, s, -1, (LPWSTR)out, size, NULL, NULL, 0);
}

static int sgn(int v) { return v < 0 ? -1 : v > 0 ? 1 : 0; }

/* Sort key memcmp order and CompareStringEx order for a pair. */
static void keyrel(const char *name, const wchar_t *loc, DWORD flags, const wchar_t *a, const wchar_t *b)
{
    BYTE ka[256], kb[256];
    int la = key(loc, flags, a, ka, sizeof ka);
    int lb = key(loc, flags, b, kb, sizeof kb);
    int c = CompareStringEx(loc, flags, a, -1, b, -1, NULL, NULL, 0);
    int m = memcmp(ka, kb, (size_t)(la < lb ? la : lb));
    if (!m) m = la - lb;
    oracle_case(name);
    o_bool("both_ok", la > 0 && lb > 0);
    o_i64("key_order", sgn(m));
    o_i64("compare", c);
    o_bool("agree", c && sgn(m) == c - 2);
    oracle_case_end();
}

static void rawkey(const char *name, const wchar_t *loc, DWORD flags, const wchar_t *s)
{
    BYTE k[256];
    int r;
    ORACLE_CALL(r, key(loc, flags, s, k, sizeof k));
    oracle_case(name);
    o_i64("ret", r);
    o_lasterr();
    if (r > 0) o_bytes("key", k, (size_t)r);
    oracle_case_end();
}

int main(void)
{
    oracle_begin("kernel32/LCMapStringEx");

    /* Windows loads its tables on first use and may reset the last error then;
     * one call up front keeps that process-history detail out of the cases. */
    {
        wchar_t warm[8];
        LCMapStringEx(L"tr-TR", LCMAP_UPPERCASE | LCMAP_LINGUISTIC_CASING, L"i", -1, warm, 8, NULL, NULL, 0);
    }

    /* Casing. */
    map("tr_upper_i", L"tr-TR", LCMAP_UPPERCASE, L"i\x0131", -1, 64);
    map("tr_upper_i_linguistic", L"tr-TR", LCMAP_UPPERCASE | LCMAP_LINGUISTIC_CASING, L"i\x0131", -1, 64);
    map("tr_lower_I_linguistic", L"tr-TR", LCMAP_LOWERCASE | LCMAP_LINGUISTIC_CASING, L"I\x0130", -1, 64);
    map("tr_lower_I", L"tr-TR", LCMAP_LOWERCASE, L"I\x0130", -1, 64);
    map("az_upper_i_linguistic", L"az-Latn-AZ", LCMAP_UPPERCASE | LCMAP_LINGUISTIC_CASING, L"i", -1, 64);
    map("en_upper_i_linguistic", L"en-US", LCMAP_UPPERCASE | LCMAP_LINGUISTIC_CASING, L"i\x0131", -1, 64);
    map("en_upper_sharp_s", L"en-US", LCMAP_UPPERCASE, L"stra\x00dfe", -1, 64);
    map("en_upper_final_sigma", L"el-GR", LCMAP_UPPERCASE, L"\x03bf\x03c2", -1, 64);
    map("el_lower_sigma_linguistic", L"el-GR", LCMAP_LOWERCASE | LCMAP_LINGUISTIC_CASING, L"\x039f\x03a3", -1, 64);
    map("en_titlecase", L"en-US", LCMAP_TITLECASE, L"hello wORLD foo-bar o'neil", -1, 64);
    map("en_titlecase_digraph", L"en-US", LCMAP_TITLECASE, L"\x01c6ungla", -1, 64);
    map("en_titlecase_with_upper", L"en-US", LCMAP_TITLECASE | LCMAP_UPPERCASE, L"ab", -1, 64);

    /* Chinese. */
    map("zh_simplified", L"zh-CN", LCMAP_SIMPLIFIED_CHINESE, L"\x6f22\x8a9e\x5b78", -1, 64);
    map("zh_traditional", L"zh-TW", LCMAP_TRADITIONAL_CHINESE, L"\x6c49\x8bed\x5b66", -1, 64);
    map("zh_both_rejected", L"zh-CN", LCMAP_SIMPLIFIED_CHINESE | LCMAP_TRADITIONAL_CHINESE, L"a", -1, 64);

    /* Kana and width. */
    map("ja_katakana_voiced", L"ja-JP", LCMAP_KATAKANA, L"\x304c\x3071\x3094", -1, 64);
    map("ja_hiragana_small", L"ja-JP", LCMAP_HIRAGANA, L"\x30a1\x30f5\x30f6\x30fc", -1, 64);
    map("ja_halfwidth_voiced", L"ja-JP", LCMAP_HALFWIDTH, L"\x30ac\x30d1\x3002", -1, 64);
    map("ja_fullwidth_halfkana", L"ja-JP", LCMAP_FULLWIDTH, L"\xff76\xff9e\xff8a\xff9f", -1, 64);
    map("ja_fullwidth_ascii", L"ja-JP", LCMAP_FULLWIDTH, L"Az 9~", -1, 64);
    map("ja_katakana_fullwidth", L"ja-JP", LCMAP_KATAKANA | LCMAP_FULLWIDTH, L"\x3042\xff71" L"a", -1, 64);
    map("ja_hiragana_katakana_rejected", L"ja-JP", LCMAP_HIRAGANA | LCMAP_KATAKANA, L"a", -1, 64);
    map("ja_half_full_rejected", L"ja-JP", LCMAP_HALFWIDTH | LCMAP_FULLWIDTH, L"a", -1, 64);

    /* Byte reversal and stripping. */
    map("byterev", L"en-US", LCMAP_BYTEREV, L"AB", -1, 64);
    map("byterev_upper", L"en-US", LCMAP_BYTEREV | LCMAP_UPPERCASE, L"ab", -1, 64);
    map("ignorenonspace", L"en-US", NORM_IGNORENONSPACE, L"r\x00e9sum\x00e9 e\x0301", -1, 64);
    map("ignoresymbols", L"en-US", NORM_IGNORESYMBOLS, L"a-b c.d!", -1, 64);
    map("ignore_both", L"en-US", NORM_IGNORENONSPACE | NORM_IGNORESYMBOLS, L"\x00e9-x", -1, 64);
    map("ignorecase_alone", L"en-US", NORM_IGNORECASE, L"Ab", -1, 64);
    map("upper_ignorenonspace", L"en-US", LCMAP_UPPERCASE | NORM_IGNORENONSPACE, L"\x00e9", -1, 64);

    /* Buffers and validation. */
    map("size_query", L"en-US", LCMAP_UPPERCASE, L"hello", -1, 0);
    map("size_query_counted", L"en-US", LCMAP_UPPERCASE, L"hello", 3, 0);
    map("buffer_too_small", L"en-US", LCMAP_UPPERCASE, L"hello", -1, 3);
    map("buffer_exact", L"en-US", LCMAP_UPPERCASE, L"hello", -1, 6);
    map("zero_length_src", L"en-US", LCMAP_UPPERCASE, L"hello", 0, 64);
    map("bad_locale", L"xx-YY", LCMAP_UPPERCASE, L"a", -1, 64);
    map("bad_flag", L"en-US", 0x00000800, L"a", -1, 64);
    map("sortkey_with_upper", L"en-US", LCMAP_SORTKEY | LCMAP_UPPERCASE, L"a", -1, 64);
    map("sortkey_with_byterev_ok", L"en-US", LCMAP_SORTKEY | LCMAP_BYTEREV, L"a", -1, 0);
    map("titlecase_with_sortkey", L"en-US", LCMAP_SORTKEY | LCMAP_TITLECASE, L"a", -1, 64);
    {
        wchar_t buf[8] = L"abc";
        int r;
        ORACLE_CALL(r, LCMapStringEx(L"en-US", LCMAP_UPPERCASE, buf, -1, buf, 8, NULL, NULL, 0));
        oracle_case("in_place_upper");
        o_i64("ret", r);
        o_lasterr();
        o_wstr("str", buf);
        oracle_case_end();
        wcscpy(buf, L"abc");
        ORACLE_CALL(r, LCMapStringEx(L"en-US", LCMAP_SORTKEY, buf, -1, buf, 8, NULL, NULL, 0));
        oracle_case("in_place_sortkey");
        o_i64("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, LCMapStringEx(L"en-US", LCMAP_UPPERCASE, L"a", -1, buf, 8, NULL, (void *)1, 0));
        oracle_case("reserved_not_null");
        o_i64("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, LCMapStringW(MAKELCID(0x041f, SORT_DEFAULT), LCMAP_UPPERCASE | LCMAP_LINGUISTIC_CASING, L"i", -1, buf, 8));
        oracle_case("w_lcid_tr_upper");
        o_i64("ret", r);
        o_lasterr();
        if (r > 0) o_bytes("out", buf, (size_t)r * 2);
        oracle_case_end();
    }

    /* Sort keys. */
    rawkey("key_a", L"en-US", 0, L"a");
    rawkey("key_A", L"en-US", 0, L"A");
    rawkey("key_abc", L"en-US", 0, L"abc");
    rawkey("key_empty", L"en-US", 0, L"");
    rawkey("key_ignorecase_A", L"en-US", NORM_IGNORECASE, L"A");
    {
        BYTE k[4];
        int r;
        ORACLE_CALL(r, key(L"en-US", 0, L"abcdef", k, 4));
        oracle_case("key_buffer_too_small");
        o_i64("ret", r);
        o_lasterr();
        oracle_case_end();
    }
    keyrel("rel_a_b", L"en-US", 0, L"a", L"b");
    keyrel("rel_a_A", L"en-US", 0, L"a", L"A");
    keyrel("rel_ignorecase", L"en-US", NORM_IGNORECASE, L"Hello", L"hello");
    keyrel("rel_accent", L"en-US", 0, L"resume", L"r\x00e9sum\x00e9");
    keyrel("rel_hyphen", L"en-US", 0, L"co-op", L"coop");
    keyrel("rel_stringsort", L"en-US", SORT_STRINGSORT, L"co-op", L"coop");
    keyrel("rel_digits", L"en-US", SORT_DIGITSASNUMBERS, L"a10", L"a9");
    keyrel("rel_sv", L"sv-SE", 0, L"\x00f6", L"z");
    keyrel("rel_de", L"de-DE", 0, L"\x00f6", L"z");
    keyrel("rel_phonebook", L"de-DE_phoneb", 0, L"\x00e4", L"ae");
    keyrel("rel_kana", L"ja-JP", NORM_IGNOREKANATYPE, L"\x3042", L"\x30a2");

    /* Hash: equal input gives equal hash, flags fold as for comparisons. */
    {
        DWORD h1 = 0, h2 = 0, h3 = 0, h4 = 0, h5 = 0;
        int r1, r2, r3, r4, r5;
        ORACLE_CALL(r1, LCMapStringEx(L"en-US", LCMAP_HASH, L"Hello", -1, (LPWSTR)&h1, sizeof h1, NULL, NULL, 0));
        DWORD e1 = oracle_last_error;
        r2 = LCMapStringEx(L"en-US", LCMAP_HASH, L"Hello", -1, (LPWSTR)&h2, sizeof h2, NULL, NULL, 0);
        r3 = LCMapStringEx(L"en-US", LCMAP_HASH, L"Hellp", -1, (LPWSTR)&h3, sizeof h3, NULL, NULL, 0);
        r4 = LCMapStringEx(L"en-US", LCMAP_HASH | NORM_IGNORECASE, L"Hello", -1, (LPWSTR)&h4, sizeof h4, NULL, NULL, 0);
        r5 = LCMapStringEx(L"en-US", LCMAP_HASH | NORM_IGNORECASE, L"hELLO", -1, (LPWSTR)&h5, sizeof h5, NULL, NULL, 0);
        oracle_case("hash_relations");
        /* The hash values themselves are internal to Windows; only print how
         * they relate to each other and to the return value. */
        oracle_last_error = e1;
        o_lasterr();
        o_bool("all_nonzero", r1 && r2 && r3 && r4 && r5);
        o_bool("ret_is_hash", (DWORD)r1 == h1 && (DWORD)r3 == h3 && (DWORD)r5 == h5);
        o_bool("same_input_same_hash", h1 == h2);
        o_bool("different_input_differs", h1 != h3);
        o_bool("ignorecase_equal", h4 == h5);
        oracle_case_end();
    }

    return oracle_end();
}
