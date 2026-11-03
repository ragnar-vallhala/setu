/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: "\\.\" (Win32 device namespace) paths.
 *
 * Unlike "\\?\", a "\\.\" path is still normalised ("." and ".." collapse,
 * '/' becomes '\', trailing dots and spaces go), but it names the device
 * namespace directly: "\\.\C:" is the volume, "\\.\NUL" the null device,
 * "\\.\pipe\x" a named pipe. Disk tools open "\\.\C:"; programs reach drive
 * paths and devices through it. This test checks files reached through
 * "\\.\<drive>:\...", volume and device opens, and what file APIs do with
 * device paths.
 *
 * The drive letter and current directory never appear in the output.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/fileio/naming-a-file
 *       https://learn.microsoft.com/en-us/dotnet/standard/io/file-path-formats
 *       https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew
 *       https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getfiletype
 */
#include "oracle.h"

static wchar_t cwd[MAX_PATH];

/* "\\.\<cwd>\<rel>" */
static const wchar_t *D(const wchar_t *rel)
{
    static wchar_t bufs[4][1024];
    static int i;
    wchar_t *b = bufs[i++ & 3];
    swprintf(b, 1024, L"\\\\.\\%ls\\%ls", cwd, rel);
    return b;
}

static void open_case(const char *name, const wchar_t *path, DWORD access, DWORD disp, DWORD flags)
{
    HANDLE h;
    ORACLE_CALL(h, CreateFileW(path, access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, disp, flags,
                               NULL));
    oracle_case(name);
    o_handle("ret", h);
    o_lasterr();
    if (h != INVALID_HANDLE_VALUE) {
        o_u32("file_type", GetFileType(h));
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

static void find_case(const char *name, const wchar_t *pattern)
{
    WIN32_FIND_DATAW fd;
    HANDLE f;
    ORACLE_CALL(f, FindFirstFileW(pattern, &fd));
    oracle_case(name);
    o_handle("ret", f);
    o_lasterr();
    if (f != INVALID_HANDLE_VALUE) {
        char names[512] = "";
        do {
            strncat(names, "[", sizeof names - strlen(names) - 1);
            strncat(names, oracle_utf8(fd.cFileName), sizeof names - strlen(names) - 1);
            strncat(names, "]", sizeof names - strlen(names) - 1);
        } while (FindNextFileW(f, &fd));
        FindClose(f);
        o_str("names", names);
    }
    oracle_case_end();
}

int main(void)
{
    oracle_begin("kernel32/device_namespace_paths");
    GetCurrentDirectoryW(MAX_PATH, cwd);
    wchar_t p[1024];
    BOOL ok;
    const DWORD RW = GENERIC_READ | GENERIC_WRITE;

    /* ---- files through \\.\<drive>:\ ---- */
    open_case("create_file", D(L"a.txt"), RW, CREATE_NEW, 0);
    open_case("open_file", D(L"a.txt"), GENERIC_READ, OPEN_EXISTING, 0);
    CreateDirectoryW(L"sub", NULL);
    open_case("open_dotdot_normalised", D(L"sub\\..\\a.txt"), GENERIC_READ, OPEN_EXISTING, 0);
    open_case("open_forward_slashes", D(L"sub/../a.txt"), GENERIC_READ, OPEN_EXISTING, 0);
    open_case("open_trailing_dot_stripped", D(L"a.txt."), GENERIC_READ, OPEN_EXISTING, 0);
    open_case("open_trailing_space_stripped", D(L"a.txt "), GENERIC_READ, OPEN_EXISTING, 0);
    open_case("open_missing", D(L"missing.txt"), GENERIC_READ, OPEN_EXISTING, 0);
    open_case("open_missing_dir", D(L"nodir\\x.txt"), GENERIC_READ, OPEN_EXISTING, 0);
    attr_case("attr_file", D(L"a.txt"));
    attr_case("attr_dir", D(L"sub"));
    find_case("find_file", D(L"a.txt"));
    find_case("find_wildcard", D(L"*.txt"));
    ORACLE_CALL(ok, CreateDirectoryW(D(L"newdir"), NULL));
    bool_case("mkdir", ok);
    ORACLE_CALL(ok, MoveFileW(D(L"a.txt"), D(L"b.txt")));
    bool_case("move", ok);
    ORACLE_CALL(ok, DeleteFileW(D(L"b.txt")));
    bool_case("delete", ok);
    ORACLE_CALL(ok, RemoveDirectoryW(D(L"newdir")));
    bool_case("rmdir", ok);
    swprintf(p, 1024, L"//./%ls/fw.txt", cwd);
    open_case("create_forward_slash_prefix", p, RW, CREATE_NEW, 0);
    swprintf(p, 1024, L"\\\\.\\%ls\\..\\..\\..\\..\\..\\..\\x.txt", cwd);
    attr_case("attr_dotdot_past_root", p);

    /* ---- volumes ---- */
    swprintf(p, 1024, L"\\\\.\\%lc:", cwd[0]);
    open_case("volume_zero_access", p, 0, OPEN_EXISTING, 0);
    attr_case("volume_attr", p);
    swprintf(p, 1024, L"\\\\.\\%lc:\\", cwd[0]);
    attr_case("volume_root_attr", p);
    find_case("volume_root_find_no_pattern", p);
    swprintf(p, 1024, L"\\\\.\\%lc:\\*", cwd[0]);
    {
        WIN32_FIND_DATAW fd;
        HANDLE f;
        ORACLE_CALL(f, FindFirstFileW(p, &fd));
        oracle_case("volume_root_find_star");
        o_handle("ret", f);
        o_lasterr();
        oracle_case_end();
        if (f != INVALID_HANDLE_VALUE) FindClose(f);
    }
    open_case("bogus_volume_letter_dev", L"\\\\.\\setu_no_such_dev", 0, OPEN_EXISTING, 0);

    /* ---- devices ---- */
    open_case("nul_device", L"\\\\.\\NUL", RW, OPEN_EXISTING, 0);
    open_case("nul_device_lowercase", L"\\\\.\\nul", RW, OPEN_EXISTING, 0);
    open_case("nul_device_create_always", L"\\\\.\\NUL", RW, CREATE_ALWAYS, 0);
    open_case("nul_device_trailing_sep", L"\\\\.\\NUL\\", RW, OPEN_EXISTING, 0);
    open_case("nul_device_subpath", L"\\\\.\\NUL\\x", RW, OPEN_EXISTING, 0);
    open_case("nul_device_with_ext", L"\\\\.\\NUL.txt", RW, OPEN_EXISTING, 0);
    attr_case("nul_device_attr", L"\\\\.\\NUL");
    find_case("nul_device_find", L"\\\\.\\NUL");
    ORACLE_CALL(ok, DeleteFileW(L"\\\\.\\NUL"));
    bool_case("nul_device_delete", ok);
    ORACLE_CALL(ok, CreateDirectoryW(L"\\\\.\\NUL", NULL));
    bool_case("nul_device_mkdir", ok);
    open_case("verbatim_nul", L"\\\\?\\NUL", RW, OPEN_EXISTING, 0);
    open_case("globalroot_null", L"\\\\?\\GLOBALROOT\\Device\\Null", RW, OPEN_EXISTING, 0);
    open_case("com9_missing", L"\\\\.\\COM9", RW, OPEN_EXISTING, 0);
    open_case("lpt9_missing", L"\\\\.\\LPT9", RW, OPEN_EXISTING, 0);

    /* ---- pipes and mailslots ---- */
    open_case("pipe_missing", L"\\\\.\\pipe\\setu_oracle_no_pipe", GENERIC_READ, OPEN_EXISTING, 0);
    open_case("pipe_root", L"\\\\.\\pipe\\", 0, OPEN_EXISTING, 0);
    open_case("mailslot_missing", L"\\\\.\\mailslot\\setu_oracle_no_slot", GENERIC_WRITE, OPEN_EXISTING, 0);
    {
        HANDLE srv = CreateNamedPipeW(L"\\\\.\\pipe\\setu_oracle_dev", PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE, 1, 64, 64, 0,
                                      NULL);
        open_case("pipe_existing", L"\\\\.\\pipe\\setu_oracle_dev", RW, OPEN_EXISTING, 0);
        CloseHandle(srv);
        srv = CreateNamedPipeW(L"\\\\.\\pipe\\setu_oracle_dev", PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE, 1, 64, 64, 0, NULL);
        open_case("pipe_existing_other_case", L"\\\\.\\PIPE\\SETU_ORACLE_DEV", RW, OPEN_EXISTING, 0);
        CloseHandle(srv);
        srv = CreateNamedPipeW(L"\\\\.\\pipe\\setu_oracle_dev", PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE, 1, 64, 64, 0, NULL);
        open_case("pipe_forward_slashes", L"//./pipe/setu_oracle_dev", RW, OPEN_EXISTING, 0);
        CloseHandle(srv);
        srv = CreateNamedPipeW(L"\\\\.\\pipe\\setu_oracle_dev", PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE, 1, 64, 64, 0, NULL);
        open_case("pipe_verbatim_prefix", L"\\\\?\\pipe\\setu_oracle_dev", RW, OPEN_EXISTING, 0);
        CloseHandle(srv);
        srv = CreateNamedPipeW(L"\\\\.\\pipe\\setu_oracle_dev", PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE, 1, 64, 64, 0, NULL);
        attr_case("pipe_attr", L"\\\\.\\pipe\\setu_oracle_dev");
        CloseHandle(srv);
    }

    /* ---- the bare prefixes ---- */
    attr_case("bare_prefix", L"\\\\.\\");
    attr_case("bare_prefix_no_sep", L"\\\\.");
    open_case("bare_prefix_open", L"\\\\.\\", 0, OPEN_EXISTING, 0);

    return oracle_end();
}
