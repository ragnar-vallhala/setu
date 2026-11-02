/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: RegQueryInfoKeyW counts and maximum lengths, RegEnumValueW
 * name/data buffer sizing, RegGetValueW type filtering, termination and
 * REG_EXPAND_SZ expansion, REG_MULTI_SZ / REG_SZ edge cases (missing
 * terminators, odd byte counts), RegSetKeyValueW / RegDeleteKeyValueW, and
 * RegLoadMUIStringW error paths.
 *
 * Works only under HKCU\Software\SetuOracle\advapi32_RegQueryInfoKeyW.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regqueryinfokeyw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regenumvaluew
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-reggetvaluew
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regsetkeyvaluew
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regdeletekeyvaluew
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regloadmuistringw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regcreatekeyexw
 *       https://learn.microsoft.com/en-us/windows/win32/sysinfo/registry-value-types
 *       https://learn.microsoft.com/en-us/windows/win32/sysinfo/registry-element-size-limits
 */
#include "oracle.h"

static HKEY root;

static void set_raw(HKEY k, const wchar_t *name, DWORD type, const void *data, DWORD n)
{
    RegSetValueExW(k, name, 0, type, (const BYTE *)data, n);
}

static void info_case(const char *name, HKEY k)
{
    wchar_t cls[64];
    DWORD ncls = 64, nsub = 0xAAAA, maxsub = 0xAAAA, maxcls = 0xAAAA, nval = 0xAAAA, maxvn = 0xAAAA, maxvd = 0xAAAA,
          sd = 0xAAAA;
    FILETIME ft;
    LONG r;
    wmemset(cls, L'#', 64);
    ORACLE_CALL(r, RegQueryInfoKeyW(k, cls, &ncls, NULL, &nsub, &maxsub, &maxcls, &nval, &maxvn, &maxvd, &sd, &ft));
    oracle_case(name);
    o_status("ret", r);
    o_lasterr();
    if (!r) {
        o_wstr("class", cls);
        o_u32("class_len", ncls);
        o_u32("subkeys", nsub);
        o_u32("max_subkey_len", maxsub);
        o_u32("max_class_len", maxcls);
        o_u32("values", nval);
        o_u32("max_value_name_len", maxvn);
        o_u32("max_value_data", maxvd);
        o_bool("sd_nonzero", sd != 0);
    }
    oracle_case_end();
}

static void enum_case(const char *name, HKEY k, DWORD idx, DWORD namelen, DWORD datalen, int null_data)
{
    wchar_t nm[64];
    BYTE data[64];
    DWORD nl = namelen, dl = datalen, type = 0xAAAA;
    LONG r;
    wmemset(nm, L'#', 64);
    memset(data, 0xCC, sizeof data);
    ORACLE_CALL(r, RegEnumValueW(k, idx, nm, &nl, NULL, &type, null_data ? NULL : data, null_data == 2 ? NULL : &dl));
    oracle_case(name);
    o_status("ret", r);
    o_lasterr();
    o_u32("name_len", nl);
    o_u32("data_len", dl);
    o_u32("type", type);
    if (nl < 64 && (r == 0 || r == ERROR_MORE_DATA)) {
        nm[63] = 0;
        o_bool("name_untouched", nm[0] == L'#');
        if (nm[0] != L'#') {
            wchar_t t[64];
            wcsncpy(t, nm, 63);
            t[63] = 0;
            wchar_t *hash = wcschr(t, L'#');
            if (hash) *hash = 0;
            o_wstr("name", t);
        }
    }
    if (!null_data && dl <= 64 && dl <= datalen) o_bytes("data", data, dl);
    oracle_case_end();
}

static void getv_case(const char *name, const wchar_t *sub, const wchar_t *val, DWORD flags, DWORD bufsize)
{
    BYTE buf[256];
    DWORD n = bufsize, type = 0xAAAA;
    LONG r;
    memset(buf, 0xCC, sizeof buf);
    ORACLE_CALL(r, RegGetValueW(root, sub, val, flags, &type, bufsize ? buf : NULL, &n));
    oracle_case(name);
    o_status("ret", r);
    o_lasterr();
    o_u32("type", type);
    o_u32("size", n);
    if (bufsize && n <= bufsize && n <= 256) o_bytes("data", buf, n);
    if (bufsize && bufsize <= 256) o_bytes("byte_after", buf + (n < bufsize ? n : bufsize), n < bufsize ? 2 : 0);
    oracle_case_end();
}

