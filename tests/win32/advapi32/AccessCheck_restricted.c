/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: AccessCheck with tokens made by CreateRestrictedToken.
 * Deny-only SIDs (SidsToDisable) still match deny ACEs but never allow ACEs;
 * restricting SIDs add a second access check, and only rights granted by both
 * the normal SIDs and the restricting SIDs are granted; WRITE_RESTRICTED
 * applies the restricting SIDs to write rights only; the owner's implicit
 * rights under restriction; DISABLE_MAX_PRIVILEGE. Also IsTokenRestricted and
 * the TokenRestrictedSids / TokenGroups attributes CreateRestrictedToken sets.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-createrestrictedtoken
 *       https://learn.microsoft.com/en-us/windows/win32/secauthz/restricted-tokens
 *       https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-accesscheck
 *       https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-istokenrestricted
 *       https://learn.microsoft.com/en-us/windows/win32/api/winnt/ne-winnt-token_information_class
 *       https://learn.microsoft.com/en-us/windows/win32/secauthz/sid-attributes-in-an-access-token
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

static PSID sid_of(const wchar_t *str)
{
    wchar_t buf[256];
    PSID s = NULL;
    expand(str, buf);
    ConvertStringSidToSidW(buf, &s);
    return s;
}

/* Restricted impersonation token from the process token. */
static HANDLE restricted(const char *name, DWORD flags, const wchar_t *disable, const wchar_t *restrict_)
{
    SID_AND_ATTRIBUTES dis[4], res[4];
    DWORD nd = 0, nr = 0;
    wchar_t buf[512], *p, *q;
    HANDLE prim = NULL, imp = NULL;
    BOOL r;
    if (disable) {
        wcscpy(buf, disable);
        for (p = buf; p; p = q) {
            q = wcschr(p, L',');
            if (q) *q++ = 0;
            dis[nd].Sid = sid_of(p);
            dis[nd++].Attributes = 0;
        }
    }
    if (restrict_) {
        wcscpy(buf, restrict_);
        for (p = buf; p; p = q) {
            q = wcschr(p, L',');
            if (q) *q++ = 0;
            res[nr].Sid = sid_of(p);
            res[nr++].Attributes = 0;
        }
    }
    ORACLE_CALL(r, CreateRestrictedToken(g_prim, flags, nd, nd ? dis : NULL, 0, NULL, nr, nr ? res : NULL, &prim));
    oracle_case(name);
    o_bool("ret", r);
    o_lasterr();
    if (r) {
        BYTE tbuf[4096];
        DWORD n = 0;
        o_bool("is_restricted", IsTokenRestricted(prim));
        if (GetTokenInformation(prim, TokenRestrictedSids, tbuf, sizeof tbuf, &n))
            o_u32("restricted_sids", ((TOKEN_GROUPS *)tbuf)->GroupCount);
        else
            o_u32("restricted_sids_error", GetLastError());
        for (DWORD i = 0; i < nd; i++) {
            /* attributes of each disabled SID inside TokenGroups */
            if (!GetTokenInformation(prim, TokenGroups, tbuf, sizeof tbuf, &n)) break;
            TOKEN_GROUPS *g = (TOKEN_GROUPS *)tbuf;
            char k[32];
            snprintf(k, sizeof k, "disabled%lu_attrs", (unsigned long)i);
            int found = 0;
            for (DWORD j = 0; j < g->GroupCount; j++)
                if (EqualSid(g->Groups[j].Sid, dis[i].Sid)) {
                    o_hex32(k, g->Groups[j].Attributes & ~SE_GROUP_OWNER);
                    found = 1;
                }
            if (!found) o_str(k, "not in groups");
        }
        DuplicateToken(prim, SecurityImpersonation, &imp);
        CloseHandle(prim);
    }
    oracle_case_end();
    for (DWORD i = 0; i < nd; i++) LocalFree(dis[i].Sid);
    for (DWORD i = 0; i < nr; i++) LocalFree(res[i].Sid);
    return imp;
}

static void chk(const char *name, const wchar_t *sddl, HANDLE tok, DWORD desired)
{
    check_tok(name, sddl, tok, desired, &g_map);
}

