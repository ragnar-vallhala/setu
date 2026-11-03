/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: SetFileInformationByHandle with FileDispositionInfoEx (DELETE,
 * POSIX_SEMANTICS, ON_CLOSE, IGNORE_READONLY_ATTRIBUTE flags) and
 * FileRenameInfoEx (REPLACE_IF_EXISTS, POSIX_SEMANTICS,
 * IGNORE_READONLY_ATTRIBUTE). The plain FileDispositionInfo / FileRenameInfo
 * classes are covered by file_info_by_handle.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-setfileinformationbyhandle
 *       https://learn.microsoft.com/en-us/windows/win32/api/winbase/ns-winbase-file_disposition_info_ex
 *       https://learn.microsoft.com/en-us/windows/win32/api/winbase/ns-winbase-file_rename_info
 *       https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntddk/ns-ntddk-_file_disposition_information_ex
 */
#include "oracle.h"

#define DISP_DO_NOT_DELETE 0x0
#define DISP_DELETE 0x1
#define DISP_POSIX 0x2
#define DISP_FORCE_IMAGE_CHECK 0x4
#define DISP_ON_CLOSE 0x8
#define DISP_IGNORE_RO 0x10

#define REN_REPLACE 0x1
#define REN_POSIX 0x2
#define REN_IGNORE_RO 0x40

typedef struct { DWORD Flags; } disp_ex;
typedef struct {
    DWORD Flags;
    HANDLE RootDirectory;
    DWORD FileNameLength;
    WCHAR FileName[MAX_PATH];
} rename_ex;

static const DWORD SHARE_ALL = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;

static void make_file(const wchar_t *path, const char *content)
{
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    DWORD w;
    WriteFile(h, content, (DWORD)strlen(content), &w, NULL);
    CloseHandle(h);
}

