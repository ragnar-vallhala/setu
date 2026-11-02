/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: GetFullPathNameW path normalisation, plus GetLongPathNameW
 * and GetShortPathNameW error paths.
 *
 * Results are printed with the machine-specific prefix replaced by a token:
 * "<cwd>" (current directory), "<parent>" (its parent) or "<root>" (the
 * current drive's root, e.g. C:\). The drive letter itself is never printed.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getfullpathnamew
 *       https://learn.microsoft.com/en-us/windows/win32/fileio/naming-a-file
 *       https://learn.microsoft.com/en-us/dotnet/standard/io/file-path-formats
 *       https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getlongpathnamew
 *       https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getshortpathnamew
 */
#include "oracle.h"

static wchar_t cwd[MAX_PATH], parent[MAX_PATH], root[8];
static wchar_t drive; /* current drive letter, used to build inputs */

static int has_prefix_ci(const wchar_t *s, const wchar_t *p)
{
    size_t n = wcslen(p);
    return n && !_wcsnicmp(s, p, n) && (s[n] == 0 || s[n] == L'\\' || p[n - 1] == L'\\');
}

/* Replace the machine-specific prefix of an absolute path with a token. */
static const char *tokenise(const wchar_t *s)
{
    static wchar_t out[2 * MAX_PATH];
    const wchar_t *rest = s;
    const wchar_t *tok = L"";
    if (has_prefix_ci(s, cwd)) { tok = L"<cwd>"; rest = s + wcslen(cwd); }
    else if (has_prefix_ci(s, parent)) { tok = L"<parent>"; rest = s + wcslen(parent); }
    else if (has_prefix_ci(s, root)) { tok = L"<root>\\"; rest = s + wcslen(root); }
    else if (s[0] == drive && s[1] == L':') { tok = L"<drive>"; rest = s + 1; }
    swprintf(out, sizeof out / sizeof out[0], L"%ls%ls", tok, rest);
    /* A literal drive letter can still appear after \\?\ or \\.\. */
    for (wchar_t *p = out; p[0] && p[1]; p++)
        if (p[0] == drive && p[1] == L':' && (p == out || p[-1] == L'\\')) *p = L'@';
    return oracle_utf8(out);
}

static void full_case(const char *name, const wchar_t *in)
{
    wchar_t buf[2 * MAX_PATH] = {0}, *part = NULL;
    DWORD ret;
    ORACLE_CALL(ret, GetFullPathNameW(in, sizeof buf / sizeof buf[0], buf, &part));
    oracle_case(name);
    o_lasterr();
    if (ret && ret < sizeof buf / sizeof buf[0]) {
        o_bool("ret_is_len", ret == wcslen(buf));
        o_str("full", tokenise(buf));
        /* The parent's own name is machine-specific: print it as a token. */
        const wchar_t *pname = wcsrchr(parent, L'\\');
        if (part && pname && !_wcsicmp(part, pname + 1)) o_str("file_part", "<parent-name>");
        else if (part) o_wstr("file_part", part);
        else o_str("file_part", NULL);
    } else {
        o_u32("ret", ret);
    }
    oracle_case_end();
}

int main(void)
{
    oracle_begin("kernel32/GetFullPathNameW");

    GetCurrentDirectoryW(MAX_PATH, cwd);
    drive = cwd[0];
    swprintf(root, 8, L"%lc:\\", drive);
    wcscpy(parent, cwd);
    wchar_t *slash = wcsrchr(parent, L'\\');
    if (slash) *slash = 0;

    wchar_t in[MAX_PATH];

    full_case("relative_name", L"a.txt");
    full_case("dot", L".");
    full_case("dotdot", L"..");
    full_case("dot_slash_name", L".\\a");
    full_case("dotdot_name", L"..\\x");
    full_case("collapse_dotdot", L"a\\..\\b");
    full_case("forward_slashes", L"a/b/c");
    full_case("double_separator", L"a\\\\b");
    full_case("trailing_separator", L"a\\");
    full_case("trailing_dot", L"a.");
    full_case("trailing_dots_spaces", L"a. . ");
    full_case("trailing_space", L"a ");
    full_case("dot_in_middle", L"a\\.\\b");
    full_case("three_dots", L"...");
    full_case("dotdot_past_root", L"\\..\\..\\x");
    full_case("rooted", L"\\x\\y");
    swprintf(in, MAX_PATH, L"%lc:x", drive);
    full_case("drive_relative", in);
    swprintf(in, MAX_PATH, L"%lc:", drive);
    full_case("drive_only", in);
    swprintf(in, MAX_PATH, L"%lc:\\a\\..\\..\\b", drive);
    full_case("absolute_dotdot_past_root", in);
    swprintf(in, MAX_PATH, L"\\\\?\\%lc:\\a\\..\\b", drive);
    full_case("verbatim_not_normalised", in);
    swprintf(in, MAX_PATH, L"\\\\.\\%lc:\\a\\..\\b", drive);
    full_case("device_path_normalised", in);
    full_case("unc", L"\\\\server\\share\\a\\..\\b");
    full_case("unc_dotdot_past_share", L"\\\\server\\share\\..\\..\\x");
    full_case("unc_server_only", L"\\\\server");
    full_case("unc_forward_slashes", L"//server/share/x");
    full_case("device_con", L"con");
    full_case("device_nul_ext", L"nul.txt");
    full_case("device_com1_upper", L"COM1");
    full_case("device_lpt1_in_dir", L"sub\\lpt1");
    full_case("device_conin", L"conin$");
    full_case("device_like_prefix", L"console");

    {   /* Buffer sizing: too small returns the size needed including the NUL. */
        wchar_t big[2 * MAX_PATH], small[4];
        DWORD need = GetFullPathNameW(L"abc", 2 * MAX_PATH, big, NULL);
        DWORD ret;
        ORACLE_CALL(ret, GetFullPathNameW(L"abc", 4, small, NULL));
        oracle_case("buffer_too_small");
        o_bool("ret_is_len_plus_nul", ret == need + 1);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(ret, GetFullPathNameW(L"abc", need, big, NULL));
        oracle_case("buffer_exactly_len");
        o_bool("ret_is_len_plus_nul", ret == need + 1);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(ret, GetFullPathNameW(L"abc", need + 1, big, NULL));
        oracle_case("buffer_len_plus_nul");
        o_bool("ret_is_len", ret == need);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(ret, GetFullPathNameW(L"abc", 0, NULL, NULL));
        oracle_case("null_buffer_zero_size");
        o_bool("ret_is_len_plus_nul", ret == need + 1);
        o_lasterr();
        oracle_case_end();
    }

    {   /* GetLongPathNameW / GetShortPathNameW. */
        HANDLE h = CreateFileW(L"short.txt", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        CloseHandle(h);
        CreateDirectoryW(L"sub", NULL);
        wchar_t buf[MAX_PATH];
        DWORD ret;

        ORACLE_CALL(ret, GetLongPathNameW(L"short.txt", buf, MAX_PATH));
        oracle_case("long_existing_relative");
        o_u32("ret", ret);
        o_lasterr();
        if (ret && ret < MAX_PATH) o_wstr("out", buf);
        oracle_case_end();

        ORACLE_CALL(ret, GetLongPathNameW(L"SHORT.TXT", buf, MAX_PATH));
        oracle_case("long_keeps_input_case");
        o_u32("ret", ret);
        o_lasterr();
        if (ret && ret < MAX_PATH) o_wstr("out", buf);
        oracle_case_end();

        ORACLE_CALL(ret, GetLongPathNameW(L"missing.txt", buf, MAX_PATH));
        oracle_case("long_missing");
        o_u32("ret", ret);
        o_lasterr();
        oracle_case_end();

        ORACLE_CALL(ret, GetLongPathNameW(L"nope\\missing.txt", buf, MAX_PATH));
        oracle_case("long_missing_parent");
        o_u32("ret", ret);
        o_lasterr();
        oracle_case_end();

        ORACLE_CALL(ret, GetLongPathNameW(L"sub\\..\\short.txt", buf, MAX_PATH));
        oracle_case("long_with_dotdot");
        o_u32("ret", ret);
        o_lasterr();
        if (ret && ret < MAX_PATH) o_wstr("out", buf);
        oracle_case_end();

        ORACLE_CALL(ret, GetLongPathNameW(L"short.txt", buf, 3));
        oracle_case("long_buffer_too_small");
        o_u32("ret", ret);
        o_lasterr();
        oracle_case_end();

        ORACLE_CALL(ret, GetLongPathNameW(L"", buf, MAX_PATH));
        oracle_case("long_empty");
        o_u32("ret", ret);
        o_lasterr();
        oracle_case_end();

        ORACLE_CALL(ret, GetShortPathNameW(L"short.txt", buf, MAX_PATH));
        oracle_case("short_already_8_3");
        o_u32("ret", ret);
        o_lasterr();
        if (ret && ret < MAX_PATH) o_wstr("out", buf);
        oracle_case_end();

        ORACLE_CALL(ret, GetShortPathNameW(L"missing.txt", buf, MAX_PATH));
        oracle_case("short_missing");
        o_u32("ret", ret);
        o_lasterr();
        oracle_case_end();
    }

    return oracle_end();
}
