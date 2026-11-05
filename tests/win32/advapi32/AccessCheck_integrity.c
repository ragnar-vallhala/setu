/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: mandatory integrity labels in AccessCheck. The SD's SACL may
 * hold a SYSTEM_MANDATORY_LABEL ACE (SDDL "ML") with a level and the policy
 * bits NW (no-write-up), NR (no-read-up) and NX (no-execute-up). A token whose
 * integrity level is below the label loses the generic write/read/execute
 * rights the policy names (as mapped by the caller's GENERIC_MAPPING); an SD
 * without a label counts as medium with no-write-up. The test lowers copies
 * of its own token with SetTokenInformation(TokenIntegrityLevel); the
 * token's starting level is printed as an RID.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/secauthz/mandatory-integrity-control
 *       https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-system_mandatory_label_ace
 *       https://learn.microsoft.com/en-us/windows/win32/secauthz/ace-strings
 *       https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-token_mandatory_label
 *       https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-settokeninformation
 *       https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-accesscheck
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

static DWORD token_il(HANDLE t)
{
    BYTE buf[128];
    DWORD n = 0;
    if (!GetTokenInformation(t, TokenIntegrityLevel, buf, sizeof buf, &n)) return 0xFFFFFFFF;
    PSID s = ((TOKEN_MANDATORY_LABEL *)buf)->Label.Sid;
    return *GetSidSubAuthority(s, *GetSidSubAuthorityCount(s) - 1);
}

static HANDLE at_level(const char *name, const wchar_t *level_sid)
{
    HANDLE t = NULL;
    PSID s = NULL;
    TOKEN_MANDATORY_LABEL tml;
    BOOL r;
    DuplicateTokenEx(g_prim, TOKEN_ALL_ACCESS, NULL, SecurityImpersonation, TokenImpersonation, &t);
    ConvertStringSidToSidW(level_sid, &s);
    tml.Label.Sid = s;
    tml.Label.Attributes = SE_GROUP_INTEGRITY;
    ORACLE_CALL(r, SetTokenInformation(t, TokenIntegrityLevel, &tml, sizeof tml + GetLengthSid(s)));
    oracle_case(name);
    o_bool("ret", r);
    o_lasterr();
    o_hex32("level", token_il(t));
    oracle_case_end();
    LocalFree(s);
    return t;
}

static void chk(const char *name, const wchar_t *sddl, HANDLE tok, DWORD desired)
{
    check_tok(name, sddl, tok, desired, &g_map);
}

int main(void)
{
    oracle_begin("advapi32/AccessCheck_integrity");
    acl_init();

    oracle_case("own_token_level");
    o_hex32("level", token_il(g_prim));
    oracle_case_end();

    const wchar_t *all = L"O:SYG:SYD:(A;;0x000F000F;;;WD)";
    /* The test's own (high, elevated) token. */
    chk("own_label_high_nw_write", L"O:SYG:SYD:(A;;0x000F000F;;;WD)S:(ML;;NW;;;HI)", g_imp, 0x2);
    chk("own_label_system_nw_write", L"O:SYG:SYD:(A;;0x000F000F;;;WD)S:(ML;;NW;;;SI)", g_imp, 0x2);
    chk("own_label_system_nw_max", L"O:SYG:SYD:(A;;0x000F000F;;;WD)S:(ML;;NW;;;SI)", g_imp, MAXIMUM_ALLOWED);

    HANDLE med = at_level("set_medium", L"S-1-16-8192");
    HANDLE low = at_level("set_low", L"S-1-16-4096");
    HANDLE unt = at_level("set_untrusted", L"S-1-16-0");
    HANDLE raise = at_level("raise_to_system", L"S-1-16-16384");
    HANDLE med_up = NULL;
    {
        /* raising a lowered token back up */
        PSID s = NULL;
        TOKEN_MANDATORY_LABEL tml;
        BOOL r;
        DuplicateTokenEx(med, TOKEN_ALL_ACCESS, NULL, SecurityImpersonation, TokenImpersonation, &med_up);
        ConvertStringSidToSidW(L"S-1-16-12288", &s);
        tml.Label.Sid = s;
        tml.Label.Attributes = SE_GROUP_INTEGRITY;
        ORACLE_CALL(r, SetTokenInformation(med_up, TokenIntegrityLevel, &tml, sizeof tml + GetLengthSid(s)));
        oracle_case("raise_medium_to_high");
        o_bool("ret", r);
        o_lasterr();
        o_hex32("level", token_il(med_up));
        oracle_case_end();
        LocalFree(s);
    }

    /* No label: medium, no-write-up. */
    chk("nolabel_medium_write", all, med, 0x2);
    chk("nolabel_low_read", all, low, 0x1);
    chk("nolabel_low_write", all, low, 0x2);
    chk("nolabel_low_exec", all, low, 0x4);
    chk("nolabel_low_max", all, low, MAXIMUM_ALLOWED);
    chk("nolabel_untrusted_write", all, unt, 0x2);
    chk("nolabel_low_wdac", all, low, WRITE_DAC);
    chk("nolabel_low_delete", all, low, DELETE);
    chk("nolabel_low_rc", all, low, READ_CONTROL);
    chk("nolabel_low_null_dacl_write", L"O:SYG:SYD:NO_ACCESS_CONTROL", low, 0x2);
    chk("nolabel_low_null_dacl_max", L"O:SYG:SYD:NO_ACCESS_CONTROL", low, MAXIMUM_ALLOWED);

    /* High label, NW. */
    const wchar_t *hi_nw = L"O:SYG:SYD:(A;;0x000F000F;;;WD)S:(ML;;NW;;;HI)";
    chk("hi_nw_medium_read", hi_nw, med, 0x1);
    chk("hi_nw_medium_write", hi_nw, med, 0x2);
    chk("hi_nw_medium_exec", hi_nw, med, 0x4);
    chk("hi_nw_medium_max", hi_nw, med, MAXIMUM_ALLOWED);
    chk("hi_nw_medium_wdac", hi_nw, med, WRITE_DAC);
    chk("hi_nw_medium_wo", hi_nw, med, WRITE_OWNER);
    chk("hi_nw_medium_delete", hi_nw, med, DELETE);
    chk("hi_nw_medium_rc", hi_nw, med, READ_CONTROL);
    chk("hi_nw_medium_raised_write", hi_nw, med_up, 0x2);
    chk("hi_nw_system_token_write", hi_nw, raise, 0x2);

    /* NR, NX, combos. */
    chk("hi_nr_medium_read", L"O:SYG:SYD:(A;;0x000F000F;;;WD)S:(ML;;NR;;;HI)", med, 0x1);
    chk("hi_nr_medium_write", L"O:SYG:SYD:(A;;0x000F000F;;;WD)S:(ML;;NR;;;HI)", med, 0x2);
    chk("hi_nr_medium_max", L"O:SYG:SYD:(A;;0x000F000F;;;WD)S:(ML;;NR;;;HI)", med, MAXIMUM_ALLOWED);
    chk("hi_nx_medium_exec", L"O:SYG:SYD:(A;;0x000F000F;;;WD)S:(ML;;NX;;;HI)", med, 0x4);
    chk("hi_nx_medium_max", L"O:SYG:SYD:(A;;0x000F000F;;;WD)S:(ML;;NX;;;HI)", med, MAXIMUM_ALLOWED);
    chk("hi_nwnrnx_medium_max", L"O:SYG:SYD:(A;;0x000F000F;;;WD)S:(ML;;NWNRNX;;;HI)", med, MAXIMUM_ALLOWED);
    chk("hi_nwnrnx_medium_rc", L"O:SYG:SYD:(A;;0x000F000F;;;WD)S:(ML;;NWNRNX;;;HI)", med, READ_CONTROL);
    chk("hi_nopolicy_medium_write", L"O:SYG:SYD:(A;;0x000F000F;;;WD)S:(ML;;;;;HI)", med, 0x2);

    /* Equal and lower labels. */
    chk("me_nw_medium_write", L"O:SYG:SYD:(A;;0x000F000F;;;WD)S:(ML;;NW;;;ME)", med, 0x2);
    chk("lw_nw_low_write", L"O:SYG:SYD:(A;;0x000F000F;;;WD)S:(ML;;NW;;;LW)", low, 0x2);
    chk("lw_nw_untrusted_write", L"O:SYG:SYD:(A;;0x000F000F;;;WD)S:(ML;;NW;;;LW)", unt, 0x2);
    chk("lw_nwnr_untrusted_max", L"O:SYG:SYD:(A;;0x000F000F;;;WD)S:(ML;;NWNR;;;LW)", unt, MAXIMUM_ALLOWED);

    /* Labels interact with the DACL: integrity never adds rights. */
    chk("me_nw_low_dacl_read_only", L"O:SYG:SYD:(A;;0x1;;;WD)S:(ML;;NW;;;ME)", low, MAXIMUM_ALLOWED);
    chk("hi_nw_medium_empty_dacl", L"O:SYG:SYD:S:(ML;;NW;;;HI)", med, MAXIMUM_ALLOWED);
    chk("hi_nw_medium_owner", L"O:$UG:SYD:S:(ML;;NW;;;HI)", med, MAXIMUM_ALLOWED);
    chk("hi_nw_medium_owner_wdac", L"O:$UG:SYD:S:(ML;;NW;;;HI)", med, WRITE_DAC);
    chk("hi_nw_medium_null_dacl_max", L"O:SYG:SYD:NO_ACCESS_CONTROLS:(ML;;NW;;;HI)", med, MAXIMUM_ALLOWED);
    chk("hi_nw_inherit_only_label", L"O:SYG:SYD:(A;;0x000F000F;;;WD)S:(ML;OICIIO;NW;;;HI)", med, 0x2);
    chk("label_with_audit_ace", L"O:SYG:SYD:(A;;0x000F000F;;;WD)S:(AU;SA;0x2;;;WD)(ML;;NW;;;HI)", med, 0x2);

    /* Restricted token keeps the level. */
    {
        HANDLE p = NULL, imp = NULL;
        CreateRestrictedToken(g_prim, 0, 0, NULL, 0, NULL, 0, NULL, &p);
        DuplicateToken(p, SecurityImpersonation, &imp);
        oracle_case("restricted_copy_level");
        o_hex32("level", token_il(imp));
        oracle_case_end();
        CloseHandle(imp);
        CloseHandle(p);
    }

    CloseHandle(med);
    CloseHandle(low);
    CloseHandle(unt);
    CloseHandle(raise);
    CloseHandle(med_up);
    return oracle_end();
}
