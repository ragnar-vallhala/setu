/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: the security side of ALPC connections, set up the way the
 * Windows App SDK's CoreMessagingXP.dll (shipped with Firefox) sets up its
 * registrar port, as Wine's own +alpc trace shows it: a waitable port that
 * allows impersonation (flags 0x70000) with an identification-level,
 * effective-only security QoS, a client that connects synchronously with
 * the same port attributes and a security message attribute (flags 0, no
 * QoS, context handle -2), and a server that receives with the security,
 * view, context and work-on-behalf attributes allocated, impersonates the
 * client and looks at the thread token.
 *
 * Printed: which receive attributes Windows fills for a connection request
 * and for a request, what NtAlpcImpersonateClientOfPort leaves on the
 * thread (token type, impersonation level, user), and the port names it
 * accepts under \BaseNamedObjects.
 *
 * Neither MinGW-w64 nor Wine declare the ALPC system calls. The prototypes and
 * structure layouts follow the public, MIT-licensed phnt headers
 * (ntlpcapi.h); everything printed here is black-box behaviour.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-security_quality_of_service
 *       https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-openthreadtoken
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
    ULONG Flags;
    SECURITY_QUALITY_OF_SERVICE *QoS;
    HANDLE ContextHandle;
} ALPC_SECURITY_ATTR_;

typedef struct {
    void *PortContext;
    void *MessageContext;
    ULONG Sequence;
    ULONG MessageId;
    ULONG CallbackId;
} ALPC_CONTEXT_ATTR_;

#define ATTR_SECURITY 0x80000000u
#define ATTR_VIEW     0x40000000u
#define ATTR_CONTEXT  0x20000000u
#define ATTR_WORK     0x02000000u
#define MSGFLG_REPLY 0x1
#define MSGFLG_SYNC 0x20000

typedef LONG (WINAPI *create_fn)(HANDLE *, OBJECT_ATTRIBUTES *, ALPC_PORT_ATTRIBUTES_ *);
typedef LONG (WINAPI *connect_fn)(HANDLE *, UNICODE_STRING *, OBJECT_ATTRIBUTES *, ALPC_PORT_ATTRIBUTES_ *,
                                  ULONG, void *, PORT_MESSAGE_ *, SIZE_T *, ALPC_MESSAGE_ATTRIBUTES_ *,
                                  ALPC_MESSAGE_ATTRIBUTES_ *, LARGE_INTEGER *);
typedef LONG (WINAPI *accept_fn)(HANDLE *, HANDLE, ULONG, OBJECT_ATTRIBUTES *, ALPC_PORT_ATTRIBUTES_ *,
                                 void *, PORT_MESSAGE_ *, ALPC_MESSAGE_ATTRIBUTES_ *, BOOLEAN);
typedef LONG (WINAPI *swr_fn)(HANDLE, ULONG, PORT_MESSAGE_ *, ALPC_MESSAGE_ATTRIBUTES_ *, PORT_MESSAGE_ *,
                              SIZE_T *, ALPC_MESSAGE_ATTRIBUTES_ *, LARGE_INTEGER *);
typedef LONG (WINAPI *impersonate_fn)(HANDLE, PORT_MESSAGE_ *, void *);
typedef LONG (WINAPI *init_fn)(ULONG, ALPC_MESSAGE_ATTRIBUTES_ *, SIZE_T, SIZE_T *);
typedef void *(WINAPI *get_fn)(ALPC_MESSAGE_ATTRIBUTES_ *, ULONG);

static create_fn pCreate;
static connect_fn pConnect;
static accept_fn pAccept;
static swr_fn pSwr;
static impersonate_fn pImpersonate;
static init_fn pInit;
static get_fn pGet;

typedef struct {
    PORT_MESSAGE_ h;
    BYTE data[0x2100];
} MSG_;

static const WCHAR port_name[] = L"\\RPC Control\\SetuOracleAlpcSecurity";

static union { ALPC_MESSAGE_ATTRIBUTES_ a; BYTE b[256]; ULONG_PTR align; } cl_attrs, sv_attrs;

static struct {
    HANDLE port;
    LONG connect_status;
    LONG request_status;
    MSG_ reply;
    SIZE_T reply_len;
    HANDLE go, done;
    LONG more_status;
    int stop;
} cl;

