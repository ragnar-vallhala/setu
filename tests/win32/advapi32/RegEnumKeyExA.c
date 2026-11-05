/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: RegEnumKeyExA and RegEnumKeyA: ANSI names (cp1252 letters,
 * and Unicode-only letters that have no cp1252 form), buffer sizes counted
 * in ANSI characters, ERROR_MORE_DATA behaviour, class buffers, and indexes
 * past the end.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regenumkeyexa
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regenumkeya
 *       https://learn.microsoft.com/en-us/windows/win32/intl/code-pages
 */
#include "oracle.h"

static HKEY root;

static void enum_a(const char *name, HKEY k, DWORD index, DWORD cap, DWORD class_cap)
{
    char nm[64], cls[64];
    DWORD n = cap, cn = class_cap;
    memset(nm, '#', 63); nm[63] = 0;
    memset(cls, '#', 63); cls[63] = 0;
    LONG st;
    ORACLE_CALL(st, RegEnumKeyExA(k, index, nm, &n, NULL, class_cap ? cls : NULL, class_cap ? &cn : NULL, NULL));
    oracle_case(name);
    o_status("ret", st);
    o_lasterr();
    o_u32("name_len", n);
    nm[24] = 0;
    o_bytes("name_bytes", nm, strlen(nm));
    if (class_cap) {
        o_u32("class_len", cn);
        cls[24] = 0;
        o_bytes("class_bytes", cls, strlen(cls));
    }
    oracle_case_end();
}

int main(void)
{
    oracle_begin("advapi32/RegEnumKeyExA");
    root = oracle_reg_root();
    oracle_case("acp");
    o_u32("acp", GetACP());
    oracle_case_end();

    HKEY k, g;
    RegCreateKeyExW(root, L"g", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &g, NULL);
    RegCreateKeyExW(g, L"café", 0, L"Cläss", 0, KEY_ALL_ACCESS, NULL, &k, NULL);
    RegCloseKey(k);
    RegCreateKeyExW(g, L"αβγ", 0, L"κ", 0, KEY_ALL_ACCESS, NULL, &k, NULL);
    RegCloseKey(k);
    RegCreateKeyExW(g, L"€", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL);
    RegCloseKey(k);
    RegCreateKeyExW(g, L"Plain", 0, L"PlainClass", 0, KEY_ALL_ACCESS, NULL, &k, NULL);
    RegCloseKey(k);

    /* Order (W): Plain, café, €? Whatever it is, find indexes by W name. */
    DWORD idx_cafe = 0, idx_greek = 0, idx_euro = 0, idx_plain = 0;
    for (DWORD i = 0; i < 4; i++) {
        wchar_t w[16];
        DWORD n = 16;
        RegEnumKeyExW(g, i, w, &n, NULL, NULL, NULL, NULL);
        if (w[0] == L'c') idx_cafe = i;
        else if (w[0] == 0x3b1) idx_greek = i;
        else if (w[0] == 0x20ac) idx_euro = i;
        else idx_plain = i;
    }
    oracle_case("w_order");
    o_u32("cafe", idx_cafe);
    o_u32("greek", idx_greek);
    o_u32("euro", idx_euro);
    o_u32("plain", idx_plain);
    oracle_case_end();

    enum_a("plain_cap_64", g, idx_plain, 64, 0);
    enum_a("plain_cap_6_exact_plus_nul", g, idx_plain, 6, 0);
    enum_a("plain_cap_5_no_nul_room", g, idx_plain, 5, 0);
    enum_a("plain_cap_0", g, idx_plain, 0, 0);
    enum_a("plain_with_class", g, idx_plain, 64, 64);
    enum_a("plain_class_cap_10_no_nul_room", g, idx_plain, 64, 10);
    enum_a("plain_class_cap_11", g, idx_plain, 64, 11);
    enum_a("cafe_cp1252", g, idx_cafe, 64, 64);
    enum_a("cafe_cap_4_no_nul_room", g, idx_cafe, 4, 0);
    enum_a("greek_unmappable", g, idx_greek, 64, 64);
    enum_a("greek_cap_3", g, idx_greek, 3, 0);
    enum_a("euro_cp1252_0x80", g, idx_euro, 64, 0);
    enum_a("past_end", g, 4, 64, 0);
    enum_a("invalid_handle", (HKEY)(ULONG_PTR)0x1234, 0, 64, 0);

    {   /* Legacy RegEnumKeyA. */
        char buf[64];
        memset(buf, '#', 63); buf[63] = 0;
        LONG st;
        ORACLE_CALL(st, RegEnumKeyA(g, idx_plain, buf, 64));
        oracle_case("regenumkeya_ok");
        o_status("ret", st);
        o_lasterr();
        o_str("buf", buf);
        oracle_case_end();
        memset(buf, '#', 63); buf[63] = 0;
        ORACLE_CALL(st, RegEnumKeyA(g, idx_plain, buf, 5));
        oracle_case("regenumkeya_cap_5");
        o_status("ret", st);
        buf[20] = 0;
        o_str("buf", buf);
        oracle_case_end();
        ORACLE_CALL(st, RegEnumKeyA(g, idx_greek, buf, 64));
        oracle_case("regenumkeya_greek");
        o_status("ret", st);
        o_bytes("buf", buf, strlen(buf));
        oracle_case_end();
        ORACLE_CALL(st, RegEnumKeyA(g, 4, buf, 64));
        oracle_case("regenumkeya_past_end");
        o_status("ret", st);
        o_lasterr();
        oracle_case_end();
    }

    RegCloseKey(g);
    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
