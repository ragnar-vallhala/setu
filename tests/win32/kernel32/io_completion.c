/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: I/O completion ports beyond the basics in overlapped_io:
 * packet FIFO order and values, GetQueuedCompletionStatusEx batch sizes and
 * alertable waits, error codes for wrong handles, re-association of a file
 * with a second port, FILE_SKIP_COMPLETION_PORT_ON_SUCCESS, pipe I/O packets,
 * and several threads draining one port.
 *
 * Packets carry made-up keys and OVERLAPPED values, which are compared but
 * never printed as pointers. Threads are always joined before printing.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/fileio/i-o-completion-ports
 *       https://learn.microsoft.com/en-us/windows/win32/fileio/createiocompletionport
 *       https://learn.microsoft.com/en-us/windows/win32/fileio/postqueuedcompletionstatus
 *       https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-getqueuedcompletionstatus
 *       https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-getqueuedcompletionstatusex
 *       https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-setfilecompletionnotificationmodes
 */
#include "oracle.h"

#define BOOL_CASE(name, expr)     \
    do {                          \
        BOOL ok_;                 \
        ORACLE_CALL(ok_, (expr)); \
        oracle_case(name);        \
        o_bool("ret", ok_);       \
        o_lasterr();              \
        oracle_case_end();        \
    } while (0)

static void get_case(const char *name, HANDLE port, DWORD timeout)
{
    DWORD bytes = 0x5555;
    ULONG_PTR key = 0x5555;
    OVERLAPPED *ov = (OVERLAPPED *)0x5555;
    BOOL ok;
    ORACLE_CALL(ok, GetQueuedCompletionStatus(port, &bytes, &key, &ov, timeout));
    oracle_case(name);
    o_bool("ret", ok);
    o_lasterr();
    o_hex32("bytes", bytes);
    o_hex32("key", (DWORD)key);
    o_hex32("ov", (DWORD)(ULONG_PTR)ov);
    oracle_case_end();
}

static void CALLBACK apc(ULONG_PTR p) { (void)p; }

static HANDLE shared_port;
static volatile LONG got_total;
static DWORD WINAPI drain_one(void *arg)
{
    (void)arg;
    DWORD bytes;
    ULONG_PTR key;
    OVERLAPPED *ov;
    if (GetQueuedCompletionStatus(shared_port, &bytes, &key, &ov, 10000)) {
        InterlockedAdd(&got_total, (LONG)key);
        return 1;
    }
    return 0;
}

