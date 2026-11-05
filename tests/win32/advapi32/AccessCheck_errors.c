/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: AccessCheck's error paths. A primary token instead of an
 * impersonation token, identification and anonymous impersonation levels,
 * a token handle without TOKEN_QUERY, NULL and invalid handles, security
 * descriptors without an owner or group, absolute (not self-relative) SDs,
 * generic rights left in the desired mask, and privilege-set buffers that are
 * too small (the needed length is reported back).
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-accesscheck
 *       https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-duplicatetoken
 *       https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-duplicatetokenex
 *       https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-initializesecuritydescriptor
 *       https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-privilege_set
 *       https://learn.microsoft.com/en-us/windows/win32/debug/system-error-codes--1300-1699-
 */
#include "oracle.h"
#include <sddl.h>

/* ---- shared ACL test helpers (kept inline: one file per test) ---------- */
static HANDLE g_prim, g_imp;
static wchar_t g_user[192];

static void acl_init(void)
{
    BYTE buf[512];
    DWORD n = 0;
    LPWSTR s = NULL;
    OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_DUPLICATE | TOKEN_ADJUST_PRIVILEGES, &g_prim);
    DuplicateToken(g_prim, SecurityImpersonation, &g_imp);
    GetTokenInformation(g_prim, TokenUser, buf, sizeof buf, &n);
    ConvertSidToStringSidW(((TOKEN_USER *)buf)->User.Sid, &s);
    wcscpy(g_user, s);
    LocalFree(s);
}

/* Expand $U (the user's SID string) in an SDDL template. */
static void expand(const wchar_t *in, wchar_t *out)
{
    while (*in) {
        if (in[0] == L'$' && in[1] == L'U') { wcscpy(out, g_user); out += wcslen(g_user); in += 2; }
        else *out++ = *in++;
    }
    *out = 0;
}

static PSECURITY_DESCRIPTOR sd_from(const wchar_t *tmpl)
{
    wchar_t buf[2048];
    PSECURITY_DESCRIPTOR sd = NULL;
    expand(tmpl, buf);
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(buf, SDDL_REVISION_1, &sd, NULL)) return NULL;
    return sd;
}
/* ----------------------------------------------------------------------- */
static __attribute__((unused)) GENERIC_MAPPING g_map = { 0x0001, 0x0002, 0x0004, 0x000F }; /* read, write, exec, all */
static __attribute__((unused)) GENERIC_MAPPING g_fmap = { FILE_GENERIC_READ, FILE_GENERIC_WRITE, FILE_GENERIC_EXECUTE, FILE_ALL_ACCESS };

static __attribute__((unused)) void check_tok(const char *name, const wchar_t *sddl, HANDLE tok, DWORD desired, GENERIC_MAPPING *map)
{
    PSECURITY_DESCRIPTOR sd = sd_from(sddl);
    BYTE psbuf[256];
    PRIVILEGE_SET *ps = (PRIVILEGE_SET *)psbuf;
    DWORD pslen = sizeof psbuf, granted = 0xDEADBEEF;
    BOOL status = 0x5A5A, r;
    memset(psbuf, 0xCC, sizeof psbuf);
    oracle_case(name);
    if (!sd) {
        o_str("sddl", "parse failed");
        oracle_case_end();
        return;
    }
    ORACLE_CALL(r, AccessCheck(sd, tok, desired, map, ps, &pslen, &granted, &status));
    o_bool("ret", r);
    o_lasterr();
    if (r) {
        o_hex32("granted", granted);
        o_bool("status", status);
        o_u32("privset_len", pslen);
        o_u32("priv_count", ps->PrivilegeCount);
    }
    oracle_case_end();
    LocalFree(sd);
}

static __attribute__((unused)) void check(const char *name, const wchar_t *sddl, DWORD desired)
{
    check_tok(name, sddl, g_imp, desired, &g_map);
}

