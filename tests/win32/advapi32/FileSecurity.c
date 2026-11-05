/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: SetFileSecurityW / GetFileSecurityW round trips on a file and
 * a directory the test creates: protected DACLs, NULL and empty DACLs,
 * directory DACLs with inheritance flags, owner changes (the user, an owner
 * group, and a SID the token doesn't hold: ERROR_INVALID_OWNER unless
 * SeRestorePrivilege is enabled), primary group, SACL without and with
 * SeSecurityPrivilege, mandatory labels, each SECURITY_INFORMATION subset,
 * buffer sizing, GetSecurityInfo / SetSecurityInfo on handles (and the access
 * they need: READ_CONTROL, WRITE_DAC, WRITE_OWNER, ACCESS_SYSTEM_SECURITY),
 * and missing paths.
 *
 * Every DACL set on the test's top-level objects is protected, so nothing
 * from the work directory's parents shows up. SDs are printed as SDDL with
 * the user's SID as <user>.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-setfilesecurityw
 *       https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-getfilesecurityw
 *       https://learn.microsoft.com/en-us/windows/win32/api/aclapi/nf-aclapi-getsecurityinfo
 *       https://learn.microsoft.com/en-us/windows/win32/api/aclapi/nf-aclapi-setsecurityinfo
 *       https://learn.microsoft.com/en-us/windows/win32/secauthz/security-information
 *       https://learn.microsoft.com/en-us/windows/win32/secauthz/sacl-access-right
 *       https://learn.microsoft.com/en-us/windows/win32/secauthz/privilege-constants
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
static void set_case(const char *name, const wchar_t *path, SECURITY_INFORMATION si, const wchar_t *sddl,
                     SECURITY_INFORMATION show)
{
    BOOL r;
    PSECURITY_DESCRIPTOR sd = sd_from(sddl);
    ORACLE_CALL(r, SetFileSecurityW(path, si, sd));
    oracle_case(name);
    o_bool("ret", r);
    o_lasterr();
    o_file_sd("after", path, show);
    oracle_case_end();
    LocalFree(sd);
}

