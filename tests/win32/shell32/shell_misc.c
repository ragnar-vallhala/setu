/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: shell32 helpers used by installers: CommandLineToArgvW
 * parsing (quotes, backslashes, the special rules for argv[0], empty and
 * whitespace-only input) and SHGetFolderPathW / SHGetSpecialFolderPathW /
 * SHGetKnownFolderPath error codes and flag handling.
 *
 * Folder paths are machine specific, so only the HRESULT, whether a path
 * came back, and whether it ends with the documented last component are
 * printed. CommandLineToArgvW("") returns the program's own path; only the
 * argument count is printed for it.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-commandlinetoargvw
 *       https://learn.microsoft.com/en-us/cpp/c-language/parsing-c-command-line-arguments
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/nf-shlobj_core-shgetfolderpathw
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/nf-shlobj_core-shgetspecialfolderpathw
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/nf-shlobj_core-shgetknownfolderpath
 *       https://learn.microsoft.com/en-us/windows/win32/shell/csidl
 *       https://learn.microsoft.com/en-us/windows/win32/shell/knownfolderid
 */
#include "oracle.h"
#include <shlobj.h>

static LPWSTR *(WINAPI *pCommandLineToArgvW)(LPCWSTR, int *);
static HRESULT(WINAPI *pSHGetFolderPathW)(HWND, int, HANDLE, DWORD, LPWSTR);
static BOOL(WINAPI *pSHGetSpecialFolderPathW)(HWND, LPWSTR, int, BOOL);
static HRESULT(WINAPI *pSHGetKnownFolderPath)(const GUID *, DWORD, HANDLE, PWSTR *);
static void(WINAPI *pCoTaskMemFree)(void *);

#define LOAD(m, f) (*(FARPROC *)&p##f = GetProcAddress(m, #f))

static void argv_case(const char *name, const wchar_t *cmd, int skip_argv0)
{
    int argc = -7;
    LPWSTR *argv;
    ORACLE_CALL(argv, pCommandLineToArgvW(cmd, &argc));
    oracle_case(name);
    o_bool("ret_nonnull", argv != NULL);
    o_lasterr();
    o_i64("argc", argc);
    if (argv) {
        for (int i = skip_argv0 ? 1 : 0; i < argc && i < 12; i++) {
            char k[16];
            snprintf(k, sizeof k, "argv%d", i);
            o_wstr(k, argv[i]);
        }
        LocalFree(argv);
    }
    oracle_case_end();
}

static int ends_with_ci(const wchar_t *s, const wchar_t *tail)
{
    size_t a = wcslen(s), b = wcslen(tail);
    return a >= b && !_wcsicmp(s + a - b, tail);
}

static void folder_case(const char *name, int csidl, DWORD flags, const wchar_t *tail)
{
    wchar_t buf[MAX_PATH + 8];
    HRESULT hr;
    wmemset(buf, L'#', MAX_PATH + 8);
    buf[MAX_PATH + 7] = 0;
    ORACLE_CALL(hr, pSHGetFolderPathW(NULL, csidl, NULL, flags, buf));
    oracle_case(name);
    o_hex32("hr", (DWORD)hr);
    buf[MAX_PATH + 7] = 0;
    o_bool("buf_empty", buf[0] == 0);
    o_bool("buf_untouched", buf[0] == L'#');
    o_bool("absolute", buf[0] && buf[1] == L':' && buf[2] == L'\\');
    if (tail) o_bool("ends_with_expected", SUCCEEDED(hr) && ends_with_ci(buf, tail));
    oracle_case_end();
}

static void known_case(const char *name, const GUID *id, DWORD flags)
{
    PWSTR p = (PWSTR)(ULONG_PTR)1;
    HRESULT hr = pSHGetKnownFolderPath(id, flags, NULL, &p);
    oracle_case(name);
    o_hex32("hr", (DWORD)hr);
    o_bool("out_null", p == NULL);
    o_bool("absolute", SUCCEEDED(hr) && p && p[0] && p[1] == L':');
    oracle_case_end();
    if (SUCCEEDED(hr) && p && p != (PWSTR)(ULONG_PTR)1) pCoTaskMemFree(p);
}

