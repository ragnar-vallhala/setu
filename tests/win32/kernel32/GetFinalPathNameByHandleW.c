/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: GetFinalPathNameByHandleW with every VOLUME_NAME_* x
 * FILE_NAME_* combination. Results are tokenised: the path of the working
 * directory in the same format is replaced by "<cwd>", so only the part
 * this test created is printed. The shape of the working directory path in
 * each format is printed separately.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getfinalpathnamebyhandlew
 */
#include "oracle.h"

static const struct { const char *name; DWORD flags; } fmts[] = {
    {"dos", VOLUME_NAME_DOS},
    {"guid", VOLUME_NAME_GUID},
    {"nt", VOLUME_NAME_NT},
    {"none", VOLUME_NAME_NONE},
    {"dos_opened", VOLUME_NAME_DOS | FILE_NAME_OPENED},
    {"guid_opened", VOLUME_NAME_GUID | FILE_NAME_OPENED},
    {"nt_opened", VOLUME_NAME_NT | FILE_NAME_OPENED},
    {"none_opened", VOLUME_NAME_NONE | FILE_NAME_OPENED},
};
#define NFMT (sizeof fmts / sizeof fmts[0])
static wchar_t base[NFMT][1024];

static void tokenised(const char *key, const wchar_t *s, unsigned f)
{
    size_t bl = wcslen(base[f]);
    wchar_t out[1100];
    if (bl && !_wcsnicmp(s, base[f], bl)) swprintf(out, 1100, L"<cwd>%ls", s + bl);
    else swprintf(out, 1100, L"<other>");
    o_wstr(key, out);
}

/* One case per handle: one field per format. */
static void handle_case(const char *name, HANDLE h)
{
    oracle_case(name);
    if (h == INVALID_HANDLE_VALUE) {
        o_str("open", "failed");
        oracle_case_end();
        return;
    }
    for (unsigned f = 0; f < NFMT; f++) {
        wchar_t buf[1024];
        DWORD r;
        char k[48];
        ORACLE_CALL(r, GetFinalPathNameByHandleW(h, buf, 1024, fmts[f].flags));
        if (r && r < 1024) {
            tokenised(fmts[f].name, buf, f);
            snprintf(k, sizeof k, "%s_len_ok", fmts[f].name);
            o_bool(k, r == wcslen(buf));
        } else {
            snprintf(k, sizeof k, "%s_error", fmts[f].name);
            o_u32(k, oracle_last_error);
        }
    }
    oracle_case_end();
}

static HANDLE op(const wchar_t *p, DWORD flags)
{
    return CreateFileW(p, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                       flags, NULL);
}

static void make_file(const wchar_t *path)
{
    CloseHandle(CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL));
}