static void init_port_attrs(ALPC_PORT_ATTRIBUTES_ *a)
{
    memset(a, 0, sizeof(*a));
    a->Flags = 0x70000;
    a->SecurityQos.Length = 12;
    a->SecurityQos.ImpersonationLevel = SecurityIdentification;
    a->SecurityQos.ContextTrackingMode = SECURITY_STATIC_TRACKING;
    a->SecurityQos.EffectiveOnly = TRUE;
    a->MaxMessageLength = 0x20f0;
}

static void init_name(UNICODE_STRING *us, const WCHAR *s)
{
    us->Buffer = (WCHAR *)s;
    us->Length = (USHORT)(wcslen(s) * sizeof(WCHAR));
    us->MaximumLength = us->Length + sizeof(WCHAR);
}

static void init_msg(MSG_ *m, const char *text)
{
    SHORT n = (SHORT)strlen(text);
    memset(m, 0, sizeof(*m));
    m->h.u1.s1.DataLength = n;
    m->h.u1.s1.TotalLength = (SHORT)(sizeof(PORT_MESSAGE_) + n);
    memcpy(m->data, text, n);
}

static void set_client_security(void)
{
    SIZE_T req;
    ALPC_SECURITY_ATTR_ *sec;

    memset(&cl_attrs, 0, sizeof(cl_attrs));
    pInit(ATTR_SECURITY, &cl_attrs.a, sizeof(cl_attrs), &req);
    sec = pGet(&cl_attrs.a, ATTR_SECURITY);
    sec->Flags = 0;
    sec->QoS = NULL;
    sec->ContextHandle = (HANDLE)(LONG_PTR)-2;
    cl_attrs.a.ValidAttributes = ATTR_SECURITY;
}

static DWORD WINAPI client_thread(void *arg)
{
    UNICODE_STRING us;
    (void)arg;
    ALPC_PORT_ATTRIBUTES_ pa;
    LARGE_INTEGER t;
    MSG_ m;
    SIZE_T len = sizeof(m);

    init_name(&us, port_name);
    init_port_attrs(&pa);
    init_msg(&m, "hello");
    set_client_security();
    t.QuadPart = -50000000;
    cl.connect_status = pConnect(&cl.port, &us, NULL, &pa, MSGFLG_SYNC, NULL, &m.h, &len, &cl_attrs.a, NULL, &t);
    if (cl.connect_status) return 0;

    WaitForSingleObject(cl.go, 10000);
    init_msg(&m, "request");
    set_client_security();
    memset(&cl.reply, 0, sizeof(cl.reply));
    cl.reply_len = sizeof(cl.reply);
    t.QuadPart = -50000000;
    cl.request_status = pSwr(cl.port, MSGFLG_SYNC, &m.h, &cl_attrs.a, &cl.reply.h, &cl.reply_len, NULL, &t);

    /* more synchronous requests, answered in different ways */
    for (;;) {
        WaitForSingleObject(cl.go, 10000);
        if (cl.stop) break;
        init_msg(&m, "again");
        memset(&cl.reply, 0, sizeof(cl.reply));
        cl.reply_len = sizeof(cl.reply);
        t.QuadPart = -20000000;
        cl.more_status = pSwr(cl.port, MSGFLG_SYNC, &m.h, NULL, &cl.reply.h, &cl.reply_len, NULL, &t);
        SetEvent(cl.done);
    }
    return 0;
}

static SECURITY_QUALITY_OF_SERVICE qos_buf;

static void print_received_attrs(void)
{
    ALPC_SECURITY_ATTR_ *sec = pGet(&sv_attrs.a, ATTR_SECURITY);
    ALPC_CONTEXT_ATTR_ *ctx = pGet(&sv_attrs.a, ATTR_CONTEXT);

    o_hex32("valid_attrs", sv_attrs.a.ValidAttributes);
    if (sv_attrs.a.ValidAttributes & ATTR_SECURITY) {
        o_hex32("security_flags", sec->Flags);
        o_bool("security_qos_null", sec->QoS == NULL);
        o_bool("security_qos_is_buffer", sec->QoS == &qos_buf);
        if (sec->QoS) {
            o_u32("security_qos_level", sec->QoS->ImpersonationLevel);
            o_u32("security_qos_tracking", sec->QoS->ContextTrackingMode);
            o_u32("security_qos_effective", sec->QoS->EffectiveOnly);
        }
        o_bool("security_ctx_minus2", sec->ContextHandle == (HANDLE)(LONG_PTR)-2);
        o_bool("security_ctx_null", sec->ContextHandle == NULL);
    }
    if (sv_attrs.a.ValidAttributes & ATTR_CONTEXT) {
        o_bool("context_port_null", ctx->PortContext == NULL);
    }
}

