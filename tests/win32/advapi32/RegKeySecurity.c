/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: RegGetKeySecurity / RegSetKeySecurity basics: buffer sizing,
 * which parts come back for each SECURITY_INFORMATION flag, the default
 * descriptor of a new key under HKCU, setting an explicit DACL and reading it
 * back, inheritance into a new subkey, access checks after a DACL change, the
 * SACL privilege rule, and parameter errors.
 *
 * Descriptors are printed machine-independently: SIDs equal to the token user
 * print as "user", the logon SID as "logon", other domain SIDs as
 * "domain-<rid>"; well-known SIDs print in S-1-... form.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-reggetkeysecurity
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regsetkeysecurity
 *       https://learn.microsoft.com/en-us/windows/win32/sysinfo/registry-key-security-and-access-rights
 *       https://learn.microsoft.com/en-us/windows/win32/secauthz/security-descriptor-control
 */
#include "oracle.h"
#include <sddl.h>

static HKEY root;
static PSID user_sid;

static void sid_str(PSID sid, char *out, size_t cap)
{
    wchar_t *s = NULL;
    if (!sid || !IsValidSid(sid)) { snprintf(out, cap, "invalid"); return; }
    if (user_sid && EqualSid(sid, user_sid)) { snprintf(out, cap, "user"); return; }
    PSID_IDENTIFIER_AUTHORITY ia = GetSidIdentifierAuthority(sid);
    UCHAR n = *GetSidSubAuthorityCount(sid);
    if (ia->Value[5] == 5 && n >= 1) {
        DWORD first = *GetSidSubAuthority(sid, 0);
        if (first == 5 && n == 3) { snprintf(out, cap, "logon"); return; }
        if (first == 21 && n == 5) { snprintf(out, cap, "domain-%lu", (unsigned long)*GetSidSubAuthority(sid, 4)); return; }
    }
    ConvertSidToStringSidW(sid, &s);
    snprintf(out, cap, "%s", s ? oracle_utf8(s) : "?");
    LocalFree(s);
}

static void o_acl(const char *k, PACL acl)
{
    char out[2048] = "", item[256], sid[96];
    if (!acl) { o_str(k, "null"); return; }
    for (DWORD i = 0; i < acl->AceCount; i++) {
        ACE_HEADER *h;
        if (!GetAce(acl, i, (void **)&h)) break;
        PSID s = NULL;
        DWORD mask = 0;
        if (h->AceType == ACCESS_ALLOWED_ACE_TYPE || h->AceType == ACCESS_DENIED_ACE_TYPE ||
            h->AceType == SYSTEM_AUDIT_ACE_TYPE || h->AceType == SYSTEM_MANDATORY_LABEL_ACE_TYPE) {
            ACCESS_ALLOWED_ACE *a = (ACCESS_ALLOWED_ACE *)h;
            mask = a->Mask;
            s = &a->SidStart;
        }
        if (s) sid_str(s, sid, sizeof sid); else snprintf(sid, sizeof sid, "-");
        snprintf(item, sizeof item, "%s%u/0x%02x/0x%08lx/%s", i ? ";" : "", h->AceType, h->AceFlags,
                 (unsigned long)mask, sid);
        strncat(out, item, sizeof out - strlen(out) - 1);
    }
    o_str(k, out);
}

