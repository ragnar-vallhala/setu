/*
 * Server-side ALPC (advanced local procedure call) ports
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
 * Three kinds of port:
 *
 *  - a connection port, made by NtAlpcCreatePort, named or not. Connection
 *    requests and every message a client sends arrive in its queue. A message
 *    sent through it goes to its own queue.
 *  - a client communication port, made by NtAlpcConnectPort. Replies to
 *    asynchronous requests and messages from the server arrive in its queue.
 *  - a server communication port, made by NtAlpcAcceptConnectPort. It has no
 *    queue of its own; it carries the port context and sends to the client.
 *
 * A message that waits for a reply (a request, a connection request) moves
 * from the queue to the receiving port's pending list when it is received,
 * and leaves it with the reply. The sender of a synchronous request waits on
 * the message object itself, which is signaled once the reply is there.
 *
 * The behaviour follows the oracle test ntdll/alpc_port (Windows 11).
 */

#include "config.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winternl.h"

#include "handle.h"
#include "thread.h"
#include "process.h"
#include "request.h"
#include "security.h"

#define PORT_CONNECT    0x0001
#define PORT_ALL_ACCESS (STANDARD_RIGHTS_REQUIRED | SYNCHRONIZE | PORT_CONNECT)

#define ALPC_MAX_MESSAGE_LENGTH 0xffff
#define ALPC_NO_QOS             ~0u   /* the client gave no security QoS */

/* message types, as the receiver sees them */
#define TYPE_REQUEST            (LPC_CONTINUATION_REQUIRED | LPC_REQUEST)
#define TYPE_DATAGRAM           LPC_DATAGRAM
#define TYPE_PORT_CLOSED        LPC_PORT_CLOSED
#define TYPE_CONNECTION_REQUEST (LPC_CONTINUATION_REQUIRED | LPC_CONNECTION_REQUEST)
#define TYPE_CONNECTION_REPLY   11
#define TYPE_CANCELED           12

static const WCHAR alpc_port_name[] = {'A','L','P','C',' ','P','o','r','t'};

struct type_descr alpc_port_type =
{
    { alpc_port_name, sizeof(alpc_port_name) },   /* name */
    PORT_ALL_ACCESS,                              /* valid_access */
    {                                             /* mapping */
        STANDARD_RIGHTS_READ | PORT_CONNECT,
        STANDARD_RIGHTS_WRITE | PORT_CONNECT,
        STANDARD_RIGHTS_EXECUTE | SYNCHRONIZE,
        PORT_ALL_ACCESS
    },
};

enum alpc_port_kind
{
    ALPC_CONNECTION_PORT,
    ALPC_CLIENT_PORT,
    ALPC_SERVER_PORT,
};

enum alpc_message_state
{
    MSG_QUEUED,      /* in a port's queue */
    MSG_PENDING,     /* received, waiting for its reply */
    MSG_REPLIED,     /* the reply is in the message */
    MSG_REFUSED,     /* connection refused */
    MSG_ABORTED,     /* the other side went away */
    MSG_CANCELED,    /* the sender stopped waiting */
    MSG_DONE,        /* received, no reply expected */
};

struct alpc_port;

struct alpc_message
{
    struct object            obj;          /* object header (the sender waits on it) */
    struct list              entry;        /* entry in a queue or pending list */
    struct list             *list;         /* the list it is on, if any */
    enum alpc_message_state  state;
    unsigned int             type;
    unsigned int             id;
    unsigned int             callback_id;
    unsigned int             sequence;
    process_id_t             pid;
    thread_id_t              tid;
    client_ptr_t             context;      /* port context of the server port it came through */
    int                      has_context;
    unsigned int             conn_id;      /* communication link it came through */
    int                      sync;         /* the sender waits for the reply */
    int                      from_server;  /* sent by the server side of the link */
    int                      is_reply;     /* an asynchronous reply */
    int                      has_security; /* a security attribute came with it */
    unsigned int             qos_level;    /* impersonation level its sender allows */
    struct alpc_port        *client;       /* connection requests: the connecting port */
    void                    *data;
    data_size_t              len;
};

struct alpc_recv_wait
{
    struct object     obj;
    struct alpc_port *port;                /* not referenced; cleared when the port goes */
};

