/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: AccessCheckAndAuditAlarmW, which checks the calling thread's
 * impersonation token (the thread must impersonate, otherwise
 * ERROR_NO_IMPERSONATION_TOKEN). Granted/denied results, generic rights in
 * the desired mask (mapped by this API, unlike AccessCheck), MAXIMUM_ALLOWED,
 * the GenerateOnClose flag, ObjectCloseAuditAlarmW, and
 * AccessCheckByTypeAndAuditAlarmW's simplest form. ImpersonateSelf and
 * RevertToSelf set up the thread token.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-accesscheckandauditalarmw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-accesscheckbytypeandauditalarmw
 *       https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-objectcloseauditalarmw
 *       https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-impersonateself
 *       https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-reverttoself
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

static void aa(const char *name, const wchar_t *sddl, DWORD desired, BOOL creation)
{
    PSECURITY_DESCRIPTOR sd = sd_from(sddl);
    DWORD granted = 0xDEADBEEF;
    BOOL status = 0x5A5A, onclose = 0x5A5A, r;
    ORACLE_CALL(r, AccessCheckAndAuditAlarmW(L"SetuOracle", (LPVOID)0x1234, (LPWSTR)L"TestObject",
                                             (LPWSTR)L"Object1", sd, desired, &g_map, creation, &granted,
                                             &status, &onclose));
    oracle_case(name);
    o_bool("ret", r);
    o_lasterr();
    if (r) {
        o_hex32("granted", granted);
        o_bool("status", status);
        o_bool("on_close", onclose);
    } else {
        o_bool("granted_untouched", granted == 0xDEADBEEF);
    }
    oracle_case_end();
    LocalFree(sd);
}

int main(void)
{
    oracle_begin("advapi32/AccessCheckAndAuditAlarm");
    acl_init();
    const wchar_t *sd = L"O:SYG:SYD:(A;;0x3;;;WD)";

    /* Not impersonating. */
    aa("not_impersonating", sd, 0x1, FALSE);
    aa("not_impersonating_denied", sd, 0x4, FALSE);

    BOOL r;
    ORACLE_CALL(r, ImpersonateSelf(SecurityImpersonation));
    oracle_case("impersonate_self");
    o_bool("ret", r);
    o_lasterr();
    oracle_case_end();

    aa("granted", sd, 0x1, FALSE);
    aa("granted_both", sd, 0x3, FALSE);
    aa("denied", sd, 0x4, FALSE);
    aa("generic_read", sd, GENERIC_READ, FALSE);
    aa("generic_all", sd, GENERIC_ALL, FALSE);
    aa("maximum_allowed", sd, MAXIMUM_ALLOWED, FALSE);
    aa("creation_flag", sd, 0x1, TRUE);
    aa("null_dacl", L"O:SYG:SYD:NO_ACCESS_CONTROL", 0x7, FALSE);
    aa("empty_dacl", L"O:SYG:SYD:", 0x1, FALSE);
    aa("owner_rc", L"O:$UG:SYD:", READ_CONTROL, FALSE);
    aa("no_owner", L"G:SYD:(A;;0x3;;;WD)", 0x1, FALSE);
    aa("system_security", sd, ACCESS_SYSTEM_SECURITY, FALSE);
    aa("with_audit_sacl", L"O:SYG:SYD:(A;;0x3;;;WD)S:(AU;SAFA;0x3;;;WD)", 0x1, FALSE);

    ORACLE_CALL(r, ObjectCloseAuditAlarmW(L"SetuOracle", (LPVOID)0x1234, FALSE));
    oracle_case("close_alarm");
    o_bool("ret", r);
    o_lasterr();
    oracle_case_end();

    {
        PSECURITY_DESCRIPTOR s = sd_from(sd);
        DWORD granted = 0xDEADBEEF;
        BOOL status = 0x5A5A, onclose = 0x5A5A;
        ORACLE_CALL(r, AccessCheckByTypeAndAuditAlarmW(L"SetuOracle", (LPVOID)0x1234, L"TestObject", L"Object1", s,
                                                       NULL, 0x1, AuditEventObjectAccess, 0, NULL, 0, &g_map,
                                                       FALSE, &granted, &status, &onclose));
        oracle_case("by_type_alarm");
        o_bool("ret", r);
        o_lasterr();
        if (r) {
            o_hex32("granted", granted);
            o_bool("status", status);
        }
        oracle_case_end();
        LocalFree(s);
    }

    /* Identification-level thread token. */
    RevertToSelf();
    ImpersonateSelf(SecurityIdentification);
    aa("identification_level", sd, 0x1, FALSE);
    RevertToSelf();
    aa("after_revert", sd, 0x1, FALSE);
    return oracle_end();
}
