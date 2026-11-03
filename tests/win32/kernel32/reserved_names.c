/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: reserved DOS device names (NUL, CON, AUX, PRN, COM1-9,
 * LPT1-9) across the file APIs.
 *
 * In a normal Win32 path these names refer to devices in any directory and
 * with any extension ("sub\nul", "nul.txt", "NUL .txt"), so programs that
 * write to "nul" discard output and programs that create a file with such a
 * name get a device instead. This test checks which spellings are devices,
 * what CreateFileW / GetFileAttributesW / FindFirstFileW / CreateDirectoryW /
 * DeleteFileW / MoveFileW / SetFileAttributesW do with them, and that a
 * "\\?\" path creates a real file of that name.
 *
 * Only NUL is opened as a device; the others are probed with
 * GetFullPathNameW and non-existent port numbers so no hardware is touched.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/fileio/naming-a-file
 *       https://learn.microsoft.com/en-us/dotnet/standard/io/file-path-formats
 *       https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getfullpathnamew
 */
#include "oracle.h"

static wchar_t cwd[MAX_PATH];

static void open_case(const char *name, const wchar_t *path, DWORD disp)
{
    HANDLE h;
    ORACLE_CALL(h, CreateFileW(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, disp, 0,
                               NULL));
    oracle_case(name);
    o_handle("ret", h);
    o_lasterr();
    if (h != INVALID_HANDLE_VALUE) {
        DWORD n = 0;
        o_u32("file_type", GetFileType(h));
        o_bool("write_ok", WriteFile(h, "x", 1, &n, NULL));
        CloseHandle(h);
    }
    oracle_case_end();
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

/* GetFullPathNameW: a device name comes back as "\\.\NAME". */
static void full_case(const char *name, const wchar_t *in)
{
    wchar_t buf[MAX_PATH];
    wchar_t *part = NULL;
    DWORD r;
    ORACLE_CALL(r, GetFullPathNameW(in, MAX_PATH, buf, &part));
    oracle_case(name);
    o_lasterr();
    if (r && r < MAX_PATH) {
        size_t n = wcslen(cwd);
        if (!_wcsnicmp(buf, cwd, n)) {
            wchar_t t[MAX_PATH];
            swprintf(t, MAX_PATH, L"<cwd>%ls", buf + n);
            o_wstr("full", t);
        } else {
            o_wstr("full", buf);
        }
        o_wstr("file_part", part);
    } else {
        o_u32("ret", r);
    }
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
        o_wstr("first", fd.cFileName);
        o_hex32("attrs", fd.dwFileAttributes);
        FindClose(f);
    }
    oracle_case_end();
}

