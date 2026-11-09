/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: the ICU that Windows ships in system32 (icu.dll, and the
 * older icuuc.dll / icuin.dll). Loads each DLL, then calls a few of the
 * documented C functions through icu.dll: u_getVersion and the data
 * versions, u_errorName, ucol_open + ucol_strcoll (en-US, Swedish,
 * strengths), ubrk word and line breaks, udat_open + udat_format for
 * en-US (styles and a pattern, in UTC), unum number and currency
 * formatting, case mapping, normalisation, character names and scripts,
 * converters, regular expressions, list and plural rules, and the
 * Windows time zone ID mapping. Also checks which of the documented
 * names the two older DLLs export, that the names carry no ICU version
 * suffix, and that the 32-bit copies exist in the WOW64 system folder.
 *
 * Windows ICU has no public header in MinGW, so the few prototypes used
 * here are declared below from the ICU C API reference.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/intl/international-components-for-unicode--icu-
 *       https://unicode-org.github.io/icu-docs/apidoc/released/icu4c/ (C API)
 */
#include "oracle.h"
#include <stdint.h>

typedef int UErrorCode; /* enum */
typedef wchar_t UChar;  /* char16_t in icu.h; same size and ABI */
typedef int8_t UBool;
typedef double UDate;
#define U_ZERO_ERROR 0
#define U_BUFFER_OVERFLOW_ERROR 15
#define U_FAILURE(x) ((x) > U_ZERO_ERROR)
#define UBRK_DONE (-1)

static void (*pu_getVersion)(uint8_t *);
static void (*pu_getUnicodeVersion)(uint8_t *);
static void (*pulocdata_getCLDRVersion)(uint8_t *, UErrorCode *);
static void (*pucol_getUCAVersion)(const void *, uint8_t *);
static const char *(*pu_errorName)(UErrorCode);
static const char *(*puloc_getDefault)(void);
static int32_t (*puloc_toLanguageTag)(const char *, char *, int32_t, UBool, UErrorCode *);
static void *(*pucol_open)(const char *, UErrorCode *);
static void (*pucol_close)(void *);
static int (*pucol_strcoll)(const void *, const UChar *, int32_t, const UChar *, int32_t);
static void (*pucol_setStrength)(void *, int);
static void *(*pubrk_open)(int, const char *, const UChar *, int32_t, UErrorCode *);
static void (*pubrk_close)(void *);
static int32_t (*pubrk_first)(void *);
static int32_t (*pubrk_next)(void *);
static int32_t (*pubrk_getRuleStatus)(void *);
static void *(*pudat_open)(int, int, const char *, const UChar *, int32_t, const UChar *, int32_t, UErrorCode *);
static void (*pudat_close)(void *);
static int32_t (*pudat_format)(const void *, UDate, UChar *, int32_t, void *, UErrorCode *);
static void *(*punum_open)(int, const UChar *, int32_t, const char *, void *, UErrorCode *);
static void (*punum_close)(void *);
static int32_t (*punum_formatDouble)(const void *, double, UChar *, int32_t, void *, UErrorCode *);
static int32_t (*punum_formatDoubleCurrency)(const void *, double, UChar *, UChar *, int32_t, void *, UErrorCode *);
static int32_t (*pu_strToUpper)(UChar *, int32_t, const UChar *, int32_t, const char *, UErrorCode *);
static int32_t (*pu_strToLower)(UChar *, int32_t, const UChar *, int32_t, const char *, UErrorCode *);
static const void *(*punorm2_getNFCInstance)(UErrorCode *);
static const void *(*punorm2_getNFDInstance)(UErrorCode *);
static int32_t (*punorm2_normalize)(const void *, const UChar *, int32_t, UChar *, int32_t, UErrorCode *);
static int32_t (*pu_charName)(int32_t, int, char *, int32_t, UErrorCode *);
static int (*puscript_getScript)(int32_t, UErrorCode *);
static const char *(*puscript_getName)(int);
static void *(*pucnv_open)(const char *, UErrorCode *);
static void (*pucnv_close)(void *);
static int32_t (*pucnv_fromUChars)(void *, char *, int32_t, const UChar *, int32_t, UErrorCode *);
static int32_t (*pucnv_toUChars)(void *, UChar *, int32_t, const char *, int32_t, UErrorCode *);
static void *(*puregex_openC)(const char *, uint32_t, void *, UErrorCode *);
static void (*puregex_close)(void *);
static void (*puregex_setText)(void *, const UChar *, int32_t, UErrorCode *);
static UBool (*puregex_find)(void *, int32_t, UErrorCode *);
static int32_t (*puregex_start)(void *, int32_t, UErrorCode *);
static int32_t (*puregex_end)(void *, int32_t, UErrorCode *);
static void *(*pulistfmt_open)(const char *, UErrorCode *);
static void (*pulistfmt_close)(void *);
static int32_t (*pulistfmt_format)(const void *, const UChar *const *, const int32_t *, int32_t, UChar *, int32_t, UErrorCode *);
static void *(*puplrules_open)(const char *, UErrorCode *);
static void (*puplrules_close)(void *);
static int32_t (*puplrules_select)(const void *, double, UChar *, int32_t, UErrorCode *);
static int32_t (*pucal_getWindowsTimeZoneID)(const UChar *, int32_t, UChar *, int32_t, UErrorCode *);
static int32_t (*pucal_getTimeZoneIDForWindowsID)(const UChar *, int32_t, const char *, UChar *, int32_t, UErrorCode *);