struct alpc_port
{
    struct object            obj;          /* object header */
    enum alpc_port_kind      kind;
    unsigned int             flags;        /* ALPC_PORFLG_* */
    unsigned int             qos_level;    /* client ports: impersonation level allowed */
    data_size_t              max_len;      /* maximum total message length */
    client_ptr_t             context;      /* port context (server ports) */
    unsigned int             conn_id;      /* id of the communication link (both ends) */
    struct alpc_port        *conn;         /* server port: its connection port (referenced) */
    struct alpc_port        *peer;         /* the other end of a link (referenced) */
    int                      disconnected; /* the connection port went away */
    int                      closed;       /* no handles left */
    struct list              queue;        /* messages to receive */
    struct list              pending;      /* received messages waiting for a reply */
    struct list              server_ports; /* connection port: its server ports */
    struct list              server_entry; /* server port: entry in the connection port's list */
    struct list              global_entry; /* server port: entry in server_port_list */
    struct object           *sync;         /* what waits on the port handle see */
    struct alpc_recv_wait   *recv_wait;    /* what receivers wait on */
    unsigned int             replied[16];  /* recently replied message ids */
    unsigned int             replied_pos;
};

static unsigned int next_message_id = 0x100;
static unsigned int next_callback_id = 0x1000;
static unsigned int next_sequence = 1;
static unsigned int next_conn_id = 1;
static struct list server_port_list = LIST_INIT( server_port_list );

static void alpc_port_dump( struct object *obj, int verbose );
static struct object *alpc_port_get_sync( struct object *obj );
static int alpc_port_close_handle( struct object *obj, struct process *process, obj_handle_t handle );
static void alpc_port_destroy( struct object *obj );

static const struct object_ops alpc_port_ops =
{
    sizeof(struct alpc_port),  /* size */
    &alpc_port_type,           /* type */
    alpc_port_dump,            /* dump */
    NULL,                      /* add_queue */
    NULL,                      /* remove_queue */
    NULL,                      /* signaled */
    NULL,                      /* satisfied */
    no_signal,                 /* signal */
    no_get_fd,                 /* get_fd */
    alpc_port_get_sync,        /* get_sync */
    default_map_access,        /* map_access */
    default_get_sd,            /* get_sd */
    default_set_sd,            /* set_sd */
    default_get_full_name,     /* get_full_name */
    no_lookup_name,            /* lookup_name */
    directory_link_name,       /* link_name */
    default_unlink_name,       /* unlink_name */
    no_open_file,              /* open_file */
    no_kernel_obj_list,        /* get_kernel_obj_list */
    alpc_port_close_handle,    /* close_handle */
    alpc_port_destroy          /* destroy */
};

static void alpc_message_dump( struct object *obj, int verbose );
static int alpc_message_signaled( struct object *obj, struct wait_queue_entry *entry );
static void alpc_message_destroy( struct object *obj );

static const struct object_ops alpc_message_ops =
{
    sizeof(struct alpc_message), /* size */
    &no_type,                  /* type */
    alpc_message_dump,         /* dump */
    add_queue,                 /* add_queue */
    remove_queue,              /* remove_queue */
    alpc_message_signaled,     /* signaled */
    no_satisfied,              /* satisfied */
    no_signal,                 /* signal */
    no_get_fd,                 /* get_fd */
    default_get_sync,          /* get_sync */
    default_map_access,        /* map_access */
    default_get_sd,            /* get_sd */
    default_set_sd,            /* set_sd */
    no_get_full_name,          /* get_full_name */
    no_lookup_name,            /* lookup_name */
    no_link_name,              /* link_name */
    NULL,                      /* unlink_name */
    no_open_file,              /* open_file */
    no_kernel_obj_list,        /* get_kernel_obj_list */
    no_close_handle,           /* close_handle */
    alpc_message_destroy       /* destroy */
};

static void alpc_recv_wait_dump( struct object *obj, int verbose );
static int alpc_recv_wait_signaled( struct object *obj, struct wait_queue_entry *entry );

static const struct object_ops alpc_recv_wait_ops =
{
    sizeof(struct alpc_recv_wait), /* size */
    &no_type,                  /* type */
    alpc_recv_wait_dump,       /* dump */
    add_queue,                 /* add_queue */
    remove_queue,              /* remove_queue */
    alpc_recv_wait_signaled,   /* signaled */
    no_satisfied,              /* satisfied */
    no_signal,                 /* signal */
    no_get_fd,                 /* get_fd */
    default_get_sync,          /* get_sync */
    default_map_access,        /* map_access */
    default_get_sd,            /* get_sd */
    default_set_sd,            /* set_sd */
    no_get_full_name,          /* get_full_name */
    no_lookup_name,            /* lookup_name */
    no_link_name,              /* link_name */
    NULL,                      /* unlink_name */
    no_open_file,              /* open_file */
    no_kernel_obj_list,        /* get_kernel_obj_list */
    no_close_handle,           /* close_handle */
    no_destroy                 /* destroy */
};

