/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: ReplaceFileW with and without a backup file, missing inputs,
 * attribute merging, open handles and flags.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-replacefilew
 */
#include "oracle.h"

static void make_file(const wchar_t *path, const char *content)
{
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    DWORD w;
    WriteFile(h, content, (DWORD)strlen(content), &w, NULL);
    CloseHandle(h);
}

static void clear(void)
{
    static const wchar_t *names[] = {L"r.txt", L"s.txt", L"b.bak", L"o.bak"};
    for (unsigned i = 0; i < sizeof names / sizeof names[0]; i++) {
        SetFileAttributesW(names[i], FILE_ATTRIBUTE_NORMAL);
        DeleteFileW(names[i]);
    }
}

static void rep(const char *name, const wchar_t *replaced, const wchar_t *replacement, const wchar_t *backup,
                DWORD flags)
{
    BOOL r;
    ORACLE_CALL(r, ReplaceFileW(replaced, replacement, backup, flags, NULL, NULL));
    oracle_case(name);
    o_bool("ret", r);
    o_lasterr();
    o_file_state("replaced", L"r.txt");
    o_file_state("replacement", L"s.txt");
    o_file_state("backup", L"b.bak");
    oracle_case_end();
}

int main(void)
{
    oracle_begin("kernel32/ReplaceFileW");

    clear();
    make_file(L"r.txt", "old");
    make_file(L"s.txt", "new!");
    rep("basic_no_backup", L"r.txt", L"s.txt", NULL, 0);

    clear();
    make_file(L"r.txt", "old");
    make_file(L"s.txt", "new!");
    rep("basic_with_backup", L"r.txt", L"s.txt", L"b.bak", 0);

    clear();
    make_file(L"r.txt", "old");
    make_file(L"s.txt", "new!");
    make_file(L"b.bak", "stale backup");
    rep("backup_exists", L"r.txt", L"s.txt", L"b.bak", 0);

    clear();
    make_file(L"s.txt", "new!");
    rep("replaced_missing", L"r.txt", L"s.txt", NULL, 0);

    clear();
    make_file(L"r.txt", "old");
    rep("replacement_missing", L"r.txt", L"s.txt", NULL, 0);

    clear();
    make_file(L"r.txt", "old");
    rep("replacement_missing_with_backup", L"r.txt", L"s.txt", L"b.bak", 0);

    clear();
    rep("both_missing", L"r.txt", L"s.txt", NULL, 0);

    clear();
    make_file(L"r.txt", "old");
    rep("same_file", L"r.txt", L"r.txt", NULL, 0);

    /* Attributes: the result keeps the replaced file's attributes. */
    clear();
    make_file(L"r.txt", "old");
    make_file(L"s.txt", "new!");
    SetFileAttributesW(L"r.txt", FILE_ATTRIBUTE_HIDDEN);
    rep("replaced_hidden", L"r.txt", L"s.txt", NULL, 0);

    clear();
    make_file(L"r.txt", "old");
    make_file(L"s.txt", "new!");
    SetFileAttributesW(L"s.txt", FILE_ATTRIBUTE_HIDDEN);
    rep("replacement_hidden", L"r.txt", L"s.txt", NULL, 0);

    clear();
    make_file(L"r.txt", "old");
    make_file(L"s.txt", "new!");
    SetFileAttributesW(L"r.txt", FILE_ATTRIBUTE_READONLY);
    rep("replaced_readonly", L"r.txt", L"s.txt", NULL, 0);

    clear();
    make_file(L"r.txt", "old");
    make_file(L"s.txt", "new!");
    SetFileAttributesW(L"s.txt", FILE_ATTRIBUTE_READONLY);
    rep("replacement_readonly", L"r.txt", L"s.txt", NULL, 0);

    clear();
    make_file(L"r.txt", "old");
    make_file(L"s.txt", "new!");
    SetFileAttributesW(L"r.txt", FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_SYSTEM);
    rep("replaced_system", L"r.txt", L"s.txt", NULL, 0);

    /* Flags. */
    clear();
    make_file(L"r.txt", "old");
    make_file(L"s.txt", "new!");
    rep("flag_write_through", L"r.txt", L"s.txt", NULL, REPLACEFILE_WRITE_THROUGH);

    clear();
    make_file(L"r.txt", "old");
    make_file(L"s.txt", "new!");
    rep("flag_ignore_merge_errors", L"r.txt", L"s.txt", NULL, REPLACEFILE_IGNORE_MERGE_ERRORS);

    clear();
    make_file(L"r.txt", "old");
    make_file(L"s.txt", "new!");
    rep("flag_ignore_acl_errors", L"r.txt", L"s.txt", NULL, REPLACEFILE_IGNORE_ACL_ERRORS);

    clear();
    make_file(L"r.txt", "old");
    make_file(L"s.txt", "new!");
    rep("flag_unknown_bit", L"r.txt", L"s.txt", NULL, 0x100);

    /* Open handles. */
    clear();
    make_file(L"r.txt", "old");
    make_file(L"s.txt", "new!");
    HANDLE h = CreateFileW(L"r.txt", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    rep("replaced_open_share_read", L"r.txt", L"s.txt", NULL, 0);
    CloseHandle(h);

    clear();
    make_file(L"r.txt", "old");
    make_file(L"s.txt", "new!");
    h = CreateFileW(L"r.txt", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                    OPEN_EXISTING, 0, NULL);
    rep("replaced_open_share_all", L"r.txt", L"s.txt", NULL, 0);
    CloseHandle(h);

    clear();
    make_file(L"r.txt", "old");
    make_file(L"s.txt", "new!");
    h = CreateFileW(L"s.txt", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    rep("replacement_open_share_read", L"r.txt", L"s.txt", NULL, 0);
    CloseHandle(h);

    clear();
    make_file(L"r.txt", "old");
    make_file(L"s.txt", "new!");
    h = CreateFileW(L"s.txt", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                    OPEN_EXISTING, 0, NULL);
    rep("replacement_open_share_all", L"r.txt", L"s.txt", NULL, 0);
    CloseHandle(h);

    clear();
    make_file(L"r.txt", "old");
    make_file(L"s.txt", "new!");
    make_file(L"b.bak", "locked");
    h = CreateFileW(L"b.bak", GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
    rep("backup_locked", L"r.txt", L"s.txt", L"b.bak", 0);
    CloseHandle(h);

    /* Directories. */
    clear();
    CreateDirectoryW(L"dir", NULL);
    make_file(L"s.txt", "new!");
    {
        BOOL r;
        ORACLE_CALL(r, ReplaceFileW(L"dir", L"s.txt", NULL, 0, NULL, NULL));
        oracle_case("replaced_is_directory");
        o_bool("ret", r);
        o_lasterr();
        o_file_state("dir", L"dir");
        o_file_state("replacement", L"s.txt");
        oracle_case_end();
    }
    make_file(L"r.txt", "old");
    rep("replacement_is_directory", L"r.txt", L"dir", NULL, 0);

    clear();
    make_file(L"r.txt", "old");
    make_file(L"s.txt", "new!");
    rep("backup_missing_dir", L"r.txt", L"s.txt", L"nodir\\b.bak", 0);

    clear();
    make_file(L"r.txt", "old");
    make_file(L"s.txt", "new!");
    rep("backup_same_as_replaced", L"r.txt", L"s.txt", L"r.txt", 0);

    oracle_case("final_listing");
    o_dir_listing("root", L".", 1);
    oracle_case_end();

    return oracle_end();
}
