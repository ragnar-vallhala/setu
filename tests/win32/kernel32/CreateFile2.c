/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: CreateFile2 and its CREATEFILE2_EXTENDED_PARAMETERS:
 * dispositions, attribute vs flag vs security-QoS fields, the dwSize check,
 * NULL parameters, and last-error on success (ERROR_ALREADY_EXISTS).
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfile2
 *       https://learn.microsoft.com/en-us/windows/win32/api/fileapi/ns-fileapi-createfile2_extended_parameters
 */
#include "oracle.h"

typedef HANDLE(WINAPI *pCreateFile2)(LPCWSTR, DWORD, DWORD, DWORD, CREATEFILE2_EXTENDED_PARAMETERS *);
static pCreateFile2 cf2;

static void cf2_case(const char *name, const wchar_t *path, DWORD access, DWORD share, DWORD disp,
                     CREATEFILE2_EXTENDED_PARAMETERS *p, const wchar_t *state)
{
    HANDLE h;
    ORACLE_CALL(h, cf2(path, access, share, disp, p));
    oracle_case(name);
    o_handle("ret", h);
    o_lasterr();
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    if (state) o_file_state("file", state);
    oracle_case_end();
}

static CREATEFILE2_EXTENDED_PARAMETERS params(DWORD attrs, DWORD flags, DWORD sqos)
{
    CREATEFILE2_EXTENDED_PARAMETERS p;
    memset(&p, 0, sizeof p);
    p.dwSize = sizeof p;
    p.dwFileAttributes = attrs;
    p.dwFileFlags = flags;
    p.dwSecurityQosFlags = sqos;
    return p;
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
    oracle_begin("kernel32/CreateFile2");
    cf2 = (pCreateFile2)(void *)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "CreateFile2");
    oracle_case("export");
    o_bool("present", cf2 != NULL);
    oracle_case_end();
    if (!cf2) return oracle_end();
    const DWORD RW = GENERIC_READ | GENERIC_WRITE;
    CREATEFILE2_EXTENDED_PARAMETERS p;

    cf2_case("create_new_null_params", L"a.txt", RW, 0, CREATE_NEW, NULL, L"a.txt");
    cf2_case("create_new_exists", L"a.txt", RW, 0, CREATE_NEW, NULL, L"a.txt");
    cf2_case("create_always_exists", L"a.txt", RW, 0, CREATE_ALWAYS, NULL, L"a.txt");
    cf2_case("open_always_exists", L"a.txt", RW, 0, OPEN_ALWAYS, NULL, L"a.txt");
    cf2_case("open_always_new", L"b.txt", RW, 0, OPEN_ALWAYS, NULL, L"b.txt");
    cf2_case("open_existing_missing", L"nope.txt", GENERIC_READ, 0, OPEN_EXISTING, NULL, NULL);
    make_file(L"t.txt", "truncate me");
    cf2_case("truncate_existing", L"t.txt", GENERIC_WRITE, 0, TRUNCATE_EXISTING, NULL, L"t.txt");
    cf2_case("disposition_0", L"c.txt", RW, 0, 0, NULL, L"c.txt");
    cf2_case("disposition_6", L"c.txt", RW, 0, 6, NULL, L"c.txt");

    p = params(FILE_ATTRIBUTE_HIDDEN, 0, 0);
    cf2_case("attrs_hidden", L"h.txt", RW, 0, CREATE_NEW, &p, L"h.txt");
    p = params(FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_ARCHIVE, 0, 0);
    cf2_case("attrs_readonly_archive", L"r.txt", GENERIC_READ, 0, CREATE_NEW, &p, L"r.txt");
    SetFileAttributesW(L"r.txt", FILE_ATTRIBUTE_NORMAL);
    p = params(FILE_ATTRIBUTE_NORMAL, 0, 0);
    cf2_case("attrs_normal", L"n.txt", RW, 0, CREATE_NEW, &p, L"n.txt");
    p = params(FILE_ATTRIBUTE_TEMPORARY, 0, 0);
    cf2_case("attrs_temporary", L"tmp.txt", RW, 0, CREATE_NEW, &p, L"tmp.txt");
    p = params(FILE_ATTRIBUTE_DIRECTORY, 0, 0);
    cf2_case("attrs_directory", L"dirattr.txt", RW, 0, CREATE_NEW, &p, L"dirattr.txt");
    p = params(FILE_FLAG_DELETE_ON_CLOSE, 0, 0);
    cf2_case("flag_in_attribute_field", L"fa.txt", RW | DELETE, 0, CREATE_NEW, &p, L"fa.txt");
    p = params(0, FILE_ATTRIBUTE_HIDDEN, 0);
    cf2_case("attribute_in_flag_field", L"af.txt", RW, 0, CREATE_NEW, &p, L"af.txt");
    p = params(0, FILE_FLAG_DELETE_ON_CLOSE, 0);
    cf2_case("flag_delete_on_close", L"doc.txt", RW | DELETE, 0, CREATE_NEW, &p, L"doc.txt");
    p = params(0, FILE_FLAG_DELETE_ON_CLOSE, 0);
    cf2_case("flag_delete_on_close_no_delete_access", L"doc2.txt", RW, 0, CREATE_NEW, &p, L"doc2.txt");
    p = params(0, FILE_FLAG_OVERLAPPED | FILE_FLAG_WRITE_THROUGH | FILE_FLAG_RANDOM_ACCESS, 0);
    cf2_case("flags_overlapped_write_through", L"o.txt", RW, 0, CREATE_NEW, &p, L"o.txt");
    p = params(0, FILE_FLAG_POSIX_SEMANTICS, 0);
    cf2_case("flag_posix_semantics", L"posix.txt", RW, 0, CREATE_NEW, &p, L"posix.txt");
    p = params(0, 0x00000400, 0);
    cf2_case("flag_bogus_low_bit", L"bogus.txt", RW, 0, CREATE_NEW, &p, L"bogus.txt");
    p = params(0, 0, SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION);
    cf2_case("sqos_identification", L"q.txt", RW, 0, CREATE_NEW, &p, L"q.txt");
    p = params(0, 0, SECURITY_ANONYMOUS);
    cf2_case("sqos_without_present", L"q2.txt", RW, 0, CREATE_NEW, &p, L"q2.txt");
    p = params(0, 0, 0x10000000);
    cf2_case("sqos_bogus", L"q3.txt", RW, 0, CREATE_NEW, &p, L"q3.txt");

    p = params(0, 0, 0);
    p.dwSize = 0;
    cf2_case("size_0", L"s0.txt", RW, 0, CREATE_NEW, &p, L"s0.txt");
    p.dwSize = sizeof p - 4;
    cf2_case("size_minus_4", L"s1.txt", RW, 0, CREATE_NEW, &p, L"s1.txt");
    p.dwSize = sizeof p + 4;
    cf2_case("size_plus_4", L"s2.txt", RW, 0, CREATE_NEW, &p, L"s2.txt");

    CreateDirectoryW(L"d", NULL);
    p = params(0, FILE_FLAG_BACKUP_SEMANTICS, 0);
    cf2_case("open_dir_backup_semantics", L"d", GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, &p, NULL);
    cf2_case("open_dir_no_flags", L"d", GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, NULL, NULL);
    p = params(FILE_ATTRIBUTE_DIRECTORY, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_POSIX_SEMANTICS, 0);
    cf2_case("create_dir_style", L"newdir", GENERIC_READ, 0, CREATE_NEW, &p, L"newdir");
    /* Probes around create_dir_style: which part of the combination fails. */
    cf2_case("create_dir_style_rw", L"newdir_rw", RW, 0, CREATE_NEW, &p, L"newdir_rw");
    cf2_case("create_dir_style_open_always", L"newdir_oa", GENERIC_READ, 0, OPEN_ALWAYS, &p, L"newdir_oa");
    p = params(FILE_ATTRIBUTE_DIRECTORY, FILE_FLAG_BACKUP_SEMANTICS, 0);
    cf2_case("dir_attr_backup_only", L"newdir_b", GENERIC_READ, 0, CREATE_NEW, &p, L"newdir_b");
    p = params(FILE_ATTRIBUTE_DIRECTORY, FILE_FLAG_POSIX_SEMANTICS, 0);
    cf2_case("dir_attr_posix_only", L"newdir_p", GENERIC_READ, 0, CREATE_NEW, &p, L"newdir_p");
    p = params(0, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_POSIX_SEMANTICS, 0);
    cf2_case("backup_posix_no_dir_attr", L"newdir_n", GENERIC_READ, 0, CREATE_NEW, &p, L"newdir_n");
    p = params(FILE_ATTRIBUTE_HIDDEN, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_POSIX_SEMANTICS, 0);
    cf2_case("backup_posix_hidden_attr", L"newdir_h", GENERIC_READ, 0, CREATE_NEW, &p, L"newdir_h");
    p = params(0, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_POSIX_SEMANTICS | FILE_ATTRIBUTE_DIRECTORY, 0);
    cf2_case("dir_bit_in_flag_field", L"newdir_f", GENERIC_READ, 0, CREATE_NEW, &p, L"newdir_f");
    p = params(FILE_ATTRIBUTE_DIRECTORY, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_POSIX_SEMANTICS, 0);
    cf2_case("dir_style_existing_dir", L"d", GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, &p, NULL);
    {
        HANDLE h;
        ORACLE_CALL(h, CreateFileW(L"cfwdir", GENERIC_READ, 0, NULL, CREATE_NEW,
                                   FILE_ATTRIBUTE_DIRECTORY | FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_POSIX_SEMANTICS,
                                   NULL));
        oracle_case("createfilew_dir_style");
        o_handle("ret", h);
        o_lasterr();
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        o_file_state("file", L"cfwdir");
        oracle_case_end();
    }

    {
        HANDLE ex = CreateFileW(L"a.txt", GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
        cf2_case("sharing_violation", L"a.txt", GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, NULL, NULL);
        CloseHandle(ex);
    }
    cf2_case("invalid_char", L"a<b.txt", RW, 0, CREATE_NEW, NULL, NULL);
    cf2_case("empty_path", L"", GENERIC_READ, 0, OPEN_EXISTING, NULL, NULL);
    cf2_case("nul_device", L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, OPEN_EXISTING, NULL, NULL);
    cf2_case("missing_dir", L"nodir\\x.txt", RW, 0, CREATE_NEW, NULL, NULL);

    oracle_case("final_listing");
    o_dir_listing("root", L".", 1);
    oracle_case_end();

    return oracle_end();
}
