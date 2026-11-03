/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: per-thread locale settings: SetThreadLocale / GetThreadLocale
 * (valid, invalid, sort-specific LCIDs, the effect on CP_THREAD_ACP and on
 * MultiByteToWideChar), SetThreadUILanguage / GetThreadUILanguage,
 * SetThreadPreferredUILanguages / GetThreadPreferredUILanguages (names and
 * IDs, buffer rules; only the thread's own list is read), and ConvertDefaultLocale. Machine defaults are only
 * compared with each other, never printed.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winnls/nf-winnls-setthreadlocale
 *       https://learn.microsoft.com/en-us/windows/win32/api/winnls/nf-winnls-getthreadlocale
 *       https://learn.microsoft.com/en-us/windows/win32/api/winnls/nf-winnls-setthreaduilanguage
 *       https://learn.microsoft.com/en-us/windows/win32/api/winnls/nf-winnls-setthreadpreferreduilanguages
 *       https://learn.microsoft.com/en-us/windows/win32/api/winnls/nf-winnls-getthreadpreferreduilanguages
 *       https://learn.microsoft.com/en-us/windows/win32/api/winnls/nf-winnls-convertdefaultlocale
 *       https://learn.microsoft.com/en-us/windows/win32/intl/code-page-identifiers
 */
#include "oracle.h"

/* Defaults are machine settings, so for those only relationships are printed. */
static void set_locale_ex(const char *name, LCID lcid, int relative)
{
    BOOL r;
    ORACLE_CALL(r, SetThreadLocale(lcid));
    oracle_case(name);
    o_bool("ret", r);
    o_lasterr();
    if (relative) {
        o_bool("now_is_user_default", GetThreadLocale() == GetUserDefaultLCID());
        o_bool("now_is_system_default", GetThreadLocale() == GetSystemDefaultLCID());
    } else
        o_hex32("now", GetThreadLocale());
    CPINFOEXW ci;
    if (!relative && GetCPInfoExW(CP_THREAD_ACP, 0, &ci)) o_u32("thread_acp", ci.CodePage);
    oracle_case_end();
}

static void set_locale(const char *name, LCID lcid)
{
    set_locale_ex(name, lcid, 0);
}

static void conv(const char *name, LCID in)
{
    LCID r;
    ORACLE_CALL(r, ConvertDefaultLocale(in));
    oracle_case(name);
    o_hex32("ret", r);
    o_lasterr();
    oracle_case_end();
}

static void o_multi(const char *k, const wchar_t *buf, ULONG n)
{
    wchar_t tmp[256];
    ULONG i;
    for (i = 0; i < n && i < 255; i++) tmp[i] = buf[i] ? buf[i] : L'|';
    tmp[i] = 0;
    o_wstr(k, tmp);
}

static void get_pref(const char *name, DWORD flags, ULONG size)
{
    wchar_t buf[256];
    ULONG num = 0xdead, sz = size;
    BOOL r;
    wmemset(buf, L'#', 256);
    ORACLE_CALL(r, GetThreadPreferredUILanguages(flags | MUI_THREAD_LANGUAGES, &num, size ? buf : NULL, &sz));
    oracle_case(name);
    o_bool("ret", r);
    o_lasterr();
    o_u32("num", num);
    o_u32("size", sz);
    if (r && size) o_multi("list", buf, sz);
    oracle_case_end();
}

static void set_pref(const char *name, DWORD flags, const wchar_t *list)
{
    ULONG num = 0xdead;
    BOOL r;
    ORACLE_CALL(r, SetThreadPreferredUILanguages(flags, list, &num));
    oracle_case(name);
    o_bool("ret", r);
    o_lasterr();
    o_u32("num", num);
    oracle_case_end();
}

int main(void)
{
    oracle_begin("kernel32/thread_locale");

    LCID initial = GetThreadLocale();
    oracle_case("initial");
    o_bool("is_user_default", initial == GetUserDefaultLCID());
    oracle_case_end();

    set_locale("set_ja_jp", 0x0411);
    {
        WCHAR w[4] = { 0 };
        int r = MultiByteToWideChar(CP_THREAD_ACP, 0, "\x82\xa0", 2, w, 4);
        oracle_case("thread_acp_decodes_sjis");
        o_i64("ret", r);
        o_hex32("w0", w[0]);
        oracle_case_end();
    }
    set_locale("set_ru_ru", 0x0419);
    set_locale("set_de_phonebook", MAKELCID(0x0407, SORT_GERMAN_PHONE_BOOK));
    set_locale("set_neutral_lang", 0x0009);
    set_locale("set_bogus", 0x7fff);
    set_locale("set_invariant", LOCALE_INVARIANT);
    set_locale_ex("set_user_default_constant", LOCALE_USER_DEFAULT, 1);
    oracle_case("after_user_default_constant");
    o_bool("is_user_default", GetThreadLocale() == GetUserDefaultLCID());
    oracle_case_end();
    set_locale_ex("set_system_default_constant", LOCALE_SYSTEM_DEFAULT, 1);
    oracle_case("after_system_default_constant");
    o_bool("is_system_default", GetThreadLocale() == GetSystemDefaultLCID());
    oracle_case_end();
    set_locale_ex("set_zero", 0, 1);
    SetThreadLocale(initial);

    conv("convert_0409", 0x0409);
    conv("convert_lang_english", 0x0009);
    conv("convert_lang_german", 0x0007);
    conv("convert_lang_chinese", 0x0004);
    conv("convert_lang_serbian", 0x001a);
    conv("convert_zh_hant_7c04", 0x7c04);
    conv("convert_sublang_default_spanish", MAKELANGID(LANG_SPANISH, SUBLANG_DEFAULT));
    conv("convert_sublang_neutral_spanish", MAKELANGID(LANG_SPANISH, SUBLANG_NEUTRAL));
    conv("convert_sort_kept", MAKELCID(0x0407, SORT_GERMAN_PHONE_BOOK));
    conv("convert_invariant", LOCALE_INVARIANT);
    conv("convert_bogus", 0x7fff);
    {
        oracle_case("convert_defaults");
        o_bool("neutral_is_user", ConvertDefaultLocale(LOCALE_NEUTRAL) == GetUserDefaultLCID());
        o_bool("user_is_user", ConvertDefaultLocale(LOCALE_USER_DEFAULT) == GetUserDefaultLCID());
        o_bool("system_is_system", ConvertDefaultLocale(LOCALE_SYSTEM_DEFAULT) == GetSystemDefaultLCID());
        oracle_case_end();
    }

    /* UI language. */
    {
        LANGID l;
        LANGID start = GetThreadUILanguage();
        oracle_case("ui_initial");
        o_bool("is_user_ui", start == GetUserDefaultUILanguage());
        oracle_case_end();
        ORACLE_CALL(l, SetThreadUILanguage(MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US)));
        oracle_case("ui_set_en_us");
        o_hex32("ret", l);
        o_lasterr();
        o_hex32("now", GetThreadUILanguage());
        oracle_case_end();
        ORACLE_CALL(l, SetThreadUILanguage(MAKELANGID(LANG_GERMAN, SUBLANG_GERMAN)));
        oracle_case("ui_set_de_de");
        o_hex32("ret", l);
        o_lasterr();
        o_hex32("now", GetThreadUILanguage());
        oracle_case_end();
        ORACLE_CALL(l, SetThreadUILanguage(0x7fff));
        oracle_case("ui_set_bogus");
        o_hex32("ret", l);
        o_lasterr();
        o_hex32("now", GetThreadUILanguage());
        oracle_case_end();
        ORACLE_CALL(l, SetThreadUILanguage(0));
        oracle_case("ui_set_zero");
        o_bool("ret_is_user_ui", l == GetUserDefaultUILanguage());
        o_lasterr();
        oracle_case_end();
    }

    /* Preferred UI languages. */
    get_pref("pref_initial_names", MUI_LANGUAGE_NAME, 0);
    set_pref("pref_set_names", MUI_LANGUAGE_NAME, L"de-DE\0fr-FR\0");
    get_pref("pref_get_names", MUI_LANGUAGE_NAME, 256);
    get_pref("pref_get_ids", MUI_LANGUAGE_ID, 256);
    get_pref("pref_size_query", MUI_LANGUAGE_NAME, 0);
    get_pref("pref_buffer_too_small", MUI_LANGUAGE_NAME, 4);
    get_pref("pref_both_flags_rejected", MUI_LANGUAGE_NAME | MUI_LANGUAGE_ID, 256);
    set_pref("pref_set_ids", MUI_LANGUAGE_ID, L"0409\0");
    get_pref("pref_get_names_after_ids", MUI_LANGUAGE_NAME, 256);
    set_pref("pref_set_bad_name", MUI_LANGUAGE_NAME, L"xx-YY\0");
    get_pref("pref_after_bad_name", MUI_LANGUAGE_NAME, 256);
    set_pref("pref_set_both_flags", MUI_LANGUAGE_NAME | MUI_LANGUAGE_ID, L"en-US\0");
    set_pref("pref_clear", MUI_LANGUAGE_NAME, NULL);
    {
        ULONG num = 0, sz = 0;
        BOOL r = GetThreadPreferredUILanguages(MUI_LANGUAGE_NAME | MUI_THREAD_LANGUAGES, &num, NULL, &sz);
        oracle_case("pref_after_clear_thread_only");
        o_bool("ret", r);
        o_u32("num", num);
        o_u32("size", sz);
        oracle_case_end();
    }

    return oracle_end();
}
