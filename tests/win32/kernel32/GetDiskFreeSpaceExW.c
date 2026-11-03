/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: GetDiskFreeSpaceExW and GetDiskFreeSpaceW. Sizes depend on the
 * machine, so the test prints only success, errors and relationships between
 * the values (free <= total, legacy and Ex agree, ...).
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getdiskfreespaceexw
 *       https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getdiskfreespacew
 */
#include "oracle.h"

static wchar_t root[MAX_PATH]; /* volume root of the working directory, e.g. C:\ */
static unsigned long long ref_total;

static int close_to(unsigned long long a, unsigned long long b)
{
    unsigned long long d = a > b ? a - b : b - a;
    return d <= b / 50 + (64ull << 20); /* 2% + 64 MiB: other writers on the volume */
}

static void ex_case(const char *name, const wchar_t *path)
{
    ULARGE_INTEGER avail = {0}, total = {0}, freeb = {0};
    BOOL r;
    ORACLE_CALL(r, GetDiskFreeSpaceExW(path, &avail, &total, &freeb));
    oracle_case(name);
    o_bool("ret", r);
    o_lasterr();
    if (r) {
        o_bool("total_nonzero", total.QuadPart != 0);
        o_bool("avail_le_free", avail.QuadPart <= freeb.QuadPart);
        o_bool("free_le_total", freeb.QuadPart <= total.QuadPart);
        o_bool("total_matches_ref", total.QuadPart == ref_total);
    }
    oracle_case_end();
}

static void legacy_case(const char *name, const wchar_t *path)
{
    DWORD spc = 0, bps = 0, freec = 0, totc = 0;
    BOOL r;
    ORACLE_CALL(r, GetDiskFreeSpaceW(path, &spc, &bps, &freec, &totc));
    oracle_case(name);
    o_bool("ret", r);
    o_lasterr();
    if (r) {
        unsigned long long cl = (unsigned long long)spc * bps;
        o_bool("bytes_per_sector_pow2", bps && !(bps & (bps - 1)));
        o_bool("sectors_per_cluster_pow2", spc && !(spc & (spc - 1)));
        o_bool("free_le_total", freec <= totc);
        o_bool("total_matches_ex", close_to(cl * totc, ref_total));
    }
    oracle_case_end();
}

int main(void)
{
    oracle_begin("kernel32/GetDiskFreeSpaceExW");
    wchar_t cwd[MAX_PATH], p[MAX_PATH + 16];
    GetCurrentDirectoryW(MAX_PATH, cwd);
    GetVolumePathNameW(cwd, root, MAX_PATH);
    {
        ULARGE_INTEGER t;
        GetDiskFreeSpaceExW(root, NULL, &t, NULL);
        ref_total = t.QuadPart;
    }
    CreateDirectoryW(L"d", NULL);
    HANDLE f = CreateFileW(L"f.txt", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    CloseHandle(f);

    oracle_case("root_shape");
    o_u32("len", (DWORD)wcslen(root));
    o_bool("drive_letter_root", wcslen(root) == 3 && root[1] == L':' && root[2] == L'\\');
    oracle_case_end();

    ex_case("ex_root", root);
    ex_case("ex_null", NULL);
    ex_case("ex_dot", L".");
    ex_case("ex_subdir", L"d");
    ex_case("ex_subdir_trailing_backslash", L"d\\");
    ex_case("ex_cwd_full", cwd);
    ex_case("ex_file", L"f.txt");
    ex_case("ex_missing_dir", L"nope");
    ex_case("ex_missing_dir_trailing_backslash", L"nope\\");
    ex_case("ex_missing_nested", L"nope\\deeper\\");
    ex_case("ex_empty", L"");
    ex_case("ex_unused_drive", L"Q:\\");
    ex_case("ex_bad_drive_char", L"1:\\");
    swprintf(p, MAX_PATH + 16, L"\\\\?\\%ls", root);
    ex_case("ex_long_prefix_root", p);
    swprintf(p, MAX_PATH + 16, L"%lc:", root[0]);
    ex_case("ex_drive_no_backslash", p);

    {
        BOOL r;
        ORACLE_CALL(r, GetDiskFreeSpaceExW(root, NULL, NULL, NULL));
        oracle_case("ex_all_outputs_null");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        ULARGE_INTEGER a1, a2;
        GetDiskFreeSpaceExW(root, &a1, NULL, NULL);
        ORACLE_CALL(r, GetDiskFreeSpaceExW(root, NULL, NULL, &a2));
        oracle_case("ex_only_free");
        o_bool("ret", r);
        o_lasterr();
        o_bool("avail_close_to_free", close_to(a1.QuadPart, a2.QuadPart));
        oracle_case_end();
    }

    legacy_case("legacy_root", root);
    legacy_case("legacy_null", NULL);
    legacy_case("legacy_dot", L".");
    legacy_case("legacy_subdir", L"d");
    legacy_case("legacy_subdir_trailing_backslash", L"d\\");
    legacy_case("legacy_cwd_full", cwd);
    swprintf(p, MAX_PATH + 16, L"%ls\\", cwd);
    legacy_case("legacy_cwd_full_trailing_backslash", p);
    legacy_case("legacy_file", L"f.txt");
    legacy_case("legacy_missing_dir", L"nope\\");
    legacy_case("legacy_empty", L"");
    legacy_case("legacy_unused_drive", L"Q:\\");
    swprintf(p, MAX_PATH + 16, L"%lc:", root[0]);
    legacy_case("legacy_drive_no_backslash", p);
    {
        BOOL r;
        DWORD spc;
        ORACLE_CALL(r, GetDiskFreeSpaceW(root, &spc, NULL, NULL, NULL));
        oracle_case("legacy_only_spc");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
    }

    {
        ULARGE_INTEGER a = {0}, t = {0}, fr = {0};
        BOOL r;
        ORACLE_CALL(r, GetDiskFreeSpaceExA(".", &a, &t, &fr));
        oracle_case("ex_ansi_dot");
        o_bool("ret", r);
        o_lasterr();
        o_bool("total_matches_ref", t.QuadPart == ref_total);
        oracle_case_end();
    }

    return oracle_end();
}
