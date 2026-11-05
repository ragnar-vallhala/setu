/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: the owner's implicit rights in AccessCheck. When the SD's owner
 * is in the caller's token (as the user or an enabled group), READ_CONTROL
 * and WRITE_DAC are granted even if the DACL says nothing, unless the DACL
 * contains an OWNER RIGHTS (S-1-3-4) ACE, which then decides the owner's
 * rights. Also deny ACEs naming the owner, other standard rights
 * (WRITE_OWNER, DELETE, SYNCHRONIZE) that are not implicit, and
 * MAXIMUM_ALLOWED for owners. The user's SID is substituted as $U.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-accesscheck
 *       https://learn.microsoft.com/en-us/windows/win32/secauthz/how-dacls-control-access-to-an-object
 *       https://learn.microsoft.com/en-us/windows/win32/secauthz/well-known-sids
 *       https://learn.microsoft.com/en-us/windows/security/identity-protection/access-control/security-identifiers
 *       https://learn.microsoft.com/en-us/windows/win32/secauthz/security-descriptor-string-format
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

int main(void)
{
    oracle_begin("advapi32/AccessCheck_owner");
    acl_init();
    /* Start from the same privileges everywhere: some logons (an SSH session
     * of an administrator, for one) have these enabled by default. */
    {
        TOKEN_PRIVILEGES tp;
        tp.PrivilegeCount = 1;
        LookupPrivilegeValueW(NULL, L"SeTakeOwnershipPrivilege", &tp.Privileges[0].Luid);
        tp.Privileges[0].Attributes = 0;
        AdjustTokenPrivileges(g_prim, FALSE, &tp, 0, NULL, NULL);
        CloseHandle(g_imp);
        DuplicateToken(g_prim, SecurityImpersonation, &g_imp);
    }

    /* Owner is the user. */
    check("user_owner_empty_rc", L"O:$UG:SYD:", READ_CONTROL);
    check("user_owner_empty_wdac", L"O:$UG:SYD:", WRITE_DAC);
    check("user_owner_empty_both", L"O:$UG:SYD:", READ_CONTROL | WRITE_DAC);
    check("user_owner_empty_wo", L"O:$UG:SYD:", WRITE_OWNER);
    check("user_owner_empty_delete", L"O:$UG:SYD:", DELETE);
    check("user_owner_empty_sync", L"O:$UG:SYD:", SYNCHRONIZE);
    check("user_owner_empty_specific", L"O:$UG:SYD:", 0x1);
    check("user_owner_empty_max", L"O:$UG:SYD:", MAXIMUM_ALLOWED);
    check("user_owner_allow_max", L"O:$UG:SYD:(A;;0x3;;;WD)", MAXIMUM_ALLOWED);
    check("user_owner_allow_mixed", L"O:$UG:SYD:(A;;0x1;;;WD)", 0x1 | READ_CONTROL | WRITE_DAC);
    check("user_owner_allow_mixed_wo", L"O:$UG:SYD:(A;;0x1;;;WD)", 0x1 | WRITE_OWNER);

    /* Deny ACEs on the implicit rights. */
    check("user_owner_deny_rc", L"O:$UG:SYD:(D;;RC;;;$U)", READ_CONTROL);
    check("user_owner_deny_wdac", L"O:$UG:SYD:(D;;WD;;;$U)", WRITE_DAC);
    check("user_owner_deny_wdac_everyone", L"O:$UG:SYD:(D;;WD;;;WD)", WRITE_DAC);
    check("user_owner_deny_all_rc", L"O:$UG:SYD:(D;;GA;;;WD)", READ_CONTROL);
    check("user_owner_deny_all_max", L"O:$UG:SYD:(D;;GA;;;WD)", MAXIMUM_ALLOWED);
    check("user_owner_deny_rc_max", L"O:$UG:SYD:(D;;RC;;;WD)(A;;0x1;;;WD)", MAXIMUM_ALLOWED);

    /* Owner is a group in the token. */
    check("admins_owner_empty_rc", L"O:BAG:SYD:", READ_CONTROL);
    check("admins_owner_empty_max", L"O:BAG:SYD:", MAXIMUM_ALLOWED);
    check("everyone_owner_empty_rc", L"O:WDG:SYD:", READ_CONTROL);
    check("users_owner_empty_max", L"O:BUG:SYD:", MAXIMUM_ALLOWED);
    /* Owner not in the token, and the group never counts. */
    check("system_owner_empty_rc", L"O:SYG:SYD:", READ_CONTROL);
    check("system_owner_empty_max", L"O:SYG:SYD:", MAXIMUM_ALLOWED);
    check("group_is_user_empty_rc", L"O:SYG:$UD:", READ_CONTROL);
    check("group_is_admins_max", L"O:SYG:BAD:", MAXIMUM_ALLOWED);

    /* OWNER RIGHTS ACEs replace the implicit rights. */
    check("owner_rights_allow_specific", L"O:$UG:SYD:(A;;0x1;;;OW)", 0x1);
    check("owner_rights_allow_specific_rc", L"O:$UG:SYD:(A;;0x1;;;OW)", READ_CONTROL);
    check("owner_rights_allow_specific_wdac", L"O:$UG:SYD:(A;;0x1;;;OW)", WRITE_DAC);
    check("owner_rights_allow_specific_max", L"O:$UG:SYD:(A;;0x1;;;OW)", MAXIMUM_ALLOWED);
    check("owner_rights_allow_rc", L"O:$UG:SYD:(A;;RC;;;OW)", READ_CONTROL);
    check("owner_rights_allow_rc_wdac", L"O:$UG:SYD:(A;;RC;;;OW)", WRITE_DAC);
    check("owner_rights_allow_wo", L"O:$UG:SYD:(A;;WO;;;OW)", WRITE_OWNER);
    check("owner_rights_allow_wo_max", L"O:$UG:SYD:(A;;WO;;;OW)", MAXIMUM_ALLOWED);
    check("owner_rights_deny_wdac", L"O:$UG:SYD:(D;;WD;;;OW)", WRITE_DAC);
    check("owner_rights_deny_wdac_rc", L"O:$UG:SYD:(D;;WD;;;OW)", READ_CONTROL);
    check("owner_rights_deny_wdac_max", L"O:$UG:SYD:(D;;WD;;;OW)(A;;0x1;;;WD)", MAXIMUM_ALLOWED);
    check("owner_rights_deny_then_allow", L"O:$UG:SYD:(D;;WD;;;OW)(A;;WD;;;WD)", WRITE_DAC);
    check("owner_rights_inherit_only", L"O:$UG:SYD:(A;IO;0x1;;;OW)", READ_CONTROL);
    check("owner_rights_inherit_only_max", L"O:$UG:SYD:(A;OICIIO;0x1;;;OW)", MAXIMUM_ALLOWED);
    check("owner_rights_not_owner", L"O:SYG:SYD:(A;;0x3;;;OW)", 0x1);
    check("owner_rights_not_owner_max", L"O:SYG:SYD:(A;;0x3;;;OW)(A;;0x4;;;WD)", MAXIMUM_ALLOWED);
    check("owner_rights_admins_owner", L"O:BAG:SYD:(A;;0x1;;;OW)", MAXIMUM_ALLOWED);
    check("owner_rights_plus_everyone", L"O:$UG:SYD:(A;;0x1;;;OW)(A;;RC;;;WD)", MAXIMUM_ALLOWED);

    /* CREATOR OWNER in a DACL is only an inheritance placeholder. */
    check("creator_owner_allow", L"O:$UG:SYD:(A;;0x1;;;CO)", 0x1);
    check("creator_owner_allow_max", L"O:$UG:SYD:(A;;0x1;;;CO)", MAXIMUM_ALLOWED);

    return oracle_end();
}
