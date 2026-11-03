/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: anonymous and named pipes: CreatePipe, CreateNamedPipeW,
 * ConnectNamedPipe, PeekNamedPipe, message vs byte mode, PIPE_NOWAIT,
 * instance limits, broken / disconnected pipe errors and pipe information.
 *
 * Everything runs on one thread. Only non-blocking or already-satisfiable
 * operations are used (data is always written before it is read), so results
 * do not depend on scheduling.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/namedpipeapi/nf-namedpipeapi-createpipe
 *       https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-createnamedpipew
 *       https://learn.microsoft.com/en-us/windows/win32/api/namedpipeapi/nf-namedpipeapi-connectnamedpipe
 *       https://learn.microsoft.com/en-us/windows/win32/api/namedpipeapi/nf-namedpipeapi-disconnectnamedpipe
 *       https://learn.microsoft.com/en-us/windows/win32/api/namedpipeapi/nf-namedpipeapi-peeknamedpipe
 *       https://learn.microsoft.com/en-us/windows/win32/api/namedpipeapi/nf-namedpipeapi-waitnamedpipew
 *       https://learn.microsoft.com/en-us/windows/win32/api/namedpipeapi/nf-namedpipeapi-getnamedpipeinfo
 *       https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-getnamedpipehandlestatew
 *       https://learn.microsoft.com/en-us/windows/win32/api/namedpipeapi/nf-namedpipeapi-setnamedpipehandlestate
 *       https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-getnamedpipeclientprocessid
 *       https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getfiletype
 *       https://learn.microsoft.com/en-us/windows/win32/ipc/named-pipe-type-read-and-wait-modes
 *       https://learn.microsoft.com/en-us/windows/win32/ipc/pipe-names
 */
#include "oracle.h"

#define PIPE(s) L"\\\\.\\pipe\\SetuOracle_pipes_" s

#define BOOL_CASE(name, expr)     \
    do {                          \
        BOOL ok_;                 \
        ORACLE_CALL(ok_, (expr)); \
        oracle_case(name);        \
        o_bool("ret", ok_);       \
        o_lasterr();              \
        oracle_case_end();        \
    } while (0)

static void write_case(const char *name, HANDLE h, const char *data, DWORD n)
{
    DWORD w = 0xAAAA;
    BOOL ok;
    ORACLE_CALL(ok, WriteFile(h, data, n, &w, NULL));
    oracle_case(name);
    o_bool("ret", ok);
    o_lasterr();
    o_u32("written", w);
    oracle_case_end();
}

static void read_case(const char *name, HANDLE h, DWORD n)
{
    char buf[256];
    DWORD r = 0xAAAA;
    BOOL ok;
    ORACLE_CALL(ok, ReadFile(h, buf, n, &r, NULL));
    oracle_case(name);
    o_bool("ret", ok);
    o_lasterr();
    o_u32("read", r);
    if (r <= n) o_bytes("data", buf, r);
    oracle_case_end();
}

static void peek_case(const char *name, HANDLE h, DWORD n)
{
    char buf[256];
    DWORD r = 0xAAAA, avail = 0xAAAA, left = 0xAAAA;
    BOOL ok;
    ORACLE_CALL(ok, PeekNamedPipe(h, n ? buf : NULL, n, &r, &avail, &left));
    oracle_case(name);
    o_bool("ret", ok);
    o_lasterr();
    if (ok) {
        o_u32("read", r);
        if (n && r <= n) o_bytes("data", buf, r);
        o_u32("avail", avail);
        o_u32("left_this_message", left);
    }
    oracle_case_end();
}

static HANDLE create_case(const char *name, const wchar_t *pname, DWORD open, DWORD mode, DWORD max)
{
    HANDLE h;
    ORACLE_CALL(h, CreateNamedPipeW(pname, open, mode, max, 1024, 1024, 0, NULL));
    oracle_case(name);
    o_handle("ret", h);
    o_lasterr();
    oracle_case_end();
    return h;
}

