/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: geographical location data: GetGeoInfoW for fixed GEOIDs and
 * GEOTYPEs, GetGeoInfoEx by ISO 3166 name, EnumSystemGeoID / EnumSystemGeoNames
 * by shape, buffer rules and invalid input. The user's own location is only
 * compared (GetUserGeoID vs GetUserDefaultGeoName), never printed.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winnls/nf-winnls-getgeoinfow
 *       https://learn.microsoft.com/en-us/windows/win32/api/winnls/nf-winnls-getgeoinfoex
 *       https://learn.microsoft.com/en-us/windows/win32/api/winnls/ne-winnls-sysgeotype
 *       https://learn.microsoft.com/en-us/windows/win32/api/winnls/nf-winnls-enumsystemgeoid
 *       https://learn.microsoft.com/en-us/windows/win32/api/winnls/nf-winnls-enumsystemgeonames
 *       https://learn.microsoft.com/en-us/windows/win32/api/winnls/nf-winnls-getuserdefaultgeoname
 *       https://learn.microsoft.com/en-us/windows/win32/intl/table-of-geographical-locations
 */
#include "oracle.h"

typedef int (WINAPI *geoex_fn)(PWSTR, GEOTYPE, PWSTR, int);
#ifndef GEO_NAME
#define GEO_NAME ((GEOTYPE)17)
#endif
#ifndef GEO_ID
#define GEO_ID ((GEOTYPE)18)
#endif
typedef BOOL (CALLBACK *geo_enumname_proc)(PWSTR, LPARAM);
typedef BOOL (WINAPI *enumnames_fn)(GEOCLASS, geo_enumname_proc, LPARAM);
typedef int (WINAPI *defname_fn)(LPWSTR, int);

struct gt { const char *name; GEOTYPE type; };
#define T(x) { #x, x }
static const struct gt types[] = {
    T(GEO_NATION), T(GEO_LATITUDE), T(GEO_LONGITUDE), T(GEO_ISO2), T(GEO_ISO3), T(GEO_RFC1766),
    T(GEO_LCID), T(GEO_FRIENDLYNAME), T(GEO_OFFICIALNAME), T(GEO_ISO_UN_NUMBER), T(GEO_PARENT),
    T(GEO_DIALINGCODE), T(GEO_CURRENCYCODE), T(GEO_CURRENCYSYMBOL), T(GEO_NAME), T(GEO_ID),
    { NULL, 0 }
};
#undef T

static void geo_group(const char *casename, GEOID id)
{
    oracle_case(casename);
    for (const struct gt *t = types; t->name; t++) {
        wchar_t buf[128];
        char k[64];
        int r = GetGeoInfoW(id, t->type, buf, 128, 0);
        if (r > 0) o_wstr(t->name + 4, buf);
        else {
            snprintf(k, sizeof k, "%s.error", t->name + 4);
            o_u32(k, GetLastError());
        }
    }
    oracle_case_end();
}

static void one(const char *name, GEOID id, GEOTYPE type, int size, LANGID lang)
{
    wchar_t buf[128];
    int r;
    wmemset(buf, L'#', 128);
    ORACLE_CALL(r, GetGeoInfoW(id, type, size ? buf : NULL, size, lang));
    oracle_case(name);
    o_i64("ret", r);
    o_lasterr();
    if (r > 0 && size) o_wstr("value", buf);
    oracle_case_end();
}

static int n_ids, has_us, has_region_world, n_names, has_us_name, has_001_name;
static BOOL CALLBACK id_cb(GEOID id)
{
    n_ids++;
    if (id == 244) has_us = 1;
    if (id == 39070) has_region_world = 1;
    return TRUE;
}
static BOOL CALLBACK name_cb(PWSTR name, LPARAM lp)
{
    (void)lp;
    n_names++;
    if (!wcscmp(name, L"US")) has_us_name = 1;
    if (!wcscmp(name, L"001")) has_001_name = 1;
    return TRUE;
}

