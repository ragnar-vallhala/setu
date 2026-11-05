/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: service and display name rules in CreateServiceW:
 * duplicate key names (exact and differing in case), a key name equal to
 * another service's display name and vice versa, duplicate display names,
 * name lengths around the documented 256-character maximum, characters the
 * docs forbid ('/' and '\\') and other punctuation, spaces, the empty name,
 * and case-insensitive lookups with OpenServiceW. Also what an existing
 * marked-for-delete service does to CreateServiceW with its name.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winsvc/nf-winsvc-createservicew
 *       https://learn.microsoft.com/en-us/windows/win32/api/winsvc/nf-winsvc-openservicew
 */
#include "svc.h"

static SC_HANDLE scm;
#define BIN L"C:\\setu-oracle\\none.exe"
#define OWN SERVICE_WIN32_OWN_PROCESS

static SC_HANDLE create(const wchar_t *name, const wchar_t *display)
{
    return CreateServiceW(scm, name, display, SERVICE_ALL_ACCESS, OWN, SERVICE_DEMAND_START, SERVICE_ERROR_IGNORE,
                          BIN, NULL, NULL, NULL, NULL, NULL);
}

static void drop(SC_HANDLE s)
{
    if (s) {
        DeleteService(s);
        CloseServiceHandle(s);
    }
}

/* Tries to create `name`; prints the result and the stored names. */
static void name_case(const char *cname, const wchar_t *name, const wchar_t *display)
{
    SC_HANDLE s;
    if (name && *name) svc_purge(scm, name);
    ORACLE_CALL(s, create(name, display));
    oracle_case(cname);
    o_handle("ret", s);
    o_lasterr();
    if (s) {
        wchar_t buf[512];
        DWORD n = 512;
        BOOL r = GetServiceKeyNameW(scm, display ? display : name, buf, &n);
        o_bool("keyname_ok", r);
        if (r) o_bool("keyname_matches", !wcscmp(buf, name));
        SC_HANDLE o = OpenServiceW(scm, name, SERVICE_QUERY_STATUS);
        o_handle("reopen", o);
        if (o) CloseServiceHandle(o);
    }
    oracle_case_end();
    drop(s);
    if (s && name && *name) svc_purge(scm, name);
}