int main(void)
{
    oracle_begin("kernel32/reserved_names");
    GetCurrentDirectoryW(MAX_PATH, cwd);
    CreateDirectoryW(L"sub", NULL);
    BOOL ok;
    wchar_t p[1024];

    /* ---- which spellings are the NUL device ---- */
    open_case("nul", L"nul", OPEN_EXISTING);
    open_case("nul_upper", L"NUL", OPEN_EXISTING);
    open_case("nul_create_new", L"NUL", CREATE_NEW);
    open_case("nul_ext", L"nul.txt", CREATE_ALWAYS);
    open_case("nul_two_exts", L"nul.tar.gz", CREATE_ALWAYS);
    open_case("nul_space_ext", L"nul .txt", CREATE_ALWAYS);
    open_case("nul_trailing_dot", L"nul.", CREATE_ALWAYS);
    open_case("nul_colon", L"nul:", CREATE_ALWAYS);
    open_case("nul_in_subdir", L"sub\\nul", OPEN_EXISTING);
    open_case("nul_in_missing_dir", L"nodir\\nul", OPEN_EXISTING);
    open_case("nul_absolute", L"C:\\nul", OPEN_EXISTING);
    open_case("nul_like_prefix", L"nulx", CREATE_ALWAYS);
    open_case("nul_like_suffix", L"xnul", CREATE_ALWAYS);
    open_case("nul_as_dir_component", L"nul\\x.txt", CREATE_ALWAYS);
    open_case("com9_missing_port", L"COM9", OPEN_EXISTING);
    open_case("com9_ext_missing_port", L"com9.log", CREATE_ALWAYS);
    open_case("lpt9_missing_port", L"LPT9", OPEN_EXISTING);
    open_case("com0_not_device", L"COM0", CREATE_ALWAYS);
    open_case("lpt0_not_device", L"lpt0.txt", CREATE_ALWAYS);
    open_case("com10_not_device", L"com10", CREATE_ALWAYS);
    open_case("conin_like_file", L"conin.txt", CREATE_ALWAYS);
    oracle_case("listing_after_opens");
    o_dir_listing("names", L".", 1);
    oracle_case_end();

    /* ---- GetFullPathNameW for every reserved base name ---- */
    full_case("full_aux", L"aux");
    full_case("full_prn_ext", L"prn.doc");
    full_case("full_con_upper_ext", L"CON.TXT");
    full_case("full_com1_ext", L"com1.txt");
    full_case("full_lpt3_in_dir", L"sub\\lpt3.log");
    full_case("full_com_superscript_1", L"COM\x00b9");
    full_case("full_lpt_superscript_2", L"lpt\x00b2.txt");
    full_case("full_com0", L"COM0");
    full_case("full_nul_trailing_spaces", L"nul   ");
    full_case("full_nul_colon", L"nul:");
    full_case("full_nul_colon_ext", L"nul:.txt");
    full_case("full_conin", L"CONIN$");
    full_case("full_conout_ext", L"conout$.txt");
    full_case("full_clock", L"clock$");
    full_case("full_trailing_sep", L"nul\\");

    /* ---- other APIs on device names ---- */
    attr_case("attr_nul", L"nul");
    attr_case("attr_nul_ext", L"nul.txt");
    attr_case("attr_com9", L"COM9");
    attr_case("attr_aux_in_subdir", L"sub\\aux");
    find_case("find_nul", L"nul");
    find_case("find_nul_ext", L"nul.txt");
    find_case("find_nul_wildcard", L"nul*");
    ORACLE_CALL(ok, CreateDirectoryW(L"nul", NULL));
    bool_case("mkdir_nul", ok);
    ORACLE_CALL(ok, CreateDirectoryW(L"con", NULL));
    bool_case("mkdir_con", ok);
    ORACLE_CALL(ok, CreateDirectoryW(L"aux.dir", NULL));
    bool_case("mkdir_aux_ext", ok);
    ORACLE_CALL(ok, DeleteFileW(L"nul"));
    bool_case("delete_nul", ok);
    ORACLE_CALL(ok, DeleteFileW(L"nul.txt"));
    bool_case("delete_nul_ext", ok);
    ORACLE_CALL(ok, RemoveDirectoryW(L"nul"));
    bool_case("rmdir_nul", ok);
    ORACLE_CALL(ok, SetFileAttributesW(L"nul", FILE_ATTRIBUTE_HIDDEN));
    bool_case("setattr_nul", ok);
    {
        HANDLE h = CreateFileW(L"src.txt", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        CloseHandle(h);
    }
    ORACLE_CALL(ok, MoveFileW(L"src.txt", L"nul"));
    bool_case("move_to_nul", ok);
    ORACLE_CALL(ok, MoveFileW(L"src.txt", L"aux.txt"));
    bool_case("move_to_aux_ext", ok);
    ORACLE_CALL(ok, CopyFileW(L"src.txt", L"nul", FALSE));
    bool_case("copy_to_nul", ok);
    ORACLE_CALL(ok, CopyFileW(L"nul", L"fromnul.txt", FALSE));
    bool_case("copy_from_nul", ok);
    attr_case("copy_from_nul_result", L"fromnul.txt");
    ORACLE_CALL(ok, SetCurrentDirectoryW(L"nul"));
    bool_case("setcd_nul", ok);
    SetCurrentDirectoryW(cwd);

    /* ---- a real file named nul through \\?\ ---- */
    swprintf(p, 1024, L"\\\\?\\%ls\\nul", cwd);
    open_case("verbatim_create_nul_file", p, CREATE_NEW);
    swprintf(p, 1024, L"\\\\?\\%ls\\con.txt", cwd);
    open_case("verbatim_create_con_txt_file", p, CREATE_NEW);
    attr_case("nul_still_device_after_file", L"nul");
    find_case("find_nul_after_file", L"nul");
    find_case("find_wildcard_sees_nul_file", L"n*");
    open_case("open_nul_after_file", L"nul", OPEN_EXISTING);
    attr_case("nul_file_size_untouched_via_normal_path", L"con.txt");
    swprintf(p, 1024, L"\\\\?\\%ls\\nul", cwd);
    attr_case("verbatim_attr_nul_file", p);
    ORACLE_CALL(ok, DeleteFileW(p));
    bool_case("verbatim_delete_nul_file", ok);
    swprintf(p, 1024, L"\\\\?\\%ls\\con.txt", cwd);
    ORACLE_CALL(ok, DeleteFileW(p));
    bool_case("verbatim_delete_con_txt_file", ok);

    oracle_case("final_listing");
    o_dir_listing("names", L".", 1);
    oracle_case_end();
    return oracle_end();
}
