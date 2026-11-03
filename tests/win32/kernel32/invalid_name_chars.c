/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: characters that are not allowed in file names, and the error
 * codes each API returns for them.
 *
 * '<' '>' '"' '|' '?' '*' and control characters 1-31 are invalid in names;
 * ':' starts an alternate data stream. Programs that sanitise user-typed
 * names, or that check GetLastError() for ERROR_INVALID_NAME versus
 * ERROR_FILE_NOT_FOUND / ERROR_PATH_NOT_FOUND, depend on these codes. In
 * FindFirstFileW patterns '*', '?', and the DOS wildcards '<' '>' '"' are
 * matching characters instead. This test covers CreateFileW, CreateDirectoryW,
 * GetFileAttributesW, DeleteFileW, MoveFileW and FindFirstFileW, with the
 * character in the last component and in a directory component.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/fileio/naming-a-file
 *       https://learn.microsoft.com/en-us/windows/win32/debug/system-error-codes--0-499-
 *       https://learn.microsoft.com/en-us/windows/win32/fileio/file-streams
 *       https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-findfirstfilew
 */
#include "oracle.h"

static void per_char(const char *label, wchar_t c)
{
    wchar_t name[32], inner[32];
    char cname[64];
    swprintf(name, 32, L"a%lcb.txt", c);
    swprintf(inner, 32, L"d%lcd\\f.txt", c);
    HANDLE h;
    DWORD a;
    BOOL ok;

    snprintf(cname, sizeof cname, "%s_create", label);
    ORACLE_CALL(h, CreateFileW(name, GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL));
    oracle_case(cname);
    o_handle("ret", h);
    o_lasterr();
    oracle_case_end();
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);

    snprintf(cname, sizeof cname, "%s_open_existing", label);
    ORACLE_CALL(h, CreateFileW(name, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL));
    oracle_case(cname);
    o_handle("ret", h);
    o_lasterr();
    oracle_case_end();
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);

    snprintf(cname, sizeof cname, "%s_in_dir_component", label);
    ORACLE_CALL(h, CreateFileW(inner, GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL));
    oracle_case(cname);
    o_handle("ret", h);
    o_lasterr();
    oracle_case_end();
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);

    snprintf(cname, sizeof cname, "%s_mkdir", label);
    ORACLE_CALL(ok, CreateDirectoryW(name, NULL));
    oracle_case(cname);
    o_bool("ret", ok);
    o_lasterr();
    oracle_case_end();

    snprintf(cname, sizeof cname, "%s_attr", label);
    ORACLE_CALL(a, GetFileAttributesW(name));
    oracle_case(cname);
    o_hex32("ret", a);
    o_lasterr();
    oracle_case_end();

    snprintf(cname, sizeof cname, "%s_delete", label);
    ORACLE_CALL(ok, DeleteFileW(name));
    oracle_case(cname);
    o_bool("ret", ok);
    o_lasterr();
    oracle_case_end();

    snprintf(cname, sizeof cname, "%s_move_to", label);
    ORACLE_CALL(ok, MoveFileW(L"src.txt", name));
    oracle_case(cname);
    o_bool("ret", ok);
    o_lasterr();
    oracle_case_end();
    if (ok) MoveFileW(name, L"src.txt");
}

static void find_case(const char *name, const wchar_t *pattern)
{
    WIN32_FIND_DATAW fd;
    HANDLE f;
    wchar_t *names[64];
    int n = 0;
    ORACLE_CALL(f, FindFirstFileW(pattern, &fd));
    oracle_case(name);
    o_handle("ret", f);
    o_lasterr();
    if (f != INVALID_HANDLE_VALUE) {
        do {
            if (n < 64) names[n++] = _wcsdup(fd.cFileName);
        } while (FindNextFileW(f, &fd));
        FindClose(f);
        qsort(names, n, sizeof names[0], oracle__wcmp);
        char out[1024] = "";
        for (int i = 0; i < n; i++) {
            strncat(out, "[", sizeof out - strlen(out) - 1);
            strncat(out, oracle_utf8(names[i]), sizeof out - strlen(out) - 1);
            strncat(out, "]", sizeof out - strlen(out) - 1);
            free(names[i]);
        }
        o_str("names", out);
    }
    oracle_case_end();
}

