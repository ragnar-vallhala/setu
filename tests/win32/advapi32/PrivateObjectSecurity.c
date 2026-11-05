/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: ACE inheritance through CreatePrivateObjectSecurity(Ex),
 * GetPrivateObjectSecurity, SetPrivateObjectSecurity(Ex) and
 * ConvertToAutoInheritPrivateObjectSecurity, with a file-style generic
 * mapping. Covers the defaults taken from the token (owner, primary group,
 * default DACL), OI/CI/NP/IO inheritance into objects and containers,
 * CREATOR OWNER / CREATOR GROUP substitution, generic rights mapped in
 * effective inherited ACEs, explicit creator DACLs vs inheritance with and
 * without SEF_DACL_AUTO_INHERIT, protected DACLs, the owner check
 * (SEF_AVOID_OWNER_CHECK), SACLs and the privilege check, object-type
 * inheritance by GUID, and buffer sizing.
 *
 * SDs are printed as SDDL with the user's SID shown as <user>, other SIDs of
 * the user's domain as <dom>-RID and logon SIDs as <logon>.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-createprivateobjectsecurity
 *       https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-createprivateobjectsecurityex
 *       https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-getprivateobjectsecurity
 *       https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-setprivateobjectsecurity
 *       https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-setprivateobjectsecurityex
 *       https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-converttoautoinheritprivateobjectsecurity
 *       https://learn.microsoft.com/en-us/windows/win32/secauthz/ace-inheritance-rules
 *       https://learn.microsoft.com/en-us/windows/win32/secauthz/automatic-propagation-of-inheritable-aces
 *       https://learn.microsoft.com/en-us/windows/win32/secauthz/security-descriptor-string-format
 */
#include "oracle.h"
#include <sddl.h>
#include <wctype.h>

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
/* SDDL output with machine-specific SIDs replaced: the user's SID by <user>,
 * other SIDs of the user's domain by <dom>-RID, logon SIDs by <logon>. */
static void tokenize(const wchar_t *in, wchar_t *out)
{
    wchar_t dom[192];
    size_t ul = wcslen(g_user), dl;
    wcscpy(dom, g_user);
    *wcsrchr(dom, L'-') = 0;
    dl = wcslen(dom);
    while (*in) {
        if (!wcsncmp(in, g_user, ul) && !iswdigit(in[ul])) {
            wcscpy(out, L"<user>"); out += 6; in += ul;
        } else if (!wcsncmp(in, dom, dl) && in[dl] == L'-') {
            wcscpy(out, L"<dom>"); out += 5; in += dl;
        } else if (!wcsncmp(in, L"S-1-5-5-", 8)) {
            wcscpy(out, L"<logon>"); out += 7; in += 8;
            while (iswdigit(*in) || *in == L'-') in++;
        } else *out++ = *in++;
    }
    *out = 0;
}

static void o_sd(const char *key, PSECURITY_DESCRIPTOR sd, SECURITY_INFORMATION si)
{
    LPWSTR s = NULL;
    static wchar_t buf[8192];
    if (!sd) { o_str(key, NULL); return; }
    if (!ConvertSecurityDescriptorToStringSecurityDescriptorW(sd, SDDL_REVISION_1, si, &s, NULL)) {
        char k[64];
        snprintf(k, sizeof k, "%s_error", key);
        o_u32(k, GetLastError());
        return;
    }
    tokenize(s, buf);
    o_wstr(key, buf);
    LocalFree(s);
}
#define SI_ALL (OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION)
static GENERIC_MAPPING g_fmap = { FILE_GENERIC_READ, FILE_GENERIC_WRITE, FILE_GENERIC_EXECUTE, FILE_ALL_ACCESS };

static void out_sd(PSECURITY_DESCRIPTOR sd)
{
    SECURITY_DESCRIPTOR_CONTROL c = 0;
    DWORD rev = 0;
    GetSecurityDescriptorControl(sd, &c, &rev);
    o_hex32("control", c);
    o_sd("sd", sd, SI_ALL | SACL_SECURITY_INFORMATION | LABEL_SECURITY_INFORMATION);
}

