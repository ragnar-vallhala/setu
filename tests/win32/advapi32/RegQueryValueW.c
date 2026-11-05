/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: the 16-bit-compatible RegQueryValueW, which reads the default
 * (unnamed) value of a subkey: NULL and "" subkeys, set and unset default
 * values, non-REG_SZ default values (REG_EXPAND_SZ, REG_DWORD, REG_BINARY),
 * size-only queries, too-small buffers, an unterminated string, missing
 * subkeys, and nested paths.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regqueryvaluew
 */
#include "oracle.h"

static HKEY root;

static void q_case(const char *cname, HKEY key, const wchar_t *sub, int with_buf, LONG bufsize)
{
    wchar_t buf[64];
    memset(buf, 0xCC, sizeof buf);
    LONG cb = bufsize;
    LONG st;
    ORACLE_CALL(st, RegQueryValueW(key, sub, with_buf ? buf : NULL, &cb));
    oracle_case(cname);
    o_status("ret", st);
    o_lasterr();
    o_i64("cb", cb);
    if (with_buf) o_bytes("buffer", buf, (size_t)bufsize + 2 < sizeof buf ? (size_t)bufsize + 2 : sizeof buf);
    oracle_case_end();
}

static void def(const wchar_t *sub, DWORD type, const void *data, DWORD size)
{
    HKEY k = NULL;
    RegCreateKeyExW(root, sub, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL);
    if (type != REG_NONE || size) RegSetValueExW(k, NULL, 0, type, data, size);
    RegCloseKey(k);
}

int main(void)
{
    oracle_begin("advapi32/RegQueryValueW");
    root = oracle_reg_root();
    def(L"sz", REG_SZ, L"hello", 12);
    def(L"unset", REG_NONE, NULL, 0);
    /* the test's own variables, so sizes do not depend on the machine */
    SetEnvironmentVariableW(L"SETU_ORACLE_X", L"xyz");
    SetEnvironmentVariableW(L"SETU_ORACLE_LONG", L"0123456789abcdefghij");
    def(L"exp", REG_EXPAND_SZ, L"%SETU_ORACLE_X%", 32);
    def(L"explong", REG_EXPAND_SZ, L"<%SETU_ORACLE_LONG%>", 42);
    def(L"dw", REG_DWORD, "\1\2\3\4", 4);
    def(L"bin", REG_BINARY, "\1\2\3", 3);
    def(L"nonul", REG_SZ, L"abc", 6);
    def(L"empty", REG_SZ, NULL, 0);
    def(L"sz\\nested", REG_SZ, L"deep", 10);
    RegSetValueExW(root, NULL, 0, REG_SZ, (const BYTE *)L"rootdef", 16);

    q_case("sz_big", root, L"sz", 1, 64);
    q_case("sz_size_only", root, L"sz", 0, 0);
    q_case("sz_exact", root, L"sz", 1, 12);
    q_case("sz_minus_2", root, L"sz", 1, 10);
    q_case("sz_zero_buffer", root, L"sz", 1, 0);
    q_case("unset_default", root, L"unset", 1, 64);
    q_case("expand_sz", root, L"exp", 1, 64);
    q_case("expand_long", root, L"explong", 1, 64);
    q_case("expand_long_small", root, L"explong", 1, 44);
    q_case("dword", root, L"dw", 1, 64);
    q_case("binary", root, L"bin", 1, 64);
    q_case("no_nul", root, L"nonul", 1, 64);
    q_case("no_nul_exact", root, L"nonul", 1, 6);
    q_case("empty", root, L"empty", 1, 64);
    q_case("nested", root, L"sz\\nested", 1, 64);
    q_case("missing_subkey", root, L"nope", 1, 64);
    q_case("null_subkey_own_default", root, NULL, 1, 64);
    q_case("empty_subkey_own_default", root, L"", 1, 64);
    q_case("subkey_other_case", root, L"SZ", 1, 64);
    q_case("invalid_handle", (HKEY)(ULONG_PTR)0x1234, L"sz", 1, 64);
    {
        LONG st;
        wchar_t b[8];
        ORACLE_CALL(st, RegQueryValueW(root, L"sz", b, NULL));
        oracle_case("buffer_without_size");
        o_status("ret", st);
        o_lasterr();
        oracle_case_end();
    }

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