int main(void)
{
    oracle_begin("kernel32/GetFinalPathNameByHandleW");
    HANDLE h;

    /* Base: the working directory in every format. */
    h = op(L".", FILE_FLAG_BACKUP_SEMANTICS);
    oracle_case("cwd_shapes");
    for (unsigned f = 0; f < NFMT; f++) {
        char k[48];
        DWORD r;
        ORACLE_CALL(r, GetFinalPathNameByHandleW(h, base[f], 1024, fmts[f].flags));
        if (!r) base[f][0] = 0;
        const wchar_t *b = base[f];
        const char *shape = "other";
        if (!r) shape = "error";
        else if (!wcsncmp(b, L"\\\\?\\Volume{", 11)) shape = "volume_guid";
        else if (!wcsncmp(b, L"\\\\?\\", 4) && b[5] == L':' && b[6] == L'\\') shape = "long_drive";
        else if (!wcsncmp(b, L"\\Device\\", 8)) shape = "device";
        else if (b[0] == L'\\' && !wcschr(b, L':')) shape = "rooted_no_volume";
        snprintf(k, sizeof k, "%s", fmts[f].name);
        o_str(k, shape);
        snprintf(k, sizeof k, "%s_trailing_backslash", fmts[f].name);
        o_bool(k, r && b[wcslen(b) - 1] == L'\\');
    }
    oracle_case_end();
    CloseHandle(h);

    make_file(L"File.txt");
    CreateDirectoryW(L"Dir", NULL);
    make_file(L"Dir\\Inner.dat");
    make_file(L"a long file name with spaces.txt");

    h = op(L"File.txt", 0);
    handle_case("file_exact_case", h);
    CloseHandle(h);
    h = op(L"FILE.TXT", 0);
    handle_case("file_other_case", h);
    CloseHandle(h);
    h = op(L"Dir\\..\\file.txt", 0);
    handle_case("file_via_dotdot", h);
    CloseHandle(h);
    h = op(L"Dir", FILE_FLAG_BACKUP_SEMANTICS);
    handle_case("directory", h);
    CloseHandle(h);
    h = op(L"dir\\", FILE_FLAG_BACKUP_SEMANTICS);
    handle_case("directory_trailing_backslash", h);
    CloseHandle(h);
    h = op(L"dir\\inner.DAT", 0);
    handle_case("nested_other_case", h);
    CloseHandle(h);
    h = op(L"a long file name with spaces.txt", 0);
    handle_case("long_name", h);
    {
        wchar_t sn[MAX_PATH] = L"";
        GetShortPathNameW(L"a long file name with spaces.txt", sn, MAX_PATH);
        HANDLE h2 = op(sn, 0);
        handle_case("opened_by_short_name", h2);
        CloseHandle(h2);
    }
    CloseHandle(h);

    /* Rename and hard link after open. */
    h = op(L"File.txt", 0);
    MoveFileW(L"File.txt", L"Renamed.txt");
    handle_case("after_rename", h);
    CloseHandle(h);
    CreateHardLinkW(L"Link.txt", L"Renamed.txt", NULL);
    h = op(L"Link.txt", 0);
    handle_case("hardlink_opened_by_link", h);
    CloseHandle(h);
    h = CreateFileW(L"Link.txt", GENERIC_READ | DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                    OPEN_EXISTING, FILE_FLAG_DELETE_ON_CLOSE, NULL);
    handle_case("delete_on_close_open", h);
    CloseHandle(h);

    /* Volume root. */
    {
        wchar_t cwd[MAX_PATH], root[MAX_PATH];
        GetCurrentDirectoryW(MAX_PATH, cwd);
        GetVolumePathNameW(cwd, root, MAX_PATH);
        h = op(root, FILE_FLAG_BACKUP_SEMANTICS);
        oracle_case("volume_root");
        for (unsigned f = 0; f < NFMT; f++) {
            wchar_t b[1024];
            char k[48];
            DWORD r;
            ORACLE_CALL(r, GetFinalPathNameByHandleW(h, b, 1024, fmts[f].flags));
            snprintf(k, sizeof k, "%s_ret_ok", fmts[f].name);
            o_bool(k, r != 0);
            snprintf(k, sizeof k, "%s_trailing_backslash", fmts[f].name);
            o_bool(k, r && b[r - 1] == L'\\');
            snprintf(k, sizeof k, "%s_is_cwd_prefix", fmts[f].name);
            o_bool(k, r && !_wcsnicmp(base[f], b, r));
        }
        oracle_case_end();
        CloseHandle(h);
    }

    /* Buffer sizes and flags. */
    h = op(L"Renamed.txt", 0);
    {
        wchar_t b[1024];
        DWORD need, r;
        ORACLE_CALL(need, GetFinalPathNameByHandleW(h, NULL, 0, VOLUME_NAME_DOS));
        oracle_case("size_query_null_buffer");
        o_bool("ret_nonzero", need != 0);
        o_lasterr();
        oracle_case_end();
        DWORD len = GetFinalPathNameByHandleW(h, b, 1024, VOLUME_NAME_DOS);
        oracle_case("size_query_relationship");
        o_bool("need_is_len_plus_1", need == len + 1);
        oracle_case_end();
        wmemset(b, L'#', 1024);
        ORACLE_CALL(r, GetFinalPathNameByHandleW(h, b, len, VOLUME_NAME_DOS));
        oracle_case("buffer_len_exact_no_nul_room");
        o_bool("ret_is_need", r == need);
        o_lasterr();
        o_bool("buffer_untouched", b[0] == L'#');
        oracle_case_end();
        ORACLE_CALL(r, GetFinalPathNameByHandleW(h, b, len + 1, VOLUME_NAME_DOS));
        oracle_case("buffer_len_plus_1");
        o_bool("ret_is_len", r == len);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, GetFinalPathNameByHandleW(h, b, 1024, 0x10));
        oracle_case("invalid_flag_0x10");
        o_u32("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, GetFinalPathNameByHandleW(h, b, 1024, VOLUME_NAME_GUID | VOLUME_NAME_NT));
        oracle_case("invalid_flag_guid_nt");
        o_u32("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, GetFinalPathNameByHandleW(h, b, 1024, 0x8 | 0x4 | 0x2));
        oracle_case("invalid_flag_none_nt");
        o_u32("ret", r);
        o_lasterr();
        oracle_case_end();
        char ab[1024];
        ORACLE_CALL(r, GetFinalPathNameByHandleA(h, ab, sizeof ab, VOLUME_NAME_NONE));
        oracle_case("ansi_none");
        o_bool("ret_nonzero", r != 0);
        o_lasterr();
        o_bool("ends_with_name", r > 12 && !strcmp(ab + r - 12, "\\Renamed.txt"));
        oracle_case_end();
    }
    CloseHandle(h);

    /* Non-file handles. */
    h = op(L"NUL", 0);
    handle_case("nul_device", h);
    CloseHandle(h);
    {
        HANDLE r, w;
        CreatePipe(&r, &w, NULL, 0);
        handle_case("anon_pipe", r);
        CloseHandle(r);
        CloseHandle(w);
        HANDLE ev = CreateEventW(NULL, FALSE, FALSE, NULL);
        handle_case("event", ev);
        CloseHandle(ev);
    }
    handle_case("null_handle", NULL);

    return oracle_end();
}
