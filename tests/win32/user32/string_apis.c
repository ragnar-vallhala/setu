/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: user32 character and string helpers: CharUpperW/CharLowerW
 * (string and single-character forms), CharUpperBuffW/CharLowerBuffW,
 * CharNextW/CharPrevW, IsCharAlphaW/AlphaNumeric/Upper/Lower, and
 * wsprintfW/wsprintfA formatting (flags, widths, precision, types, the
 * 1024-character limit).
 *
 * user32 is not linked into oracle tests, so it is loaded with LoadLibraryW
 * and every function is fetched with GetProcAddress.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-charupperw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-charlowerw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-charupperbuffw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-charnextw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-charprevw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-ischaralphaw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-ischaralphanumericw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-ischarupperw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-ischarlowerw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-wsprintfw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-wsprintfa
 *       https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-wvsprintfw
 */
#include "oracle.h"

static LPWSTR (WINAPI *pCharUpperW)(LPWSTR);
static LPWSTR (WINAPI *pCharLowerW)(LPWSTR);
static DWORD (WINAPI *pCharUpperBuffW)(LPWSTR, DWORD);
static DWORD (WINAPI *pCharLowerBuffW)(LPWSTR, DWORD);
static LPWSTR (WINAPI *pCharNextW)(LPCWSTR);
static LPWSTR (WINAPI *pCharPrevW)(LPCWSTR, LPCWSTR);
static BOOL (WINAPI *pIsCharAlphaW)(WCHAR);
static BOOL (WINAPI *pIsCharAlphaNumericW)(WCHAR);
static BOOL (WINAPI *pIsCharUpperW)(WCHAR);
static BOOL (WINAPI *pIsCharLowerW)(WCHAR);
static int (WINAPIV *pwsprintfW)(LPWSTR, LPCWSTR, ...);
static int (WINAPIV *pwsprintfA)(LPSTR, LPCSTR, ...);

#define LOAD(m, f) (*(FARPROC *)&p##f = GetProcAddress(m, #f))

static void upper_lower_case(const char *name, const wchar_t *s)
{
    wchar_t u[64], l[64];
    wcscpy(u, s);
    wcscpy(l, s);
    LPWSTR ru = pCharUpperW(u), rl = pCharLowerW(l);
    oracle_case(name);
    o_bool("upper_ret_is_buf", ru == u);
    o_bool("lower_ret_is_buf", rl == l);
    o_wstr("upper", u);
    o_wstr("lower", l);
    o_bytes("upper_raw", u, wcslen(u) * 2);
    o_bytes("lower_raw", l, wcslen(l) * 2);
    oracle_case_end();
}

static void char_class_case(const char *name, wchar_t c)
{
    ULONG_PTR up = (ULONG_PTR)pCharUpperW((LPWSTR)(ULONG_PTR)c);
    ULONG_PTR lo = (ULONG_PTR)pCharLowerW((LPWSTR)(ULONG_PTR)c);
    oracle_case(name);
    o_bool("alpha", pIsCharAlphaW(c));
    o_bool("alnum", pIsCharAlphaNumericW(c));
    o_bool("upper", pIsCharUpperW(c));
    o_bool("lower", pIsCharLowerW(c));
    o_hex32("to_upper", (DWORD)up);
    o_hex32("to_lower", (DWORD)lo);
    oracle_case_end();
}

#define WSPW(name, ...)                                \
    do {                                               \
        wchar_t b_[1100];                              \
        wmemset(b_, L'#', 1100);                       \
        int r_ = pwsprintfW(b_, __VA_ARGS__);          \
        oracle_case(name);                             \
        o_i64("ret", r_);                              \
        o_bool("ret_is_len", r_ == (int)wcslen(b_));   \
        o_wstr("out", b_);                             \
        oracle_case_end();                             \
    } while (0)

#define WSPA(name, ...)                                \
    do {                                               \
        char b_[1100];                                 \
        memset(b_, '#', 1100);                         \
        int r_ = pwsprintfA(b_, __VA_ARGS__);          \
        oracle_case(name);                             \
        o_i64("ret", r_);                              \
        o_bool("ret_is_len", r_ == (int)strlen(b_));   \
        o_bytes("out", b_, strlen(b_));                \
        oracle_case_end();                             \
    } while (0)