/* receive one message on PORT with the attributes FLAGS; WITH_QOS points
 * the security attribute at a QoS buffer first */
static LONG server_receive_with(HANDLE port, MSG_ *m, ULONG flags, int with_qos, SIZE_T len)
{
    SIZE_T req;
    LARGE_INTEGER t;
    ALPC_SECURITY_ATTR_ *sec;

    memset(&sv_attrs, 0, sizeof(sv_attrs));
    pInit(flags, &sv_attrs.a, sizeof(sv_attrs), &req);
    if (with_qos && (sec = pGet(&sv_attrs.a, ATTR_SECURITY))) {
        memset(&qos_buf, 0, sizeof(qos_buf));
        sec->QoS = &qos_buf;
    }
    memset(m, 0, sizeof(*m));
    t.QuadPart = -30000000;
    return pSwr(port, 0, NULL, NULL, &m->h, &len, &sv_attrs.a, &t);
}

/* wait for the waitable port, then receive with the attributes
 * CoreMessagingXP asks for */
static LONG server_receive(HANDLE port, MSG_ *m)
{
    o_u32("wait", WaitForSingleObject(port, 5000));
    return server_receive_with(port, m, ATTR_SECURITY | ATTR_VIEW | ATTR_CONTEXT | ATTR_WORK, 0, 0x20f0);
}

/* ---- which settings let a connection request be received ---- */

static struct probe {
    const WCHAR *name;
    ULONG port_flags, client_flags;
    int port_qos, client_qos, sec_attr;
    HANDLE ctx_handle;
    LONG connect_status;
} pr;

static void init_attrs_for(ALPC_PORT_ATTRIBUTES_ *a, ULONG flags, int qos)
{
    init_port_attrs(a);
    a->Flags = flags;
    if (!qos) memset(&a->SecurityQos, 0, sizeof(a->SecurityQos));
}

static DWORD WINAPI probe_client(void *arg)
{
    UNICODE_STRING us;
    ALPC_PORT_ATTRIBUTES_ pa;
    LARGE_INTEGER t;
    HANDLE port = NULL;
    MSG_ m;
    SIZE_T len = sizeof(m);
    SIZE_T req;
    ALPC_SECURITY_ATTR_ *sec;
    static union { ALPC_MESSAGE_ATTRIBUTES_ a; BYTE b[64]; ULONG_PTR align; } at;

    (void)arg;
    init_name(&us, pr.name);
    init_attrs_for(&pa, pr.client_flags, pr.client_qos);
    init_msg(&m, "hello");
    memset(&at, 0, sizeof(at));
    pInit(ATTR_SECURITY, &at.a, sizeof(at), &req);
    sec = pGet(&at.a, ATTR_SECURITY);
    sec->ContextHandle = pr.ctx_handle;
    at.a.ValidAttributes = ATTR_SECURITY;
    t.QuadPart = -20000000;
    pr.connect_status = pConnect(&port, &us, NULL, &pa, MSGFLG_SYNC, NULL, &m.h, &len,
                                 pr.sec_attr ? &at.a : NULL, NULL, &t);
    if (port) CloseHandle(port);
    return 0;
}

