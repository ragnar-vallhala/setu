/*
 * ALPC (advanced local procedure call) ports
 *
 * Copyright 2026 The Setu authors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

/*
 * The ports and their messages live in the server (server/alpc.c). The
 * behaviour follows the oracle test ntdll/alpc_port (Windows 11).
 */

#if 0
#pragma makedep unix
#endif

#include "config.h"

#include <stdarg.h>
#include <string.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winternl.h"
#include "unix_private.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(alpc);

#define HEADER_SIZE ((ULONG)sizeof(PORT_MESSAGE))

/* the attribute bodies follow the ALPC_MESSAGE_ATTRIBUTES header, from the
 * highest flag down; each one has this size */
static ULONG attribute_size( ULONG flag )
{
    switch (flag)
    {
    case ALPC_MESSAGE_SECURITY_ATTRIBUTE: return 3 * sizeof(void *);
    case ALPC_MESSAGE_VIEW_ATTRIBUTE: return 4 * sizeof(void *);
    case ALPC_MESSAGE_CONTEXT_ATTRIBUTE: return sizeof(ALPC_CONTEXT_ATTR);
    case ALPC_MESSAGE_HANDLE_ATTRIBUTE: return 2 * sizeof(void *) + 8;
    case ALPC_MESSAGE_TOKEN_ATTRIBUTE: return 24;
    case ALPC_MESSAGE_DIRECT_ATTRIBUTE: return sizeof(void *);
    case ALPC_MESSAGE_WORK_ON_BEHALF_ATTRIBUTE: return 8;
    }
    return 0;
}

static void *get_attribute( ALPC_MESSAGE_ATTRIBUTES *attrs, ULONG flag )
{
    ULONG bit, offset = sizeof(*attrs);

    if (!(attrs->AllocatedAttributes & flag)) return NULL;
    for (bit = 0x80000000; bit > flag; bit >>= 1)
        if (attrs->AllocatedAttributes & bit) offset += attribute_size( bit );
    return (char *)attrs + offset;
}

static void fill_header( PORT_MESSAGE *msg, unsigned int type, data_size_t len, process_id_t pid,
                         thread_id_t tid, unsigned int id, unsigned int callback_id )
{
    msg->DataSize = len;
    msg->MessageSize = len + HEADER_SIZE;
    msg->MessageType = type;
    msg->VirtualRangesOffset = 0;
    msg->ClientId.UniqueProcess = ULongToHandle( pid );
    msg->ClientId.UniqueThread = ULongToHandle( tid );
    msg->MessageId = id;
    msg->SectionSize = callback_id;
}

/* convert a relative timeout to an absolute one, so that it survives retries */
static LARGE_INTEGER *absolute_timeout( const LARGE_INTEGER *timeout, LARGE_INTEGER *abs )
{
    LARGE_INTEGER now;

    if (!timeout) return NULL;
    if (timeout->QuadPart >= 0)
    {
        *abs = *timeout;
        return abs;
    }
    NtQuerySystemTime( &now );
    abs->QuadPart = now.QuadPart - timeout->QuadPart;
    return abs;
}

/* the room for message data in a receive buffer */
static data_size_t receive_room( const SIZE_T *len )
{
    if (!len) return 0xffff;
    if (*len < HEADER_SIZE) return 0;
    return min( *len - HEADER_SIZE, 0xffff );
}

static NTSTATUS wait_request( HANDLE wait, const LARGE_INTEGER *timeout, BOOL alertable )
{
    NTSTATUS status = server_wait_for_object( wait, alertable, timeout );

    if (status == STATUS_SUCCESS) return status;
    /* timed out or interrupted: the request no longer has a waiter */
    SERVER_START_REQ( alpc_cancel )
    {
        req->wait = wine_server_obj_handle( wait );
        wine_server_call( req );
    }
    SERVER_END_REQ;
    return status;
}