/* ---- messages ---------------------------------------------------------- */

static void alpc_message_dump( struct object *obj, int verbose )
{
    struct alpc_message *msg = (struct alpc_message *)obj;
    fprintf( stderr, "ALPC message id=%08x type=%04x state=%d len=%u\n",
             msg->id, msg->type, msg->state, msg->len );
}

static int alpc_message_signaled( struct object *obj, struct wait_queue_entry *entry )
{
    struct alpc_message *msg = (struct alpc_message *)obj;
    return msg->state == MSG_REPLIED || msg->state == MSG_REFUSED || msg->state == MSG_ABORTED;
}

static void alpc_message_destroy( struct object *obj )
{
    struct alpc_message *msg = (struct alpc_message *)obj;
    assert( !msg->list );
    if (msg->client) release_object( msg->client );
    free( msg->data );
}

static struct alpc_message *create_message( unsigned int type, const void *data, data_size_t len )
{
    struct alpc_message *msg;

    if (!(msg = alloc_object( &alpc_message_ops ))) return NULL;
    msg->list        = NULL;
    msg->state       = MSG_QUEUED;
    msg->type        = type;
    msg->id          = next_message_id++;
    msg->callback_id = next_callback_id++;
    msg->sequence    = next_sequence++;
    msg->pid         = current ? current->process->id : 0;
    msg->tid         = current ? current->id : 0;
    msg->context     = 0;
    msg->has_context = 0;
    msg->conn_id     = 0;
    msg->sync        = 0;
    msg->from_server = 0;
    msg->is_reply    = 0;
    msg->has_security = 0;
    msg->qos_level   = SecurityImpersonation;
    msg->client      = NULL;
    msg->data        = NULL;
    msg->len         = 0;
    if (!next_message_id) next_message_id = 0x100;
    if (!next_callback_id) next_callback_id = 0x1000;
    if (!next_sequence) next_sequence = 1;
    if (len && !(msg->data = memdup( data, len )))
    {
        release_object( msg );
        return NULL;
    }
    msg->len = len;
    return msg;
}

/* take a message off its list; the reference the list held goes to the caller */
static void unlist_message( struct alpc_message *msg )
{
    if (!msg->list) return;
    list_remove( &msg->entry );
    msg->list = NULL;
}

static void set_message_data( struct alpc_message *msg, const void *data, data_size_t len )
{
    void *copy = NULL;

    if (len && !(copy = memdup( data, len ))) len = 0;
    free( msg->data );
    msg->data = copy;
    msg->len = len;
}

/* end a message that somebody may wait on */
static void finish_message( struct alpc_message *msg, enum alpc_message_state state )
{
    msg->state = state;
    if (msg->sync) wake_up( &msg->obj, 0 );
}

/* ---- ports ------------------------------------------------------------- */

static void alpc_recv_wait_dump( struct object *obj, int verbose )
{
    struct alpc_recv_wait *wait = (struct alpc_recv_wait *)obj;
    fprintf( stderr, "ALPC receive wait port=%p\n", wait->port );
}

static int alpc_recv_wait_signaled( struct object *obj, struct wait_queue_entry *entry )
{
    struct alpc_recv_wait *wait = (struct alpc_recv_wait *)obj;
    return !wait->port || !list_empty( &wait->port->queue );
}

static void alpc_port_dump( struct object *obj, int verbose )
{
    struct alpc_port *port = (struct alpc_port *)obj;
    fprintf( stderr, "ALPC port kind=%d flags=%08x peer=%p conn=%p\n",
             port->kind, port->flags, port->peer, port->conn );
}

static struct object *alpc_port_get_sync( struct object *obj )
{
    struct alpc_port *port = (struct alpc_port *)obj;
    return grab_object( port->sync );
}