int main(void)
{
    oracle_begin("advapi32/RegQueryInfoKeyW");
    root = oracle_reg_root();
    SetEnvironmentVariableW(L"SETU_ORACLE_X", L"expanded");

    LONG r;
    HKEY k, sub;

    /* ---- RegQueryInfoKeyW ---- */
    info_case("info_empty_root", root);
    RegCreateKeyExW(root, L"withclass", 0, L"MyClass", 0, KEY_ALL_ACCESS, NULL, &k, NULL);
    info_case("info_class_only", k);
    {
        wchar_t cls[4];
        DWORD n = 4;
        ORACLE_CALL(r, RegQueryInfoKeyW(k, cls, &n, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL));
        oracle_case("info_class_buffer_too_small");
        o_status("ret", r);
        o_lasterr();
        o_u32("class_len", n);
        oracle_case_end();
        n = 0;
        ORACLE_CALL(r, RegQueryInfoKeyW(k, NULL, &n, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL));
        oracle_case("info_class_null_buffer");
        o_status("ret", r);
        o_lasterr();
        o_u32("class_len", n);
        oracle_case_end();
        n = 7;
        wchar_t c7[8];
        wmemset(c7, L'#', 8);
        ORACLE_CALL(r, RegQueryInfoKeyW(k, c7, &n, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL));
        oracle_case("info_class_buffer_exact_len");
        o_status("ret", r);
        o_u32("class_len", n);
        o_bool("buf_untouched", c7[0] == L'#');
        oracle_case_end();
        ORACLE_CALL(r, RegQueryInfoKeyW(k, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL));
        oracle_case("info_all_null");
        o_status("ret", r);
        oracle_case_end();
        DWORD reserved = 0;
        ORACLE_CALL(r, RegQueryInfoKeyW(k, NULL, NULL, &reserved, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL));
        oracle_case("info_reserved_non_null");
        o_status("ret", r);
        oracle_case_end();
    }
    RegCloseKey(k);

    RegCreateKeyExW(root, L"a", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &sub, NULL);
    RegCloseKey(sub);
    RegCreateKeyExW(root, L"LongerSubkeyName", 0, L"cls12345", 0, KEY_ALL_ACCESS, NULL, &sub, NULL);
    RegCloseKey(sub);
    set_raw(root, L"v", REG_DWORD, "\x01\x02\x03\x04", 4);
    set_raw(root, L"LongValueName12", REG_BINARY, "0123456789", 10);
    set_raw(root, L"", REG_SZ, L"default", 16);
    set_raw(root, L"empty", REG_SZ, L"", 0);
    info_case("info_populated", root);
    set_raw(root, L"éééééééééééééééééé", REG_DWORD, "\0\0\0\0", 4);
    info_case("info_non_ascii_name", root);
    RegDeleteValueW(root, L"éééééééééééééééééé");
    RegDeleteValueW(root, L"LongValueName12");
    RegDeleteKeyW(root, L"LongerSubkeyName");
    info_case("info_max_lengths_after_delete", root);
    {
        HKEY ro;
        RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\SetuOracle\\advapi32_RegQueryInfoKeyW", 0, KEY_ENUMERATE_SUB_KEYS, &ro);
        info_case("info_without_query_value_access", ro);
        RegCloseKey(ro);
        RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\SetuOracle\\advapi32_RegQueryInfoKeyW", 0, KEY_SET_VALUE, &ro);
        info_case("info_set_value_access_only", ro);
        RegCloseKey(ro);
    }
    info_case("info_invalid_handle", (HKEY)(ULONG_PTR)0x1234);

    /* ---- RegEnumValueW sizing ---- */
    RegCreateKeyExW(root, L"enum", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL);
    set_raw(k, L"name5", REG_BINARY, "ABCDEFGH", 8);
    enum_case("enum_ok", k, 0, 64, 64, 0);
    enum_case("enum_name_exact_len", k, 0, 5, 64, 0);
    enum_case("enum_name_len_plus_1", k, 0, 6, 64, 0);
    enum_case("enum_name_zero", k, 0, 0, 64, 0);
    enum_case("enum_data_too_small", k, 0, 64, 4, 0);
    enum_case("enum_data_size_query", k, 0, 64, 0, 1);
    enum_case("enum_no_data_no_size", k, 0, 64, 0, 2);
    enum_case("enum_both_too_small", k, 0, 2, 2, 0);
    enum_case("enum_index_past_end", k, 1, 64, 64, 0);
    set_raw(k, L"sz", REG_SZ, L"abc", 8);
    enum_case("enum_sz_data_too_small", k, 1, 64, 6, 0);
    enum_case("enum_sz_exact", k, 1, 64, 8, 0);
    RegCloseKey(k);

    /* ---- REG_SZ / MULTI_SZ / EXPAND_SZ edge cases ---- */
    set_raw(root, L"sz_no_nul", REG_SZ, L"abc", 6);
    set_raw(root, L"sz_odd", REG_SZ, "a\0b\0c", 5);
    set_raw(root, L"sz_embedded_nul", REG_SZ, L"ab\0cd", 12);
    set_raw(root, L"multi_ok", REG_MULTI_SZ, L"one\0two\0", 18);
    set_raw(root, L"multi_single_nul", REG_MULTI_SZ, L"one\0two", 16);
    set_raw(root, L"multi_no_nul", REG_MULTI_SZ, L"one\0two", 14);
    set_raw(root, L"multi_empty", REG_MULTI_SZ, L"", 0);
    set_raw(root, L"expand", REG_EXPAND_SZ, L"<%SETU_ORACLE_X%>", 36);
    set_raw(root, L"expand_undefined", REG_EXPAND_SZ, L"<%SETU_ORACLE_UNDEF%>", 44);
    set_raw(root, L"expand_no_nul", REG_EXPAND_SZ, L"%SETU_ORACLE_X%", 30);
    set_raw(root, L"dw", REG_DWORD, "\x78\x56\x34\x12", 4);
    set_raw(root, L"dw_short", REG_DWORD, "\x78\x56", 2);
    set_raw(root, L"qw", REG_QWORD, "\x01\x02\x03\x04\x05\x06\x07\x08", 8);
    set_raw(root, L"none", REG_NONE, "xy", 2);

    getv_case("get_sz_no_nul_terminated", NULL, L"sz_no_nul", RRF_RT_REG_SZ, 64);
    getv_case("get_sz_no_nul_exact_buffer", NULL, L"sz_no_nul", RRF_RT_REG_SZ, 6);
    getv_case("get_sz_no_nul_size_query", NULL, L"sz_no_nul", RRF_RT_REG_SZ, 0);
    getv_case("get_sz_odd", NULL, L"sz_odd", RRF_RT_REG_SZ, 64);
    getv_case("get_sz_embedded_nul", NULL, L"sz_embedded_nul", RRF_RT_REG_SZ, 64);
    getv_case("get_sz_noexpand_flag", NULL, L"sz_no_nul", RRF_RT_REG_SZ | RRF_NOEXPAND, 64);
    getv_case("get_multi_ok", NULL, L"multi_ok", RRF_RT_REG_MULTI_SZ, 64);
    getv_case("get_multi_single_nul", NULL, L"multi_single_nul", RRF_RT_REG_MULTI_SZ, 64);
    getv_case("get_multi_no_nul", NULL, L"multi_no_nul", RRF_RT_REG_MULTI_SZ, 64);
    getv_case("get_multi_empty", NULL, L"multi_empty", RRF_RT_REG_MULTI_SZ, 64);
    getv_case("get_multi_as_sz", NULL, L"multi_ok", RRF_RT_REG_SZ, 64);
    getv_case("get_expand", NULL, L"expand", RRF_RT_REG_SZ, 64);
    getv_case("get_expand_as_expand_sz_type", NULL, L"expand", RRF_RT_REG_EXPAND_SZ, 64);
    getv_case("get_expand_noexpand", NULL, L"expand", RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND, 64);
    getv_case("get_expand_noexpand_any", NULL, L"expand", RRF_RT_ANY | RRF_NOEXPAND, 64);
    getv_case("get_expand_size_query", NULL, L"expand", RRF_RT_REG_SZ, 0);
    getv_case("get_expand_small_buffer", NULL, L"expand", RRF_RT_REG_SZ, 8);
    getv_case("get_expand_undefined", NULL, L"expand_undefined", RRF_RT_REG_SZ, 128);
    getv_case("get_expand_no_nul", NULL, L"expand_no_nul", RRF_RT_REG_SZ, 64);
    getv_case("get_expand_any_flag", NULL, L"expand", RRF_RT_ANY, 64);
    getv_case("get_dword", NULL, L"dw", RRF_RT_REG_DWORD, 64);
    getv_case("get_dword_as_qword", NULL, L"dw", RRF_RT_REG_QWORD, 64);
    getv_case("get_dword_rt_dword_flag", NULL, L"dw", RRF_RT_DWORD, 64);
    getv_case("get_dword_short", NULL, L"dw_short", RRF_RT_REG_DWORD, 64);
    getv_case("get_dword_short_as_binary", NULL, L"dw_short", RRF_RT_REG_BINARY | RRF_RT_REG_DWORD, 64);
    getv_case("get_dword_small_buffer", NULL, L"dw", RRF_RT_REG_DWORD, 2);
    getv_case("get_qword", NULL, L"qw", RRF_RT_QWORD, 64);
    getv_case("get_qword_as_dword", NULL, L"qw", RRF_RT_DWORD, 64);
    getv_case("get_none", NULL, L"none", RRF_RT_REG_NONE, 64);
    getv_case("get_none_any", NULL, L"none", RRF_RT_ANY, 64);
    getv_case("get_type_mismatch", NULL, L"dw", RRF_RT_REG_SZ, 64);
    getv_case("get_missing_value", NULL, L"missing", RRF_RT_ANY, 64);
    getv_case("get_missing_subkey", L"nosuchkey", L"v", RRF_RT_ANY, 64);
    getv_case("get_default_value", NULL, NULL, RRF_RT_REG_SZ, 64);
    getv_case("get_default_value_empty_name", NULL, L"", RRF_RT_REG_SZ, 64);
    getv_case("get_flags_zero", NULL, L"dw", 0, 64);
    getv_case("get_zeroonfailure", NULL, L"dw", RRF_RT_REG_SZ | RRF_ZEROONFAILURE, 16);
    getv_case("get_zeroonfailure_small", NULL, L"sz_no_nul", RRF_RT_REG_SZ | RRF_ZEROONFAILURE, 4);
    {
        BYTE buf[16];
        DWORD n = 16;
        ORACLE_CALL(r, RegGetValueW(root, NULL, L"dw", RRF_RT_ANY, NULL, buf, NULL));
        oracle_case("get_data_without_size");
        o_status("ret", r);
        oracle_case_end();
        ORACLE_CALL(r, RegGetValueW(root, NULL, L"dw", RRF_RT_ANY, NULL, NULL, NULL));
        oracle_case("get_existence_check");
        o_status("ret", r);
        oracle_case_end();
        (void)n;
    }

    /* ---- RegSetKeyValueW / RegDeleteKeyValueW ---- */
    ORACLE_CALL(r, RegSetKeyValueW(root, L"made\\by\\setkeyvalue", L"x", REG_SZ, L"hi", 6));
    oracle_case("setkeyvalue_creates_path");
    o_status("ret", r);
    oracle_case_end();
    getv_case("setkeyvalue_readback", L"made\\by\\setkeyvalue", L"x", RRF_RT_REG_SZ, 64);
    ORACLE_CALL(r, RegSetKeyValueW(root, NULL, L"y", REG_DWORD, "\1\0\0\0", 4));
    oracle_case("setkeyvalue_null_subkey");
    o_status("ret", r);
    oracle_case_end();
    ORACLE_CALL(r, RegSetKeyValueW(root, L"s", L"bad", REG_DWORD, "\1\0", 2));
    oracle_case("setkeyvalue_short_dword");
    o_status("ret", r);
    oracle_case_end();
    ORACLE_CALL(r, RegDeleteKeyValueW(root, L"made\\by\\setkeyvalue", L"x"));
    oracle_case("deletekeyvalue");
    o_status("ret", r);
    oracle_case_end();
    ORACLE_CALL(r, RegDeleteKeyValueW(root, L"made\\by\\setkeyvalue", L"x"));
    oracle_case("deletekeyvalue_again");
    o_status("ret", r);
    oracle_case_end();
    ORACLE_CALL(r, RegDeleteKeyValueW(root, L"nosuchkey", L"x"));
    oracle_case("deletekeyvalue_missing_key");
    o_status("ret", r);
    oracle_case_end();

    /* ---- RegLoadMUIStringW error paths ---- */
    {
        LONG (WINAPI *pLoad)(HKEY, LPCWSTR, LPWSTR, DWORD, LPDWORD, DWORD, LPCWSTR) =
            (LONG (WINAPI *)(HKEY, LPCWSTR, LPWSTR, DWORD, LPDWORD, DWORD, LPCWSTR))(void *)GetProcAddress(
                GetModuleHandleW(L"advapi32.dll"), "RegLoadMUIStringW");
        oracle_case("mui_present");
        o_bool("present", pLoad != NULL);
        oracle_case_end();
        if (pLoad) {
            wchar_t out[64];
            DWORD need = 0xAAAA;
            set_raw(root, L"mui_plain", REG_SZ, L"plain text", 22);
            set_raw(root, L"mui_bad_ref", REG_SZ, L"@nosuchdll_setu.dll,-100", 50);
            set_raw(root, L"mui_no_comma", REG_SZ, L"@justtext", 20);
            set_raw(root, L"mui_dword", REG_DWORD, "\1\0\0\0", 4);
            struct { const char *name; const wchar_t *val; DWORD size; DWORD flags; } cs[] = {
                {"mui_plain_string", L"mui_plain", sizeof out, 0},
                {"mui_plain_small_buffer", L"mui_plain", 8, 0},
                {"mui_plain_odd_buffer", L"mui_plain", 63, 0},
                {"mui_bad_reference", L"mui_bad_ref", sizeof out, 0},
                {"mui_no_comma", L"mui_no_comma", sizeof out, 0},
                {"mui_dword_value", L"mui_dword", sizeof out, 0},
                {"mui_missing_value", L"missing", sizeof out, 0},
                {"mui_bad_flags", L"mui_plain", sizeof out, 0x80},
                {"mui_truncate_flag", L"mui_plain", 8, REG_MUI_STRING_TRUNCATE},
            };
            for (size_t i = 0; i < sizeof cs / sizeof cs[0]; i++) {
                wmemset(out, L'#', 64);
                need = 0xAAAA;
                ORACLE_CALL(r, pLoad(root, cs[i].val, out, cs[i].size, &need, cs[i].flags, NULL));
                oracle_case(cs[i].name);
                o_status("ret", r);
                o_lasterr();
                o_u32("needed", need);
                if (!r) o_wstr("out", out);
                oracle_case_end();
            }
            ORACLE_CALL(r, pLoad(root, L"mui_plain", NULL, 0, &need, 0, NULL));
            oracle_case("mui_null_buffer");
            o_status("ret", r);
            o_u32("needed", need);
            oracle_case_end();
        }
    }

    /* ---- probes: class buffer sizing, string sizing in RegGetValueW ---- */
    RegOpenKeyExW(root, L"withclass", 0, KEY_ALL_ACCESS, &k);
    {
        static const DWORD sizes[] = {1, 5, 6, 8};
        for (size_t i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
            wchar_t c[16];
            DWORD n = sizes[i], nsub = 0xAAAA;
            char nm[64];
            wmemset(c, L'#', 16);
            ORACLE_CALL(r, RegQueryInfoKeyW(k, c, &n, NULL, &nsub, NULL, NULL, NULL, NULL, NULL, NULL, NULL));
            snprintf(nm, sizeof nm, "probe_class_len_%lu", (unsigned long)sizes[i]);
            oracle_case(nm);
            o_status("ret", r);
            o_lasterr();
            o_u32("class_len", n);
            o_u32("subkeys", nsub);
            c[15] = 0;
            o_wstr("buf", c);
            oracle_case_end();
        }
        DWORD n = 50, nsub = 0xAAAA;
        ORACLE_CALL(r, RegQueryInfoKeyW(k, NULL, &n, NULL, &nsub, NULL, NULL, NULL, NULL, NULL, NULL, NULL));
        oracle_case("probe_class_null_buffer_len_50");
        o_status("ret", r);
        o_u32("class_len", n);
        o_u32("subkeys", nsub);
        oracle_case_end();
        wchar_t c[4] = {L'#', L'#', L'#', 0};
        n = 0;
        ORACLE_CALL(r, RegQueryInfoKeyW(k, c, &n, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL));
        oracle_case("probe_class_buffer_len_0");
        o_status("ret", r);
        o_u32("class_len", n);
        o_wstr("buf", c);
        oracle_case_end();
    }
    RegCloseKey(k);
    RegCreateKeyExW(root, L"noclass", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL);
    {
        wchar_t c[4] = {L'#', L'#', L'#', 0};
        DWORD n = 0;
        ORACLE_CALL(r, RegQueryInfoKeyW(k, c, &n, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL));
        oracle_case("probe_noclass_buffer_len_0");
        o_status("ret", r);
        o_u32("class_len", n);
        o_wstr("buf", c);
        oracle_case_end();
        n = 1;
        ORACLE_CALL(r, RegQueryInfoKeyW(k, c, &n, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL));
        oracle_case("probe_noclass_buffer_len_1");
        o_status("ret", r);
        o_u32("class_len", n);
        o_wstr("buf", c);
        oracle_case_end();
    }
    RegCloseKey(k);
    set_raw(root, L"sz_term", REG_SZ, L"abc", 8);
    getv_case("probe_sz_term_size_query", NULL, L"sz_term", RRF_RT_REG_SZ, 0);
    getv_case("probe_sz_term_exact", NULL, L"sz_term", RRF_RT_REG_SZ, 8);
    getv_case("probe_sz_term_small", NULL, L"sz_term", RRF_RT_REG_SZ, 6);
    getv_case("probe_sz_no_nul_buf8", NULL, L"sz_no_nul", RRF_RT_REG_SZ, 8);
    getv_case("probe_sz_no_nul_buf9", NULL, L"sz_no_nul", RRF_RT_REG_SZ, 9);
    getv_case("probe_sz_no_nul_buf10", NULL, L"sz_no_nul", RRF_RT_REG_SZ, 10);
    getv_case("probe_sz_odd_size_query", NULL, L"sz_odd", RRF_RT_REG_SZ, 0);
    getv_case("probe_sz_odd_buf6", NULL, L"sz_odd", RRF_RT_REG_SZ, 6);
    getv_case("probe_sz_empty", NULL, L"empty", RRF_RT_REG_SZ, 64);
    getv_case("probe_sz_empty_size_query", NULL, L"empty", RRF_RT_REG_SZ, 0);
    getv_case("probe_multi_ok_size_query", NULL, L"multi_ok", RRF_RT_REG_MULTI_SZ, 0);
    getv_case("probe_multi_ok_exact", NULL, L"multi_ok", RRF_RT_REG_MULTI_SZ, 18);
    getv_case("probe_multi_no_nul_size_query", NULL, L"multi_no_nul", RRF_RT_REG_MULTI_SZ, 0);
    getv_case("probe_multi_no_nul_buf18", NULL, L"multi_no_nul", RRF_RT_REG_MULTI_SZ, 18);
    getv_case("probe_multi_single_nul_size_query", NULL, L"multi_single_nul", RRF_RT_REG_MULTI_SZ, 0);
    getv_case("probe_multi_empty_size_query", NULL, L"multi_empty", RRF_RT_REG_MULTI_SZ, 0);
    getv_case("probe_expand_noexpand_size_query", NULL, L"expand", RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND, 0);
    getv_case("probe_expand_no_nul_size_query", NULL, L"expand_no_nul", RRF_RT_REG_SZ, 0);
    getv_case("probe_expand_no_nul_noexpand_size_query", NULL, L"expand_no_nul", RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND, 0);
    getv_case("probe_expand_undefined_size_query", NULL, L"expand_undefined", RRF_RT_REG_SZ, 0);
    getv_case("probe_expand_as_expand_sz_size_query", NULL, L"expand", RRF_RT_REG_EXPAND_SZ, 0);
    {
        static const wchar_t g[] = L"%SETU_ORACLE_X%";
        set_raw(root, L"expand_grow", REG_EXPAND_SZ, g, sizeof g);
        SetEnvironmentVariableW(L"SETU_ORACLE_X", L"a-much-longer-expansion-text");
    }
    getv_case("probe_expand_grow_size_query", NULL, L"expand_grow", RRF_RT_REG_SZ, 0);
    getv_case("probe_expand_grow_small", NULL, L"expand_grow", RRF_RT_REG_SZ, 8);
    getv_case("probe_expand_grow", NULL, L"expand_grow", RRF_RT_REG_SZ, 128);
    {
        /* RegEnumValueW past the end with other type and size inputs */
        DWORD t = 0x1234, nl = 10, dl = 10;
        wchar_t nm[16];
        ORACLE_CALL(r, RegEnumValueW(root, 1000, nm, &nl, NULL, &t, NULL, &dl));
        oracle_case("probe_enum_past_end_root");
        o_status("ret", r);
        o_u32("type", t);
        o_u32("name_len", nl);
        o_u32("data_len", dl);
        oracle_case_end();
    }
    {
        DWORD sd = 0xAAAA;
        HKEY ro;
        RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\SetuOracle\\advapi32_RegQueryInfoKeyW", 0, KEY_QUERY_VALUE, &ro);
        ORACLE_CALL(r, RegQueryInfoKeyW(ro, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &sd, NULL));
        oracle_case("probe_query_value_access_only");
        o_status("ret", r);
        o_bool("sd_nonzero", sd != 0 && sd != 0xAAAA);
        oracle_case_end();
        RegCloseKey(ro);
        RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\SetuOracle\\advapi32_RegQueryInfoKeyW", 0, READ_CONTROL, &ro);
        ORACLE_CALL(r, RegQueryInfoKeyW(ro, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &sd, NULL));
        oracle_case("probe_read_control_only");
        o_status("ret", r);
        oracle_case_end();
        RegCloseKey(ro);
    }
    {
        /* RegLoadMUIStringW on empty and expand-sz values */
        LONG (WINAPI *pLoad)(HKEY, LPCWSTR, LPWSTR, DWORD, LPDWORD, DWORD, LPCWSTR) =
            (LONG (WINAPI *)(HKEY, LPCWSTR, LPWSTR, DWORD, LPDWORD, DWORD, LPCWSTR))(void *)GetProcAddress(
                GetModuleHandleW(L"advapi32.dll"), "RegLoadMUIStringW");
        if (pLoad) {
            wchar_t out[64];
            DWORD need = 0xAAAA;
            set_raw(root, L"mui_binary", REG_BINARY, "@x,-1", 5);
            ORACLE_CALL(r, pLoad(root, L"empty", out, sizeof out, &need, 0, NULL));
            oracle_case("probe_mui_empty_sz");
            o_status("ret", r);
            o_lasterr();
            oracle_case_end();
            ORACLE_CALL(r, pLoad(root, L"mui_binary", out, sizeof out, &need, 0, NULL));
            oracle_case("probe_mui_binary");
            o_status("ret", r);
            o_lasterr();
            oracle_case_end();
            ORACLE_CALL(r, pLoad(root, L"expand", out, sizeof out, &need, 0, NULL));
            oracle_case("probe_mui_expand_plain");
            o_status("ret", r);
            o_lasterr();
            oracle_case_end();
        }
    }

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
