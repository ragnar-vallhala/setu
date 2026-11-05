/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: privileges in AccessCheck and AdjustTokenPrivileges on an
 * impersonation token. ACCESS_SYSTEM_SECURITY needs SeSecurityPrivilege
 * enabled (ERROR_PRIVILEGE_NOT_HELD otherwise) and the privilege comes back
 * in the PRIVILEGE_SET marked as used for access; SeTakeOwnershipPrivilege
 * and WRITE_OWNER; the privilege set length needed when a privilege is
 * reported; AdjustTokenPrivileges' ERROR_NOT_ALL_ASSIGNED, previous state,
 * DisableAllPrivileges and PrivilegeCheck.
 *
 * Privilege LUIDs are the documented constants (SE_SECURITY_PRIVILEGE = 8,
 * SE_TAKE_OWNERSHIP_PRIVILEGE = 9, ...), looked up by name.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-accesscheck
 *       https://learn.microsoft.com/en-us/windows/win32/secauthz/access-system-security
 *       https://learn.microsoft.com/en-us/windows/win32/secauthz/privilege-constants
 *       https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-adjusttokenprivileges
 *       https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-privilegecheck
 *       https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-privilege_set
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
static GENERIC_MAPPING g_map = { 0x0001, 0x0002, 0x0004, 0x000F };

static void check_priv(const char *name, const wchar_t *sddl, HANDLE tok, DWORD desired, DWORD pslen_in)
{
    PSECURITY_DESCRIPTOR sd = sd_from(sddl);
    BYTE psbuf[256];
    PRIVILEGE_SET *ps = (PRIVILEGE_SET *)psbuf;
    DWORD pslen = pslen_in, granted = 0xDEADBEEF;
    BOOL status = 0x5A5A, r;
    memset(psbuf, 0, sizeof psbuf);
    ORACLE_CALL(r, AccessCheck(sd, tok, desired, &g_map, ps, &pslen, &granted, &status));
    oracle_case(name);
    o_bool("ret", r);
    o_lasterr();
    o_u32("privset_len", pslen);
    if (r) {
        o_hex32("granted", granted);
        o_bool("status", status);
        o_u32("priv_count", ps->PrivilegeCount);
        o_u32("priv_control", ps->Control);
        for (DWORD i = 0; i < ps->PrivilegeCount && i < 4; i++) {
            char k[32];
            snprintf(k, sizeof k, "priv%lu_luid", (unsigned long)i);
            o_u32(k, ps->Privilege[i].Luid.LowPart);
            snprintf(k, sizeof k, "priv%lu_attrs", (unsigned long)i);
            o_hex32(k, ps->Privilege[i].Attributes);
        }
    }
    oracle_case_end();
    LocalFree(sd);
}

static BOOL set_priv(HANDLE tok, const wchar_t *name, DWORD attrs)
{
    TOKEN_PRIVILEGES tp;
    tp.PrivilegeCount = 1;
    LookupPrivilegeValueW(NULL, name, &tp.Privileges[0].Luid);
    tp.Privileges[0].Attributes = attrs;
    return AdjustTokenPrivileges(tok, FALSE, &tp, 0, NULL, NULL) && GetLastError() == ERROR_SUCCESS;
}

static void adjust(const char *name, HANDLE tok, const wchar_t *priv, DWORD attrs)
{
    BYTE prevbuf[64];
    TOKEN_PRIVILEGES tp, *prev = (TOKEN_PRIVILEGES *)prevbuf;
    DWORD retlen = 0xDEADBEEF;
    BOOL r;
    tp.PrivilegeCount = 1;
    LookupPrivilegeValueW(NULL, priv, &tp.Privileges[0].Luid);
    tp.Privileges[0].Attributes = attrs;
    memset(prevbuf, 0, sizeof prevbuf);
    ORACLE_CALL(r, AdjustTokenPrivileges(tok, FALSE, &tp, sizeof prevbuf, prev, &retlen));
    oracle_case(name);
    o_bool("ret", r);
    o_lasterr();
    o_u32("luid", tp.Privileges[0].Luid.LowPart);
    o_u32("retlen", retlen);
    o_u32("prev_count", prev->PrivilegeCount);
    /* which privileges are enabled by default depends on the logon */
    if (prev->PrivilegeCount)
        o_hex32("prev_attrs", prev->Privileges[0].Attributes & ~SE_PRIVILEGE_ENABLED_BY_DEFAULT);
    oracle_case_end();
}

