/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: mailslots: CreateMailslotW (names, duplicates, bad names),
 * GetMailslotInfo (max size, next message size, count, timeout),
 * SetMailslotInfo, writing through CreateFileW on \\.\mailslot\...,
 * message boundaries, oversized messages, ReadFile with a small buffer,
 * timeouts on an empty slot, and what happens after the server handle closes.
 *
 * Mailslot names are unique to this test.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-createmailslotw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-getmailslotinfo
 *       https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-setmailslotinfo
 *       https://learn.microsoft.com/en-us/windows/win32/ipc/mailslot-names
 *       https://learn.microsoft.com/en-us/windows/win32/ipc/writing-to-a-mailslot
 *       https://learn.microsoft.com/en-us/windows/win32/ipc/reading-from-a-mailslot
 */
#include "oracle.h"

#define SLOT L"\\\\.\\mailslot\\setu_oracle_k32\\slot1"

static void info(const char *name, HANDLE h)
{
    DWORD maxsz = 0xdead, next = 0xdead, count = 0xdead, timeout = 0xdead;
    BOOL r;
    ORACLE_CALL(r, GetMailslotInfo(h, &maxsz, &next, &count, &timeout));
    oracle_case(name);
    o_bool("ret", r);
    o_lasterr();
    if (r) {
        o_u32("max_size", maxsz);
        o_hex32("next_size", next);
        o_u32("count", count);
        o_hex32("timeout", timeout);
    }
    oracle_case_end();
}