/* fetch the reply to a request we waited for */
static NTSTATUS get_reply( HANDLE wait, PORT_MESSAGE *msg, SIZE_T *len )
{
    NTSTATUS status;
    data_size_t room = msg ? receive_room( len ) : 0;

    SERVER_START_REQ( alpc_get_reply )
    {
        req->wait = wine_server_obj_handle( wait );
        req->size = room;
        if (msg) wine_server_set_reply( req, msg + 1, room );
        status = wine_server_call( req );
        if (!status && msg)
        {
            fill_header( msg, reply->type, reply->total, reply->pid, reply->tid, reply->id, reply->callback_id );
            if (len) *len = reply->total + HEADER_SIZE;
        }
        else if (status == STATUS_BUFFER_TOO_SMALL && len) *len = reply->total + HEADER_SIZE;
    }
    SERVER_END_REQ;
    return status;
}

static void clear_attributes( ALPC_MESSAGE_ATTRIBUTES *attrs )
{
    if (attrs) attrs->ValidAttributes = 0;
}

static NTSTATUS connect_port( HANDLE *handle, OBJECT_ATTRIBUTES *attr, ALPC_PORT_ATTRIBUTES *port_attr,
                              ULONG flags, PORT_MESSAGE *msg, SIZE_T *len,
                              ALPC_MESSAGE_ATTRIBUTES *out_attrs, ALPC_MESSAGE_ATTRIBUTES *in_attrs,
                              LARGE_INTEGER *timeout )
{
    struct object_attributes *objattr;
    data_size_t attr_len;
    HANDLE port = 0, wait = 0;
    BOOL security = FALSE;
    LARGE_INTEGER abs;
    NTSTATUS status;

    if (port_attr)
        TRACE( "port attrs flags %#x qos len %u level %u tracking %u effective %u max %#lx\n", (int)port_attr->Flags,
               (int)port_attr->SecurityQos.Length, port_attr->SecurityQos.ImpersonationLevel,
               port_attr->SecurityQos.ContextTrackingMode, port_attr->SecurityQos.EffectiveOnly,
               (long)port_attr->MaxMessageLength );
    if (out_attrs)
    {
        ULONG *sec = get_attribute( out_attrs, ALPC_MESSAGE_SECURITY_ATTRIBUTE );
        TRACE( "send attrs %#x valid %#x\n", (int)out_attrs->AllocatedAttributes, (int)out_attrs->ValidAttributes );
        if (sec) TRACE( "security attr flags %#x qos %p ctx %p\n", (int)sec[0], ((void **)sec)[1], ((void **)sec)[2] );
    }
    if (!handle || !attr) return STATUS_INVALID_PARAMETER;
    *handle = 0;
    if (out_attrs && (out_attrs->ValidAttributes & ALPC_MESSAGE_SECURITY_ATTRIBUTE))
    {
        ALPC_SECURITY_ATTR *sec = get_attribute( out_attrs, ALPC_MESSAGE_SECURITY_ATTRIBUTE );
        if (sec && !sec->ContextHandle) return STATUS_INVALID_HANDLE;
        security = TRUE;
    }
    if (msg && (msg->MessageSize < HEADER_SIZE || msg->MessageSize != msg->DataSize + HEADER_SIZE))
        return STATUS_INVALID_PARAMETER;
    if ((status = alloc_object_attributes( attr, &objattr, &attr_len ))) return status;

    SERVER_START_REQ( alpc_connect )
    {
        req->flags   = port_attr ? port_attr->Flags : 0;
        req->max_len = port_attr ? min( port_attr->MaxMessageLength, 0xffff ) : 0;
        req->qos_level = port_attr && port_attr->SecurityQos.Length ? port_attr->SecurityQos.ImpersonationLevel : ~0u;
        req->security = security;
        wine_server_add_data( req, objattr, attr_len );
        if (msg) wine_server_add_data( req, msg + 1, msg->DataSize );
        status = wine_server_call( req );
        port = wine_server_ptr_handle( reply->handle );
        wait = wine_server_ptr_handle( reply->wait );
    }
    SERVER_END_REQ;
    free( objattr );

    if (!status)
    {
        status = wait_request( wait, absolute_timeout( timeout, &abs ), FALSE );
        if (!status) status = get_reply( wait, msg, len );
    }
    if (wait) NtClose( wait );
    if (status)
    {
        if (port) NtClose( port );
        return status;
    }
    clear_attributes( in_attrs );
    *handle = port;
    TRACE( "-> %p\n", port );
    return STATUS_SUCCESS;
}


