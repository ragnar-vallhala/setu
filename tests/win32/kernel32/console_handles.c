/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: console APIs on handles that are not consoles (pipes, files,
 * the NUL device): GetConsoleMode / SetConsoleMode / WriteConsoleW /
 * GetConsoleScreenBufferInfo failures, GetFileType, GetStdHandle /
 * SetStdHandle rules; then FreeConsole, the no-console state, AllocConsole
 * (new console handles, default modes, writing, a second AllocConsole) and
 * FreeConsole again.
 *
 * How the harness starts the test (attached console or not) differs by
 * machine, so the first FreeConsole result is not printed; everything
 * after it starts from "no console".
 *
 * Docs: https://learn.microsoft.com/en-us/windows/console/getconsolemode
 *       https://learn.microsoft.com/en-us/windows/console/setconsolemode
 *       https://learn.microsoft.com/en-us/windows/console/writeconsole
 *       https://learn.microsoft.com/en-us/windows/console/getstdhandle
 *       https://learn.microsoft.com/en-us/windows/console/setstdhandle
 *       https://learn.microsoft.com/en-us/windows/console/allocconsole
 *       https://learn.microsoft.com/en-us/windows/console/freeconsole
 *       https://learn.microsoft.com/en-us/windows/console/attachconsole
 *       https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getfiletype
 */
#include "oracle.h"

static void not_console(const char *name, HANDLE h)
{
    DWORD mode = 0xdeadbeef, written = 0xdeadbeef;
    CONSOLE_SCREEN_BUFFER_INFO info;
    BOOL r;
    oracle_case(name);
    o_u32("file_type", GetFileType(h));
    SetLastError(ORACLE_SENTINEL);
    r = GetConsoleMode(h, &mode);
    o_bool("get_mode", r);
    o_u32("get_mode_error", GetLastError());
    o_bool("mode_untouched", mode == 0xdeadbeef);
    SetLastError(ORACLE_SENTINEL);
    r = SetConsoleMode(h, ENABLE_PROCESSED_OUTPUT);
    o_bool("set_mode", r);
    o_u32("set_mode_error", GetLastError());
    SetLastError(ORACLE_SENTINEL);
    r = WriteConsoleW(h, L"hi", 2, &written, NULL);
    o_bool("write_console", r);
    o_u32("write_console_error", GetLastError());
    o_bool("written_untouched", written == 0xdeadbeef);
    SetLastError(ORACLE_SENTINEL);
    r = GetConsoleScreenBufferInfo(h, &info);
    o_bool("screen_info", r);
    o_u32("screen_info_error", GetLastError());
    oracle_case_end();
}