static void privcheck(const char *name, HANDLE tok, const wchar_t *priv)
{
    PRIVILEGE_SET ps;
    BOOL res = 0x5A5A, r;
    ps.PrivilegeCount = 1;
    ps.Control = PRIVILEGE_SET_ALL_NECESSARY;
    LookupPrivilegeValueW(NULL, priv, &ps.Privilege[0].Luid);
    ps.Privilege[0].Attributes = 0;
    ORACLE_CALL(r, PrivilegeCheck(tok, &ps, &res));
    oracle_case(name);
    o_bool("ret", r);
    o_lasterr();
    if (r) {
        o_bool("result", res);
        o_hex32("attrs", ps.Privilege[0].Attributes);
    }
    oracle_case_end();
}

int main(void)
{
    oracle_begin("advapi32/AccessCheck_privileges");
    acl_init();
    HANDLE t = NULL;
    DuplicateTokenEx(g_prim, TOKEN_ALL_ACCESS, NULL, SecurityImpersonation, TokenImpersonation, &t);
    /* Start from the same privileges everywhere: some logons (an SSH session
     * of an administrator, for one) have these enabled by default. */
    set_priv(t, L"SeBackupPrivilege", 0);
    set_priv(t, L"SeRestorePrivilege", 0);
    set_priv(t, L"SeTakeOwnershipPrivilege", 0);
    set_priv(t, L"SeSecurityPrivilege", 0);
    const wchar_t *sd = L"O:SYG:SYD:(A;;0x3;;;WD)";
    const wchar_t *sd_sacl = L"O:SYG:SYD:(A;;0x3;;;WD)S:(AU;SAFA;0x3;;;WD)";

    /* SeSecurityPrivilege present but disabled. */
    set_priv(t, L"SeSecurityPrivilege", 0);
    privcheck("privcheck_security_disabled", t, L"SeSecurityPrivilege");
    check_priv("ass_disabled", sd, t, ACCESS_SYSTEM_SECURITY, 256);
    check_priv("ass_disabled_plus_read", sd, t, ACCESS_SYSTEM_SECURITY | 0x1, 256);
    check_priv("ass_disabled_max", sd, t, MAXIMUM_ALLOWED, 256);
    check_priv("ass_disabled_max_plus", sd, t, MAXIMUM_ALLOWED | ACCESS_SYSTEM_SECURITY, 256);
    check_priv("ass_disabled_null_dacl", L"O:SYG:SYD:NO_ACCESS_CONTROL", t, ACCESS_SYSTEM_SECURITY, 256);
    check_priv("ass_disabled_sacl", sd_sacl, t, ACCESS_SYSTEM_SECURITY, 256);
    check_priv("ass_disabled_ace_grants", L"O:SYG:SYD:(A;;0x01000003;;;WD)", t, ACCESS_SYSTEM_SECURITY, 256);

    /* Enabled. */
    adjust("adjust_enable_security", t, L"SeSecurityPrivilege", SE_PRIVILEGE_ENABLED);
    adjust("adjust_enable_security_again", t, L"SeSecurityPrivilege", SE_PRIVILEGE_ENABLED);
    privcheck("privcheck_security_enabled", t, L"SeSecurityPrivilege");
    check_priv("ass_enabled", sd, t, ACCESS_SYSTEM_SECURITY, 256);
    check_priv("ass_enabled_plus_read", sd, t, ACCESS_SYSTEM_SECURITY | 0x1, 256);
    check_priv("ass_enabled_plus_denied", sd, t, ACCESS_SYSTEM_SECURITY | 0x4, 256);
    check_priv("ass_enabled_empty_dacl", L"O:SYG:SYD:", t, ACCESS_SYSTEM_SECURITY, 256);
    check_priv("ass_enabled_deny_all", L"O:SYG:SYD:(D;;0x0F1F01FF;;;WD)", t, ACCESS_SYSTEM_SECURITY, 256);
    check_priv("ass_enabled_null_dacl", L"O:SYG:SYD:NO_ACCESS_CONTROL", t, ACCESS_SYSTEM_SECURITY, 256);
    check_priv("ass_enabled_max", sd, t, MAXIMUM_ALLOWED, 256);
    check_priv("ass_enabled_max_plus", sd, t, MAXIMUM_ALLOWED | ACCESS_SYSTEM_SECURITY, 256);
    check_priv("ass_enabled_pslen_header", sd, t, ACCESS_SYSTEM_SECURITY, sizeof(PRIVILEGE_SET));
    check_priv("ass_enabled_pslen_small", sd, t, ACCESS_SYSTEM_SECURITY, sizeof(PRIVILEGE_SET) - 4);
    check_priv("ass_enabled_pslen_zero", sd, t, ACCESS_SYSTEM_SECURITY, 0);
    check_priv("no_ass_enabled", sd, t, 0x1, 256);
    adjust("adjust_disable_security", t, L"SeSecurityPrivilege", 0);
    check_priv("ass_after_disable", sd, t, ACCESS_SYSTEM_SECURITY, 256);

    /* WRITE_OWNER and SeTakeOwnershipPrivilege. */
    set_priv(t, L"SeTakeOwnershipPrivilege", 0);
    check_priv("wo_disabled", sd, t, WRITE_OWNER, 256);
    adjust("adjust_enable_take_ownership", t, L"SeTakeOwnershipPrivilege", SE_PRIVILEGE_ENABLED);
    check_priv("wo_enabled", sd, t, WRITE_OWNER, 256);
    check_priv("wo_enabled_empty_dacl", L"O:SYG:SYD:", t, WRITE_OWNER, 256);
    check_priv("wo_enabled_deny", L"O:SYG:SYD:(D;;WO;;;WD)", t, WRITE_OWNER, 256);
    check_priv("wo_enabled_plus_read", sd, t, WRITE_OWNER | 0x1, 256);
    check_priv("wo_enabled_max", L"O:SYG:SYD:", t, MAXIMUM_ALLOWED, 256);
    check_priv("wo_enabled_pslen_header", L"O:SYG:SYD:", t, WRITE_OWNER, sizeof(PRIVILEGE_SET));
    adjust("adjust_both_enabled", t, L"SeSecurityPrivilege", SE_PRIVILEGE_ENABLED);
    check_priv("both_privs", L"O:SYG:SYD:", t, WRITE_OWNER | ACCESS_SYSTEM_SECURITY, 256);
    check_priv("both_privs_pslen_header", L"O:SYG:SYD:", t, WRITE_OWNER | ACCESS_SYSTEM_SECURITY,
               sizeof(PRIVILEGE_SET));
    set_priv(t, L"SeTakeOwnershipPrivilege", 0);
    set_priv(t, L"SeSecurityPrivilege", 0);

    /* Backup/restore privileges don't change AccessCheck. */
    adjust("adjust_enable_backup", t, L"SeBackupPrivilege", SE_PRIVILEGE_ENABLED);
    check_priv("backup_enabled_read", L"O:SYG:SYD:", t, 0x1, 256);
    adjust("adjust_enable_restore", t, L"SeRestorePrivilege", SE_PRIVILEGE_ENABLED);
    check_priv("restore_enabled_write", L"O:SYG:SYD:", t, 0x2, 256);
    set_priv(t, L"SeBackupPrivilege", 0);
    set_priv(t, L"SeRestorePrivilege", 0);

    /* AdjustTokenPrivileges odds. */
    adjust("adjust_not_held", t, L"SeCreateTokenPrivilege", SE_PRIVILEGE_ENABLED);
    adjust("adjust_not_held_disable", t, L"SeCreateTokenPrivilege", 0);
    {
        BOOL r;
        ORACLE_CALL(r, AdjustTokenPrivileges(t, TRUE, NULL, 0, NULL, NULL));
        oracle_case("disable_all");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
    }
    privcheck("privcheck_after_disable_all", t, L"SeChangeNotifyPrivilege");
    check_priv("ass_after_disable_all", sd, t, ACCESS_SYSTEM_SECURITY, 256);
    {
        HANDLE noadj = NULL;
        BOOL r;
        TOKEN_PRIVILEGES tp;
        DuplicateHandle(GetCurrentProcess(), t, GetCurrentProcess(), &noadj, TOKEN_QUERY, FALSE, 0);
        tp.PrivilegeCount = 1;
        LookupPrivilegeValueW(NULL, L"SeSecurityPrivilege", &tp.Privileges[0].Luid);
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        ORACLE_CALL(r, AdjustTokenPrivileges(noadj, FALSE, &tp, 0, NULL, NULL));
        oracle_case("adjust_without_access");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        CloseHandle(noadj);
    }
    return oracle_end();
}