/***********************************************************************
 *             NtAlpcCreatePort  (NTDLL.@)
 */
NTSTATUS WINAPI NtAlpcCreatePort( HANDLE *handle, OBJECT_ATTRIBUTES *attr, ALPC_PORT_ATTRIBUTES *port_attr )
{
    struct object_attributes *objattr;
    data_size_t len;
    NTSTATUS status;

    TRACE( "(%p, %s, %p) flags %#x max %#lx\n", handle, debugstr_us( attr ? attr->ObjectName : NULL ), port_attr,
           port_attr ? (int)port_attr->Flags : 0, port_attr ? (long)port_attr->MaxMessageLength : 0 );

    if (port_attr)
        TRACE( "qos len %u level %u tracking %u effective %u\n", (int)port_attr->SecurityQos.Length,
               port_attr->SecurityQos.ImpersonationLevel, port_attr->SecurityQos.ContextTrackingMode,
               port_attr->SecurityQos.EffectiveOnly );
    *handle = 0;
    if (port_attr && port_attr->MaxMessageLength > 0xffff) return STATUS_INVALID_PARAMETER;
    if ((status = alloc_object_attributes( attr, &objattr, &len ))) return status;

    SERVER_START_REQ( create_alpc_port )
    {
        req->flags   = port_attr ? port_attr->Flags : 0;
        req->max_len = port_attr ? port_attr->MaxMessageLength : 0;
        wine_server_add_data( req, objattr, len );
        status = wine_server_call( req );
        *handle = wine_server_ptr_handle( reply->handle );
    }
    SERVER_END_REQ;

    free( objattr );
    TRACE( "-> %#x %p\n", (int)status, *handle );
    return status;
}


/***********************************************************************
 *             NtAlpcConnectPort  (NTDLL.@)
 */
NTSTATUS WINAPI NtAlpcConnectPort( HANDLE *handle, UNICODE_STRING *name, OBJECT_ATTRIBUTES *attr,
                                   ALPC_PORT_ATTRIBUTES *port_attr, ULONG flags, PSID sid,
                                   PORT_MESSAGE *msg, SIZE_T *len, ALPC_MESSAGE_ATTRIBUTES *out_attrs,
                                   ALPC_MESSAGE_ATTRIBUTES *in_attrs, LARGE_INTEGER *timeout )
{
    OBJECT_ATTRIBUTES conn_attr;

    TRACE( "(%p, %s, %p, %p, %#x, %p, %p, %p, %p, %p, %p)\n", handle, debugstr_us( name ), attr, port_attr,
           (int)flags, sid, msg, len, out_attrs, in_attrs, timeout );

    if (!name) return STATUS_INVALID_PARAMETER;
    InitializeObjectAttributes( &conn_attr, name, OBJ_CASE_INSENSITIVE, 0, NULL );
    return connect_port( handle, &conn_attr, port_attr, flags, msg, len, out_attrs, in_attrs, timeout );
}


/***********************************************************************
 *             NtAlpcConnectPortEx  (NTDLL.@)
 */
