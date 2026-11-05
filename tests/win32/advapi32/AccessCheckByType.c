/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: AccessCheckByType and AccessCheckByTypeResultList with
 * object-type ACEs (SDDL "OA"/"OD") and an object type list (object, property
 * set, properties). Which object ACEs apply with and without a list, ACEs for
 * GUIDs not in the list, rights granted on a property set flowing down to its
 * properties, denies on one property, PRINCIPAL_SELF substitution, plain ACEs
 * applying to every element, MAXIMUM_ALLOWED per element, and malformed lists.
 * GUIDs are arbitrary test values; rights use the documented DS bits
 * (RP 0x10, WP 0x20, CC 0x1, DC 0x2).
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-accesscheckbytype
 *       https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-accesscheckbytyperesultlist
 *       https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-object_type_list
 *       https://learn.microsoft.com/en-us/windows/win32/secauthz/object-specific-aces
 *       https://learn.microsoft.com/en-us/windows/win32/secauthz/ace-strings
 *       https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-access_allowed_object_ace
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
static GENERIC_MAPPING g_map = { 0x0014, 0x0028, 0x0004, 0x00FF };

#define G0 L"10000000-0000-0000-0000-000000000000"
#define G1 L"11000000-0000-0000-0000-000000000000"
#define G2 L"11100000-0000-0000-0000-000000000000"
#define G3 L"11200000-0000-0000-0000-000000000000"
#define GX L"99999999-0000-0000-0000-000000000000"

static GUID g0 = { 0x10000000, 0, 0, { 0 } };
static GUID g1 = { 0x11000000, 0, 0, { 0 } };
static GUID g2 = { 0x11100000, 0, 0, { 0 } };
static GUID g3 = { 0x11200000, 0, 0, { 0 } };
static GUID gx = { 0x99999999, 0, 0, { 0 } };

static OBJECT_TYPE_LIST full[4];
static OBJECT_TYPE_LIST only_root[1];

static void by_type(const char *name, const wchar_t *sddl, PSID self, DWORD desired, OBJECT_TYPE_LIST *list,
                    DWORD n)
{
    PSECURITY_DESCRIPTOR sd = sd_from(sddl);
    BYTE psbuf[256];
    DWORD pslen = sizeof psbuf, granted = 0xDEADBEEF;
    BOOL status = 0x5A5A, r;
    oracle_case(name);
    if (!sd) {
        o_str("sddl", "parse failed");
        oracle_case_end();
        return;
    }
    ORACLE_CALL(r, AccessCheckByType(sd, self, g_imp, desired, list, n, &g_map, (PRIVILEGE_SET *)psbuf, &pslen,
                                     &granted, &status));
    o_bool("ret", r);
    o_lasterr();
    if (r) {
        o_hex32("granted", granted);
        o_bool("status", status);
    }
    oracle_case_end();
    LocalFree(sd);
}

static void by_type_list(const char *name, const wchar_t *sddl, PSID self, DWORD desired, OBJECT_TYPE_LIST *list,
                         DWORD n)
{
    PSECURITY_DESCRIPTOR sd = sd_from(sddl);
    BYTE psbuf[256];
    DWORD pslen = sizeof psbuf, granted[8], status[8];
    BOOL r;
    for (int i = 0; i < 8; i++) granted[i] = status[i] = 0xDEADBEEF;
    oracle_case(name);
    if (!sd) {
        o_str("sddl", "parse failed");
        oracle_case_end();
        return;
    }
    ORACLE_CALL(r, AccessCheckByTypeResultList(sd, self, g_imp, desired, list, n, &g_map, (PRIVILEGE_SET *)psbuf,
                                               &pslen, granted, status));
    o_bool("ret", r);
    o_lasterr();
    if (r) {
        for (DWORD i = 0; i < n && i < 8; i++) {
            char k[32];
            snprintf(k, sizeof k, "granted%lu", (unsigned long)i);
            o_hex32(k, granted[i]);
            snprintf(k, sizeof k, "status%lu", (unsigned long)i);
            o_u32(k, status[i]);
        }
    }
    oracle_case_end();
    LocalFree(sd);
}

