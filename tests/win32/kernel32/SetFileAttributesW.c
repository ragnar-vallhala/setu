/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: SetFileAttributesW with each attribute bit on a file and on a
 * directory, which bits are stored, ignored or rejected, and the error
 * paths (missing paths, open handles, name forms).
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-setfileattributesw
 *       https://learn.microsoft.com/en-us/windows/win32/fileio/file-attribute-constants
 */
#include "oracle.h"

static const struct { const char *name; DWORD bit; } bits[] = {
    {"readonly", 0x1},
    {"hidden", 0x2},
    {"system", 0x4},
    {"directory", 0x10},
    {"archive", 0x20},
    {"device", 0x40},
    {"normal", 0x80},
    {"temporary", 0x100},
    {"sparse", 0x200},
    {"reparse", 0x400},
    {"compressed", 0x800},
    {"offline", 0x1000},
    {"not_content_indexed", 0x2000},
    {"encrypted", 0x4000},
    {"integrity_stream", 0x8000},
    {"virtual", 0x10000},
    {"no_scrub_data", 0x20000},
    {"recall_on_open", 0x40000},
    {"pinned", 0x80000},
    {"unpinned", 0x100000},
    {"recall_on_data_access", 0x400000},
};

static void sfa_case(const char *name, const wchar_t *path, DWORD attrs, const wchar_t *state)
{
    BOOL r;
    ORACLE_CALL(r, SetFileAttributesW(path, attrs));
    oracle_case(name);
    o_bool("ret", r);
    o_lasterr();
    DWORD a = GetFileAttributesW(state);
    if (a == INVALID_FILE_ATTRIBUTES) o_str("attrs", "missing");
    else o_hex32("attrs", a);
    oracle_case_end();
}

int main(void)
{
    oracle_begin("kernel32/SetFileAttributesW");
    char name[64];
    CloseHandle(CreateFileW(L"f.txt", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL));
    CreateDirectoryW(L"d", NULL);

    oracle_case("fresh");
    o_hex32("file", GetFileAttributesW(L"f.txt"));
    o_hex32("dir", GetFileAttributesW(L"d"));
    oracle_case_end();

    for (unsigned i = 0; i < sizeof bits / sizeof bits[0]; i++) {
        snprintf(name, sizeof name, "file_%s", bits[i].name);
        SetFileAttributesW(L"f.txt", FILE_ATTRIBUTE_NORMAL);
        sfa_case(name, L"f.txt", bits[i].bit, L"f.txt");
    }
    for (unsigned i = 0; i < sizeof bits / sizeof bits[0]; i++) {
        snprintf(name, sizeof name, "dir_%s", bits[i].name);
        SetFileAttributesW(L"d", FILE_ATTRIBUTE_NORMAL);
        sfa_case(name, L"d", bits[i].bit, L"d");
    }
    SetFileAttributesW(L"d", FILE_ATTRIBUTE_NORMAL);
    SetFileAttributesW(L"f.txt", FILE_ATTRIBUTE_NORMAL);

    sfa_case("file_zero", L"f.txt", 0, L"f.txt");
    sfa_case("file_all_dos_bits", L"f.txt", 0x27, L"f.txt");
    sfa_case("file_normal_with_hidden", L"f.txt", FILE_ATTRIBUTE_NORMAL | FILE_ATTRIBUTE_HIDDEN, L"f.txt");
    sfa_case("file_all_bits", L"f.txt", 0xFFFFFFFF, L"f.txt");
    SetFileAttributesW(L"f.txt", FILE_ATTRIBUTE_NORMAL);
    sfa_case("file_high_unknown_bit", L"f.txt", 0x10000000 | FILE_ATTRIBUTE_HIDDEN, L"f.txt");
    SetFileAttributesW(L"f.txt", FILE_ATTRIBUTE_NORMAL);
    sfa_case("dir_zero", L"d", 0, L"d");
    sfa_case("dir_all_dos_bits", L"d", 0x27, L"d");
    SetFileAttributesW(L"d", FILE_ATTRIBUTE_NORMAL);

    /* Name forms. */
    sfa_case("missing_file", L"nope.txt", FILE_ATTRIBUTE_HIDDEN, L"nope.txt");
    sfa_case("missing_dir", L"nodir\\x.txt", FILE_ATTRIBUTE_HIDDEN, L"nodir\\x.txt");
    sfa_case("file_trailing_backslash", L"f.txt\\", FILE_ATTRIBUTE_HIDDEN, L"f.txt");
    sfa_case("file_trailing_dot", L"f.txt.", FILE_ATTRIBUTE_SYSTEM, L"f.txt");
    sfa_case("file_other_case", L"F.TXT", FILE_ATTRIBUTE_ARCHIVE, L"f.txt");
    sfa_case("dir_trailing_backslash", L"d\\", FILE_ATTRIBUTE_HIDDEN, L"d");
    sfa_case("wildcard", L"*.txt", FILE_ATTRIBUTE_HIDDEN, L"f.txt");
    sfa_case("empty_path", L"", FILE_ATTRIBUTE_HIDDEN, L"f.txt");
    sfa_case("dot", L".", FILE_ATTRIBUTE_ARCHIVE, L".");
    sfa_case("nul_device", L"NUL", FILE_ATTRIBUTE_HIDDEN, L"f.txt");
    SetFileAttributesW(L"f.txt", FILE_ATTRIBUTE_NORMAL);
    SetFileAttributesW(L".", FILE_ATTRIBUTE_DIRECTORY);

    /* Open handles. */
    {
        HANDLE h = CreateFileW(L"f.txt", GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
        sfa_case("open_exclusive", L"f.txt", FILE_ATTRIBUTE_HIDDEN, L"f.txt");
        CloseHandle(h);
        h = CreateFileW(L"f.txt", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        sfa_case("open_share_read", L"f.txt", FILE_ATTRIBUTE_HIDDEN, L"f.txt");
        CloseHandle(h);
        h = CreateFileW(L"f.txt", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        sfa_case("open_share_read_write", L"f.txt", FILE_ATTRIBUTE_SYSTEM, L"f.txt");
        CloseHandle(h);
        h = CreateFileW(L"f.txt", FILE_READ_ATTRIBUTES, 0, NULL, OPEN_EXISTING, 0, NULL);
        sfa_case("open_attributes_only_no_share", L"f.txt", FILE_ATTRIBUTE_ARCHIVE, L"f.txt");
        CloseHandle(h);
        h = CreateFileW(L"f.txt", DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                        FILE_FLAG_DELETE_ON_CLOSE, NULL);
        sfa_case("delete_pending", L"f.txt", FILE_ATTRIBUTE_HIDDEN, L"f.txt");
        CloseHandle(h);
    }

    /* Read-only file stays writable for attributes. */
    CloseHandle(CreateFileW(L"r.txt", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL));
    SetFileAttributesW(L"r.txt", FILE_ATTRIBUTE_READONLY);
    sfa_case("readonly_to_hidden", L"r.txt", FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN, L"r.txt");
    sfa_case("readonly_cleared", L"r.txt", FILE_ATTRIBUTE_NORMAL, L"r.txt");

    {
        BOOL r;
        ORACLE_CALL(r, SetFileAttributesA("r.txt", FILE_ATTRIBUTE_HIDDEN));
        oracle_case("ansi_hidden");
        o_bool("ret", r);
        o_lasterr();
        o_hex32("attrs", GetFileAttributesW(L"r.txt"));
        oracle_case_end();
    }

    return oracle_end();
}
