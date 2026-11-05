/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: the lpSecurityAttributes parameter of RegCreateKeyExW:
 * bInheritHandle (is the returned handle inheritable?), a security
 * descriptor applied to a new key (read back as SDDL DACL), ignored for an
 * existing key, a bad nLength, and the security descriptor size
 * RegQueryInfoKeyW reports compared with RegGetKeySecurity. Only DACLs made
 * of well-known SIDs are printed.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regcreatekeyexw
 *       https://learn.microsoft.com/en-us/windows/win32/api/wtypesbase/ns-wtypesbase-security_attributes
 *       https://learn.microsoft.com/en-us/windows/win32/api/handleapi/nf-handleapi-gethandleinformation
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-reggetkeysecurity
 *       https://learn.microsoft.com/en-us/windows/win32/secauthz/security-descriptor-string-format
 */
#include "oracle.h"
#include <sddl.h>

static HKEY root;

static void dacl_field(const char *field, HKEY k)
{
    BYTE sd[4096];
    DWORD len = sizeof sd;
    LONG st = RegGetKeySecurity(k, DACL_SECURITY_INFORMATION, sd, &len);
    if (st) {
        o_status(field, st);
        return;
    }
    wchar_t *s = NULL;
    ConvertSecurityDescriptorToStringSecurityDescriptorW(sd, SDDL_REVISION_1, DACL_SECURITY_INFORMATION, &s, NULL);
    o_wstr(field, s ? s : L"(none)");
    LocalFree(s);
}

static void create_case(const char *name, const wchar_t *sub, const wchar_t *sddl, BOOL inherit, DWORD nlength)
{
    SECURITY_ATTRIBUTES sa;
    PSECURITY_DESCRIPTOR sd = NULL;
    if (sddl) ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, SDDL_REVISION_1, &sd, NULL);
    sa.nLength = nlength;
    sa.lpSecurityDescriptor = sd;
    sa.bInheritHandle = inherit;
    HKEY k = NULL;
    DWORD disp = 0;
    LONG st;
    ORACLE_CALL(st, RegCreateKeyExW(root, sub, 0, NULL, 0, KEY_ALL_ACCESS, &sa, &k, &disp));
    oracle_case(name);
    o_status("ret", st);
    o_lasterr();
    if (!st) {
        DWORD flags = 0;
        o_u32("disposition", disp);
        o_bool("inheritable", GetHandleInformation(k, &flags) && (flags & HANDLE_FLAG_INHERIT));
        if (sddl) dacl_field("dacl", k);
        RegCloseKey(k);
    }
    oracle_case_end();
    LocalFree(sd);
}

int main(void)
{
    oracle_begin("advapi32/RegCreateKeyEx_security");
    root = oracle_reg_root();

    create_case("no_inherit", L"a", NULL, FALSE, sizeof(SECURITY_ATTRIBUTES));
    create_case("inherit_new", L"b", NULL, TRUE, sizeof(SECURITY_ATTRIBUTES));
    create_case("inherit_existing", L"a", NULL, TRUE, sizeof(SECURITY_ATTRIBUTES));
    create_case("sd_new", L"c", L"D:P(A;;KA;;;WD)(A;;KR;;;BU)", FALSE, sizeof(SECURITY_ATTRIBUTES));
    create_case("sd_existing_ignored", L"c", L"D:P(A;;KR;;;WD)", FALSE, sizeof(SECURITY_ATTRIBUTES));
    create_case("sd_null_dacl", L"d", L"D:NO_ACCESS_CONTROL", FALSE, sizeof(SECURITY_ATTRIBUTES));
    create_case("sd_inherit_flags_ace", L"e", L"D:P(A;CI;KA;;;WD)", FALSE, sizeof(SECURITY_ATTRIBUTES));
    create_case("nlength_zero", L"f", NULL, TRUE, 0);
    create_case("nlength_huge", L"g", NULL, TRUE, 0x1000);
    {   /* Child of e inherits its CI ACE. */
        HKEY k = NULL;
        RegCreateKeyExW(root, L"e\\child", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL);
        oracle_case("child_inherits_ci_ace");
        dacl_field("dacl", k);
        oracle_case_end();
        RegCloseKey(k);
    }
    {   /* Default (inherited) DACL of a plain test key: protected or not? */
        HKEY k = NULL;
        RegOpenKeyExW(root, L"a", 0, KEY_READ, &k);
        BYTE sd[4096];
        DWORD len = sizeof sd, info_len = 0;
        LONG g = RegGetKeySecurity(k, DACL_SECURITY_INFORMATION | OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION,
                                   sd, &len);
        LONG q = RegQueryInfoKeyW(k, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &info_len, NULL);
        SECURITY_DESCRIPTOR_CONTROL ctl = 0;
        DWORD rev = 0;
        GetSecurityDescriptorControl(sd, &ctl, &rev);
        oracle_case("default_sd");
        o_status("get", g);
        o_status("query_info", q);
        o_bool("info_len_nonzero", info_len > 0);
        o_bool("info_len_ge_get_len", info_len >= len);
        o_bool("self_relative", (ctl & SE_SELF_RELATIVE) != 0);
        o_bool("dacl_present", (ctl & SE_DACL_PRESENT) != 0);
        o_bool("dacl_protected", (ctl & SE_DACL_PROTECTED) != 0);
        o_bool("dacl_auto_inherited", (ctl & SE_DACL_AUTO_INHERITED) != 0);
        oracle_case_end();
        RegCloseKey(k);
    }
    {   /* Restore permissive DACLs so cleanup can delete everything. */
        PSECURITY_DESCRIPTOR sd = NULL;
        ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:(A;;KA;;;WD)", SDDL_REVISION_1, &sd, NULL);
        static const wchar_t *keys[] = {L"c", L"d", L"e", L"e\\child"};
        for (int i = 0; i < 4; i++) {
            HKEY k;
            if (!RegOpenKeyExW(root, keys[i], 0, WRITE_DAC, &k)) {
                RegSetKeySecurity(k, DACL_SECURITY_INFORMATION, sd);
                RegCloseKey(k);
            }
        }
        LocalFree(sd);
    }

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