static struct alpc_port *create_port( enum alpc_port_kind kind, struct object *root,
                                      const struct unicode_str *name, unsigned int attr,
                                      unsigned int flags, data_size_t max_len,
                                      const struct security_descriptor *sd )
{
    struct alpc_port *port;

    if (!(port = create_named_object( root, &alpc_port_ops, name, attr, sd ))) return NULL;
    if (get_error() == STATUS_OBJECT_NAME_EXISTS)
    {
        release_object( port );
        set_error( STATUS_OBJECT_NAME_COLLISION );
        return NULL;
    }
    port->kind         = kind;
    port->qos_level    = SecurityImpersonation;
    port->flags        = flags;
    port->max_len      = max_len;
    port->context      = 0;
    port->conn_id      = 0;
    port->conn         = NULL;
    port->peer         = NULL;
    port->disconnected = 0;
    port->closed       = 0;
    port->replied_pos  = 0;
    memset( port->replied, 0, sizeof(port->replied) );
    list_init( &port->queue );
    list_init( &port->pending );
    list_init( &port->server_ports );
    list_init( &port->server_entry );
    list_init( &port->global_entry );
    port->recv_wait    = NULL;
    /* a waitable port is signaled once per arriving message; any other port always is */
    if (flags & ALPC_PORFLG_WAITABLE_PORT) port->sync = create_internal_sync( 0, 0 );
    else port->sync = create_internal_sync( 1, 1 );
    if (!port->sync || !(port->recv_wait = alloc_object( &alpc_recv_wait_ops )))
    {
        release_object( port );
        return NULL;
    }
    port->recv_wait->port = port;
    return port;
}

static void queue_message( struct alpc_port *port, struct alpc_message *msg )
{
    unlist_message( msg );
    msg->state = MSG_QUEUED;
    list_add_tail( &port->queue, &msg->entry );
    msg->list = &port->queue;
    grab_object( msg );
    wake_up( &port->recv_wait->obj, 0 );
    if (port->flags & ALPC_PORFLG_WAITABLE_PORT) signal_sync( port->sync );
}

/* abort every message on LIST: queued requests and pending ones */
static void abort_messages( struct list *list )
{
    struct alpc_message *msg;
    struct list *ptr;

    while ((ptr = list_head( list )))
    {
        msg = LIST_ENTRY( ptr, struct alpc_message, entry );
        unlist_message( msg );
        if (msg->client) msg->state = MSG_REFUSED;
        else msg->state = MSG_ABORTED;
        if (msg->sync) wake_up( &msg->obj, 0 );
        release_object( msg );
    }
}

/* break the link between a client port and its server port */
static void unlink_ports( struct alpc_port *port )
{
    struct alpc_port *peer = port->peer;
    struct alpc_message *msg, *next;

    if (!peer) return;
    port->peer = NULL;
    peer->peer = NULL;
    /* nobody will reply to what the client still waits for */
    if (port->kind == ALPC_SERVER_PORT || peer->kind == ALPC_SERVER_PORT)
    {
        struct alpc_port *server = port->kind == ALPC_SERVER_PORT ? port : peer;
        if (server->conn)
        {
            LIST_FOR_EACH_ENTRY_SAFE( msg, next, &server->conn->pending, struct alpc_message, entry )
            {
                if (msg->conn_id != server->conn_id || msg->from_server || !msg->sync) continue;
                unlist_message( msg );
                finish_message( msg, MSG_ABORTED );
                release_object( msg );
            }
        }
    }
    release_object( peer );
    release_object( port );
}

static int alpc_port_close_handle( struct object *obj, struct process *process, obj_handle_t handle )
{
    struct alpc_port *port = (struct alpc_port *)obj, *server;
    struct alpc_message *msg;
    LARGE_INTEGER zero;

    if (port->obj.handle_count != 1) return 1;

    port->closed = 1;
    switch (port->kind)
    {
    case ALPC_CLIENT_PORT:
        if ((server = port->peer) && server->conn && !server->disconnected)
        {
            /* tell the server */
            zero.QuadPart = 0;
            if ((msg = create_message( TYPE_PORT_CLOSED, &zero, sizeof(zero) )))
            {
                msg->pid = msg->tid = 0;
                msg->context = server->context;
                msg->has_context = 1;
                msg->conn_id = server->conn_id;
                queue_message( server->conn, msg );
                release_object( msg );
            }
        }
        unlink_ports( port );
        abort_messages( &port->pending );
        break;
    case ALPC_SERVER_PORT:
        unlink_ports( port );
        break;
    case ALPC_CONNECTION_PORT:
        LIST_FOR_EACH_ENTRY( server, &port->server_ports, struct alpc_port, server_entry )
        {
            server->disconnected = 1;
            if (server->peer) server->peer->disconnected = 1;
        }
        abort_messages( &port->queue );
        abort_messages( &port->pending );
        unlink_named_object( &port->obj );
        break;
    }
    return 1;
}