int main(void)
{
    oracle_begin("kernel32/console_handles");

    HANDLE orig_in = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE orig_out = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE orig_err = GetStdHandle(STD_ERROR_HANDLE);

    oracle_case("stdout_is_crt_handle");
    o_bool("same", (HANDLE)_get_osfhandle(1) == orig_out);
    o_u32("stdout_type", GetFileType(orig_out));
    oracle_case_end();

    HANDLE rd, wr;
    CreatePipe(&rd, &wr, NULL, 0);
    not_console("pipe_read_end", rd);
    not_console("pipe_write_end", wr);
    HANDLE f = CreateFileW(L"plain.txt", GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    not_console("disk_file", f);
    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    not_console("nul_device", nul);
    not_console("invalid_handle_value", INVALID_HANDLE_VALUE);
    not_console("null_handle", NULL);

    {
        HANDLE h;
        BOOL r;
        ORACLE_CALL(h, GetStdHandle((DWORD)-20));
        oracle_case("get_std_bogus");
        o_handle("ret", h);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, SetStdHandle((DWORD)-20, wr));
        oracle_case("set_std_bogus");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, SetStdHandle(STD_OUTPUT_HANDLE, wr));
        oracle_case("set_std_pipe");
        o_bool("ret", r);
        o_lasterr();
        o_bool("get_returns_it", GetStdHandle(STD_OUTPUT_HANDLE) == wr);
        oracle_case_end();
        ORACLE_CALL(r, SetStdHandle(STD_ERROR_HANDLE, INVALID_HANDLE_VALUE));
        oracle_case("set_std_invalid_value");
        o_bool("ret", r);
        o_lasterr();
        o_handle("get", GetStdHandle(STD_ERROR_HANDLE));
        oracle_case_end();
        ORACLE_CALL(r, SetStdHandle(STD_ERROR_HANDLE, NULL));
        oracle_case("set_std_null");
        o_bool("ret", r);
        o_lasterr();
        o_handle("get", GetStdHandle(STD_ERROR_HANDLE));
        oracle_case_end();
        SetStdHandle(STD_OUTPUT_HANDLE, orig_out);
        SetStdHandle(STD_ERROR_HANDLE, orig_err);
    }

    {
        DWORD w = 0;
        char buf[8] = { 0 };
        DWORD got = 0;
        BOOL r = WriteFile(wr, "pipe", 4, &w, NULL);
        ReadFile(rd, buf, 4, &got, NULL);
        oracle_case("writefile_on_pipe_still_works");
        o_bool("ret", r);
        o_str("data", buf);
        oracle_case_end();
    }

    /* Detach from whatever console the harness gave us. */
    FreeConsole();
    {
        BOOL r;
        ORACLE_CALL(r, FreeConsole());
        oracle_case("free_without_console");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        UINT cp;
        ORACLE_CALL(cp, GetConsoleCP());
        oracle_case("no_console_state");
        o_bool("window_null", GetConsoleWindow() == NULL);
        o_u32("console_cp", cp);
        o_lasterr();
        oracle_case_end();
        HANDLE co;
        ORACLE_CALL(co, CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                    NULL, OPEN_EXISTING, 0, NULL));
        oracle_case("conout_without_console");
        o_handle("ret", co);
        o_lasterr();
        oracle_case_end();
        if (co != INVALID_HANDLE_VALUE) CloseHandle(co);
        ORACLE_CALL(r, AttachConsole(0xfffffff0));
        oracle_case("attach_bogus_pid");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
    }

    /* A fresh console. */
    {
        BOOL r;
        DWORD mode = 0;
        ORACLE_CALL(r, AllocConsole());
        oracle_case("alloc_console");
        o_bool("ret", r);
        o_lasterr();
        HANDLE in = GetStdHandle(STD_INPUT_HANDLE), out = GetStdHandle(STD_OUTPUT_HANDLE);
        HANDLE err = GetStdHandle(STD_ERROR_HANDLE);
        o_bool("stdout_replaced", out != orig_out);
        o_bool("stdin_replaced", in != orig_in);
        o_bool("stderr_is_stdout", err == out);
        o_bool("window_exists", GetConsoleWindow() != NULL);
        o_u32("out_type", GetFileType(out));
        o_bool("out_mode_ok", GetConsoleMode(out, &mode));
        o_hex32("out_mode", mode);
        mode = 0;
        o_bool("in_mode_ok", GetConsoleMode(in, &mode));
        o_hex32("in_mode", mode);
        o_bool("cp_nonzero", GetConsoleCP() != 0);
        o_bool("output_cp_matches_oem", GetConsoleOutputCP() == GetOEMCP());
        oracle_case_end();

        DWORD written = 0;
        ORACLE_CALL(r, WriteConsoleW(out, L"hello", 5, &written, NULL));
        oracle_case("write_console_new");
        o_bool("ret", r);
        o_lasterr();
        o_u32("written", written);
        oracle_case_end();
        ORACLE_CALL(r, WriteConsoleW(in, L"x", 1, &written, NULL));
        oracle_case("write_console_to_input");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, SetConsoleMode(out, 0xffff));
        oracle_case("set_mode_bogus_bits");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, SetConsoleMode(out, ENABLE_PROCESSED_OUTPUT));
        mode = 0;
        GetConsoleMode(out, &mode);
        oracle_case("set_mode_processed_only");
        o_bool("ret", r);
        o_lasterr();
        o_hex32("mode", mode);
        oracle_case_end();
        ORACLE_CALL(r, AllocConsole());
        oracle_case("alloc_console_twice");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, FreeConsole());
        oracle_case("free_console");
        o_bool("ret", r);
        o_lasterr();
        o_bool("window_gone", GetConsoleWindow() == NULL);
        o_bool("std_handle_kept", GetStdHandle(STD_OUTPUT_HANDLE) == out);
        SetLastError(ORACLE_SENTINEL);
        o_bool("old_handle_mode", GetConsoleMode(out, &mode));
        o_u32("old_handle_mode_error", GetLastError());
        oracle_case_end();
    }

    SetStdHandle(STD_INPUT_HANDLE, orig_in);
    SetStdHandle(STD_OUTPUT_HANDLE, orig_out);
    SetStdHandle(STD_ERROR_HANDLE, orig_err);
    return oracle_end();
}
