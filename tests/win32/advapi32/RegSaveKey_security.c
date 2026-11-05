/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: key security across a RegSaveKey / RegRestoreKey round trip.
 * A saved tree has a subkey with a protected DACL (Everyone: read; the
 * current user: full) and a subkey with an inherited DACL. After restoring
 * into a fresh key, the test prints whether each restored key's DACL is
 * protected, how many ACEs it has and which are inherited, and the access
 * Everyone and the user get, as SDDL-free summaries. The restore target's
 * own DACL is printed before and after. Hive bytes are never examined.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regsavekeyw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regrestorekeyw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regsetkeysecurity
 */
#include "oracle.h"
#include <aclapi.h>

static HKEY root;
static PSID user_sid, everyone_sid;

static void set_privilege(const wchar_t *name)
{
    HANDLE tok;
    TOKEN_PRIVILEGES tp = {1, {{{0, 0}, SE_PRIVILEGE_ENABLED}}};
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &tok)) return;
    LookupPrivilegeValueW(NULL, name, &tp.Privileges[0].Luid);
    AdjustTokenPrivileges(tok, FALSE, &tp, 0, NULL, NULL);
    CloseHandle(tok);
}

static HKEY mk(HKEY parent, const wchar_t *name)
{
    HKEY k = NULL;
    RegCreateKeyExW(parent, name, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL);
    return k;
}

/* Prints a machine-independent summary of the key's DACL. */
static void dacl_case(const char *name, const wchar_t *rel)
{
    HKEY k = NULL;
    LONG st = RegOpenKeyExW(root, rel, 0, READ_CONTROL, &k);
    oracle_case(name);
    o_status("open", st);
    if (!st) {
        static BYTE buf[4096];
        DWORD size = sizeof buf;
        st = RegGetKeySecurity(k, DACL_SECURITY_INFORMATION, buf, &size);
        o_status("get", st);
        if (!st) {
            SECURITY_DESCRIPTOR_CONTROL ctl = 0;
            DWORD rev;
            BOOL present = FALSE, def = FALSE;
            PACL acl = NULL;
            GetSecurityDescriptorControl(buf, &ctl, &rev);
            GetSecurityDescriptorDacl(buf, &present, &acl, &def);
            o_bool("protected", (ctl & SE_DACL_PROTECTED) != 0);
            o_bool("present", present);
            if (present && acl) {
                DWORD inherited = 0, everyone = 0, user = 0;
                for (DWORD i = 0; i < acl->AceCount; i++) {
                    ACCESS_ALLOWED_ACE *ace;
                    if (!GetAce(acl, i, (void **)&ace)) continue;
                    if (ace->Header.AceFlags & INHERITED_ACE) inherited++;
                    if (ace->Header.AceType != ACCESS_ALLOWED_ACE_TYPE) continue;
                    if (EqualSid((PSID)&ace->SidStart, everyone_sid)) everyone |= ace->Mask;
                    if (EqualSid((PSID)&ace->SidStart, user_sid)) user |= ace->Mask;
                }
                o_u32("aces", acl->AceCount);
                o_u32("inherited_aces", inherited);
                o_hex32("everyone_mask", everyone);
                o_hex32("user_mask", user);
            }
        }
        RegCloseKey(k);
    }
    oracle_case_end();
}

int main(void)
{
    oracle_begin("advapi32/RegSaveKey_security");
    root = oracle_reg_root();
    set_privilege(L"SeBackupPrivilege");
    set_privilege(L"SeRestorePrivilege");
    {
        static BYTE tu[256];
        HANDLE tok;
        DWORD n;
        OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok);
        GetTokenInformation(tok, TokenUser, tu, sizeof tu, &n);
        CloseHandle(tok);
        user_sid = ((TOKEN_USER *)tu)->User.Sid;
        static BYTE ev[SECURITY_MAX_SID_SIZE];
        DWORD evn = sizeof ev;
        CreateWellKnownSid(WinWorldSid, NULL, ev, &evn);
        everyone_sid = ev;
    }

    HKEY src = mk(root, L"src"), prot = mk(src, L"protected"), inh = mk(src, L"inherited");
    {
        EXPLICIT_ACCESSW ea[2] = {0};
        PACL acl = NULL;
        SECURITY_DESCRIPTOR sd;
        ea[0].grfAccessPermissions = KEY_READ;
        ea[0].grfAccessMode = SET_ACCESS;
        ea[0].Trustee.TrusteeForm = TRUSTEE_IS_SID;
        ea[0].Trustee.ptstrName = (LPWSTR)everyone_sid;
        ea[1].grfAccessPermissions = KEY_ALL_ACCESS;
        ea[1].grfAccessMode = SET_ACCESS;
        ea[1].Trustee.TrusteeForm = TRUSTEE_IS_SID;
        ea[1].Trustee.ptstrName = (LPWSTR)user_sid;
        SetEntriesInAclW(2, ea, NULL, &acl);
        InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
        SetSecurityDescriptorDacl(&sd, TRUE, acl, FALSE);
        SetSecurityDescriptorControl(&sd, SE_DACL_PROTECTED, SE_DACL_PROTECTED);
        oracle_case("set_protected_dacl");
        o_status("ret", RegSetKeySecurity(prot, DACL_SECURITY_INFORMATION, &sd));
        oracle_case_end();
        LocalFree(acl);
    }
    RegCloseKey(prot);
    RegCloseKey(inh);

    dacl_case("source_protected", L"src\\protected");
    dacl_case("source_inherited", L"src\\inherited");

    DeleteFileW(L"sec.hiv");
    oracle_case("save");
    o_status("ret", RegSaveKeyW(src, L"sec.hiv", NULL));
    oracle_case_end();
    RegCloseKey(src);

    HKEY dst = mk(root, L"dst");
    dacl_case("target_before_restore", L"dst");
    oracle_case("restore");
    o_status("ret", RegRestoreKeyW(dst, L"sec.hiv", 0));
    oracle_case_end();
    RegCloseKey(dst);
    dacl_case("target_after_restore", L"dst");
    dacl_case("restored_protected", L"dst\\protected");
    dacl_case("restored_inherited", L"dst\\inherited");

    DeleteFileW(L"sec.hiv");
    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