int main(void)
{
    oracle_begin("advapi32/AccessCheckByType");
    acl_init();
    full[0].Level = ACCESS_OBJECT_GUID;       full[0].Sbz = 0; full[0].ObjectType = &g0;
    full[1].Level = ACCESS_PROPERTY_SET_GUID; full[1].Sbz = 0; full[1].ObjectType = &g1;
    full[2].Level = ACCESS_PROPERTY_GUID;     full[2].Sbz = 0; full[2].ObjectType = &g2;
    full[3].Level = ACCESS_PROPERTY_GUID;     full[3].Sbz = 0; full[3].ObjectType = &g3;
    only_root[0] = full[0];
    PSID self = NULL;
    ConvertStringSidToSidW(g_user, &self);

    /* Plain ACEs, with and without a type list. */
    by_type("plain_no_list", L"O:SYG:SYD:(A;;RPWP;;;WD)", NULL, 0x10, NULL, 0);
    by_type("plain_root_list", L"O:SYG:SYD:(A;;RPWP;;;WD)", NULL, 0x10, only_root, 1);
    by_type("plain_full_list", L"O:SYG:SYD:(A;;RPWP;;;WD)", NULL, 0x30, full, 4);
    by_type("plain_full_denied", L"O:SYG:SYD:(A;;RP;;;WD)", NULL, 0x30, full, 4);
    by_type("plain_max_full", L"O:SYG:SYD:(A;;RPWP;;;WD)", NULL, MAXIMUM_ALLOWED, full, 4);

    /* Object ACEs. */
    by_type("oa_root_no_list", L"O:SYG:SYD:(OA;;RP;" G0 L";;WD)", NULL, 0x10, NULL, 0);
    by_type("oa_root_root_list", L"O:SYG:SYD:(OA;;RP;" G0 L";;WD)", NULL, 0x10, only_root, 1);
    by_type("oa_propset_root_list", L"O:SYG:SYD:(OA;;RP;" G1 L";;WD)", NULL, 0x10, only_root, 1);
    by_type("oa_propset_full", L"O:SYG:SYD:(OA;;RP;" G1 L";;WD)", NULL, 0x10, full, 4);
    by_type("oa_prop_full", L"O:SYG:SYD:(OA;;RP;" G2 L";;WD)", NULL, 0x10, full, 4);
    by_type("oa_both_props_full", L"O:SYG:SYD:(OA;;RP;" G2 L";;WD)(OA;;RP;" G3 L";;WD)", NULL, 0x10, full, 4);
    by_type("oa_unknown_guid", L"O:SYG:SYD:(OA;;RP;" GX L";;WD)", NULL, 0x10, full, 4);
    by_type("oa_no_type", L"O:SYG:SYD:(OA;;RP;;;WD)", NULL, 0x10, full, 4);
    by_type("oa_inherited_type_only", L"O:SYG:SYD:(OA;;RP;;" G0 L";WD)", NULL, 0x10, full, 4);
    by_type("od_prop_then_allow", L"O:SYG:SYD:(OD;;RP;" G2 L";;WD)(A;;RP;;;WD)", NULL, 0x10, full, 4);
    by_type("od_unknown_then_allow", L"O:SYG:SYD:(OD;;RP;" GX L";;WD)(A;;RP;;;WD)", NULL, 0x10, full, 4);
    by_type("od_root_then_allow", L"O:SYG:SYD:(OD;;RP;" G0 L";;WD)(A;;RP;;;WD)", NULL, 0x10, full, 4);
    by_type("oa_propset_max", L"O:SYG:SYD:(OA;;RPWP;" G1 L";;WD)(A;;CC;;;WD)", NULL, MAXIMUM_ALLOWED, full, 4);

    /* PRINCIPAL_SELF. */
    by_type("self_given", L"O:SYG:SYD:(A;;RP;;;PS)", self, 0x10, NULL, 0);
    by_type("self_null", L"O:SYG:SYD:(A;;RP;;;PS)", NULL, 0x10, NULL, 0);
    by_type("self_object_ace", L"O:SYG:SYD:(OA;;RP;" G0 L";;PS)", self, 0x10, only_root, 1);
    by_type("self_other_sid", L"O:SYG:SYD:(A;;RP;;;PS)", (PSID)NULL, 0x10, only_root, 1);

    /* Result lists: one result per element. */
    by_type_list("list_plain", L"O:SYG:SYD:(A;;RP;;;WD)", NULL, 0x10, full, 4);
    by_type_list("list_oa_propset", L"O:SYG:SYD:(OA;;RP;" G1 L";;WD)", NULL, 0x10, full, 4);
    by_type_list("list_oa_prop", L"O:SYG:SYD:(OA;;RP;" G2 L";;WD)", NULL, 0x10, full, 4);
    by_type_list("list_oa_both_props", L"O:SYG:SYD:(OA;;RP;" G2 L";;WD)(OA;;RP;" G3 L";;WD)", NULL, 0x10, full, 4);
    by_type_list("list_od_prop", L"O:SYG:SYD:(OD;;RP;" G2 L";;WD)(A;;RP;;;WD)", NULL, 0x10, full, 4);
    by_type_list("list_od_propset", L"O:SYG:SYD:(OD;;RP;" G1 L";;WD)(A;;RP;;;WD)", NULL, 0x10, full, 4);
    by_type_list("list_allow_then_od", L"O:SYG:SYD:(A;;RP;;;WD)(OD;;RP;" G2 L";;WD)", NULL, 0x10, full, 4);
    by_type_list("list_max", L"O:SYG:SYD:(OA;;WP;" G2 L";;WD)(A;;RP;;;WD)", NULL, MAXIMUM_ALLOWED, full, 4);
    by_type_list("list_max_od", L"O:SYG:SYD:(OD;;RP;" G3 L";;WD)(A;;RPWP;;;WD)", NULL, MAXIMUM_ALLOWED, full, 4);
    by_type_list("list_nothing", L"O:SYG:SYD:", NULL, 0x10, full, 4);
    by_type_list("list_owner", L"O:$UG:SYD:", NULL, READ_CONTROL, full, 4);

    /* Malformed lists. */
    {
        OBJECT_TYPE_LIST bad[3];
        bad[0] = full[1];   /* starts at level 1 */
        by_type("list_starts_level1", L"O:SYG:SYD:(A;;RP;;;WD)", NULL, 0x10, bad, 1);
        bad[0] = full[0];
        bad[1] = full[2];   /* jumps from level 0 to 2 */
        by_type("list_level_jump", L"O:SYG:SYD:(A;;RP;;;WD)", NULL, 0x10, bad, 2);
        bad[1] = full[0];   /* two roots */
        by_type("list_two_roots", L"O:SYG:SYD:(A;;RP;;;WD)", NULL, 0x10, bad, 2);
        bad[1] = full[1];
        bad[2] = full[1];   /* duplicate GUID */
        by_type("list_duplicate_guid", L"O:SYG:SYD:(A;;RP;;;WD)", NULL, 0x10, bad, 3);
        bad[0].Level = 5;
        by_type("list_bad_level", L"O:SYG:SYD:(A;;RP;;;WD)", NULL, 0x10, bad, 1);
    }
    /* Ordinary error paths. */
    by_type("generic_desired", L"O:SYG:SYD:(A;;RP;;;WD)", NULL, GENERIC_READ, full, 4);
    {
        PSECURITY_DESCRIPTOR sd = sd_from(L"O:SYG:SYD:(A;;RP;;;WD)");
        BYTE psbuf[256];
        DWORD pslen = sizeof psbuf, granted = 0;
        BOOL status = 0, r;
        ORACLE_CALL(r, AccessCheckByType(sd, NULL, g_prim, 0x10, full, 4, &g_map, (PRIVILEGE_SET *)psbuf, &pslen,
                                         &granted, &status));
        oracle_case("primary_token");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        LocalFree(sd);
    }
    LocalFree(self);
    (void)gx;
    return oracle_end();
}
