/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: overlapped (asynchronous) I/O: ReadFile/WriteFile with an
 * OVERLAPPED structure on files and named pipes, GetOverlappedResult(Ex),
 * the event in the OVERLAPPED, cancellation, ReadFileEx/WriteFileEx
 * completion routines and I/O completion ports.
 *
 * Whether a file operation completes at once or returns ERROR_IO_PENDING is
 * up to the file system, so for disk files the test records only the final
 * outcome ("sync" and "pending" both print as "started"). Pipe reads with no
 * data are always pending, so those are recorded exactly.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-readfile
 *       https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-writefile
 *       https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-readfileex
 *       https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-writefileex
 *       https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-getoverlappedresult
 *       https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-getoverlappedresultex
 *       https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-cancelio
 *       https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-cancelioex
 *       https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-createiocompletionport
 *       https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-getqueuedcompletionstatus
 *       https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-getqueuedcompletionstatusex
 *       https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-postqueuedcompletionstatus
 *       https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-setfilecompletionnotificationmodes
 *       https://learn.microsoft.com/en-us/windows/win32/fileio/synchronous-and-asynchronous-i-o
 */
#include "oracle.h"

#define PIPE(s) L"\\\\.\\pipe\\SetuOracle_ovl_" s

#define BOOL_CASE(name, expr)     \
    do {                          \
        BOOL ok_;                 \
        ORACLE_CALL(ok_, (expr)); \
        oracle_case(name);        \
        o_bool("ret", ok_);       \
        o_lasterr();              \
        oracle_case_end();        \
    } while (0)

static int signalled(HANDLE h) { return WaitForSingleObject(h, 0) == WAIT_OBJECT_0; }

/* For disk files: "started" if TRUE or ERROR_IO_PENDING, else the error. */
static void start_fields(BOOL ok, DWORD err)
{
    if (ok || err == ERROR_IO_PENDING) o_str("start", "started");
    else o_u32("start_error", err);
}

static void result_fields(HANDLE h, OVERLAPPED *ov)
{
    DWORD n = 0xAAAA;
    BOOL ok;
    ORACLE_CALL(ok, GetOverlappedResult(h, ov, &n, TRUE));
    o_bool("result", ok);
    o_lasterr();
    o_u32("bytes", n);
    o_hex32("internal", (DWORD)ov->Internal);
    o_u64("internal_high", ov->InternalHigh);
}

static HANDLE open_ovl(const wchar_t *name, DWORD disp)
{
    return CreateFileW(name, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, disp,
                       FILE_FLAG_OVERLAPPED, NULL);
}

static int apc_calls;
static DWORD apc_err, apc_bytes;
static OVERLAPPED *apc_ov;
static void CALLBACK completion(DWORD err, DWORD bytes, OVERLAPPED *ov)
{
    apc_calls++;
    apc_err = err;
    apc_bytes = bytes;
    apc_ov = ov;
}

