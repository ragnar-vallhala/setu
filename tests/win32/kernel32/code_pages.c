/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: code page information: GetCPInfo / GetCPInfoExW for fixed
 * code pages (max char size, default chars, lead-byte ranges, names),
 * IsValidCodePage, IsDBCSLeadByteEx, the CP_ACP / CP_OEMCP / CP_THREAD_ACP
 * aliases compared with GetACP / GetOEMCP (never printed), and
 * EnumSystemCodePagesW by shape.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winnls/nf-winnls-getcpinfo
 *       https://learn.microsoft.com/en-us/windows/win32/api/winnls/nf-winnls-getcpinfoexw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winnls/ns-winnls-cpinfoexw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winnls/nf-winnls-isvalidcodepage
 *       https://learn.microsoft.com/en-us/windows/win32/api/winnls/nf-winnls-isdbcsleadbyteex
 *       https://learn.microsoft.com/en-us/windows/win32/api/winnls/nf-winnls-enumsystemcodepagesw
 *       https://learn.microsoft.com/en-us/windows/win32/intl/code-page-identifiers
 */
#include "oracle.h"

static void lead_ranges(const BYTE *lb)
{
    char s[64] = "";
    for (int i = 0; i < MAX_LEADBYTES && (lb[i] || lb[i + 1]); i += 2) {
        char t[16];
        snprintf(t, sizeof t, "%s%02x-%02x", s[0] ? "," : "", lb[i], lb[i + 1]);
        strncat(s, t, sizeof s - strlen(s) - 1);
    }
    o_str("lead_bytes", s);
}

static void cpinfo(const char *name, UINT cp)
{
    CPINFOEXW ex;
    CPINFO ci;
    BOOL r;
    memset(&ex, 0xcc, sizeof ex);
    ORACLE_CALL(r, GetCPInfoExW(cp, 0, &ex));
    oracle_case(name);
    o_bool("ret", r);
    o_lasterr();
    if (r) {
        o_u32("max_char_size", ex.MaxCharSize);
        o_bytes("default_char", ex.DefaultChar, MAX_DEFAULTCHAR);
        lead_ranges(ex.LeadByte);
        o_hex32("unicode_default_char", ex.UnicodeDefaultChar);
        o_u32("code_page", ex.CodePage);
        o_wstr("name", ex.CodePageName);
    }
    memset(&ci, 0xcc, sizeof ci);
    r = GetCPInfo(cp, &ci);
    o_bool("cpinfo_ret", r);
    if (r) o_bool("cpinfo_matches", ci.MaxCharSize == ex.MaxCharSize &&
                  !memcmp(ci.DefaultChar, ex.DefaultChar, MAX_DEFAULTCHAR) &&
                  !memcmp(ci.LeadByte, ex.LeadByte, MAX_LEADBYTES));
    oracle_case_end();
}

static void valid(const char *name, UINT cp)
{
    BOOL r;
    ORACLE_CALL(r, IsValidCodePage(cp));
    oracle_case(name);
    o_bool("ret", r);
    o_lasterr();
    oracle_case_end();
}

static int enum_count, enum_has_1252, enum_has_65001, enum_all_valid = 1, enum_stop;
static BOOL CALLBACK enum_cb(LPWSTR s)
{
    UINT cp = (UINT)wcstoul(s, NULL, 10);
    enum_count++;
    if (cp == 1252) enum_has_1252 = 1;
    if (cp == 65001) enum_has_65001 = 1;
    if (!IsValidCodePage(cp)) enum_all_valid = 0;
    return !(enum_stop && enum_count >= enum_stop);
}