static void alpc_port_destroy( struct object *obj )
{
    struct alpc_port *port = (struct alpc_port *)obj;

    assert( !port->peer );
    abort_messages( &port->queue );
    abort_messages( &port->pending );
    list_remove( &port->server_entry );
    list_remove( &port->global_entry );
    if (port->conn) release_object( port->conn );
    if (port->recv_wait)
    {
        port->recv_wait->port = NULL;
        wake_up( &port->recv_wait->obj, 0 );
        release_object( port->recv_wait );
    }
    if (port->sync) release_object( port->sync );
}

static struct alpc_port *get_port_obj( struct process *process, obj_handle_t handle, unsigned int access )
{
    return (struct alpc_port *)get_handle_obj( process, handle, access, &alpc_port_ops );
}

/* the port that receives what is sent to PORT's side of a link */
static struct alpc_port *receiving_port( struct alpc_port *port )
{
    if (port->kind == ALPC_SERVER_PORT) return port->conn;
    return port;
}

static struct alpc_message *find_pending( struct alpc_port *port, unsigned int id )
{
    struct alpc_message *msg;

    LIST_FOR_EACH_ENTRY( msg, &port->pending, struct alpc_message, entry )
        if (msg->id == id) return msg;
    return NULL;
}

static void remember_reply( struct alpc_port *port, unsigned int id )
{
    port->replied[port->replied_pos++ % ARRAY_SIZE(port->replied)] = id;
}

static int was_replied( struct alpc_port *port, unsigned int id )
{
    unsigned int i;
    for (i = 0; i < ARRAY_SIZE(port->replied); i++) if (id && port->replied[i] == id) return 1;
    return 0;
}

/* status for a message id that is not pending on PORT */
static unsigned int unknown_message_status( struct alpc_port *port, unsigned int id )
{
    return was_replied( port, id ) ? STATUS_ACCESS_DENIED : STATUS_INVALID_MESSAGE;
}

static struct alpc_port *find_server_port( unsigned int conn_id )
{
    struct alpc_port *port;

    LIST_FOR_EACH_ENTRY( port, &server_port_list, struct alpc_port, global_entry )
        if (port->conn_id == conn_id) return port;
    return NULL;
}

/* fill the common reply fields for a message */
#define FILL_MESSAGE_REPLY(reply, msg) \
    do { \
        (reply)->context     = (msg)->context; \
        (reply)->pid         = (msg)->pid; \
        (reply)->tid         = (msg)->tid; \
        (reply)->id          = (msg)->id; \
        (reply)->callback_id = (msg)->callback_id; \
        (reply)->sequence    = (msg)->sequence; \
        (reply)->type        = (msg)->type; \
        (reply)->total       = (msg)->len; \
    } while (0)

/* ---- requests ---------------------------------------------------------- */

/* create an ALPC connection port */
DECL_HANDLER(create_alpc_port)
{
    struct alpc_port *port;
    struct unicode_str name;
    struct object *root;
    const struct security_descriptor *sd;
    const struct object_attributes *objattr = get_req_object_attributes( &sd, &name, &root );

    if (!objattr) return;

    if (objattr->attributes & OBJ_OPENIF) set_error( STATUS_INVALID_PARAMETER );
    else if (req->max_len > ALPC_MAX_MESSAGE_LENGTH) set_error( STATUS_INVALID_PARAMETER );
    else if ((port = create_port( ALPC_CONNECTION_PORT, root, &name, objattr->attributes,
                                  req->flags | ALPC_PORFLG_ALLOW_LPC_REQUESTS,
                                  req->max_len ? req->max_len : ALPC_MAX_MESSAGE_LENGTH, sd )))
    {
        reply->handle = alloc_handle_no_access_check( current->process, port, PORT_ALL_ACCESS,
                                                      objattr->attributes & OBJ_INHERIT );
        release_object( port );
    }
    if (root) release_object( root );
}

