/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: GetNamedSecurityInfoW / SetNamedSecurityInfoW and
 * SetSecurityInfo on files and directories (SE_FILE_OBJECT). Unlike
 * SetFileSecurityW, these apply the automatic inheritance model: setting a
 * directory's DACL propagates its inheritable ACEs to existing children, a
 * child's unprotected DACL gets the parent's inheritable ACEs merged in, and
 * PROTECTED_ / UNPROTECTED_DACL_SECURITY_INFORMATION switch protection. Also
 * the returned owner/group/DACL pointers, SetEntriesInAclW +
 * GetExplicitEntriesFromAclW round trips, and error codes for missing paths.
 *
 * SDs are printed as SDDL with the user's SID as <user>.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/aclapi/nf-aclapi-getnamedsecurityinfow
 *       https://learn.microsoft.com/en-us/windows/win32/api/aclapi/nf-aclapi-setnamedsecurityinfow
 *       https://learn.microsoft.com/en-us/windows/win32/api/aclapi/nf-aclapi-setsecurityinfo
 *       https://learn.microsoft.com/en-us/windows/win32/api/aclapi/nf-aclapi-setentriesinaclw
 *       https://learn.microsoft.com/en-us/windows/win32/api/aclapi/nf-aclapi-getexplicitentriesfromaclw
 *       https://learn.microsoft.com/en-us/windows/win32/secauthz/automatic-propagation-of-inheritable-aces
 */
#include "oracle.h"
#include <sddl.h>
#include <aclapi.h>
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
/* File security helpers. */
static void o_file_sd(const char *key, const wchar_t *path, SECURITY_INFORMATION si)
{
    static BYTE buf[16384];
    DWORD need = 0;
    if (!GetFileSecurityW(path, si, buf, sizeof buf, &need)) {
        char k[64];
        snprintf(k, sizeof k, "%s_error", key);
        o_u32(k, GetLastError());
        return;
    }
    SECURITY_DESCRIPTOR_CONTROL c = 0;
    DWORD rev = 0;
    char k[64];
    GetSecurityDescriptorControl(buf, &c, &rev);
    snprintf(k, sizeof k, "%s_control", key);
    o_hex32(k, c & ~SE_SELF_RELATIVE);
    o_sd(key, buf, si);
}

static __attribute__((unused)) BOOL set_file_sd(const wchar_t *path, SECURITY_INFORMATION si, const wchar_t *sddl)
{
    PSECURITY_DESCRIPTOR sd = sd_from(sddl);
    BOOL r = SetFileSecurityW(path, si, sd);
    LocalFree(sd);
    return r;
}

static __attribute__((unused)) BOOL enable_priv(const wchar_t *name, BOOL on)
{
    TOKEN_PRIVILEGES tp;
    tp.PrivilegeCount = 1;
    LookupPrivilegeValueW(NULL, name, &tp.Privileges[0].Luid);
    tp.Privileges[0].Attributes = on ? SE_PRIVILEGE_ENABLED : 0;
    return AdjustTokenPrivileges(g_prim, FALSE, &tp, 0, NULL, NULL) && GetLastError() == ERROR_SUCCESS;
}
static void show(const char *name, const wchar_t *path)
{
    oracle_case(name);
    o_file_sd("sd", path, DACL_SECURITY_INFORMATION);
    oracle_case_end();
}

static PACL dacl_of(PSECURITY_DESCRIPTOR sd)
{
    BOOL present = FALSE, def = FALSE;
    PACL acl = NULL;
    GetSecurityDescriptorDacl(sd, &present, &acl, &def);
    return acl;
}

static void named_set(const char *name, const wchar_t *path, SECURITY_INFORMATION si, const wchar_t *sddl)
{
    PSECURITY_DESCRIPTOR sd = sd_from(sddl);
    DWORD r = SetNamedSecurityInfoW((LPWSTR)path, SE_FILE_OBJECT, si, NULL, NULL, dacl_of(sd), NULL);
    oracle_case(name);
    o_u32("ret", r);
    o_file_sd("sd", path, DACL_SECURITY_INFORMATION);
    oracle_case_end();
    LocalFree(sd);
}

