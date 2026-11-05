/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: HKEY_PERFORMANCE_DATA basics: RegQueryValueExW with "Global"
 * and "Counter 009" and a growing buffer (ERROR_MORE_DATA until big enough;
 * only the signature, type and booleans about sizes are printed),
 * PERF_DATA_BLOCK header fields that are fixed by the docs, RegQueryInfoKeyW,
 * RegEnumKeyExW, RegOpenKeyExW subkeys, RegSetValueExW (refused), and
 * RegCloseKey. Also HKEY_PERFORMANCE_TEXT / NLSTEXT.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/perfctrs/using-the-registry-functions-to-consume-counter-data
 *       https://learn.microsoft.com/en-us/windows/win32/api/winperf/ns-winperf-perf_data_block
 *       https://learn.microsoft.com/en-us/windows/win32/sysinfo/predefined-keys
 */
#include "oracle.h"
#include <winperf.h>

#ifndef HKEY_PERFORMANCE_TEXT
#define HKEY_PERFORMANCE_TEXT ((HKEY)(ULONG_PTR)((LONG)0x80000050))
#endif
#ifndef HKEY_PERFORMANCE_NLSTEXT
#define HKEY_PERFORMANCE_NLSTEXT ((HKEY)(ULONG_PTR)((LONG)0x80000060))
#endif

static BYTE *query(HKEY k, const wchar_t *name, DWORD *size, DWORD *type, LONG *first, LONG *last, int *rounds)
{
    DWORD cap = 1024;
    BYTE *buf = NULL;
    *rounds = 0;
    for (;;) {
        buf = realloc(buf, cap);
        DWORD sz = cap;
        LONG st = RegQueryValueExW(k, name, NULL, type, buf, &sz);
        if (*rounds == 0) *first = st;
        (*rounds)++;
        *last = st;
        if (st != ERROR_MORE_DATA || cap > 64u * 1024 * 1024) {
            *size = sz;
            return buf;
        }
        cap *= 2;
    }
}

int main(void)
{
    oracle_begin("advapi32/RegPerformanceData");

    {
        DWORD size = 0, type = 0;
        LONG first, last;
        int rounds;
        BYTE *b = query(HKEY_PERFORMANCE_DATA, L"Global", &size, &type, &first, &last, &rounds);
        oracle_case("global");
        o_status("first_ret", first);
        o_status("final_ret", last);
        o_bool("needed_more_than_1k", rounds > 1);
        if (!last) {
            PERF_DATA_BLOCK *p = (PERF_DATA_BLOCK *)b;
            o_u32("type", type);
            o_bool("signature_PERF", !memcmp(p->Signature, L"PERF", 8));
            o_u32("little_endian", p->LittleEndian);
            o_u32("version", p->Version);
            o_bool("total_le_size", p->TotalByteLength <= size);
            o_bool("has_objects", p->NumObjectTypes > 0);
            o_bool("header_le_total", p->HeaderLength <= p->TotalByteLength);
        }
        oracle_case_end();
        free(b);
    }
    {
        DWORD size = 0, type = 0;
        LONG first, last;
        int rounds;
        BYTE *b = query(HKEY_PERFORMANCE_DATA, L"Counter 009", &size, &type, &first, &last, &rounds);
        oracle_case("counter_009");
        o_status("final_ret", last);
        if (!last) {
            o_u32("type", type);
            o_bool("starts_with_1", size > 4 && !memcmp(b, L"1\0", 4));
        }
        oracle_case_end();
        free(b);
        b = query(HKEY_PERFORMANCE_TEXT, L"Counter", &size, &type, &first, &last, &rounds);
        oracle_case("performance_text_counter");
        o_status("final_ret", last);
        if (!last) {
            o_u32("type", type);
            o_bool("starts_with_1", size > 4 && !memcmp(b, L"1\0", 4));
        }
        oracle_case_end();
        free(b);
    }
    {
        DWORD v = 0, sz = sizeof v;
        LONG st;
        ORACLE_CALL(st, RegQueryValueExW(HKEY_PERFORMANCE_DATA, L"Global", NULL, NULL, (BYTE *)&v, &sz));
        oracle_case("global_4_byte_buffer");
        o_status("ret", st);
        o_lasterr();
        oracle_case_end();
        sz = 0;
        ORACLE_CALL(st, RegQueryValueExW(HKEY_PERFORMANCE_DATA, L"Global", NULL, NULL, NULL, &sz));
        oracle_case("global_size_query");
        o_status("ret", st);
        oracle_case_end();
    }
    {
        DWORD nsub = 77, nval = 77;
        LONG st;
        ORACLE_CALL(st, RegQueryInfoKeyW(HKEY_PERFORMANCE_DATA, NULL, NULL, NULL, &nsub, NULL, NULL, &nval, NULL, NULL, NULL,
                                         NULL));
        oracle_case("query_info");
        o_status("ret", st);
        o_lasterr();
        if (!st) o_u32("subkeys", nsub);
        oracle_case_end();
        wchar_t nm[64];
        DWORD n = 64;
        ORACLE_CALL(st, RegEnumKeyExW(HKEY_PERFORMANCE_DATA, 0, nm, &n, NULL, NULL, NULL, NULL));
        oracle_case("enum_key");
        o_status("ret", st);
        oracle_case_end();
        HKEY k = NULL;
        ORACLE_CALL(st, RegOpenKeyExW(HKEY_PERFORMANCE_DATA, L"Global", 0, KEY_READ, &k));
        oracle_case("open_subkey");
        o_status("ret", st);
        oracle_case_end();
        if (!st) RegCloseKey(k);
        k = NULL;
        ORACLE_CALL(st, RegCreateKeyExW(HKEY_PERFORMANCE_DATA, L"SetuOracle", 0, NULL, REG_OPTION_VOLATILE, KEY_READ, NULL,
                                        &k, NULL));
        oracle_case("create_subkey");
        o_status("ret", st);
        oracle_case_end();
        if (!st) RegCloseKey(k);
        DWORD v = 1;
        ORACLE_CALL(st, RegSetValueExW(HKEY_PERFORMANCE_DATA, L"SetuOracle", 0, REG_DWORD, (const BYTE *)&v, sizeof v));
        oracle_case("set_value");
        o_status("ret", st);
        oracle_case_end();
        ORACLE_CALL(st, RegQueryValueExW(HKEY_PERFORMANCE_DATA, L"NoSuchObjectName", NULL, NULL, NULL, NULL));
        oracle_case("query_unknown_value_no_buffer");
        o_status("ret", st);
        oracle_case_end();
    }
    {
        LONG st;
        ORACLE_CALL(st, RegCloseKey(HKEY_PERFORMANCE_DATA));
        oracle_case("close");
        o_status("ret", st);
        o_lasterr();
        oracle_case_end();
    }

    return oracle_end();
}