static void o_sd(const char *prefix, PSECURITY_DESCRIPTOR sd)
{
    char k[64], s[96];
    SECURITY_DESCRIPTOR_CONTROL ctl = 0;
    DWORD rev;
    PSID owner = NULL, group = NULL;
    PACL dacl = NULL, sacl = NULL;
    BOOL present, def;
    GetSecurityDescriptorControl(sd, &ctl, &rev);
    snprintf(k, sizeof k, "%s.control", prefix);
    o_hex32(k, ctl);
    GetSecurityDescriptorOwner(sd, &owner, &def);
    snprintf(k, sizeof k, "%s.owner", prefix);
    if (owner) { sid_str(owner, s, sizeof s); o_str(k, s); } else o_str(k, NULL);
    GetSecurityDescriptorGroup(sd, &group, &def);
    snprintf(k, sizeof k, "%s.group_present", prefix);
    o_bool(k, group != NULL);
    present = FALSE;
    GetSecurityDescriptorDacl(sd, &present, &dacl, &def);
    snprintf(k, sizeof k, "%s.dacl", prefix);
    if (present) o_acl(k, dacl); else o_str(k, "absent");
    present = FALSE;
    GetSecurityDescriptorSacl(sd, &present, &sacl, &def);
    snprintf(k, sizeof k, "%s.sacl", prefix);
    if (present) o_acl(k, sacl); else o_str(k, "absent");
}

static void get_case(const char *name, HKEY key, SECURITY_INFORMATION si)
{
    BYTE buf[4096];
    DWORD size = 0;
    LONG st, st2;
    ORACLE_CALL(st, RegGetKeySecurity(key, si, NULL, &size));
    DWORD need = size;
    size = sizeof buf;
    ORACLE_CALL(st2, RegGetKeySecurity(key, si, buf, &size));
    oracle_case(name);
    o_status("size_query", st);
    o_bool("size_nonzero", need != 0);
    o_status("ret", st2);
    o_lasterr();
    o_bool("size_matches", size == need);
    if (!st2) o_sd("sd", buf);
    oracle_case_end();
}

static BOOL set_privilege(const wchar_t *name, BOOL on)
{
    HANDLE tok;
    TOKEN_PRIVILEGES tp = {1, {{{0, 0}, on ? SE_PRIVILEGE_ENABLED : 0}}};
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &tok)) return FALSE;
    LookupPrivilegeValueW(NULL, name, &tp.Privileges[0].Luid);
    BOOL ok = AdjustTokenPrivileges(tok, FALSE, &tp, 0, NULL, NULL) && GetLastError() == ERROR_SUCCESS;
    CloseHandle(tok);
    return ok;
}