#define LOAD(m, f) (*(FARPROC *)&p##f = GetProcAddress(m, #f))

static int sgn(int v) { return v < 0 ? -1 : v > 0; }

static void o_version(const char *k, const uint8_t v[4])
{
    char t[32];
    snprintf(t, sizeof t, "%u.%u.%u.%u", v[0], v[1], v[2], v[3]);
    o_str(k, t);
}

static void o_err(UErrorCode e)
{
    o_str("status", pu_errorName ? pu_errorName(e) : "?");
}

static const char *const documented[] = {
    "u_getVersion", "u_strlen", "u_errorName", "uloc_getDefault", "ubrk_open",
    "ucnv_open", "unorm2_getNFCInstance", "uscript_getScript", /* common */
    "ucol_open", "ucol_strcoll", "udat_open", "udat_format", "unum_open",
    "uregex_openC", "ucal_open", "usearch_open", /* i18n */
    "ucal_getHostTimeZone", "unumrf_close", "uset_hasStrings", /* newer */
};

/* Which names the two older DLLs still have: the ICU version each first
 * appeared in (ICU's header annotations) brackets the version Windows
 * stopped adding to them at (no new APIs there after Windows 10 1903). */
static const char *const old_dll_probe[] = {
    "u_getBidiPairedBracket",         /* ICU 52 */
    "ubiditransform_open",            /* 58 */
    "ubrk_openBinaryRules",           /* 59 */
    "unum_formatDoubleForFields",     /* 59 */
    "udatpg_getFieldDisplayName",     /* 61 */
    "unumf_openForSkeletonAndLocale", /* 62 */
    "unumf_close",                    /* 62 */
    "ucptrie_get",                    /* 63 */
    "umutablecptrie_open",            /* 63 */
    "u_getIntPropertyMap",            /* 63 */
    "ucfpos_open",                    /* 64 */
    "ufmtval_getString",              /* 64 */
    "uloc_openAvailableByType",       /* 65 */
    "ulistfmt_openForType",           /* 67 */
    "ubrk_safeClone",                 /* old, deprecated in 69 */
    "ucnv_safeClone",                 /* old, deprecated in 71 */
    "ucol_safeClone",                 /* old, deprecated in 71 */
    "utf8_nextCharSafeBody",          /* internal, no version */
    "ucptrie_internalSmallIndex",     /* internal, with ucptrie (63) */
};