int main(int argc, char **argv)
{
    int rc = svc_child_main("advapi32/SvcCreateNames", argc, argv);
    if (rc >= 0) return rc;
    oracle_begin("advapi32/SvcCreateNames");
    scm = svc_scm();
    for (int i = 1; i <= 8; i++) svc_purge(scm, svc_name(i));
    const wchar_t *a = svc_name(1), *b = svc_name(2);

    /* Duplicates. */
    {
        SC_HANDLE s1, s2;
        s1 = create(a, L"SetuOracle display A");
        oracle_case("first");
        o_handle("ret", s1);
        oracle_case_end();

        ORACLE_CALL(s2, create(a, L"SetuOracle display other"));
        oracle_case("dup_name_exact");
        o_handle("ret", s2);
        o_lasterr();
        oracle_case_end();
        drop(s2);

        wchar_t upper[128];
        wcscpy(upper, a);
        _wcsupr(upper);
        ORACLE_CALL(s2, create(upper, L"SetuOracle display other"));
        oracle_case("dup_name_case");
        o_handle("ret", s2);
        o_lasterr();
        oracle_case_end();
        drop(s2);

        ORACLE_CALL(s2, create(b, L"SetuOracle display A"));
        oracle_case("dup_display_exact");
        o_handle("ret", s2);
        o_lasterr();
        oracle_case_end();
        drop(s2);
        svc_purge(scm, b);

        ORACLE_CALL(s2, create(b, L"SETUORACLE DISPLAY a"));
        oracle_case("dup_display_case");
        o_handle("ret", s2);
        o_lasterr();
        oracle_case_end();
        drop(s2);
        svc_purge(scm, b);

        /* New display name equal to an existing key name. */
        ORACLE_CALL(s2, create(b, a));
        oracle_case("display_equals_other_keyname");
        o_handle("ret", s2);
        o_lasterr();
        oracle_case_end();
        drop(s2);
        svc_purge(scm, b);

        /* New key name equal to an existing display name. */
        ORACLE_CALL(s2, create(L"SetuOracle display A", L"SetuOracle display B"));
        oracle_case("keyname_equals_other_display");
        o_handle("ret", s2);
        o_lasterr();
        oracle_case_end();
        drop(s2);
        svc_purge(scm, L"SetuOracle display A");

        /* Display name equal to its own key name is fine. */
        ORACLE_CALL(s2, create(b, b));
        oracle_case("display_equals_own_keyname");
        o_handle("ret", s2);
        o_lasterr();
        oracle_case_end();
        drop(s2);
        svc_purge(scm, b);

        /* Case-insensitive lookups. */
        SC_HANDLE o;
        ORACLE_CALL(o, OpenServiceW(scm, upper, SERVICE_QUERY_STATUS));
        oracle_case("open_upper");
        o_handle("ret", o);
        o_lasterr();
        oracle_case_end();
        if (o) CloseServiceHandle(o);
        ORACLE_CALL(o, OpenServiceW(scm, L"SetuOracle display A", SERVICE_QUERY_STATUS));
        oracle_case("open_by_display_name");
        o_handle("ret", o);
        o_lasterr();
        oracle_case_end();
        if (o) CloseServiceHandle(o);

        /* Key name stored with its original case. */
        {
            wchar_t buf[256];
            DWORD n = 256;
            BOOL r;
            ORACLE_CALL(r, GetServiceKeyNameW(scm, L"setuoracle DISPLAY a", buf, &n));
            oracle_case("keyname_case_preserved");
            o_bool("ret", r);
            o_lasterr();
            if (r) o_wstr("key", buf);
            oracle_case_end();
        }

        /* Marked for delete: the name stays taken until the last handle closes. */
        SC_HANDLE keep = OpenServiceW(scm, a, SERVICE_QUERY_STATUS);
        BOOL r;
        ORACLE_CALL(r, DeleteService(s1));
        CloseServiceHandle(s1);
        oracle_case("delete_while_open");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(s2, create(a, L"SetuOracle display A2"));
        oracle_case("create_over_marked");
        o_handle("ret", s2);
        o_lasterr();
        oracle_case_end();
        drop(s2);
        CloseServiceHandle(keep);
        svc_purge(scm, a);
        ORACLE_CALL(s2, create(a, L"SetuOracle display A"));
        oracle_case("create_after_last_close");
        o_handle("ret", s2);
        o_lasterr();
        oracle_case_end();
        drop(s2);
        svc_purge(scm, a);
    }

    /* Characters and lengths. */
    name_case("name_empty", L"", L"x");
    name_case("name_slash", L"SetuOracle_SvcCreateNames/x", NULL);
    name_case("name_backslash", L"SetuOracle_SvcCreateNames\\x", NULL);
    name_case("name_space", L"SetuOracle SvcCreateNames 3", NULL);
    name_case("name_punct", L"SetuOracle_SvcCreateNames.$-#@!~", NULL);
    name_case("name_colon_star", L"SetuOracle_SvcCreateNames:*?", NULL);
    name_case("name_unicode", L"SetuOracle_SvcCreateNames_\x00e9\x4e2d", NULL);
    name_case("display_slash", svc_name(4), L"SetuOracle a/b\\c");
    {
        wchar_t n[600];
        static const int lens[] = { 255, 256, 257, 300 };
        for (size_t i = 0; i < 4; i++) {
            wcscpy(n, L"SetuOracle_SvcCreateNames_");
            for (size_t j = wcslen(n); j < (size_t)lens[i]; j++) n[j] = L'a' + (j % 26);
            n[lens[i]] = 0;
            char cn[32];
            snprintf(cn, sizeof cn, "name_len_%d", lens[i]);
            name_case(cn, n, NULL);
        }
        static const int dlens[] = { 255, 256, 257, 300 };
        for (size_t i = 0; i < 4; i++) {
            wcscpy(n, L"SetuOracle display ");
            for (size_t j = wcslen(n); j < (size_t)dlens[i]; j++) n[j] = L'a' + (j % 26);
            n[dlens[i]] = 0;
            char cn[32];
            snprintf(cn, sizeof cn, "display_len_%d", dlens[i]);
            name_case(cn, svc_name(5), n);
        }
    }

    for (int i = 1; i <= 8; i++) svc_purge(scm, svc_name(i));
    CloseServiceHandle(scm);
    svc_log_remove();
    return oracle_end();
}