int main(void)
{
    oracle_begin("shell32/shell_misc");
    HMODULE m;
    ORACLE_CALL(m, LoadLibraryW(L"shell32.dll"));
    oracle_case("load_shell32");
    o_handle("ret", m);
    oracle_case_end();
    if (!m) return oracle_end();
    HMODULE ole = LoadLibraryW(L"ole32.dll");
    LOAD(m, CommandLineToArgvW);
    LOAD(m, SHGetFolderPathW);
    LOAD(m, SHGetSpecialFolderPathW);
    LOAD(m, SHGetKnownFolderPath);
    LOAD(ole, CoTaskMemFree);

    /* ---- CommandLineToArgvW ---- */
    argv_case("argv_simple", L"prog a b c", 0);
    argv_case("argv_multiple_spaces_tabs", L"prog  a\t\tb   c", 0);
    argv_case("argv_quoted_arg", L"prog \"a b\" c", 0);
    argv_case("argv_quoted_program", L"\"C:\\Program Files\\p.exe\" x", 0);
    argv_case("argv_program_backslash_quote", L"C:\\dir\\\"p q\" x", 0);
    argv_case("argv_program_not_unquoted_midword", L"pro\"g a\" b", 0);
    argv_case("argv_escaped_quote", L"prog a\\\"b", 0);
    argv_case("argv_two_backslashes_quote", L"prog a\\\\\"b c\"", 0);
    argv_case("argv_three_backslashes_quote", L"prog a\\\\\\\"b", 0);
    argv_case("argv_backslashes_no_quote", L"prog a\\\\b\\c", 0);
    argv_case("argv_trailing_backslash", L"prog \"a\\\\\" b", 0);
    argv_case("argv_double_quote_inside_quotes", L"prog \"a\"\"b\" c", 0);
    argv_case("argv_triple_quotes", L"prog \"\"\"a\"\"\" b", 0);
    argv_case("argv_empty_quotes", L"prog \"\" b", 0);
    argv_case("argv_unterminated_quote", L"prog \"a b", 0);
    argv_case("argv_quote_mid_word", L"prog ab\"c d\"ef g", 0);
    argv_case("argv_trailing_spaces", L"prog a   ", 0);
    argv_case("argv_leading_space", L" prog a", 0);
    argv_case("argv_leading_tab", L"\tprog a", 0);
    argv_case("argv_program_only", L"prog", 0);
    argv_case("argv_empty_program_quotes", L"\"\" a", 0);
    argv_case("argv_newline_not_separator", L"prog a\nb", 0);
    argv_case("argv_unicode", L"prog \x00e9t\x00e9 \x20ac", 0);
    argv_case("argv_empty_string", L"", 1);
    argv_case("argv_only_spaces", L"   ", 0);
    {
        LPWSTR *argv;
        ORACLE_CALL(argv, pCommandLineToArgvW(L"a b", NULL));
        oracle_case("argv_null_argc");
        o_bool("ret_nonnull", argv != NULL);
        o_lasterr();
        oracle_case_end();
        if (argv) LocalFree(argv);
    }

    /* ---- SHGetFolderPathW ---- */
    folder_case("folder_windows", CSIDL_WINDOWS, SHGFP_TYPE_CURRENT, NULL);
    folder_case("folder_system", CSIDL_SYSTEM, SHGFP_TYPE_CURRENT, L"\\system32");
    folder_case("folder_program_files", CSIDL_PROGRAM_FILES, SHGFP_TYPE_CURRENT, L"\\Program Files");
    folder_case("folder_common_appdata", CSIDL_COMMON_APPDATA, SHGFP_TYPE_CURRENT, L"\\ProgramData");
    folder_case("folder_appdata", CSIDL_APPDATA, SHGFP_TYPE_CURRENT, L"\\Roaming");
    folder_case("folder_local_appdata", CSIDL_LOCAL_APPDATA, SHGFP_TYPE_CURRENT, L"\\Local");
    folder_case("folder_appdata_default", CSIDL_APPDATA, SHGFP_TYPE_DEFAULT, L"\\Roaming");
    folder_case("folder_fonts", CSIDL_FONTS, SHGFP_TYPE_CURRENT, L"\\Fonts");
    folder_case("folder_appdata_dont_verify", CSIDL_APPDATA | CSIDL_FLAG_DONT_VERIFY, SHGFP_TYPE_CURRENT, L"\\Roaming");
    folder_case("folder_bogus_csidl_0xff", 0xff, SHGFP_TYPE_CURRENT, NULL);
    folder_case("folder_bogus_csidl_0x7f", 0x7f, SHGFP_TYPE_CURRENT, NULL);
    folder_case("folder_desktop_virtual_0", CSIDL_DESKTOP, SHGFP_TYPE_CURRENT, L"\\Desktop");
    folder_case("folder_internet_virtual", CSIDL_INTERNET, SHGFP_TYPE_CURRENT, NULL);
    folder_case("folder_drives_virtual", CSIDL_DRIVES, SHGFP_TYPE_CURRENT, NULL);
    folder_case("folder_bad_flags", CSIDL_WINDOWS, 7, NULL);
    folder_case("folder_negative_csidl", -1, SHGFP_TYPE_CURRENT, NULL);
    /* probes: other flag values and other virtual (path-less) folders */
    folder_case("folder_flags_2", CSIDL_WINDOWS, 2, NULL);
    folder_case("folder_flags_high_bit", CSIDL_WINDOWS, 0x80000000u, NULL);
    folder_case("folder_appdata_bad_flags", CSIDL_APPDATA, 7, L"\\Roaming");
    folder_case("folder_controls_virtual", CSIDL_CONTROLS, SHGFP_TYPE_CURRENT, NULL);
    folder_case("folder_printers_virtual", CSIDL_PRINTERS, SHGFP_TYPE_CURRENT, NULL);
    folder_case("folder_bitbucket_virtual", CSIDL_BITBUCKET, SHGFP_TYPE_CURRENT, NULL);
    folder_case("folder_network_virtual", CSIDL_NETWORK, SHGFP_TYPE_CURRENT, NULL);
    folder_case("folder_connections_virtual", CSIDL_CONNECTIONS, SHGFP_TYPE_CURRENT, NULL);
    folder_case("folder_bogus_csidl_0x3c", 0x3c, SHGFP_TYPE_CURRENT, NULL);

    /* ---- SHGetSpecialFolderPathW ---- */
    {
        wchar_t buf[MAX_PATH];
        BOOL r;
        ORACLE_CALL(r, pSHGetSpecialFolderPathW(NULL, buf, CSIDL_SYSTEM, FALSE));
        oracle_case("special_system");
        o_bool("ret", r);
        o_bool("ends_with_system32", r && ends_with_ci(buf, L"\\system32"));
        oracle_case_end();
        ORACLE_CALL(r, pSHGetSpecialFolderPathW(NULL, buf, 0xff, FALSE));
        oracle_case("special_bogus");
        o_bool("ret", r);
        oracle_case_end();
    }

    /* ---- SHGetKnownFolderPath ---- */
    {
        static const GUID FOLDERID_Windows_ = {0xF38BF404, 0x1D43, 0x42F2, {0x93, 0x05, 0x67, 0xDE, 0x0B, 0x28, 0xFC, 0x23}};
        static const GUID FOLDERID_RoamingAppData_ = {0x3EB685DB, 0x65F9, 0x4CF6, {0xA0, 0x3A, 0xE3, 0xEF, 0x65, 0x72, 0x9F, 0x3D}};
        static const GUID FOLDERID_ComputerFolder_ = {0x0AC0837C, 0xBBF8, 0x452A, {0x85, 0x0D, 0x79, 0xD0, 0x8E, 0x66, 0x7C, 0xA7}};
        static const GUID bogus = {0x5e7a0002, 0x1111, 0x2222, {0x33, 0x33, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44}};
        known_case("known_windows", &FOLDERID_Windows_, 0);
        known_case("known_roaming_appdata", &FOLDERID_RoamingAppData_, 0);
        known_case("known_virtual_computer", &FOLDERID_ComputerFolder_, 0);
        known_case("known_bogus_guid", &bogus, 0);
        known_case("known_windows_bogus_flags", &FOLDERID_Windows_, 0x80000000u);
        static const GUID FOLDERID_RecycleBinFolder_ = {0xB7534046, 0x3ECB, 0x4C18, {0xBE, 0x4E, 0x64, 0xCD, 0x4C, 0xB7, 0xD6, 0xAC}};
        static const GUID FOLDERID_NetworkFolder_ = {0xD20BEEC4, 0x5CA8, 0x4905, {0xAE, 0x3B, 0xBF, 0x25, 0x1E, 0xA0, 0x9B, 0x53}};
        static const GUID FOLDERID_ControlPanelFolder_ = {0x82A74AEB, 0xAEB4, 0x465C, {0xA0, 0x14, 0xD0, 0x97, 0xEE, 0x34, 0x6D, 0x63}};
        known_case("known_virtual_recycle_bin", &FOLDERID_RecycleBinFolder_, 0);
        known_case("known_virtual_network", &FOLDERID_NetworkFolder_, 0);
        known_case("known_virtual_control_panel", &FOLDERID_ControlPanelFolder_, 0);
    }
    return oracle_end();
}
