/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: "\\?\" (verbatim) paths across the file APIs.
 *
 * A "\\?\" path is passed to the file system without normalisation: no "."
 * or ".." collapsing, no '/' to '\' conversion, no stripping of trailing dots
 * and spaces, and no DOS device names. Installers, version control tools and
 * runtimes use it to reach names the normal path rules would change. This
 * test creates and opens such names with CreateFileW, CreateDirectoryW,
 * GetFileAttributesW, FindFirstFileW, MoveFileW, DeleteFileW and
 * RemoveDirectoryW, and checks how the same names behave without the prefix.
 *
 * Paths are built from the current directory; outputs never contain it.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/fileio/naming-a-file
 *       https://learn.microsoft.com/en-us/dotnet/standard/io/file-path-formats
 *       https://learn.microsoft.com/en-us/windows/win32/fileio/maximum-file-path-limitation
 */
#include "oracle.h"

static wchar_t cwd[MAX_PATH];

/* "\\?\<cwd>\<rel>" */
static const wchar_t *V(const wchar_t *rel)
{
    static wchar_t bufs[4][1024];
    static int i;
    wchar_t *b = bufs[i++ & 3];
    swprintf(b, 1024, L"\\\\?\\%ls\\%ls", cwd, rel);
    return b;
}

static void create_case(const char *name, const wchar_t *path, DWORD disp)
{
    HANDLE h;
    ORACLE_CALL(h, CreateFileW(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               NULL, disp, 0, NULL));
    oracle_case(name);
    o_handle("ret", h);
    o_lasterr();
    oracle_case_end();
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
}

static void attr_case(const char *name, const wchar_t *path)
{
    DWORD a;
    ORACLE_CALL(a, GetFileAttributesW(path));
    oracle_case(name);
    o_hex32("ret", a);
    o_lasterr();
    oracle_case_end();
}

static void bool_case(const char *name, BOOL ok)
{
    oracle_case(name);
    o_bool("ret", ok);
    o_lasterr();
    oracle_case_end();
}

static void find_case(const char *name, const wchar_t *pattern)
{
    WIN32_FIND_DATAW fd;
    HANDLE f;
    ORACLE_CALL(f, FindFirstFileW(pattern, &fd));
    oracle_case(name);
    o_handle("ret", f);
    o_lasterr();
    if (f != INVALID_HANDLE_VALUE) {
        char names[1024] = "";
        do {
            if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
            strncat(names, "[", sizeof names - strlen(names) - 1);
            strncat(names, oracle_utf8(fd.cFileName), sizeof names - strlen(names) - 1);
            strncat(names, "]", sizeof names - strlen(names) - 1);
        } while (FindNextFileW(f, &fd));
        FindClose(f);
        o_str("names", names);
    }
    oracle_case_end();
}

/* Listing of the cwd through a verbatim pattern, so odd names show as-is. */
static void listing(const char *name)
{
    wchar_t pat[1024];
    swprintf(pat, 1024, L"\\\\?\\%ls\\*", cwd);
    find_case(name, pat);
}

