/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: "reg add /f" over existing values and keys: replacing a value
 * with one of another type, replacing the default value (/ve) with each type,
 * a key whose name differs only in case, a key that is a volatile key created
 * by the API, and adding under a key the user may not write (a key the test
 * gives a read-only DACL). Each case prints reg.exe's exit code and output and
 * the resulting tree.
 *
 * Docs: https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/reg-add
 */
#include "regtool.h"
#include <aclapi.h>

#define K L"HKCU\\Software\\SetuOracle\\reg_add_overwrite"
#define SUB L"Software\\SetuOracle\\reg_add_overwrite"

static void tc(const char *name, const wchar_t *cmd, const wchar_t *rel)
{
    wchar_t sub[256];
    rt_run(cmd);
    oracle_case(name);
    o_u32("exit", rt_exit);
    o_str("out", rt_out);
    o_str("err", rt_err);
    swprintf(sub, 256, L"%ls\\%ls", SUB, rel);
    o_regtree("tree", HKEY_CURRENT_USER, sub);
    oracle_case_end();
}

int main(void)
{
    oracle_begin("reg/add_overwrite");
    HKEY root = oracle_reg_root(), k;

    tc("sz_initial", L"reg.exe add " K L"\\t /v a /d text /f", L"t");
    tc("sz_to_dword", L"reg.exe add " K L"\\t /v a /t REG_DWORD /d 5 /f", L"t");
    tc("dword_to_binary", L"reg.exe add " K L"\\t /v a /t REG_BINARY /d 0102 /f", L"t");
    tc("binary_to_multi", L"reg.exe add " K L"\\t /v a /t REG_MULTI_SZ /d x\\0y /f", L"t");
    tc("multi_to_expand", L"reg.exe add " K L"\\t /v a /t REG_EXPAND_SZ /d %TEMP% /f", L"t");
    tc("expand_to_none", L"reg.exe add " K L"\\t /v a /t REG_NONE /f", L"t");
    tc("default_sz", L"reg.exe add " K L"\\t /ve /d def /f", L"t");
    tc("default_dword", L"reg.exe add " K L"\\t /ve /t REG_DWORD /d 1 /f", L"t");
    tc("default_qword", L"reg.exe add " K L"\\t /ve /t REG_QWORD /d 0x10 /f", L"t");
    tc("default_no_data", L"reg.exe add " K L"\\t /ve /f", L"t");
    tc("value_other_case", L"reg.exe add " K L"\\t /v A /d upper /f", L"t");
    tc("key_other_case", L"reg.exe add " K L"\\T /v b /d via_upper /f", L"t");

    RegCreateKeyExW(root, L"vol", 0, NULL, REG_OPTION_VOLATILE, KEY_ALL_ACCESS, NULL, &k, NULL);
    RegCloseKey(k);
    tc("into_volatile_key", L"reg.exe add " K L"\\vol /v a /d x /f", L"vol");
    tc("subkey_of_volatile_key", L"reg.exe add " K L"\\vol\\child /f", L"vol");

    /* A key the user can read but not write. */
    RegCreateKeyExW(root, L"ro", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL);
    RegSetValueExW(k, L"a", 0, REG_SZ, (const BYTE *)L"orig", 10);
    {
        static BYTE tu[256];
        HANDLE tok;
        DWORD n;
        EXPLICIT_ACCESSW ea = {0};
        PACL acl = NULL;
        OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok);
        GetTokenInformation(tok, TokenUser, tu, sizeof tu, &n);
        CloseHandle(tok);
        ea.grfAccessPermissions = KEY_READ | WRITE_DAC | DELETE;
        ea.grfAccessMode = SET_ACCESS;
        ea.Trustee.TrusteeForm = TRUSTEE_IS_SID;
        ea.Trustee.ptstrName = (LPWSTR)((TOKEN_USER *)tu)->User.Sid;
        SetEntriesInAclW(1, &ea, NULL, &acl);
        SetSecurityInfo(k, SE_REGISTRY_KEY, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                        NULL, NULL, acl, NULL);
        LocalFree(acl);
    }
    tc("into_read_only_key", L"reg.exe add " K L"\\ro /v a /d new /f", L"ro");
    tc("subkey_of_read_only_key", L"reg.exe add " K L"\\ro\\child /f", L"ro");
    tc("delete_in_read_only_key", L"reg.exe delete " K L"\\ro /v a /f", L"ro");
    /* Give access back so the cleanup can delete the key. */
    SetSecurityInfo(k, SE_REGISTRY_KEY, DACL_SECURITY_INFORMATION | UNPROTECTED_DACL_SECURITY_INFORMATION,
                    NULL, NULL, NULL, NULL);
    RegCloseKey(k);

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