int main(void)
{
    oracle_begin("advapi32/AccessCheck_restricted");
    acl_init();
    HANDLE t;

    /* Administrators as a deny-only group. */
    t = restricted("make_deny_only_admins", 0, L"BA", NULL);
    chk("deny_only_allow_admins", L"O:SYG:SYD:(A;;0x3;;;BA)", t, 0x1);
    chk("deny_only_allow_admins_max", L"O:SYG:SYD:(A;;0x3;;;BA)(A;;0x4;;;WD)", t, MAXIMUM_ALLOWED);
    chk("deny_only_deny_admins", L"O:SYG:SYD:(D;;0x1;;;BA)(A;;0x3;;;WD)", t, 0x1);
    chk("deny_only_deny_admins_other", L"O:SYG:SYD:(D;;0x1;;;BA)(A;;0x3;;;WD)", t, 0x2);
    chk("deny_only_owner_admins", L"O:BAG:SYD:", t, READ_CONTROL);
    chk("deny_only_owner_admins_max", L"O:BAG:SYD:", t, MAXIMUM_ALLOWED);
    chk("deny_only_allow_everyone", L"O:SYG:SYD:(A;;0x3;;;WD)", t, 0x3);
    CloseHandle(t);

    /* The user SID as deny-only. */
    t = restricted("make_deny_only_user", 0, L"$U", NULL);
    chk("deny_only_user_allow_user", L"O:SYG:SYD:(A;;0x3;;;$U)", t, 0x1);
    chk("deny_only_user_deny_user", L"O:SYG:SYD:(D;;0x1;;;$U)(A;;0x3;;;WD)", t, 0x1);
    chk("deny_only_user_owner", L"O:$UG:SYD:", t, READ_CONTROL);
    chk("deny_only_user_owner_max", L"O:$UG:SYD:(A;;0x1;;;WD)", t, MAXIMUM_ALLOWED);
    CloseHandle(t);

    /* Everyone as deny-only (Everyone is mandatory: it may be refused). */
    t = restricted("make_deny_only_everyone", 0, L"WD", NULL);
    if (t) {
        chk("deny_only_everyone_allow", L"O:SYG:SYD:(A;;0x3;;;WD)", t, 0x1);
        CloseHandle(t);
    }
    /* A SID not in the token. */
    t = restricted("make_deny_only_absent", 0, L"SY", NULL);
    if (t) {
        chk("deny_only_absent_allow_system", L"O:SYG:SYD:(A;;0x3;;;SY)", t, 0x1);
        CloseHandle(t);
    }

    /* Restricting SIDs: two checks, intersection. */
    t = restricted("make_restrict_everyone", 0, NULL, L"WD");
    chk("restrict_wd_allow_wd", L"O:SYG:SYD:(A;;0x3;;;WD)", t, 0x3);
    chk("restrict_wd_allow_user", L"O:SYG:SYD:(A;;0x3;;;$U)", t, 0x1);
    chk("restrict_wd_split", L"O:SYG:SYD:(A;;0x3;;;$U)(A;;0x1;;;WD)", t, 0x1);
    chk("restrict_wd_split_both", L"O:SYG:SYD:(A;;0x3;;;$U)(A;;0x1;;;WD)", t, 0x3);
    chk("restrict_wd_split_max", L"O:SYG:SYD:(A;;0x3;;;$U)(A;;0x5;;;WD)", t, MAXIMUM_ALLOWED);
    chk("restrict_wd_deny_wd", L"O:SYG:SYD:(D;;0x1;;;WD)(A;;0x3;;;$U)", t, 0x1);
    chk("restrict_wd_deny_user", L"O:SYG:SYD:(D;;0x1;;;$U)(A;;0x3;;;WD)", t, 0x1);
    chk("restrict_wd_deny_user_other", L"O:SYG:SYD:(D;;0x1;;;$U)(A;;0x3;;;WD)", t, 0x2);
    chk("restrict_wd_null_dacl", L"O:SYG:SYD:NO_ACCESS_CONTROL", t, 0x7);
    chk("restrict_wd_owner_user", L"O:$UG:SYD:", t, READ_CONTROL);
    chk("restrict_wd_owner_user_max", L"O:$UG:SYD:(A;;0x1;;;WD)", t, MAXIMUM_ALLOWED);
    chk("restrict_wd_owner_everyone", L"O:WDG:SYD:", t, READ_CONTROL);
    chk("restrict_wd_allow_rc_only", L"O:SYG:SYD:(A;;0x1;;;$U)(A;;0x1;;;RC)", t, 0x1);
    CloseHandle(t);

    t = restricted("make_restrict_rc", 0, NULL, L"RC");
    chk("restrict_rc_allow_wd", L"O:SYG:SYD:(A;;0x3;;;WD)", t, 0x1);
    chk("restrict_rc_allow_both", L"O:SYG:SYD:(A;;0x3;;;WD)(A;;0x1;;;RC)", t, 0x1);
    chk("restrict_rc_allow_both_w", L"O:SYG:SYD:(A;;0x3;;;WD)(A;;0x1;;;RC)", t, 0x2);
    chk("restrict_rc_max", L"O:SYG:SYD:(A;;0x7;;;WD)(A;;0x3;;;RC)", t, MAXIMUM_ALLOWED);
    chk("restrict_rc_deny_rc", L"O:SYG:SYD:(D;;0x1;;;RC)(A;;0x3;;;WD)(A;;0x3;;;RC)", t, 0x1);
    CloseHandle(t);

    t = restricted("make_restrict_user_admins", 0, NULL, L"$U,BA");
    chk("restrict_ua_allow_admins", L"O:SYG:SYD:(A;;0x3;;;BA)", t, 0x3);
    chk("restrict_ua_allow_everyone", L"O:SYG:SYD:(A;;0x3;;;WD)", t, 0x1);
    chk("restrict_ua_mix_max", L"O:SYG:SYD:(A;;0x7;;;WD)(A;;0x1;;;$U)(A;;0x2;;;BA)", t, MAXIMUM_ALLOWED);
    CloseHandle(t);

    /* Restricting SID that the token doesn't otherwise hold. */
    t = restricted("make_restrict_system", 0, NULL, L"SY");
    chk("restrict_sy_allow_both", L"O:SYG:SYD:(A;;0x3;;;WD)(A;;0x3;;;SY)", t, 0x3);
    chk("restrict_sy_allow_sy_only", L"O:SYG:SYD:(A;;0x3;;;SY)", t, 0x1);
    CloseHandle(t);

    /* WRITE_RESTRICTED: restricting SIDs apply to write rights only. */
    t = restricted("make_write_restricted", WRITE_RESTRICTED, NULL, L"RC");
    chk("write_restricted_read", L"O:SYG:SYD:(A;;0x3;;;WD)", t, 0x1);
    chk("write_restricted_write", L"O:SYG:SYD:(A;;0x3;;;WD)", t, 0x2);
    chk("write_restricted_write_ok", L"O:SYG:SYD:(A;;0x3;;;WD)(A;;0x2;;;RC)", t, 0x2);
    chk("write_restricted_max", L"O:SYG:SYD:(A;;0x7;;;WD)", t, MAXIMUM_ALLOWED);
    chk("write_restricted_wdac", L"O:SYG:SYD:(A;;0x000F0007;;;WD)", t, WRITE_DAC);
    chk("write_restricted_delete", L"O:SYG:SYD:(A;;0x000F0007;;;WD)", t, DELETE);
    CloseHandle(t);

    /* Both deny-only and restricting SIDs. */
    t = restricted("make_deny_and_restrict", 0, L"BA", L"BA,WD");
    chk("both_allow_admins", L"O:SYG:SYD:(A;;0x3;;;BA)", t, 0x1);
    chk("both_allow_everyone", L"O:SYG:SYD:(A;;0x3;;;WD)", t, 0x3);
    CloseHandle(t);

    /* DISABLE_MAX_PRIVILEGE. */
    t = restricted("make_disable_max_privilege", DISABLE_MAX_PRIVILEGE, NULL, NULL);
    if (t) {
        BYTE buf[4096];
        DWORD n = 0;
        GetTokenInformation(t, TokenPrivileges, buf, sizeof buf, &n);
        oracle_case("disable_max_privilege_count");
        o_u32("count", ((TOKEN_PRIVILEGES *)buf)->PrivilegeCount);
        oracle_case_end();
        chk("disable_max_privilege_allow", L"O:SYG:SYD:(A;;0x3;;;WD)", t, 0x3);
        CloseHandle(t);
    }

    /* Restricting an already restricted token. */
    {
        HANDLE p1 = NULL, p2 = NULL, imp = NULL;
        SID_AND_ATTRIBUTES ra, rb;
        BOOL r;
        ra.Sid = sid_of(L"WD"); ra.Attributes = 0;
        rb.Sid = sid_of(L"RC"); rb.Attributes = 0;
        CreateRestrictedToken(g_prim, 0, 0, NULL, 0, NULL, 1, &ra, &p1);
        ORACLE_CALL(r, CreateRestrictedToken(p1, 0, 0, NULL, 0, NULL, 1, &rb, &p2));
        oracle_case("make_restrict_twice");
        o_bool("ret", r);
        o_lasterr();
        if (r) {
            BYTE tbuf[1024];
            DWORD n = 0;
            if (GetTokenInformation(p2, TokenRestrictedSids, tbuf, sizeof tbuf, &n))
                o_u32("restricted_sids", ((TOKEN_GROUPS *)tbuf)->GroupCount);
        }
        oracle_case_end();
        if (r) {
            DuplicateToken(p2, SecurityImpersonation, &imp);
            chk("restrict_twice_wd", L"O:SYG:SYD:(A;;0x3;;;WD)", imp, 0x1);
            chk("restrict_twice_both", L"O:SYG:SYD:(A;;0x3;;;WD)(A;;0x3;;;RC)", imp, 0x1);
            CloseHandle(imp);
            CloseHandle(p2);
        }
        CloseHandle(p1);
        LocalFree(ra.Sid);
        LocalFree(rb.Sid);
    }
    oracle_case("own_token_not_restricted");
    o_bool("is_restricted", IsTokenRestricted(g_prim));
    oracle_case_end();
    return oracle_end();
}