static HANDLE open_case(const char *name, const wchar_t *pname, DWORD access)
{
    HANDLE h;
    ORACLE_CALL(h, CreateFileW(pname, access, 0, NULL, OPEN_EXISTING, 0, NULL));
    oracle_case(name);
    o_handle("ret", h);
    o_lasterr();
    oracle_case_end();
    return h;
}

static void info_case(const char *name, HANDLE h)
{
    DWORD flags = 0xAAAA, outsz = 0xAAAA, insz = 0xAAAA, maxi = 0xAAAA;
    BOOL ok;
    ORACLE_CALL(ok, GetNamedPipeInfo(h, &flags, &outsz, &insz, &maxi));
    oracle_case(name);
    o_bool("ret", ok);
    o_lasterr();
    if (ok) {
        o_hex32("flags", flags);
        o_u32("out_size", outsz);
        o_u32("in_size", insz);
        o_u32("max_instances", maxi);
    }
    oracle_case_end();
}

static void state_case(const char *name, HANDLE h)
{
    DWORD state = 0xAAAA, cur = 0xAAAA;
    BOOL ok;
    ORACLE_CALL(ok, GetNamedPipeHandleStateW(h, &state, &cur, NULL, NULL, NULL, 0));
    oracle_case(name);
    o_bool("ret", ok);
    o_lasterr();
    if (ok) {
        o_hex32("state", state);
        o_u32("instances", cur);
    }
    oracle_case_end();
}

