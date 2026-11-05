/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: the 16-bit-compatible RegQueryValueA: the ANSI default value
 * of a subkey, sizes reported for size-only queries and small buffers,
 * unset defaults, non-string defaults, and missing subkeys.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regqueryvaluea
 */
#include "oracle.h"

static HKEY root;

static void q_case(const char *cname, const char *sub, int with_buf, LONG bufsize)
{
    char buf[64];
    memset(buf, 0xCC, sizeof buf);
    LONG cb = bufsize;
    LONG st;
    ORACLE_CALL(st, RegQueryValueA(root, sub, with_buf ? buf : NULL, &cb));
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
    if (size || type != REG_NONE) RegSetValueExW(k, NULL, 0, type, data, size);
    RegCloseKey(k);
}

int main(void)
{
    oracle_begin("advapi32/RegQueryValueA");
    root = oracle_reg_root();
    def(L"sz", REG_SZ, L"hello", 12);
    def(L"unset", REG_NONE, NULL, 0);
    def(L"dw", REG_DWORD, "\1\2\3\4", 4);
    /* the test's own variables, so sizes do not depend on the machine */
    SetEnvironmentVariableW(L"SETU_ORACLE_X", L"xyz");
    SetEnvironmentVariableW(L"SETU_ORACLE_LONG", L"0123456789abcdefghij");
    def(L"exp", REG_EXPAND_SZ, L"%SETU_ORACLE_X%", 32);
    def(L"explong", REG_EXPAND_SZ, L"<%SETU_ORACLE_LONG%>", 42);
    def(L"nonul", REG_SZ, L"abc", 6);
    def(L"hi", REG_SZ, L"caf\x00e9", 10);

    q_case("sz_big", "sz", 1, 64);
    q_case("sz_size_only", "sz", 0, 0);
    q_case("sz_ansi_exact", "sz", 1, 6);
    q_case("sz_5", "sz", 1, 5);
    q_case("sz_unicode_size", "sz", 1, 12);
    q_case("sz_zero", "sz", 1, 0);
    q_case("unset", "unset", 1, 64);
    q_case("unset_size_only", "unset", 0, 0);
    q_case("dword", "dw", 1, 64);
    q_case("expand", "exp", 1, 64);
    q_case("expand_long", "explong", 1, 64);
    q_case("expand_long_small", "explong", 1, 22);
    q_case("no_nul", "nonul", 1, 64);
    q_case("no_nul_size_only", "nonul", 0, 0);
    q_case("high_ansi", "hi", 1, 64);
    q_case("missing", "nope", 1, 64);
    q_case("null_subkey", NULL, 1, 64);

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