int main(void)
{
    oracle_begin("user32/string_apis");

    HMODULE u32;
    ORACLE_CALL(u32, LoadLibraryW(L"user32.dll"));
    oracle_case("load_user32");
    o_handle("ret", u32);
    oracle_case_end();
    if (!u32) return oracle_end();
    LOAD(u32, CharUpperW);
    LOAD(u32, CharLowerW);
    LOAD(u32, CharUpperBuffW);
    LOAD(u32, CharLowerBuffW);
    LOAD(u32, CharNextW);
    LOAD(u32, CharPrevW);
    LOAD(u32, IsCharAlphaW);
    LOAD(u32, IsCharAlphaNumericW);
    LOAD(u32, IsCharUpperW);
    LOAD(u32, IsCharLowerW);
    LOAD(u32, wsprintfW);
    LOAD(u32, wsprintfA);

    /* ---- CharUpper / CharLower on strings ---- */
    upper_lower_case("case_ascii", L"Hello, World 123!");
    upper_lower_case("case_latin1", L"àéîõüÿÀÉß");
    upper_lower_case("case_greek", L"αβσςΑΒΣ");
    upper_lower_case("case_cyrillic", L"абёАБЁ");
    upper_lower_case("case_turkish_i", L"iıIİ");
    upper_lower_case("case_fullwidth", L"ａＡ");
    upper_lower_case("case_titlecase_dz", L"Ǆǅǆ");
    upper_lower_case("case_micro_sign", L"µ");
    upper_lower_case("case_empty", L"");

    /* ---- Buff variants ---- */
    {
        wchar_t b[16];
        DWORD r;
        wcscpy(b, L"abcdef");
        ORACLE_CALL(r, pCharUpperBuffW(b, 3));
        oracle_case("upper_buff_partial");
        o_u32("ret", r);
        o_lasterr();
        o_wstr("out", b);
        oracle_case_end();
        wcscpy(b, L"ab");
        b[2] = L'c';
        b[3] = 0;
        b[1] = 0;
        ORACLE_CALL(r, pCharUpperBuffW(b, 3));
        oracle_case("upper_buff_through_nul");
        o_u32("ret", r);
        o_bytes("raw", b, 6);
        oracle_case_end();
        wcscpy(b, L"ABC");
        ORACLE_CALL(r, pCharLowerBuffW(b, 0));
        oracle_case("lower_buff_zero");
        o_u32("ret", r);
        o_lasterr();
        o_wstr("out", b);
        oracle_case_end();
    }

    /* ---- CharNext / CharPrev ---- */
    {
        const wchar_t s[] = L"áb";
        LPWSTR p;
        p = pCharNextW(s);
        oracle_case("char_next_combining");
        o_i64("offset", p - s);
        oracle_case_end();
        p = pCharNextW(s + 3);
        oracle_case("char_next_at_nul");
        o_i64("offset", p - s);
        oracle_case_end();
        p = pCharPrevW(s, s + 3);
        oracle_case("char_prev_combining");
        o_i64("offset", p - s);
        oracle_case_end();
        p = pCharPrevW(s, s);
        oracle_case("char_prev_at_start");
        o_i64("offset", p - s);
        oracle_case_end();
        const wchar_t sur[] = L"\xd83d\xde00x";
        p = pCharNextW(sur);
        oracle_case("char_next_surrogate_pair");
        o_i64("offset", p - sur);
        oracle_case_end();
    }

    /* ---- single characters ---- */
    char_class_case("char_a", L'a');
    char_class_case("char_Z", L'Z');
    char_class_case("char_5", L'5');
    char_class_case("char_underscore", L'_');
    char_class_case("char_space", L' ');
    char_class_case("char_e_acute", 0x00e9);
    char_class_case("char_sharp_s", 0x00df);
    char_class_case("char_y_diaeresis", 0x00ff);
    char_class_case("char_dotless_i", 0x0131);
    char_class_case("char_dotted_I", 0x0130);
    char_class_case("char_final_sigma", 0x03c2);
    char_class_case("char_cjk", 0x4e00);
    char_class_case("char_arabic_digit", 0x0660);
    char_class_case("char_superscript_2", 0x00b2);
    char_class_case("char_roman_numeral", 0x2160);
    char_class_case("char_titlecase_Dz", 0x01c5);
    char_class_case("char_combining_acute", 0x0301);
    char_class_case("char_nul", 0);

    /* ---- wsprintfW ---- */
    WSPW("w_d", L"%d|%i|%u", -5, -5, -5);
    WSPW("w_x", L"%x|%X|%#x|%#X|%08x", 255, 255, 255, 255, 255);
    WSPW("w_width_flags", L"[%5d][%-5d][%05d][%+d][% d]", 42, 42, 42, 42, 42);
    WSPW("w_long", L"%ld|%lu|%lx|%li", -7L, 7UL, 0xabcUL, 3L);
    WSPW("w_I64", L"%I64d|%I64u|%I64x", (long long)-1, (unsigned long long)-1, 0x123456789abcULL);
    WSPW("w_strings", L"[%s][%ls][%hs][%S]", L"wide", L"wide2", "narrow", "narrow2");
    WSPW("w_string_precision", L"[%.3s][%10.2s][%-6s]", L"abcdef", L"xyz", L"ab");
    WSPW("w_null_string", L"[%s]", (wchar_t *)NULL);
    WSPW("w_chars", L"[%c][%hc][%lc][%C]", L'W', 'n', L'L', 'C');
    WSPW("w_percent", L"100%% [%]", 0);
    WSPW("w_unsupported_f", L"[%f][%d]", 1LL, 2LL);
    WSPW("w_unsupported_e_p", L"[%e][%p]", 1LL, 0x1234LL);
    WSPW("w_star_width", L"[%*d]", 5, 1);
    WSPW("w_precision_int", L"[%.5d][%.0d]", 42, 0);
    WSPW("w_hh_h", L"[%hd][%hu][%hx]", 0x12345, 0x12345, 0x12345);
    WSPW("w_z_and_ll", L"[%zd][%lld]", 1LL, 2LL);
    {
        wchar_t big[1200];
        for (int i = 0; i < 1199; i++) big[i] = L'a' + (i % 26);
        big[1199] = 0;
        wchar_t b[1100];
        wmemset(b, L'#', 1100);
        int r = pwsprintfW(b, L"%s", big);
        oracle_case("w_limit_1024");
        o_i64("ret", r);
        o_u32("strlen", (DWORD)wcsnlen(b, 1100));
        o_bool("untouched_1025", b[1025] == L'#');
        oracle_case_end();
    }

    /* ---- wsprintfA ---- */
    WSPA("a_d", "%d|%u|%x|%X", -5, -5, 0xab, 0xab);
    WSPA("a_strings", "[%s][%hs][%ls][%S]", "narrow", "narrow2", L"wide", L"wide2");
    WSPA("a_non_ascii_wide", "[%ls]", L"é一");
    WSPA("a_chars", "[%c][%lc][%C]", 'a', L'é', L'B');
    WSPA("a_width", "[%-4s][%4s][%.1s]", "ab", "ab", "ab");

    FreeLibrary(u32);
    return oracle_end();
}