int main(void)
{
    oracle_begin("kernel32/pipes");

    HANDLE r, w, s, c, s2, c2;
    BOOL ok;

    /* ---- anonymous pipes ---- */
    ORACLE_CALL(ok, CreatePipe(&r, &w, NULL, 0));
    oracle_case("anon_create");
    o_bool("ret", ok);
    o_lasterr();
    o_u32("type_read", GetFileType(r));
    o_u32("type_write", GetFileType(w));
    oracle_case_end();
    peek_case("anon_peek_empty", r, 16);
    write_case("anon_write_5", w, "hello", 5);
    write_case("anon_write_zero", w, "", 0);
    peek_case("anon_peek_2_of_5", r, 2);
    peek_case("anon_peek_null_buffer", r, 0);
    read_case("anon_read_3", r, 3);
    peek_case("anon_peek_after_read", r, 16);
    read_case("anon_read_rest", r, 16);
    write_case("anon_write_on_read_end", r, "x", 1);
    read_case("anon_read_on_write_end", w, 1);
    info_case("anon_info_read_end", r);
    info_case("anon_info_write_end", w);
    state_case("anon_state_read_end", r);
    {
        DWORD mode = PIPE_READMODE_MESSAGE;
        BOOL_CASE("anon_set_message_mode", SetNamedPipeHandleState(r, &mode, NULL, NULL));
        mode = PIPE_READMODE_BYTE | PIPE_NOWAIT;
        BOOL_CASE("anon_set_nowait", SetNamedPipeHandleState(r, &mode, NULL, NULL));
        state_case("anon_state_after_nowait", r);
        read_case("anon_nowait_read_empty", r, 4);
        mode = PIPE_READMODE_BYTE | PIPE_WAIT;
        SetNamedPipeHandleState(r, &mode, NULL, NULL);
    }
    {
        LARGE_INTEGER sz;
        ORACLE_CALL(ok, GetFileSizeEx(r, &sz));
        oracle_case("anon_get_file_size");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        DWORD p;
        ORACLE_CALL(p, SetFilePointer(r, 10, NULL, FILE_BEGIN));
        oracle_case("anon_set_file_pointer");
        o_hex32("ret", p);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(p, SetFilePointer(r, 0, NULL, FILE_CURRENT));
        oracle_case("anon_get_file_pointer_after_set");
        o_hex32("ret", p);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(p, SetFilePointer(r, 0, NULL, FILE_END));
        oracle_case("anon_set_file_pointer_end");
        o_hex32("ret", p);
        o_lasterr();
        oracle_case_end();
        BOOL_CASE("anon_flush_write_end", FlushFileBuffers(w));
    }
    write_case("anon_write_before_close", w, "bye", 3);
    CloseHandle(w);
    peek_case("anon_peek_after_writer_closed", r, 16);
    read_case("anon_read_after_writer_closed", r, 16);
    read_case("anon_read_after_writer_closed_empty", r, 16);
    peek_case("anon_peek_after_writer_closed_empty", r, 16);
    CloseHandle(r);

    CreatePipe(&r, &w, NULL, 0);
    CloseHandle(r);
    write_case("anon_write_after_reader_closed", w, "x", 1);
    write_case("anon_write_zero_after_reader_closed", w, "", 0);
    CloseHandle(w);

    /* ---- named pipe, message mode ---- */
    s = create_case("msg_create", PIPE("msg"), PIPE_ACCESS_DUPLEX,
                    PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT, 1);
    info_case("msg_info_server_before_connect", s);
    state_case("msg_state_server_before_connect", s);
    {
        DWORD mode = PIPE_READMODE_MESSAGE | PIPE_NOWAIT;
        SetNamedPipeHandleState(s, &mode, NULL, NULL);
        BOOL_CASE("msg_connect_nowait_no_client", ConnectNamedPipe(s, NULL));
        read_case("msg_read_listening", s, 4);
        write_case("msg_write_listening", s, "x", 1);
        mode = PIPE_READMODE_MESSAGE | PIPE_WAIT;
        SetNamedPipeHandleState(s, &mode, NULL, NULL);
    }
    c = open_case("msg_client_open", PIPE("msg"), GENERIC_READ | GENERIC_WRITE);
    BOOL_CASE("msg_connect_after_client", ConnectNamedPipe(s, NULL));
    c2 = open_case("msg_client_open_busy", PIPE("msg"), GENERIC_READ | GENERIC_WRITE);
    if (c2 != INVALID_HANDLE_VALUE) CloseHandle(c2);
    BOOL_CASE("msg_wait_named_pipe_busy", WaitNamedPipeW(PIPE("msg"), 1));
    s2 = create_case("msg_create_over_max", PIPE("msg"), PIPE_ACCESS_DUPLEX,
                     PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE, 1);
    if (s2 != INVALID_HANDLE_VALUE) CloseHandle(s2);
    info_case("msg_info_client", c);
    state_case("msg_state_client", c);
    state_case("msg_state_server", s);
    {
        ULONG pid = 0;
        ORACLE_CALL(ok, GetNamedPipeClientProcessId(s, &pid));
        oracle_case("msg_client_process_id");
        o_bool("ret", ok);
        o_lasterr();
        o_bool("is_self", pid == GetCurrentProcessId());
        oracle_case_end();
        pid = 0;
        ORACLE_CALL(ok, GetNamedPipeServerProcessId(c, &pid));
        oracle_case("msg_server_process_id");
        o_bool("ret", ok);
        o_lasterr();
        o_bool("is_self", pid == GetCurrentProcessId());
        oracle_case_end();
    }
    write_case("msg_client_write_10", c, "0123456789", 10);
    write_case("msg_client_write_3", c, "abc", 3);
    write_case("msg_client_write_empty", c, "", 0);
    peek_case("msg_peek_4", s, 4);
    peek_case("msg_peek_null_buffer", s, 0);
    read_case("msg_read_4_of_10", s, 4);
    peek_case("msg_peek_after_partial", s, 16);
    read_case("msg_read_rest_of_10", s, 16);
    read_case("msg_read_3", s, 16);
    peek_case("msg_peek_empty_message", s, 16);
    read_case("msg_read_empty_message", s, 16);
    peek_case("msg_peek_nothing", s, 16);
    write_case("msg_server_write", s, "pong", 4);
    {
        DWORD mode = PIPE_READMODE_BYTE;
        BOOL_CASE("msg_client_set_byte_mode", SetNamedPipeHandleState(c, &mode, NULL, NULL));
        read_case("msg_client_byte_read_2", c, 2);
        read_case("msg_client_byte_read_rest", c, 16);
    }
    BOOL_CASE("msg_disconnect", DisconnectNamedPipe(s));
    write_case("msg_client_write_after_disconnect", c, "x", 1);
    read_case("msg_client_read_after_disconnect", c, 4);
    peek_case("msg_client_peek_after_disconnect", c, 4);
    state_case("msg_state_server_after_disconnect", s);
    read_case("msg_server_read_after_disconnect", s, 4);
    BOOL_CASE("msg_disconnect_again", DisconnectNamedPipe(s));
    CloseHandle(c);
    /* After a disconnect the instance can be reused. */
    c = open_case("msg_client_open_after_disconnect", PIPE("msg"), GENERIC_READ | GENERIC_WRITE);
    if (c != INVALID_HANDLE_VALUE) CloseHandle(c);
    CloseHandle(s);
    c = open_case("msg_client_open_after_server_closed", PIPE("msg"), GENERIC_READ | GENERIC_WRITE);
    if (c != INVALID_HANDLE_VALUE) CloseHandle(c);

    /* ---- named pipe, byte mode, broken pipe ---- */
    s = create_case("byte_create", PIPE("byte"), PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE, PIPE_UNLIMITED_INSTANCES);
    s2 = create_case("byte_create_second_instance", PIPE("byte"), PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE,
                     PIPE_UNLIMITED_INSTANCES);
    {
        HANDLE s3 = create_case("byte_create_first_instance_flag", PIPE("byte"),
                                PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE, PIPE_TYPE_BYTE,
                                PIPE_UNLIMITED_INSTANCES);
        if (s3 != INVALID_HANDLE_VALUE) CloseHandle(s3);
        s3 = create_case("byte_create_different_max", PIPE("byte"), PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE, 4);
        if (s3 != INVALID_HANDLE_VALUE) CloseHandle(s3);
        s3 = create_case("byte_create_message_type_mismatch", PIPE("byte"), PIPE_ACCESS_DUPLEX,
                         PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE, PIPE_UNLIMITED_INSTANCES);
        if (s3 != INVALID_HANDLE_VALUE) CloseHandle(s3);
        s3 = create_case("byte_create_inbound_mismatch", PIPE("byte"), PIPE_ACCESS_INBOUND, PIPE_TYPE_BYTE,
                         PIPE_UNLIMITED_INSTANCES);
        if (s3 != INVALID_HANDLE_VALUE) CloseHandle(s3);
    }
    state_case("byte_state_two_instances", s);
    c = open_case("byte_client_open", PIPE("byte"), GENERIC_READ | GENERIC_WRITE);
    c2 = open_case("byte_client_open_second", PIPE("byte"), GENERIC_READ | GENERIC_WRITE);
    {
        HANDLE c3 = open_case("byte_client_open_third_no_instance", PIPE("byte"), GENERIC_READ | GENERIC_WRITE);
        if (c3 != INVALID_HANDLE_VALUE) CloseHandle(c3);
    }
    {
        DWORD mode = PIPE_READMODE_MESSAGE;
        BOOL_CASE("byte_client_set_message_mode", SetNamedPipeHandleState(c, &mode, NULL, NULL));
    }
    write_case("byte_client_write_a", c, "ab", 2);
    write_case("byte_client_write_b", c, "cd", 2);
    peek_case("byte_peek_merged", s, 16);
    read_case("byte_read_merged", s, 16);
    write_case("byte_client_write_before_close", c, "zz", 2);
    CloseHandle(c);
    read_case("byte_server_read_after_client_closed", s, 16);
    read_case("byte_server_read_after_client_closed_empty", s, 16);
    write_case("byte_server_write_after_client_closed", s, "x", 1);
    peek_case("byte_server_peek_after_client_closed", s, 16);
    BOOL_CASE("byte_connect_after_client_closed", ConnectNamedPipe(s, NULL));
    CloseHandle(s2);
    read_case("byte_client2_read_after_server_closed", c2, 4);
    write_case("byte_client2_write_after_server_closed", c2, "x", 1);
    CloseHandle(c2);
    CloseHandle(s);

    /* ---- read-only clients, inbound pipes ---- */
    s = create_case("inbound_create", PIPE("in"), PIPE_ACCESS_INBOUND, PIPE_TYPE_BYTE, 1);
    c = open_case("inbound_client_open_read_write", PIPE("in"), GENERIC_READ | GENERIC_WRITE);
    if (c != INVALID_HANDLE_VALUE) CloseHandle(c);
    c = open_case("inbound_client_open_write", PIPE("in"), GENERIC_WRITE);
    if (c != INVALID_HANDLE_VALUE) {
        write_case("inbound_server_write", s, "x", 1);
        read_case("inbound_client_read", c, 1);
        CloseHandle(c);
    }
    CloseHandle(s);

    /* ---- names and lookup errors ---- */
    s = create_case("create_no_pipe_prefix", L"SetuOracle_pipes_noprefix", PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE, 1);
    if (s != INVALID_HANDLE_VALUE) CloseHandle(s);
    s = create_case("create_bad_open_mode", PIPE("bad"), 0, PIPE_TYPE_BYTE, 1);
    if (s != INVALID_HANDLE_VALUE) CloseHandle(s);
    s = create_case("create_max_instances_zero", PIPE("zero"), PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE, 0);
    if (s != INVALID_HANDLE_VALUE) CloseHandle(s);
    s = create_case("create_max_instances_256", PIPE("big"), PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE, 256);
    if (s != INVALID_HANDLE_VALUE) CloseHandle(s);
    s = create_case("create_max_instances_255", PIPE("big255"), PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE, 255);
    if (s != INVALID_HANDLE_VALUE) CloseHandle(s);
    s = create_case("create_max_instances_254", PIPE("big254"), PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE, 254);
    if (s != INVALID_HANDLE_VALUE) CloseHandle(s);
    s = create_case("create_first_instance_flag_fresh", PIPE("first"),
                    PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE, PIPE_TYPE_BYTE, 1);
    if (s != INVALID_HANDLE_VALUE) CloseHandle(s);
    s = create_case("create_readmode_message_on_byte", PIPE("rm"), PIPE_ACCESS_DUPLEX,
                    PIPE_TYPE_BYTE | PIPE_READMODE_MESSAGE, 1);
    if (s != INVALID_HANDLE_VALUE) CloseHandle(s);
    c = open_case("open_missing_pipe", PIPE("missing"), GENERIC_READ);
    BOOL_CASE("wait_missing_pipe", WaitNamedPipeW(PIPE("missing"), 1));
    s = CreateNamedPipeW(PIPE("free"), PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE, 1, 0, 0, 0, NULL);
    BOOL_CASE("wait_free_instance", WaitNamedPipeW(PIPE("free"), NMPWAIT_NOWAIT));
    BOOL_CASE("wait_free_instance_other_case", WaitNamedPipeW(L"\\\\.\\PIPE\\SETUORACLE_PIPES_FREE", 1));
    c = open_case("open_other_case", L"\\\\.\\PIPE\\SETUORACLE_PIPES_FREE", GENERIC_READ);
    if (c != INVALID_HANDLE_VALUE) CloseHandle(c);
    info_case("zero_buffer_sizes_info", s);
    CloseHandle(s);
    BOOL_CASE("connect_on_file_handle", ConnectNamedPipe(
        CreateFileW(L"f.txt", GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_FLAG_DELETE_ON_CLOSE, NULL),
        NULL));
    {
        HANDLE f = CreateFileW(L"g.txt", GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        peek_case("peek_on_file_handle", f, 4);
        info_case("info_on_file_handle", f);
        CloseHandle(f);
    }

    /* Probes: the file position of a pipe. Is it kept per file object or
     * per handle, and do reads and writes move it? */
    {
        HANDLE r, w, dup = NULL;
        DWORD p;
        LARGE_INTEGER d, np;
        BOOL ok;
        CreatePipe(&r, &w, NULL, 0);
#define POS_CASE(name, h, dist, method)                       \
    do {                                                      \
        ORACLE_CALL(p, SetFilePointer((h), (dist), NULL, (method))); \
        oracle_case(name);                                    \
        o_hex32("ret", p);                                    \
        o_lasterr();                                          \
        oracle_case_end();                                    \
    } while (0)
        POS_CASE("probe_pos_fresh_read_end", r, 0, FILE_CURRENT);
        POS_CASE("probe_pos_set_10", r, 10, FILE_BEGIN);
        POS_CASE("probe_pos_write_end_untouched", w, 0, FILE_CURRENT);
        write_case("probe_pos_write_5", w, "hello", 5);
        POS_CASE("probe_pos_read_end_after_write", r, 0, FILE_CURRENT);
        POS_CASE("probe_pos_write_end_after_write", w, 0, FILE_CURRENT);
        read_case("probe_pos_read_3", r, 3);
        POS_CASE("probe_pos_read_end_after_read", r, 0, FILE_CURRENT);
        POS_CASE("probe_pos_end_with_data", r, 0, FILE_END);
        POS_CASE("probe_pos_after_end_with_data", r, 0, FILE_CURRENT);
        read_case("probe_pos_read_rest", r, 16);
        POS_CASE("probe_pos_current_plus_5", r, 5, FILE_CURRENT);
        POS_CASE("probe_pos_negative", r, -1, FILE_BEGIN);
        POS_CASE("probe_pos_after_negative", r, 0, FILE_CURRENT);
        DuplicateHandle(GetCurrentProcess(), r, GetCurrentProcess(), &dup, 0, FALSE, DUPLICATE_SAME_ACCESS);
        POS_CASE("probe_pos_dup_sees", dup, 0, FILE_CURRENT);
        POS_CASE("probe_pos_dup_set_20", dup, 20, FILE_BEGIN);
        POS_CASE("probe_pos_orig_after_dup_set", r, 0, FILE_CURRENT);
        CloseHandle(dup);
        POS_CASE("probe_pos_write_end_set_7", w, 7, FILE_BEGIN);
        write_case("probe_pos_write_2", w, "ab", 2);
        POS_CASE("probe_pos_write_end_after_set_write", w, 0, FILE_CURRENT);
        read_case("probe_pos_read_2", r, 2);
        POS_CASE("probe_pos_read_end_final", r, 0, FILE_CURRENT);
        d.QuadPart = 0x7fffffffffffffffLL;
        np.QuadPart = -12345;
        ORACLE_CALL(ok, SetFilePointerEx(r, d, &np, FILE_BEGIN));
        oracle_case("probe_pos_huge");
        o_bool("ret", ok);
        o_lasterr();
        o_i64("new_pos", np.QuadPart);
        oracle_case_end();
        d.QuadPart = 0;
        np.QuadPart = -12345;
        ORACLE_CALL(ok, SetFilePointerEx(r, d, &np, FILE_CURRENT));
        oracle_case("probe_pos_after_huge");
        o_bool("ret", ok);
        o_lasterr();
        o_i64("new_pos", np.QuadPart);
        oracle_case_end();
        CloseHandle(r);
        CloseHandle(w);

        /* named pipe: server and client ends */
        HANDLE s = CreateNamedPipeW(PIPE("pos"), PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE, 1, 0, 0, 0, NULL);
        HANDLE c = CreateFileW(PIPE("pos"), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        POS_CASE("probe_pos_named_server_set_4", s, 4, FILE_BEGIN);
        POS_CASE("probe_pos_named_client", c, 0, FILE_CURRENT);
        POS_CASE("probe_pos_named_server", s, 0, FILE_CURRENT);
        CloseHandle(c);
        CloseHandle(s);
#undef POS_CASE
    }

    return oracle_end();
}