static void probe_config(const char *case_name, const WCHAR *name, ULONG port_flags, int port_qos,
                         ULONG client_flags, int client_qos, int sec_attr, HANDLE ctx_handle, int wait_first)
{
    UNICODE_STRING us;
    OBJECT_ATTRIBUTES oa;
    ALPC_PORT_ATTRIBUTES_ pa;
    HANDLE conn = NULL, thread, comm = NULL;
    MSG_ m, reply;
    LONG st;

    oracle_case(case_name);
    memset(&pr, 0, sizeof(pr));
    pr.name = name;
    pr.port_flags = port_flags;
    pr.client_flags = client_flags;
    pr.port_qos = port_qos;
    pr.client_qos = client_qos;
    pr.sec_attr = sec_attr;
    pr.ctx_handle = ctx_handle;
    init_name(&us, name);
    memset(&oa, 0, sizeof(oa));
    oa.Length = sizeof(oa);
    oa.ObjectName = &us;
    init_attrs_for(&pa, port_flags, port_qos);
    st = pCreate(&conn, &oa, &pa);
    o_status("create", st);
    if (st) { oracle_case_end(); return; }
    thread = CreateThread(NULL, 0, probe_client, NULL, 0, NULL);
    if (wait_first) o_u32("wait", WaitForSingleObject(conn, 3000));
    else Sleep(500);
    st = server_receive_with(conn, &m, ATTR_CONTEXT, 0, sizeof(m.h) + 0x2000);
    o_status("receive", st);
    if (!st) {
        o_hex32("type", (USHORT)m.h.u2.s2.Type);
        init_msg(&reply, "x");
        reply.h.MessageId = m.h.MessageId;
        reply.h.ClientId = m.h.ClientId;
        st = pAccept(&comm, conn, 0, NULL, NULL, NULL, &reply.h, NULL, TRUE);
        o_status("accept", st);
    }
    WaitForSingleObject(thread, 5000);
    CloseHandle(thread);
    o_status("connect", pr.connect_status);
    if (comm) CloseHandle(comm);
    CloseHandle(conn);
    oracle_case_end();
}

static BYTE process_user[256];

static void print_thread_token(void)
{
    HANDLE token = NULL;
    BOOL ok;
    DWORD len, err;
    TOKEN_TYPE type;
    SECURITY_IMPERSONATION_LEVEL level;
    BYTE ubuf[256];

    ok = OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &token);
    err = ok ? 0 : GetLastError();
    o_bool("open_thread_token", ok);
    o_u32("open_thread_token_error", err);
    if (!ok) return;
    if (GetTokenInformation(token, TokenType, &type, sizeof(type), &len)) o_u32("token_type", type);
    if (GetTokenInformation(token, TokenImpersonationLevel, &level, sizeof(level), &len))
        o_u32("token_level", level);
    if (GetTokenInformation(token, TokenUser, ubuf, sizeof(ubuf), &len))
        o_bool("token_user_is_process_user",
               EqualSid(((TOKEN_USER *)ubuf)->User.Sid, ((TOKEN_USER *)process_user)->User.Sid));
    CloseHandle(token);
}

