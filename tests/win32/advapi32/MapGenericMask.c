/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: MapGenericMask and GetEffectiveRightsFromAclW. MapGenericMask
 * replaces each generic bit with the mapping's rights and keeps every other
 * bit (including MAXIMUM_ALLOWED and ACCESS_SYSTEM_SECURITY).
 * GetEffectiveRightsFromAclW evaluates an ACL for a trustee given by SID:
 * the user, a group it belongs to, Everyone; allow and deny ACEs, inherit-only
 * ACEs, and an empty and a NULL ACL. The user's SID is substituted as $U.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-mapgenericmask
 *       https://learn.microsoft.com/en-us/windows/win32/api/aclapi/nf-aclapi-geteffectiverightsfromaclw
 *       https://learn.microsoft.com/en-us/windows/win32/api/accctrl/ns-accctrl-trustee_w
 *       https://learn.microsoft.com/en-us/windows/win32/secauthz/generic-access-rights
 */
#include "oracle.h"
#include <sddl.h>
#include <aclapi.h>

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
static void map(const char *name, DWORD in, DWORD r, DWORD w, DWORD x, DWORD a)
{
    GENERIC_MAPPING m = { r, w, x, a };
    DWORD v = in;
    MapGenericMask(&v, &m);
    oracle_case(name);
    o_hex32("in", in);
    o_hex32("out", v);
    oracle_case_end();
}

static void eff(const char *name, const wchar_t *dacl_sddl, const wchar_t *trustee)
{
    PSECURITY_DESCRIPTOR sd = sd_from(dacl_sddl);
    BOOL present = FALSE, def = FALSE;
    PACL acl = NULL;
    PSID sid = NULL;
    wchar_t buf[256];
    TRUSTEE_W t;
    ACCESS_MASK m = 0xDEADBEEF;
    DWORD r;
    GetSecurityDescriptorDacl(sd, &present, &acl, &def);
    expand(trustee, buf);
    ConvertStringSidToSidW(buf, &sid);
    BuildTrusteeWithSidW(&t, sid);
    r = GetEffectiveRightsFromAclW(acl, &t, &m);
    oracle_case(name);
    o_u32("ret", r);
    if (r == ERROR_SUCCESS) o_hex32("rights", m);
    oracle_case_end();
    LocalFree(sid);
    LocalFree(sd);
}

int main(void)
{
    oracle_begin("advapi32/MapGenericMask");
    acl_init();

    map("read", GENERIC_READ, 0x1, 0x2, 0x4, 0xF);
    map("write", GENERIC_WRITE, 0x1, 0x2, 0x4, 0xF);
    map("execute", GENERIC_EXECUTE, 0x1, 0x2, 0x4, 0xF);
    map("all", GENERIC_ALL, 0x1, 0x2, 0x4, 0xF);
    map("read_write", GENERIC_READ | GENERIC_WRITE, 0x1, 0x2, 0x4, 0xF);
    map("all_four", GENERIC_READ | GENERIC_WRITE | GENERIC_EXECUTE | GENERIC_ALL, 0x1, 0x2, 0x4, 0xF);
    map("specific_kept", 0x00000030, 0x1, 0x2, 0x4, 0xF);
    map("generic_plus_specific", GENERIC_READ | 0x30, 0x1, 0x2, 0x4, 0xF);
    map("standard_kept", GENERIC_WRITE | READ_CONTROL | SYNCHRONIZE, 0x1, 0x2, 0x4, 0xF);
    map("maximum_allowed_kept", MAXIMUM_ALLOWED | GENERIC_READ, 0x1, 0x2, 0x4, 0xF);
    map("system_security_kept", ACCESS_SYSTEM_SECURITY | GENERIC_EXECUTE, 0x1, 0x2, 0x4, 0xF);
    map("reserved_bits_kept", 0x06000000 | GENERIC_READ, 0x1, 0x2, 0x4, 0xF);
    map("mapping_has_generic", GENERIC_READ, GENERIC_WRITE | 0x1, 0x2, 0x4, 0xF);
    map("mapping_zero", GENERIC_ALL | GENERIC_READ, 0, 0, 0, 0);
    map("nothing", 0, 0x1, 0x2, 0x4, 0xF);
    map("file_read", GENERIC_READ, FILE_GENERIC_READ, FILE_GENERIC_WRITE, FILE_GENERIC_EXECUTE, FILE_ALL_ACCESS);
    map("file_all", GENERIC_ALL, FILE_GENERIC_READ, FILE_GENERIC_WRITE, FILE_GENERIC_EXECUTE, FILE_ALL_ACCESS);

    eff("eff_allow_user", L"D:(A;;0x3;;;$U)", L"$U");
    eff("eff_allow_everyone_for_user", L"D:(A;;0x3;;;WD)", L"$U");
    eff("eff_allow_everyone", L"D:(A;;0x3;;;WD)", L"WD");
    eff("eff_deny_then_allow", L"D:(D;;0x1;;;$U)(A;;0x7;;;$U)", L"$U");
    eff("eff_allow_then_deny", L"D:(A;;0x7;;;$U)(D;;0x1;;;$U)", L"$U");
    eff("eff_two_allows", L"D:(A;;0x1;;;$U)(A;;0x2;;;WD)", L"$U");
    eff("eff_inherit_only", L"D:(A;IO;0x1;;;$U)(A;;0x2;;;$U)", L"$U");
    eff("eff_admins_for_admins", L"D:(A;;0x3;;;BA)", L"BA");
    eff("eff_system_for_user", L"D:(A;;0x3;;;SY)", L"$U");
    eff("eff_empty", L"D:", L"$U");
    eff("eff_null_dacl", L"D:NO_ACCESS_CONTROL", L"$U");
    eff("eff_generic", L"D:(A;;GA;;;WD)", L"WD");
    return oracle_end();
}