static void touch(const wchar_t *name)
{
    HANDLE h = CreateFileW(name, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    CloseHandle(h);
}

int main(void)
{
    oracle_begin("kernel32/invalid_name_chars");
    touch(L"src.txt");

    per_char("lt", L'<');
    per_char("gt", L'>');
    per_char("quote", L'"');
    per_char("pipe", L'|');
    per_char("question", L'?');
    per_char("star", L'*');
    per_char("ctrl_01", 0x01);
    per_char("ctrl_tab", L'\t');
    per_char("ctrl_1f", 0x1f);
    per_char("del_7f", 0x7f);
    per_char("colon", L':');

    /* ---- colons: alternate data streams ---- */
    {
        HANDLE h;
        touch(L"base.txt");
        ORACLE_CALL(h, CreateFileW(L"base.txt:stream", GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL));
        oracle_case("colon_stream_on_existing_file");
        o_handle("ret", h);
        o_lasterr();
        oracle_case_end();
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        ORACLE_CALL(h, CreateFileW(L"base.txt::$DATA", GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL));
        oracle_case("colon_default_stream");
        o_handle("ret", h);
        o_lasterr();
        oracle_case_end();
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        ORACLE_CALL(h, CreateFileW(L"base.txt:a:b", GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL));
        oracle_case("colon_two_colons_bad_type");
        o_handle("ret", h);
        o_lasterr();
        oracle_case_end();
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        ORACLE_CALL(h, CreateFileW(L":onlystream", GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL));
        oracle_case("colon_leading");
        o_handle("ret", h);
        o_lasterr();
        oracle_case_end();
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        ORACLE_CALL(h, CreateFileW(L"trail:", GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL));
        oracle_case("colon_trailing");
        o_handle("ret", h);
        o_lasterr();
        oracle_case_end();
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        ORACLE_CALL(h, CreateFileW(L"dir:x\\f.txt", GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL));
        oracle_case("colon_stream_name_as_dir");
        o_handle("ret", h);
        o_lasterr();
        oracle_case_end();
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    }

    /* ---- other edge names ---- */
    {
        HANDLE h;
        ORACLE_CALL(h, CreateFileW(L"", GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL));
        oracle_case("empty_name");
        o_handle("ret", h);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(h, CreateFileW(L"sl\\", GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL));
        oracle_case("trailing_backslash_new_file");
        o_handle("ret", h);
        o_lasterr();
        oracle_case_end();
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        ORACLE_CALL(h, CreateFileW(L"\x00e9t\x00e9 \x4e2d.txt", GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL));
        oracle_case("non_ascii_name");
        o_handle("ret", h);
        o_lasterr();
        oracle_case_end();
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        ORACLE_CALL(h, CreateFileW(L"semi;comma,plus+eq=[br]{c}#$%&'@^`~!.txt", GENERIC_WRITE, 0, NULL, CREATE_NEW,
                                   0, NULL));
        oracle_case("punctuation_allowed");
        o_handle("ret", h);
        o_lasterr();
        oracle_case_end();
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    }

    /* ---- FindFirstFileW: wildcards, including the DOS ones ---- */
    touch(L"abc.txt");
    touch(L"abd.txt");
    touch(L"ab.c");
    touch(L"abcd");
    find_case("find_question", L"ab?.txt");
    find_case("find_star", L"ab*");
    find_case("find_dos_star_lt", L"ab<");
    find_case("find_dos_qm_gt", L"ab>.txt");
    find_case("find_dos_dot_quote", L"ab\"c");
    find_case("find_pipe", L"ab|");
    find_case("find_wildcard_in_dir", L"s*\\x.txt");
    find_case("find_ctrl_char", L"ab\x01");

    oracle_case("final_listing");
    o_dir_listing("names", L".", 1);
    oracle_case_end();
    return oracle_end();
}
