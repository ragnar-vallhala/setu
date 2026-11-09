/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: ALPC ports between two threads of one process, as the Windows
 * App SDK's CoreMessagingXP.dll (shipped with Firefox) uses them:
 * NtAlpcCreatePort (named and unnamed, name collisions, the object type),
 * NtAlpcConnectPort (missing port, timeout, accept and refuse),
 * NtAlpcAcceptConnectPort, NtAlpcSendWaitReceivePort (connection requests,
 * synchronous requests and their replies, datagrams in both directions,
 * a short receive buffer, an empty queue), the context message attribute,
 * NtAlpcQueryInformation (basic information), NtAlpcImpersonateClientOfPort
 * and NtAlpcDisconnectPort / closing a port (what the other side receives).
 *
 * The thread that owns the connection port is the main thread; a client
 * thread connects and sends. Every blocking call has a timeout so that a
 * missing implementation cannot hang the run. Message ids, client ids and
 * sequence numbers are printed as comparisons, never as values.
 *
 * Neither MinGW-w64 nor Wine declare the ALPC system calls. The prototypes and
 * structure layouts follow the public, MIT-licensed phnt headers
 * (ntlpcapi.h); everything printed here is black-box behaviour.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/etw/alpc
 *       https://learn.microsoft.com/en-us/windows/win32/api/winternl/ns-winternl-object_attributes
 *       https://github.com/winsiderss/phnt/blob/master/ntlpcapi.h
 */
#include "oracle.h"
#include <winternl.h>

typedef struct {
    union { struct { SHORT DataLength, TotalLength; } s1; ULONG Length; } u1;
    union { struct { SHORT Type, DataInfoOffset; } s2; ULONG ZeroInit; } u2;
    union { CLIENT_ID ClientId; double DoNotUseThisField; };
    ULONG MessageId;
    union { SIZE_T ClientViewSize; ULONG CallbackId; };
} PORT_MESSAGE_;

typedef struct {
    ULONG Flags;
    SECURITY_QUALITY_OF_SERVICE SecurityQos;
    SIZE_T MaxMessageLength;
    SIZE_T MemoryBandwidth;
    SIZE_T MaxPoolUsage;
    SIZE_T MaxSectionSize;
    SIZE_T MaxViewSize;
    SIZE_T MaxTotalSectionSize;
    ULONG DupObjectTypes;
#ifdef _WIN64
    ULONG Reserved;
#endif
} ALPC_PORT_ATTRIBUTES_;

typedef struct {
    ULONG AllocatedAttributes;
    ULONG ValidAttributes;
} ALPC_MESSAGE_ATTRIBUTES_;

typedef struct {
    void *PortContext;
    void *MessageContext;
    ULONG Sequence;
    ULONG MessageId;
    ULONG CallbackId;
} ALPC_CONTEXT_ATTR_;

typedef struct {
    ULONG Flags;
    ULONG SequenceNo;
    void *PortContext;
} ALPC_BASIC_INFORMATION_;

#define ATTR_CONTEXT 0x20000000u
#define MSGFLG_REPLY 0x1
#define MSGFLG_RELEASE 0x10000
#define MSGFLG_SYNC 0x20000
#define PORFLG_ALLOW_IMPERSONATION 0x10000
#define PORFLG_WAITABLE 0x40000

typedef LONG (WINAPI *create_fn)(HANDLE *, OBJECT_ATTRIBUTES *, ALPC_PORT_ATTRIBUTES_ *);
typedef LONG (WINAPI *connect_fn)(HANDLE *, UNICODE_STRING *, OBJECT_ATTRIBUTES *, ALPC_PORT_ATTRIBUTES_ *,
                                  ULONG, void *, PORT_MESSAGE_ *, SIZE_T *, ALPC_MESSAGE_ATTRIBUTES_ *,
                                  ALPC_MESSAGE_ATTRIBUTES_ *, LARGE_INTEGER *);
typedef LONG (WINAPI *accept_fn)(HANDLE *, HANDLE, ULONG, OBJECT_ATTRIBUTES *, ALPC_PORT_ATTRIBUTES_ *,
                                 void *, PORT_MESSAGE_ *, ALPC_MESSAGE_ATTRIBUTES_ *, BOOLEAN);
typedef LONG (WINAPI *swr_fn)(HANDLE, ULONG, PORT_MESSAGE_ *, ALPC_MESSAGE_ATTRIBUTES_ *, PORT_MESSAGE_ *,
                              SIZE_T *, ALPC_MESSAGE_ATTRIBUTES_ *, LARGE_INTEGER *);
typedef LONG (WINAPI *disconnect_fn)(HANDLE, ULONG);
typedef LONG (WINAPI *query_fn)(HANDLE, ULONG, void *, ULONG, ULONG *);
typedef LONG (WINAPI *impersonate_fn)(HANDLE, PORT_MESSAGE_ *, void *);
typedef LONG (WINAPI *init_fn)(ULONG, ALPC_MESSAGE_ATTRIBUTES_ *, SIZE_T, SIZE_T *);
typedef void *(WINAPI *get_fn)(ALPC_MESSAGE_ATTRIBUTES_ *, ULONG);
typedef LONG (WINAPI *qobj_fn)(HANDLE, ULONG, void *, ULONG, ULONG *);

static create_fn pCreate;
static connect_fn pConnect;
static accept_fn pAccept;
static swr_fn pSwr;
static disconnect_fn pDisconnect;
static query_fn pQuery;
static impersonate_fn pImpersonate;
static init_fn pInit;
static get_fn pGet;
static qobj_fn pQueryObject;

typedef struct {
    PORT_MESSAGE_ h;
    BYTE data[0x200];
} MSG_;

