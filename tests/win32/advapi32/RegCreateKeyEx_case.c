/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: case-insensitive matching and case preservation of key names,
 * for ASCII and non-ASCII letters. Each case creates a key named A, then
 * calls RegCreateKeyExW with name B under the same parent: disposition 2
 * (REG_OPENED_EXISTING_KEY) means Windows treats A and B as the same name.
 * The stored names are read back with RegEnumKeyExW to check preservation.
 * Which letters fold is observed here, not assumed (Turkish dotted and
 * dotless i, Greek final sigma, German sharp s, Kelvin sign, titlecase
 * digraphs, fullwidth letters, letters outside the BMP...).
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/sysinfo/structure-of-the-registry
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regcreatekeyexw
 */
#include "oracle.h"

static HKEY root;

static void escape(const wchar_t *w, DWORD n, char *out, size_t cap)
{
    out[0] = 0;
    for (DWORD j = 0; j < n; j++) {
        char t[16];
        if (w[j] >= 0x20 && w[j] < 0x7f) { t[0] = (char)w[j]; t[1] = 0; }
        else snprintf(t, sizeof t, "\\u%04x", w[j]);
        strncat(out, t, cap - strlen(out) - 1);
    }
}

static void pair_case(const char *name, const wchar_t *a, const wchar_t *b)
{
    HKEY c = NULL, k = NULL;
    DWORD disp = 0;
    char list[512] = "";
    RegCreateKeyExW(root, L"c", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &c, NULL);
    RegCreateKeyExW(c, a, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL);
    RegCloseKey(k);
    LONG st;
    ORACLE_CALL(st, RegCreateKeyExW(c, b, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, &disp));
    if (!st) RegCloseKey(k);
    HKEY o = NULL;
    LONG ost = RegOpenKeyExW(c, b, 0, KEY_READ, &o);
    if (!ost) RegCloseKey(o);
    DWORD count = 0;
    for (DWORD i = 0;; i++) {
        wchar_t nm[64];
        char esc[256];
        DWORD n = 64;
        if (RegEnumKeyExW(c, i, nm, &n, NULL, NULL, NULL, NULL)) break;
        escape(nm, n, esc, sizeof esc);
        if (i) strncat(list, "|", sizeof list - strlen(list) - 1);
        strncat(list, esc, sizeof list - strlen(list) - 1);
        count++;
    }
    /* Deleting by the other spelling. */
    LONG dst = RegDeleteKeyW(c, b);
    oracle_case(name);
    o_status("ret", st);
    o_u32("disposition", disp);
    o_status("open_b", ost);
    o_u32("keys", count);
    o_str("stored", list);
    o_status("delete_b", dst);
    oracle_case_end();
    RegCloseKey(c);
    RegDeleteTreeW(root, L"c");
    RegDeleteKeyW(root, L"c");
}

int main(void)
{
    oracle_begin("advapi32/RegCreateKeyEx_case");
    root = oracle_reg_root();

    pair_case("ascii_lower_upper", L"abc", L"ABC");
    pair_case("ascii_mixed", L"MiXeD", L"mIxEd");
    pair_case("digits_symbols", L"a_1-[", L"A_1-[");
    pair_case("bracket_vs_brace", L"a[", L"a{");
    pair_case("at_vs_backtick", L"a@", L"a`");
    pair_case("latin1_e_acute", L"café", L"CAFÉ");
    pair_case("latin1_y_diaeresis", L"ÿ", L"Ÿ");
    pair_case("latin1_a_ring", L"å", L"Å");
    pair_case("angstrom_sign_vs_a_ring", L"Å", L"Å");
    pair_case("micro_vs_greek_mu_upper", L"µ", L"Μ");
    pair_case("micro_vs_greek_mu_lower", L"µ", L"μ");
    pair_case("sharp_s_vs_capital_sharp_s", L"ß", L"ẞ");
    pair_case("sharp_s_vs_ss", L"straße", L"STRASSE");
    pair_case("turkish_i_vs_I", L"i", L"I");
    pair_case("turkish_dotted_I_vs_i", L"İ", L"i");
    pair_case("turkish_dotted_I_vs_I", L"İ", L"I");
    pair_case("turkish_dotless_i_vs_I", L"ı", L"I");
    pair_case("turkish_dotless_i_vs_i", L"ı", L"i");
    pair_case("greek_sigma_vs_capital", L"σ", L"Σ");
    pair_case("greek_final_sigma_vs_capital", L"ς", L"Σ");
    pair_case("greek_final_sigma_vs_sigma", L"ς", L"σ");
    pair_case("greek_accented_alpha", L"ά", L"Ά");
    pair_case("greek_iota_subscript", L"ͅ", L"Ι");
    pair_case("long_s_vs_S", L"ſ", L"S");
    pair_case("kelvin_vs_k", L"K", L"k");
    pair_case("kelvin_vs_K", L"K", L"K");
    pair_case("ohm_vs_omega", L"Ω", L"ω");
    pair_case("titlecase_dz", L"ǅ", L"Ǆ");
    pair_case("titlecase_dz_lower", L"ǆ", L"Ǆ");
    pair_case("cyrillic_a", L"а", L"А");
    pair_case("cyrillic_yo", L"ё", L"Ё");
    pair_case("armenian_ayb", L"ա", L"Ա");
    pair_case("georgian_an_mkhedruli", L"ა", L"Ა");
    pair_case("cherokee_a", L"ꭰ", L"Ꭰ");
    pair_case("glagolitic_az", L"ⰰ", L"Ⰰ");
    pair_case("latin_ext_a_dot_below", L"ạ", L"Ạ");
    pair_case("latin_ext_b_turned_alpha", L"ɒ", L"Ɒ");
    pair_case("roman_numeral_one", L"ⅰ", L"Ⅰ");
    pair_case("circled_a", L"ⓐ", L"Ⓐ");
    pair_case("fullwidth_a", L"ａ", L"Ａ");
    pair_case("fullwidth_vs_ascii", L"ａ", L"a");
    pair_case("deseret_long_i", L"\xd801\xdc28", L"\xd801\xdc00");
    pair_case("precomposed_vs_combining", L"é", L"é");
    pair_case("ligature_fi_vs_FI", L"ﬁ", L"FI");
    pair_case("lone_surrogates_case", L"\xd801", L"\xd801");

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