NTSTATUS WINAPI NtAlpcConnectPortEx( HANDLE *handle, OBJECT_ATTRIBUTES *conn_attr, OBJECT_ATTRIBUTES *client_attr,
                                     ALPC_PORT_ATTRIBUTES *port_attr, ULONG flags, PSECURITY_DESCRIPTOR sd,
                                     PORT_MESSAGE *msg, SIZE_T *len, ALPC_MESSAGE_ATTRIBUTES *out_attrs,
                                     ALPC_MESSAGE_ATTRIBUTES *in_attrs, LARGE_INTEGER *timeout )
{
    TRACE( "(%p, %s, %p, %p, %#x, %p, %p, %p, %p, %p, %p)\n", handle,
           debugstr_us( conn_attr ? conn_attr->ObjectName : NULL ), client_attr, port_attr,
           (int)flags, sd, msg, len, out_attrs, in_attrs, timeout );

    return connect_port( handle, conn_attr, port_attr, flags, msg, len, out_attrs, in_attrs, timeout );
}


/***********************************************************************
 *             NtAlpcAcceptConnectPort  (NTDLL.@)
 */
NTSTATUS WINAPI NtAlpcAcceptConnectPort( HANDLE *handle, HANDLE conn, ULONG flags, OBJECT_ATTRIBUTES *attr,
                                         ALPC_PORT_ATTRIBUTES *port_attr, void *context, PORT_MESSAGE *msg,
                                         ALPC_MESSAGE_ATTRIBUTES *msg_attrs, BOOLEAN accept )
{
    NTSTATUS status;
    HANDLE port = 0;

    TRACE( "(%p, %p, %#x, %p, %p, %p, %p, %p, %d)\n", handle, conn, (int)flags, attr, port_attr, context,
           msg, msg_attrs, accept );

    if (!msg) return STATUS_INVALID_PARAMETER;
    if (msg->MessageSize < HEADER_SIZE || msg->MessageSize != msg->DataSize + HEADER_SIZE)
        return STATUS_INVALID_PARAMETER;
    if (port_attr) TRACE( "port attrs flags %#x max %#lx\n", (int)port_attr->Flags, (long)port_attr->MaxMessageLength );

    SERVER_START_REQ( alpc_accept )
    {
        req->handle  = wine_server_obj_handle( conn );
        req->context = wine_server_client_ptr( context );
        req->id      = msg->MessageId;
        req->flags   = port_attr ? port_attr->Flags : 0;
        req->max_len = port_attr ? min( port_attr->MaxMessageLength, 0xffff ) : 0;
        req->accept  = accept;
        wine_server_add_data( req, msg + 1, msg->DataSize );
        status = wine_server_call( req );
        port = wine_server_ptr_handle( reply->handle );
    }
    SERVER_END_REQ;

    if (!status && accept) *handle = port;
    TRACE( "-> %#x %p\n", (int)status, port );
    return status;
}


/***********************************************************************
 *             NtAlpcSendWaitReceivePort  (NTDLL.@)
 */
