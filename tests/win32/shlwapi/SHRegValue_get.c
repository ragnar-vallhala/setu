/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: shlwapi's registry value helpers (loaded with LoadLibrary):
 * SHSetValueW (creates the subkey), SHGetValueW and SHQueryValueExW (which
 * expand REG_EXPAND_SZ and report REG_SZ), their size-only queries and
 * small buffers, unterminated strings, SHDeleteValueW, and SHRegGetValueW's
 * SRRF_* type filters.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/nf-shlwapi-shgetvaluew
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/nf-shlwapi-shqueryvalueexw
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/nf-shlwapi-shsetvaluew
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/nf-shlwapi-shdeletevaluew
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/nf-shlwapi-shreggetvaluew
 */
#include "oracle.h"

static HKEY root;
typedef LONG (WINAPI *shget_fn)(HKEY, const wchar_t *, const wchar_t *, DWORD *, void *, DWORD *);
typedef LONG (WINAPI *shset_fn)(HKEY, const wchar_t *, const wchar_t *, DWORD, const void *, DWORD);
typedef LONG (WINAPI *shdel_fn)(HKEY, const wchar_t *, const wchar_t *);
typedef LONG (WINAPI *shqv_fn)(HKEY, const wchar_t *, DWORD *, DWORD *, void *, DWORD *);
typedef LONG (WINAPI *shrgv_fn)(HKEY, const wchar_t *, const wchar_t *, int, DWORD *, void *, DWORD *);
static shget_fn shget;
static shqv_fn shqv;
static shrgv_fn shrgv;

static void get_case(const char *cname, const wchar_t *sub, const wchar_t *value, int with_buf, DWORD bufsize)
{
    unsigned char b[128];
    memset(b, 0xCC, sizeof b);
    DWORD t = 0xAAAAAAAAu, sz = bufsize;
    LONG st;
    ORACLE_CALL(st, shget(root, sub, value, &t, with_buf ? b : NULL, &sz));
    oracle_case(cname);
    o_status("ret", st);
    o_u32("type", t);
    o_u32("size", sz);
    if (with_buf) o_bytes("buffer", b, bufsize + 2 < sizeof b ? bufsize + 2 : sizeof b);
    oracle_case_end();
}

static void qv_case(const char *cname, const wchar_t *value, int with_buf, DWORD bufsize)
{
    unsigned char b[128];
    memset(b, 0xCC, sizeof b);
    DWORD t = 0xAAAAAAAAu, sz = bufsize;
    LONG st;
    ORACLE_CALL(st, shqv(root, value, NULL, &t, with_buf ? b : NULL, &sz));
    oracle_case(cname);
    o_status("ret", st);
    o_u32("type", t);
    o_u32("size", sz);
    if (with_buf) o_bytes("buffer", b, bufsize + 2 < sizeof b ? bufsize + 2 : sizeof b);
    oracle_case_end();
}

static void rgv_case(const char *cname, const wchar_t *value, int flags)
{
    unsigned char b[64];
    memset(b, 0xCC, sizeof b);
    DWORD t = 0xAAAAAAAAu, sz = sizeof b;
    LONG st;
    ORACLE_CALL(st, shrgv(root, NULL, value, flags, &t, b, &sz));
    oracle_case(cname);
    o_status("ret", st);
    o_u32("type", t);
    o_u32("size", sz);
    if (!st) o_bytes("data", b, sz);
    oracle_case_end();
}

int main(void)
{
    oracle_begin("shlwapi/SHRegValue_get");
    root = oracle_reg_root();
    HMODULE m = LoadLibraryW(L"shlwapi.dll");
    shget = (shget_fn)(void *)GetProcAddress(m, "SHGetValueW");
    shset_fn shset = (shset_fn)(void *)GetProcAddress(m, "SHSetValueW");
    shdel_fn shdel = (shdel_fn)(void *)GetProcAddress(m, "SHDeleteValueW");
    shqv = (shqv_fn)(void *)GetProcAddress(m, "SHQueryValueExW");
    shrgv = (shrgv_fn)(void *)GetProcAddress(m, "SHRegGetValueW");
    oracle_case("exports");
    o_bool("all", shget && shset && shdel && shqv && shrgv);
    oracle_case_end();
    if (!(shget && shset && shdel && shqv && shrgv)) { oracle_reg_cleanup(); return oracle_end(); }

    SetEnvironmentVariableW(L"SETU_ORACLE_SH", L"0123456789");
    LONG st;
    ORACLE_CALL(st, shset(root, L"new\\sub", L"Sz", REG_SZ, L"hello", 12));
    oracle_case("set_creates_subkey");
    o_status("ret", st);
    oracle_case_end();
    shset(root, NULL, L"Exp", REG_EXPAND_SZ, L"<%SETU_ORACLE_SH%>", 38);
    shset(root, NULL, L"ExpShrink", REG_EXPAND_SZ, L"%SETU_ORACLE_NONE_X%", 42);
    shset(root, NULL, L"NoNul", REG_SZ, L"abc", 6);
    shset(root, NULL, L"Dw", REG_DWORD, "\1\2\3\4", 4);
    shset(root, NULL, L"Sz", REG_SZ, L"top", 8);

    get_case("get_subkey_sz", L"new\\sub", L"Sz", 1, 128);
    get_case("get_null_subkey", NULL, L"Sz", 1, 128);
    get_case("get_expand_big", NULL, L"Exp", 1, 128);
    get_case("get_expand_size_only", NULL, L"Exp", 0, 0);
    get_case("get_expand_raw_size", NULL, L"Exp", 1, 38);
    get_case("get_expand_small", NULL, L"Exp", 1, 8);
    get_case("get_expand_undefined_var", NULL, L"ExpShrink", 1, 128);
    get_case("get_no_nul", NULL, L"NoNul", 1, 128);
    get_case("get_no_nul_exact", NULL, L"NoNul", 1, 6);
    get_case("get_dword", NULL, L"Dw", 1, 128);
    get_case("get_missing", NULL, L"Missing", 1, 128);
    get_case("get_missing_subkey", L"nope", L"Sz", 1, 128);
    qv_case("query_expand_big", L"Exp", 1, 128);
    qv_case("query_expand_size_only", L"Exp", 0, 0);
    qv_case("query_expand_small", L"Exp", 1, 8);
    qv_case("query_no_nul", L"NoNul", 1, 128);
    qv_case("query_sz_small", L"Sz", 1, 4);
    rgv_case("rgv_sz_as_sz", L"Sz", 0x00000002 /* SRRF_RT_REG_SZ */);
    rgv_case("rgv_sz_as_dword", L"Sz", 0x00000010 /* SRRF_RT_REG_DWORD */);
    rgv_case("rgv_expand_as_sz", L"Exp", 0x00000002);
    rgv_case("rgv_expand_noexpand", L"Exp", 0x00000004 | 0x10000000 /* SRRF_RT_REG_EXPAND_SZ|SRRF_NOEXPAND */);
    rgv_case("rgv_dword_any", L"Dw", 0x0000ffff);
    rgv_case("rgv_missing_any", L"Missing", 0x0000ffff);

    ORACLE_CALL(st, shdel(root, L"new\\sub", L"Sz"));
    oracle_case("delete_value_in_subkey");
    o_status("ret", st);
    oracle_case_end();
    ORACLE_CALL(st, shdel(root, L"new\\sub", L"Sz"));
    oracle_case("delete_value_again");
    o_status("ret", st);
    oracle_case_end();
    ORACLE_CALL(st, shdel(root, L"nope", L"Sz"));
    oracle_case("delete_missing_subkey");
    o_status("ret", st);
    oracle_case_end();

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