int main(void)
{
    oracle_begin("kernel32/io_completion");
    HANDLE port, p2;
    BOOL ok;

    ORACLE_CALL(port, CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0x99, 0));
    oracle_case("create_port");
    o_handle("ret", port);
    o_lasterr();
    oracle_case_end();

    get_case("get_empty_timeout_0", port, 0);
    get_case("get_empty_timeout_10", port, 10);

    /* ---- FIFO order and exact values ---- */
    BOOL_CASE("post_1", PostQueuedCompletionStatus(port, 1, 0x11, (OVERLAPPED *)0x1000));
    BOOL_CASE("post_2", PostQueuedCompletionStatus(port, 2, 0x22, NULL));
    BOOL_CASE("post_max_bytes", PostQueuedCompletionStatus(port, 0xffffffff, 0x33, (OVERLAPPED *)0x3000));
    get_case("get_first", port, 0);
    get_case("get_second_null_overlapped", port, 0);
    get_case("get_third_max_bytes", port, 0);
    get_case("get_drained", port, 0);

    {
        DWORD r;
        ORACLE_CALL(r, WaitForSingleObject(port, 0));
        oracle_case("wait_on_port_empty");
        o_hex32("ret", r);
        o_lasterr();
        oracle_case_end();
        PostQueuedCompletionStatus(port, 0, 1, NULL);
        ORACLE_CALL(r, WaitForSingleObject(port, 0));
        oracle_case("wait_on_port_with_packet");
        o_hex32("ret", r);
        o_lasterr();
        oracle_case_end();
        get_case("get_after_wait_on_port", port, 0);
    }

    /* ---- GetQueuedCompletionStatusEx ---- */
    {
        OVERLAPPED_ENTRY e[8];
        ULONG n = 0x55;
        for (int i = 0; i < 5; i++) PostQueuedCompletionStatus(port, 100 + i, 0x40 + i, (OVERLAPPED *)(ULONG_PTR)(0x4000 + i));
        memset(e, 0xcc, sizeof e);
        ORACLE_CALL(ok, GetQueuedCompletionStatusEx(port, e, 3, &n, 0, FALSE));
        oracle_case("get_ex_three_of_five");
        o_bool("ret", ok);
        o_lasterr();
        o_u32("removed", n);
        char buf[128] = "";
        for (ULONG i = 0; i < n && i < 8; i++) {
            char t[40];
            snprintf(t, sizeof t, "%s%lu/%lx/%lx", i ? "," : "", e[i].dwNumberOfBytesTransferred,
                     (unsigned long)e[i].lpCompletionKey, (unsigned long)(ULONG_PTR)e[i].lpOverlapped);
            strcat(buf, t);
        }
        o_str("entries", buf);
        o_hex32("internal0", (DWORD)e[0].Internal);
        oracle_case_end();
        n = 0x55;
        ORACLE_CALL(ok, GetQueuedCompletionStatusEx(port, e, 8, &n, 0, FALSE));
        oracle_case("get_ex_remaining_two");
        o_bool("ret", ok);
        o_lasterr();
        o_u32("removed", n);
        oracle_case_end();
        n = 0x55;
        ORACLE_CALL(ok, GetQueuedCompletionStatusEx(port, e, 8, &n, 0, FALSE));
        oracle_case("get_ex_empty");
        o_bool("ret", ok);
        o_lasterr();
        o_u32("removed", n);
        oracle_case_end();
        n = 0x55;
        ORACLE_CALL(ok, GetQueuedCompletionStatusEx(port, e, 0, &n, 0, FALSE));
        oracle_case("get_ex_count_zero");
        o_bool("ret", ok);
        o_lasterr();
        o_u32("removed", n);
        oracle_case_end();

        QueueUserAPC(apc, GetCurrentThread(), 0);
        n = 0x55;
        ORACLE_CALL(ok, GetQueuedCompletionStatusEx(port, e, 8, &n, 0, FALSE));
        oracle_case("get_ex_not_alertable_apc_queued");
        o_bool("ret", ok);
        o_lasterr();
        o_u32("removed", n);
        oracle_case_end();
        n = 0x55;
        ORACLE_CALL(ok, GetQueuedCompletionStatusEx(port, e, 8, &n, 1000, TRUE));
        oracle_case("get_ex_alertable_apc_queued");
        o_bool("ret", ok);
        o_lasterr();
        o_u32("removed", n);
        oracle_case_end();
        PostQueuedCompletionStatus(port, 7, 7, NULL);
        QueueUserAPC(apc, GetCurrentThread(), 0);
        n = 0x55;
        ORACLE_CALL(ok, GetQueuedCompletionStatusEx(port, e, 8, &n, 1000, TRUE));
        oracle_case("get_ex_alertable_packet_and_apc");
        o_bool("ret", ok);
        o_lasterr();
        o_u32("removed", n);
        oracle_case_end();
        SleepEx(0, TRUE);
        GetQueuedCompletionStatusEx(port, e, 8, &n, 0, FALSE);

        /* Probe: does NtRemoveIoCompletionEx itself write 0 entries on a
         * timeout or a user APC, or only GetQueuedCompletionStatusEx? */
        typedef LONG(WINAPI * remove_ex_fn)(HANDLE, void *, ULONG, ULONG *, LARGE_INTEGER *, BOOLEAN);
        remove_ex_fn nt_remove_ex =
            (remove_ex_fn)(void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtRemoveIoCompletionEx");
        if (nt_remove_ex) {
            LARGE_INTEGER zero = {{0, 0}}, second;
            LONG st;
            second.QuadPart = -10000000;
            n = 0x55;
            st = nt_remove_ex(port, e, 8, &n, &zero, FALSE);
            oracle_case("probe_nt_remove_ex_timeout");
            o_hex32("status", (DWORD)st);
            o_u32("removed", n);
            oracle_case_end();
            n = 0x55;
            st = nt_remove_ex(port, e, 0, &n, &zero, FALSE);
            oracle_case("probe_nt_remove_ex_count_zero");
            o_hex32("status", (DWORD)st);
            o_u32("removed", n);
            oracle_case_end();
            QueueUserAPC(apc, GetCurrentThread(), 0);
            n = 0x55;
            st = nt_remove_ex(port, e, 8, &n, &second, TRUE);
            oracle_case("probe_nt_remove_ex_user_apc");
            o_hex32("status", (DWORD)st);
            o_u32("removed", n);
            oracle_case_end();
            n = 0x55;
            ORACLE_CALL(ok, GetQueuedCompletionStatusEx(port, e, 8, &n, 1, FALSE));
            oracle_case("probe_get_ex_timeout_1ms");
            o_bool("ret", ok);
            o_lasterr();
            o_u32("removed", n);
            oracle_case_end();
        }
    }

    /* ---- wrong handles ---- */
    {
        HANDLE ev = CreateEventW(NULL, TRUE, FALSE, NULL);
        BOOL_CASE("post_to_event", PostQueuedCompletionStatus(ev, 0, 0, NULL));
        get_case("get_from_event", ev, 0);
        BOOL_CASE("post_to_null", PostQueuedCompletionStatus(NULL, 0, 0, NULL));
        ORACLE_CALL(p2, CreateIoCompletionPort(INVALID_HANDLE_VALUE, ev, 0, 0));
        oracle_case("create_with_event_as_port");
        o_handle("ret", p2);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(p2, CreateIoCompletionPort(ev, NULL, 0, 0));
        oracle_case("associate_event");
        o_handle("ret", p2);
        o_lasterr();
        oracle_case_end();
        if (p2) CloseHandle(p2);
        CloseHandle(ev);
    }

    /* ---- file association ---- */
    {
        HANDLE f = CreateFileW(L"io.bin", GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                               FILE_FLAG_OVERLAPPED | FILE_FLAG_DELETE_ON_CLOSE, NULL);
        OVERLAPPED ov = {0};
        DWORD n;
        ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
        if (!WriteFile(f, "0123456789", 10, &n, &ov)) WaitForSingleObject(ov.hEvent, 5000);
        CloseHandle(ov.hEvent);
        ORACLE_CALL(p2, CreateIoCompletionPort(f, port, 0xf1, 0));
        oracle_case("associate_file");
        o_bool("same_port", p2 == port);
        o_lasterr();
        oracle_case_end();
        HANDLE other = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
        ORACLE_CALL(p2, CreateIoCompletionPort(f, other, 0xf2, 0));
        oracle_case("associate_file_second_port");
        o_handle("ret", p2);
        o_lasterr();
        oracle_case_end();

        char buf[8];
        HANDLE ovev = CreateEventW(NULL, TRUE, FALSE, NULL);
        memset(&ov, 0, sizeof ov);
        ov.hEvent = ovev;
        ok = ReadFile(f, buf, 4, NULL, &ov);
        DWORD err = GetLastError();
        DWORD wr = WaitForSingleObject(ovev, 5000);
        oracle_case("read_with_port");
        o_bool("ret_or_pending", ok || err == ERROR_IO_PENDING);
        o_bool("completed", wr == WAIT_OBJECT_0);
        oracle_case_end();
        {
            DWORD bytes = 0;
            ULONG_PTR key = 0;
            OVERLAPPED *pov = NULL;
            ok = GetQueuedCompletionStatus(port, &bytes, &key, &pov, 1000);
            oracle_case("read_packet");
            o_bool("ret", ok);
            o_u32("bytes", bytes);
            o_hex32("key", (DWORD)key);
            o_bool("ov_matches", pov == &ov);
            oracle_case_end();
        }
        get_case("other_port_empty", other, 0);

        typedef BOOL(WINAPI * sfcnm_fn)(HANDLE, UCHAR);
        sfcnm_fn sfcnm = (sfcnm_fn)(void *)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetFileCompletionNotificationModes");
        BOOL_CASE("skip_on_success", sfcnm(f, FILE_SKIP_COMPLETION_PORT_ON_SUCCESS));
        BOOL_CASE("skip_bad_flag", sfcnm(f, 0x80));
        memset(&ov, 0, sizeof ov);
        ResetEvent(ovev);
        ov.hEvent = ovev;
        ok = ReadFile(f, buf, 4, &n, &ov);
        err = GetLastError();
        if (!ok && err == ERROR_IO_PENDING) WaitForSingleObject(ovev, 5000);
        {
            DWORD bytes = 0x5555;
            ULONG_PTR key = 0;
            OVERLAPPED *pov = NULL;
            /* A packet is queued only if the read went asynchronous. */
            BOOL got = GetQueuedCompletionStatus(port, &bytes, &key, &pov, 200);
            oracle_case("skip_on_success_packet");
            o_bool("read_sync", ok);
            o_bool("packet_iff_async", got == !ok);
            oracle_case_end();
        }
        CloseHandle(other);
        CloseHandle(ovev);
        CloseHandle(f);
    }

    /* ---- pipe packets ---- */
    {
        HANDLE srv = CreateNamedPipeW(L"\\\\.\\pipe\\setu_oracle_iocp", PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                                      PIPE_TYPE_BYTE, 1, 512, 512, 0, NULL);
        HANDLE cli = CreateFileW(L"\\\\.\\pipe\\setu_oracle_iocp", GENERIC_READ | GENERIC_WRITE, 0, NULL,
                                 OPEN_EXISTING, 0, NULL);
        CreateIoCompletionPort(srv, port, 0x77, 0);
        char buf[16];
        OVERLAPPED ov = {0};
        ok = ReadFile(srv, buf, sizeof buf, NULL, &ov);
        DWORD err = GetLastError();
        get_case("pipe_no_packet_before_write", port, 0);
        DWORD n;
        WriteFile(cli, "hello", 5, &n, NULL);
        DWORD bytes = 0;
        ULONG_PTR key = 0;
        OVERLAPPED *pov = NULL;
        BOOL got = GetQueuedCompletionStatus(port, &bytes, &key, &pov, 2000);
        oracle_case("pipe_read_packet");
        o_bool("read_pending", !ok && err == ERROR_IO_PENDING);
        o_bool("ret", got);
        o_u32("bytes", bytes);
        o_hex32("key", (DWORD)key);
        o_bool("ov_matches", pov == &ov);
        oracle_case_end();

        memset(&ov, 0, sizeof ov);
        ReadFile(srv, buf, sizeof buf, NULL, &ov);
        CloseHandle(cli);
        got = GetQueuedCompletionStatus(port, &bytes, &key, &pov, 2000);
        DWORD gerr = GetLastError();
        oracle_case("pipe_broken_packet");
        o_bool("ret", got);
        o_u32("error", got ? 0 : gerr);
        o_u32("bytes", bytes);
        o_bool("ov_matches", pov == &ov);
        oracle_case_end();
        CloseHandle(srv);
    }

    /* ---- three threads drain three packets ---- */
    {
        HANDLE th[3];
        shared_port = port;
        for (int i = 0; i < 3; i++) th[i] = CreateThread(NULL, 0, drain_one, NULL, 0, NULL);
        PostQueuedCompletionStatus(port, 0, 1, NULL);
        PostQueuedCompletionStatus(port, 0, 10, NULL);
        PostQueuedCompletionStatus(port, 0, 100, NULL);
        WaitForMultipleObjects(3, th, TRUE, 20000);
        int all = 1;
        for (int i = 0; i < 3; i++) {
            DWORD c = 0;
            GetExitCodeThread(th[i], &c);
            all &= c == 1;
            CloseHandle(th[i]);
        }
        oracle_case("three_threads_three_packets");
        o_bool("all_got_one", all);
        o_i64("key_sum", got_total);
        oracle_case_end();
        get_case("empty_after_threads", port, 0);
    }

    BOOL_CASE("close_port", CloseHandle(port));
    get_case("get_on_closed_port", port, 0);

    return oracle_end();
}