/* CreatePrivateObjectSecurityEx with the given SDDL templates (NULL = none). */
static void create_ex(const char *name, const wchar_t *parent, const wchar_t *creator, GUID *type, BOOL container,
                      ULONG flags, HANDLE tok)
{
    PSECURITY_DESCRIPTOR p = parent ? sd_from(parent) : NULL, c = creator ? sd_from(creator) : NULL, n = NULL;
    BOOL r;
    ORACLE_CALL(r, CreatePrivateObjectSecurityEx(p, c, &n, type, container, flags, tok, &g_fmap));
    oracle_case(name);
    o_bool("ret", r);
    o_lasterr();
    if (r) {
        out_sd(n);
        DestroyPrivateObjectSecurity(&n);
    }
    oracle_case_end();
    LocalFree(p);
    LocalFree(c);
}

static void create(const char *name, const wchar_t *parent, const wchar_t *creator, BOOL container)
{
    PSECURITY_DESCRIPTOR p = parent ? sd_from(parent) : NULL, c = creator ? sd_from(creator) : NULL, n = NULL;
    BOOL r;
    ORACLE_CALL(r, CreatePrivateObjectSecurity(p, c, &n, container, g_prim, &g_fmap));
    oracle_case(name);
    o_bool("ret", r);
    o_lasterr();
    if (r) {
        out_sd(n);
        DestroyPrivateObjectSecurity(&n);
    }
    oracle_case_end();
    LocalFree(p);
    LocalFree(c);
}

static const wchar_t *P_ALL = L"O:SYG:SYD:(A;OICI;FA;;;SY)(A;OI;FR;;;WD)(A;CI;FX;;;BU)(A;OICIIO;GA;;;CO)"
                              L"(A;OICINP;0x1;;;AU)(A;;0x2;;;BA)";

