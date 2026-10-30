/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: CreateFileW creation dispositions, last-error on success, and
 * error codes for common failure inputs.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew
 *       https://learn.microsoft.com/en-us/windows/win32/fileio/naming-a-file
 */
#include "oracle.h"

static void create_case(const char *name, const wchar_t *path, DWORD access, DWORD share,
                        DWORD disposition, DWORD flags, const wchar_t *state_path)
{
    HANDLE h;
    ORACLE_CALL(h, CreateFileW(path, access, share, NULL, disposition, flags, NULL));
    oracle_case(name);
    o_handle("ret", h);
    o_lasterr();
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    if (state_path) o_file_state("file", state_path);
    oracle_case_end();
}

static void make_file(const wchar_t *path, const char *content)
{
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    DWORD w;
    WriteFile(h, content, (DWORD)strlen(content), &w, NULL);
    CloseHandle(h);
}

int main(void)
{
    oracle_begin("kernel32/CreateFileW");
    const DWORD RW = GENERIC_READ | GENERIC_WRITE;

    create_case("create_new_fresh", L"a.txt", RW, 0, CREATE_NEW, 0, L"a.txt");
    create_case("create_new_exists", L"a.txt", RW, 0, CREATE_NEW, 0, L"a.txt");

    make_file(L"b.txt", "hello");
    create_case("create_always_exists_truncates", L"b.txt", RW, 0, CREATE_ALWAYS, 0, L"b.txt");
    create_case("create_always_fresh", L"c.txt", RW, 0, CREATE_ALWAYS, 0, L"c.txt");

    make_file(L"d.txt", "keep");
    create_case("open_always_exists", L"d.txt", RW, 0, OPEN_ALWAYS, 0, L"d.txt");
    create_case("open_always_fresh", L"e.txt", RW, 0, OPEN_ALWAYS, 0, L"e.txt");

    create_case("open_existing_missing", L"nope.txt", GENERIC_READ, 0, OPEN_EXISTING, 0, L"nope.txt");
    create_case("open_existing_missing_dir", L"nodir\\x.txt", GENERIC_READ, 0, OPEN_EXISTING, 0, NULL);
    create_case("create_new_missing_dir", L"nodir\\x.txt", RW, 0, CREATE_NEW, 0, NULL);

    make_file(L"t.txt", "truncate me");
    create_case("truncate_existing", L"t.txt", GENERIC_WRITE, 0, TRUNCATE_EXISTING, 0, L"t.txt");
    create_case("truncate_existing_missing", L"t2.txt", GENERIC_WRITE, 0, TRUNCATE_EXISTING, 0, L"t2.txt");
    make_file(L"t3.txt", "abc");
    create_case("truncate_existing_readonly_access", L"t3.txt", GENERIC_READ, 0, TRUNCATE_EXISTING, 0, L"t3.txt");

    create_case("invalid_disposition_0", L"f.txt", RW, 0, 0, 0, L"f.txt");
    create_case("invalid_disposition_6", L"f.txt", RW, 0, 6, 0, L"f.txt");

    CreateDirectoryW(L"dir", NULL);
    create_case("open_dir_no_backup_semantics", L"dir", GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, 0, NULL);
    create_case("open_dir_backup_semantics", L"dir", GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING,
                FILE_FLAG_BACKUP_SEMANTICS, NULL);
    create_case("create_new_over_dir", L"dir", RW, 0, CREATE_NEW, 0, L"dir");

    create_case("invalid_char_lt", L"a<b.txt", RW, 0, CREATE_ALWAYS, 0, NULL);
    create_case("invalid_char_pipe", L"a|b.txt", RW, 0, CREATE_ALWAYS, 0, NULL);
    create_case("invalid_char_quote", L"a\"b.txt", RW, 0, CREATE_ALWAYS, 0, NULL);
    create_case("empty_path", L"", GENERIC_READ, 0, OPEN_EXISTING, 0, NULL);

    /* Trailing dots and spaces are stripped by Win32 path normalisation. */
    create_case("trailing_dot", L"g.txt.", RW, 0, CREATE_NEW, 0, L"g.txt");
    create_case("trailing_space", L"h.txt ", RW, 0, CREATE_NEW, 0, L"h.txt");

    /* Sharing violation: the first handle denies write sharing. */
    {
        HANDLE h1 = CreateFileW(L"s.txt", RW, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
        create_case("sharing_violation_write", L"s.txt", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    OPEN_EXISTING, 0, NULL);
        create_case("sharing_ok_read", L"s.txt", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    OPEN_EXISTING, 0, NULL);
        CloseHandle(h1);
    }

    /* FILE_FLAG_DELETE_ON_CLOSE removes the file when the last handle closes. */
    create_case("delete_on_close", L"doc.txt", RW | DELETE, 0, CREATE_NEW, FILE_FLAG_DELETE_ON_CLOSE, L"doc.txt");

    return oracle_end();
}