int main(void)
{
    oracle_begin("kernel32/geo_info");

    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    geoex_fn pGetGeoInfoEx = (geoex_fn)(void *)GetProcAddress(k32, "GetGeoInfoEx");
    enumnames_fn pEnumNames = (enumnames_fn)(void *)GetProcAddress(k32, "EnumSystemGeoNames");
    defname_fn pDefName = (defname_fn)(void *)GetProcAddress(k32, "GetUserDefaultGeoName");

    geo_group("us_244", 244);
    geo_group("germany_94", 94);
    geo_group("japan_122", 122);
    geo_group("india_113", 113);
    geo_group("world_39070", 39070);
    geo_group("caribbean_10039880", 10039880);

    one("size_query", 244, GEO_FRIENDLYNAME, 0, 0);
    one("buffer_too_small", 244, GEO_FRIENDLYNAME, 3, 0);
    one("bad_geoid", 99999, GEO_ISO2, 128, 0);
    one("geoid_none", GEOID_NOT_AVAILABLE, GEO_ISO2, 128, 0);
    one("bad_geotype", 244, (GEOTYPE)99, 128, 0);
    one("langid_english", 244, GEO_FRIENDLYNAME, 128, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US));
    one("langid_nonzero_iso2", 244, GEO_ISO2, 128, MAKELANGID(LANG_GERMAN, SUBLANG_GERMAN));

    oracle_case("have_ex_fns");
    o_bool("get_geo_info_ex", pGetGeoInfoEx != NULL);
    o_bool("enum_system_geo_names", pEnumNames != NULL);
    o_bool("get_user_default_geo_name", pDefName != NULL);
    oracle_case_end();
    if (pGetGeoInfoEx) {
        static const struct { const char *n; const wchar_t *loc; GEOTYPE t; } ex[] = {
            { "ex_us_iso3", L"US", GEO_ISO3 }, { "ex_us_friendly", L"US", GEO_FRIENDLYNAME },
            { "ex_lower_us", L"us", GEO_ISO2 }, { "ex_de_currency", L"DE", GEO_CURRENCYCODE },
            { "ex_un_world", L"001", GEO_FRIENDLYNAME }, { "ex_un_world_id", L"001", GEO_ID },
            { "ex_bad_name", L"QQ", GEO_ISO2 }, { "ex_nation_rejected", L"US", GEO_NATION },
            { "ex_lcid_rejected", L"US", GEO_LCID }, { "ex_name", L"JP", GEO_NAME },
        };
        for (size_t i = 0; i < sizeof ex / sizeof ex[0]; i++) {
            wchar_t buf[128];
            int r;
            ORACLE_CALL(r, pGetGeoInfoEx((PWSTR)ex[i].loc, ex[i].t, buf, 128));
            oracle_case(ex[i].n);
            o_i64("ret", r);
            o_lasterr();
            if (r > 0) o_wstr("value", buf);
            oracle_case_end();
        }
    }

    {
        BOOL r;
        ORACLE_CALL(r, EnumSystemGeoID(GEOCLASS_NATION, 0, id_cb));
        oracle_case("enum_nations");
        o_bool("ret", r);
        o_lasterr();
        o_bool("many", n_ids > 150);
        o_bool("has_us", has_us);
        o_bool("has_world", has_region_world);
        oracle_case_end();
        int nations = n_ids;
        n_ids = has_us = has_region_world = 0;
        r = EnumSystemGeoID(GEOCLASS_REGION, 0, id_cb);
        oracle_case("enum_regions");
        o_bool("ret", r);
        o_bool("fewer_than_nations", n_ids < nations);
        o_bool("has_us", has_us);
        o_bool("has_world", has_region_world);
        oracle_case_end();
        n_ids = 0;
        ORACLE_CALL(r, EnumSystemGeoID(GEOCLASS_ALL, 0, id_cb));
        oracle_case("enum_all");
        o_bool("ret", r);
        o_lasterr();
        o_bool("at_least_nations", n_ids >= nations);
        oracle_case_end();
        ORACLE_CALL(r, EnumSystemGeoID(7, 0, id_cb));
        oracle_case("enum_bad_class");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, EnumSystemGeoID(GEOCLASS_NATION, 244, id_cb));
        oracle_case("enum_parent_nonzero");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        if (pEnumNames) {
            ORACLE_CALL(r, pEnumNames(GEOCLASS_NATION, name_cb, 0));
            oracle_case("enum_names_nations");
            o_bool("ret", r);
            o_lasterr();
            o_bool("has_us", has_us_name);
            o_bool("has_001", has_001_name);
            o_bool("same_count_as_ids", n_names == nations);
            oracle_case_end();
        }
    }

    {
        GEOID id = GetUserGeoID(GEOCLASS_NATION);
        wchar_t name[32] = L"", iso2[32] = L"";
        int rn = pDefName ? pDefName(name, 32) : 0;
        GetGeoInfoW(id, GEO_ISO2, iso2, 32, 0);
        oracle_case("user_geo_consistent");
        o_bool("id_valid", id != GEOID_NOT_AVAILABLE);
        o_bool("name_ok", rn > 0);
        o_bool("name_matches_id", rn > 0 && !wcscmp(name, iso2));
        o_u32("region_class_id_valid", GetUserGeoID(GEOCLASS_REGION) != GEOID_NOT_AVAILABLE);
        o_u32("bad_class", GetUserGeoID(7));
        oracle_case_end();
    }

    return oracle_end();
}
