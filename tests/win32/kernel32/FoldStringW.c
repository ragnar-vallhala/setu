/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: FoldStringW and FoldStringA: MAP_FOLDCZONE, MAP_FOLDDIGITS,
 * MAP_PRECOMPOSED, MAP_COMPOSITE, MAP_EXPAND_LIGATURES, their valid and
 * invalid combinations, and the buffer rules.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winnls/nf-winnls-foldstringw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winnls/nf-winnls-foldstringa
 */
#include "oracle.h"

static void fold(const char *name, DWORD flags, const wchar_t *src, int n, int size)
{
    wchar_t out[64];
    int r;
    wmemset(out, L'#', 64);
    ORACLE_CALL(r, FoldStringW(flags, src, n, size ? out : NULL, size));
    oracle_case(name);
    o_i64("ret", r);
    o_lasterr();
    if (r > 0 && size && r <= size) o_bytes("out", out, (size_t)r * 2);
    oracle_case_end();
}

int main(void)
{
    oracle_begin("kernel32/FoldStringW");

    fold("digits_ascii_kept", MAP_FOLDDIGITS, L"012", -1, 64);
    fold("digits_arabic_indic", MAP_FOLDDIGITS, L"\x0660\x0661\x0669", -1, 64);
    fold("digits_devanagari", MAP_FOLDDIGITS, L"\x0966\x096f", -1, 64);
    fold("digits_fullwidth", MAP_FOLDDIGITS, L"\xff10\xff19", -1, 64);
    fold("digits_superscript", MAP_FOLDDIGITS, L"\x00b2\x00b3\x00b9\x2070", -1, 64);
    fold("digits_circled", MAP_FOLDDIGITS, L"\x2460\x2469", -1, 64);
    fold("digits_thai", MAP_FOLDDIGITS, L"\x0e50\x0e59", -1, 64);
    fold("foldczone_fullwidth", MAP_FOLDCZONE, L"\xff21\xff41\xff01", -1, 64);
    fold("foldczone_ligature_fi", MAP_FOLDCZONE, L"\xfb01", -1, 64);
    fold("foldczone_compat_ohm", MAP_FOLDCZONE, L"\x2126\x212b", -1, 64);
    fold("foldczone_halfwidth_kana", MAP_FOLDCZONE, L"\xff76\xff9e", -1, 64);
    fold("foldczone_plain_kept", MAP_FOLDCZONE, L"abc", -1, 64);
    fold("precomposed", MAP_PRECOMPOSED, L"e\x0301 a\x0308", -1, 64);
    fold("precomposed_no_composition", MAP_PRECOMPOSED, L"x\x0301", -1, 64);
    fold("composite", MAP_COMPOSITE, L"\x00e9\x00e4", -1, 64);
    fold("composite_hangul", MAP_COMPOSITE, L"\xd55c", -1, 64);
    fold("composite_plain_kept", MAP_COMPOSITE, L"abc", -1, 64);
    fold("expand_ligatures", MAP_EXPAND_LIGATURES, L"\x00e6\x00c6\x0153\x00df\xfb01", -1, 64);
    fold("expand_ligatures_ij", MAP_EXPAND_LIGATURES, L"\x0133\x01c6", -1, 64);
    fold("foldczone_and_digits", MAP_FOLDCZONE | MAP_FOLDDIGITS, L"\xff21\x0661", -1, 64);
    fold("foldczone_and_composite", MAP_FOLDCZONE | MAP_COMPOSITE, L"\x00e9\xff21", -1, 64);
    fold("digits_and_precomposed", MAP_FOLDDIGITS | MAP_PRECOMPOSED, L"\x0661e\x0301", -1, 64);

    /* Invalid combinations and parameters. */
    fold("precomposed_and_composite", MAP_PRECOMPOSED | MAP_COMPOSITE, L"a", -1, 64);
    fold("ligatures_and_precomposed", MAP_EXPAND_LIGATURES | MAP_PRECOMPOSED, L"a", -1, 64);
    fold("ligatures_and_composite", MAP_EXPAND_LIGATURES | MAP_COMPOSITE, L"a", -1, 64);
    fold("no_flags", 0, L"a", -1, 64);
    fold("bad_flag", 0x1000, L"a", -1, 64);
    fold("zero_length", MAP_FOLDDIGITS, L"a", 0, 64);
    fold("null_source", MAP_FOLDDIGITS, NULL, -1, 64);
    fold("size_query", MAP_EXPAND_LIGATURES, L"\x00e6x", -1, 0);
    fold("buffer_too_small", MAP_EXPAND_LIGATURES, L"\x00e6x", -1, 3);
    fold("buffer_exact", MAP_EXPAND_LIGATURES, L"\x00e6x", -1, 4);
    fold("counted_no_nul", MAP_FOLDDIGITS, L"\x0661\x0662\x0663", 2, 64);
    {
        wchar_t buf[8] = L"\x0661\x0662";
        int r;
        ORACLE_CALL(r, FoldStringW(MAP_FOLDDIGITS, buf, -1, buf, 8));
        oracle_case("same_buffer");
        o_i64("ret", r);
        o_lasterr();
        oracle_case_end();
    }
    {
        char out[16];
        int r;
        memset(out, '#', sizeof out);
        ORACLE_CALL(r, FoldStringA(MAP_FOLDDIGITS, "a\xb2" "b", -1, out, 16));
        oracle_case("ansi_superscript_two");
        o_i64("ret", r);
        o_lasterr();
        if (r > 0) o_bytes("out", out, (size_t)r);
        oracle_case_end();
        ORACLE_CALL(r, FoldStringA(MAP_EXPAND_LIGATURES, "\xe6\xdf", -1, out, 16));
        oracle_case("ansi_ligatures");
        o_i64("ret", r);
        o_lasterr();
        if (r > 0) o_bytes("out", out, (size_t)r);
        oracle_case_end();
        ORACLE_CALL(r, FoldStringA(MAP_FOLDDIGITS, "abc", -1, out, 2));
        oracle_case("ansi_buffer_too_small");
        o_i64("ret", r);
        o_lasterr();
        oracle_case_end();
    }

    return oracle_end();
}