static void handle_get(const char *name, const wchar_t *path, DWORD access, SECURITY_INFORMATION si)
{
    HANDLE h = CreateFileW(path, access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                           OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    PSECURITY_DESCRIPTOR sd = NULL;
    DWORD r;
    oracle_case(name);
    o_handle("open", h);
    if (h != INVALID_HANDLE_VALUE) {
        r = GetSecurityInfo(h, SE_FILE_OBJECT, si, NULL, NULL, NULL, NULL, &sd);
        o_u32("ret", r);
        if (r == ERROR_SUCCESS) {
            o_sd("sd", sd, si);
            LocalFree(sd);
        }
        CloseHandle(h);
    }
    oracle_case_end();
}

static void handle_set(const char *name, const wchar_t *path, DWORD access, SECURITY_INFORMATION si,
                       const wchar_t *sddl)
{
    HANDLE h = CreateFileW(path, access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                           OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    PSECURITY_DESCRIPTOR sd = sd_from(sddl);
    BOOL present = FALSE, def = FALSE;
    PACL dacl = NULL, sacl = NULL;
    PSID owner = NULL, group = NULL;
    DWORD r;
    GetSecurityDescriptorDacl(sd, &present, &dacl, &def);
    GetSecurityDescriptorSacl(sd, &present, &sacl, &def);
    GetSecurityDescriptorOwner(sd, &owner, &def);
    GetSecurityDescriptorGroup(sd, &group, &def);
    oracle_case(name);
    o_handle("open", h);
    if (h != INVALID_HANDLE_VALUE) {
        r = SetSecurityInfo(h, SE_FILE_OBJECT, si, owner, group, dacl, sacl);
        o_u32("ret", r);
        CloseHandle(h);
        o_file_sd("after", path, si & ~(PROTECTED_DACL_SECURITY_INFORMATION | UNPROTECTED_DACL_SECURITY_INFORMATION));
    }
    oracle_case_end();
    LocalFree(sd);
}

int main(void)
{
    oracle_begin("advapi32/FileSecurity");
    acl_init();
    /* Start from the same privileges everywhere: some logons (an SSH session
     * of an administrator, for one) have these enabled by default. */
    enable_priv(L"SeBackupPrivilege", FALSE);
    enable_priv(L"SeRestorePrivilege", FALSE);
    enable_priv(L"SeTakeOwnershipPrivilege", FALSE);
    enable_priv(L"SeSecurityPrivilege", FALSE);
    const SECURITY_INFORMATION OGD = SI_ALL;
    CreateDirectoryW(L"d", NULL);
    HANDLE h = CreateFileW(L"f", GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL);
    CloseHandle(h);

    /* DACLs. */
    set_case("file_protected_dacl", L"f", DACL_SECURITY_INFORMATION, L"D:P(A;;FA;;;$U)(A;;FR;;;WD)",
             DACL_SECURITY_INFORMATION);
    set_case("file_dacl_generic", L"f", DACL_SECURITY_INFORMATION, L"D:P(A;;GA;;;$U)(A;;GR;;;WD)",
             DACL_SECURITY_INFORMATION);
    set_case("file_dacl_specific_mask", L"f", DACL_SECURITY_INFORMATION,
             L"D:P(A;;0x1f01ff;;;$U)(A;;0x120089;;;BU)(D;;0x2;;;AN)", DACL_SECURITY_INFORMATION);
    set_case("file_dacl_deny_first_kept", L"f", DACL_SECURITY_INFORMATION,
             L"D:P(A;;FA;;;$U)(D;;FW;;;BG)", DACL_SECURITY_INFORMATION);
    set_case("file_null_dacl", L"f", DACL_SECURITY_INFORMATION, L"D:NO_ACCESS_CONTROL", DACL_SECURITY_INFORMATION);
    set_case("file_empty_dacl", L"f", DACL_SECURITY_INFORMATION, L"D:P", DACL_SECURITY_INFORMATION);
    set_case("file_empty_dacl_owner_reads", L"f", DACL_SECURITY_INFORMATION, L"D:P(A;;FA;;;$U)",
             DACL_SECURITY_INFORMATION | OWNER_SECURITY_INFORMATION);
    set_case("file_dacl_inherit_flags_on_file", L"f", DACL_SECURITY_INFORMATION,
             L"D:P(A;OICI;FA;;;$U)(A;OICIIO;GR;;;WD)", DACL_SECURITY_INFORMATION);
    set_case("file_dacl_inherited_flag_kept", L"f", DACL_SECURITY_INFORMATION,
             L"D:PAI(A;;FA;;;$U)(A;ID;FR;;;WD)", DACL_SECURITY_INFORMATION);
    set_case("dir_protected_dacl", L"d", DACL_SECURITY_INFORMATION,
             L"D:P(A;OICI;FA;;;$U)(A;OICIIO;GA;;;CO)(A;CI;FR;;;WD)(A;OINP;FX;;;BU)", DACL_SECURITY_INFORMATION);
    set_case("dir_auto_inherited_flags", L"d", DACL_SECURITY_INFORMATION,
             L"D:PAI(A;OICI;FA;;;$U)(A;OICI;FA;;;SY)", DACL_SECURITY_INFORMATION);

    /* Owner and group. */
    set_case("file_owner_user", L"f", OWNER_SECURITY_INFORMATION, L"O:$U", OGD);
    set_case("file_owner_admins", L"f", OWNER_SECURITY_INFORMATION, L"O:BA", OGD);
    set_case("file_owner_system", L"f", OWNER_SECURITY_INFORMATION, L"O:SY", OGD);
    set_case("file_owner_everyone", L"f", OWNER_SECURITY_INFORMATION, L"O:WD", OGD);
    oracle_case("enable_restore");
    o_bool("ret", enable_priv(L"SeRestorePrivilege", TRUE));
    oracle_case_end();
    set_case("file_owner_system_restore_priv", L"f", OWNER_SECURITY_INFORMATION, L"O:SY", OGD);
    set_case("file_owner_back_to_user", L"f", OWNER_SECURITY_INFORMATION, L"O:$U", OGD);
    enable_priv(L"SeRestorePrivilege", FALSE);
    set_case("file_group_system", L"f", GROUP_SECURITY_INFORMATION, L"G:SY", OGD);
    set_case("file_group_everyone", L"f", GROUP_SECURITY_INFORMATION, L"G:WD", OGD);
    set_case("file_group_user", L"f", GROUP_SECURITY_INFORMATION, L"G:$U", OGD);
    set_case("file_owner_group_dacl", L"f", OGD, L"O:$UG:BAD:P(A;;FA;;;$U)(A;;FR;;;BA)", OGD);
    set_case("file_owner_missing_in_sd", L"f", OWNER_SECURITY_INFORMATION, L"G:BA", OGD);
    set_case("file_dacl_missing_in_sd", L"f", DACL_SECURITY_INFORMATION, L"O:$U", OGD);
    set_case("dir_owner_admins", L"d", OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION, L"O:BAG:BA", OGD);

    /* SACL and labels. */
    set_case("file_sacl_no_priv", L"f", SACL_SECURITY_INFORMATION, L"S:(AU;SAFA;FA;;;WD)", DACL_SECURITY_INFORMATION);
    set_case("file_sacl_read_no_priv", L"f", 0, L"", SACL_SECURITY_INFORMATION);
    oracle_case("enable_security");
    o_bool("ret", enable_priv(L"SeSecurityPrivilege", TRUE));
    oracle_case_end();
    set_case("file_sacl_with_priv", L"f", SACL_SECURITY_INFORMATION, L"S:(AU;SAFA;FA;;;WD)",
             SACL_SECURITY_INFORMATION);
    set_case("file_sacl_protected", L"f", SACL_SECURITY_INFORMATION, L"S:P(AU;FA;FW;;;BU)", SACL_SECURITY_INFORMATION);
    set_case("file_sacl_empty", L"f", SACL_SECURITY_INFORMATION, L"S:", SACL_SECURITY_INFORMATION);
    enable_priv(L"SeSecurityPrivilege", FALSE);
    set_case("file_label_low", L"f", LABEL_SECURITY_INFORMATION, L"S:(ML;;NW;;;LW)", LABEL_SECURITY_INFORMATION);
    set_case("file_label_high", L"f", LABEL_SECURITY_INFORMATION, L"S:(ML;;NWNR;;;HI)", LABEL_SECURITY_INFORMATION);
    set_case("file_label_system", L"f", LABEL_SECURITY_INFORMATION, L"S:(ML;;NW;;;SI)", LABEL_SECURITY_INFORMATION);
    set_case("file_label_medium", L"f", LABEL_SECURITY_INFORMATION, L"S:(ML;;NW;;;ME)", LABEL_SECURITY_INFORMATION);
    set_case("file_label_none", L"f", LABEL_SECURITY_INFORMATION, L"S:", LABEL_SECURITY_INFORMATION);

    /* Reading subsets and sizing. */
    set_file_sd(L"f", OGD, L"O:$UG:BAD:P(A;;FA;;;$U)(A;;FR;;;WD)");
    {
        BYTE buf[1024];
        DWORD need = 0;
        BOOL r;
        ORACLE_CALL(r, GetFileSecurityW(L"f", DACL_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION, buf, 0, &need));
        oracle_case("get_size_query");
        o_bool("ret", r);
        o_lasterr();
        o_bool("need_nonzero", need > 0);
        DWORD need2 = 0;
        BOOL r2 = GetFileSecurityW(L"f", DACL_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION, buf, need, &need2);
        o_bool("exact_ret", r2);
        o_bool("exact_need_same", need2 == need);
        oracle_case_end();
        ORACLE_CALL(r, GetFileSecurityW(L"f", 0, buf, sizeof buf, &need));
        oracle_case("get_nothing");
        o_bool("ret", r);
        o_lasterr();
        if (r) o_sd("sd", buf, OGD);
        oracle_case_end();
        ORACLE_CALL(r, GetFileSecurityW(L"missing", DACL_SECURITY_INFORMATION, buf, sizeof buf, &need));
        oracle_case("get_missing");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        PSECURITY_DESCRIPTOR sd = sd_from(L"D:P(A;;FA;;;WD)");
        ORACLE_CALL(r, SetFileSecurityW(L"missing", DACL_SECURITY_INFORMATION, sd));
        oracle_case("set_missing");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, SetFileSecurityW(L"nodir\\x", DACL_SECURITY_INFORMATION, sd));
        oracle_case("set_missing_path");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        LocalFree(sd);
    }
    oracle_case("subsets");
    o_file_sd("owner", L"f", OWNER_SECURITY_INFORMATION);
    o_file_sd("group", L"f", GROUP_SECURITY_INFORMATION);
    o_file_sd("dacl", L"f", DACL_SECURITY_INFORMATION);
    o_file_sd("label", L"f", LABEL_SECURITY_INFORMATION);
    oracle_case_end();

    /* Handles: Get/SetSecurityInfo and the access rights they need. */
    handle_get("handle_get_read_control", L"f", READ_CONTROL, OGD);
    handle_get("handle_get_without_read_control", L"f", FILE_READ_ATTRIBUTES, DACL_SECURITY_INFORMATION);
    handle_get("handle_get_sacl_without_ass", L"f", READ_CONTROL, SACL_SECURITY_INFORMATION);
    handle_set("handle_set_dacl_write_dac", L"f", WRITE_DAC, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
               L"D:P(A;;FA;;;$U)(A;;FX;;;BU)");
    handle_set("handle_set_dacl_no_write_dac", L"f", READ_CONTROL, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
               L"D:P(A;;FA;;;$U)");
    handle_set("handle_set_owner_write_owner", L"f", WRITE_OWNER | READ_CONTROL, OWNER_SECURITY_INFORMATION, L"O:BA");
    handle_set("handle_set_owner_write_dac_only", L"f", WRITE_DAC | READ_CONTROL, OWNER_SECURITY_INFORMATION, L"O:$U");
    handle_set("handle_set_group_write_owner", L"f", WRITE_OWNER | READ_CONTROL, GROUP_SECURITY_INFORMATION, L"G:BU");
    handle_set("handle_set_dir_dacl", L"d", WRITE_DAC | READ_CONTROL,
               DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, L"D:P(A;OICI;FA;;;$U)(A;OICI;FR;;;BU)");
    handle_set("handle_set_label", L"f", WRITE_OWNER | READ_CONTROL, LABEL_SECURITY_INFORMATION, L"S:(ML;;NW;;;LW)");
    enable_priv(L"SeSecurityPrivilege", TRUE);
    handle_get("handle_get_sacl_with_ass", L"f", READ_CONTROL | ACCESS_SYSTEM_SECURITY, SACL_SECURITY_INFORMATION);
    handle_set("handle_set_sacl_with_ass", L"f", ACCESS_SYSTEM_SECURITY | READ_CONTROL, SACL_SECURITY_INFORMATION,
               L"S:(AU;FA;FA;;;BA)");
    enable_priv(L"SeSecurityPrivilege", FALSE);

    /* Leave everything deletable. */
    set_file_sd(L"f", DACL_SECURITY_INFORMATION, L"D:P(A;;FA;;;WD)");
    set_file_sd(L"d", DACL_SECURITY_INFORMATION, L"D:P(A;OICI;FA;;;WD)");
    DeleteFileW(L"f");
    RemoveDirectoryW(L"d");
    return oracle_end();
}
