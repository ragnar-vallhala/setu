/*
 * Server-side WNF (Windows Notification Facility) state names
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
 * A state name holds a blob of data and a change stamp that every update
 * bumps. Names made by NtCreateWnfStateName are remembered after they are
 * deleted, so that later use fails the way it does on Windows. Any other
 * name is taken to be a well-known one: it exists, empty, until somebody
 * publishes to it.
 *
 * A subscription is a waitable object that is signaled while its state name
 * has a stamp newer than the last one it delivered; ntdll's dispatcher waits
 * on it and fetches the data.
 *
 * The behaviour follows the oracle test ntdll/wnf_state (Windows 11).
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

#define WNF_MAX_STATE_SIZE 0x1000

/* lifetimes and scopes, as NtCreateWnfStateName takes them */
#define WNF_LIFETIME_TEMPORARY 3

/* a state name is its fields xor'ed with this key: a 4-bit version (1),
 * the 2-bit lifetime, the 4-bit data scope, a permanent-data bit and a
 * unique number in the 53 bits left */
#define WNF_STATE_KEY          0x41c64e6da3bc0074ull
#define WNF_NAME_VERSION(n)    (((n) ^ WNF_STATE_KEY) & 0xf)
#define WNF_NAME_LIFETIME(n)   ((((n) ^ WNF_STATE_KEY) >> 4) & 3)
#define WNF_SCOPE_PROCESS      3
#define WNF_SCOPE_MAX          5

/* information classes */
#define WNF_INFO_EXISTS        0
#define WNF_INFO_SUBSCRIBERS   1
#define WNF_INFO_QUIESCENT     2

struct wnf_state
{
    struct list    entry;         /* entry in the global list */
    unsigned __int64 name;        /* state name value */
    unsigned int   stamp;         /* change stamp */
    data_size_t    max_size;      /* maximum data size */
    void          *data;          /* current data */
    data_size_t    len;
    int            created;       /* made by NtCreateWnfStateName */
    int            deleted;       /* deleted; kept so later use fails */
    struct list    subscriptions; /* its subscriptions */
};

struct wnf_subscription
{
    struct object     obj;        /* object header */
    struct list       entry;      /* entry in the state's list */
    struct wnf_state *state;      /* the state name */
    unsigned int      seen;       /* last stamp delivered */
};

static struct list wnf_states = LIST_INIT( wnf_states );
static unsigned int next_wnf_name = 1;

static void wnf_subscription_dump( struct object *obj, int verbose );
static int wnf_subscription_signaled( struct object *obj, struct wait_queue_entry *entry );
static void wnf_subscription_destroy( struct object *obj );

static const struct object_ops wnf_subscription_ops =
{
    sizeof(struct wnf_subscription), /* size */
    &no_type,                  /* type */
    wnf_subscription_dump,     /* dump */
    add_queue,                 /* add_queue */
    remove_queue,              /* remove_queue */
    wnf_subscription_signaled, /* signaled */
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
    wnf_subscription_destroy   /* destroy */
};

static void wnf_subscription_dump( struct object *obj, int verbose )
{
    struct wnf_subscription *sub = (struct wnf_subscription *)obj;
    fprintf( stderr, "WNF subscription name=%016llx seen=%u\n",
             (unsigned long long)sub->state->name, sub->seen );
}

static int wnf_subscription_signaled( struct object *obj, struct wait_queue_entry *entry )
{
    struct wnf_subscription *sub = (struct wnf_subscription *)obj;
    return !sub->state->deleted && sub->state->stamp > sub->seen;
}

static void wnf_subscription_destroy( struct object *obj )
{
    struct wnf_subscription *sub = (struct wnf_subscription *)obj;
    list_remove( &sub->entry );
}

static struct wnf_state *find_state( unsigned __int64 name )
{
    struct wnf_state *state;

    LIST_FOR_EACH_ENTRY( state, &wnf_states, struct wnf_state, entry )
        if (state->name == name) return state;
    return NULL;
}

static struct wnf_state *add_state( unsigned __int64 name, data_size_t max_size, int created )
{
    struct wnf_state *state;

    if (!(state = mem_alloc( sizeof(*state) ))) return NULL;
    state->name     = name;
    state->stamp    = 0;
    state->max_size = max_size;
    state->data     = NULL;
    state->len      = 0;
    state->created  = created;
    state->deleted  = 0;
    list_init( &state->subscriptions );
    list_add_tail( &wnf_states, &state->entry );
    return state;
}

static int valid_name( unsigned __int64 name )
{
    if (WNF_NAME_VERSION( name ) == 1) return 1;
    set_error( STATUS_INVALID_PARAMETER );
    return 0;
}

/* the state for NAME; a well-known name springs into existence, a
 * temporary one has to have been created */
static struct wnf_state *get_state( unsigned __int64 name )
{
    struct wnf_state *state;

