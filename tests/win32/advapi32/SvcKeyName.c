/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: GetServiceKeyNameW/A and GetServiceDisplayNameW/A: lookups
 * by display name and key name (case-insensitive), missing names, buffer
 * sizes (NULL buffer, zero, too small by one, exact), what the size
 * argument holds afterwards (characters, without or with the terminator,
 * W versus A), and whether a too-small buffer is written to.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winsvc/nf-winsvc-getservicekeynamew
 *       https://learn.microsoft.com/en-us/windows/win32/api/winsvc/nf-winsvc-getservicedisplaynamew
 */
#include "svc.h"

static SC_HANDLE scm;
#define DISPLAY L"SetuOracle KeyName Display"

typedef BOOL(WINAPI *getw_fn)(SC_HANDLE, LPCWSTR, LPWSTR, LPDWORD);
typedef BOOL(WINAPI *geta_fn)(SC_HANDLE, LPCSTR, LPSTR, LPDWORD);

static void w_case(const char *name, getw_fn fn, SC_HANDLE m, const wchar_t *in, int use_buf, DWORD size)
{
    wchar_t buf[300];
    BOOL r;
    DWORD n = size;
    for (int i = 0; i < 300; i++) buf[i] = L'#';
    ORACLE_CALL(r, fn(m, in, use_buf ? buf : NULL, &n));
    oracle_case(name);
    o_bool("ret", r);
    o_lasterr();
    o_u32("size", n);
    if (use_buf) {
        if (r) o_wstr("out", buf);
        else o_hex32("buf0", (DWORD)buf[0]);
    }
    oracle_case_end();
}

static void a_case(const char *name, geta_fn fn, const char *in, int use_buf, DWORD size)
{
    char buf[300];
    BOOL r;
    DWORD n = size;
    memset(buf, '#', sizeof buf);
    ORACLE_CALL(r, fn(scm, in, use_buf ? buf : NULL, &n));
    oracle_case(name);
    o_bool("ret", r);
    o_lasterr();
    o_u32("size", n);
    if (use_buf) {
        if (r) o_str("out", buf);
        else o_hex32("buf0", (unsigned char)buf[0]);
    }
    oracle_case_end();
}

