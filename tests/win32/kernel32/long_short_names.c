/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: 8.3 short names (GetShortPathNameW, GetLongPathNameW and
 * WIN32_FIND_DATAW.cAlternateFileName).
 *
 * Installers and old programs store short paths ("PROGRA~1") and convert
 * them back with GetLongPathNameW; others call GetShortPathNameW to get a
 * path without spaces. This test creates long names in a fresh directory
 * (in a fixed order) and prints the short names Windows reports for them,
 * whether names that already fit 8.3 get one, how short names round-trip,
 * and the buffer-size and error rules.
 *
 * The current directory's own short form is machine-dependent, so outputs
 * use relative inputs or replace the prefix with "<cwd>" / "<cwd-short>".
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getshortpathnamew
 *       https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getlongpathnamew
 *       https://learn.microsoft.com/en-us/windows/win32/api/minwinbase/ns-minwinbase-win32_find_dataw
 *       https://learn.microsoft.com/en-us/windows/win32/fileio/naming-a-file#short-vs-long-names
 */
#include "oracle.h"

static wchar_t cwd[MAX_PATH], cwd_short[MAX_PATH];

static const char *tokenise(const wchar_t *s)
{
    static wchar_t out[2 * MAX_PATH];
    const wchar_t *pre = L"";
    size_t n = wcslen(cwd), ns = wcslen(cwd_short);
    if (!wcsncmp(s, L"\\\\?\\", 4)) {
        pre = L"\\\\?\\";
        s += 4;
    }
    if (!_wcsnicmp(s, cwd, n) && (s[n] == 0 || s[n] == L'\\')) swprintf(out, 2 * MAX_PATH, L"%ls<cwd>%ls", pre, s + n);
    else if (ns && !_wcsnicmp(s, cwd_short, ns) && (s[ns] == 0 || s[ns] == L'\\'))
        swprintf(out, 2 * MAX_PATH, L"%ls<cwd-short>%ls", pre, s + ns);
    else swprintf(out, 2 * MAX_PATH, L"%ls%ls", pre, s);
    return oracle_utf8(out);
}