int main(void)
{
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    UNICODE_STRING us;
    OBJECT_ATTRIBUTES oa;
    ALPC_PORT_ATTRIBUTES_ pa;
    HANDLE conn = NULL, comm = NULL, thread, port;
    MSG_ m, reply;
    LONG st;
    BOOL reverted;

    oracle_begin("ntdll/alpc_security");
    {
        HANDLE ptoken;
        DWORD len;
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &ptoken)) {
            GetTokenInformation(ptoken, TokenUser, process_user, sizeof(process_user), &len);
            CloseHandle(ptoken);
        }
    }

    pCreate = (create_fn)(void (*)(void))GetProcAddress(ntdll, "NtAlpcCreatePort");
    pConnect = (connect_fn)(void (*)(void))GetProcAddress(ntdll, "NtAlpcConnectPort");
    pAccept = (accept_fn)(void (*)(void))GetProcAddress(ntdll, "NtAlpcAcceptConnectPort");
    pSwr = (swr_fn)(void (*)(void))GetProcAddress(ntdll, "NtAlpcSendWaitReceivePort");
    pImpersonate = (impersonate_fn)(void (*)(void))GetProcAddress(ntdll, "NtAlpcImpersonateClientOfPort");
    pInit = (init_fn)(void (*)(void))GetProcAddress(ntdll, "AlpcInitializeMessageAttribute");
    pGet = (get_fn)(void (*)(void))GetProcAddress(ntdll, "AlpcGetMessageAttribute");
    if (!pCreate || !pConnect || !pAccept || !pSwr || !pImpersonate || !pInit || !pGet) {
        oracle_case("exports");
        o_bool("all", FALSE);
        oracle_case_end();
        return oracle_end();
    }

    /* where an unprivileged process may name a port */
    oracle_case("base_named_objects");
    init_name(&us, L"\\BaseNamedObjects\\SetuOracleAlpcBno");
    memset(&oa, 0, sizeof(oa));
    oa.Length = sizeof(oa);
    oa.ObjectName = &us;
    init_port_attrs(&pa);
    port = NULL;
    st = pCreate(&port, &oa, &pa);
    o_status("create_global", st);
    if (!st) CloseHandle(port);
    oracle_case_end();

    probe_config("probe_plain", L"\\RPC Control\\SetuOracleAlpcP1", 0, 0, 0, 0, 0, NULL, 0);
    probe_config("probe_qos_ports", L"\\RPC Control\\SetuOracleAlpcP2", 0x70000, 1, 0x70000, 1, 0, NULL, 0);
    probe_config("probe_qos_ports_sec", L"\\RPC Control\\SetuOracleAlpcP3", 0x70000, 1, 0x70000, 1, 1,
                 (HANDLE)(LONG_PTR)-2, 0);
    probe_config("probe_qos_ports_sec_ctx0", L"\\RPC Control\\SetuOracleAlpcP4", 0x70000, 1, 0x70000, 1, 1, NULL, 0);
    probe_config("probe_server_qos_only", L"\\RPC Control\\SetuOracleAlpcP5", 0x70000, 1, 0, 0, 0, NULL, 0);
    probe_config("probe_client_qos_only", L"\\RPC Control\\SetuOracleAlpcP6", 0, 0, 0x70000, 1, 0, NULL, 0);
    probe_config("probe_client_impersonation_flag", L"\\RPC Control\\SetuOracleAlpcP7", 0, 0, 0x10000, 1, 0, NULL, 0);
    probe_config("probe_client_waitable_flag", L"\\RPC Control\\SetuOracleAlpcP8", 0, 0, 0x40000, 1, 0, NULL, 0);
    probe_config("probe_client_lpc_flag", L"\\RPC Control\\SetuOracleAlpcP9", 0, 0, 0x20000, 1, 0, NULL, 0);
    probe_config("probe_client_flags_noqos", L"\\RPC Control\\SetuOracleAlpcPa", 0, 0, 0x70000, 0, 0, NULL, 0);
    probe_config("probe_waitable", L"\\RPC Control\\SetuOracleAlpcPc", 0x40000, 0, 0, 0, 0, NULL, 0);
    probe_config("probe_waitable_wait_first", L"\\RPC Control\\SetuOracleAlpcPd", 0x40000, 0, 0, 0, 0, NULL, 1);
    probe_config("probe_impersonation", L"\\RPC Control\\SetuOracleAlpcPe", 0x10000, 1, 0, 0, 0, NULL, 0);
    probe_config("probe_firefox_wait_first", L"\\RPC Control\\SetuOracleAlpcPf", 0x70000, 1, 0x70000, 1, 1,
                 (HANDLE)(LONG_PTR)-2, 1);
    probe_config("probe_sec_only", L"\\RPC Control\\SetuOracleAlpcPb", 0, 0, 0, 0, 1, (HANDLE)(LONG_PTR)-2, 0);

    init_name(&us, port_name);
    memset(&oa, 0, sizeof(oa));
    oa.Length = sizeof(oa);
    oa.ObjectName = &us;
    init_port_attrs(&pa);
    oracle_case("create");
    st = pCreate(&conn, &oa, &pa);
    o_status("status", st);
    oracle_case_end();
    if (st) return oracle_end();

    /* a waitable port does not block a receive */
    oracle_case("receive_empty_waitable");
    {
        DWORD start = GetTickCount();
        st = server_receive_with(conn, &m, ATTR_CONTEXT, 0, 0x20f0);
        o_status("status", st);
        o_bool("returned_at_once", GetTickCount() - start < 1500);
    }
    oracle_case_end();

    cl.go = CreateEventW(NULL, FALSE, FALSE, NULL);
    cl.done = CreateEventW(NULL, TRUE, FALSE, NULL);
    thread = CreateThread(NULL, 0, client_thread, NULL, 0, NULL);

    oracle_case("connection_request");
    st = server_receive(conn, &m);
    o_status("receive", st);
    if (!st) {
        o_hex32("type", (USHORT)m.h.u2.s2.Type);
        print_received_attrs();
        st = pImpersonate(conn, &m.h, NULL);
        o_status("impersonate", st);
        if (!st) print_thread_token();
        reverted = RevertToSelf();
        o_bool("revert", reverted);
        init_msg(&reply, "welcome");
        reply.h.MessageId = m.h.MessageId;
        reply.h.ClientId = m.h.ClientId;
        init_port_attrs(&pa);
        pa.Flags = 0;
        st = pAccept(&comm, conn, 0, NULL, &pa, (void *)0x1234, &reply.h, NULL, TRUE);
        o_status("accept", st);
    }
    oracle_case_end();

    if (comm) {
        SetEvent(cl.go);
        oracle_case("request");
        st = server_receive(conn, &m);
        o_status("receive", st);
        if (!st) {
            o_hex32("type", (USHORT)m.h.u2.s2.Type);
            print_received_attrs();
            st = pImpersonate(comm, &m.h, NULL);
            o_status("impersonate", st);
            if (!st) print_thread_token();
            o_bool("revert", RevertToSelf());
            init_msg(&reply, "answer");
            reply.h.MessageId = m.h.MessageId;
            reply.h.CallbackId = m.h.CallbackId;
            reply.h.ClientId = m.h.ClientId;
            st = pSwr(comm, MSGFLG_REPLY, &reply.h, NULL, NULL, NULL, NULL, NULL);
            o_status("reply", st);
        }
        oracle_case_end();
    }

    /* answer a request by sending through the connection port, with the
     * request's message id and the context attribute it came with, the way
     * CoreMessagingXP does */
    if (comm) {
        static const struct { const char *name; ULONG flags; int attrs; } ways[] = {
            { "via_conn_release_attrs", 0x10000, 1 },
            { "via_conn_release", 0x10000, 0 },
            { "via_conn_plain_attrs", 0, 1 },
            { "via_comm_release", 0x10000, 2 },
        };
        unsigned i;
        LARGE_INTEGER t0;

        for (i = 0; i < sizeof(ways) / sizeof(ways[0]); i++) {
            oracle_case(ways[i].name);
            ResetEvent(cl.done);
            SetEvent(cl.go);
            st = server_receive(conn, &m);
            o_status("receive", st);
            if (!st) {
                init_msg(&reply, "answer");
                reply.h.MessageId = m.h.MessageId;
                reply.h.CallbackId = m.h.CallbackId;
                st = pSwr(ways[i].attrs == 2 ? comm : conn, ways[i].flags, &reply.h,
                          ways[i].attrs == 1 ? &sv_attrs.a : NULL, NULL, NULL, NULL, NULL);
                o_status("send", st);
            }
            o_u32("client_done", WaitForSingleObject(cl.done, 4000));
            o_status("client_status", cl.more_status);
            if (!cl.more_status) {
                o_u64("client_reply_len", cl.reply_len);
                o_hex32("client_reply_type", (USHORT)cl.reply.h.u2.s2.Type);
                o_bool("client_reply_same_id", cl.reply.h.MessageId == m.h.MessageId);
            }
            /* did it land in the connection port's own queue instead? */
            {
                SIZE_T len = sizeof(reply);
                t0.QuadPart = 0;
                st = pSwr(conn, 0, NULL, NULL, &reply.h, &len, NULL, &t0);
                o_status("conn_queue", st);
                if (!st) o_hex32("conn_queue_type", (USHORT)reply.h.u2.s2.Type);
            }
            oracle_case_end();
        }
        cl.stop = 1;
        SetEvent(cl.go);
    }

    WaitForSingleObject(thread, 15000);
    oracle_case("client");
    o_status("connect", cl.connect_status);
    o_status("request", cl.request_status);
    if (!cl.request_status) o_u64("reply_len", cl.reply_len);
    o_hex32("send_attrs_valid_after", cl_attrs.a.ValidAttributes);
    oracle_case_end();

    if (cl.port) CloseHandle(cl.port);
    if (comm) CloseHandle(comm);
    CloseHandle(conn);
    return oracle_end();
}