static HANDLE client(void)
{
    return CreateFileW(SLOT, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
}

static void write_case(const char *name, HANDLE c, const void *data, DWORD n)
{
    DWORD w = 0xdead;
    BOOL r;
    ORACLE_CALL(r, WriteFile(c, data, n, &w, NULL));
    oracle_case(name);
    o_bool("ret", r);
    o_lasterr();
    o_u32("written", w);
    oracle_case_end();
}

static void read_case(const char *name, HANDLE s, DWORD size)
{
    char buf[256];
    DWORD got = 0xdead;
    BOOL r;
    memset(buf, 0, sizeof buf);
    ORACLE_CALL(r, ReadFile(s, buf, size, &got, NULL));
    oracle_case(name);
    o_bool("ret", r);
    o_lasterr();
    o_u32("got", got);
    if (r) o_bytes("data", buf, got < 256 ? got : 256);
    oracle_case_end();
}

int main(void)
{
    oracle_begin("kernel32/mailslots");

    HANDLE s, s2, c;
    ORACLE_CALL(s, CreateMailslotW(SLOT, 64, 0, NULL));
    oracle_case("create");
    o_handle("ret", s);
    o_lasterr();
    o_u32("file_type", GetFileType(s));
    oracle_case_end();

    ORACLE_CALL(s2, CreateMailslotW(SLOT, 64, 0, NULL));
    oracle_case("create_duplicate");
    o_handle("ret", s2);
    o_lasterr();
    oracle_case_end();
    ORACLE_CALL(s2, CreateMailslotW(L"\\\\.\\mailslot\\SETU_ORACLE_K32\\SLOT1", 64, 0, NULL));
    oracle_case("create_duplicate_other_case");
    o_handle("ret", s2);
    o_lasterr();
    oracle_case_end();
    ORACLE_CALL(s2, CreateMailslotW(L"\\\\.\\pipe\\setu_oracle_not_a_mailslot", 64, 0, NULL));
    oracle_case("create_wrong_prefix");
    o_handle("ret", s2);
    o_lasterr();
    oracle_case_end();
    ORACLE_CALL(s2, CreateMailslotW(L"setu_plain_name", 64, 0, NULL));
    oracle_case("create_plain_name");
    o_handle("ret", s2);
    o_lasterr();
    oracle_case_end();
    ORACLE_CALL(s2, CreateMailslotW(L"\\\\otherhost\\mailslot\\setu", 64, 0, NULL));
    oracle_case("create_remote_name");
    o_handle("ret", s2);
    o_lasterr();
    oracle_case_end();
    ORACLE_CALL(s2, CreateMailslotW(L"\\\\.\\mailslot\\", 64, 0, NULL));
    oracle_case("create_empty_slot_name");
    o_handle("ret", s2);
    o_lasterr();
    if (s2 != INVALID_HANDLE_VALUE) CloseHandle(s2);
    oracle_case_end();

    info("info_empty", s);

    ORACLE_CALL(c, client());
    oracle_case("open_client");
    o_handle("ret", c);
    o_lasterr();
    o_u32("file_type", GetFileType(c));
    oracle_case_end();
    {
        HANDLE r;
        ORACLE_CALL(r, CreateFileW(SLOT, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL));
        oracle_case("open_client_for_read");
        o_handle("ret", r);
        o_lasterr();
        oracle_case_end();
        if (r != INVALID_HANDLE_VALUE) CloseHandle(r);
        ORACLE_CALL(r, CreateFileW(L"\\\\.\\mailslot\\setu_oracle_k32\\missing", GENERIC_WRITE, FILE_SHARE_READ, NULL,
                                   OPEN_EXISTING, 0, NULL));
        oracle_case("open_missing_slot");
        o_handle("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, CreateFileW(SLOT, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL));
        oracle_case("open_client_no_share");
        o_handle("ret", r);
        o_lasterr();
        oracle_case_end();
        if (r != INVALID_HANDLE_VALUE) CloseHandle(r);
    }

    write_case("write_first", c, "first message", 13);
    write_case("write_second", c, "2nd", 3);
    write_case("write_empty", c, "", 0);
    {
        char big[100];
        memset(big, 'B', sizeof big);
        write_case("write_too_big", c, big, sizeof big);
        write_case("write_exact_max", c, big, 64);
    }
    info("info_after_writes", s);
    read_case("read_small_buffer", s, 4);
    info("info_after_small_read", s);
    read_case("read_first", s, 256);
    read_case("read_second", s, 256);
    read_case("read_empty_message", s, 256);
    read_case("read_max_message", s, 256);
    info("info_drained", s);

    {
        DWORD start = GetTickCount();
        BOOL r;
        ORACLE_CALL(r, SetMailslotInfo(s, 50));
        oracle_case("set_timeout_50");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        info("info_timeout_50", s);
        read_case("read_timeout_empty", s, 256);
        oracle_case("read_timeout_waited");
        o_bool("waited_some", GetTickCount() - start >= 30);
        oracle_case_end();
        ORACLE_CALL(r, SetMailslotInfo(c, 10));
        oracle_case("set_info_on_client");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        info("info_on_client", c);
        read_case("read_from_client", c, 16);
    }

    {
        DWORD maxsz = 0xdead;
        BOOL r;
        ORACLE_CALL(r, GetMailslotInfo(s, &maxsz, NULL, NULL, NULL));
        oracle_case("info_null_outputs");
        o_bool("ret", r);
        o_lasterr();
        o_u32("max_size", maxsz);
        oracle_case_end();
    }
    {
        HANDLE z;
        ORACLE_CALL(z, CreateMailslotW(L"\\\\.\\mailslot\\setu_oracle_k32\\nolimit", 0, MAILSLOT_WAIT_FOREVER, NULL));
        oracle_case("create_no_limit");
        o_handle("ret", z);
        o_lasterr();
        oracle_case_end();
        info("info_no_limit", z);
        CloseHandle(z);
    }

    /* Server goes away. */
    write_case("write_before_close", c, "late", 4);
    CloseHandle(s);
    write_case("write_after_server_closed", c, "x", 1);
    ORACLE_CALL(s2, CreateMailslotW(SLOT, 64, 0, NULL));
    oracle_case("recreate_after_close");
    o_handle("ret", s2);
    o_lasterr();
    oracle_case_end();
    if (s2 != INVALID_HANDLE_VALUE) {
        info("info_recreated", s2);
        CloseHandle(s2);
    }
    CloseHandle(c);

    return oracle_end();
}
