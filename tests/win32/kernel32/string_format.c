/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: locale-invariant kernel32 string APIs: lstrlen, lstrcpyn,
 * lstrcat, lstrcmp/lstrcmpi ordering, CompareStringOrdinal, CompareStringEx
 * and LCMapStringEx with the invariant locale, GetStringTypeW, and
 * FormatMessageW with FORMAT_MESSAGE_FROM_STRING (inserts, printf-style
 * formats, escapes, width limits, buffer sizing) plus a few system messages.
 *
 * System message text is language dependent; both targets use English.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-lstrlenw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-lstrcpynw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-lstrcatw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-lstrcmpw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-lstrcmpiw
 *       https://learn.microsoft.com/en-us/windows/win32/api/stringapiset/nf-stringapiset-comparestringordinal
 *       https://learn.microsoft.com/en-us/windows/win32/api/stringapiset/nf-stringapiset-comparestringex
 *       https://learn.microsoft.com/en-us/windows/win32/api/winnls/nf-winnls-lcmapstringex
 *       https://learn.microsoft.com/en-us/windows/win32/api/stringapiset/nf-stringapiset-getstringtypew
 *       https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-formatmessagew
 *       https://learn.microsoft.com/en-us/windows/win32/intl/handling-sorting-in-your-applications
 */
#include "oracle.h"

static const char *sign(int v) { return v < 0 ? "lt" : v > 0 ? "gt" : "eq"; }

static void cmp_case(const char *name, const wchar_t *a, const wchar_t *b)
{
    int r1, r2, r3, r4;
    r1 = lstrcmpW(a, b);
    r2 = lstrcmpiW(a, b);
    r3 = CompareStringOrdinal(a, -1, b, -1, FALSE);
    r4 = CompareStringOrdinal(a, -1, b, -1, TRUE);
    oracle_case(name);
    o_str("lstrcmp", sign(r1));
    o_str("lstrcmpi", sign(r2));
    o_i64("ordinal", r3);
    o_i64("ordinal_ic", r4);
    oracle_case_end();
}

static void csx_case(const char *name, DWORD flags, const wchar_t *a, int na, const wchar_t *b, int nb)
{
    int r;
    ORACLE_CALL(r, CompareStringEx(LOCALE_NAME_INVARIANT, flags, a, na, b, nb, NULL, NULL, 0));
    oracle_case(name);
    o_i64("ret", r);
    o_lasterr();
    oracle_case_end();
}

static void map_case(const char *name, DWORD flags, const wchar_t *src, int n)
{
    wchar_t out[64];
    int r;
    wmemset(out, L'#', 64);
    ORACLE_CALL(r, LCMapStringEx(LOCALE_NAME_INVARIANT, flags, src, n, out, 64, NULL, NULL, 0));
    oracle_case(name);
    o_i64("ret", r);
    o_lasterr();
    if (r > 0 && r < 64 && !(flags & LCMAP_SORTKEY)) {
        out[r] = 0;
        o_wstr("out", out);
        o_bytes("raw", out, (size_t)r * 2);
    }
    oracle_case_end();
}

/* FormatMessageW from a string with up to 4 DWORD_PTR arguments (array form). */
static void fm_case(const char *name, DWORD flags, const wchar_t *fmt, DWORD size, int nargs, ...)
{
    /* Unused slots point at "_", so stray inserts (%99) stay well defined. */
    DWORD_PTR args[100];
    for (int i = 0; i < 100; i++) args[i] = (DWORD_PTR)L"_";
    va_list ap;
    va_start(ap, nargs);
    for (int i = 0; i < nargs; i++) args[i] = va_arg(ap, DWORD_PTR);
    va_end(ap);
    wchar_t buf[256];
    wmemset(buf, L'#', 256);
    DWORD r;
    ORACLE_CALL(r, FormatMessageW(FORMAT_MESSAGE_FROM_STRING | FORMAT_MESSAGE_ARGUMENT_ARRAY | flags, fmt, 0, 0, buf,
                                  size, (va_list *)args));
    oracle_case(name);
    o_u32("ret", r);
    o_lasterr();
    if (r && r < 256) {
        o_wstr("out", buf);
        o_bool("terminated", buf[r] == 0);
    } else if (!r) {
        o_bool("buffer_untouched", buf[0] == L'#');
    }
    oracle_case_end();
}