static void touch(const wchar_t *name)
{
    HANDLE h = CreateFileW(name, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    CloseHandle(h);
}

static void short_case(const char *name, const wchar_t *in)
{
    wchar_t buf[MAX_PATH];
    DWORD r;
    ORACLE_CALL(r, GetShortPathNameW(in, buf, MAX_PATH));
    oracle_case(name);
    o_lasterr();
    if (r && r < MAX_PATH) {
        o_bool("ret_is_len", r == wcslen(buf));
        o_str("short", tokenise(buf));
    } else {
        o_u32("ret", r);
    }
    oracle_case_end();
}

static void long_case(const char *name, const wchar_t *in)
{
    wchar_t buf[MAX_PATH];
    DWORD r;
    ORACLE_CALL(r, GetLongPathNameW(in, buf, MAX_PATH));
    oracle_case(name);
    o_lasterr();
    if (r && r < MAX_PATH) {
        o_bool("ret_is_len", r == wcslen(buf));
        o_str("long", tokenise(buf));
    } else {
        o_u32("ret", r);
    }
    oracle_case_end();
}

/* Short name of each entry in sub-directory "dir", as FindFirstFileW sees it. */
static void find_alt_case(const char *name, const wchar_t *dir)
{
    wchar_t pat[MAX_PATH], *names[64], *alts[64];
    int n = 0;
    WIN32_FIND_DATAW fd;
    swprintf(pat, MAX_PATH, L"%ls\\*", dir);
    HANDLE f = FindFirstFileW(pat, &fd);
    if (f != INVALID_HANDLE_VALUE) {
        do {
            if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
            if (n < 64) {
                names[n] = _wcsdup(fd.cFileName);
                alts[n++] = _wcsdup(fd.cAlternateFileName);
            }
        } while (FindNextFileW(f, &fd));
        FindClose(f);
    }
    /* Sort by long name. */
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            if (wcscmp(names[j], names[i]) < 0) {
                wchar_t *t = names[i]; names[i] = names[j]; names[j] = t;
                t = alts[i]; alts[i] = alts[j]; alts[j] = t;
            }
    /* The list of names, then one case per entry with its short name. */
    oracle_case(name);
    char out[2048] = "";
    for (int i = 0; i < n; i++) {
        strncat(out, oracle_utf8(names[i]), sizeof out - strlen(out) - 1);
        strncat(out, ";", sizeof out - strlen(out) - 1);
    }
    o_str("names", out);
    oracle_case_end();
    for (int i = 0; i < n; i++) {
        char cname[160];
        snprintf(cname, sizeof cname, "%s[%s]", name, oracle_utf8(names[i]));
        oracle_case(cname);
        o_wstr("alt", alts[i]);
        oracle_case_end();
        free(names[i]);
        free(alts[i]);
    }
}

int main(void)
{
    oracle_begin("kernel32/long_short_names");
    GetCurrentDirectoryW(MAX_PATH, cwd);
    GetShortPathNameW(cwd, cwd_short, MAX_PATH);

    /* Created one by one, in this order. */
    CreateDirectoryW(L"n", NULL);
    static const wchar_t *files[] = {
        L"longfilename.txt", L"longfilename2.txt", L"LongFileName3.txt", L"longfilename4.txt",
        L"Mixed Case Name.txt", L"a b.txt", L"x.y.z.txt", L"page.html", L"lower.txt", L"UPPER.TXT",
        L"MiXed.Txt", L"a+b.txt", L"comma,name.txt", L"semi;colon.txt", L"br[1].txt", L"eq=sign.txt",
        L".hidden", L"noext", L"verylongnoext", L"ab.cdef", L"\x00e9t\x00e9.txt", L"trailing.", L"sp ace",
        L"12345678.123", L"123456789.txt",
    };
    for (size_t i = 0; i < sizeof files / sizeof files[0]; i++) {
        wchar_t p[MAX_PATH];
        swprintf(p, MAX_PATH, L"n\\%ls", files[i]);
        touch(p);
    }
    CreateDirectoryW(L"n\\Long Directory Name", NULL);
    touch(L"n\\Long Directory Name\\another long file.txt");
    find_alt_case("find_alternate_names", L"n");

    /* ---- GetShortPathNameW ---- */
    short_case("short_long_name", L"n\\longfilename.txt");
    short_case("short_collision_2", L"n\\longfilename2.txt");
    short_case("short_mixed_case_spaces", L"n\\Mixed Case Name.txt");
    short_case("short_already_8_3_lower", L"n\\lower.txt");
    short_case("short_already_8_3_upper", L"n\\UPPER.TXT");
    short_case("short_dir_and_file", L"n\\Long Directory Name\\another long file.txt");
    short_case("short_dir_trailing_sep", L"n\\Long Directory Name\\");
    short_case("short_input_other_case", L"N\\LONGFILENAME.TXT");
    short_case("short_dot_prefix", L".\\n\\longfilename.txt");
    short_case("short_dotdot_kept", L"n\\..\\n\\longfilename.txt");
    short_case("short_forward_slash", L"n/longfilename.txt");
    short_case("short_missing", L"n\\nonexistent long name.txt");
    short_case("short_missing_dir", L"nodir\\x.txt");
    {
        wchar_t in[MAX_PATH];
        swprintf(in, MAX_PATH, L"%ls\\n\\longfilename.txt", cwd);
        short_case("short_absolute", in);
        swprintf(in, MAX_PATH, L"\\\\?\\%ls\\n\\longfilename.txt", cwd);
        short_case("short_verbatim", in);
    }
    {
        wchar_t buf[MAX_PATH];
        DWORD need, r;
        ORACLE_CALL(need, GetShortPathNameW(L"n\\longfilename.txt", NULL, 0));
        oracle_case("short_size_query");
        o_u32("ret", need);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, GetShortPathNameW(L"n\\longfilename.txt", buf, need - 1));
        oracle_case("short_buffer_len");
        o_u32("ret", r);
        o_lasterr();
        oracle_case_end();
        wchar_t same[MAX_PATH] = L"n\\Mixed Case Name.txt";
        ORACLE_CALL(r, GetShortPathNameW(same, same, MAX_PATH));
        oracle_case("short_in_place");
        o_u32("ret", r);
        o_lasterr();
        o_wstr("out", same);
        oracle_case_end();
    }

    /* ---- GetLongPathNameW from short names ---- */
    {
        wchar_t s[MAX_PATH];
        GetShortPathNameW(L"n\\longfilename.txt", s, MAX_PATH);
        long_case("long_from_short", s);
        GetShortPathNameW(L"n\\Long Directory Name\\another long file.txt", s, MAX_PATH);
        long_case("long_from_short_dir_and_file", s);
        wchar_t abs[MAX_PATH];
        swprintf(abs, MAX_PATH, L"%ls\\n\\Long Directory Name", cwd);
        GetShortPathNameW(abs, s, MAX_PATH);
        long_case("long_from_short_absolute", s);
        long_case("long_from_lowercase_short", L"n\\longfi~1.txt");
        long_case("long_from_bogus_short", L"n\\LONGFI~9.TXT");
        long_case("long_of_long_other_case", L"n\\LONGFILENAME.TXT");
        long_case("long_of_8_3_other_case", L"N\\LOWER.TXT");
        long_case("long_of_8_3_upper_file_lower_input", L"n\\upper.txt");
        long_case("long_of_8_3_dir_other_case", L"N");
        long_case("long_wildcard", L"n\\long*.txt");
        swprintf(abs, MAX_PATH, L"\\\\?\\%ls\\n\\LONGFI~1.TXT", cwd);
        long_case("long_verbatim_short", abs);
    }

    /* ---- opening by short name, renaming, short name after rename ---- */
    {
        wchar_t s[MAX_PATH];
        GetShortPathNameW(L"n\\longfilename4.txt", s, MAX_PATH);
        HANDLE h;
        ORACLE_CALL(h, CreateFileW(s, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL));
        oracle_case("open_by_short_name");
        o_handle("ret", h);
        o_lasterr();
        oracle_case_end();
        CloseHandle(h);
        BOOL ok;
        ORACLE_CALL(ok, MoveFileW(s, L"n\\renamed long file.txt"));
        oracle_case("rename_by_short_name");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        short_case("short_after_rename", L"n\\renamed long file.txt");
        touch(L"n\\longfilename5.txt");
        short_case("short_new_after_free_slot", L"n\\longfilename5.txt");
    }

    find_alt_case("find_alternate_names_final", L"n");
    return oracle_end();
}