static const WCHAR port_name[] = L"\\RPC Control\\SetuOracleAlpcPort";

static void init_msg(MSG_ *m, const char *text)
{
    SHORT n = (SHORT)strlen(text);
    memset(m, 0, sizeof(*m));
    m->h.u1.s1.DataLength = n;
    m->h.u1.s1.TotalLength = (SHORT)(sizeof(PORT_MESSAGE_) + n);
    memcpy(m->data, text, n);
}

static LARGE_INTEGER *ms_timeout(LARGE_INTEGER *t, LONG ms)
{
    t->QuadPart = -(LONGLONG)ms * 10000;
    return t;
}

static void init_name(UNICODE_STRING *us, const WCHAR *s)
{
    us->Buffer = (WCHAR *)s;
    us->Length = (USHORT)(wcslen(s) * sizeof(WCHAR));
    us->MaximumLength = us->Length + sizeof(WCHAR);
}

static void init_oa(OBJECT_ATTRIBUTES *oa, UNICODE_STRING *us)
{
    memset(oa, 0, sizeof(*oa));
    oa->Length = sizeof(*oa);
    oa->ObjectName = us;
}

static void init_port_attrs(ALPC_PORT_ATTRIBUTES_ *a, ULONG flags, SIZE_T maxlen)
{
    memset(a, 0, sizeof(*a));
    a->Flags = flags;
    a->SecurityQos.Length = sizeof(a->SecurityQos);
    a->SecurityQos.ImpersonationLevel = SecurityImpersonation;
    a->SecurityQos.ContextTrackingMode = SECURITY_DYNAMIC_TRACKING;
    a->SecurityQos.EffectiveOnly = FALSE;
    a->MaxMessageLength = maxlen;
}

/* a message attribute buffer with the context attribute */
static union { ALPC_MESSAGE_ATTRIBUTES_ a; BYTE b[256]; ULONG_PTR align; } rattr;

static ALPC_MESSAGE_ATTRIBUTES_ *ctx_attrs(void *u)
{
    ALPC_MESSAGE_ATTRIBUTES_ *a = u;
    SIZE_T req;
    memset(u, 0xcc, 256);
    pInit(ATTR_CONTEXT, a, 256, &req);
    return a;
}

/* ---- client thread ------------------------------------------------------ */

enum { OP_CONNECT, OP_REQUEST, OP_DATAGRAM, OP_RECEIVE, OP_DISCONNECT, OP_CLOSE, OP_EXIT };

struct client {
    HANDLE thread, go, done;
    int op;
    /* inputs */
    const char *text;
    LONG timeout_ms;
    ULONG flags;
    /* outputs */
    LONG status;
    HANDLE port;
    MSG_ msg;
    SIZE_T len;
    DWORD tid;
};

static struct client cl;

static DWORD WINAPI client_main(void *arg)
{
    struct client *c = arg;
    UNICODE_STRING us;
    ALPC_PORT_ATTRIBUTES_ pa;
    LARGE_INTEGER t;
    MSG_ send;

    (void)arg;
    c->tid = GetCurrentThreadId();
    for (;;) {
        WaitForSingleObject(c->go, INFINITE);
        c->len = sizeof(c->msg);
        switch (c->op) {
        case OP_CONNECT:
            init_name(&us, port_name);
            init_port_attrs(&pa, 0, 0x200);
            init_msg(&c->msg, c->text);
            c->len = sizeof(c->msg);
            c->port = NULL;
            c->status = pConnect(&c->port, &us, NULL, &pa, c->flags, NULL, &c->msg.h, &c->len,
                                 NULL, NULL, ms_timeout(&t, c->timeout_ms));
            break;
        case OP_REQUEST:
            init_msg(&send, c->text);
            memset(&c->msg, 0xcc, sizeof(c->msg));
            c->status = pSwr(c->port, MSGFLG_SYNC | c->flags, &send.h, NULL, &c->msg.h, &c->len, NULL,
                             ms_timeout(&t, c->timeout_ms));
            break;
        case OP_DATAGRAM:
            init_msg(&send, c->text);
            c->status = pSwr(c->port, c->flags, &send.h, NULL, NULL, NULL, NULL, NULL);
            break;
        case OP_RECEIVE:
            memset(&c->msg, 0xcc, sizeof(c->msg));
            c->status = pSwr(c->port, c->flags, NULL, NULL, &c->msg.h, &c->len, NULL,
                             ms_timeout(&t, c->timeout_ms));
            break;
        case OP_DISCONNECT:
            c->status = pDisconnect(c->port, 0);
            break;
        case OP_CLOSE:
            c->status = CloseHandle(c->port) ? 0 : (LONG)GetLastError();
            c->port = NULL;
            break;
        case OP_EXIT:
            SetEvent(c->done);
            return 0;
        }
        SetEvent(c->done);
    }
}

/* start OP on the client thread; doesn't wait */
static void client_start(int op, const char *text, ULONG flags, LONG timeout_ms)
{
    ResetEvent(cl.done);
    cl.op = op;
    cl.text = text;
    cl.flags = flags;
    cl.timeout_ms = timeout_ms;
    SetEvent(cl.go);
}

static int client_wait(DWORD ms)
{
    return WaitForSingleObject(cl.done, ms) == WAIT_OBJECT_0;
}

/* ---- printing ----------------------------------------------------------- */

static ULONG last_msgid, last_cb;

static int printable(const BYTE *p, int n)
{
    int i;
    for (i = 0; i < n; i++) if (p[i] < 0x20 || p[i] > 0x7e) return 0;
    return 1;
}

