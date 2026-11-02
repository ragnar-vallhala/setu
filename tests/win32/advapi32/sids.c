/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: SIDs and SDDL: ConvertStringSidToSidW (numeric forms, SDDL
 * aliases, malformed input), ConvertSidToStringSidW round trips, IsValidSid,
 * GetLengthSid, GetSidSubAuthorityCount, GetSidIdentifierAuthority,
 * CreateWellKnownSid (buffer sizing, missing domain), IsWellKnownSid,
 * EqualSid, AllocateAndInitializeSid, and
 * ConvertStringSecurityDescriptorToSecurityDescriptorW parsing (valid
 * strings round-tripped through ConvertSecurityDescriptorToStringSecurityDescriptorW
 * and malformed strings' error codes).
 *
 * Only well-known and synthetic SIDs are used; nothing machine-, domain- or
 * user-specific is printed, and no account names are looked up.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/sddl/nf-sddl-convertstringsidtosidw
 *       https://learn.microsoft.com/en-us/windows/win32/api/sddl/nf-sddl-convertsidtostringsidw
 *       https://learn.microsoft.com/en-us/windows/win32/secauthz/sid-strings
 *       https://learn.microsoft.com/en-us/windows/win32/secauthz/well-known-sids
 *       https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-isvalidsid
 *       https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-getlengthsid
 *       https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-getsidsubauthoritycount
 *       https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-createwellknownsid
 *       https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-iswellknownsid
 *       https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-equalsid
 *       https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-allocateandinitializesid
 *       https://learn.microsoft.com/en-us/windows/win32/api/sddl/nf-sddl-convertstringsecuritydescriptortosecuritydescriptorw
 *       https://learn.microsoft.com/en-us/windows/win32/api/sddl/nf-sddl-convertsecuritydescriptortostringsecuritydescriptorw
 *       https://learn.microsoft.com/en-us/windows/win32/secauthz/security-descriptor-string-format
 *       https://learn.microsoft.com/en-us/windows/win32/secauthz/ace-strings
 */
#include "oracle.h"
#include <sddl.h>

static void sid_fields(PSID s)
{
    LPWSTR str = NULL;
    o_bool("valid", IsValidSid(s));
    o_u32("length", GetLengthSid(s));
    o_u32("subauth_count", *GetSidSubAuthorityCount(s));
    if (ConvertSidToStringSidW(s, &str)) {
        o_wstr("string", str);
        LocalFree(str);
    } else {
        o_str("string", "convert failed");
    }
}

static void str2sid(const char *name, const wchar_t *in)
{
    PSID s = NULL;
    BOOL r;
    ORACLE_CALL(r, ConvertStringSidToSidW(in, &s));
    oracle_case(name);
    o_bool("ret", r);
    o_lasterr();
    if (r && s) {
        sid_fields(s);
        LocalFree(s);
    }
    oracle_case_end();
}

static void wellknown(const char *name, WELL_KNOWN_SID_TYPE t, PSID domain)
{
    BYTE buf[SECURITY_MAX_SID_SIZE];
    DWORD sz = 0;
    BOOL r;
    ORACLE_CALL(r, CreateWellKnownSid(t, domain, NULL, &sz));
    oracle_case(name);
    o_bool("size_query_ret", r);
    o_u32("size_query_err", oracle_last_error);
    o_u32("size", sz);
    DWORD sz2 = sizeof buf;
    ORACLE_CALL(r, CreateWellKnownSid(t, domain, buf, &sz2));
    o_bool("ret", r);
    o_lasterr();
    o_u32("size_after", sz2);
    if (r) {
        sid_fields(buf);
        o_bool("is_well_known", IsWellKnownSid(buf, t));
    }
    oracle_case_end();
}

static void sddl(const char *name, const wchar_t *in)
{
    PSECURITY_DESCRIPTOR sd = NULL;
    ULONG len = 0;
    BOOL r;
    ORACLE_CALL(r, ConvertStringSecurityDescriptorToSecurityDescriptorW(in, SDDL_REVISION_1, &sd, &len));
    oracle_case(name);
    o_bool("ret", r);
    o_lasterr();
    if (r && sd) {
        o_u32("length", len);
        o_u32("getlength", GetSecurityDescriptorLength(sd));
        SECURITY_DESCRIPTOR_CONTROL c = 0;
        DWORD rev = 0;
        GetSecurityDescriptorControl(sd, &c, &rev);
        o_hex32("control", c);
        LPWSTR out = NULL;
        if (ConvertSecurityDescriptorToStringSecurityDescriptorW(
                sd, SDDL_REVISION_1,
                OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION |
                    SACL_SECURITY_INFORMATION | LABEL_SECURITY_INFORMATION,
                &out, NULL)) {
            o_wstr("roundtrip", out);
            LocalFree(out);
        } else {
            o_u32("roundtrip_error", GetLastError());
        }
        LocalFree(sd);
    }
    oracle_case_end();
}

int main(void)
{
    oracle_begin("advapi32/sids");

    /* ---- ConvertStringSidToSidW ---- */
    str2sid("str_local_system", L"S-1-5-18");
    str2sid("str_everyone", L"S-1-1-0");
    str2sid("str_builtin_admins", L"S-1-5-32-544");
    str2sid("str_domain_like", L"S-1-5-21-1-2-3-500");
    str2sid("str_lowercase_s", L"s-1-5-18");
    str2sid("str_hex_authority", L"S-1-0x5-18");
    str2sid("str_large_authority", L"S-1-281474976710655-1");
    str2sid("str_authority_too_large", L"S-1-281474976710656-1");
    str2sid("str_hex_authority_6_bytes", L"S-1-0xFFFFFFFFFFFF-1");
    str2sid("str_no_subauthorities", L"S-1-5");
    str2sid("str_15_subauthorities", L"S-1-5-1-2-3-4-5-6-7-8-9-10-11-12-13-14-15");
    str2sid("str_16_subauthorities", L"S-1-5-1-2-3-4-5-6-7-8-9-10-11-12-13-14-15-16");
    str2sid("str_subauth_max", L"S-1-5-4294967295");
    str2sid("str_subauth_overflow", L"S-1-5-4294967296");
    str2sid("str_subauth_hex", L"S-1-5-0x12");
    str2sid("str_revision_2", L"S-2-5-18");
    str2sid("str_trailing_dash", L"S-1-5-18-");
    str2sid("str_double_dash", L"S-1-5--18");
    str2sid("str_spaces", L"S-1-5-18 ");
    str2sid("str_letters", L"S-1-5-abc");
    str2sid("str_missing_prefix", L"1-5-18");
    str2sid("str_empty", L"");
    str2sid("str_only_s", L"S-");
    str2sid("alias_wd", L"WD");
    str2sid("alias_sy", L"SY");
    str2sid("alias_ba", L"BA");
    str2sid("alias_bu", L"BU");
    str2sid("alias_au", L"AU");
    str2sid("alias_ns", L"NS");
    str2sid("alias_ls", L"LS");
    str2sid("alias_co", L"CO");
    str2sid("alias_lowercase", L"wd");
    str2sid("alias_unknown", L"ZZ");
    str2sid("alias_integrity_high", L"HI");
    str2sid("alias_integrity_low", L"LW");
    {
        BOOL r;
        PSID s = NULL;
        ORACLE_CALL(r, ConvertStringSidToSidW(NULL, &s));
        oracle_case("str_null");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
    }

    /* ---- CreateWellKnownSid ---- */
    wellknown("wk_world", WinWorldSid, NULL);
    wellknown("wk_local_system", WinLocalSystemSid, NULL);
    wellknown("wk_builtin_admins", WinBuiltinAdministratorsSid, NULL);
    wellknown("wk_builtin_users", WinBuiltinUsersSid, NULL);
    wellknown("wk_authenticated_users", WinAuthenticatedUserSid, NULL);
    wellknown("wk_null", WinNullSid, NULL);
    wellknown("wk_creator_owner", WinCreatorOwnerSid, NULL);
    wellknown("wk_high_label", WinHighLabelSid, NULL);
    wellknown("wk_account_admin_no_domain", WinAccountAdministratorSid, NULL);
    {
        PSID dom = NULL;
        ConvertStringSidToSidW(L"S-1-5-21-1-2-3", &dom);
        wellknown("wk_account_admin_with_domain", WinAccountAdministratorSid, dom);
        wellknown("wk_account_guest_with_domain", WinAccountGuestSid, dom);
        wellknown("wk_world_with_domain_ignored", WinWorldSid, dom);
        LocalFree(dom);
        PSID notdom = NULL;
        ConvertStringSidToSidW(L"S-1-5-18", &notdom);
        wellknown("wk_account_admin_bad_domain", WinAccountAdministratorSid, notdom);
        LocalFree(notdom);
    }
    wellknown("wk_bogus_type", (WELL_KNOWN_SID_TYPE)5000, NULL);
    {
        BYTE buf[SECURITY_MAX_SID_SIZE];
        DWORD sz = 4;
        BOOL r;
        ORACLE_CALL(r, CreateWellKnownSid(WinWorldSid, NULL, buf, &sz));
        oracle_case("wk_buffer_too_small");
        o_bool("ret", r);
        o_lasterr();
        o_u32("size", sz);
        oracle_case_end();
    }

    /* ---- AllocateAndInitializeSid / EqualSid / IsValidSid ---- */
    {
        SID_IDENTIFIER_AUTHORITY nt = {SECURITY_NT_AUTHORITY};
        PSID a = NULL, b = NULL;
        BOOL r;
        ORACLE_CALL(r, AllocateAndInitializeSid(&nt, 2, 32, 544, 0, 0, 0, 0, 0, 0, &a));
        oracle_case("allocate_builtin_admins");
        o_bool("ret", r);
        o_lasterr();
        if (r) sid_fields(a);
        oracle_case_end();
        ConvertStringSidToSidW(L"BA", &b);
        oracle_case("equalsid");
        o_bool("alloc_vs_alias", EqualSid(a, b));
        PSID w = NULL;
        ConvertStringSidToSidW(L"WD", &w);
        o_bool("admins_vs_world", EqualSid(a, w));
        oracle_case_end();
        LocalFree(w);
        LocalFree(b);
        FreeSid(a);
        ORACLE_CALL(r, AllocateAndInitializeSid(&nt, 9, 1, 2, 3, 4, 5, 6, 7, 8, &a));
        oracle_case("allocate_9_subauthorities");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        if (r) FreeSid(a);
        ORACLE_CALL(r, AllocateAndInitializeSid(&nt, 0, 0, 0, 0, 0, 0, 0, 0, 0, &a));
        oracle_case("allocate_0_subauthorities");
        o_bool("ret", r);
        o_lasterr();
        if (r) sid_fields(a);
        oracle_case_end();
        if (r) FreeSid(a);

        BYTE bad[16] = {2, 1, 0, 0, 0, 0, 0, 5, 18, 0, 0, 0};
        oracle_case("isvalidsid_bad_revision");
        o_bool("ret", IsValidSid(bad));
        oracle_case_end();
        bad[0] = 1;
        bad[1] = 16;
        oracle_case("isvalidsid_too_many_subauth");
        o_bool("ret", IsValidSid(bad));
        oracle_case_end();
        LPWSTR str = NULL;
        ORACLE_CALL(r, ConvertSidToStringSidW(bad, &str));
        oracle_case("sidtostring_invalid_sid");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        if (r) LocalFree(str);
    }

    /* ---- SDDL security descriptors ---- */
    sddl("sd_owner_group_dacl", L"O:BAG:SYD:(A;;GA;;;WD)");
    sddl("sd_dacl_only", L"D:(A;;FA;;;BA)(A;;FR;;;BU)");
    sddl("sd_protected_autoinherited", L"D:PAI(A;OICI;FA;;;SY)");
    sddl("sd_deny_and_allow", L"D:(D;;WD;;;AN)(A;;0x1200a9;;;WD)");
    sddl("sd_hex_mask", L"D:(A;;0x1f01ff;;;SY)");
    sddl("sd_null_dacl", L"D:NO_ACCESS_CONTROL");
    sddl("sd_empty_dacl", L"D:");
    sddl("sd_sacl_audit", L"S:(AU;SAFA;FA;;;WD)");
    sddl("sd_mandatory_label", L"S:(ML;;NW;;;LW)");
    sddl("sd_numeric_sid", L"O:S-1-5-21-1-2-3-500D:(A;;GA;;;S-1-5-21-1-2-3-500)");
    sddl("sd_whitespace", L"O:BA G:SY D:(A;;GA;;;WD)");
    sddl("sd_lowercase", L"o:bad:(a;;ga;;;wd)");
    sddl("sd_empty", L"");
    sddl("sd_bad_ace_type", L"D:(X;;GA;;;WD)");
    sddl("sd_bad_rights", L"D:(A;;QQ;;;WD)");
    sddl("sd_bad_sid_alias", L"D:(A;;GA;;;ZZ)");
    sddl("sd_unterminated_ace", L"D:(A;;GA;;;WD");
    sddl("sd_missing_fields", L"D:(A;GA;WD)");
    sddl("sd_bad_owner", L"O:XXD:(A;;GA;;;WD)");
    sddl("sd_unknown_component", L"Q:BA");
    sddl("sd_object_ace", L"D:(OA;;CR;00299570-246d-11d0-a768-00aa006e0529;;WD)");
    {
        PSECURITY_DESCRIPTOR sd = NULL;
        BOOL r;
        ORACLE_CALL(r, ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:(A;;GA;;;WD)", 2, &sd, NULL));
        oracle_case("sd_bad_revision");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        if (r) LocalFree(sd);
        ORACLE_CALL(r, ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:(A;;GA;;;WD)", SDDL_REVISION_1, &sd, NULL));
        oracle_case("sd_null_size_out");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        if (r) LocalFree(sd);
    }
    return oracle_end();
}