/* connect to an ALPC connection port */
DECL_HANDLER(alpc_connect)
{
    struct alpc_port *conn, *client;
    struct alpc_message *msg;
    struct unicode_str name;
    struct object *root;
    const struct security_descriptor *sd;
    const struct object_attributes *objattr = get_req_object_attributes( &sd, &name, &root );
    const void *data;
    data_size_t len;

    if (!objattr) return;
    data = get_req_data_after_objattr( objattr, &len );

    conn = open_named_object( root, &alpc_port_ops, &name, objattr->attributes );
    if (root) release_object( root );
    if (!conn) return;

    if (conn->kind != ALPC_CONNECTION_PORT || conn->closed)
    {
        set_error( STATUS_OBJECT_NAME_NOT_FOUND );
        release_object( conn );
        return;
    }
    if (len + 0x28 > conn->max_len)
    {
        set_error( STATUS_PORT_MESSAGE_TOO_LONG );
        release_object( conn );
        return;
    }

    if (!(client = create_port( ALPC_CLIENT_PORT, NULL, NULL, 0, req->flags,
                                req->max_len ? min( req->max_len, ALPC_MAX_MESSAGE_LENGTH ) : conn->max_len,
                                NULL )))
    {
        release_object( conn );
        return;
    }
    client->qos_level = req->qos_level;
    if ((msg = create_message( TYPE_CONNECTION_REQUEST, data, len )))
    {
        msg->sync = 1;
        msg->has_security = req->security;
        msg->qos_level = client->qos_level;
        msg->client = (struct alpc_port *)grab_object( client );
        if ((reply->handle = alloc_handle_no_access_check( current->process, client, PORT_ALL_ACCESS, 0 )) &&
            (reply->wait = alloc_handle_no_access_check( current->process, msg, SYNCHRONIZE, 0 )))
            queue_message( conn, msg );
        release_object( msg );
    }
    release_object( client );
    release_object( conn );
}

/* accept or refuse an ALPC connection request */
DECL_HANDLER(alpc_accept)
{
    struct alpc_port *conn, *server, *client;
    struct alpc_message *msg;

    if (!(conn = get_port_obj( current->process, req->handle, 0 ))) return;

    if (conn->kind != ALPC_CONNECTION_PORT)
    {
        set_error( STATUS_INVALID_PORT_HANDLE );
        release_object( conn );
        return;
    }
    if (!(msg = find_pending( conn, req->id )) || !msg->client)
    {
        set_error( msg ? STATUS_INVALID_MESSAGE : unknown_message_status( conn, req->id ));
        release_object( conn );
        return;
    }

    client = msg->client;
    unlist_message( msg );
    if (!req->accept || client->closed)
    {
        finish_message( msg, MSG_REFUSED );
        release_object( msg );
        release_object( conn );
        return;
    }

    if ((server = create_port( ALPC_SERVER_PORT, NULL, NULL, 0, req->flags,
                               req->max_len ? min( req->max_len, ALPC_MAX_MESSAGE_LENGTH ) : conn->max_len,
                               NULL )))
    {
        server->context = req->context;
        server->conn = (struct alpc_port *)grab_object( conn );
        server->conn_id = client->conn_id = next_conn_id++;
        list_add_tail( &conn->server_ports, &server->server_entry );
        /* replies to asynchronous requests find the link by its id */
        list_add_tail( &server_port_list, &server->global_entry );
        server->peer = (struct alpc_port *)grab_object( client );
        client->peer = (struct alpc_port *)grab_object( server );

        if ((reply->handle = alloc_handle_no_access_check( current->process, server, PORT_ALL_ACCESS, 0 )))
        {
            set_message_data( msg, get_req_data(), get_req_data_size() );
            msg->type = TYPE_CONNECTION_REPLY;
            msg->pid  = current->process->id;
            msg->tid  = current->id;
            finish_message( msg, MSG_REPLIED );
        }
        else
        {
            unlink_ports( server );
            finish_message( msg, MSG_REFUSED );
        }
        release_object( server );
    }
    else finish_message( msg, MSG_REFUSED );

    release_object( msg );
    release_object( conn );
}

/* deliver the reply to an asynchronous request into the requester's queue */
static void queue_async_reply( struct alpc_message *msg )
{
    struct alpc_port *server, *target = NULL;

    if (msg->from_server)
    {
        /* a request the server sent to the client: the reply goes to the connection port */
        if ((server = find_server_port( msg->conn_id ))) target = server->conn;
    }
    else if ((server = find_server_port( msg->conn_id ))) target = server->peer;

    msg->is_reply = 1;
    if (target && !target->closed) queue_message( target, msg );
}