/* AccessCheck with an explicit privilege-set buffer length. */
static void check_pslen(const char *name, const wchar_t *sddl, DWORD desired, DWORD pslen_in)
{
    PSECURITY_DESCRIPTOR sd = sd_from(sddl);
    BYTE psbuf[256];
    DWORD pslen = pslen_in, granted = 0xDEADBEEF;
    BOOL status = 0x5A5A, r;
    memset(psbuf, 0xCC, sizeof psbuf);
    ORACLE_CALL(r, AccessCheck(sd, g_imp, desired, &g_map, (PRIVILEGE_SET *)psbuf, &pslen, &granted, &status));
    oracle_case(name);
    o_bool("ret", r);
    o_lasterr();
    o_u32("privset_len", pslen);
    o_bool("granted_untouched", granted == 0xDEADBEEF);
    if (r) {
        o_hex32("granted", granted);
        o_bool("status", status);
    }
    oracle_case_end();
    LocalFree(sd);
}

static void check_sd(const char *name, PSECURITY_DESCRIPTOR sd, HANDLE tok, DWORD desired)
{
    BYTE psbuf[256];
    DWORD pslen = sizeof psbuf, granted = 0xDEADBEEF;
    BOOL status = 0x5A5A, r;
    ORACLE_CALL(r, AccessCheck(sd, tok, desired, &g_map, (PRIVILEGE_SET *)psbuf, &pslen, &granted, &status));
    oracle_case(name);
    o_bool("ret", r);
    o_lasterr();
    if (r) {
        o_hex32("granted", granted);
        o_bool("status", status);
    } else {
        o_bool("granted_untouched", granted == 0xDEADBEEF);
    }
    oracle_case_end();
}