NTSTATUS WINAPI NtAlpcSendWaitReceivePort( HANDLE handle, ULONG flags, PORT_MESSAGE *send,
                                           ALPC_MESSAGE_ATTRIBUTES *send_attrs, PORT_MESSAGE *recv,
                                           SIZE_T *len, ALPC_MESSAGE_ATTRIBUTES *recv_attrs,
                                           LARGE_INTEGER *timeout )
{
    BOOL alertable = !!(flags & ALPC_MSGFLG_WAIT_ALERTABLE);
    LARGE_INTEGER abs, *deadline = absolute_timeout( timeout, &abs );
    HANDLE wait = 0;
    NTSTATUS status;

    TRACE( "(%p, %#x, %p, %p, %p, %p(%#lx), %p, %s)\n", handle, (int)flags, send, send_attrs, recv, len,
           len ? (long)*len : 0, recv_attrs, timeout ? wine_dbgstr_longlong( timeout->QuadPart ) : "inf" );

    if (send)
    {
        if (send->MessageSize < HEADER_SIZE || send->MessageSize != send->DataSize + HEADER_SIZE)
            return STATUS_INVALID_PARAMETER;

        TRACE( "send id %#x size %u attrs %#x\n", (int)send->MessageId, send->DataSize,
               send_attrs ? (int)send_attrs->ValidAttributes : 0 );
        SERVER_START_REQ( alpc_send )
        {
            req->handle = wine_server_obj_handle( handle );
            req->flags  = flags;
            req->id     = send->MessageId;
            req->total  = send->MessageSize;
            req->security = send_attrs && (send_attrs->ValidAttributes & ALPC_MESSAGE_SECURITY_ATTRIBUTE);
            wine_server_add_data( req, send + 1, send->DataSize );
            status = wine_server_call( req );
            wait = wine_server_ptr_handle( reply->wait );
        }
        SERVER_END_REQ;
        if (status) goto done;

        if (wait)
        {
            /* a synchronous request: the reply is what we receive */
            if (!(status = wait_request( wait, deadline, alertable )))
                status = get_reply( wait, recv, len );
            NtClose( wait );
            if (!status) clear_attributes( recv_attrs );
            goto done;
        }
    }

    if (!recv) return STATUS_SUCCESS;

    for (;;)
    {
        data_size_t room = receive_room( len );
        ALPC_CONTEXT_ATTR *ctx;
        ALPC_SECURITY_ATTR *sec;

        SERVER_START_REQ( alpc_receive )
        {
            req->handle = wine_server_obj_handle( handle );
            req->size   = room;
            wine_server_set_reply( req, recv + 1, room );
            status = wine_server_call( req );
            if (!status)
            {
                fill_header( recv, reply->type, reply->total, reply->pid, reply->tid, reply->id,
                             reply->callback_id );
                if (recv_attrs)
                {
                    recv_attrs->ValidAttributes = 0;
                    if (reply->has_context && (ctx = get_attribute( recv_attrs, ALPC_MESSAGE_CONTEXT_ATTRIBUTE )))
                    {
                        ctx->PortContext    = wine_server_get_ptr( reply->context );
                        ctx->MessageContext = NULL;
                        ctx->Sequence       = reply->sequence;
                        ctx->MessageId      = reply->id;
                        ctx->CallbackId     = reply->callback_id;
                        recv_attrs->ValidAttributes |= ALPC_MESSAGE_CONTEXT_ATTRIBUTE;
                    }
                    if (reply->has_security && (sec = get_attribute( recv_attrs, ALPC_MESSAGE_SECURITY_ATTRIBUTE )))
                    {
                        /* the context handle is opaque; nothing here uses it */
                        sec->Flags = 0;
                        sec->QoS = NULL;
                        sec->ContextHandle = ULongToHandle( reply->id );
                        recv_attrs->ValidAttributes |= ALPC_MESSAGE_SECURITY_ATTRIBUTE;
                    }
                }
            }
            else if (status == STATUS_BUFFER_TOO_SMALL && len) *len = reply->total + HEADER_SIZE;
            wait = wine_server_ptr_handle( reply->wait );
        }
        SERVER_END_REQ;

        if (status != STATUS_PENDING) break;
        if (deadline && !deadline->QuadPart) status = STATUS_TIMEOUT;
        else if (timeout && !timeout->QuadPart) status = STATUS_TIMEOUT;
        else status = server_wait_for_object( wait, alertable, deadline );
        NtClose( wait );
        if (status) break;
    }

done:
    TRACE( "-> %#x\n", (int)status );
    if (!status && recv)
        TRACE( "received type %#x id %#x size %u from %04x\n", recv->MessageType, (int)recv->MessageId,
               recv->DataSize, HandleToULong( recv->ClientId.UniqueThread ));
    return status;
}


/***********************************************************************
 *             NtAlpcDisconnectPort  (NTDLL.@)
 */
NTSTATUS WINAPI NtAlpcDisconnectPort( HANDLE handle, ULONG flags )
{
    NTSTATUS status;

    TRACE( "(%p, %#x)\n", handle, (int)flags );

    SERVER_START_REQ( alpc_disconnect )
    {
        req->handle = wine_server_obj_handle( handle );
        status = wine_server_call( req );
    }
    SERVER_END_REQ;
    return status;
}


/***********************************************************************
 *             NtAlpcQueryInformation  (NTDLL.@)
 */
