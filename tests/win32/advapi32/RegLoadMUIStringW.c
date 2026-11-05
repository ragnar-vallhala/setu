/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: RegLoadMUIStringW (found with GetProcAddress) on values that
 * are not indirect strings (plain REG_SZ, REG_EXPAND_SZ, REG_DWORD, empty,
 * missing), indirect strings naming a DLL that does not exist or that are
 * malformed, buffer sizes (too small, odd, zero, NULL with pcbData),
 * REG_MUI_STRING_TRUNCATE, invalid flags, and RegLoadMUIStringA.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regloadmuistringw
 *       https://learn.microsoft.com/en-us/windows/win32/intl/using-registry-string-redirection
 */
#include "oracle.h"

static HKEY root;

typedef LONG (WINAPI *muiw_fn)(HKEY, const wchar_t *, wchar_t *, DWORD, DWORD *, DWORD, const wchar_t *);
typedef LONG (WINAPI *muia_fn)(HKEY, const char *, char *, DWORD, DWORD *, DWORD, const char *);
static muiw_fn muiw;

static void l_case(const char *cname, const wchar_t *value, int with_buf, DWORD bufsize, int with_cb, DWORD flags)
{
    wchar_t buf[64];
    memset(buf, 0xCC, sizeof buf);
    DWORD cb = 0xAAAAAAAAu;
    LONG st;
    ORACLE_CALL(st, muiw(root, value, with_buf ? buf : NULL, bufsize, with_cb ? &cb : NULL, flags, NULL));
    oracle_case(cname);
    o_status("ret", st);
    o_lasterr();
    if (with_cb) o_u32("cb", cb);
    if (with_buf) o_bytes("buffer", buf, bufsize + 2 < sizeof buf ? bufsize + 2 : sizeof buf);
    oracle_case_end();
}

static void set(const wchar_t *name, DWORD type, const wchar_t *s)
{
    RegSetValueExW(root, name, 0, type, (const BYTE *)s, (DWORD)((wcslen(s) + 1) * sizeof(wchar_t)));
}

int main(void)
{
    oracle_begin("advapi32/RegLoadMUIStringW");
    root = oracle_reg_root();
    HMODULE adv = GetModuleHandleW(L"advapi32.dll");
    muiw = (muiw_fn)(void *)GetProcAddress(adv, "RegLoadMUIStringW");
    muia_fn muia = (muia_fn)(void *)GetProcAddress(adv, "RegLoadMUIStringA");
    oracle_case("exports");
    o_bool("w", muiw != NULL);
    o_bool("a", muia != NULL);
    oracle_case_end();
    if (!muiw) { oracle_reg_cleanup(); return oracle_end(); }

    SetEnvironmentVariableW(L"SETU_ORACLE_M", L"mm");
    set(L"Plain", REG_SZ, L"plain text");
    set(L"Exp", REG_EXPAND_SZ, L"%SETU_ORACLE_M%");
    set(L"NoDll", REG_SZ, L"@setu_no_such_module.dll,-100");
    set(L"NoDllExp", REG_EXPAND_SZ, L"@%SETU_ORACLE_M%\\setu_none.dll,-1");
    set(L"BadNoComma", REG_SZ, L"@justtext");
    set(L"BadNoMinus", REG_SZ, L"@setu_no_such_module.dll,100");
    set(L"AtOnly", REG_SZ, L"@");
    set(L"Multi", REG_MULTI_SZ, L"a");
    RegSetValueExW(root, L"Dw", 0, REG_DWORD, (const BYTE *)"\1\0\0\0", 4);
    RegSetValueExW(root, L"Empty", 0, REG_SZ, NULL, 0);
    RegSetValueExW(root, L"NoNul", 0, REG_SZ, (const BYTE *)L"abc", 6);

    l_case("plain_big", L"Plain", 1, 128, 1, 0);
    l_case("plain_exact", L"Plain", 1, 22, 1, 0);
    l_case("plain_small", L"Plain", 1, 10, 1, 0);
    l_case("plain_small_truncate", L"Plain", 1, 10, 1, REG_MUI_STRING_TRUNCATE);
    l_case("plain_odd_size", L"Plain", 1, 21, 1, 0);
    l_case("plain_zero_buffer", L"Plain", 1, 0, 1, 0);
    l_case("plain_null_buffer", L"Plain", 0, 0, 1, 0);
    l_case("plain_no_cb", L"Plain", 1, 128, 0, 0);
    l_case("plain_truncate_no_cb", L"Plain", 1, 10, 0, REG_MUI_STRING_TRUNCATE);
    l_case("plain_flags_2", L"Plain", 1, 128, 1, 2);
    l_case("expand_sz", L"Exp", 1, 128, 1, 0);
    l_case("no_such_dll", L"NoDll", 1, 128, 1, 0);
    l_case("no_such_dll_expand", L"NoDllExp", 1, 128, 1, 0);
    l_case("bad_no_comma", L"BadNoComma", 1, 128, 1, 0);
    l_case("bad_no_minus", L"BadNoMinus", 1, 128, 1, 0);
    l_case("at_only", L"AtOnly", 1, 128, 1, 0);
    l_case("multi_sz", L"Multi", 1, 128, 1, 0);
    l_case("dword", L"Dw", 1, 128, 1, 0);
    l_case("empty", L"Empty", 1, 128, 1, 0);
    l_case("no_nul", L"NoNul", 1, 128, 1, 0);
    l_case("missing", L"Missing", 1, 128, 1, 0);
    l_case("default_unset", NULL, 1, 128, 1, 0);

    if (muia) {
        char b[32];
        DWORD cb = 0xAAAAAAAAu;
        LONG st;
        ORACLE_CALL(st, muia(root, "Plain", b, sizeof b, &cb, 0, NULL));
        oracle_case("ansi_plain");
        o_status("ret", st);
        o_lasterr();
        o_u32("cb", cb);
        oracle_case_end();
    }

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
