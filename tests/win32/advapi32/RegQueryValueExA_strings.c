/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: RegQueryValueExA converting string values (REG_SZ,
 * REG_EXPAND_SZ, REG_MULTI_SZ) stored as Unicode to ANSI: the sizes
 * reported by size-only queries and by successful reads, and the bytes.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regqueryvalueexa
 */
#include "oracle.h"

static HKEY root;

static void q_case(const char *cname, const wchar_t *wname, DWORD type, const void *data, DWORD size)
{
    RegSetValueExW(root, wname, 0, type, data, size);
    char name[32];
    WideCharToMultiByte(CP_ACP, 0, wname, -1, name, 32, NULL, NULL);
    DWORD t = 0xAAAAAAAAu, need = 0;
    LONG st;
    ORACLE_CALL(st, RegQueryValueExA(root, name, NULL, &t, NULL, &need));
    oracle_case(cname);
    o_status("sizeonly_ret", st);
    o_lasterr();
    o_u32("sizeonly_type", t);
    o_u32("sizeonly_size", need);
    unsigned char b[128];
    memset(b, 0xCC, sizeof b);
    DWORD sz = sizeof b;
    LONG q = RegQueryValueExA(root, name, NULL, &t, b, &sz);
    o_status("ret", q);
    o_u32("type", t);
    o_u32("size", sz);
    if (!q) o_bytes("data", b, sz + 2);
    /* Size-only query with a zero-size buffer pointer instead of NULL. */
    DWORD zero = 0;
    LONG z = RegQueryValueExA(root, name, NULL, NULL, b, &zero);
    o_status("zero_buf_ret", z);
    o_u32("zero_buf_size", zero);
    oracle_case_end();
}

int main(void)
{
    oracle_begin("advapi32/RegQueryValueExA_strings");
    root = oracle_reg_root();

    static const wchar_t hello[] = L"hello";
    static const wchar_t multi[] = L"ab\0cde\0";
    static const wchar_t emb[] = L"ab\0cd";

    q_case("sz", L"Sz", REG_SZ, hello, sizeof hello);
    q_case("sz_no_nul", L"SzNoNul", REG_SZ, hello, sizeof hello - 2);
    q_case("sz_two_nuls", L"SzTwo", REG_SZ, L"hello\0", sizeof L"hello\0");
    q_case("sz_embedded_nul", L"SzEmb", REG_SZ, emb, sizeof emb);
    q_case("sz_empty", L"SzEmpty", REG_SZ, NULL, 0);
    q_case("sz_only_nul", L"SzNul", REG_SZ, L"", 2);
    q_case("expand", L"Exp", REG_EXPAND_SZ, L"%PATH%", sizeof L"%PATH%");
    q_case("expand_no_nul", L"ExpNoNul", REG_EXPAND_SZ, hello, sizeof hello - 2);
    q_case("multi", L"Multi", REG_MULTI_SZ, multi, sizeof multi);
    q_case("multi_no_final", L"MultiNoFinal", REG_MULTI_SZ, multi, sizeof multi - 2);
    q_case("sz_e_acute", L"SzE", REG_SZ, L"caf\x00e9", sizeof L"caf\x00e9");
    q_case("sz_unmappable", L"SzCjk", REG_SZ, L"a\x4e2d" L"b", sizeof L"a\x4e2d" L"b");
    q_case("sz_euro", L"SzEuro", REG_SZ, L"\x20ac", sizeof L"\x20ac");
    q_case("sz_surrogate_pair", L"SzSur", REG_SZ, L"\xd83d\xde00", sizeof L"\xd83d\xde00");
    q_case("sz_lone_surrogate", L"SzLone", REG_SZ, L"x\xd83d", sizeof L"x\xd83d");
    {
        static wchar_t big[600];
        for (int i = 0; i < 599; i++) big[i] = L'a' + i % 26;
        big[599] = 0;
        RegSetValueExW(root, L"Big", 0, REG_SZ, (const BYTE *)big, sizeof big);
        DWORD need = 0, t = 0;
        LONG st = RegQueryValueExA(root, "Big", NULL, &t, NULL, &need);
        oracle_case("sz_long_size_only");
        o_status("ret", st);
        o_u32("size", need);
        static char b[2000];
        DWORD sz = sizeof b;
        st = RegQueryValueExA(root, "Big", NULL, &t, (BYTE *)b, &sz);
        o_status("read_ret", st);
        o_u32("read_size", sz);
        o_u32("read_strlen", (DWORD)strlen(b));
        oracle_case_end();
    }

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