static void o_header(const char *prefix, const MSG_ *m, SIZE_T len, DWORD tid)
{
    char k[64], text[0x201];
    int n = m->h.u1.s1.DataLength;

#define KEY(s) (wsprintfA(k, "%s_%s", prefix, s), k)
    o_u64(KEY("buflen"), len);
    o_u32(KEY("data_length"), (USHORT)m->h.u1.s1.DataLength);
    o_u32(KEY("total_length"), (USHORT)m->h.u1.s1.TotalLength);
    o_hex32(KEY("type"), (USHORT)m->h.u2.s2.Type);
    o_u32(KEY("data_info_offset"), (USHORT)m->h.u2.s2.DataInfoOffset);
    o_bool(KEY("pid_is_self"), (DWORD)(ULONG_PTR)m->h.ClientId.UniqueProcess == GetCurrentProcessId());
    if ((DWORD)(ULONG_PTR)m->h.ClientId.UniqueProcess != GetCurrentProcessId())
        o_bool(KEY("pid_zero"), m->h.ClientId.UniqueProcess == 0);
    if (tid) o_bool(KEY("tid_is_sender"), (DWORD)(ULONG_PTR)m->h.ClientId.UniqueThread == tid);
    else o_bool(KEY("tid_zero"), m->h.ClientId.UniqueThread == 0);
    o_bool(KEY("msgid_nonzero"), m->h.MessageId != 0);
    if (last_msgid) o_bool(KEY("msgid_same_as_last"), m->h.MessageId == last_msgid);
    o_bool(KEY("view_size_or_callback_zero"), m->h.ClientViewSize == 0);
    if (last_cb) o_bool(KEY("callback_same_as_last"), m->h.CallbackId == last_cb);
    if (n > 0 && n <= 0x200 && !printable(m->data, n)) o_str(KEY("data"), "<binary>");
    else if (n > 0 && n <= 0x200) {
        memcpy(text, m->data, n);
        text[n] = 0;
        o_str(KEY("data"), text);
    }
#undef KEY
}

static void o_ctx(const char *prefix, ALPC_MESSAGE_ATTRIBUTES_ *a, const MSG_ *m)
{
    ALPC_CONTEXT_ATTR_ *c = pGet(a, ATTR_CONTEXT);
    char k[64];

#define KEY(s) (wsprintfA(k, "%s_%s", prefix, s), k)
    o_hex32(KEY("valid"), a->ValidAttributes);
    if (c && (a->ValidAttributes & ATTR_CONTEXT)) {
        o_hex32(KEY("port_context"), (DWORD)(ULONG_PTR)c->PortContext);
        o_bool(KEY("message_context_null"), c->MessageContext == NULL);
        o_bool(KEY("msgid_matches"), c->MessageId == m->h.MessageId);
        o_bool(KEY("callback_id_matches"), c->CallbackId == m->h.CallbackId);
        o_bool(KEY("callback_id_zero"), c->CallbackId == 0);
        o_bool(KEY("sequence_nonzero"), c->Sequence != 0);
    }
#undef KEY
}

/* receive one message on PORT with a context attribute buffer */
static LONG server_receive(HANDLE port, MSG_ *m, SIZE_T *len, LONG ms)
{
    LARGE_INTEGER t;
    last_msgid = last_cb = 0;
    *len = sizeof(*m);
    memset(m, 0xcc, sizeof(*m));
    ctx_attrs(&rattr);
    return pSwr(port, 0, NULL, NULL, &m->h, len, &rattr.a, ms_timeout(&t, ms));
}

static void o_type_name(const char *k, HANDLE h)
{
    union { BYTE b[1024]; ULONG_PTR align; } info;
    ULONG ret = 0;
    UNICODE_STRING *name = (UNICODE_STRING *)info.b;
    WCHAR s[128];
    LONG st = pQueryObject(h, 2 /* ObjectTypeInformation */, info.b, sizeof(info), &ret);
    if (st) { o_status(k, st); return; }
    memcpy(s, name->Buffer, min(name->Length, sizeof(s) - 2));
    s[min(name->Length, sizeof(s) - 2) / 2] = 0;
    o_wstr(k, s);
}

