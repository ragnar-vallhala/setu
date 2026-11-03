/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: GetDriveTypeW name forms, plus the shape (never the content)
 * of GetLogicalDrives / GetLogicalDriveStringsW, since the set of drives is
 * machine-specific.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getdrivetypew
 *       https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getlogicaldrives
 *       https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getlogicaldrivestringsw
 */
#include "oracle.h"

static void dt_case(const char *name, const wchar_t *path)
{
    UINT t;
    ORACLE_CALL(t, GetDriveTypeW(path));
    oracle_case(name);
    o_u32("type", t);
    o_lasterr();
    oracle_case_end();
}

int main(void)
{
    oracle_begin("kernel32/GetDriveTypeW");
    wchar_t cwd[MAX_PATH], root[MAX_PATH], p[MAX_PATH + 16];
    GetCurrentDirectoryW(MAX_PATH, cwd);
    GetVolumePathNameW(cwd, root, MAX_PATH);
    CreateDirectoryW(L"d", NULL);

    dt_case("root", root);
    dt_case("null", NULL);
    swprintf(p, MAX_PATH + 16, L"%lc:", root[0]);
    dt_case("drive_no_backslash", p);
    swprintf(p, MAX_PATH + 16, L"%lc:/", root[0]);
    dt_case("drive_forward_slash", p);
    swprintf(p, MAX_PATH + 16, L"%lc:\\", towlower(root[0]));
    dt_case("lowercase_drive", p);
    swprintf(p, MAX_PATH + 16, L"%lc:\\\\", root[0]);
    dt_case("double_backslash", p);
    swprintf(p, MAX_PATH + 16, L"\\\\?\\%ls", root);
    dt_case("long_prefix_root", p);
    swprintf(p, MAX_PATH + 16, L"\\\\.\\%ls", root);
    dt_case("dot_prefix_root", p);
    swprintf(p, MAX_PATH + 16, L"\\\\?\\%lc:", root[0]);
    dt_case("long_prefix_no_backslash", p);
    dt_case("cwd_not_root", cwd);
    swprintf(p, MAX_PATH + 16, L"%ls\\", cwd);
    dt_case("cwd_trailing_backslash", p);
    dt_case("relative_dir", L"d\\");
    dt_case("relative_dir_no_backslash", L"d");
    dt_case("dot", L".");
    dt_case("dot_backslash", L".\\");
    dt_case("empty", L"");
    dt_case("backslash", L"\\");
    dt_case("unused_drive", L"Q:\\");
    dt_case("unused_drive_no_backslash", L"Q:");
    dt_case("bad_drive_letter", L"1:\\");
    dt_case("missing_dir", L"nope\\");

    {
        UINT t;
        char ra[8] = "";
        snprintf(ra, sizeof ra, "%c:\\", (char)root[0]);
        ORACLE_CALL(t, GetDriveTypeA(ra));
        oracle_case("ansi_root");
        o_u32("type", t);
        o_lasterr();
        oracle_case_end();
    }

    /* GetLogicalDrives / GetLogicalDriveStringsW shape. */
    DWORD mask, bits = 0, len, len2;
    ORACLE_CALL(mask, GetLogicalDrives());
    for (DWORD m = mask; m; m &= m - 1) bits++;
    oracle_case("logical_drives_mask");
    o_lasterr();
    o_bool("nonzero", mask != 0);
    o_bool("root_bit_set", (mask >> (towupper(root[0]) - L'A')) & 1);
    o_bool("q_bit_clear", !((mask >> (L'Q' - L'A')) & 1));
    o_bool("high_bits_clear", (mask >> 26) == 0);
    oracle_case_end();

    ORACLE_CALL(len, GetLogicalDriveStringsW(0, NULL));
    oracle_case("strings_size_query");
    o_lasterr();
    o_bool("len_is_4n_plus_1", len == bits * 4 + 1);
    oracle_case_end();

    {
        wchar_t buf[512];
        wmemset(buf, L'#', 512);
        ORACLE_CALL(len2, GetLogicalDriveStringsW(512, buf));
        oracle_case("strings_full");
        o_lasterr();
        o_bool("ret_is_4n", len2 == bits * 4);
        int shape_ok = 1, found_root = 0;
        DWORD count = 0;
        for (wchar_t *q = buf; *q; q += wcslen(q) + 1) {
            count++;
            if (wcslen(q) != 3 || q[1] != L':' || q[2] != L'\\' || q[0] < L'A' || q[0] > L'Z') shape_ok = 0;
            if (!(mask >> (q[0] - L'A') & 1)) shape_ok = 0;
            if (towupper(q[0]) == towupper(root[0])) found_root = 1;
        }
        o_bool("each_entry_is_X_colon_backslash", shape_ok);
        o_bool("count_matches_mask", count == bits);
        o_bool("contains_root", found_root);
        o_bool("double_nul_terminated", buf[len2] == 0);
        int sorted = 1;
        for (DWORD i = 1; i < count; i++)
            if (buf[i * 4] <= buf[(i - 1) * 4]) sorted = 0;
        o_bool("sorted", sorted);
        oracle_case_end();

        wmemset(buf, L'#', 512);
        ORACLE_CALL(len2, GetLogicalDriveStringsW(len - 1, buf));
        oracle_case("strings_buffer_exact_minus_one");
        o_lasterr();
        o_bool("ret_is_required", len2 == len);
        o_bool("buffer_untouched", buf[0] == L'#');
        oracle_case_end();

        wmemset(buf, L'#', 512);
        ORACLE_CALL(len2, GetLogicalDriveStringsW(len, buf));
        oracle_case("strings_buffer_exact");
        o_lasterr();
        o_bool("ret_is_4n", len2 == bits * 4);
        oracle_case_end();

        wmemset(buf, L'#', 512);
        ORACLE_CALL(len2, GetLogicalDriveStringsW(3, buf));
        oracle_case("strings_buffer_3");
        o_lasterr();
        o_bool("ret_is_required", len2 == len);
        o_bool("buffer_untouched", buf[0] == L'#');
        oracle_case_end();

        char abuf[512];
        DWORD alen;
        ORACLE_CALL(alen, GetLogicalDriveStringsA(sizeof abuf, abuf));
        oracle_case("strings_ansi");
        o_lasterr();
        o_bool("ret_is_4n", alen == bits * 4);
        oracle_case_end();
    }

    return oracle_end();
}
