/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: MoveFileExW flags (MOVEFILE_REPLACE_EXISTING, COPY_ALLOWED,
 * WRITE_THROUGH, DELAY_UNTIL_REBOOT error paths, FAIL_IF_NOT_TRACKABLE),
 * directories, open handles and invalid flag bits.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-movefileexw
 */
#include "oracle.h"

static void make_file(const wchar_t *path, const char *content)
{
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    DWORD w;
    WriteFile(h, content, (DWORD)strlen(content), &w, NULL);
    CloseHandle(h);
}

static void mv(const char *name, const wchar_t *src, const wchar_t *dst, DWORD flags)
{
    BOOL r;
    ORACLE_CALL(r, MoveFileExW(src, dst, flags));
    oracle_case(name);
    o_bool("ret", r);
    o_lasterr();
    o_file_state("src", src);
    if (dst) o_file_state("dst", dst);
    oracle_case_end();
}

int main(void)
{
    oracle_begin("kernel32/MoveFileExW");

    make_file(L"a.txt", "aaa");
    mv("rename_plain", L"a.txt", L"b.txt", 0);

    make_file(L"c.txt", "ccc");
    mv("onto_existing_no_flag", L"b.txt", L"c.txt", 0);
    mv("onto_existing_replace", L"b.txt", L"c.txt", MOVEFILE_REPLACE_EXISTING);

    make_file(L"d.txt", "ddd");
    mv("write_through", L"d.txt", L"e.txt", MOVEFILE_WRITE_THROUGH);
    mv("copy_allowed_same_volume", L"e.txt", L"f.txt", MOVEFILE_COPY_ALLOWED);
    make_file(L"g.txt", "ggg");
    mv("copy_allowed_replace", L"f.txt", L"g.txt", MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING);

    mv("missing_source", L"nope.txt", L"x.txt", 0);
    mv("missing_source_replace", L"nope.txt", L"g.txt", MOVEFILE_REPLACE_EXISTING);
    mv("missing_dest_dir", L"g.txt", L"nodir\\x.txt", 0);
    mv("same_name", L"g.txt", L"g.txt", 0);
    mv("same_name_replace", L"g.txt", L"g.txt", MOVEFILE_REPLACE_EXISTING);
    mv("same_name_other_case", L"g.txt", L"G.TXT", 0);
    oracle_case("listing_after_case_rename");
    o_dir_listing("root", L".", 1);
    oracle_case_end();

    /* Directories. */
    CreateDirectoryW(L"d1", NULL);
    make_file(L"d1\\in.txt", "in");
    mv("dir_rename", L"d1", L"d2", 0);
    CreateDirectoryW(L"d3", NULL);
    mv("dir_onto_existing_dir", L"d2", L"d3", 0);
    mv("dir_onto_existing_dir_replace", L"d2", L"d3", MOVEFILE_REPLACE_EXISTING);
    make_file(L"h.txt", "hhh");
    mv("dir_onto_file_replace", L"d2", L"h.txt", MOVEFILE_REPLACE_EXISTING);
    mv("file_onto_dir_replace", L"h.txt", L"d3", MOVEFILE_REPLACE_EXISTING);
    mv("file_into_dir", L"h.txt", L"d2\\h.txt", 0);
    mv("dir_copy_allowed", L"d2", L"d4", MOVEFILE_COPY_ALLOWED);
    mv("dir_into_itself", L"d4", L"d4\\sub", 0);

    /* Replace a read-only / hidden destination. */
    make_file(L"ro.txt", "ro");
    SetFileAttributesW(L"ro.txt", FILE_ATTRIBUTE_READONLY);
    make_file(L"i.txt", "iii");
    mv("replace_readonly_dest", L"i.txt", L"ro.txt", MOVEFILE_REPLACE_EXISTING);
    SetFileAttributesW(L"ro.txt", FILE_ATTRIBUTE_NORMAL);
    make_file(L"hid.txt", "hid");
    SetFileAttributesW(L"hid.txt", FILE_ATTRIBUTE_HIDDEN);
    mv("replace_hidden_dest", L"i.txt", L"hid.txt", MOVEFILE_REPLACE_EXISTING);
    make_file(L"j.txt", "jjj");
    SetFileAttributesW(L"j.txt", FILE_ATTRIBUTE_READONLY);
    mv("move_readonly_source", L"j.txt", L"k.txt", 0);
    SetFileAttributesW(L"k.txt", FILE_ATTRIBUTE_NORMAL);

    /* Open handles on source and destination. */
    make_file(L"l.txt", "lll");
    HANDLE h = CreateFileW(L"l.txt", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    mv("source_open_no_share_delete", L"l.txt", L"m.txt", 0);
    CloseHandle(h);
    h = CreateFileW(L"l.txt", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    mv("source_open_share_delete", L"l.txt", L"m.txt", 0);
    CloseHandle(h);
    make_file(L"n.txt", "nnn");
    h = CreateFileW(L"n.txt", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    mv("dest_open_replace", L"m.txt", L"n.txt", MOVEFILE_REPLACE_EXISTING);
    CloseHandle(h);
    h = CreateFileW(L"n.txt", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    mv("dest_open_share_delete_replace", L"m.txt", L"n.txt", MOVEFILE_REPLACE_EXISTING);
    CloseHandle(h);

    /* Flag validation. */
    make_file(L"p.txt", "ppp");
    mv("invalid_flag_bit", L"p.txt", L"q.txt", 0x80000000);
    mv("fail_if_not_trackable", L"p.txt", L"q.txt", MOVEFILE_FAIL_IF_NOT_TRACKABLE);
    mv("create_hardlink_flag", L"q.txt", L"r.txt", MOVEFILE_CREATE_HARDLINK);

    /* DELAY_UNTIL_REBOOT: not allowed together with COPY_ALLOWED; a non-admin
     * caller cannot write PendingFileRenameOperations. Only the combined error
     * path is deterministic across users. */
    mv("delay_with_copy_allowed", L"r.txt", L"s.txt", MOVEFILE_DELAY_UNTIL_REBOOT | MOVEFILE_COPY_ALLOWED);

    /* NULL destination without DELAY_UNTIL_REBOOT. */
    mv("null_dest_no_delay", L"r.txt", NULL, 0);

    /* Bad names. */
    mv("dest_invalid_char", L"r.txt", L"a<b.txt", 0);
    mv("dest_empty", L"r.txt", L"", 0);
    mv("src_empty", L"", L"t.txt", 0);
    mv("trailing_backslash_dest", L"r.txt", L"t.txt\\", 0);
    mv("wildcard_source", L"r*.txt", L"t.txt", 0);

    oracle_case("final_listing");
    o_dir_listing("root", L".", 1);
    o_dir_listing("d2", L"d2", 1);
    o_dir_listing("d3", L"d3", 1);
    oracle_case_end();

    return oracle_end();
}