int main(void)
{
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    UNICODE_STRING us, us_missing;
    OBJECT_ATTRIBUTES oa, oa_missing;
    ALPC_PORT_ATTRIBUTES_ pa;
    ALPC_BASIC_INFORMATION_ bi;
    HANDLE conn = NULL, conn2 = NULL, unnamed = NULL, waitable = NULL, comm = NULL, h;
    MSG_ m, reply;
    SIZE_T len;
    ULONG ret;
    LONG st;
    LARGE_INTEGER t;

    oracle_begin("ntdll/alpc_port");

    pCreate = (create_fn)(void (*)(void))GetProcAddress(ntdll, "NtAlpcCreatePort");
    pConnect = (connect_fn)(void (*)(void))GetProcAddress(ntdll, "NtAlpcConnectPort");
    pAccept = (accept_fn)(void (*)(void))GetProcAddress(ntdll, "NtAlpcAcceptConnectPort");
    pSwr = (swr_fn)(void (*)(void))GetProcAddress(ntdll, "NtAlpcSendWaitReceivePort");
    pDisconnect = (disconnect_fn)(void (*)(void))GetProcAddress(ntdll, "NtAlpcDisconnectPort");
    pQuery = (query_fn)(void (*)(void))GetProcAddress(ntdll, "NtAlpcQueryInformation");
    pImpersonate = (impersonate_fn)(void (*)(void))GetProcAddress(ntdll, "NtAlpcImpersonateClientOfPort");
    pInit = (init_fn)(void (*)(void))GetProcAddress(ntdll, "AlpcInitializeMessageAttribute");
    pGet = (get_fn)(void (*)(void))GetProcAddress(ntdll, "AlpcGetMessageAttribute");
    pQueryObject = (qobj_fn)(void (*)(void))GetProcAddress(ntdll, "NtQueryObject");
    oracle_case("exports");
    o_bool("create", pCreate != NULL);
    o_bool("connect", pConnect != NULL);
    o_bool("accept", pAccept != NULL);
    o_bool("send_wait_receive", pSwr != NULL);
    o_bool("disconnect", pDisconnect != NULL);
    o_bool("query", pQuery != NULL);
    o_bool("impersonate", pImpersonate != NULL);
    o_bool("connect_ex", GetProcAddress(ntdll, "NtAlpcConnectPortEx") != NULL);
    o_bool("set_information", GetProcAddress(ntdll, "NtAlpcSetInformation") != NULL);
    oracle_case_end();
    if (!pCreate || !pConnect || !pAccept || !pSwr || !pDisconnect || !pQuery || !pImpersonate ||
        !pInit || !pGet)
        return oracle_end();

    /* ---- creation ---- */
    st = pCreate(&unnamed, NULL, NULL);
    oracle_case("create_unnamed_no_attributes");
    o_status("ret", st);
    o_handle("handle", unnamed);
    if (!st) {
        o_type_name("type_name", unnamed);
        o_u32("wait", WaitForSingleObject(unnamed, 0));
        memset(&bi, 0xcc, sizeof(bi));
        ret = 0x7777;
        st = pQuery(unnamed, 0 /* AlpcBasicInformation */, &bi, sizeof(bi), &ret);
        o_status("basic_ret", st);
        o_u32("basic_retlen", ret);
        if (!st) {
            o_hex32("basic_flags", bi.Flags);
            o_bool("basic_seq_nonzero", bi.SequenceNo != 0);
            o_bool("basic_context_null", bi.PortContext == NULL);
        }
        ret = 0x7777;
        st = pQuery(unnamed, 0, &bi, sizeof(bi) - 1, &ret);
        o_status("basic_short_ret", st);
        o_u32("basic_short_retlen", ret);
    }
    oracle_case_end();

    init_port_attrs(&pa, PORFLG_WAITABLE, 0x200);
    st = pCreate(&waitable, NULL, &pa);
    oracle_case("create_waitable");
    o_status("ret", st);
    if (!st) {
        o_u32("wait_empty", WaitForSingleObject(waitable, 0));
        memset(&bi, 0xcc, sizeof(bi));
        st = pQuery(waitable, 0, &bi, sizeof(bi), &ret);
        o_status("basic_ret", st);
        if (!st) o_hex32("basic_flags", bi.Flags);
    }
    oracle_case_end();

    init_port_attrs(&pa, 0, 0x100000);
    st = pCreate(&h, NULL, &pa);
    oracle_case("create_huge_message_length");
    o_status("ret", st);
    if (!st) CloseHandle(h);
    oracle_case_end();

    init_name(&us, port_name);
    init_oa(&oa, &us);
    init_port_attrs(&pa, PORFLG_ALLOW_IMPERSONATION, 0x200);
    st = pCreate(&conn, &oa, &pa);
    oracle_case("create_named");
    o_status("ret", st);
    o_handle("handle", conn);
    oracle_case_end();
    if (st) return oracle_end();

    st = pCreate(&conn2, &oa, &pa);
    oracle_case("create_named_twice");
    o_status("ret", st);
    if (!st) CloseHandle(conn2);
    oracle_case_end();

    oa.Attributes = OBJ_OPENIF;
    conn2 = NULL;
    st = pCreate(&conn2, &oa, &pa);
    oracle_case("create_named_openif");
    o_status("ret", st);
    if (st >= 0 && conn2) CloseHandle(conn2);
    oracle_case_end();
    oa.Attributes = 0;

    /* ---- receive on an empty queue ---- */
    st = server_receive(conn, &m, &len, 0);
    oracle_case("receive_empty_timeout0");
    o_status("ret", st);
    o_u64("buflen", len);
    oracle_case_end();

    /* ---- connecting ---- */
    cl.go = CreateEventW(NULL, FALSE, FALSE, NULL);
    cl.done = CreateEventW(NULL, TRUE, FALSE, NULL);
    cl.thread = CreateThread(NULL, 0, client_main, &cl, 0, NULL);

    {
        HANDLE c = NULL;
        init_name(&us_missing, L"\\RPC Control\\SetuOracleAlpcMissing");
        init_oa(&oa_missing, &us_missing);
        len = sizeof(m);
        init_msg(&m, "x");
        st = pConnect(&c, &us_missing, NULL, NULL, MSGFLG_SYNC, NULL, &m.h, &len, NULL, NULL,
                      ms_timeout(&t, 1000));
        oracle_case("connect_missing");
        o_status("ret", st);
        o_handle("handle", c);
        oracle_case_end();
    }

    /* nobody listens: the request times out */
    client_start(OP_CONNECT, "timeout", MSGFLG_SYNC, 300);
    oracle_case("connect_timeout");
    o_bool("returned", client_wait(5000));
    o_status("ret", cl.status);
    o_handle("handle", cl.port);
    oracle_case_end();
    if (cl.port) CloseHandle(cl.port);

    /* is the timed-out request still queued? */
    st = server_receive(conn, &m, &len, 0);
    oracle_case("after_timeout_queue");
    o_status("ret", st);
    if (!st) {
        o_header("msg", &m, len, cl.tid);
        o_ctx("ctx", &rattr.a, &m);
        h = NULL;
        st = pAccept(&h, conn, 0, NULL, NULL, NULL, &m.h, NULL, TRUE);
        o_status("accept_ret", st);
        o_handle("accept_handle", h);
        if (!st && h) CloseHandle(h);
    }
    oracle_case_end();

    /* refused */
    client_start(OP_CONNECT, "refuse me", MSGFLG_SYNC, 5000);
    st = server_receive(conn, &m, &len, 5000);
    oracle_case("connect_request_refused");
    o_status("receive_ret", st);
    if (!st) {
        o_header("request", &m, len, cl.tid);
        h = (HANDLE)0x1;
        st = pAccept(&h, conn, 0, NULL, NULL, NULL, &m.h, NULL, FALSE);
        o_status("accept_false_ret", st);
        o_bool("accept_false_handle_untouched", h == (HANDLE)0x1);
        o_handle("accept_false_handle", h);
    }
    o_bool("client_returned", client_wait(5000));
    o_status("client_ret", cl.status);
    o_handle("client_handle", cl.port);
    oracle_case_end();
    if (cl.port) CloseHandle(cl.port);

    /* accepted, with connection data in both directions */
    client_start(OP_CONNECT, "hello server", MSGFLG_SYNC, 5000);
    st = server_receive(conn, &m, &len, 5000);
    oracle_case("connect_request");
    o_status("receive_ret", st);
    if (!st) {
        o_header("request", &m, len, cl.tid);
        o_ctx("request_ctx", &rattr.a, &m);
        o_bool("client_still_waiting", !client_wait(200));
    }
    oracle_case_end();
    if (st) goto done;

    last_msgid = m.h.MessageId;
    memcpy(m.data, "welcome", 7);
    m.h.u1.s1.DataLength = 7;
    m.h.u1.s1.TotalLength = sizeof(PORT_MESSAGE_) + 7;
    init_port_attrs(&pa, 0, 0x200);
    st = pAccept(&comm, conn, 0, NULL, &pa, (void *)0x1234, &m.h, NULL, TRUE);
    oracle_case("accept");
    o_status("ret", st);
    o_handle("handle", comm);
    o_bool("client_returned", client_wait(5000));
    o_status("client_ret", cl.status);
    o_handle("client_handle", cl.port);
    if (!cl.status) o_header("client_reply", &cl.msg, cl.len, 0);
    if (!st) {
        o_type_name("comm_type_name", comm);
        memset(&bi, 0xcc, sizeof(bi));
        st = pQuery(comm, 0, &bi, sizeof(bi), &ret);
        o_status("comm_basic_ret", st);
        if (!st) {
            o_hex32("comm_basic_flags", bi.Flags);
            o_hex32("comm_basic_context", (DWORD)(ULONG_PTR)bi.PortContext);
        }
        if (cl.port) {
            memset(&bi, 0xcc, sizeof(bi));
            st = pQuery(cl.port, 0, &bi, sizeof(bi), &ret);
            o_status("client_basic_ret", st);
            if (!st) {
                o_hex32("client_basic_flags", bi.Flags);
                o_bool("client_basic_context_null", bi.PortContext == NULL);
            }
        }
    }
    oracle_case_end();
    if (cl.status || !comm) goto done;

    /* ---- a synchronous request and its reply through the connection port ---- */
    client_start(OP_REQUEST, "request one", 0, 5000);
    st = server_receive(conn, &m, &len, 5000);
    oracle_case("request_on_connection_port");
    o_status("receive_ret", st);
    if (!st) {
        o_header("request", &m, len, cl.tid);
        o_ctx("ctx", &rattr.a, &m);
        st = pImpersonate(conn, &m.h, NULL);
        o_status("impersonate_ret", st);
        if (!st) {
            HANDLE tok = NULL;
            o_bool("thread_token", OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &tok));
            o_u32("thread_token_error", tok ? 0 : GetLastError());
            if (tok) CloseHandle(tok);
            tok = NULL;
            o_bool("thread_token_not_self", OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, FALSE, &tok));
            if (tok) CloseHandle(tok);
            RevertToSelf();
        }
        last_msgid = m.h.MessageId;
        last_cb = m.h.CallbackId;
        reply = m;
        memcpy(reply.data, "reply one", 9);
        reply.h.u1.s1.DataLength = 9;
        reply.h.u1.s1.TotalLength = sizeof(PORT_MESSAGE_) + 9;
        st = pSwr(conn, MSGFLG_REPLY, &reply.h, NULL, NULL, NULL, NULL, NULL);
        o_status("reply_ret", st);
        o_bool("client_returned", client_wait(5000));
        o_status("client_ret", cl.status);
        if (!cl.status) o_header("client_reply", &cl.msg, cl.len, 0);
        /* the request is gone now */
        st = pImpersonate(conn, &m.h, NULL);
        o_status("impersonate_after_reply", st);
        st = pSwr(conn, MSGFLG_REPLY, &reply.h, NULL, NULL, NULL, NULL, NULL);
        o_status("reply_twice_ret", st);
    } else client_wait(5000);
    oracle_case_end();

    /* reply through the communication port */
    client_start(OP_REQUEST, "request two", 0, 5000);
    st = server_receive(conn, &m, &len, 5000);
    oracle_case("reply_on_communication_port");
    o_status("receive_ret", st);
    if (!st) {
        reply = m;
        memcpy(reply.data, "reply two", 9);
        reply.h.u1.s1.DataLength = 9;
        reply.h.u1.s1.TotalLength = sizeof(PORT_MESSAGE_) + 9;
        st = pSwr(comm, MSGFLG_REPLY, &reply.h, NULL, NULL, NULL, NULL, NULL);
        o_status("reply_ret", st);
        o_bool("client_returned", client_wait(5000));
        o_status("client_ret", cl.status);
        last_msgid = m.h.MessageId;
        last_cb = m.h.CallbackId;
        if (!cl.status) o_header("client_reply", &cl.msg, cl.len, GetCurrentThreadId());
    } else client_wait(5000);
    oracle_case_end();

    /* a reply whose header has another type and ids */
    client_start(OP_REQUEST, "request zero", 0, 5000);
    st = server_receive(conn, &m, &len, 5000);
    oracle_case("reply_header_rewritten");
    o_status("receive_ret", st);
    if (!st) {
        reply = m;
        reply.h.u2.ZeroInit = 0;
        reply.h.ClientId.UniqueProcess = (HANDLE)0x10;
        reply.h.ClientId.UniqueThread = (HANDLE)0x20;
        reply.h.CallbackId = 0;
        reply.h.u1.s1.DataLength = 4;
        reply.h.u1.s1.TotalLength = sizeof(PORT_MESSAGE_) + 4;
        memcpy(reply.data, "zero", 4);
        st = pSwr(conn, MSGFLG_REPLY, &reply.h, NULL, NULL, NULL, NULL, NULL);
        o_status("reply_ret", st);
        o_bool("client_returned", client_wait(5000));
        o_status("client_ret", cl.status);
        last_msgid = m.h.MessageId;
        last_cb = m.h.CallbackId;
        if (!cl.status) o_header("client_reply", &cl.msg, cl.len, GetCurrentThreadId());
    } else client_wait(5000);
    oracle_case_end();

    /* a reply with a wrong message id */
    client_start(OP_REQUEST, "request wrong id", 0, 1000);
    st = server_receive(conn, &m, &len, 5000);
    oracle_case("reply_wrong_id");
    o_status("receive_ret", st);
    if (!st) {
        reply = m;
        reply.h.MessageId ^= 0x10000;
        st = pSwr(conn, MSGFLG_REPLY, &reply.h, NULL, NULL, NULL, NULL, NULL);
        o_status("reply_ret", st);
        reply.h.MessageId = m.h.MessageId;
        st = pSwr(conn, MSGFLG_REPLY, &reply.h, NULL, NULL, NULL, NULL, NULL);
        o_status("right_reply_ret", st);
    }
    o_bool("client_returned", client_wait(5000));
    o_status("client_ret", cl.status);
    oracle_case_end();

    /* a request that times out on the client side, then the late reply */
    client_start(OP_REQUEST, "request three", 0, 300);
    st = server_receive(conn, &m, &len, 5000);
    oracle_case("request_client_timeout");
    o_status("receive_ret", st);
    o_bool("client_returned", client_wait(5000));
    o_status("client_ret", cl.status);
    if (!st) {
        reply = m;
        reply.h.u1.s1.DataLength = 1;
        reply.h.u1.s1.TotalLength = sizeof(PORT_MESSAGE_) + 1;
        st = pSwr(conn, MSGFLG_REPLY, &reply.h, NULL, NULL, NULL, NULL, NULL);
        o_status("late_reply_ret", st);
    }
    oracle_case_end();
    /* the client may have a cancelled reply queued; drain it */
    client_start(OP_RECEIVE, NULL, 0, 0);
    client_wait(5000);
    oracle_case("after_client_timeout_client_queue");
    o_status("ret", cl.status);
    if (!cl.status) o_hex32("type", (USHORT)cl.msg.h.u2.s2.Type);
    oracle_case_end();

    /* ---- datagrams (release flag) ---- */
    client_start(OP_DATAGRAM, "release up", MSGFLG_RELEASE, 0);
    client_wait(5000);
    oracle_case("release_to_server");
    o_status("send_ret", cl.status);
    client_start(OP_DATAGRAM, "release up 2", MSGFLG_RELEASE, 0);
    client_wait(5000);
    o_status("send2_ret", cl.status);
    st = server_receive(conn, &m, &len, 5000);
    o_status("receive_ret", st);
    if (!st) {
        o_header("msg", &m, len, cl.tid);
        o_ctx("ctx", &rattr.a, &m);
        reply = m;
        st = pSwr(conn, MSGFLG_REPLY, &reply.h, NULL, NULL, NULL, NULL, NULL);
        o_status("reply_ret", st);
    }
    st = server_receive(conn, &m, &len, 5000);
    o_status("receive2_ret", st);
    if (!st) o_header("msg2", &m, len, cl.tid);
    oracle_case_end();

    init_msg(&m, "release down");
    st = pSwr(comm, MSGFLG_RELEASE, &m.h, NULL, NULL, NULL, NULL, NULL);
    oracle_case("release_to_client");
    o_status("send_ret", st);
    client_start(OP_RECEIVE, NULL, 0, 1000);
    o_bool("client_returned", client_wait(5000));
    o_status("client_ret", cl.status);
    if (!cl.status) o_header("msg", &cl.msg, cl.len, GetCurrentThreadId());
    oracle_case_end();

    init_msg(&m, "sync down");
    {
        MSG_ r2;
        SIZE_T l2 = sizeof(r2);
        st = pSwr(comm, MSGFLG_SYNC, &m.h, NULL, &r2.h, &l2, NULL, ms_timeout(&t, 300));
    }
    oracle_case("sync_request_to_client");
    o_status("send_ret", st);
    oracle_case_end();

    /* ---- datagrams ---- */
    client_start(OP_DATAGRAM, "datagram up", 0, 0);
    client_wait(5000);
    oracle_case("datagram_to_server");
    o_status("send_ret", cl.status);
    st = server_receive(conn, &m, &len, 5000);
    o_status("receive_ret", st);
    if (!st) {
        o_header("msg", &m, len, cl.tid);
        o_ctx("ctx", &rattr.a, &m);
        reply = m;
        st = pSwr(conn, MSGFLG_REPLY, &reply.h, NULL, NULL, NULL, NULL, NULL);
        o_status("reply_to_datagram_ret", st);
    }
    oracle_case_end();

    init_msg(&m, "datagram down");
    st = pSwr(comm, 0, &m.h, NULL, NULL, NULL, NULL, NULL);
    oracle_case("datagram_to_client");
    o_status("send_ret", st);
    client_start(OP_RECEIVE, NULL, 0, 5000);
    o_bool("client_returned", client_wait(5000));
    o_status("client_ret", cl.status);
    if (!cl.status) o_header("msg", &cl.msg, cl.len, GetCurrentThreadId());
    oracle_case_end();

    /* sending on the connection port itself (no peer) */
    init_msg(&m, "to nobody");
    st = pSwr(conn, 0, &m.h, NULL, NULL, NULL, NULL, NULL);
    oracle_case("datagram_on_connection_port");
    o_status("ret", st);
    oracle_case_end();

    /* ---- message validation ---- */
    init_msg(&m, "bad");
    m.h.u1.s1.TotalLength = 4;
    st = pSwr(comm, 0, &m.h, NULL, NULL, NULL, NULL, NULL);
    oracle_case("send_total_below_header");
    o_status("ret", st);
    oracle_case_end();

    init_msg(&m, "bad");
    m.h.u1.s1.TotalLength = sizeof(PORT_MESSAGE_) + 8;
    st = pSwr(comm, 0, &m.h, NULL, NULL, NULL, NULL, NULL);
    oracle_case("send_total_not_header_plus_data");
    o_status("ret", st);
    oracle_case_end();
    client_start(OP_RECEIVE, NULL, 0, 0);
    client_wait(5000);

    init_msg(&m, "big");
    m.h.u1.s1.DataLength = 0x300;
    m.h.u1.s1.TotalLength = sizeof(PORT_MESSAGE_) + 0x300;
    st = pSwr(comm, 0, &m.h, NULL, NULL, NULL, NULL, NULL);
    oracle_case("send_longer_than_max");
    o_status("ret", st);
    oracle_case_end();

    /* ---- a receive buffer that is too small (client to server) ---- */
    client_start(OP_DATAGRAM, "a message that is longer than the buffer", MSGFLG_RELEASE, 0);
    client_wait(5000);
    oracle_case("server_receive_short_buffer");
    o_status("send_ret", cl.status);
    {
        MSG_ small;
        SIZE_T slen = sizeof(PORT_MESSAGE_) + 4;
        memset(&small, 0xcc, sizeof(small));
        st = pSwr(conn, 0, NULL, NULL, &small.h, &slen, NULL, ms_timeout(&t, 1000));
        o_status("short_ret", st);
        o_u64("short_len", slen);
        o_bool("header_untouched", small.h.u1.Length == 0xcccccccc);
        o_bool("data_untouched", small.data[0] == 0xcc);
        slen = sizeof(small);
        st = pSwr(conn, 0, NULL, NULL, &small.h, &slen, NULL, ms_timeout(&t, 0));
        o_status("full_ret", st);
        o_u64("full_len", slen);
        if (!st) o_header("full", &small, slen, cl.tid);
        st = pSwr(conn, 0, NULL, NULL, &small.h, NULL, NULL, ms_timeout(&t, 0));
        o_status("null_len_ret", st);
    }
    oracle_case_end();

    /* ---- a receive buffer that is too small ---- */
    init_msg(&m, "a message that is longer than the buffer");
    st = pSwr(comm, 0, &m.h, NULL, NULL, NULL, NULL, NULL);
    oracle_case("receive_short_buffer");
    o_status("send_ret", st);
    {
        MSG_ small;
        SIZE_T slen = sizeof(PORT_MESSAGE_) + 4;
        memset(&small, 0xcc, sizeof(small));
        st = pSwr(cl.port, 0, NULL, NULL, &small.h, &slen, NULL, ms_timeout(&t, 1000));
        o_status("short_ret", st);
        o_u64("short_len", slen);
        o_bool("header_untouched", small.h.u1.Length == 0xcccccccc);
        slen = sizeof(PORT_MESSAGE_);
        st = pSwr(cl.port, 0, NULL, NULL, &small.h, &slen, NULL, ms_timeout(&t, 0));
        o_status("header_only_ret", st);
        o_u64("header_only_len", slen);
        slen = sizeof(small);
        st = pSwr(cl.port, 0, NULL, NULL, &small.h, &slen, NULL, ms_timeout(&t, 0));
        o_status("full_ret", st);
        o_u64("full_len", slen);
    }
    oracle_case_end();

    /* ---- waitable port signalling ---- */
    oracle_case("waitable_port_signal");
    if (waitable) {
        /* a datagram sent to oneself is not possible without a connection;
         * just check the state of the waitable connection port */
        o_u32("wait", WaitForSingleObject(waitable, 0));
        init_msg(&m, "self");
        st = pSwr(waitable, MSGFLG_RELEASE, &m.h, NULL, NULL, NULL, NULL, NULL);
        o_status("self_send_ret", st);
        o_u32("wait_queued", WaitForSingleObject(waitable, 0));
        o_u32("wait_queued_again", WaitForSingleObject(waitable, 0));
        st = server_receive(waitable, &m, &len, 0);
        o_status("self_receive_ret", st);
        if (!st) o_header("self", &m, len, GetCurrentThreadId());
        o_u32("wait_drained", WaitForSingleObject(waitable, 0));
    }
    o_u32("conn_wait", WaitForSingleObject(conn, 0));
    oracle_case_end();

    /* ---- disconnect ---- */
    client_start(OP_DISCONNECT, NULL, 0, 0);
    client_wait(5000);
    oracle_case("client_disconnect");
    o_status("ret", cl.status);
    st = server_receive(conn, &m, &len, 1000);
    o_status("server_receive_ret", st);
    if (!st) {
        o_header("msg", &m, len, 0);
        o_ctx("ctx", &rattr.a, &m);
    }
    st = server_receive(conn, &m, &len, 0);
    o_status("server_receive_again", st);
    init_msg(&m, "after disconnect");
    st = pSwr(comm, 0, &m.h, NULL, NULL, NULL, NULL, NULL);
    o_status("server_send_ret", st);
    oracle_case_end();

    client_start(OP_DATAGRAM, "after disconnect", 0, 0);
    client_wait(5000);
    oracle_case("send_after_disconnect");
    o_status("client_send_ret", cl.status);
    st = pDisconnect(cl.port, 0);
    o_status("disconnect_twice", st);
    oracle_case_end();

    client_start(OP_CLOSE, NULL, 0, 0);
    client_wait(5000);
    CloseHandle(comm);
    comm = NULL;

    /* second connection: closing the client handle without disconnecting */
    client_start(OP_CONNECT, "second", MSGFLG_SYNC, 5000);
    st = server_receive(conn, &m, &len, 5000);
    if (!st) {
        m.h.u1.s1.DataLength = 0;
        m.h.u1.s1.TotalLength = sizeof(PORT_MESSAGE_);
        st = pAccept(&comm, conn, 0, NULL, NULL, (void *)0x5678, &m.h, NULL, TRUE);
    }
    oracle_case("client_close");
    o_status("accept_ret", st);
    o_bool("client_returned", client_wait(5000));
    o_status("client_ret", cl.status);
    if (!st && !cl.status) {
        o_header("client_reply", &cl.msg, cl.len, 0);
        client_start(OP_CLOSE, NULL, 0, 0);
        client_wait(5000);
        st = server_receive(conn, &m, &len, 1000);
        o_status("server_receive_ret", st);
        if (!st) {
            o_header("msg", &m, len, 0);
            o_ctx("ctx", &rattr.a, &m);
        }
        init_msg(&m, "after close");
        st = pSwr(comm, 0, &m.h, NULL, NULL, NULL, NULL, NULL);
        o_status("server_send_ret", st);
    }
    oracle_case_end();
    if (comm) CloseHandle(comm);
    comm = NULL;

    /* third connection: the server closes its communication port */
    client_start(OP_CONNECT, "third", MSGFLG_SYNC, 5000);
    st = server_receive(conn, &m, &len, 5000);
    if (!st) {
        m.h.u1.s1.DataLength = 0;
        m.h.u1.s1.TotalLength = sizeof(PORT_MESSAGE_);
        st = pAccept(&comm, conn, 0, NULL, NULL, (void *)0x9abc, &m.h, NULL, TRUE);
    }
    oracle_case("server_close_comm");
    o_status("accept_ret", st);
    o_bool("client_returned", client_wait(5000));
    o_status("client_ret", cl.status);
    if (!st && !cl.status) {
        CloseHandle(comm);
        comm = NULL;
        client_start(OP_RECEIVE, NULL, 0, 1000);
        client_wait(5000);
        o_status("client_receive_ret", cl.status);
        if (!cl.status) o_header("msg", &cl.msg, cl.len, 0);
        client_start(OP_DATAGRAM, "to closed", 0, 0);
        client_wait(5000);
        o_status("client_send_ret", cl.status);
        st = server_receive(conn, &m, &len, 0);
        o_status("server_receive_ret", st);
        if (!st) o_hex32("server_receive_type", (USHORT)m.h.u2.s2.Type);
    }
    oracle_case_end();
    client_start(OP_CLOSE, NULL, 0, 0);
    client_wait(5000);

    /* fourth connection: the connection port is closed while connected */
    client_start(OP_CONNECT, "fourth", MSGFLG_SYNC, 5000);
    st = server_receive(conn, &m, &len, 5000);
    if (!st) {
        m.h.u1.s1.DataLength = 0;
        m.h.u1.s1.TotalLength = sizeof(PORT_MESSAGE_);
        st = pAccept(&comm, conn, 0, NULL, NULL, NULL, &m.h, NULL, TRUE);
    }
    oracle_case("close_connection_port");
    o_status("accept_ret", st);
    o_bool("client_returned", client_wait(5000));
    o_status("client_ret", cl.status);
    if (!st && !cl.status) {
        HANDLE c2 = NULL;
        CloseHandle(conn);
        conn = NULL;
        init_msg(&m, "comm after conn closed");
        st = pSwr(comm, 0, &m.h, NULL, NULL, NULL, NULL, NULL);
        o_status("server_send_ret", st);
        client_start(OP_RECEIVE, NULL, 0, 1000);
        client_wait(5000);
        o_status("client_receive_ret", cl.status);
        if (!cl.status) o_header("msg", &cl.msg, cl.len, GetCurrentThreadId());
        client_start(OP_DATAGRAM, "up after conn closed", 0, 0);
        client_wait(5000);
        o_status("client_send_ret", cl.status);
        len = sizeof(m);
        init_msg(&m, "x");
        st = pConnect(&c2, &us, NULL, NULL, MSGFLG_SYNC, NULL, &m.h, &len, NULL, NULL,
                      ms_timeout(&t, 300));
        o_status("connect_after_close", st);
        if (c2) CloseHandle(c2);
    }
    oracle_case_end();
    client_start(OP_CLOSE, NULL, 0, 0);
    client_wait(5000);
    if (comm) CloseHandle(comm);

done:
    client_start(OP_EXIT, NULL, 0, 0);
    client_wait(5000);
    if (conn) CloseHandle(conn);
    if (unnamed) CloseHandle(unnamed);
    if (waitable) CloseHandle(waitable);
    return oracle_end();
}
