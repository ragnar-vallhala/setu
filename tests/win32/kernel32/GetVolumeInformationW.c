/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: GetVolumeInformationW / GetVolumeInformationByHandleW /
 * GetVolumePathNameW / GetVolumeNameForVolumeMountPointW on the volume that
 * holds the working directory. The serial number, label and volume GUID are
 * machine-specific, so they are tokenised (nonzero, length, shape); the file
 * system name, flags and component length are printed.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getvolumeinformationw
 *       https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getvolumeinformationbyhandlew
 *       https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getvolumepathnamew
 *       https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getvolumenameforvolumemountpointw
 */
#include "oracle.h"

static wchar_t root[MAX_PATH], cwd[MAX_PATH];
static DWORD ref_serial;

static void vi_case(const char *name, const wchar_t *path)
{
    wchar_t label[MAX_PATH + 1] = L"?", fs[MAX_PATH + 1] = L"?";
    DWORD serial = 0, maxc = 0, flags = 0;
    BOOL r;
    ORACLE_CALL(r, GetVolumeInformationW(path, label, MAX_PATH + 1, &serial, &maxc, &flags, fs, MAX_PATH + 1));
    oracle_case(name);
    o_bool("ret", r);
    o_lasterr();
    if (r) {
        o_wstr("fs", fs);
        o_hex32("flags", flags);
        o_u32("max_component", maxc);
        o_bool("serial_nonzero", serial != 0);
        o_bool("serial_matches_ref", serial == ref_serial);
        o_bool("label_terminated", wcslen(label) <= 32);
    }
    oracle_case_end();
}

static void path_case(const char *name, const wchar_t *path, DWORD len)
{
    wchar_t out[MAX_PATH + 1];
    BOOL r;
    wmemset(out, L'#', MAX_PATH + 1);
    ORACLE_CALL(r, GetVolumePathNameW(path, out, len));
    oracle_case(name);
    o_bool("ret", r);
    o_lasterr();
    if (r) {
        o_bool("equals_root", !wcscmp(out, root));
        o_u32("len", (DWORD)wcslen(out));
    }
    oracle_case_end();
}

