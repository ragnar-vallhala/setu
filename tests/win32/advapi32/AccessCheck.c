/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: AccessCheck DACL evaluation against security descriptors the
 * test builds from SDDL, with an impersonation copy of the test's own token:
 * NULL vs empty DACL, which well-known groups match, ACE order (deny before
 * and after allow, partial masks accumulating over several ACEs), inherit-only
 * and inherited ACEs, generic rights in ACE masks (mapped with the caller's
 * GENERIC_MAPPING), MAXIMUM_ALLOWED, and the returned privilege set size.
 *
 * Owners are SYSTEM unless a case is about the owner, so the owner's implicit
 * rights never mix in (AccessCheck_owner covers those). The user's own SID is
 * substituted into SDDL as $U; nothing machine-specific is printed.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-accesscheck
 *       https://learn.microsoft.com/en-us/windows/win32/secauthz/how-dacls-control-access-to-an-object
 *       https://learn.microsoft.com/en-us/windows/win32/secauthz/order-of-aces-in-a-dacl
 *       https://learn.microsoft.com/en-us/windows/win32/secauthz/null-dacls-and-empty-dacls
 *       https://learn.microsoft.com/en-us/windows/win32/secauthz/generic-access-rights
 *       https://learn.microsoft.com/en-us/windows/win32/secauthz/access-mask
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
    oracle_begin("advapi32/AccessCheck");
    acl_init();
    /* Start from the same privileges everywhere: some logons (an SSH session
     * of an administrator, for one) have these enabled by default. */
    {
        static const wchar_t *const privs[] = { L"SeTakeOwnershipPrivilege", L"SeSecurityPrivilege" };
        TOKEN_PRIVILEGES tp;
        unsigned int i;
        for (i = 0; i < 2; i++) {
            tp.PrivilegeCount = 1;
            LookupPrivilegeValueW(NULL, privs[i], &tp.Privileges[0].Luid);
            tp.Privileges[0].Attributes = 0;
            AdjustTokenPrivileges(g_prim, FALSE, &tp, 0, NULL, NULL);
        }
        CloseHandle(g_imp);
        DuplicateToken(g_prim, SecurityImpersonation, &g_imp);
    }

    /* NULL DACL: no D: part at all, and D:NO_ACCESS_CONTROL. */
    check("no_dacl_read", L"O:SYG:SY", 0x1);
    check("no_dacl_all", L"O:SYG:SY", 0xF);
    check("no_dacl_high_bits", L"O:SYG:SY", 0x00FF0000 & ~ACCESS_SYSTEM_SECURITY);
    check("no_dacl_max", L"O:SYG:SY", MAXIMUM_ALLOWED);
    check("null_dacl_read", L"O:SYG:SYD:NO_ACCESS_CONTROL", 0x1);
    check("null_dacl_max", L"O:SYG:SYD:NO_ACCESS_CONTROL", MAXIMUM_ALLOWED);
    check("null_dacl_max_plus", L"O:SYG:SYD:NO_ACCESS_CONTROL", MAXIMUM_ALLOWED | 0x10);
    check("null_dacl_zero", L"O:SYG:SYD:NO_ACCESS_CONTROL", 0);

    /* Empty DACL: no access for anyone. */
    check("empty_dacl_read", L"O:SYG:SYD:", 0x1);
    check("empty_dacl_max", L"O:SYG:SYD:", MAXIMUM_ALLOWED);
    check("empty_dacl_zero", L"O:SYG:SYD:", 0);
    check("protected_empty_dacl", L"O:SYG:SYD:P", 0x1);

    /* Which trustees in the DACL match the test's token. */
    check("allow_everyone", L"O:SYG:SYD:(A;;0x3;;;WD)", 0x1);
    check("allow_everyone_both", L"O:SYG:SYD:(A;;0x3;;;WD)", 0x3);
    check("allow_everyone_more", L"O:SYG:SYD:(A;;0x3;;;WD)", 0x7);
    check("allow_user", L"O:SYG:SYD:(A;;0x3;;;$U)", 0x3);
    check("allow_admins", L"O:SYG:SYD:(A;;0x3;;;BA)", 0x3);
    check("allow_users", L"O:SYG:SYD:(A;;0x3;;;BU)", 0x3);
    check("allow_authenticated", L"O:SYG:SYD:(A;;0x3;;;AU)", 0x3);
    check("allow_system", L"O:SYG:SYD:(A;;0x3;;;SY)", 0x1);
    check("allow_anonymous", L"O:SYG:SYD:(A;;0x3;;;AN)", 0x1);
    check("allow_guests", L"O:SYG:SYD:(A;;0x3;;;BG)", 0x1);
    check("allow_restricted_code", L"O:SYG:SYD:(A;;0x3;;;RC)", 0x1);
    check("allow_creator_owner", L"O:SYG:SYD:(A;;0x3;;;CO)", 0x1);
    check("allow_self", L"O:SYG:SYD:(A;;0x3;;;PS)", 0x1);

    /* Order: the first matching ACE that decides a bit wins. */
    check("deny_then_allow", L"O:SYG:SYD:(D;;0x1;;;WD)(A;;0x3;;;WD)", 0x1);
    check("deny_then_allow_other", L"O:SYG:SYD:(D;;0x1;;;WD)(A;;0x3;;;WD)", 0x2);
    check("deny_then_allow_both", L"O:SYG:SYD:(D;;0x1;;;WD)(A;;0x3;;;WD)", 0x3);
    check("allow_then_deny", L"O:SYG:SYD:(A;;0x3;;;WD)(D;;0x1;;;WD)", 0x1);
    check("allow_then_deny_both", L"O:SYG:SYD:(A;;0x3;;;WD)(D;;0x1;;;WD)", 0x3);
    check("allow_partial_then_deny", L"O:SYG:SYD:(A;;0x1;;;WD)(D;;0x3;;;WD)(A;;0x2;;;WD)", 0x3);
    check("accumulate_two_aces", L"O:SYG:SYD:(A;;0x1;;;WD)(A;;0x2;;;BA)", 0x3);
    check("accumulate_three_aces", L"O:SYG:SYD:(A;;0x1;;;WD)(A;;0x2;;;BA)(A;;0x4;;;$U)", 0x7);
    check("accumulate_missing", L"O:SYG:SYD:(A;;0x1;;;WD)(A;;0x2;;;SY)", 0x3);
    check("deny_other_sid", L"O:SYG:SYD:(D;;0x1;;;SY)(A;;0x1;;;WD)", 0x1);
    check("deny_user_allow_group", L"O:SYG:SYD:(D;;0x1;;;$U)(A;;0x1;;;BA)", 0x1);
    check("deny_group_allow_user", L"O:SYG:SYD:(D;;0x2;;;BA)(A;;0x3;;;$U)", 0x1);
    check("deny_group_allow_user_w", L"O:SYG:SYD:(D;;0x2;;;BA)(A;;0x3;;;$U)", 0x2);
    check("deny_zero_mask", L"O:SYG:SYD:(D;;0x0;;;WD)(A;;0x1;;;WD)", 0x1);

    /* Inherit-only ACEs don't apply to the object; inherited ones do, in order. */
    check("inherit_only_allow", L"O:SYG:SYD:(A;IO;0x1;;;WD)", 0x1);
    check("inherit_only_deny", L"O:SYG:SYD:(D;OICIIO;0x1;;;WD)(A;;0x1;;;WD)", 0x1);
    check("oici_allow", L"O:SYG:SYD:(A;OICI;0x1;;;WD)", 0x1);
    check("inherited_allow", L"O:SYG:SYD:(A;ID;0x1;;;WD)", 0x1);
    check("inherited_deny_first", L"O:SYG:SYD:(D;ID;0x1;;;WD)(A;;0x1;;;WD)", 0x1);
    check("explicit_deny_after_inherited_allow", L"O:SYG:SYD:(A;ID;0x1;;;WD)(D;;0x1;;;WD)", 0x1);
    check("audit_flags_on_allow", L"O:SYG:SYD:(A;SA;0x1;;;WD)", 0x1);

    /* Generic rights in ACE masks are mapped with the caller's mapping. */
    check("ace_generic_read", L"O:SYG:SYD:(A;;GR;;;WD)", 0x1);
    check("ace_generic_read_w", L"O:SYG:SYD:(A;;GR;;;WD)", 0x2);
    check("ace_generic_write", L"O:SYG:SYD:(A;;GW;;;WD)", 0x2);
    check("ace_generic_exec", L"O:SYG:SYD:(A;;GX;;;WD)", 0x4);
    check("ace_generic_all", L"O:SYG:SYD:(A;;GA;;;WD)", 0xF);
    check("ace_generic_all_std", L"O:SYG:SYD:(A;;GA;;;WD)", READ_CONTROL);
    check("ace_deny_generic_write", L"O:SYG:SYD:(D;;GW;;;WD)(A;;0xF;;;WD)", 0x2);
    check("ace_deny_generic_write_r", L"O:SYG:SYD:(D;;GW;;;WD)(A;;0xF;;;WD)", 0x1);
    check_tok("file_ace_fr", L"O:SYG:SYD:(A;;FR;;;WD)", g_imp, FILE_GENERIC_READ, &g_fmap);
    check_tok("file_ace_fr_write", L"O:SYG:SYD:(A;;FR;;;WD)", g_imp, FILE_WRITE_DATA, &g_fmap);
    check_tok("file_ace_gr", L"O:SYG:SYD:(A;;GR;;;WD)", g_imp, FILE_READ_DATA | SYNCHRONIZE, &g_fmap);
    check_tok("file_ace_fa", L"O:SYG:SYD:(A;;FA;;;WD)", g_imp, FILE_ALL_ACCESS, &g_fmap);
    check_tok("file_ace_ga_max", L"O:SYG:SYD:(A;;GA;;;WD)", g_imp, MAXIMUM_ALLOWED, &g_fmap);
    check_tok("file_ace_gr_max", L"O:SYG:SYD:(A;;GR;;;WD)", g_imp, MAXIMUM_ALLOWED, &g_fmap);

    /* MAXIMUM_ALLOWED. */
    check("max_allow", L"O:SYG:SYD:(A;;0x3;;;WD)", MAXIMUM_ALLOWED);
    check("max_allow_deny", L"O:SYG:SYD:(A;;0x3;;;WD)(D;;0x1;;;WD)", MAXIMUM_ALLOWED);
    check("max_deny_allow", L"O:SYG:SYD:(D;;0x1;;;WD)(A;;0x3;;;WD)", MAXIMUM_ALLOWED);
    check("max_deny_all", L"O:SYG:SYD:(D;;0xF;;;WD)(A;;0x3;;;WD)", MAXIMUM_ALLOWED);
    check("max_no_match", L"O:SYG:SYD:(A;;0x3;;;SY)", MAXIMUM_ALLOWED);
    check("max_plus_granted", L"O:SYG:SYD:(A;;0x3;;;WD)", MAXIMUM_ALLOWED | 0x1);
    check("max_plus_missing", L"O:SYG:SYD:(A;;0x3;;;WD)", MAXIMUM_ALLOWED | 0x4);
    check("max_std_rights", L"O:SYG:SYD:(A;;0x001F0003;;;WD)", MAXIMUM_ALLOWED);
    check("max_accumulate", L"O:SYG:SYD:(A;;0x1;;;WD)(D;;0x2;;;BA)(A;;0x6;;;$U)", MAXIMUM_ALLOWED);

    /* Odd desired masks. */
    check("desired_zero_allowed", L"O:SYG:SYD:(A;;0x3;;;WD)", 0);
    check("desired_std_read_control", L"O:SYG:SYD:(A;;RC;;;WD)", READ_CONTROL);
    check("desired_synchronize", L"O:SYG:SYD:(A;;0x3;;;WD)", SYNCHRONIZE);
    check("desired_high_bit", L"O:SYG:SYD:(A;;0x3;;;WD)", 0x01000000 >> 1);
    check("allow_all_bits", L"O:SYG:SYD:(A;;0x0FFFFFFF;;;WD)", 0x00FFFFFF & ~ACCESS_SYSTEM_SECURITY);

    return oracle_end();
}