int main(void)
{
    oracle_begin("advapi32/AccessCheck_errors");
    acl_init();
    const wchar_t *ok = L"O:SYG:SYD:(A;;0x3;;;WD)";

    /* Token kinds. */
    check_tok("primary_token", ok, g_prim, 0x1, &g_map);
    HANDLE ident = NULL, anon = NULL, query_only = NULL, noquery = NULL, primdup = NULL;
    DuplicateToken(g_prim, SecurityIdentification, &ident);
    check_tok("identification_token", ok, ident, 0x1, &g_map);
    BOOL r;
    ORACLE_CALL(r, DuplicateToken(g_prim, SecurityAnonymous, &anon));
    oracle_case("duplicate_anonymous");
    o_bool("ret", r);
    o_lasterr();
    oracle_case_end();
    if (r) check_tok("anonymous_token", ok, anon, 0x1, &g_map);
    DuplicateToken(g_prim, SecurityDelegation, &primdup);
    check_tok("delegation_token", ok, primdup, 0x1, &g_map);
    DuplicateHandle(GetCurrentProcess(), g_imp, GetCurrentProcess(), &query_only, TOKEN_QUERY, FALSE, 0);
    check_tok("token_query_only", ok, query_only, 0x1, &g_map);
    DuplicateHandle(GetCurrentProcess(), g_imp, GetCurrentProcess(), &noquery, TOKEN_IMPERSONATE, FALSE, 0);
    check_tok("token_without_query", ok, noquery, 0x1, &g_map);
    check_tok("null_token", ok, NULL, 0x1, &g_map);
    check_tok("pseudo_process_handle", ok, GetCurrentProcess(), 0x1, &g_map);
    HANDLE ev = CreateEventW(NULL, FALSE, FALSE, NULL);
    check_tok("event_handle", ok, ev, 0x1, &g_map);
    HANDLE dupprim = NULL;
    DuplicateTokenEx(g_prim, TOKEN_ALL_ACCESS, NULL, SecurityImpersonation, TokenPrimary, &dupprim);
    check_tok("duplicated_primary", ok, dupprim, 0x1, &g_map);

    /* Generic rights in the desired mask. */
    check("desired_generic_read", ok, GENERIC_READ);
    check("desired_generic_all", ok, GENERIC_ALL);
    check("desired_generic_with_max", ok, GENERIC_READ | MAXIMUM_ALLOWED);
    check("desired_generic_null_dacl", L"O:SYG:SYD:NO_ACCESS_CONTROL", GENERIC_WRITE);
    check("desired_generic_primary", ok, GENERIC_READ);
    check_tok("desired_generic_primary_token", ok, g_prim, GENERIC_READ, &g_map);

    /* SDs missing parts. */
    check("no_owner", L"G:SYD:(A;;0x3;;;WD)", 0x1);
    check("no_group", L"O:SYD:(A;;0x3;;;WD)", 0x1);
    check("no_owner_no_group", L"D:(A;;0x3;;;WD)", 0x1);
    check("no_owner_null_dacl", L"G:SYD:NO_ACCESS_CONTROL", 0x1);
    check("no_owner_max", L"G:SYD:(A;;0x3;;;WD)", MAXIMUM_ALLOWED);
    check_tok("no_owner_primary_token", L"G:SYD:(A;;0x3;;;WD)", g_prim, 0x1, &g_map);

    /* Absolute security descriptors work too. */
    {
        SECURITY_DESCRIPTOR abs;
        BYTE aclbuf[256];
        PACL acl = (PACL)aclbuf;
        PSID world = NULL, sys = NULL;
        SID_IDENTIFIER_AUTHORITY wa = { SECURITY_WORLD_SID_AUTHORITY }, na = { SECURITY_NT_AUTHORITY };
        AllocateAndInitializeSid(&wa, 1, SECURITY_WORLD_RID, 0, 0, 0, 0, 0, 0, 0, &world);
        AllocateAndInitializeSid(&na, 1, SECURITY_LOCAL_SYSTEM_RID, 0, 0, 0, 0, 0, 0, 0, &sys);
        InitializeAcl(acl, sizeof aclbuf, ACL_REVISION);
        AddAccessAllowedAce(acl, ACL_REVISION, 0x3, world);
        InitializeSecurityDescriptor(&abs, SECURITY_DESCRIPTOR_REVISION);
        check_sd("absolute_empty_sd", &abs, g_imp, 0x1);
        SetSecurityDescriptorOwner(&abs, sys, FALSE);
        SetSecurityDescriptorGroup(&abs, sys, FALSE);
        check_sd("absolute_no_dacl", &abs, g_imp, 0x1);
        SetSecurityDescriptorDacl(&abs, TRUE, NULL, FALSE);
        check_sd("absolute_null_dacl", &abs, g_imp, 0x1);
        SetSecurityDescriptorDacl(&abs, TRUE, acl, FALSE);
        check_sd("absolute_allow", &abs, g_imp, 0x3);
        check_sd("absolute_allow_denied", &abs, g_imp, 0x4);
        SetSecurityDescriptorDacl(&abs, FALSE, acl, FALSE);
        check_sd("absolute_dacl_not_present", &abs, g_imp, 0x4);
        FreeSid(world);
        FreeSid(sys);
    }

    /* Privilege-set buffer length. */
    check_pslen("pslen_zero", ok, 0x1, 0);
    check_pslen("pslen_header_minus_one", ok, 0x1, sizeof(PRIVILEGE_SET) - 1);
    check_pslen("pslen_8", ok, 0x1, 8);
    check_pslen("pslen_header", ok, 0x1, sizeof(PRIVILEGE_SET));
    check_pslen("pslen_large", ok, 0x1, 256);
    check_pslen("pslen_zero_denied", ok, 0x4, 0);
    check_pslen("pslen_zero_null_dacl", L"O:SYG:SYD:NO_ACCESS_CONTROL", 0x1, 0);
    check_pslen("pslen_zero_no_owner", L"G:SYD:(A;;0x3;;;WD)", 0x1, 0);
    check_pslen("pslen_zero_generic", ok, GENERIC_READ, 0);

    /* Denied results set ERROR_ACCESS_DENIED but the call itself succeeds. */
    check("denied_last_error", ok, 0x4);
    check("granted_last_error", ok, 0x1);

    CloseHandle(ev);
    return oracle_end();
}
