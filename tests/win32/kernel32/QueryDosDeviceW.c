/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: QueryDosDeviceW and DefineDosDeviceW. Volume device names are
 * machine-specific, so drive targets are tokenised; well-known device
 * targets and the multi-string format are printed.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-querydosdevicew
 *       https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-definedosdevicew
 */
#include "oracle.h"

static wchar_t buf[65536];

/* Prints the first target, the number of targets and whether the length
 * returned matches the double-NUL-terminated multi-string. */
static void qdd_case(const char *name, const wchar_t *dev, DWORD size, int print_target)
{
    DWORD r;
    wmemset(buf, L'#', 64);
    ORACLE_CALL(r, QueryDosDeviceW(dev, buf, size));
    oracle_case(name);
    o_u32("ret", r);
    o_lasterr();
    if (r) {
        DWORD n = 0, used = 0;
        for (wchar_t *q = buf; *q && used < r; q += wcslen(q) + 1) {
            n++;
            used += (DWORD)wcslen(q) + 1;
        }
        o_u32("targets", n);
        o_bool("ret_counts_final_nul", r == used + 1);
        if (print_target) o_wstr("first", buf);
        else o_bool("first_is_device", !wcsncmp(buf, L"\\Device\\", 8));
    }
    oracle_case_end();
}

int main(void)
{
    oracle_begin("kernel32/QueryDosDeviceW");
    wchar_t cwd[MAX_PATH], root[MAX_PATH], drive[4];
    GetCurrentDirectoryW(MAX_PATH, cwd);
    GetVolumePathNameW(cwd, root, MAX_PATH);
    drive[0] = root[0];
    drive[1] = L':';
    drive[2] = 0;

    qdd_case("root_drive", drive, 65536, 0);
    {
        wchar_t lower[4] = {(wchar_t)towlower(root[0]), L':', 0};
        qdd_case("root_drive_lowercase", lower, 65536, 0);
        wchar_t bs[4] = {root[0], L':', L'\\', 0};
        qdd_case("root_drive_trailing_backslash", bs, 65536, 0);
    }
    qdd_case("nul", L"NUL", 65536, 1);
    qdd_case("nul_lowercase", L"nul", 65536, 1);
    qdd_case("pipe", L"PIPE", 65536, 1);
    qdd_case("mailslot", L"MAILSLOT", 65536, 1);
    qdd_case("unused_drive", L"Q:", 65536, 1);
    qdd_case("missing_name", L"SETUNOSUCHDEVICE", 65536, 1);
    qdd_case("empty_name", L"", 65536, 1);
    qdd_case("nul_buffer_1", L"NUL", 1, 1);
    qdd_case("nul_buffer_12", L"NUL", 12, 1);
    qdd_case("nul_buffer_13", L"NUL", 13, 1);
    qdd_case("nul_buffer_14", L"NUL", 14, 1);

    /* Full list. */
    {
        DWORD r;
        ORACLE_CALL(r, QueryDosDeviceW(NULL, buf, 65536));
        oracle_case("list_all");
        o_bool("ret_nonzero", r != 0);
        o_lasterr();
        int has_nul = 0, has_drive = 0, has_pipe = 0;
        DWORD used = 0;
        for (wchar_t *q = buf; r && *q && used < r; q += wcslen(q) + 1) {
            used += (DWORD)wcslen(q) + 1;
            if (!wcscmp(q, L"NUL")) has_nul = 1;
            if (!_wcsicmp(q, drive)) has_drive = 1;
            if (!wcscmp(q, L"PIPE")) has_pipe = 1;
        }
        o_bool("has_nul", has_nul);
        o_bool("has_root_drive", has_drive);
        o_bool("has_pipe", has_pipe);
        o_bool("ret_counts_final_nul", r == used + 1);
        oracle_case_end();
        ORACLE_CALL(r, QueryDosDeviceW(NULL, buf, 10));
        oracle_case("list_all_small_buffer");
        o_u32("ret", r);
        o_lasterr();
        oracle_case_end();
    }

    /* DefineDosDeviceW on a private name. */
    {
        BOOL ok;
        ORACLE_CALL(ok, DefineDosDeviceW(DDD_RAW_TARGET_PATH, L"SETUORACLEDEV", L"\\Device\\Null"));
        oracle_case("define_raw");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        qdd_case("query_defined_raw", L"SETUORACLEDEV", 65536, 1);
        ORACLE_CALL(ok, DefineDosDeviceW(DDD_RAW_TARGET_PATH, L"SETUORACLEDEV", L"\\Device\\NamedPipe"));
        oracle_case("define_raw_push_second");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        qdd_case("query_defined_two", L"SETUORACLEDEV", 65536, 1);
        {
            HANDLE h;
            ORACLE_CALL(h, CreateFileW(L"\\\\.\\SETUORACLEDEV", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                       OPEN_EXISTING, 0, NULL));
            oracle_case("open_defined");
            o_handle("ret", h);
            o_lasterr();
            oracle_case_end();
            if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        }
        ORACLE_CALL(ok, DefineDosDeviceW(DDD_REMOVE_DEFINITION | DDD_RAW_TARGET_PATH | DDD_EXACT_MATCH_ON_REMOVE,
                                         L"SETUORACLEDEV", L"\\Device\\NamedPipe"));
        oracle_case("remove_exact");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        qdd_case("query_after_remove_exact", L"SETUORACLEDEV", 65536, 1);
        ORACLE_CALL(ok, DefineDosDeviceW(DDD_REMOVE_DEFINITION, L"SETUORACLEDEV", NULL));
        oracle_case("remove_all");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        qdd_case("query_after_remove_all", L"SETUORACLEDEV", 65536, 1);
        ORACLE_CALL(ok, DefineDosDeviceW(DDD_REMOVE_DEFINITION, L"SETUORACLEDEV", NULL));
        oracle_case("remove_missing");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
    }

    return oracle_end();
}
