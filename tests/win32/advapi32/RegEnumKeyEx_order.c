/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: the order in which RegEnumKeyExW returns subkeys. Keys are
 * created in scrambled order; the enumeration order shows how Windows sorts
 * them: prefixes, digits, punctuation around the letters, case, Latin-1 and
 * other non-ASCII letters, surrogate pairs vs high BMP characters, and the
 * order after renames, deletes and re-creation.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regenumkeyexw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regrenamekey
 */
#include "oracle.h"

typedef LONG(WINAPI *rename_fn)(HKEY, LPCWSTR, LPCWSTR);
static HKEY root;

static void escape_cat(char *out, size_t cap, const wchar_t *w, DWORD n)
{
    for (DWORD j = 0; j < n; j++) {
        char t[16];
        if (w[j] >= 0x20 && w[j] < 0x7f) { t[0] = (char)w[j]; t[1] = 0; }
        else snprintf(t, sizeof t, "\\u%04x", w[j]);
        strncat(out, t, cap - strlen(out) - 1);
    }
}

static void order_case(const char *name, HKEY k)
{
    char order[4096] = "";
    DWORD i;
    LONG st;
    for (i = 0;; i++) {
        wchar_t nm[300];
        DWORD n = 300;
        st = RegEnumKeyExW(k, i, nm, &n, NULL, NULL, NULL, NULL);
        if (st) break;
        if (i) strncat(order, "|", sizeof order - strlen(order) - 1);
        escape_cat(order, sizeof order, nm, n);
    }
    oracle_case(name);
    o_str("order", order);
    o_u32("count", i);
    o_status("final_ret", st);
    oracle_case_end();
}

static HKEY group(const wchar_t *gname, const wchar_t *const *names, size_t count)
{
    HKEY g, k;
    RegCreateKeyExW(root, gname, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &g, NULL);
    for (size_t i = 0; i < count; i++)
        if (!RegCreateKeyExW(g, names[i], 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL)) RegCloseKey(k);
    return g;
}

#define N(a) (sizeof(a) / sizeof *(a))

int main(void)
{
    oracle_begin("advapi32/RegEnumKeyEx_order");
    root = oracle_reg_root();

    static const wchar_t *prefixes[] = {L"abc", L"ab", L"abcd", L"a", L"ab_", L"ab0", L"ab ", L"ab-", L"ab.", L"ab~"};
    HKEY g = group(L"prefixes", prefixes, N(prefixes));
    order_case("prefixes", g);
    RegCloseKey(g);

    static const wchar_t *digits[] = {L"100", L"20", L"3", L"0", L"-1", L"+1", L"1a", L"1A0", L"01", L"9"};
    g = group(L"digits", digits, N(digits));
    order_case("digits", g);
    RegCloseKey(g);

    static const wchar_t *ascii[] = {L"!", L"#", L"$", L"%", L"&", L"'", L"(", L")", L",", L";", L"=", L"@",
                                     L"]", L"_", L"`", L"{", L"}", L"~", L"\x7f", L" x", L"Z", L"z", L"\x01"};
    g = group(L"ascii", ascii, N(ascii));
    order_case("ascii_punctuation", g);
    RegCloseKey(g);

    static const wchar_t *latin[] = {L"été", L"Ete", L"ÉTA", L"ß", L"ss", L"ÿ", L"Ÿ",
                                     L"æ", L"Æx", L"ø", L"×", L"÷", L"µ", L"ª",
                                     L"ı", L"İ", L"i", L"j"};
    g = group(L"latin", latin, N(latin));
    order_case("latin1_and_turkish", g);
    RegCloseKey(g);

    static const wchar_t *wide[] = {L"α", L"Αb", L"ς", L"σx", L"а", L"Аb", L"中",
                                    L"あ", L"ａ", L"Ａb", L"😀", L"𐀀", L"",
                                    L"�", L"K", L"k2"};
    g = group(L"wide", wide, N(wide));
    order_case("greek_cyrillic_cjk_surrogates", g);
    RegCloseKey(g);

    /* 40 keys created in reverse order. */
    HKEY many;
    RegCreateKeyExW(root, L"many", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &many, NULL);
    for (int i = 39; i >= 0; i--) {
        wchar_t nm[16];
        HKEY k;
        swprintf(nm, 16, L"k%c%d", L'a' + (i % 26), i);
        RegCreateKeyExW(many, nm, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL);
        RegCloseKey(k);
    }
    order_case("many_reverse_created", many);

    /* Changes: delete, re-create with other case, rename. */
    static const wchar_t *mod[] = {L"mike", L"alpha", L"tango", L"charlie"};
    g = group(L"mod", mod, N(mod));
    RegDeleteKeyW(g, L"alpha");
    {
        HKEY k;
        RegCreateKeyExW(g, L"ALPHA", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL);
        RegCloseKey(k);
    }
    order_case("after_recreate_other_case", g);
    rename_fn pRename = (rename_fn)(void (*)(void))GetProcAddress(GetModuleHandleW(L"advapi32.dll"), "RegRenameKey");
    if (pRename) {
        pRename(g, L"tango", L"bravo");
        order_case("after_rename", g);
        pRename(g, L"mike", L"MIKE");
        order_case("after_case_only_rename", g);
    }
    {
        HKEY k;
        RegCreateKeyExW(g, L"Zulu", 0, NULL, REG_OPTION_VOLATILE, KEY_ALL_ACCESS, NULL, &k, NULL);
        RegCloseKey(k);
        RegCreateKeyExW(g, L"delta", 0, NULL, REG_OPTION_VOLATILE, KEY_ALL_ACCESS, NULL, &k, NULL);
        RegCloseKey(k);
    }
    order_case("volatile_mixed_in", g);
    RegCloseKey(g);
    RegCloseKey(many);

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