NTSTATUS WINAPI NtAlpcQueryInformation( HANDLE handle, ALPC_PORT_INFORMATION_CLASS class, void *info,
                                        ULONG len, ULONG *ret_len )
{
    ALPC_BASIC_INFORMATION *basic = info;
    NTSTATUS status;

    TRACE( "(%p, %u, %p, %u, %p)\n", handle, class, info, (int)len, ret_len );

    switch (class)
    {
    case AlpcBasicInformation:
        if (ret_len) *ret_len = sizeof(*basic);
        if (len < sizeof(*basic)) return STATUS_INFO_LENGTH_MISMATCH;
        SERVER_START_REQ( alpc_query )
        {
            req->handle = wine_server_obj_handle( handle );
            req->id = 0;
            if (!(status = wine_server_call( req )))
            {
                basic->Flags = reply->flags;
                basic->SequenceNo = 0;
                basic->PortContext = wine_server_get_ptr( reply->context );
            }
        }
        SERVER_END_REQ;
        return status;
    default:
        FIXME( "class %u not implemented\n", class );
        return STATUS_INVALID_INFO_CLASS;
    }
}


/***********************************************************************
 *             NtAlpcSetInformation  (NTDLL.@)
 */
NTSTATUS WINAPI NtAlpcSetInformation( HANDLE handle, ALPC_PORT_INFORMATION_CLASS class, void *info, ULONG len )
{
    FIXME( "(%p, %u, %p, %u) stub\n", handle, class, info, (int)len );
    return STATUS_NOT_IMPLEMENTED;
}


/***********************************************************************
 *             NtAlpcImpersonateClientOfPort  (NTDLL.@)
 */
NTSTATUS WINAPI NtAlpcImpersonateClientOfPort( HANDLE handle, PORT_MESSAGE *msg, void *flags )
{
    SECURITY_QUALITY_OF_SERVICE qos;
    OBJECT_ATTRIBUTES attr;
    CLIENT_ID cid;
    HANDLE process, token, dup;
    unsigned int level = SecurityImpersonation;
    process_id_t pid = 0;
    NTSTATUS status;

    TRACE( "(%p, %p, %p)\n", handle, msg, flags );

    if (!msg) return STATUS_INVALID_PARAMETER;
    /* the message must still wait for its reply */
    SERVER_START_REQ( alpc_query )
    {
        req->handle = wine_server_obj_handle( handle );
        req->id = msg->MessageId;
        if (!(status = wine_server_call( req )))
        {
            pid = reply->pid;
            level = reply->qos_level;
        }
    }
    SERVER_END_REQ;
    if (status) return status;
    /* no QoS, or a port that does not allow it: the call succeeds, and the
     * thread is left without a token */
    if (level == ~0u) return STATUS_SUCCESS;

    /* the thread gets an impersonation token of the client's process, at
     * the level the client allowed */
    InitializeObjectAttributes( &attr, NULL, 0, 0, NULL );
    cid.UniqueProcess = ULongToHandle( pid );
    cid.UniqueThread = 0;
    if ((status = NtOpenProcess( &process, PROCESS_QUERY_LIMITED_INFORMATION, &attr, &cid ))) return status;
    status = NtOpenProcessToken( process, TOKEN_DUPLICATE | TOKEN_QUERY, &token );
    NtClose( process );
    if (status) return status;
    qos.Length = sizeof(qos);
    qos.ImpersonationLevel = level;
    qos.ContextTrackingMode = SECURITY_STATIC_TRACKING;
    qos.EffectiveOnly = FALSE;
    attr.SecurityQualityOfService = &qos;
    status = NtDuplicateToken( token, TOKEN_IMPERSONATE | TOKEN_QUERY, &attr, FALSE, TokenImpersonation, &dup );
    NtClose( token );
    if (status) return status;
    status = NtSetInformationThread( GetCurrentThread(), ThreadImpersonationToken, &dup, sizeof(dup) );
    NtClose( dup );
    TRACE( "-> %#x level %u\n", (int)status, level );
    return status;
}