int main(void)
{
    oracle_begin("icu/icu_basic");
    HMODULE icu, uc, in;

    oracle_case("load");
    ORACLE_CALL(icu, LoadLibraryW(L"icu.dll"));
    o_bool("icu", icu != NULL);
    ORACLE_CALL(uc, LoadLibraryW(L"icuuc.dll"));
    o_bool("icuuc", uc != NULL);
    ORACLE_CALL(in, LoadLibraryW(L"icuin.dll"));
    o_bool("icuin", in != NULL);
    oracle_case_end();
    if (!icu) return oracle_end();

    oracle_case("exports");
    for (size_t i = 0; i < ARRAYSIZE(documented); i++) {
        char k[96];
        snprintf(k, sizeof k, "icu.%s", documented[i]);
        o_bool(k, GetProcAddress(icu, documented[i]) != NULL);
        snprintf(k, sizeof k, "icuuc.%s", documented[i]);
        o_bool(k, uc && GetProcAddress(uc, documented[i]) != NULL);
        snprintf(k, sizeof k, "icuin.%s", documented[i]);
        o_bool(k, in && GetProcAddress(in, documented[i]) != NULL);
    }
    oracle_case_end();

    /* "icu": in icu.dll; "old": in icuuc.dll or icuin.dll */
    oracle_case("old_dll_exports");
    for (size_t i = 0; i < ARRAYSIZE(old_dll_probe); i++) {
        char k[96];
        snprintf(k, sizeof k, "%s.icu", old_dll_probe[i]);
        o_bool(k, GetProcAddress(icu, old_dll_probe[i]) != NULL);
        snprintf(k, sizeof k, "%s.old", old_dll_probe[i]);
        o_bool(k, (uc && GetProcAddress(uc, old_dll_probe[i])) ||
                  (in && GetProcAddress(in, old_dll_probe[i])));
    }
    oracle_case_end();

    LOAD(icu, u_getVersion);
    LOAD(icu, u_getUnicodeVersion);
    LOAD(icu, ulocdata_getCLDRVersion);
    LOAD(icu, ucol_getUCAVersion);
    LOAD(icu, u_errorName);
    LOAD(icu, uloc_getDefault);
    LOAD(icu, uloc_toLanguageTag);
    LOAD(icu, ucol_open);
    LOAD(icu, ucol_close);
    LOAD(icu, ucol_strcoll);
    LOAD(icu, ucol_setStrength);
    LOAD(icu, ubrk_open);
    LOAD(icu, ubrk_close);
    LOAD(icu, ubrk_first);
    LOAD(icu, ubrk_next);
    LOAD(icu, ubrk_getRuleStatus);
    LOAD(icu, udat_open);
    LOAD(icu, udat_close);
    LOAD(icu, udat_format);
    LOAD(icu, unum_open);
    LOAD(icu, unum_close);
    LOAD(icu, unum_formatDouble);
    LOAD(icu, unum_formatDoubleCurrency);
    LOAD(icu, u_strToUpper);
    LOAD(icu, u_strToLower);
    LOAD(icu, unorm2_getNFCInstance);
    LOAD(icu, unorm2_getNFDInstance);
    LOAD(icu, unorm2_normalize);
    LOAD(icu, u_charName);
    LOAD(icu, uscript_getScript);
    LOAD(icu, uscript_getName);
    LOAD(icu, ucnv_open);
    LOAD(icu, ucnv_close);
    LOAD(icu, ucnv_fromUChars);
    LOAD(icu, ucnv_toUChars);
    LOAD(icu, uregex_openC);
    LOAD(icu, uregex_close);
    LOAD(icu, uregex_setText);
    LOAD(icu, uregex_find);
    LOAD(icu, uregex_start);
    LOAD(icu, uregex_end);
    LOAD(icu, ulistfmt_open);
    LOAD(icu, ulistfmt_close);
    LOAD(icu, ulistfmt_format);
    LOAD(icu, uplrules_open);
    LOAD(icu, uplrules_close);
    LOAD(icu, uplrules_select);
    LOAD(icu, ucal_getWindowsTimeZoneID);
    LOAD(icu, ucal_getTimeZoneIDForWindowsID);

    uint8_t v[4] = {0};
    char vname[64];

    oracle_case("versions");
    pu_getVersion(v);
    /* major.minor: the other two numbers are the vendor's own builds */
    snprintf(vname, sizeof vname, "%u.%u", v[0], v[1]);
    o_str("icu", vname);
    /* Windows exports the names without ICU's "_<major>" renaming suffix. */
    snprintf(vname, sizeof vname, "u_getVersion_%u", v[0]);
    o_bool("suffixed_name_exported", GetProcAddress(icu, vname) != NULL);
    memset(v, 0, sizeof v);
    pu_getUnicodeVersion(v);
    o_version("unicode", v);
    {
        UErrorCode st = U_ZERO_ERROR;
        memset(v, 0, sizeof v);
        pulocdata_getCLDRVersion(v, &st);
        o_version("cldr", v);
        o_err(st);
    }
    oracle_case_end();

    oracle_case("error_names");
    o_str("0", pu_errorName(0));
    o_str("15", pu_errorName(U_BUFFER_OVERFLOW_ERROR));
    o_str("1", pu_errorName(1));
    o_str("-128", pu_errorName(-128));
    o_str("65536", pu_errorName(0x10000));
    oracle_case_end();

    oracle_case("default_locale");
    {
        char tag[64] = "";
        UErrorCode st = U_ZERO_ERROR;
        const char *d = puloc_getDefault();
        o_str("default", d);
        puloc_toLanguageTag(d, tag, sizeof tag, 0, &st);
        o_str("tag", tag);
        o_err(st);
    }
    oracle_case_end();

    oracle_case("collation_en_US");
    {
        static const wchar_t *pairs[][2] = {
            {L"apple", L"Banana"}, {L"a", L"A"}, {L"resume", L"résumé"},
            {L"abc", L"abc"}, {L"file10", L"file9"}, {L"co-op", L"coop"},
            {L"å", L"z"}, {L"ss", L"ß"},
        };
        UErrorCode st = U_ZERO_ERROR;
        void *coll = pucol_open("en_US", &st);
        o_err(st);
        o_bool("opened", coll != NULL);
        if (coll) {
            char k[32];
            memset(v, 0, sizeof v);
            pucol_getUCAVersion(coll, v);
            o_version("uca", v);
            for (size_t i = 0; i < ARRAYSIZE(pairs); i++) {
                snprintf(k, sizeof k, "tertiary_%zu", i);
                o_i64(k, sgn(pucol_strcoll(coll, pairs[i][0], -1, pairs[i][1], -1)));
            }
            pucol_setStrength(coll, 0); /* UCOL_PRIMARY */
            for (size_t i = 0; i < ARRAYSIZE(pairs); i++) {
                snprintf(k, sizeof k, "primary_%zu", i);
                o_i64(k, sgn(pucol_strcoll(coll, pairs[i][0], -1, pairs[i][1], -1)));
            }
            pucol_close(coll);
        }
    }
    oracle_case_end();

    oracle_case("collation_sv_SE");
    {
        UErrorCode st = U_ZERO_ERROR;
        void *coll = pucol_open("sv_SE", &st);
        o_err(st);
        if (coll) {
            o_i64("z_vs_o_umlaut", sgn(pucol_strcoll(coll, L"z", -1, L"ö", -1)));
            o_i64("a_ring_vs_a_umlaut", sgn(pucol_strcoll(coll, L"å", -1, L"ä", -1)));
            pucol_close(coll);
        }
    }
    oracle_case_end();

    oracle_case("word_break");
    {
        static const wchar_t text[] = L"Hello, world! It's 3.14 o'clock.";
        UErrorCode st = U_ZERO_ERROR;
        void *bi = pubrk_open(1 /* UBRK_WORD */, "en_US", text, -1, &st);
        o_err(st);
        if (bi) {
            char list[512] = "", t[24];
            for (int32_t p = pubrk_first(bi); p != UBRK_DONE; p = pubrk_next(bi)) {
                snprintf(t, sizeof t, "%s%d:%d", *list ? " " : "", p, pubrk_getRuleStatus(bi));
                strncat(list, t, sizeof list - strlen(list) - 1);
            }
            o_str("boundaries", list);
            pubrk_close(bi);
        }
    }
    oracle_case_end();

    oracle_case("line_break");
    {
        static const wchar_t text[] = L"The quick (\"brown\") fox can't jump 32.3 feet, right?";
        UErrorCode st = U_ZERO_ERROR;
        void *bi = pubrk_open(2 /* UBRK_LINE */, "en_US", text, -1, &st);
        o_err(st);
        if (bi) {
            char list[512] = "", t[16];
            for (int32_t p = pubrk_first(bi); p != UBRK_DONE; p = pubrk_next(bi)) {
                snprintf(t, sizeof t, "%s%d", *list ? " " : "", p);
                strncat(list, t, sizeof list - strlen(list) - 1);
            }
            o_str("boundaries", list);
            pubrk_close(bi);
        }
    }
    oracle_case_end();

    /* 2006-01-02 15:04:05 UTC */
    const UDate when = 1136214245000.0;
    oracle_case("date_pattern_en_US");
    {
        static const wchar_t pat[] = L"yyyy-MM-dd'T'HH:mm:ss EEEE MMMM";
        UErrorCode st = U_ZERO_ERROR;
        wchar_t out[128] = L"";
        void *df = pudat_open(-2 /* UDAT_PATTERN */, -2, "en_US", L"UTC", -1, pat, -1, &st);
        o_err(st);
        if (df) {
            int32_t n = pudat_format(df, when, out, ARRAYSIZE(out), NULL, &st);
            o_i64("len", n);
            o_wstr("text", out);
            pudat_close(df);
        }
    }
    oracle_case_end();

    oracle_case("date_styles_en_US");
    {
        static const char *names[] = {"full", "long", "medium", "short"};
        for (int s = 0; s < 4; s++) {
            UErrorCode st = U_ZERO_ERROR;
            wchar_t out[128] = L"";
            char k[32];
            void *df = pudat_open(-1 /* UDAT_NONE */, s, "en_US", L"UTC", -1, NULL, 0, &st);
            if (df) {
                pudat_format(df, when, out, ARRAYSIZE(out), NULL, &st);
                pudat_close(df);
            }
            snprintf(k, sizeof k, "date_%s", names[s]);
            o_wstr(k, out);
            out[0] = 0;
            st = U_ZERO_ERROR;
            df = pudat_open(s, -1, "en_US", L"UTC", -1, NULL, 0, &st);
            if (df) {
                pudat_format(df, when, out, ARRAYSIZE(out), NULL, &st);
                pudat_close(df);
            }
            snprintf(k, sizeof k, "time_%s", names[s]);
            o_wstr(k, out);
        }
    }
    oracle_case_end();

    oracle_case("date_buffer_overflow");
    {
        UErrorCode st = U_ZERO_ERROR;
        void *df = pudat_open(-1, 3 /* UDAT_SHORT */, "en_US", L"UTC", -1, NULL, 0, &st);
        if (df) {
            int32_t n = pudat_format(df, when, NULL, 0, NULL, &st);
            o_i64("len", n);
            o_err(st);
            pudat_close(df);
        }
    }
    oracle_case_end();

    oracle_case("numbers");
    {
        static const struct { const char *name, *loc; int style; double val; } nums[] = {
            {"decimal_en_US", "en_US", 1, 1234567.891},
            {"decimal_de_DE", "de_DE", 1, 1234567.891},
            {"decimal_fr_FR", "fr_FR", 1, 1234567.891},
            {"percent_en_US", "en_US", 3, 0.256},
            {"scientific_en_US", "en_US", 4, 123456.0},
            {"spellout_en_US", "en_US", 5, 42.0},
        };
        for (size_t i = 0; i < ARRAYSIZE(nums); i++) {
            UErrorCode st = U_ZERO_ERROR;
            wchar_t out[128] = L"";
            void *nf = punum_open(nums[i].style, NULL, 0, nums[i].loc, NULL, &st);
            if (nf) {
                punum_formatDouble(nf, nums[i].val, out, ARRAYSIZE(out), NULL, &st);
                punum_close(nf);
            }
            o_wstr(nums[i].name, U_FAILURE(st) ? L"(error)" : out);
        }
    }
    oracle_case_end();

    oracle_case("currency");
    {
        static const struct { const char *name, *loc; const wchar_t *cur; } curs[] = {
            {"usd_en_US", "en_US", L"USD"},
            {"eur_en_US", "en_US", L"EUR"},
            {"eur_de_DE", "de_DE", L"EUR"},
            {"jpy_ja_JP", "ja_JP", L"JPY"},
        };
        for (size_t i = 0; i < ARRAYSIZE(curs); i++) {
            UErrorCode st = U_ZERO_ERROR;
            wchar_t out[128] = L"";
            wchar_t cur[4];
            memcpy(cur, curs[i].cur, sizeof cur);
            void *nf = punum_open(2 /* UNUM_CURRENCY */, NULL, 0, curs[i].loc, NULL, &st);
            if (nf) {
                punum_formatDoubleCurrency(nf, -1234.5, cur, out, ARRAYSIZE(out), NULL, &st);
                punum_close(nf);
            }
            o_wstr(curs[i].name, U_FAILURE(st) ? L"(error)" : out);
        }
    }
    oracle_case_end();

    oracle_case("case_mapping");
    {
        wchar_t out[64];
        UErrorCode st = U_ZERO_ERROR;
        int32_t n = pu_strToUpper(out, ARRAYSIZE(out), L"straße", -1, "en_US", &st);
        o_i64("upper_len", n);
        o_wstr("upper", out);
        st = U_ZERO_ERROR;
        pu_strToUpper(out, ARRAYSIZE(out), L"istanbul", -1, "tr_TR", &st);
        o_wstr("upper_tr", out);
        st = U_ZERO_ERROR;
        pu_strToLower(out, ARRAYSIZE(out), L"Iİ", -1, "en_US", &st);
        o_bytes("lower_en", out, wcslen(out) * sizeof(wchar_t));
        st = U_ZERO_ERROR;
        pu_strToLower(out, ARRAYSIZE(out), L"Iİ", -1, "tr_TR", &st);
        o_bytes("lower_tr", out, wcslen(out) * sizeof(wchar_t));
        o_err(st);
    }
    oracle_case_end();

    oracle_case("normalization");
    {
        wchar_t out[32];
        UErrorCode st = U_ZERO_ERROR;
        int32_t n = punorm2_normalize(punorm2_getNFDInstance(&st), L"éÅ", -1, out, ARRAYSIZE(out), &st);
        o_i64("nfd_len", n);
        o_bytes("nfd", out, n * sizeof(wchar_t));
        n = punorm2_normalize(punorm2_getNFCInstance(&st), L"éÅ", -1, out, ARRAYSIZE(out), &st);
        o_i64("nfc_len", n);
        o_bytes("nfc", out, n * sizeof(wchar_t));
        o_err(st);
    }
    oracle_case_end();

    oracle_case("properties");
    {
        static const int32_t cps[] = {0x41, 0x3a9, 0x5d0, 0x4e2d, 0x1f600};
        for (size_t i = 0; i < ARRAYSIZE(cps); i++) {
            char name[128] = "", k[32];
            UErrorCode st = U_ZERO_ERROR;
            pu_charName(cps[i], 0 /* U_UNICODE_CHAR_NAME */, name, sizeof name, &st);
            snprintf(k, sizeof k, "name_%x", (unsigned)cps[i]);
            o_str(k, name);
            st = U_ZERO_ERROR;
            snprintf(k, sizeof k, "script_%x", (unsigned)cps[i]);
            o_str(k, puscript_getName(puscript_getScript(cps[i], &st)));
        }
    }
    oracle_case_end();

    oracle_case("converters");
    {
        static const struct { const char *name; } cnvs[] = {
            {"windows-1252"}, {"ibm-850"}, {"Shift_JIS"}, {"UTF-8"}, {"no-such-charset"},
        };
        for (size_t i = 0; i < ARRAYSIZE(cnvs); i++) {
            char out[32] = "", k[48];
            UErrorCode st = U_ZERO_ERROR;
            void *c = pucnv_open(cnvs[i].name, &st);
            int32_t n = 0;
            if (c) {
                n = pucnv_fromUChars(c, out, sizeof out, L"A€é日", -1, &st);
                pucnv_close(c);
            }
            snprintf(k, sizeof k, "%s", cnvs[i].name);
            o_bytes(k, out, n > 0 && n < (int32_t)sizeof out ? (size_t)n : 0);
            snprintf(k, sizeof k, "%s_status", cnvs[i].name);
            o_str(k, pu_errorName(st));
        }
        {
            wchar_t w[16] = L"";
            UErrorCode st = U_ZERO_ERROR;
            void *c = pucnv_open("windows-1252", &st);
            if (c) {
                pucnv_toUChars(c, w, ARRAYSIZE(w), "\x80\x93\xe9", 3, &st);
                pucnv_close(c);
            }
            o_bytes("to_uchars_1252", w, wcslen(w) * sizeof(wchar_t));
        }
    }
    oracle_case_end();

    oracle_case("regex");
    {
        UErrorCode st = U_ZERO_ERROR;
        void *re = puregex_openC("\\p{L}+\\d{2}", 0, NULL, &st);
        o_err(st);
        if (re) {
            static const wchar_t text[] = L"-- café42 x";
            puregex_setText(re, text, -1, &st);
            o_bool("found", puregex_find(re, 0, &st));
            o_i64("start", puregex_start(re, 0, &st));
            o_i64("end", puregex_end(re, 0, &st));
            puregex_close(re);
        }
        st = U_ZERO_ERROR;
        re = puregex_openC("(unclosed", 0, NULL, &st);
        o_bool("bad_opened", re != NULL);
        o_str("bad_status", pu_errorName(st));
        if (re) puregex_close(re);
    }
    oracle_case_end();

    oracle_case("list_plural");
    {
        static const UChar *items[] = {L"red", L"green", L"blue"};
        wchar_t out[64] = L"";
        UErrorCode st = U_ZERO_ERROR;
        void *lf = pulistfmt_open("en_US", &st);
        if (lf) {
            pulistfmt_format(lf, items, NULL, 3, out, ARRAYSIZE(out), &st);
            pulistfmt_close(lf);
        }
        o_wstr("list", out);
        st = U_ZERO_ERROR;
        void *pr = puplrules_open("en_US", &st);
        if (pr) {
            puplrules_select(pr, 1, out, ARRAYSIZE(out), &st);
            o_wstr("plural_1", out);
            puplrules_select(pr, 2, out, ARRAYSIZE(out), &st);
            o_wstr("plural_2", out);
            puplrules_close(pr);
        }
        o_err(st);
    }
    oracle_case_end();

    oracle_case("windows_time_zones");
    {
        wchar_t out[64] = L"";
        UErrorCode st = U_ZERO_ERROR;
        int32_t n = pucal_getWindowsTimeZoneID(L"America/New_York", -1, out, ARRAYSIZE(out), &st);
        o_i64("ny_len", n);
        o_wstr("ny", out);
        st = U_ZERO_ERROR;
        pucal_getWindowsTimeZoneID(L"Europe/Berlin", -1, out, ARRAYSIZE(out), &st);
        o_wstr("berlin", out);
        st = U_ZERO_ERROR;
        pucal_getTimeZoneIDForWindowsID(L"Tokyo Standard Time", -1, NULL, out, ARRAYSIZE(out), &st);
        o_wstr("tokyo", out);
        st = U_ZERO_ERROR;
        pucal_getTimeZoneIDForWindowsID(L"Pacific Standard Time", -1, "CA", out, ARRAYSIZE(out), &st);
        o_wstr("pacific_ca", out);
        o_err(st);
    }
    oracle_case_end();

    oracle_case("wow64_copies");
    {
        static const wchar_t *dlls[] = {L"icu.dll", L"icuuc.dll", L"icuin.dll"};
        wchar_t dir[MAX_PATH], path[MAX_PATH + 16];
        UINT n = GetSystemWow64DirectoryW(dir, MAX_PATH);
        o_bool("has_wow64_dir", n > 0);
        for (size_t i = 0; n && i < ARRAYSIZE(dlls); i++) {
            swprintf(path, ARRAYSIZE(path), L"%ls\\%ls", dir, dlls[i]);
            o_bool(oracle_utf8(dlls[i]), GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES);
        }
    }
    oracle_case_end();

    return oracle_end();
}