/* send a message through an ALPC port */
DECL_HANDLER(alpc_send)
{
    struct alpc_port *port, *target, *server = NULL, *recv;
    struct alpc_message *msg;
    int is_reply = 0;
    const void *data = get_req_data();
    data_size_t len = get_req_data_size();

    if (!(port = get_port_obj( current->process, req->handle, 0 ))) return;

    /* a message that names a request waiting for its reply is that reply,
     * with or without the reply flag */
    recv = receiving_port( port );
    if (!(req->flags & ALPC_MSGFLG_REPLY_MESSAGE) && req->id && recv &&
        (msg = find_pending( recv, req->id )) && !msg->client)
        is_reply = 1;

    if ((req->flags & ALPC_MSGFLG_REPLY_MESSAGE) || is_reply)
    {
        if (!recv) set_error( STATUS_PORT_DISCONNECTED );
        else if (!(msg = find_pending( recv, req->id )))
            set_error( unknown_message_status( recv, req->id ));
        else if (msg->state == MSG_CANCELED)
        {
            unlist_message( msg );
            release_object( msg );
            set_error( STATUS_REQUEST_CANCELED );
        }
        else if (req->total > port->max_len) set_error( STATUS_PORT_MESSAGE_TOO_LONG );
        else
        {
            unlist_message( msg );
            remember_reply( recv, msg->id );
            set_message_data( msg, data, len );
            msg->pid = current->process->id;
            msg->tid = current->id;
            msg->has_context = 0;
            /* a reply that releases the message is a plain LPC reply */
            if (req->flags & ALPC_MSGFLG_RELEASE_MESSAGE) msg->type = LPC_REPLY;
            if (msg->sync) finish_message( msg, MSG_REPLIED );
            else queue_async_reply( msg );
            release_object( msg );
        }
        release_object( port );
        return;
    }

    switch (port->kind)
    {
    case ALPC_CONNECTION_PORT:
        target = port;
        break;
    case ALPC_CLIENT_PORT:
        server = port->peer;
        if (!server || port->disconnected || !server->conn)
        {
            set_error( STATUS_PORT_DISCONNECTED );
            release_object( port );
            return;
        }
        target = server->conn;
        break;
    case ALPC_SERVER_PORT:
    default:
        /* the length is checked first, then the link */
        if (port->peer && (req->total > port->max_len || req->total > port->peer->max_len))
            set_error( STATUS_PORT_MESSAGE_TOO_LONG );
        else if (!port->peer) set_error( STATUS_PORT_DISCONNECTED );
        else if (!(port->peer->flags & ALPC_PORFLG_ALLOW_LPC_REQUESTS)) set_error( STATUS_LPC_REQUESTS_NOT_ALLOWED );
        else if (port->disconnected) set_error( STATUS_PORT_DISCONNECTED );
        if (get_error())
        {
            release_object( port );
            return;
        }
        server = port;
        target = port->peer;
        break;
    }

    if (req->total > port->max_len || req->total > target->max_len)
    {
        set_error( STATUS_PORT_MESSAGE_TOO_LONG );
        release_object( port );
        return;
    }

    if ((msg = create_message( (req->flags & ALPC_MSGFLG_RELEASE_MESSAGE) ? TYPE_DATAGRAM : TYPE_REQUEST,
                               data, len )))
    {
        msg->has_security = req->security;
        if (server)
        {
            msg->conn_id = server->conn_id;
            msg->from_server = (port->kind == ALPC_SERVER_PORT);
            if (port->kind == ALPC_CLIENT_PORT)
            {
                msg->context = server->context;
                msg->has_context = 1;
                msg->qos_level = port->qos_level;
            }
        }
        if ((req->flags & ALPC_MSGFLG_SYNC_REQUEST) && msg->type == TYPE_REQUEST)
        {
            msg->sync = 1;
            if ((reply->wait = alloc_handle_no_access_check( current->process, msg, SYNCHRONIZE, 0 )))
                queue_message( target, msg );
        }
        else queue_message( target, msg );
        release_object( msg );
    }
    release_object( port );
}

