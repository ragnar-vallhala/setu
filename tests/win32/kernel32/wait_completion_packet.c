/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: ntdll's wait completion packets (NtCreateWaitCompletionPacket,
 * NtAssociateWaitCompletionPacket, NtCancelWaitCompletionPacket), which the
 * Windows App SDK's CoreMessagingXP.dll (shipped with Firefox) imports:
 * when a packet reaches the I/O completion port and with which values,
 * AlreadySignaled, whether the wait consumes an auto-reset event, a second
 * association while one is active, and the status of a cancel in each state.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/devnotes/ntcreatewaitcompletionpacket
 *       https://learn.microsoft.com/en-us/windows/win32/devnotes/ntassociatewaitcompletionpacket
 *       https://learn.microsoft.com/en-us/windows/win32/devnotes/ntcancelwaitcompletionpacket
 *       https://learn.microsoft.com/en-us/windows/win32/devnotes/ntremoveiocompletion
 */
#include "oracle.h"
#include <winternl.h>

typedef LONG (WINAPI *create_fn)(HANDLE *, ACCESS_MASK, void *);
typedef LONG (WINAPI *assoc_fn)(HANDLE, HANDLE, HANDLE, void *, void *, LONG, ULONG_PTR, BOOLEAN *);
typedef LONG (WINAPI *cancel_fn)(HANDLE, BOOLEAN);
typedef LONG (WINAPI *remove_fn)(HANDLE, ULONG_PTR *, ULONG_PTR *, IO_STATUS_BLOCK *, LARGE_INTEGER *);

static create_fn pCreate;
static assoc_fn pAssoc;
static cancel_fn pCancel;
static remove_fn pRemove;
static HANDLE port;

/* dequeue one packet with a timeout in ms; print what came out */
static void o_dequeue(const char *prefix, DWORD ms)
{
    ULONG_PTR key = 0, apc = 0;
    IO_STATUS_BLOCK io;
    LARGE_INTEGER t;
    LONG st;
    char k[64];

    t.QuadPart = -(LONGLONG)ms * 10000;
    io.Status = 0x7777; io.Information = 0x7777;
    st = pRemove(port, &key, &apc, &io, &t);
    wsprintfA(k, "%s_got", prefix);
    o_bool(k, st == 0);
    if (st == 0) {
        wsprintfA(k, "%s_key", prefix); o_hex32(k, (DWORD)key);
        wsprintfA(k, "%s_apc", prefix); o_hex32(k, (DWORD)apc);
        wsprintfA(k, "%s_status", prefix); o_status(k, io.Status);
        wsprintfA(k, "%s_info", prefix); o_hex32(k, (DWORD)io.Information);
    } else {
        wsprintfA(k, "%s_ret", prefix); o_status(k, st);
    }
}

int main(void)
{
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    HANDLE pkt = NULL, ev, auto_ev;
    BOOLEAN sig;
    LONG st;

    oracle_begin("kernel32/wait_completion_packet");

    pCreate = (create_fn)(void (*)(void))GetProcAddress(ntdll, "NtCreateWaitCompletionPacket");
    pAssoc = (assoc_fn)(void (*)(void))GetProcAddress(ntdll, "NtAssociateWaitCompletionPacket");
    pCancel = (cancel_fn)(void (*)(void))GetProcAddress(ntdll, "NtCancelWaitCompletionPacket");
    pRemove = (remove_fn)(void (*)(void))GetProcAddress(ntdll, "NtRemoveIoCompletion");
    oracle_case("exports");
    o_bool("create", pCreate != NULL);
    o_bool("associate", pAssoc != NULL);
    o_bool("cancel", pCancel != NULL);
    oracle_case_end();
    if (!pCreate || !pAssoc || !pCancel || !pRemove) return oracle_end();

    port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    ev = CreateEventW(NULL, TRUE, FALSE, NULL);
    auto_ev = CreateEventW(NULL, FALSE, FALSE, NULL);

    st = pCreate(&pkt, GENERIC_ALL, NULL);
    oracle_case("create");
    o_status("ret", st);
    o_bool("handle", pkt != NULL && pkt != INVALID_HANDLE_VALUE);
    oracle_case_end();

    st = pCancel(pkt, FALSE);
    oracle_case("cancel_never_associated");
    o_status("ret", st);
    oracle_case_end();

    sig = 0x55;
    st = pAssoc(pkt, port, ev, (void *)0x1111, (void *)0x2222, 0x103, 0x3333, &sig);
    oracle_case("associate_unsignaled");
    o_status("ret", st);
    o_u32("already_signaled", sig);
    o_dequeue("before_signal", 0);
    SetEvent(ev);
    o_dequeue("after_signal", 1000);
    o_dequeue("again", 100);
    oracle_case_end();

    st = pCancel(pkt, FALSE);
    oracle_case("cancel_after_pickup");
    o_status("ret", st);
    oracle_case_end();

    /* the target is still signaled (manual reset) */
    sig = 0x55;
    st = pAssoc(pkt, port, ev, (void *)0x4444, NULL, 0, 0, &sig);
    oracle_case("associate_signaled");
    o_status("ret", st);
    o_u32("already_signaled", sig);
    o_dequeue("queued", 0);
    oracle_case_end();
    ResetEvent(ev);

    sig = 0x55;
    st = pAssoc(pkt, port, auto_ev, (void *)0x5555, NULL, 0, 0, &sig);
    oracle_case("auto_reset_event");
    o_status("ret", st);
    o_u32("already_signaled", sig);
    SetEvent(auto_ev);
    o_dequeue("packet", 1000);
    o_bool("event_still_set", WaitForSingleObject(auto_ev, 0) == WAIT_OBJECT_0);
    oracle_case_end();

    pAssoc(pkt, port, ev, (void *)0x6666, NULL, 0, 0, NULL);
    st = pAssoc(pkt, port, ev, (void *)0x7777, NULL, 0, 0, NULL);
    oracle_case("associate_while_active");
    o_status("ret", st);
    oracle_case_end();

    st = pCancel(pkt, FALSE);
    oracle_case("cancel_waiting");
    o_status("ret", st);
    SetEvent(ev);
    o_dequeue("after_signal", 200);
    oracle_case_end();
    ResetEvent(ev);

    st = pCancel(pkt, FALSE);
    oracle_case("cancel_twice");
    o_status("ret", st);
    oracle_case_end();

    /* signaled and queued, not yet picked up */
    pAssoc(pkt, port, ev, (void *)0x8888, NULL, 0, 0, NULL);
    SetEvent(ev);
    Sleep(200);
    ResetEvent(ev);
    st = pCancel(pkt, FALSE);
    oracle_case("cancel_queued_keep");
    o_status("ret", st);
    o_dequeue("packet", 0);
    oracle_case_end();

    st = pAssoc(pkt, port, ev, (void *)0x9999, NULL, 0, 0, NULL);
    oracle_case("reassociate_after_cancel");
    o_status("ret", st);
    oracle_case_end();
    pCancel(pkt, FALSE);

    st = pAssoc(ev, port, auto_ev, NULL, NULL, 0, 0, NULL);
    oracle_case("associate_wrong_handle_type");
    o_status("ret", st);
    oracle_case_end();

    st = pCancel(ev, FALSE);
    oracle_case("cancel_wrong_handle_type");
    o_status("ret", st);
    oracle_case_end();

    CloseHandle(pkt);
    CloseHandle(ev);
    CloseHandle(auto_ev);
    CloseHandle(port);
    return oracle_end();
}
