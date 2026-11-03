/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: trailing dots and spaces in names, across the file APIs.
 *
 * Win32 path normalisation strips trailing dots and spaces from the last
 * component and from directory components, so "a." and "a " name "a".
 * Programs (and archives extracted by them) hit this with names such as
 * "file.txt.", "dir.\file" or "name ". FindFirstFileW patterns follow other,
 * older rules: "*." matches names without an extension. This test checks
 * creating, opening, renaming, deleting and finding such names, and the
 * resulting names on disk.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/fileio/naming-a-file
 *       https://learn.microsoft.com/en-us/dotnet/standard/io/file-path-formats
 *       https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-findfirstfilew
 */
#include "oracle.h"

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

static void listing(const char *name, const wchar_t *dir)
{
    oracle_case(name);
    o_dir_listing("names", dir, 1);
    oracle_case_end();
}

int main(void)
{
    oracle_begin("kernel32/trailing_dots_spaces");
    BOOL ok;

    /* ---- creating ---- */
    create_case("create_trailing_dot", L"t1.", CREATE_NEW);
    create_case("create_trailing_space", L"t2 ", CREATE_NEW);
    create_case("create_trailing_dot_space_mix", L"t3. . .", CREATE_NEW);
    create_case("create_ext_trailing_dot", L"t4.txt.", CREATE_NEW);
    create_case("create_double_dot_inside", L"t5..txt", CREATE_NEW);
    create_case("create_leading_space", L" lead.txt", CREATE_NEW);
    create_case("create_leading_dot", L".dotfile", CREATE_NEW);
    create_case("create_space_before_ext", L"t6 .txt", CREATE_NEW);
    create_case("create_existing_via_trailing_dot", L"t1..", CREATE_NEW);
    create_case("create_only_space", L" ", CREATE_NEW);
    create_case("create_only_dots", L"...", CREATE_NEW);
    create_case("create_dot_space", L". ", CREATE_NEW);
    listing("listing_after_create", L".");

    /* ---- opening and attributes ---- */
    create_case("open_with_extra_dots", L"t1...", OPEN_EXISTING);
    create_case("open_with_extra_spaces", L"t2   ", OPEN_EXISTING);
    create_case("open_ext_with_trailing_space", L"t4.txt ", OPEN_EXISTING);
    create_case("open_without_inner_dot", L"t5.txt", OPEN_EXISTING);
    attr_case("attr_trailing_dot", L"t1.");
    attr_case("attr_trailing_space_dot", L"t4.txt .");

    /* ---- directories ---- */
    ORACLE_CALL(ok, CreateDirectoryW(L"d1.", NULL));
    bool_case("mkdir_trailing_dot", ok);
    ORACLE_CALL(ok, CreateDirectoryW(L"d2 ", NULL));
    bool_case("mkdir_trailing_space", ok);
    create_case("create_through_dir_with_dot", L"d1.\\in1.txt", CREATE_NEW);
    create_case("create_through_dir_with_space", L"d2 \\in2.txt", CREATE_NEW);
    create_case("create_through_dir_with_dots_spaces", L"d1 . .\\in3.txt", CREATE_NEW);
    attr_case("attr_dir_trailing_dot_sep", L"d1.\\");
    attr_case("attr_dir_trailing_space_sep", L"d2 \\");
    listing("listing_d1", L"d1");
    ORACLE_CALL(ok, SetCurrentDirectoryW(L"d1."));
    bool_case("setcd_trailing_dot", ok);
    {
        wchar_t now[MAX_PATH];
        GetCurrentDirectoryW(MAX_PATH, now);
        const wchar_t *last = wcsrchr(now, L'\\');
        oracle_case("getcd_after_trailing_dot");
        o_wstr("last_component", last ? last + 1 : now);
        oracle_case_end();
        SetCurrentDirectoryW(L"..");
    }

    /* ---- FindFirstFileW patterns ---- */
    find_case("find_exact_trailing_dot", L"t1.");
    find_case("find_exact_trailing_space", L"t2 ");
    find_case("find_star_dot", L"*.");
    find_case("find_t_star_dot", L"t*.");
    find_case("find_star_dot_star", L"t*.*");
    find_case("find_star_txt_dot", L"*.txt.");
    find_case("find_question_dot", L"t?.");
    find_case("find_in_dir_with_dot", L"d1.\\*");
    find_case("find_star_space", L"t* ");

    /* ---- rename and delete ---- */
    ORACLE_CALL(ok, MoveFileW(L"t1.", L"r1."));
    bool_case("move_trailing_dots", ok);
    ORACLE_CALL(ok, MoveFileW(L"t2 ", L"r2 . "));
    bool_case("move_trailing_spaces", ok);
    ORACLE_CALL(ok, CopyFileW(L"t4.txt.", L"c4.txt .", TRUE));
    bool_case("copy_trailing", ok);
    ORACLE_CALL(ok, DeleteFileW(L"r1..."));
    bool_case("delete_trailing_dots", ok);
    ORACLE_CALL(ok, DeleteFileW(L"r2 "));
    bool_case("delete_trailing_space", ok);
    ORACLE_CALL(ok, RemoveDirectoryW(L"d2 . "));
    bool_case("rmdir_nonempty_trailing", ok);
    DeleteFileW(L"d2\\in2.txt");
    ORACLE_CALL(ok, RemoveDirectoryW(L"d2 . "));
    bool_case("rmdir_trailing", ok);

    listing("final_listing", L".");
    return oracle_end();
}