int main(void)
{
    oracle_begin("kernel32/verbatim_paths");
    GetCurrentDirectoryW(MAX_PATH, cwd);
    BOOL ok;
    wchar_t p[1024];

    /* ---- plain names ---- */
    create_case("create_plain", V(L"plain.txt"), CREATE_NEW);
    attr_case("attr_plain_without_prefix", L"plain.txt");
    attr_case("attr_plain", V(L"plain.txt"));
    attr_case("attr_plain_other_case", V(L"PLAIN.TXT"));
    swprintf(p, 1024, L"\\\\?\\%lc%ls\\plain.txt", towlower(cwd[0]), cwd + 1);
    attr_case("attr_lowercase_drive", p);
    attr_case("attr_cwd_dir", V(L""));
    swprintf(p, 1024, L"\\\\?\\%ls", cwd);
    attr_case("attr_cwd_no_trailing_sep", p);
    swprintf(p, 1024, L"\\\\?\\%lc:\\", cwd[0]);
    attr_case("attr_drive_root", p);

    /* ---- no normalisation ---- */
    CreateDirectoryW(L"sub", NULL);
    create_case("open_dotdot_component", V(L"sub\\..\\plain.txt"), OPEN_EXISTING);
    create_case("open_dot_component", V(L".\\plain.txt"), OPEN_EXISTING);
    create_case("open_forward_slash", V(L"sub/../plain.txt"), OPEN_EXISTING);
    create_case("create_forward_slash_name", V(L"fw/slash.txt"), CREATE_NEW);
    create_case("open_double_separator", V(L"sub\\\\..\\plain.txt"), OPEN_EXISTING);
    create_case("create_in_double_separator", V(L"sub\\\\dbl.txt"), CREATE_NEW);
    attr_case("attr_dotdot_component", V(L"sub\\.."));
    attr_case("attr_dot_component", V(L"."));
    swprintf(p, 1024, L"\\\\?\\plain.txt");
    create_case("relative_after_prefix", p, OPEN_EXISTING);
    swprintf(p, 1024, L"\\\\?\\.\\plain.txt");
    create_case("dot_after_prefix", p, OPEN_EXISTING);
    swprintf(p, 1024, L"\\\\?\\");
    attr_case("prefix_only", p);
    swprintf(p, 1024, L"//?/%ls\\plain.txt", cwd);
    attr_case("forward_slash_prefix", p);
    swprintf(p, 1024, L"\\\\?/%ls\\plain.txt", cwd);
    attr_case("mixed_slash_prefix", p);

    /* ---- trailing dots and spaces are kept ---- */
    create_case("create_trailing_dot", V(L"dot."), CREATE_NEW);
    create_case("create_trailing_space", V(L"space "), CREATE_NEW);
    create_case("create_trailing_dots_spaces", V(L"mix. . "), CREATE_NEW);
    create_case("create_only_dots", V(L"..."), CREATE_NEW);
    create_case("create_only_space", V(L" "), CREATE_NEW);
    listing("listing_after_odd_names");
    attr_case("attr_trailing_dot_verbatim", V(L"dot."));
    attr_case("attr_trailing_dot_normal", L"dot.");
    attr_case("attr_stripped_name_normal", L"dot");
    create_case("open_trailing_dot_normal", L"dot.", OPEN_EXISTING);
    create_case("open_trailing_space_normal", L"space ", OPEN_EXISTING);
    find_case("find_trailing_dot_normal", L"dot.");
    find_case("find_trailing_dot_verbatim", V(L"dot."));
    find_case("find_wildcard_dot_star_normal", L"dot*");
    ORACLE_CALL(ok, DeleteFileW(L"dot."));
    bool_case("delete_trailing_dot_normal", ok);
    ORACLE_CALL(ok, MoveFileW(V(L"space "), V(L"renamed ")));
    bool_case("move_trailing_space_verbatim", ok);
    ORACLE_CALL(ok, MoveFileW(V(L"renamed "), L"renamed2"));
    bool_case("move_verbatim_to_normal", ok);
    ORACLE_CALL(ok, DeleteFileW(V(L"dot.")));
    bool_case("delete_trailing_dot_verbatim", ok);
    ORACLE_CALL(ok, DeleteFileW(V(L"mix. . ")));
    bool_case("delete_trailing_mix_verbatim", ok);
    ORACLE_CALL(ok, DeleteFileW(V(L"...")));
    bool_case("delete_only_dots_verbatim", ok);
    ORACLE_CALL(ok, DeleteFileW(V(L" ")));
    bool_case("delete_only_space_verbatim", ok);

    /* ---- directories ---- */
    ORACLE_CALL(ok, CreateDirectoryW(V(L"vdir"), NULL));
    bool_case("mkdir_plain", ok);
    ORACLE_CALL(ok, CreateDirectoryW(V(L"ddir."), NULL));
    bool_case("mkdir_trailing_dot", ok);
    create_case("create_in_trailing_dot_dir", V(L"ddir.\\inner.txt"), CREATE_NEW);
    attr_case("attr_in_trailing_dot_dir_normal", L"ddir.\\inner.txt");
    ORACLE_CALL(ok, RemoveDirectoryW(V(L"ddir.")));
    bool_case("rmdir_nonempty_trailing_dot", ok);
    DeleteFileW(V(L"ddir.\\inner.txt"));
    ORACLE_CALL(ok, RemoveDirectoryW(L"ddir."));
    bool_case("rmdir_trailing_dot_normal", ok);
    ORACLE_CALL(ok, RemoveDirectoryW(V(L"ddir.")));
    bool_case("rmdir_trailing_dot_verbatim", ok);
    ORACLE_CALL(ok, CreateDirectoryW(V(L"vdir\\..\\up"), NULL));
    bool_case("mkdir_dotdot_component", ok);
    ORACLE_CALL(ok, CreateDirectoryW(V(L"vdir\\"), NULL));
    bool_case("mkdir_existing_trailing_sep", ok);
    ORACLE_CALL(ok, SetCurrentDirectoryW(V(L"vdir")));
    bool_case("setcd_verbatim", ok);
    {
        wchar_t now[1024];
        GetCurrentDirectoryW(1024, now);
        oracle_case("getcd_after_verbatim_setcd");
        o_bool("starts_with_prefix", !wcsncmp(now, L"\\\\?\\", 4));
        o_bool("ends_with_vdir", wcslen(now) >= 5 && !_wcsicmp(now + wcslen(now) - 5, L"\\vdir"));
        oracle_case_end();
        DWORD a;
        ORACLE_CALL(a, GetFileAttributesW(L"..\\plain.txt"));
        oracle_case("relative_attr_under_verbatim_cwd");
        o_hex32("ret", a);
        o_lasterr();
        oracle_case_end();
        SetCurrentDirectoryW(cwd);
    }

    /* ---- long names ---- */
    {
        wchar_t rel[1024] = L"";
        /* Nested directories so the full path is longer than MAX_PATH. */
        for (int i = 0; i < 3; i++) {
            wcscat(rel, i ? L"\\" : L"");
            for (int k = 0; k < 100; k++) wcscat(rel, L"d");
            ok = CreateDirectoryW(V(rel), NULL);
        }
        bool_case("mkdir_deep_past_max_path", ok);
        wcscat(rel, L"\\deep.txt");
        create_case("create_deep_past_max_path", V(rel), CREATE_NEW);
        attr_case("attr_deep_verbatim", V(rel));
        attr_case("attr_deep_normal_relative", rel);
        swprintf(p, 1024, L"%ls\\%ls", cwd, rel);
        attr_case("attr_deep_normal_absolute", p);
        wchar_t comp[300] = L"";
        for (int k = 0; k < 255; k++) wcscat(comp, L"n");
        create_case("create_component_255", V(comp), CREATE_NEW);
        wcscat(comp, L"n");
        create_case("create_component_256", V(comp), CREATE_NEW);
    }

    listing("final_listing");
    return oracle_end();
}