/* receive a message from an ALPC port */
DECL_HANDLER(alpc_receive)
{
    struct alpc_port *port, *recv;
    struct alpc_message *msg;
    struct list *ptr;

    if (!(port = get_port_obj( current->process, req->handle, 0 ))) return;

    if (!(recv = receiving_port( port )))
    {
        set_error( STATUS_PORT_DISCONNECTED );
        release_object( port );
        return;
    }

    if (!(ptr = list_head( &recv->queue )) && (recv->flags & ALPC_PORFLG_WAITABLE_PORT))
    {
        /* a waitable port is waited on first; receiving does not block */
        set_error( STATUS_UNSUCCESSFUL );
        release_object( port );
        return;
    }
    if (!ptr)
    {
        reply->wait = alloc_handle_no_access_check( current->process, recv->recv_wait, SYNCHRONIZE, 0 );
        if (reply->wait) set_error( STATUS_PENDING );
        release_object( port );
        return;
    }

    msg = LIST_ENTRY( ptr, struct alpc_message, entry );
    FILL_MESSAGE_REPLY( reply, msg );
    reply->has_context = msg->has_context;
    reply->has_security = msg->has_security;
    if (msg->len > req->size)
    {
        set_error( STATUS_BUFFER_TOO_SMALL );
        release_object( port );
        return;
    }
    set_reply_data( msg->data, msg->len );

    unlist_message( msg );
    if (!msg->is_reply && (msg->type == TYPE_CONNECTION_REQUEST || msg->type == TYPE_REQUEST))
    {
        /* keep it until it gets its answer; a reply to one whose sender
         * gave up fails later */
        if (msg->state != MSG_CANCELED) msg->state = MSG_PENDING;
        list_add_tail( &recv->pending, &msg->entry );
        msg->list = &recv->pending;
    }
    else
    {
        msg->state = MSG_DONE;
        release_object( msg );
    }
    release_object( port );
}

/* get the reply to a synchronous request */
DECL_HANDLER(alpc_get_reply)
{
    struct alpc_message *msg;

    if (!(msg = (struct alpc_message *)get_handle_obj( current->process, req->wait, 0, &alpc_message_ops )))
        return;

    switch (msg->state)
    {
    case MSG_REPLIED:
        FILL_MESSAGE_REPLY( reply, msg );
        if (msg->len > req->size) set_error( STATUS_BUFFER_TOO_SMALL );
        else set_reply_data( msg->data, msg->len );
        break;
    case MSG_REFUSED:
        set_error( STATUS_PORT_CONNECTION_REFUSED );
        break;
    case MSG_ABORTED:
        set_error( STATUS_PORT_DISCONNECTED );
        break;
    default:
        set_error( STATUS_PENDING );
        break;
    }
    release_object( msg );
}

/* cancel a synchronous request whose wait timed out */
DECL_HANDLER(alpc_cancel)
{
    struct alpc_message *msg;

    if (!(msg = (struct alpc_message *)get_handle_obj( current->process, req->wait, 0, &alpc_message_ops )))
        return;

    msg->sync = 0;
    switch (msg->state)
    {
    case MSG_QUEUED:
        if (msg->client)
        {
            /* a connection request stays queued, without its data */
            msg->type = TYPE_CANCELED;
            set_message_data( msg, NULL, 0 );
            release_object( msg->client );
            msg->client = NULL;
        }
        else msg->state = MSG_CANCELED;
        break;
    case MSG_PENDING:
        if (msg->client)
        {
            /* accepting it fails later */
            release_object( msg->client );
            msg->client = NULL;
            unlist_message( msg );
            release_object( msg );
            break;
        }
        msg->state = MSG_CANCELED;
        break;
    default:
        break;
    }
    release_object( msg );
}

/* disconnect an ALPC communication port */
DECL_HANDLER(alpc_disconnect)
{
    struct alpc_port *port;

    if (!(port = get_port_obj( current->process, req->handle, 0 ))) return;
    if (port->kind != ALPC_CONNECTION_PORT)
    {
        if (!port->peer) set_error( STATUS_PORT_DISCONNECTED );
        else unlink_ports( port );
    }
    release_object( port );
}

/* query an ALPC port, and check a message received through it */
DECL_HANDLER(alpc_query)
{
    struct alpc_port *port, *recv;
    struct alpc_message *msg;

    if (!(port = get_port_obj( current->process, req->handle, 0 ))) return;

    reply->flags = port->flags;
    reply->context = port->context;
    if (port->kind == ALPC_CLIENT_PORT && port->peer) reply->context = port->peer->context;
    if (req->id)
    {
        if (!(recv = receiving_port( port ))) set_error( STATUS_PORT_DISCONNECTED );
        else if (!(msg = find_pending( recv, req->id ))) set_error( unknown_message_status( recv, req->id ));
        else if (msg->state != MSG_PENDING) set_error( STATUS_ACCESS_DENIED );
        else
        {
            reply->pid = msg->pid;
            /* the client's security context comes with a security attribute,
             * and only to a port that allows impersonation */
            reply->qos_level = (msg->has_security && (recv->flags & ALPC_PORFLG_ALLOW_IMPERSONATION))
                               ? msg->qos_level : ALPC_NO_QOS;
        }
    }
    release_object( port );
}