int main(void)
{
    oracle_begin("kernel32/GetVolumeInformationW");
    wchar_t p[MAX_PATH + 16];
    BOOL r;
    GetCurrentDirectoryW(MAX_PATH, cwd);
    GetVolumePathNameW(cwd, root, MAX_PATH);
    GetVolumeInformationW(root, NULL, 0, &ref_serial, NULL, NULL, NULL, 0);
    CreateDirectoryW(L"d", NULL);
    HANDLE f = CreateFileW(L"f.txt", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    CloseHandle(f);

    vi_case("root", root);
    vi_case("null_root", NULL);
    vi_case("cwd_not_root", cwd);
    swprintf(p, MAX_PATH + 16, L"%ls\\", cwd);
    vi_case("cwd_trailing_backslash", p);
    vi_case("relative_subdir", L"d\\");
    vi_case("relative_file", L"f.txt");
    swprintf(p, MAX_PATH + 16, L"%lc:", root[0]);
    vi_case("drive_no_backslash", p);
    swprintf(p, MAX_PATH + 16, L"%lc:/", root[0]);
    vi_case("drive_forward_slash", p);
    swprintf(p, MAX_PATH + 16, L"\\\\?\\%ls", root);
    vi_case("long_prefix_root", p);
    swprintf(p, MAX_PATH + 16, L"\\\\.\\%ls", root);
    vi_case("dot_prefix_root", p);
    vi_case("unused_drive", L"Q:\\");
    vi_case("empty", L"");
    vi_case("missing_dir", L"nope\\");

    /* Output buffer edge cases. */
    {
        wchar_t fs[16];
        ORACLE_CALL(r, GetVolumeInformationW(root, NULL, 0, NULL, NULL, NULL, NULL, 0));
        oracle_case("all_outputs_null");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        wmemset(fs, L'#', 16);
        ORACLE_CALL(r, GetVolumeInformationW(root, NULL, 0, NULL, NULL, NULL, fs, 4));
        oracle_case("fs_buffer_4");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        wmemset(fs, L'#', 16);
        ORACLE_CALL(r, GetVolumeInformationW(root, NULL, 0, NULL, NULL, NULL, fs, 5));
        oracle_case("fs_buffer_5");
        o_bool("ret", r);
        o_lasterr();
        if (r) o_wstr("fs", fs);
        oracle_case_end();
        ORACLE_CALL(r, GetVolumeInformationW(root, NULL, 0, NULL, NULL, NULL, fs, 0));
        oracle_case("fs_buffer_0");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        char fsa[16] = "";
        DWORD flags = 0;
        ORACLE_CALL(r, GetVolumeInformationA(NULL, NULL, 0, NULL, NULL, &flags, fsa, sizeof fsa));
        oracle_case("ansi_null_root");
        o_bool("ret", r);
        o_lasterr();
        o_str("fs", fsa);
        o_hex32("flags", flags);
        oracle_case_end();
    }

    /* By handle. */
    {
        static const struct { const char *name; const wchar_t *path; DWORD access, flags; } hs[] = {
            {"by_handle_file", L"f.txt", GENERIC_READ, 0},
            {"by_handle_file_no_access", L"f.txt", 0, 0},
            {"by_handle_directory", L"d", GENERIC_READ, FILE_FLAG_BACKUP_SEMANTICS},
            {"by_handle_nul", L"NUL", GENERIC_READ, 0},
        };
        for (unsigned i = 0; i < sizeof hs / sizeof hs[0]; i++) {
            HANDLE h = CreateFileW(hs[i].path, hs[i].access, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                   OPEN_EXISTING, hs[i].flags, NULL);
            wchar_t label[64] = L"?", fs[64] = L"?";
            DWORD serial = 0, maxc = 0, flags = 0;
            ORACLE_CALL(r, GetVolumeInformationByHandleW(h, label, 64, &serial, &maxc, &flags, fs, 64));
            oracle_case(hs[i].name);
            o_bool("ret", r);
            o_lasterr();
            if (r) {
                o_wstr("fs", fs);
                o_hex32("flags", flags);
                o_u32("max_component", maxc);
                o_bool("serial_matches_ref", serial == ref_serial);
            }
            oracle_case_end();
            CloseHandle(h);
        }
        HANDLE rd, wr;
        CreatePipe(&rd, &wr, NULL, 0);
        ORACLE_CALL(r, GetVolumeInformationByHandleW(rd, NULL, 0, NULL, NULL, NULL, NULL, 0));
        oracle_case("by_handle_pipe");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        CloseHandle(rd);
        CloseHandle(wr);
    }

    /* GetVolumePathNameW. */
    path_case("volpath_cwd", cwd, MAX_PATH);
    path_case("volpath_relative_file", L"f.txt", MAX_PATH);
    path_case("volpath_missing_file", L"no\\such\\file.txt", MAX_PATH);
    path_case("volpath_root", root, MAX_PATH);
    path_case("volpath_dot", L".", MAX_PATH);
    path_case("volpath_buffer_4", cwd, 4);
    path_case("volpath_buffer_3", cwd, 3);
    path_case("volpath_buffer_2", cwd, 2);
    path_case("volpath_empty", L"", MAX_PATH);
    path_case("volpath_unused_drive", L"Q:\\x\\y", MAX_PATH);

    /* Volume GUID path of the root. */
    {
        wchar_t vol[MAX_PATH] = L"";
        ORACLE_CALL(r, GetVolumeNameForVolumeMountPointW(root, vol, MAX_PATH));
        oracle_case("volume_guid_root");
        o_bool("ret", r);
        o_lasterr();
        if (r) {
            o_u32("len", (DWORD)wcslen(vol));
            o_bool("prefix", !wcsncmp(vol, L"\\\\?\\Volume{", 11));
            o_bool("trailing_backslash", vol[wcslen(vol) - 1] == L'\\');
        }
        oracle_case_end();
        ORACLE_CALL(r, GetVolumeNameForVolumeMountPointW(cwd, vol, MAX_PATH));
        oracle_case("volume_guid_not_mount_point");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        swprintf(p, MAX_PATH + 16, L"%lc:", root[0]);
        ORACLE_CALL(r, GetVolumeNameForVolumeMountPointW(p, vol, MAX_PATH));
        oracle_case("volume_guid_no_backslash");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, GetVolumeNameForVolumeMountPointW(root, vol, 10));
        oracle_case("volume_guid_small_buffer");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
    }

    return oracle_end();
}