static HANDLE op(const wchar_t *p, DWORD access)
{
    return CreateFileW(p, access, SHARE_ALL, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
}

static BOOL set_disp(const char *name, HANDLE h, DWORD flags, const wchar_t *state)
{
    disp_ex d = {flags};
    BOOL r;
    ORACLE_CALL(r, SetFileInformationByHandle(h, (FILE_INFO_BY_HANDLE_CLASS)21 /* FileDispositionInfoEx */, &d, sizeof d));
    oracle_case(name);
    o_bool("ret", r);
    o_lasterr();
    if (state) {
        o_file_state("file", state);
        o_dir_listing("listing", L".", 1);
    }
    oracle_case_end();
    return r;
}

static void state_case(const char *name, const wchar_t *path)
{
    oracle_case(name);
    o_file_state("file", path);
    o_dir_listing("listing", L".", 1);
    oracle_case_end();
}

static void set_rename(const char *name, HANDLE h, DWORD flags, const wchar_t *target)
{
    rename_ex r;
    BOOL ok;
    memset(&r, 0, sizeof r);
    r.Flags = flags;
    r.FileNameLength = (DWORD)(wcslen(target) * sizeof(WCHAR));
    wcscpy(r.FileName, target);
    ORACLE_CALL(ok, SetFileInformationByHandle(h, (FILE_INFO_BY_HANDLE_CLASS)22 /* FileRenameInfoEx */, &r, sizeof r));
    oracle_case(name);
    o_bool("ret", ok);
    o_lasterr();
    o_dir_listing("listing", L".", 1);
    oracle_case_end();
}

int main(void)
{
    oracle_begin("kernel32/FileDispositionInfoEx");
    HANDLE h, h2;
    CreateDirectoryW(L"keep", NULL); /* the listing is never empty */

    /* Classic semantics through the Ex class: name stays until last close. */
    make_file(L"a.txt", "a");
    h = op(L"a.txt", DELETE);
    h2 = op(L"a.txt", GENERIC_READ);
    set_disp("delete_classic", h, DISP_DELETE, L"a.txt");
    CloseHandle(h);
    state_case("classic_after_first_close", L"a.txt");
    CloseHandle(h2);
    state_case("classic_after_last_close", L"a.txt");

    /* POSIX semantics: the name goes away at once, other handles keep working. */
    make_file(L"p.txt", "posix");
    h = op(L"p.txt", DELETE);
    h2 = op(L"p.txt", GENERIC_READ | GENERIC_WRITE);
    set_disp("delete_posix", h, DISP_DELETE | DISP_POSIX, L"p.txt");
    {
        HANDLE h3;
        ORACLE_CALL(h3, CreateFileW(L"p.txt", GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL));
        oracle_case("posix_recreate_same_name");
        o_handle("ret", h3);
        o_lasterr();
        oracle_case_end();
        if (h3 != INVALID_HANDLE_VALUE) CloseHandle(h3);
        char buf[16] = "";
        DWORD n = 0;
        BOOL r;
        ORACLE_CALL(r, ReadFile(h2, buf, sizeof buf - 1, &n, NULL));
        oracle_case("posix_other_handle_reads");
        o_bool("ret", r);
        o_lasterr();
        o_bytes("data", buf, n);
        oracle_case_end();
        ORACLE_CALL(r, WriteFile(h2, "!", 1, &n, NULL));
        oracle_case("posix_other_handle_writes");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        FILE_STANDARD_INFO si;
        ORACLE_CALL(r, GetFileInformationByHandleEx(h2, FileStandardInfo, &si, sizeof si));
        oracle_case("posix_standard_info");
        o_bool("ret", r);
        o_bool("delete_pending", si.DeletePending);
        o_u32("links", si.NumberOfLinks);
        oracle_case_end();
    }
    CloseHandle(h);
    CloseHandle(h2);
    state_case("posix_after_close", L"p.txt");
    DeleteFileW(L"p.txt");

    /* Clearing. */
    make_file(L"c.txt", "c");
    h = op(L"c.txt", DELETE);
    set_disp("set_then", h, DISP_DELETE, NULL);
    set_disp("clear_with_do_not_delete", h, DISP_DO_NOT_DELETE, L"c.txt");
    CloseHandle(h);
    state_case("cleared_file_kept", L"c.txt");
    h = op(L"c.txt", DELETE);
    set_disp("posix_set", h, DISP_DELETE | DISP_POSIX, NULL);
    set_disp("posix_clear_after", h, DISP_DO_NOT_DELETE, L"c.txt");
    CloseHandle(h);
    state_case("posix_clear_after_close", L"c.txt");

    /* Read-only files. */
    make_file(L"ro.txt", "ro");
    SetFileAttributesW(L"ro.txt", FILE_ATTRIBUTE_READONLY);
    h = op(L"ro.txt", DELETE);
    set_disp("readonly_plain", h, DISP_DELETE, L"ro.txt");
    set_disp("readonly_ignore_ro", h, DISP_DELETE | DISP_IGNORE_RO, L"ro.txt");
    CloseHandle(h);
    state_case("readonly_after_close", L"ro.txt");
    make_file(L"ro2.txt", "ro");
    SetFileAttributesW(L"ro2.txt", FILE_ATTRIBUTE_READONLY);
    h = op(L"ro2.txt", DELETE);
    set_disp("readonly_posix_ignore_ro", h, DISP_DELETE | DISP_POSIX | DISP_IGNORE_RO, L"ro2.txt");
    CloseHandle(h);

    /* ON_CLOSE: toggles FILE_DELETE_ON_CLOSE; only valid on handles opened with it. */
    make_file(L"oc.txt", "oc");
    h = op(L"oc.txt", DELETE);
    set_disp("on_close_without_doc_open", h, DISP_DELETE | DISP_ON_CLOSE, L"oc.txt");
    CloseHandle(h);
    state_case("on_close_without_doc_after", L"oc.txt");
    make_file(L"oc2.txt", "oc");
    h = CreateFileW(L"oc2.txt", DELETE, SHARE_ALL, NULL, OPEN_EXISTING, FILE_FLAG_DELETE_ON_CLOSE, NULL);
    set_disp("on_close_clear_doc", h, DISP_DO_NOT_DELETE | DISP_ON_CLOSE, L"oc2.txt");
    CloseHandle(h);
    state_case("on_close_clear_doc_after", L"oc2.txt");

    /* Flag validation and access. */
    make_file(L"v.txt", "v");
    h = op(L"v.txt", DELETE);
    set_disp("unknown_flag_0x100", h, DISP_DELETE | 0x100, L"v.txt");
    set_disp("force_image_section_check", h, DISP_DELETE | DISP_FORCE_IMAGE_CHECK, L"v.txt");
    {
        DWORD small = DISP_DELETE;
        BOOL r;
        ORACLE_CALL(r, SetFileInformationByHandle(h, (FILE_INFO_BY_HANDLE_CLASS)21 /* FileDispositionInfoEx */, &small, 2));
        oracle_case("short_buffer");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
    }
    CloseHandle(h);
    make_file(L"v2.txt", "v");
    h = op(L"v2.txt", GENERIC_READ);
    set_disp("without_delete_access", h, DISP_DELETE | DISP_POSIX, L"v2.txt");
    CloseHandle(h);

    /* Directories. */
    CreateDirectoryW(L"ed", NULL);
    h = op(L"ed", DELETE);
    set_disp("posix_empty_directory", h, DISP_DELETE | DISP_POSIX, L"ed");
    {
        BOOL r;
        ORACLE_CALL(r, CreateDirectoryW(L"ed", NULL));
        oracle_case("posix_recreate_directory");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
    }
    CloseHandle(h);
    RemoveDirectoryW(L"ed");
    CreateDirectoryW(L"nd", NULL);
    make_file(L"nd\\x", "x");
    h = op(L"nd", DELETE);
    set_disp("posix_nonempty_directory", h, DISP_DELETE | DISP_POSIX, L"nd");
    CloseHandle(h);

    /* Delete with an open mapped section. */
    make_file(L"m.txt", "mapped");
    {
        HANDLE f = CreateFileW(L"m.txt", GENERIC_READ | DELETE, SHARE_ALL, NULL, OPEN_EXISTING, 0, NULL);
        HANDLE map = CreateFileMappingW(f, NULL, PAGE_READONLY, 0, 0, NULL);
        void *v = MapViewOfFile(map, FILE_MAP_READ, 0, 0, 0);
        set_disp("posix_with_mapped_view", f, DISP_DELETE | DISP_POSIX, L"m.txt");
        UnmapViewOfFile(v);
        CloseHandle(map);
        CloseHandle(f);
        state_case("mapped_after_close", L"m.txt");
    }

    /* FileRenameInfoEx. */
    make_file(L"r1.txt", "r1");
    make_file(L"r2.txt", "r2");
    h = op(L"r1.txt", DELETE);
    set_rename("rename_ex_no_replace", h, 0, L"r2.txt");
    set_rename("rename_ex_replace", h, REN_REPLACE, L"r2.txt");
    CloseHandle(h);
    make_file(L"r3.txt", "r3");
    h = op(L"r3.txt", DELETE);
    h2 = op(L"r2.txt", GENERIC_READ);
    set_rename("rename_ex_replace_target_open", h, REN_REPLACE, L"r2.txt");
    set_rename("rename_ex_posix_replace_target_open", h, REN_REPLACE | REN_POSIX, L"r2.txt");
    {
        char buf[8] = "";
        DWORD n = 0;
        SetFilePointer(h2, 0, NULL, FILE_BEGIN);
        ReadFile(h2, buf, sizeof buf, &n, NULL);
        oracle_case("replaced_target_handle_still_reads");
        o_bytes("data", buf, n);
        oracle_case_end();
    }
    CloseHandle(h2);
    CloseHandle(h);
    make_file(L"r4.txt", "r4");
    make_file(L"rro.txt", "ro");
    SetFileAttributesW(L"rro.txt", FILE_ATTRIBUTE_READONLY);
    h = op(L"r4.txt", DELETE);
    set_rename("rename_ex_replace_readonly", h, REN_REPLACE, L"rro.txt");
    set_rename("rename_ex_replace_readonly_ignore", h, REN_REPLACE | REN_IGNORE_RO, L"rro.txt");
    set_rename("rename_ex_unknown_flag", h, 0x1000, L"r5.txt");
    CloseHandle(h);
    {
        oracle_case("rename_final_contents");
        o_file_state("r2", L"r2.txt");
        o_file_state("rro", L"rro.txt");
        oracle_case_end();
    }

    return oracle_end();
}