int main(int argc, char **argv)
{
    int rc = svc_child_main("advapi32/SvcKeyName", argc, argv);
    if (rc >= 0) return rc;
    oracle_begin("advapi32/SvcKeyName");
    scm = svc_scm();
    const wchar_t *svc = svc_name(1), *nodisp = svc_name(2);
    svc_purge(scm, svc);
    svc_purge(scm, nodisp);
    SC_HANDLE s = CreateServiceW(scm, svc, DISPLAY, SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS,
                                 SERVICE_DEMAND_START, SERVICE_ERROR_IGNORE, L"C:\\setu-oracle\\none.exe", NULL, NULL,
                                 NULL, NULL, NULL);
    SC_HANDLE s2 = CreateServiceW(scm, nodisp, NULL, SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS,
                                  SERVICE_DEMAND_START, SERVICE_ERROR_IGNORE, L"C:\\setu-oracle\\none.exe", NULL, NULL,
                                  NULL, NULL, NULL);
    oracle_case("setup");
    o_handle("a", s);
    o_handle("b", s2);
    oracle_case_end();
    /* "SetuOracle_SvcKeyName_1" is 23 characters, DISPLAY 26. */

    w_case("key_exact", GetServiceKeyNameW, scm, DISPLAY, 1, 300);
    w_case("key_case", GetServiceKeyNameW, scm, L"setuoracle keyname DISPLAY", 1, 300);
    w_case("key_by_keyname", GetServiceKeyNameW, scm, svc, 1, 300);
    w_case("key_missing", GetServiceKeyNameW, scm, L"SetuOracle no such display", 1, 300);
    w_case("key_null_buffer", GetServiceKeyNameW, scm, DISPLAY, 0, 0);
    w_case("key_zero_size", GetServiceKeyNameW, scm, DISPLAY, 1, 0);
    w_case("key_one", GetServiceKeyNameW, scm, DISPLAY, 1, 1);
    w_case("key_len", GetServiceKeyNameW, scm, DISPLAY, 1, 23);
    w_case("key_len_plus_one", GetServiceKeyNameW, scm, DISPLAY, 1, 24);
    w_case("key_missing_null_buffer", GetServiceKeyNameW, scm, L"SetuOracle no such display", 0, 0);
    w_case("key_null_scm", GetServiceKeyNameW, NULL, DISPLAY, 1, 300);
    w_case("key_empty_name", GetServiceKeyNameW, scm, L"", 1, 300);
    w_case("key_no_display", GetServiceKeyNameW, scm, nodisp, 1, 300);

    w_case("display_exact", GetServiceDisplayNameW, scm, svc, 1, 300);
    w_case("display_case", GetServiceDisplayNameW, scm, L"SETUORACLE_SVCKEYNAME_1", 1, 300);
    w_case("display_by_display", GetServiceDisplayNameW, scm, DISPLAY, 1, 300);
    w_case("display_missing", GetServiceDisplayNameW, scm, L"SetuOracleNoSuchKey", 1, 300);
    w_case("display_null_buffer", GetServiceDisplayNameW, scm, svc, 0, 0);
    w_case("display_zero_size", GetServiceDisplayNameW, scm, svc, 1, 0);
    w_case("display_len", GetServiceDisplayNameW, scm, svc, 1, 26);
    w_case("display_len_plus_one", GetServiceDisplayNameW, scm, svc, 1, 27);
    w_case("display_default", GetServiceDisplayNameW, scm, nodisp, 1, 300);
    w_case("display_missing_null_buffer", GetServiceDisplayNameW, scm, L"SetuOracleNoSuchKey", 0, 0);

    a_case("ansi_key_exact", GetServiceKeyNameA, "SetuOracle KeyName Display", 1, 300);
    a_case("ansi_key_null_buffer", GetServiceKeyNameA, "SetuOracle KeyName Display", 0, 0);
    a_case("ansi_key_len", GetServiceKeyNameA, "SetuOracle KeyName Display", 1, 23);
    a_case("ansi_key_len_plus_one", GetServiceKeyNameA, "SetuOracle KeyName Display", 1, 24);
    a_case("ansi_key_missing", GetServiceKeyNameA, "SetuOracle no such display", 1, 300);
    a_case("ansi_display_exact", GetServiceDisplayNameA, "SetuOracle_SvcKeyName_1", 1, 300);
    a_case("ansi_display_null_buffer", GetServiceDisplayNameA, "SetuOracle_SvcKeyName_1", 0, 0);
    a_case("ansi_display_len", GetServiceDisplayNameA, "SetuOracle_SvcKeyName_1", 1, 26);
    a_case("ansi_display_len_plus_one", GetServiceDisplayNameA, "SetuOracle_SvcKeyName_1", 1, 27);
    a_case("ansi_display_missing", GetServiceDisplayNameA, "SetuOracleNoSuchKey", 1, 300);

    /* After the display name changes. */
    ChangeServiceConfigW(s, SERVICE_NO_CHANGE, SERVICE_NO_CHANGE, SERVICE_NO_CHANGE, NULL, NULL, NULL, NULL, NULL,
                         NULL, L"SetuOracle KeyName Renamed");
    w_case("key_old_display_after_change", GetServiceKeyNameW, scm, DISPLAY, 1, 300);
    w_case("key_new_display_after_change", GetServiceKeyNameW, scm, L"SetuOracle KeyName Renamed", 1, 300);

    /* Marked for delete. */
    DeleteService(s);
    w_case("display_marked_for_delete", GetServiceDisplayNameW, scm, svc, 1, 300);
    w_case("key_marked_for_delete", GetServiceKeyNameW, scm, L"SetuOracle KeyName Renamed", 1, 300);

    CloseServiceHandle(s);
    CloseServiceHandle(s2);
    svc_purge(scm, svc);
    svc_purge(scm, nodisp);
    CloseServiceHandle(scm);
    svc_log_remove();
    return oracle_end();
}
