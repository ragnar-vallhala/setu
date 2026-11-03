/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: CreateDirectoryExW (template directory attributes, error
 * paths) and the MAX_PATH-12 limit of CreateDirectoryW. Long names are sized
 * relative to the working directory so the total length is the same on
 * every machine.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-createdirectoryexw
 *       https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createdirectoryw
 *       https://learn.microsoft.com/en-us/windows/win32/fileio/maximum-file-path-limitation
 */
#include "oracle.h"

static void mkx_case(const char *name, const wchar_t *tmpl, const wchar_t *dir)
{
    BOOL r;
    ORACLE_CALL(r, CreateDirectoryExW(tmpl, dir, NULL));
    oracle_case(name);
    o_bool("ret", r);
    o_lasterr();
    o_file_state("dir", dir);
    oracle_case_end();
}

static void mk_case(const char *name, const wchar_t *dir)
{
    BOOL r;
    ORACLE_CALL(r, CreateDirectoryW(dir, NULL));
    oracle_case(name);
    o_bool("ret", r);
    o_lasterr();
    o_file_state("dir", dir);
    oracle_case_end();
}

static void make_file(const wchar_t *path)
{
    CloseHandle(CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL));
}

int main(void)
{
    oracle_begin("kernel32/CreateDirectoryExW");
    wchar_t cwd[MAX_PATH], name[MAX_PATH * 2], full[MAX_PATH * 2];
    GetCurrentDirectoryW(MAX_PATH, cwd);

    CreateDirectoryW(L"t_plain", NULL);
    CreateDirectoryW(L"t_hidden", NULL);
    SetFileAttributesW(L"t_hidden", FILE_ATTRIBUTE_HIDDEN);
    CreateDirectoryW(L"t_ro", NULL);
    SetFileAttributesW(L"t_ro", FILE_ATTRIBUTE_READONLY);
    CreateDirectoryW(L"t_sys_arch", NULL);
    SetFileAttributesW(L"t_sys_arch", FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_ARCHIVE);
    CreateDirectoryW(L"t_nci", NULL);
    SetFileAttributesW(L"t_nci", FILE_ATTRIBUTE_NOT_CONTENT_INDEXED);
    CreateDirectoryW(L"t_full", NULL);
    CreateDirectoryW(L"t_full\\child", NULL);
    make_file(L"t_full\\file.txt");
    make_file(L"tfile.txt");
    SetFileAttributesW(L"tfile.txt", FILE_ATTRIBUTE_HIDDEN);

    mkx_case("template_plain", L"t_plain", L"n_plain");
    mkx_case("template_hidden", L"t_hidden", L"n_hidden");
    mkx_case("template_readonly", L"t_ro", L"n_ro");
    mkx_case("template_system_archive", L"t_sys_arch", L"n_sys_arch");
    mkx_case("template_not_content_indexed", L"t_nci", L"n_nci");
    mkx_case("template_with_children", L"t_full", L"n_full");
    oracle_case("children_not_copied");
    o_dir_listing("n_full", L"n_full", 1);
    oracle_case_end();
    mkx_case("template_is_file", L"tfile.txt", L"n_from_file");
    mkx_case("template_missing", L"t_missing", L"n_missing");
    mkx_case("template_missing_parent", L"nodir\\t", L"n_missing2");
    mkx_case("template_empty", L"", L"n_empty_tmpl");
    mkx_case("target_exists", L"t_plain", L"n_plain");
    mkx_case("target_is_existing_file", L"t_plain", L"tfile.txt");
    mkx_case("target_missing_parent", L"t_plain", L"nodir\\x");
    mkx_case("target_empty", L"t_plain", L"");
    mkx_case("target_invalid_char", L"t_plain", L"a<b");
    mkx_case("target_trailing_backslash", L"t_plain", L"n_tb\\");
    mkx_case("target_inside_template", L"t_plain", L"t_plain\\inner");
    mkx_case("template_is_cwd", L".", L"n_from_cwd");
    mkx_case("template_dotdot_path", L"t_full\\..\\t_hidden", L"n_dotdot");
    {
        BOOL r;
        SECURITY_ATTRIBUTES sa = {sizeof sa, NULL, FALSE};
        ORACLE_CALL(r, CreateDirectoryExW(L"t_hidden", L"n_with_sa", &sa));
        oracle_case("template_with_security_attributes");
        o_bool("ret", r);
        o_lasterr();
        o_file_state("dir", L"n_with_sa");
        oracle_case_end();
    }
    {
        BOOL r;
        ORACLE_CALL(r, CreateDirectoryExA("t_ro", "n_ansi", NULL));
        oracle_case("ansi_template_readonly");
        o_bool("ret", r);
        o_lasterr();
        o_file_state("dir", L"n_ansi");
        oracle_case_end();
    }

    /* Length limits: the full path is cwd + "\\" + name. */
    size_t base = wcslen(cwd) + 1;
    static const struct { const char *name; size_t total; } lens[] = {
        {"mkdir_total_246", 246}, {"mkdir_total_247", 247}, {"mkdir_total_248", 248},
        {"mkdir_total_249", 249}, {"mkdir_total_259", 259}, {"mkdir_total_260", 260},
    };
    for (unsigned i = 0; i < sizeof lens / sizeof lens[0]; i++) {
        size_t n = lens[i].total - base;
        for (size_t j = 0; j < n; j++) name[j] = L'a' + (wchar_t)i;
        name[n] = 0;
        BOOL r;
        ORACLE_CALL(r, CreateDirectoryW(name, NULL));
        oracle_case(lens[i].name);
        o_bool("ret", r);
        o_lasterr();
        o_bool("exists", GetFileAttributesW(name) != INVALID_FILE_ATTRIBUTES);
        oracle_case_end();
        if (r) RemoveDirectoryW(name);
    }
    {
        size_t n = 300 - base;
        for (size_t j = 0; j < n; j++) name[j] = L'z';
        name[n] = 0;
        swprintf(full, MAX_PATH * 2, L"\\\\?\\%ls\\%ls", cwd, name);
        BOOL r;
        ORACLE_CALL(r, CreateDirectoryW(full, NULL));
        oracle_case("mkdir_long_prefix_total_300");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, RemoveDirectoryW(full));
        oracle_case("rmdir_long_prefix_total_300");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        name[0] = 0;
        for (int j = 0; j < 256; j++) wcscat(name, L"q");
        ORACLE_CALL(r, CreateDirectoryW(name, NULL));
        oracle_case("mkdir_component_256");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
    }

    mk_case("mkdir_dot_component", L".\\n_dotc");
    mk_case("mkdir_trailing_dot", L"n_trail.");
    mk_case("mkdir_trailing_space", L"n_trail_sp ");
    mk_case("mkdir_device_name", L"con");
    mk_case("mkdir_forward_slashes", L"n_fs/");

    oracle_case("final_listing");
    o_dir_listing("root", L".", 1);
    oracle_case_end();

    return oracle_end();
}