    if (!valid_name( name )) return NULL;
    if (!(state = find_state( name )))
    {
        if (WNF_NAME_LIFETIME( name ) != WNF_LIFETIME_TEMPORARY)
            return add_state( name, WNF_MAX_STATE_SIZE, 0 );
        set_error( STATUS_OBJECT_NAME_NOT_FOUND );
        return NULL;
    }
    if (state->deleted)
    {
        set_error( STATUS_OBJECT_NAME_NOT_FOUND );
        return NULL;
    }
    return state;
}

static void wake_subscriptions( struct wnf_state *state )
{
    struct wnf_subscription *sub, *next;

    LIST_FOR_EACH_ENTRY_SAFE( sub, next, &state->subscriptions, struct wnf_subscription, entry )
        wake_up( &sub->obj, 0 );
}

/* create a temporary WNF state name */
DECL_HANDLER(wnf_create)
{
    struct wnf_state *state;
    unsigned __int64 name;

    if (req->max_size > WNF_MAX_STATE_SIZE || req->scope > WNF_SCOPE_MAX ||
        (req->lifetime == WNF_LIFETIME_TEMPORARY && (req->scope == WNF_SCOPE_PROCESS || req->scope == WNF_SCOPE_MAX)))
    {
        set_error( STATUS_INVALID_PARAMETER );
        return;
    }
    do
    {
        name = ((unsigned __int64)next_wnf_name++ << 11) | (req->scope << 6) |
               (WNF_LIFETIME_TEMPORARY << 4) | 1;
        name ^= WNF_STATE_KEY;
    } while (find_state( name ));

    if ((state = add_state( name, req->max_size, 1 ))) reply->name = state->name;
}

/* delete a WNF state name */
DECL_HANDLER(wnf_delete)
{
    struct wnf_state *state;

    if (!valid_name( req->name )) return;
    state = find_state( req->name );
    if (!state || state->deleted)
    {
        set_error( STATUS_OBJECT_NAME_NOT_FOUND );
        return;
    }
    state->deleted = 1;
    free( state->data );
    state->data = NULL;
    state->len = 0;
    wake_subscriptions( state );
}

/* publish new data for a WNF state name */
DECL_HANDLER(wnf_update)
{
    struct wnf_state *state;
    data_size_t len = get_req_data_size();
    void *data = NULL;

    if (!(state = get_state( req->name ))) return;
    if (len > state->max_size)
    {
        set_error( STATUS_INVALID_PARAMETER );
        return;
    }
    if (req->check_stamp && req->match_stamp != state->stamp)
    {
        set_error( STATUS_UNSUCCESSFUL );
        return;
    }
    if (len && !(data = memdup( get_req_data(), len ))) return;
    free( state->data );
    state->data = data;
    state->len = len;
    state->stamp++;
    wake_subscriptions( state );
}

/* read the data of a WNF state name */
DECL_HANDLER(wnf_query)
{
    struct wnf_state *state;

    if (!(state = get_state( req->name ))) return;
    reply->stamp = state->stamp;
    reply->total = state->len;
    if (state->len > get_reply_max_size()) set_error( STATUS_BUFFER_TOO_SMALL );
    else set_reply_data( state->data, state->len );
}

/* query information about a WNF state name */
DECL_HANDLER(wnf_info)
{
    struct wnf_state *state;

    if (!valid_name( req->name )) return;
    state = find_state( req->name );

    switch (req->info_class)
    {
    case WNF_INFO_EXISTS:
        reply->value = state ? !state->deleted : WNF_NAME_LIFETIME( req->name ) != WNF_LIFETIME_TEMPORARY;
        break;
    case WNF_INFO_SUBSCRIBERS:
        reply->value = state && !state->deleted && !list_empty( &state->subscriptions );
        break;
    case WNF_INFO_QUIESCENT:
        reply->value = 1;
        break;
    default:
        set_error( STATUS_INVALID_INFO_CLASS );
        break;
    }
}

/* subscribe to the changes of a WNF state name */
DECL_HANDLER(wnf_subscribe)
{
    struct wnf_subscription *sub;
    struct wnf_state *state;

    if (!(state = get_state( req->name ))) return;
    if (!(sub = alloc_object( &wnf_subscription_ops ))) return;
    sub->state = state;
    sub->seen  = req->stamp;
    list_add_tail( &state->subscriptions, &sub->entry );
    reply->handle = alloc_handle_no_access_check( current->process, sub, SYNCHRONIZE, 0 );
    release_object( sub );
}

/* fetch the change a subscription has been signaled for */
DECL_HANDLER(wnf_fetch)
{
    struct wnf_subscription *sub;
    struct wnf_state *state;

    if (!(sub = (struct wnf_subscription *)get_handle_obj( current->process, req->handle, 0,
                                                            &wnf_subscription_ops )))
        return;
    state = sub->state;
    if (state->deleted || state->stamp <= sub->seen) set_error( STATUS_NO_MORE_ENTRIES );
    else
    {
        reply->name  = state->name;
        reply->stamp = state->stamp;
        reply->total = state->len;
        if (state->len > get_reply_max_size()) set_error( STATUS_BUFFER_TOO_SMALL );
        else
        {
            set_reply_data( state->data, state->len );
            sub->seen = state->stamp;
        }
    }
    release_object( sub );
}