int main(void)
{
    oracle_begin("advapi32/RegKeySecurity");
    root = oracle_reg_root();
    {
        static BYTE tu[256];
        HANDLE tok;
        DWORD n;
        OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok);
        if (GetTokenInformation(tok, TokenUser, tu, sizeof tu, &n)) user_sid = ((TOKEN_USER *)tu)->User.Sid;
        CloseHandle(tok);
    }

    HKEY k = NULL;
    RegCreateKeyExW(root, L"k", 0, NULL, 0, KEY_ALL_ACCESS | ACCESS_SYSTEM_SECURITY, NULL, &k, NULL);
    if (!k) RegCreateKeyExW(root, L"k", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL);

    get_case("get_owner", k, OWNER_SECURITY_INFORMATION);
    get_case("get_group", k, GROUP_SECURITY_INFORMATION);
    get_case("get_default_dacl", k, DACL_SECURITY_INFORMATION);
    get_case("get_owner_dacl", k, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION);
    get_case("get_none", k, 0);
    get_case("get_label", k, LABEL_SECURITY_INFORMATION);

    {   /* Too small buffer. */
        BYTE buf[8];
        DWORD size = sizeof buf;
        LONG st;
        ORACLE_CALL(st, RegGetKeySecurity(k, DACL_SECURITY_INFORMATION, buf, &size));
        oracle_case("get_small_buffer");
        o_status("ret", st);
        o_lasterr();
        o_bool("size_grew", size > sizeof buf);
        oracle_case_end();
    }
    {   /* SACL needs ACCESS_SYSTEM_SECURITY on the handle. */
        HKEY plain = NULL;
        RegOpenKeyExW(root, L"k", 0, KEY_READ, &plain);
        get_case("get_sacl_without_access", plain, SACL_SECURITY_INFORMATION);
        RegCloseKey(plain);
        BOOL had = set_privilege(L"SeSecurityPrivilege", TRUE);
        HKEY sec = NULL;
        LONG st = RegOpenKeyExW(root, L"k", 0, KEY_READ | ACCESS_SYSTEM_SECURITY, &sec);
        oracle_case("open_with_system_security");
        o_bool("privilege_enabled", had);
        o_status("ret", st);
        oracle_case_end();
        if (!st) {
            get_case("get_sacl_with_access", sec, SACL_SECURITY_INFORMATION);
            RegCloseKey(sec);
        }
        set_privilege(L"SeSecurityPrivilege", FALSE);
        sec = NULL;
        st = RegOpenKeyExW(root, L"k", 0, KEY_READ | ACCESS_SYSTEM_SECURITY, &sec);
        oracle_case("open_system_security_privilege_disabled");
        o_status("ret", st);
        oracle_case_end();
        if (sec) RegCloseKey(sec);
    }

    {   /* Set a protected DACL: user gets KEY_READ only, Administrators full. */
        SECURITY_DESCRIPTOR sd;
        BYTE aclbuf[512];
        PACL acl = (PACL)aclbuf;
        BYTE admins[SECURITY_MAX_SID_SIZE];
        DWORD n = sizeof admins;
        CreateWellKnownSid(WinBuiltinAdministratorsSid, NULL, admins, &n);
        InitializeAcl(acl, sizeof aclbuf, ACL_REVISION);
        AddAccessAllowedAceEx(acl, ACL_REVISION, CONTAINER_INHERIT_ACE, KEY_READ, user_sid);
        AddAccessAllowedAceEx(acl, ACL_REVISION, CONTAINER_INHERIT_ACE, KEY_ALL_ACCESS, admins);
        InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
        SetSecurityDescriptorDacl(&sd, TRUE, acl, FALSE);
        SetSecurityDescriptorControl(&sd, SE_DACL_PROTECTED, SE_DACL_PROTECTED);
        LONG st;
        ORACLE_CALL(st, RegSetKeySecurity(k, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, &sd));
        oracle_case("set_protected_dacl");
        o_status("ret", st);
        o_lasterr();
        oracle_case_end();
        get_case("get_after_set_dacl", k, DACL_SECURITY_INFORMATION);

        HKEY sub = NULL;
        st = RegCreateKeyExW(k, L"child", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &sub, NULL);
        oracle_case("create_child_after_dacl");
        o_status("ret", st);
        oracle_case_end();
        if (sub) {
            get_case("child_inherited_dacl", sub, DACL_SECURITY_INFORMATION);
            RegCloseKey(sub);
        }

        /* Empty DACL: nobody gets access except through ownership. */
        SECURITY_DESCRIPTOR sd2;
        BYTE empty[64];
        InitializeAcl((PACL)empty, sizeof empty, ACL_REVISION);
        InitializeSecurityDescriptor(&sd2, SECURITY_DESCRIPTOR_REVISION);
        SetSecurityDescriptorDacl(&sd2, TRUE, (PACL)empty, FALSE);
        HKEY e = NULL;
        RegCreateKeyExW(root, L"emptydacl", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &e, NULL);
        ORACLE_CALL(st, RegSetKeySecurity(e, DACL_SECURITY_INFORMATION, &sd2));
        oracle_case("set_empty_dacl");
        o_status("ret", st);
        oracle_case_end();
        HKEY h = NULL;
        oracle_case("open_empty_dacl_read");
        o_status("ret", RegOpenKeyExW(root, L"emptydacl", 0, KEY_READ, &h));
        oracle_case_end();
        if (h) { RegCloseKey(h); h = NULL; }
        oracle_case("open_empty_dacl_read_control");
        o_status("ret", RegOpenKeyExW(root, L"emptydacl", 0, READ_CONTROL, &h));
        oracle_case_end();
        if (h) { RegCloseKey(h); h = NULL; }
        oracle_case("open_empty_dacl_write_dac");
        o_status("ret", RegOpenKeyExW(root, L"emptydacl", 0, WRITE_DAC, &h));
        oracle_case_end();
        if (h) {   /* Owner restores access so the key can be deleted. */
            SECURITY_DESCRIPTOR sd3;
            InitializeSecurityDescriptor(&sd3, SECURITY_DESCRIPTOR_REVISION);
            SetSecurityDescriptorDacl(&sd3, TRUE, NULL, FALSE);
            oracle_case("restore_null_dacl");
            o_status("ret", RegSetKeySecurity(h, DACL_SECURITY_INFORMATION, &sd3));
            oracle_case_end();
            get_case("get_null_dacl", h, DACL_SECURITY_INFORMATION);
            RegCloseKey(h);
        }
        {   /* Whatever happened above: the original handle still has WRITE_DAC. */
            SECURITY_DESCRIPTOR sd4;
            InitializeSecurityDescriptor(&sd4, SECURITY_DESCRIPTOR_REVISION);
            SetSecurityDescriptorDacl(&sd4, TRUE, NULL, FALSE);
            RegSetKeySecurity(e, DACL_SECURITY_INFORMATION, &sd4);
        }
        RegCloseKey(e);
    }

    {   /* Errors. */
        LONG st;
        SECURITY_DESCRIPTOR sd;
        InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
        ORACLE_CALL(st, RegSetKeySecurity(k, DACL_SECURITY_INFORMATION, NULL));
        oracle_case("set_null_sd");
        o_status("ret", st);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(st, RegSetKeySecurity(k, 0, &sd));
        oracle_case("set_no_info");
        o_status("ret", st);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(st, RegSetKeySecurity(k, SACL_SECURITY_INFORMATION, &sd));
        oracle_case("set_sacl_without_access");
        o_status("ret", st);
        o_lasterr();
        oracle_case_end();
        HKEY ro = NULL;
        RegOpenKeyExW(root, L"k", 0, KEY_QUERY_VALUE, &ro);
        BYTE buf[1024];
        DWORD size = sizeof buf;
        ORACLE_CALL(st, RegGetKeySecurity(ro, DACL_SECURITY_INFORMATION, buf, &size));
        oracle_case("get_without_read_control");
        o_status("ret", st);
        o_lasterr();
        oracle_case_end();
        SetSecurityDescriptorDacl(&sd, TRUE, NULL, FALSE);
        ORACLE_CALL(st, RegSetKeySecurity(ro, DACL_SECURITY_INFORMATION, &sd));
        oracle_case("set_without_write_dac");
        o_status("ret", st);
        o_lasterr();
        oracle_case_end();
        RegCloseKey(ro);
        size = sizeof buf;
        ORACLE_CALL(st, RegGetKeySecurity(HKEY_CURRENT_USER, OWNER_SECURITY_INFORMATION, buf, &size));
        oracle_case("get_predefined_hkcu_owner");
        o_status("ret", st);
        o_lasterr();
        oracle_case_end();
        size = sizeof buf;
        ORACLE_CALL(st, RegGetKeySecurity(NULL, OWNER_SECURITY_INFORMATION, buf, &size));
        oracle_case("get_null_key");
        o_status("ret", st);
        o_lasterr();
        oracle_case_end();
    }

    {   /* Make sure cleanup can delete everything. */
        SECURITY_DESCRIPTOR sd;
        HKEY c = NULL;
        InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
        SetSecurityDescriptorDacl(&sd, TRUE, NULL, FALSE);
        RegSetKeySecurity(k, DACL_SECURITY_INFORMATION, &sd);
        if (!RegOpenKeyExW(k, L"child", 0, WRITE_DAC, &c)) {
            RegSetKeySecurity(c, DACL_SECURITY_INFORMATION, &sd);
            RegCloseKey(c);
        }
    }
    RegCloseKey(k);
    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
