/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: RegEnumValueW returning string data: values stored without a
 * NUL terminator, with odd byte counts, or empty, read with a buffer of the
 * stored size, the stored size plus one and plus two, and a large buffer:
 * does RegEnumValueW add a terminator like RegQueryValueExW, and what
 * size does it report? Buffers are filled with 0xCC first.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regenumvaluew
 */
#include "oracle.h"

static HKEY root;

static void s_case(const char *cname, DWORD type, const void *data, DWORD size)
{
    RegSetValueExW(root, L"V", 0, type, data, size);
    oracle_case(cname);
    DWORD sizes[4] = { size, size + 1, size + 2, 64 };
    const char *names[4] = { "stored", "plus1", "plus2", "big" };
    for (int i = 0; i < 4; i++) {
        wchar_t name[8];
        unsigned char b[72];
        char k[32];
        memset(b, 0xCC, sizeof b);
        DWORD nl = 8, dl = sizes[i], t = 0;
        LONG st = RegEnumValueW(root, 0, name, &nl, NULL, &t, b, &dl);
        snprintf(k, sizeof k, "%s_ret", names[i]); o_status(k, st);
        snprintf(k, sizeof k, "%s_size", names[i]); o_u32(k, dl);
        snprintf(k, sizeof k, "%s_buffer", names[i]); o_bytes(k, b, sizes[i] + 2 < 64 ? sizes[i] + 2 : 64);
    }
    RegDeleteValueW(root, L"V");
    oracle_case_end();
}

int main(void)
{
    oracle_begin("advapi32/RegEnumValueW_strings");
    root = oracle_reg_root();

    static const wchar_t abc[] = L"abc";
    s_case("sz_terminated", REG_SZ, abc, 8);
    s_case("sz_no_nul", REG_SZ, abc, 6);
    s_case("sz_odd_5", REG_SZ, abc, 5);
    s_case("sz_empty", REG_SZ, NULL, 0);
    s_case("expand_no_nul", REG_EXPAND_SZ, abc, 6);
    s_case("multi_no_final_nul", REG_MULTI_SZ, L"a\0b", 6);
    s_case("multi_terminated", REG_MULTI_SZ, L"a\0b\0", 10);
    s_case("binary_6", REG_BINARY, abc, 6);
    s_case("link_no_nul", REG_LINK, abc, 6);

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