int main(void)
{
    oracle_begin("kernel32/overlapped_io");

    HANDLE f, ev;
    OVERLAPPED ov;
    BOOL ok;
    char buf[64];
    DWORD n;

    ev = CreateEventW(NULL, TRUE, FALSE, NULL);

    /* ---- disk file: write, read, EOF ---- */
    f = open_ovl(L"a.bin", CREATE_ALWAYS);
    memset(&ov, 0, sizeof ov);
    ov.hEvent = ev;
    ORACLE_CALL(ok, WriteFile(f, "0123456789", 10, NULL, &ov));
    oracle_case("file_write_offset_0");
    start_fields(ok, oracle_last_error);
    result_fields(f, &ov);
    o_bool("event_signalled", signalled(ev));
    oracle_case_end();

    memset(&ov, 0, sizeof ov);
    ov.hEvent = ev;
    ov.Offset = 20;
    ORACLE_CALL(ok, WriteFile(f, "XY", 2, NULL, &ov));
    oracle_case("file_write_offset_20_leaves_gap");
    start_fields(ok, oracle_last_error);
    result_fields(f, &ov);
    oracle_case_end();
    oracle_case("file_state_after_gap_write");
    o_file_state("file", L"a.bin");
    oracle_case_end();

    memset(&ov, 0, sizeof ov);
    ov.hEvent = ev;
    ov.Offset = 0xFFFFFFFF;
    ov.OffsetHigh = 0xFFFFFFFF;
    ORACLE_CALL(ok, WriteFile(f, "E", 1, NULL, &ov));
    oracle_case("file_write_append_offset");
    start_fields(ok, oracle_last_error);
    result_fields(f, &ov);
    oracle_case_end();
    oracle_case("file_state_after_append_write");
    o_file_state("file", L"a.bin");
    oracle_case_end();

    SetEvent(ev);
    memset(&ov, 0, sizeof ov);
    ov.hEvent = ev;
    ov.Offset = 3;
    ORACLE_CALL(ok, ReadFile(f, buf, 4, NULL, &ov));
    oracle_case("file_read_offset_3");
    start_fields(ok, oracle_last_error);
    result_fields(f, &ov);
    o_bytes("data", buf, 4);
    oracle_case_end();

    memset(&ov, 0, sizeof ov);
    ov.hEvent = ev;
    ov.Offset = 20;
    ORACLE_CALL(ok, ReadFile(f, buf, 10, NULL, &ov));
    oracle_case("file_read_short_at_end");
    start_fields(ok, oracle_last_error);
    result_fields(f, &ov);
    oracle_case_end();

    ResetEvent(ev);
    memset(&ov, 0, sizeof ov);
    ov.hEvent = ev;
    ov.Offset = 1000;
    ORACLE_CALL(ok, ReadFile(f, buf, 4, NULL, &ov));
    oracle_case("file_read_past_eof");
    start_fields(ok, oracle_last_error);
    result_fields(f, &ov);
    o_bool("event_signalled", signalled(ev));
    oracle_case_end();

    memset(&ov, 0, sizeof ov);
    ov.hEvent = ev;
    ov.Offset = 1000;
    ORACLE_CALL(ok, ReadFile(f, buf, 0, NULL, &ov));
    oracle_case("file_read_zero_past_eof");
    start_fields(ok, oracle_last_error);
    result_fields(f, &ov);
    oracle_case_end();

    /* ReadFile with NULL lpOverlapped on an overlapped handle. */
    n = 0xAAAA;
    ORACLE_CALL(ok, ReadFile(f, buf, 4, &n, NULL));
    oracle_case("file_read_no_overlapped_on_async_handle");
    o_bool("ret", ok);
    o_lasterr();
    o_u32("read", n);
    oracle_case_end();
    n = 0xAAAA;
    ORACLE_CALL(ok, WriteFile(f, "q", 1, &n, NULL));
    oracle_case("file_write_no_overlapped_on_async_handle");
    o_bool("ret", ok);
    o_lasterr();
    o_u32("written", n);
    oracle_case_end();

    /* No event: the file handle itself is signalled. */
    memset(&ov, 0, sizeof ov);
    ov.Offset = 0;
    ORACLE_CALL(ok, ReadFile(f, buf, 2, NULL, &ov));
    oracle_case("file_read_without_event");
    start_fields(ok, oracle_last_error);
    result_fields(f, &ov);
    o_bool("file_handle_signalled", signalled(f));
    oracle_case_end();

    BOOL_CASE("cancel_io_nothing_pending", CancelIo(f));
    BOOL_CASE("cancel_io_ex_nothing_pending", CancelIoEx(f, NULL));
    BOOL_CASE("cancel_io_ex_completed_overlapped", CancelIoEx(f, &ov));
    BOOL_CASE("cancel_io_ex_invalid_handle", CancelIoEx((HANDLE)(ULONG_PTR)0x1234, NULL));

    /* ---- ReadFileEx / WriteFileEx ---- */
    apc_calls = 0;
    memset(&ov, 0, sizeof ov);
    ov.Offset = 2;
    ORACLE_CALL(ok, ReadFileEx(f, buf, 3, &ov, completion));
    oracle_case("readfileex_start");
    o_bool("ret", ok);
    o_lasterr();
    o_i64("calls_before_alertable", apc_calls);
    oracle_case_end();
    ORACLE_CALL(n, SleepEx(INFINITE, TRUE));
    oracle_case("readfileex_complete");
    o_hex32("sleep_ret", n);
    o_i64("calls", apc_calls);
    o_u32("err", apc_err);
    o_u32("bytes", apc_bytes);
    o_bool("same_overlapped", apc_ov == &ov);
    o_bytes("data", buf, 3);
    oracle_case_end();

    apc_calls = 0;
    memset(&ov, 0, sizeof ov);
    ov.Offset = 5000;
    ORACLE_CALL(ok, ReadFileEx(f, buf, 3, &ov, completion));
    oracle_case("readfileex_past_eof");
    o_bool("ret", ok);
    o_lasterr();
    oracle_case_end();
    if (ok) SleepEx(INFINITE, TRUE);
    oracle_case("readfileex_past_eof_completion");
    o_i64("calls", apc_calls);
    if (apc_calls) {
        o_u32("err", apc_err);
        o_u32("bytes", apc_bytes);
    }
    oracle_case_end();

    apc_calls = 0;
    memset(&ov, 0, sizeof ov);
    ov.Offset = 30;
    ORACLE_CALL(ok, WriteFileEx(f, "WX", 2, &ov, completion));
    oracle_case("writefileex_start");
    o_bool("ret", ok);
    o_lasterr();
    oracle_case_end();
    if (ok) SleepEx(INFINITE, TRUE);
    oracle_case("writefileex_complete");
    o_i64("calls", apc_calls);
    o_u32("err", apc_err);
    o_u32("bytes", apc_bytes);
    oracle_case_end();
    CloseHandle(f);

    {   /* ReadFileEx on a synchronous handle. */
        HANDLE s = CreateFileW(L"a.bin", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        apc_calls = 0;
        memset(&ov, 0, sizeof ov);
        ORACLE_CALL(ok, ReadFileEx(s, buf, 3, &ov, completion));
        oracle_case("readfileex_sync_handle");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        if (ok) SleepEx(0, TRUE);

        /* An OVERLAPPED on a synchronous handle: reads at Offset, moves the pointer, sets the event. */
        ResetEvent(ev);
        memset(&ov, 0, sizeof ov);
        ov.hEvent = ev;
        ov.Offset = 4;
        n = 0xAAAA;
        ORACLE_CALL(ok, ReadFile(s, buf, 3, &n, &ov));
        oracle_case("sync_handle_with_overlapped");
        o_bool("ret", ok);
        o_lasterr();
        o_u32("read", n);
        o_bytes("data", buf, 3);
        o_bool("event_signalled", signalled(ev));
        o_u32("pointer_after", SetFilePointer(s, 0, NULL, FILE_CURRENT));
        o_hex32("internal", (DWORD)ov.Internal);
        o_u64("internal_high", ov.InternalHigh);
        oracle_case_end();

        memset(&ov, 0, sizeof ov);
        ov.Offset = 4000;
        n = 0xAAAA;
        ORACLE_CALL(ok, ReadFile(s, buf, 3, &n, &ov));
        oracle_case("sync_handle_with_overlapped_past_eof");
        o_bool("ret", ok);
        o_lasterr();
        o_u32("read", n);
        o_hex32("internal", (DWORD)ov.Internal);
        oracle_case_end();
        CloseHandle(s);
    }

    /* ---- named pipe: reads that must pend ---- */
    {
        HANDLE srv = CreateNamedPipeW(PIPE("p"), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                                      PIPE_TYPE_BYTE, 1, 1024, 1024, 0, NULL);
        OVERLAPPED cov;
        memset(&cov, 0, sizeof cov);
        cov.hEvent = CreateEventW(NULL, TRUE, TRUE, NULL);
        ORACLE_CALL(ok, ConnectNamedPipe(srv, &cov));
        oracle_case("pipe_connect_overlapped_pending");
        o_bool("ret", ok);
        o_lasterr();
        o_bool("event_reset", !signalled(cov.hEvent));
        o_bool("has_completed", HasOverlappedIoCompleted(&cov));
        oracle_case_end();
        HANDLE cli = CreateFileW(PIPE("p"), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                                 FILE_FLAG_OVERLAPPED, NULL);
        oracle_case("pipe_connect_completes_on_client_open");
        result_fields(srv, &cov);
        o_bool("event_signalled", signalled(cov.hEvent));
        oracle_case_end();
        CloseHandle(cov.hEvent);

        SetEvent(ev);
        memset(&ov, 0, sizeof ov);
        ov.hEvent = ev;
        ORACLE_CALL(ok, ReadFile(srv, buf, 8, NULL, &ov));
        oracle_case("pipe_read_pending");
        o_bool("ret", ok);
        o_lasterr();
        o_bool("event_reset", !signalled(ev));
        o_bool("has_completed", HasOverlappedIoCompleted(&ov));
        o_hex32("internal", (DWORD)ov.Internal);
        oracle_case_end();
        n = 0xAAAA;
        ORACLE_CALL(ok, GetOverlappedResult(srv, &ov, &n, FALSE));
        oracle_case("pipe_result_incomplete");
        o_bool("ret", ok);
        o_lasterr();
        o_u32("bytes", n);
        oracle_case_end();
        n = 0xAAAA;
        ORACLE_CALL(ok, GetOverlappedResultEx(srv, &ov, &n, 0, FALSE));
        oracle_case("pipe_result_ex_timeout_0");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        n = 0xAAAA;
        ORACLE_CALL(ok, GetOverlappedResultEx(srv, &ov, &n, 1, FALSE));
        oracle_case("pipe_result_ex_timeout_1");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        DWORD w;
        WriteFile(cli, "data", 4, &w, NULL);
        {   /* A synchronous WriteFile on an overlapped handle without an OVERLAPPED. */
        }
        oracle_case("pipe_read_completes_after_write");
        result_fields(srv, &ov);
        o_bytes("data", buf, 4);
        o_bool("event_signalled", signalled(ev));
        oracle_case_end();

        memset(&ov, 0, sizeof ov);
        ov.hEvent = ev;
        ReadFile(srv, buf, 8, NULL, &ov);
        BOOL_CASE("pipe_cancel_io_ex", CancelIoEx(srv, &ov));
        oracle_case("pipe_result_after_cancel");
        result_fields(srv, &ov);
        oracle_case_end();
        BOOL_CASE("pipe_cancel_io_ex_again", CancelIoEx(srv, &ov));

        memset(&ov, 0, sizeof ov);
        ov.hEvent = ev;
        ReadFile(srv, buf, 8, NULL, &ov);
        BOOL_CASE("pipe_cancel_io", CancelIo(srv));
        oracle_case("pipe_result_after_cancel_io");
        result_fields(srv, &ov);
        oracle_case_end();

        /* Pending read, then the client closes. */
        memset(&ov, 0, sizeof ov);
        ov.hEvent = ev;
        ReadFile(srv, buf, 8, NULL, &ov);
        CloseHandle(cli);
        oracle_case("pipe_result_after_client_close");
        result_fields(srv, &ov);
        oracle_case_end();
        CloseHandle(srv);
    }

    /* ---- I/O completion ports ---- */
    {
        HANDLE port;
        ORACLE_CALL(port, CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1));
        oracle_case("iocp_create");
        o_handle("ret", port);
        o_lasterr();
        oracle_case_end();

        DWORD bytes = 0xAAAA;
        ULONG_PTR key = 0xAAAA;
        OVERLAPPED *pov = (OVERLAPPED *)1;
        ORACLE_CALL(ok, GetQueuedCompletionStatus(port, &bytes, &key, &pov, 0));
        oracle_case("iocp_get_empty");
        o_bool("ret", ok);
        o_lasterr();
        o_bool("ov_null", pov == NULL);
        oracle_case_end();

        BOOL_CASE("iocp_post", PostQueuedCompletionStatus(port, 77, 0x1234, (OVERLAPPED *)&ov));
        BOOL_CASE("iocp_post_null_overlapped", PostQueuedCompletionStatus(port, 5, 0x55, NULL));
        ORACLE_CALL(ok, GetQueuedCompletionStatus(port, &bytes, &key, &pov, 0));
        oracle_case("iocp_get_posted");
        o_bool("ret", ok);
        o_lasterr();
        o_u32("bytes", bytes);
        o_hex32("key", (DWORD)key);
        o_bool("ov_matches", pov == &ov);
        oracle_case_end();
        ORACLE_CALL(ok, GetQueuedCompletionStatus(port, &bytes, &key, &pov, 0));
        oracle_case("iocp_get_posted_null_overlapped");
        o_bool("ret", ok);
        o_lasterr();
        o_u32("bytes", bytes);
        o_hex32("key", (DWORD)key);
        o_bool("ov_null", pov == NULL);
        oracle_case_end();

        /* A file read completes to the port with the file's key. */
        f = open_ovl(L"a.bin", OPEN_EXISTING);
        HANDLE p2;
        ORACLE_CALL(p2, CreateIoCompletionPort(f, port, 0xF11E, 0));
        oracle_case("iocp_associate_file");
        o_bool("same_port", p2 == port);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(p2, CreateIoCompletionPort(f, port, 0xF11F, 0));
        oracle_case("iocp_associate_file_twice");
        o_handle("ret", p2);
        o_lasterr();
        oracle_case_end();
        memset(&ov, 0, sizeof ov);
        ORACLE_CALL(ok, ReadFile(f, buf, 5, NULL, &ov));
        oracle_case("iocp_file_read");
        start_fields(ok, oracle_last_error);
        oracle_case_end();
        ORACLE_CALL(ok, GetQueuedCompletionStatus(port, &bytes, &key, &pov, INFINITE));
        oracle_case("iocp_file_read_packet");
        o_bool("ret", ok);
        o_lasterr();
        o_u32("bytes", bytes);
        o_hex32("key", (DWORD)key);
        o_bool("ov_matches", pov == &ov);
        oracle_case_end();

        /* A failing read (past EOF) also queues a packet. */
        memset(&ov, 0, sizeof ov);
        ov.Offset = 9000;
        ORACLE_CALL(ok, ReadFile(f, buf, 5, NULL, &ov));
        oracle_case("iocp_file_read_eof");
        start_fields(ok, oracle_last_error);
        oracle_case_end();
        pov = NULL;
        ORACLE_CALL(ok, GetQueuedCompletionStatus(port, &bytes, &key, &pov, 0));
        oracle_case("iocp_file_read_eof_packet");
        o_bool("ret", ok);
        o_lasterr();
        o_bool("ov_matches", pov == &ov);
        oracle_case_end();

        /* Low bit of hEvent set: no completion packet. */
        HANDLE e2 = CreateEventW(NULL, TRUE, FALSE, NULL);
        memset(&ov, 0, sizeof ov);
        ov.hEvent = (HANDLE)((ULONG_PTR)e2 | 1);
        ORACLE_CALL(ok, ReadFile(f, buf, 5, NULL, &ov));
        GetOverlappedResult(f, &ov, &n, TRUE);
        pov = NULL;
        ORACLE_CALL(ok, GetQueuedCompletionStatus(port, &bytes, &key, &pov, 0));
        oracle_case("iocp_event_low_bit_suppresses_packet");
        o_bool("ret", ok);
        o_lasterr();
        o_bool("ov_null", pov == NULL);
        oracle_case_end();
        CloseHandle(e2);

        {
            OVERLAPPED_ENTRY ents[4];
            ULONG got = 99;
            PostQueuedCompletionStatus(port, 1, 1, NULL);
            PostQueuedCompletionStatus(port, 2, 2, NULL);
            ORACLE_CALL(ok, GetQueuedCompletionStatusEx(port, ents, 4, &got, 0, FALSE));
            oracle_case("iocp_get_ex_two");
            o_bool("ret", ok);
            o_lasterr();
            o_u32("count", got);
            if (ok && got == 2) {
                o_u32("bytes0", ents[0].dwNumberOfBytesTransferred);
                o_u32("bytes1", ents[1].dwNumberOfBytesTransferred);
                o_hex32("key0", (DWORD)ents[0].lpCompletionKey);
            }
            oracle_case_end();
            got = 99;
            ORACLE_CALL(ok, GetQueuedCompletionStatusEx(port, ents, 4, &got, 0, FALSE));
            oracle_case("iocp_get_ex_empty");
            o_bool("ret", ok);
            o_lasterr();
            o_u32("count", got);
            oracle_case_end();
        }

        BOOL_CASE("skip_on_success_mode", SetFileCompletionNotificationModes(
                                             f, FILE_SKIP_COMPLETION_PORT_ON_SUCCESS | FILE_SKIP_SET_EVENT_ON_HANDLE));
        CloseHandle(f);

        ORACLE_CALL(p2, CreateIoCompletionPort(INVALID_HANDLE_VALUE, port, 0, 0));
        oracle_case("iocp_invalid_file_existing_port");
        o_handle("ret", p2);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(ok, GetQueuedCompletionStatus(ev, &bytes, &key, &pov, 0));
        oracle_case("iocp_get_on_event_handle");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        BOOL_CASE("iocp_post_on_event_handle", PostQueuedCompletionStatus(ev, 1, 1, NULL));
        CloseHandle(port);
    }
    {   /* A synchronous handle can be associated with a port. */
        HANDLE s = CreateFileW(L"a.bin", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        HANDLE p;
        ORACLE_CALL(p, CreateIoCompletionPort(s, NULL, 1, 0));
        oracle_case("iocp_associate_sync_handle");
        o_handle("ret", p);
        o_lasterr();
        oracle_case_end();
        if (p) CloseHandle(p);
        CloseHandle(s);
    }

    CloseHandle(ev);
    return oracle_end();
}