int main(void)
{
    oracle_begin("advapi32/NamedSecurityInfo");
    acl_init();
    DWORD r;

    CreateDirectoryW(L"p", NULL);
    set_file_sd(L"p", DACL_SECURITY_INFORMATION, L"D:P(A;OICI;FA;;;$U)(A;OICI;FR;;;BU)");
    CreateDirectoryW(L"p\\d", NULL);
    HANDLE h = CreateFileW(L"p\\f", GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL);
    CloseHandle(h);
    h = CreateFileW(L"p\\d\\g", GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL);
    CloseHandle(h);
    h = CreateFileW(L"p\\explicit", GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL);
    CloseHandle(h);
    set_file_sd(L"p\\explicit", DACL_SECURITY_INFORMATION, L"D:(A;;FA;;;BG)");
    show("initial_child", L"p\\f");
    show("initial_explicit_unprotected", L"p\\explicit");

    /* GetNamedSecurityInfoW out-pointers. */
    {
        PSID owner = NULL, group = NULL;
        PACL dacl = NULL, sacl = NULL;
        PSECURITY_DESCRIPTOR sd = NULL;
        r = GetNamedSecurityInfoW(L"p\\f", SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | OWNER_SECURITY_INFORMATION,
                                  &owner, &group, &dacl, &sacl, &sd);
        oracle_case("get_named_pointers");
        o_u32("ret", r);
        o_bool("owner_set", owner != NULL);
        o_bool("group_set", group != NULL);
        o_bool("dacl_set", dacl != NULL);
        o_bool("sacl_set", sacl != NULL);
        if (dacl) o_u32("ace_count", dacl->AceCount);
        oracle_case_end();
        LocalFree(sd);
        r = GetNamedSecurityInfoW(L"p\\missing", SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, &dacl, NULL,
                                  &sd);
        oracle_case("get_named_missing");
        o_u32("ret", r);
        oracle_case_end();
        r = GetNamedSecurityInfoW(L"p\\nodir\\x", SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, &dacl, NULL,
                                  &sd);
        oracle_case("get_named_missing_path");
        o_u32("ret", r);
        oracle_case_end();
        r = GetNamedSecurityInfoW(L"p\\f", SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, NULL, NULL, NULL);
        oracle_case("get_named_no_outputs");
        o_u32("ret", r);
        oracle_case_end();
    }

    /* Setting the parent's DACL propagates to children. */
    named_set("set_parent_dacl", L"p", DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
              L"D:(A;OICI;FA;;;$U)(A;OICI;FX;;;BG)(A;CI;FR;;;AU)");
    show("child_after_named_set", L"p\\f");
    show("subdir_after_named_set", L"p\\d");
    show("grandchild_after_named_set", L"p\\d\\g");
    show("explicit_after_named_set", L"p\\explicit");

    /* Child: unprotected DACL merges the parent's ACEs, protected drops them. */
    named_set("child_set_unprotected", L"p\\f", DACL_SECURITY_INFORMATION, L"D:(A;;FR;;;BA)");
    named_set("child_set_protected", L"p\\f", DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
              L"D:(A;;FR;;;BA)");
    named_set("child_set_unprotect_flag", L"p\\f", DACL_SECURITY_INFORMATION | UNPROTECTED_DACL_SECURITY_INFORMATION,
              L"D:(A;;FR;;;BA)");
    named_set("child_set_sddl_protected", L"p\\f", DACL_SECURITY_INFORMATION, L"D:P(A;;FA;;;$U)");
    named_set("child_set_empty_unprotected", L"p\\f", DACL_SECURITY_INFORMATION | UNPROTECTED_DACL_SECURITY_INFORMATION,
              L"D:");

    /* SetSecurityInfo on a handle. */
    {
        PSECURITY_DESCRIPTOR sd = sd_from(L"D:(A;OICI;FA;;;$U)(A;OICI;FR;;;BU)");
        h = CreateFileW(L"p", WRITE_DAC | READ_CONTROL, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
        r = SetSecurityInfo(h, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, NULL,
                            NULL, dacl_of(sd), NULL);
        CloseHandle(h);
        oracle_case("handle_set_parent");
        o_u32("ret", r);
        o_file_sd("sd", L"p", DACL_SECURITY_INFORMATION);
        oracle_case_end();
        LocalFree(sd);
        show("subdir_after_handle_set", L"p\\d");
        show("grandchild_after_handle_set", L"p\\d\\g");
    }

    /* SetEntriesInAclW and GetExplicitEntriesFromAclW. */
    {
        EXPLICIT_ACCESS_W ea[2];
        PACL acl = NULL, acl2 = NULL;
        PSID user = NULL, users = NULL;
        ConvertStringSidToSidW(g_user, &user);
        ConvertStringSidToSidW(L"BU", &users);
        memset(ea, 0, sizeof ea);
        ea[0].grfAccessPermissions = GENERIC_ALL;
        ea[0].grfAccessMode = SET_ACCESS;
        ea[0].grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
        BuildTrusteeWithSidW(&ea[0].Trustee, user);
        ea[1].grfAccessPermissions = FILE_GENERIC_READ;
        ea[1].grfAccessMode = DENY_ACCESS;
        ea[1].grfInheritance = NO_INHERITANCE;
        BuildTrusteeWithSidW(&ea[1].Trustee, users);
        r = SetEntriesInAclW(2, ea, NULL, &acl);
        oracle_case("set_entries");
        o_u32("ret", r);
        if (r == ERROR_SUCCESS) {
            SECURITY_DESCRIPTOR sd;
            InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
            SetSecurityDescriptorDacl(&sd, TRUE, acl, FALSE);
            o_sd("sd", &sd, DACL_SECURITY_INFORMATION);
        }
        oracle_case_end();
        ea[0].grfAccessPermissions = FILE_GENERIC_EXECUTE;
        ea[0].grfAccessMode = GRANT_ACCESS;
        ea[0].grfInheritance = NO_INHERITANCE;
        BuildTrusteeWithSidW(&ea[0].Trustee, users);
        r = SetEntriesInAclW(1, ea, acl, &acl2);
        oracle_case("set_entries_merge_grant");
        o_u32("ret", r);
        if (r == ERROR_SUCCESS) {
            SECURITY_DESCRIPTOR sd;
            InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
            SetSecurityDescriptorDacl(&sd, TRUE, acl2, FALSE);
            o_sd("sd", &sd, DACL_SECURITY_INFORMATION);
        }
        oracle_case_end();
        ULONG n = 0;
        EXPLICIT_ACCESS_W *list = NULL;
        r = GetExplicitEntriesFromAclW(acl2, &n, &list);
        oracle_case("explicit_entries");
        o_u32("ret", r);
        o_u32("count", n);
        for (ULONG i = 0; i < n && i < 4; i++) {
            char k[32];
            snprintf(k, sizeof k, "e%lu_access", (unsigned long)i);
            o_hex32(k, list[i].grfAccessPermissions);
            snprintf(k, sizeof k, "e%lu_mode", (unsigned long)i);
            o_u32(k, list[i].grfAccessMode);
            snprintf(k, sizeof k, "e%lu_inherit", (unsigned long)i);
            o_hex32(k, list[i].grfInheritance);
        }
        oracle_case_end();
        LocalFree(list);
        LocalFree(acl);
        LocalFree(acl2);
        LocalFree(user);
        LocalFree(users);
    }
    {
        PSECURITY_DESCRIPTOR sd = sd_from(L"D:P(A;;FA;;;WD)");
        r = SetNamedSecurityInfoW((LPWSTR)L"p\\missing", SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL,
                                  dacl_of(sd), NULL);
        oracle_case("set_named_missing");
        o_u32("ret", r);
        oracle_case_end();
        LocalFree(sd);
    }
    return oracle_end();
}