int main(void)
{
    oracle_begin("advapi32/PrivateObjectSecurity");
    acl_init();

    /* Defaults from the token. */
    create("default_object", NULL, NULL, FALSE);
    create("default_container", NULL, NULL, TRUE);
    create("creator_dacl_only", NULL, L"D:(A;;GR;;;WD)", FALSE);
    create("creator_owner_group_only", NULL, L"O:$UG:BA", FALSE);
    create("creator_null_dacl", NULL, L"D:NO_ACCESS_CONTROL", FALSE);
    create("creator_empty_dacl", NULL, L"D:", FALSE);
    create("creator_generic_mapped", NULL, L"D:(A;;GA;;;WD)(A;OICI;GR;;;BA)", TRUE);

    /* Inheritance from a parent. */
    create("inherit_object", P_ALL, NULL, FALSE);
    create("inherit_container", P_ALL, NULL, TRUE);
    create("inherit_parent_no_inheritable_obj", L"O:SYG:SYD:(A;;FA;;;WD)", NULL, FALSE);
    create("inherit_parent_empty_dacl", L"O:SYG:SYD:", NULL, FALSE);
    create("inherit_parent_null_dacl", L"O:SYG:SYD:NO_ACCESS_CONTROL", NULL, FALSE);
    create("inherit_parent_ci_only_obj", L"O:SYG:SYD:(A;CI;FA;;;WD)", NULL, FALSE);
    create("inherit_parent_oi_only_container", L"O:SYG:SYD:(A;OI;FA;;;WD)", NULL, TRUE);
    create("inherit_parent_oinp_container", L"O:SYG:SYD:(A;OINP;FA;;;WD)", NULL, TRUE);
    create("inherit_parent_deny_order", L"O:SYG:SYD:(A;OICI;FR;;;WD)(D;OICI;FW;;;WD)", NULL, FALSE);
    create("inherit_creator_owner_obj", L"O:SYG:SYD:(A;OICI;FA;;;CO)", NULL, FALSE);
    create("inherit_creator_owner_container", L"O:SYG:SYD:(A;OICI;FA;;;CO)", NULL, TRUE);
    create("inherit_creator_group_container", L"O:SYG:SYD:(A;OICI;GR;;;CG)", NULL, TRUE);
    create("inherit_creator_owner_explicit_owner", L"O:SYG:SYD:(A;OICI;FA;;;CO)", L"O:BAG:SY", FALSE);
    create("inherit_parent_already_inherited", L"O:SYG:SYD:AI(A;OICIID;FA;;;WD)", NULL, FALSE);
    create("inherit_parent_protected", L"O:SYG:SYD:P(A;OICI;FA;;;WD)", NULL, FALSE);

    /* Creator DACL vs inheritance. */
    create("creator_overrides_inheritance", P_ALL, L"D:(A;;FR;;;WD)", FALSE);
    create("creator_null_dacl_with_parent", P_ALL, L"D:NO_ACCESS_CONTROL", FALSE);
    create("creator_no_dacl_with_parent", P_ALL, L"O:$UG:BA", FALSE);
    create_ex("ex_auto_inherit", P_ALL, L"D:(A;;FR;;;WD)", NULL, FALSE, SEF_DACL_AUTO_INHERIT, g_prim);
    create_ex("ex_auto_inherit_container", P_ALL, L"D:(A;;FR;;;WD)", NULL, TRUE, SEF_DACL_AUTO_INHERIT, g_prim);
    create_ex("ex_auto_inherit_protected", P_ALL, L"D:P(A;;FR;;;WD)", NULL, FALSE, SEF_DACL_AUTO_INHERIT, g_prim);
    create_ex("ex_auto_inherit_no_creator", P_ALL, NULL, NULL, FALSE, SEF_DACL_AUTO_INHERIT, g_prim);
    create_ex("ex_auto_inherit_creator_inherited_aces", P_ALL, L"D:(A;ID;FA;;;SY)(A;;FR;;;WD)", NULL, FALSE,
              SEF_DACL_AUTO_INHERIT, g_prim);
    create_ex("ex_no_flags", P_ALL, NULL, NULL, FALSE, 0, g_prim);
    create_ex("ex_default_for_object", P_ALL, L"D:(A;;FR;;;WD)", NULL, FALSE, SEF_DEFAULT_DESCRIPTOR_FOR_OBJECT,
              g_prim);
    create_ex("ex_default_owner_from_parent", L"O:BAG:BUD:(A;OICI;FA;;;WD)", NULL, NULL, FALSE,
              SEF_DEFAULT_OWNER_FROM_PARENT | SEF_DEFAULT_GROUP_FROM_PARENT, g_prim);

    /* Owner check. */
    create("creator_owner_other_sid", NULL, L"O:SYG:SYD:(A;;FA;;;WD)", FALSE);
    create("creator_owner_admins", NULL, L"O:BAG:SYD:(A;;FA;;;WD)", FALSE);
    create("creator_owner_users", NULL, L"O:BUG:SYD:(A;;FA;;;WD)", FALSE);
    create_ex("ex_avoid_owner_check", NULL, L"O:SYG:SYD:(A;;FA;;;WD)", NULL, FALSE, SEF_AVOID_OWNER_CHECK, g_prim);
    create("creator_group_any", NULL, L"O:$UG:SYD:(A;;FA;;;WD)", FALSE);

    /* SACLs. */
    create("creator_sacl_no_privilege", NULL, L"D:(A;;FA;;;WD)S:(AU;SA;FA;;;WD)", FALSE);
    create_ex("ex_sacl_avoid_privilege", NULL, L"D:(A;;FA;;;WD)S:(AU;SA;FA;;;WD)", NULL, FALSE,
              SEF_AVOID_PRIVILEGE_CHECK, g_prim);
    create("parent_sacl_inheritable", L"O:SYG:SYD:(A;OICI;FA;;;WD)S:(AU;OICISA;FA;;;WD)", NULL, FALSE);
    create("creator_label", NULL, L"D:(A;;FA;;;WD)S:(ML;;NW;;;LW)", FALSE);
    create("parent_label_inheritable", L"O:SYG:SYD:(A;OICI;FA;;;WD)S:(ML;OICI;NW;;;LW)", NULL, FALSE);

    /* Object-type inheritance (inherited object type GUID). */
    {
        static GUID ga = { 0x22000000, 0, 0, { 0 } }, gb = { 0x33000000, 0, 0, { 0 } };
        const wchar_t *p = L"O:SYG:SYD:(OA;CI;RP;;22000000-0000-0000-0000-000000000000;WD)"
                           L"(OA;CI;WP;;33000000-0000-0000-0000-000000000000;WD)(A;CI;CC;;;BA)";
        create_ex("objtype_a", p, NULL, &ga, TRUE, 0, g_prim);
        create_ex("objtype_b", p, NULL, &gb, TRUE, 0, g_prim);
        create_ex("objtype_none", p, NULL, NULL, TRUE, 0, g_prim);
    }

    /* Token argument. */
    create_ex("token_null", NULL, NULL, NULL, FALSE, 0, NULL);
    create_ex("token_impersonation", NULL, NULL, NULL, FALSE, 0, g_imp);

    /* GetPrivateObjectSecurity. */
    {
        PSECURITY_DESCRIPTOR n = NULL;
        PSECURITY_DESCRIPTOR c = sd_from(L"O:$UG:BAD:(A;;FA;;;WD)");
        BYTE buf[1024];
        DWORD len = 0;
        BOOL r;
        CreatePrivateObjectSecurity(NULL, c, &n, FALSE, g_prim, &g_fmap);
        ORACLE_CALL(r, GetPrivateObjectSecurity(n, DACL_SECURITY_INFORMATION, buf, 0, &len));
        oracle_case("get_size_query");
        o_bool("ret", r);
        o_lasterr();
        o_u32("len", len);
        oracle_case_end();
        ORACLE_CALL(r, GetPrivateObjectSecurity(n, DACL_SECURITY_INFORMATION, buf, sizeof buf, &len));
        oracle_case("get_dacl");
        o_bool("ret", r);
        o_lasterr();
        o_u32("len", len);
        if (r) out_sd(buf);
        oracle_case_end();
        ORACLE_CALL(r, GetPrivateObjectSecurity(n, OWNER_SECURITY_INFORMATION, buf, sizeof buf, &len));
        oracle_case("get_owner");
        o_bool("ret", r);
        o_lasterr();
        if (r) out_sd(buf);
        oracle_case_end();
        ORACLE_CALL(r, GetPrivateObjectSecurity(n, SI_ALL | SACL_SECURITY_INFORMATION, buf, sizeof buf, &len));
        oracle_case("get_all");
        o_bool("ret", r);
        o_lasterr();
        if (r) out_sd(buf);
        oracle_case_end();

        /* SetPrivateObjectSecurity. */
        PSECURITY_DESCRIPTOR m = sd_from(L"D:(A;;FR;;;BU)");
        ORACLE_CALL(r, SetPrivateObjectSecurity(DACL_SECURITY_INFORMATION, m, &n, &g_fmap, g_prim));
        oracle_case("set_dacl");
        o_bool("ret", r);
        o_lasterr();
        out_sd(n);
        oracle_case_end();
        LocalFree(m);
        m = sd_from(L"D:(A;;GA;;;BU)");
        ORACLE_CALL(r, SetPrivateObjectSecurity(DACL_SECURITY_INFORMATION, m, &n, &g_fmap, g_prim));
        oracle_case("set_dacl_generic");
        o_bool("ret", r);
        o_lasterr();
        out_sd(n);
        oracle_case_end();
        LocalFree(m);
        m = sd_from(L"O:SY");
        ORACLE_CALL(r, SetPrivateObjectSecurity(OWNER_SECURITY_INFORMATION, m, &n, &g_fmap, g_prim));
        oracle_case("set_owner_other");
        o_bool("ret", r);
        o_lasterr();
        out_sd(n);
        oracle_case_end();
        LocalFree(m);
        m = sd_from(L"O:BA");
        ORACLE_CALL(r, SetPrivateObjectSecurity(OWNER_SECURITY_INFORMATION, m, &n, &g_fmap, g_prim));
        oracle_case("set_owner_admins");
        o_bool("ret", r);
        o_lasterr();
        out_sd(n);
        oracle_case_end();
        LocalFree(m);
        m = sd_from(L"G:SY");
        ORACLE_CALL(r, SetPrivateObjectSecurity(GROUP_SECURITY_INFORMATION, m, &n, &g_fmap, g_prim));
        oracle_case("set_group");
        o_bool("ret", r);
        o_lasterr();
        out_sd(n);
        oracle_case_end();
        LocalFree(m);
        m = sd_from(L"D:AI(A;;FR;;;BU)");
        ORACLE_CALL(r, SetPrivateObjectSecurityEx(DACL_SECURITY_INFORMATION, m, &n, SEF_DACL_AUTO_INHERIT, &g_fmap,
                                                  g_prim));
        oracle_case("set_ex_auto_inherit");
        o_bool("ret", r);
        o_lasterr();
        out_sd(n);
        oracle_case_end();
        LocalFree(m);
        DestroyPrivateObjectSecurity(&n);
        LocalFree(c);
    }

    /* ConvertToAutoInheritPrivateObjectSecurity. */
    {
        const wchar_t *parent = L"O:SYG:SYD:(A;OICI;FA;;;SY)(A;OI;FR;;;WD)";
        struct { const char *name; const wchar_t *cur; BOOL dir; } cv[] = {
            { "convert_matching", L"O:SYG:SYD:(A;;FA;;;SY)(A;;FR;;;WD)", FALSE },
            { "convert_extra_explicit", L"O:SYG:SYD:(A;;FX;;;BU)(A;;FA;;;SY)(A;;FR;;;WD)", FALSE },
            { "convert_no_match", L"O:SYG:SYD:(A;;FX;;;BU)", FALSE },
            { "convert_container", L"O:SYG:SYD:(A;OICI;FA;;;SY)(A;OIIO;FR;;;WD)", TRUE },
            { "convert_protected", L"O:SYG:SYD:P(A;;FA;;;SY)(A;;FR;;;WD)", FALSE },
            { "convert_null_dacl", L"O:SYG:SYD:NO_ACCESS_CONTROL", FALSE },
        };
        for (size_t i = 0; i < sizeof cv / sizeof cv[0]; i++) {
            PSECURITY_DESCRIPTOR p = sd_from(parent), c = sd_from(cv[i].cur), n = NULL;
            BOOL r;
            ORACLE_CALL(r, ConvertToAutoInheritPrivateObjectSecurity(p, c, &n, NULL, cv[i].dir, &g_fmap));
            oracle_case(cv[i].name);
            o_bool("ret", r);
            o_lasterr();
            if (r) {
                out_sd(n);
                DestroyPrivateObjectSecurity(&n);
            }
            oracle_case_end();
            LocalFree(p);
            LocalFree(c);
        }
        PSECURITY_DESCRIPTOR c = sd_from(L"O:SYG:SYD:(A;;FA;;;SY)"), n = NULL;
        BOOL r;
        ORACLE_CALL(r, ConvertToAutoInheritPrivateObjectSecurity(NULL, c, &n, NULL, FALSE, &g_fmap));
        oracle_case("convert_no_parent");
        o_bool("ret", r);
        o_lasterr();
        if (r) {
            out_sd(n);
            DestroyPrivateObjectSecurity(&n);
        }
        oracle_case_end();
        LocalFree(c);
    }
    return oracle_end();
}