int main(void)
{
    oracle_begin("kernel32/code_pages");

    /* Windows loads its tables on first use and may reset the last error then;
     * one call up front keeps that process-history detail out of the cases. */
    {
        CPINFOEXW warm;
        GetCPInfoExW(1252, 0, &warm);
    }

    cpinfo("cp1252", 1252);
    cpinfo("cp1250", 1250);
    cpinfo("cp1251", 1251);
    cpinfo("cp1253", 1253);
    cpinfo("cp1255", 1255);
    cpinfo("cp1256", 1256);
    cpinfo("cp874", 874);
    cpinfo("cp437", 437);
    cpinfo("cp850", 850);
    cpinfo("cp866", 866);
    cpinfo("cp932", 932);
    cpinfo("cp936", 936);
    cpinfo("cp949", 949);
    cpinfo("cp950", 950);
    cpinfo("cp28591", 28591);
    cpinfo("cp20127", 20127);
    cpinfo("cp10000", 10000);
    cpinfo("cp_utf8", CP_UTF8);
    cpinfo("cp_utf7", CP_UTF7);
    cpinfo("cp_symbol", CP_SYMBOL);
    cpinfo("cp_unknown", 12345);
    cpinfo("cp_1200_utf16", 1200);
    {
        CPINFOEXW ex;
        BOOL r;
        ORACLE_CALL(r, GetCPInfoExW(1252, 1, &ex));
        oracle_case("cpinfoex_flag_nonzero");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, GetCPInfoExW(1252, 0, NULL));
        oracle_case("cpinfoex_null_out");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
    }

    /* Aliases resolve to the process code pages. */
    {
        CPINFOEXW a, o, t, m;
        BOOL ra = GetCPInfoExW(CP_ACP, 0, &a);
        BOOL ro = GetCPInfoExW(CP_OEMCP, 0, &o);
        BOOL rt = GetCPInfoExW(CP_THREAD_ACP, 0, &t);
        BOOL rm = GetCPInfoExW(CP_MACCP, 0, &m);
        oracle_case("aliases");
        o_bool("acp_ok", ra);
        o_bool("acp_is_getacp", ra && a.CodePage == GetACP());
        o_bool("oem_ok", ro);
        o_bool("oem_is_getoemcp", ro && o.CodePage == GetOEMCP());
        o_bool("thread_acp_ok", rt);
        o_bool("thread_acp_is_getacp", rt && t.CodePage == GetACP());
        o_bool("mac_ok", rm);
        o_bool("acp_valid", IsValidCodePage(GetACP()));
        o_bool("oem_valid", IsValidCodePage(GetOEMCP()));
        o_bool("acp_not_utf8", GetACP() != CP_UTF8);
        oracle_case_end();
    }

    valid("valid_1252", 1252);
    valid("valid_utf8", CP_UTF8);
    valid("valid_utf7", CP_UTF7);
    valid("valid_cp_acp_alias", CP_ACP);
    valid("valid_cp_oemcp_alias", CP_OEMCP);
    valid("valid_symbol", CP_SYMBOL);
    valid("valid_1200", 1200);
    valid("valid_20932", 20932);
    valid("valid_50220", 50220);
    valid("valid_54936", 54936);
    valid("valid_unknown", 12345);

    {
        oracle_case("dbcs_lead_bytes");
        o_bool("932_81", IsDBCSLeadByteEx(932, 0x81));
        o_bool("932_a0", IsDBCSLeadByteEx(932, 0xa0));
        o_bool("932_e0", IsDBCSLeadByteEx(932, 0xe0));
        o_bool("932_41", IsDBCSLeadByteEx(932, 0x41));
        o_bool("936_81", IsDBCSLeadByteEx(936, 0x81));
        o_bool("936_ff", IsDBCSLeadByteEx(936, 0xff));
        o_bool("949_a1", IsDBCSLeadByteEx(949, 0xa1));
        o_bool("1252_81", IsDBCSLeadByteEx(1252, 0x81));
        oracle_case_end();
        BOOL r;
        ORACLE_CALL(r, IsDBCSLeadByteEx(12345, 0x81));
        oracle_case("dbcs_bad_code_page");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, IsDBCSLeadByteEx(CP_UTF8, 0xc3));
        oracle_case("dbcs_utf8");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
    }

    {
        BOOL r;
        ORACLE_CALL(r, EnumSystemCodePagesW(enum_cb, CP_SUPPORTED));
        oracle_case("enum_supported");
        o_bool("ret", r);
        o_lasterr();
        o_bool("many", enum_count > 20);
        o_bool("has_1252", enum_has_1252);
        o_bool("has_65001", enum_has_65001);
        o_bool("all_valid", enum_all_valid);
        oracle_case_end();
        int supported = enum_count;
        enum_count = enum_has_1252 = enum_has_65001 = 0;
        enum_all_valid = 1;
        r = EnumSystemCodePagesW(enum_cb, CP_INSTALLED);
        oracle_case("enum_installed");
        o_bool("ret", r);
        o_bool("le_supported", enum_count <= supported);
        o_bool("has_1252", enum_has_1252);
        o_bool("all_valid", enum_all_valid);
        oracle_case_end();
        enum_count = 0;
        enum_stop = 2;
        r = EnumSystemCodePagesW(enum_cb, CP_SUPPORTED);
        oracle_case("enum_stop");
        o_bool("ret", r);
        o_i64("count", enum_count);
        oracle_case_end();
        enum_stop = 0;
        ORACLE_CALL(r, EnumSystemCodePagesW(enum_cb, 7));
        oracle_case("enum_bad_flags");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
    }

    return oracle_end();
}