static void sysmsg_case(const char *name, DWORD id, DWORD extra)
{
    wchar_t buf[512];
    DWORD r;
    ORACLE_CALL(r, FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS | extra, NULL, id,
                                  MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), buf, 512, NULL));
    oracle_case(name);
    o_u32("ret", r);
    o_lasterr();
    if (r && r < 512) o_wstr("out", buf);
    oracle_case_end();
}

int main(void)
{
    oracle_begin("kernel32/string_format");

    /* ---- lstrlen / lstrcpyn / lstrcat ---- */
    oracle_case("lstrlen");
    o_i64("w_abc", lstrlenW(L"abc"));
    o_i64("w_empty", lstrlenW(L""));
    o_i64("w_null", lstrlenW(NULL));
    o_i64("a_abc", lstrlenA("abc"));
    o_i64("a_null", lstrlenA(NULL));
    o_i64("a_utf8_bytes", lstrlenA("\xc3\xa9t\xc3\xa9"));
    oracle_case_end();
    {
        int r;
        ORACLE_CALL(r, lstrlenW((LPCWSTR)(ULONG_PTR)1));
        oracle_case("lstrlen_bad_pointer");
        o_i64("ret", r);
        o_lasterr();
        oracle_case_end();
    }
    {
        wchar_t d[16];
        wchar_t *p;
        wmemset(d, L'#', 16);
        ORACLE_CALL(p, lstrcpynW(d, L"abcdef", 4));
        oracle_case("lstrcpyn_truncates");
        o_bool("ret_is_dest", p == d);
        o_lasterr();
        o_wstr("out", d);
        o_bool("untouched_after", d[4] == L'#');
        oracle_case_end();
        wmemset(d, L'#', 16);
        ORACLE_CALL(p, lstrcpynW(d, L"ab", 10));
        oracle_case("lstrcpyn_short");
        o_wstr("out", d);
        o_bool("untouched_after_nul", d[3] == L'#');
        oracle_case_end();
        wmemset(d, L'#', 16);
        ORACLE_CALL(p, lstrcpynW(d, L"ab", 1));
        oracle_case("lstrcpyn_size_1");
        o_bool("ret_is_dest", p == d);
        o_u32("first", d[0]);
        oracle_case_end();
        wmemset(d, L'#', 16);
        ORACLE_CALL(p, lstrcpynW(d, L"ab", 0));
        oracle_case("lstrcpyn_size_0");
        o_bool("ret_is_dest", p == d);
        o_bool("untouched", d[0] == L'#');
        oracle_case_end();
        ORACLE_CALL(p, lstrcpynW(d, NULL, 4));
        oracle_case("lstrcpyn_null_src");
        o_bool("ret_null", p == NULL);
        o_lasterr();
        oracle_case_end();
        wcscpy(d, L"ab");
        ORACLE_CALL(p, lstrcatW(d, L"cd"));
        oracle_case("lstrcat");
        o_bool("ret_is_dest", p == d);
        o_wstr("out", d);
        oracle_case_end();
        ORACLE_CALL(p, lstrcatW(d, NULL));
        oracle_case("lstrcat_null_src");
        o_bool("ret_null", p == NULL);
        o_lasterr();
        o_wstr("out", d);
        oracle_case_end();
    }

    /* ---- comparisons ---- */
    cmp_case("cmp_equal", L"abc", L"abc");
    cmp_case("cmp_case_only", L"abc", L"ABC");
    cmp_case("cmp_a_vs_B", L"a", L"B");
    cmp_case("cmp_prefix", L"ab", L"abc");
    cmp_case("cmp_hyphen_word_sort", L"co-op", L"coop");
    cmp_case("cmp_hyphen_vs_letter", L"co-op", L"cop");
    cmp_case("cmp_apostrophe", L"it's", L"its");
    cmp_case("cmp_digits_vs_letters", L"1", L"a");
    cmp_case("cmp_underscore_vs_letter", L"_", L"a");
    cmp_case("cmp_space_vs_letter", L" ", L"a");
    cmp_case("cmp_accent", L"é", L"f");
    cmp_case("cmp_accent_vs_plain", L"é", L"e");
    cmp_case("cmp_empty_vs_null", L"", NULL);
    cmp_case("cmp_null_null", NULL, NULL);
    cmp_case("cmp_sharp_s", L"ß", L"ss");
    cmp_case("cmp_ligature", L"æ", L"ae");
    cmp_case("cmp_greek_case", L"σ", L"Σ");
    cmp_case("cmp_turkish_i", L"i", L"İ");
    cmp_case("cmp_fullwidth_a", L"Ａ", L"A");
    {
        int r;
        ORACLE_CALL(r, CompareStringOrdinal(L"abc", 2, L"abd", 2, FALSE));
        oracle_case("ordinal_counted");
        o_i64("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, CompareStringOrdinal(NULL, 1, L"a", 1, FALSE));
        oracle_case("ordinal_null");
        o_i64("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, CompareStringOrdinal(L"a\0b", 3, L"a\0c", 3, FALSE));
        oracle_case("ordinal_embedded_nul");
        o_i64("ret", r);
        o_lasterr();
        oracle_case_end();
    }
    csx_case("csx_ignore_case", NORM_IGNORECASE, L"Hello", -1, L"hELLO", -1);
    csx_case("csx_ignore_nonspace", NORM_IGNORENONSPACE, L"résumé", -1, L"resume", -1);
    csx_case("csx_ignore_symbols", NORM_IGNORESYMBOLS, L"a-b.c", -1, L"abc", -1);
    csx_case("csx_ignore_width", NORM_IGNOREWIDTH, L"Ａ", -1, L"A", -1);
    csx_case("csx_ignore_kanatype", NORM_IGNOREKANATYPE, L"あ", -1, L"ア", -1);
    csx_case("csx_stringsort_hyphen", SORT_STRINGSORT, L"co-op", -1, L"coop", -1);
    csx_case("csx_digits_as_numbers", SORT_DIGITSASNUMBERS, L"a10", -1, L"a9", -1);
    csx_case("csx_no_digits_as_numbers", 0, L"a10", -1, L"a9", -1);
    csx_case("csx_counted_with_nul", 0, L"ab\0", 3, L"ab", 2);
    csx_case("csx_zero_lengths", 0, L"a", 0, L"b", 0);
    csx_case("csx_bad_flag", 0x80000000u, L"a", -1, L"a", -1);
    csx_case("csx_null_string", 0, NULL, -1, L"a", -1);
    csx_case("csx_linguistic_casing", LINGUISTIC_IGNORECASE, L"ABC", -1, L"abc", -1);
    {
        int r;
        ORACLE_CALL(r, CompareStringEx(L"xx-INVALID", 0, L"a", -1, L"a", -1, NULL, NULL, 0));
        oracle_case("csx_invalid_locale");
        o_i64("ret", r);
        o_lasterr();
        oracle_case_end();
    }

    /* ---- LCMapStringEx ---- */
    map_case("map_upper", LCMAP_UPPERCASE, L"abc éßσ", -1);
    map_case("map_lower", LCMAP_LOWERCASE, L"ABC ÉİΣ", -1);
    map_case("map_linguistic_upper", LCMAP_UPPERCASE | LCMAP_LINGUISTIC_CASING, L"iÿ", -1);
    map_case("map_fullwidth", LCMAP_FULLWIDTH, L"Ab1 ", -1);
    map_case("map_halfwidth", LCMAP_HALFWIDTH, L"Ａｂ１　", -1);
    map_case("map_hiragana", LCMAP_HIRAGANA, L"アカ", -1);
    map_case("map_katakana", LCMAP_KATAKANA, L"あか", -1);
    map_case("map_counted_no_nul", LCMAP_UPPERCASE, L"abcdef", 3);
    map_case("map_upper_and_lower", LCMAP_UPPERCASE | LCMAP_LOWERCASE, L"abc", -1);
    map_case("map_no_flags", 0, L"abc", -1);
    map_case("map_zero_length", LCMAP_UPPERCASE, L"abc", 0);
    {
        int r;
        ORACLE_CALL(r, LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, L"abcdef", -1, NULL, 0, NULL, NULL, 0));
        oracle_case("map_size_query");
        o_i64("ret", r);
        o_lasterr();
        oracle_case_end();
        wchar_t small[3];
        ORACLE_CALL(r, LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, L"abcdef", -1, small, 3, NULL, NULL, 0));
        oracle_case("map_buffer_too_small");
        o_i64("ret", r);
        o_lasterr();
        oracle_case_end();
        wchar_t same[8];
        wcscpy(same, L"abc");
        ORACLE_CALL(r, LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, same, -1, same, 8, NULL, NULL, 0));
        oracle_case("map_in_place");
        o_i64("ret", r);
        o_lasterr();
        o_wstr("out", same);
        oracle_case_end();
        ORACLE_CALL(r, LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_SORTKEY, L"abc", -1, NULL, 0, NULL, NULL, 0));
        oracle_case("map_sortkey_size");
        o_bool("ret_positive", r > 0);
        o_lasterr();
        oracle_case_end();
        BYTE k1[64], k2[64];
        int n1 = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_SORTKEY, L"abc", -1, (LPWSTR)k1, 64, NULL, NULL, 0);
        int n2 = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_SORTKEY | NORM_IGNORECASE, L"ABC", -1, (LPWSTR)k2, 64, NULL, NULL, 0);
        int n3 = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_SORTKEY, L"ABC", -1, (LPWSTR)k2 + 0, 64, NULL, NULL, 0);
        oracle_case("map_sortkey_order");
        o_bool("got", n1 > 0 && n2 > 0 && n3 > 0);
        o_str("abc_vs_ABC", n1 > 0 && n3 > 0 ? sign(memcmp(k1, k2, n1 < n3 ? n1 : n3)) : "n/a");
        oracle_case_end();
    }

    /* ---- GetStringTypeW ---- */
    {
        const wchar_t s[] = L"aZ5 _\té一٠Ａ!​";
        WORD t[16];
        BOOL ok;
        ORACLE_CALL(ok, GetStringTypeW(CT_CTYPE1, s, (int)wcslen(s), t));
        oracle_case("string_type_ctype1");
        o_bool("ret", ok);
        o_lasterr();
        char k[16];
        for (size_t i = 0; i < wcslen(s); i++) {
            snprintf(k, sizeof k, "c%zu", i);
            o_hex32(k, t[i]);
        }
        oracle_case_end();
        ORACLE_CALL(ok, GetStringTypeW(CT_CTYPE2, s, (int)wcslen(s), t));
        oracle_case("string_type_ctype2");
        o_bool("ret", ok);
        for (size_t i = 0; i < wcslen(s); i++) {
            snprintf(k, sizeof k, "c%zu", i);
            o_u32(k, t[i]);
        }
        oracle_case_end();
        ORACLE_CALL(ok, GetStringTypeW(CT_CTYPE3, s, (int)wcslen(s), t));
        oracle_case("string_type_ctype3");
        o_bool("ret", ok);
        for (size_t i = 0; i < wcslen(s); i++) {
            snprintf(k, sizeof k, "c%zu", i);
            o_hex32(k, t[i]);
        }
        oracle_case_end();
        ORACLE_CALL(ok, GetStringTypeW(3 + 4, s, 1, t));
        oracle_case("string_type_bad_type");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(ok, GetStringTypeW(CT_CTYPE1, s, 0, t));
        oracle_case("string_type_zero_length");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
    }

    /* ---- FormatMessageW from string ---- */
    fm_case("fm_plain", 0, L"hello", 256, 0);
    fm_case("fm_insert_string", 0, L"a %1 b", 256, 1, (DWORD_PTR)L"XY");
    fm_case("fm_insert_order", 0, L"%2-%1", 256, 2, (DWORD_PTR)L"one", (DWORD_PTR)L"two");
    fm_case("fm_insert_repeat", 0, L"%1%1", 256, 1, (DWORD_PTR)L"z");
    fm_case("fm_printf_d", 0, L"[%1!d!]", 256, 1, (DWORD_PTR)-42);
    fm_case("fm_printf_u", 0, L"[%1!u!]", 256, 1, (DWORD_PTR)0xFFFFFFFF);
    fm_case("fm_printf_x", 0, L"[%1!08x!]", 256, 1, (DWORD_PTR)0xBEEF);
    fm_case("fm_printf_X", 0, L"[%1!X!]", 256, 1, (DWORD_PTR)0xbeef);
    fm_case("fm_printf_width", 0, L"[%1!5s!]", 256, 1, (DWORD_PTR)L"ab");
    fm_case("fm_printf_left", 0, L"[%1!-5s!]", 256, 1, (DWORD_PTR)L"ab");
    fm_case("fm_printf_c", 0, L"[%1!c!]", 256, 1, (DWORD_PTR)L'Q');
    fm_case("fm_printf_star_width", 0, L"[%1!*s!]", 256, 2, (DWORD_PTR)6, (DWORD_PTR)L"ab");
    fm_case("fm_printf_star_then_next", 0, L"[%1!*s!][%3]", 256, 3, (DWORD_PTR)4, (DWORD_PTR)L"ab", (DWORD_PTR)L"c");
    fm_case("fm_printf_ls", 0, L"[%1!ls!]", 256, 1, (DWORD_PTR)L"wide");
    fm_case("fm_printf_hs", 0, L"[%1!hs!]", 256, 1, (DWORD_PTR)"narrow");
    fm_case("fm_escapes", 0, L"a%%b%.c%!d%n", 256, 0);
    fm_case("fm_escape_space_tab", 0, L"x% y%tz", 256, 0);
    fm_case("fm_percent_0_stops", 0, L"abc%0def", 256, 0);
    fm_case("fm_trailing_percent", 0, L"abc%", 256, 0);
    fm_case("fm_percent_unknown_letter", 0, L"a%qb", 256, 0);
    fm_case("fm_crlf_kept", 0, L"a\r\nb\nc\rd", 256, 0);
    fm_case("fm_insert_99", 0, L"%99", 256, 0);
    fm_case("fm_insert_100_literal", 0, L"%100", 256, 1, (DWORD_PTR)L"x");
    fm_case("fm_ignore_inserts", FORMAT_MESSAGE_IGNORE_INSERTS, L"a %1 %2!d! %% %n b", 256, 0);
    fm_case("fm_max_width_8", 8, L"one two three four five", 256, 0);
    fm_case("fm_max_width_mask", FORMAT_MESSAGE_MAX_WIDTH_MASK, L"one\r\ntwo  three\n", 256, 0);
    fm_case("fm_max_width_8_long_word", 8, L"abcdefghijklmnop q", 256, 0);
    fm_case("fm_buffer_exact", 0, L"hello", 6, 0);
    fm_case("fm_buffer_too_small", 0, L"hello", 5, 0);
    fm_case("fm_buffer_zero", 0, L"hello", 0, 0);
    fm_case("fm_empty_format", 0, L"", 256, 0);
    fm_case("fm_insert_without_args_flag_ok", 0, L"%1", 256, 1, (DWORD_PTR)L"");
    {
        wchar_t *p = NULL;
        DWORD r;
        DWORD_PTR args[1] = {(DWORD_PTR)L"alloc"};
        ORACLE_CALL(r, FormatMessageW(FORMAT_MESSAGE_FROM_STRING | FORMAT_MESSAGE_ARGUMENT_ARRAY | FORMAT_MESSAGE_ALLOCATE_BUFFER,
                                      L"x%1y", 0, 0, (LPWSTR)&p, 0, (va_list *)args));
        oracle_case("fm_allocate_buffer");
        o_u32("ret", r);
        o_lasterr();
        o_wstr("out", p);
        oracle_case_end();
        if (p) LocalFree(p);
        p = NULL;
        ORACLE_CALL(r, FormatMessageW(FORMAT_MESSAGE_FROM_STRING | FORMAT_MESSAGE_ALLOCATE_BUFFER, L"", 0, 0,
                                      (LPWSTR)&p, 0, NULL));
        oracle_case("fm_allocate_buffer_empty");
        o_u32("ret", r);
        o_lasterr();
        o_bool("out_null", p == NULL);
        oracle_case_end();
        if (p) LocalFree(p);
        ORACLE_CALL(r, FormatMessageW(0, L"abc", 0, 0, (LPWSTR)&p, 16, NULL));
        oracle_case("fm_no_source_flag");
        o_u32("ret", r);
        o_lasterr();
        oracle_case_end();
    }

    /* ---- system messages (English) ---- */
    sysmsg_case("sys_error_success", 0, 0);
    sysmsg_case("sys_file_not_found", ERROR_FILE_NOT_FOUND, 0);
    sysmsg_case("sys_access_denied", ERROR_ACCESS_DENIED, 0);
    sysmsg_case("sys_invalid_parameter", ERROR_INVALID_PARAMETER, 0);
    sysmsg_case("sys_sharing_violation", ERROR_SHARING_VIOLATION, 0);
    sysmsg_case("sys_already_exists", ERROR_ALREADY_EXISTS, 0);
    sysmsg_case("sys_more_data", ERROR_MORE_DATA, 0);
    sysmsg_case("sys_mod_not_found", ERROR_MOD_NOT_FOUND, 0);
    sysmsg_case("sys_insert_message_kept", ERROR_WRONG_DISK, 0);
    sysmsg_case("sys_file_not_found_max_width", ERROR_FILE_NOT_FOUND, FORMAT_MESSAGE_MAX_WIDTH_MASK);
    sysmsg_case("sys_missing_id", 0x2FFFFFFF, 0);
    sysmsg_case("sys_hresult_e_fail", 0x80004005, 0);
    sysmsg_case("sys_hresult_e_invalidarg", 0x80070057, 0);
    {
        wchar_t buf[64];
        DWORD r;
        ORACLE_CALL(r, FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL,
                                      ERROR_FILE_NOT_FOUND, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), buf, 10, NULL));
        oracle_case("sys_buffer_too_small");
        o_u32("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL,
                                      ERROR_FILE_NOT_FOUND, MAKELANGID(LANG_ZULU, SUBLANG_DEFAULT), buf, 64, NULL));
        oracle_case("sys_unknown_language");
        o_u32("ret", r);
        o_lasterr();
        oracle_case_end();
    }

    /* ---- probes: three-digit inserts, with distinct arguments ---- */
    {
        /* only inserts within the 100 arguments: %101 would read past them */
        static const wchar_t *fmts[] = {L"%100", L"%1000", L"%10!s!0", L"%9 %99"};
        static const char *names[] = {"probe_fm_100", "probe_fm_1000", "probe_fm_10_bang", "probe_fm_9_99"};
        static wchar_t vals[100][4];
        DWORD_PTR args[100];
        for (int i = 0; i < 100; i++) {
            swprintf(vals[i], 4, L"%d", i + 1);
            args[i] = (DWORD_PTR)vals[i];
        }
        for (int i = 0; i < 4; i++) {
            wchar_t buf[64];
            DWORD r;
            wmemset(buf, L'#', 64);
            ORACLE_CALL(r, FormatMessageW(FORMAT_MESSAGE_FROM_STRING | FORMAT_MESSAGE_ARGUMENT_ARRAY, fmts[i], 0, 0,
                                          buf, 64, (va_list *)args));
            oracle_case(names[i]);
            o_u32("ret", r);
            o_lasterr();
            if (r && r < 64) o_wstr("out", buf);
            oracle_case_end();
        }
    }
    {
        WORD t[4];
        BOOL ok;
        ORACLE_CALL(ok, GetStringTypeW(CT_CTYPE1, L"ab", -1, t));
        oracle_case("probe_string_type_minus_one");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(ok, GetStringTypeW(0, L"ab", 0, t));
        oracle_case("probe_string_type_bad_type_zero_length");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(ok, GetStringTypeW(CT_CTYPE1 | CT_CTYPE2, L"ab", 2, t));
        oracle_case("probe_string_type_two_types");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
    }
    map_case("probe_map_upper_lower_mixed", LCMAP_UPPERCASE | LCMAP_LOWERCASE, L"aBC dEF", -1);
    map_case("probe_map_upper_lower_counted", LCMAP_UPPERCASE | LCMAP_LOWERCASE, L"abc", 3);

    return oracle_end();
}
