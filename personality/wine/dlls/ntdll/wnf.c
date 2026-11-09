/*
 * WNF (Windows Notification Facility) user-mode subscriptions
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
 * Each subscription is a waitable server object (server/wnf.c) that is
 * signaled while its state name has news. A thread-pool wait runs the
 * callback on a pool thread with the current data, one change at a time.
 *
 * The behaviour follows the oracle test ntdll/wnf_state (Windows 11).
 */

#include <stdarg.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winternl.h"
#include "wine/server.h"
#include "ntdll_misc.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(wnf);

#define WNF_MAX_STATE_SIZE 0x1000

struct wnf_user_subscription
{
    HANDLE              handle;     /* server subscription */
    HANDLE              wait;       /* thread-pool wait on it */
    WNF_STATE_NAME      name;
    PWNF_USER_CALLBACK  callback;
    void               *context;
    RTL_CRITICAL_SECTION cs;        /* held while the callback runs */
    DWORD               cb_thread;  /* thread running the callback */
    BOOL                dead;       /* unsubscribed from its own callback */
};

static void free_subscription( struct wnf_user_subscription *sub )
{
    NtClose( sub->handle );
    RtlDeleteCriticalSection( &sub->cs );
    RtlFreeHeap( GetProcessHeap(), 0, sub );
}

static void CALLBACK subscription_callback( void *arg, BOOLEAN timeout )
{
    struct wnf_user_subscription *sub = arg;
    WNF_CHANGE_STAMP stamp = 0;
    ULONG len = 0;
    NTSTATUS status;
    BOOL dead;
    BYTE *data;

    if (!(data = RtlAllocateHeap( GetProcessHeap(), 0, WNF_MAX_STATE_SIZE ))) return;
    RtlEnterCriticalSection( &sub->cs );
    if (!sub->dead)
    {
        SERVER_START_REQ( wnf_fetch )
        {
            req->handle = wine_server_obj_handle( sub->handle );
            wine_server_set_reply( req, data, WNF_MAX_STATE_SIZE );
            if (!(status = wine_server_call( req )))
            {
                stamp = reply->stamp;
                len = reply->total;
            }
        }
        SERVER_END_REQ;
        if (!status)
        {
            TRACE( "calling %p for %s stamp %u len %u\n", sub->callback,
                   wine_dbgstr_longlong( sub->name.Value ), (int)stamp, (int)len );
            sub->cb_thread = GetCurrentThreadId();
            sub->callback( sub->name, stamp, NULL, sub->context, len ? data : NULL, len );
            sub->cb_thread = 0;
        }
    }
    dead = sub->dead;
    RtlLeaveCriticalSection( &sub->cs );
    RtlFreeHeap( GetProcessHeap(), 0, data );
    if (dead) free_subscription( sub );
}


/***********************************************************************
 *             RtlSubscribeWnfStateChangeNotification  (NTDLL.@)
 */
NTSTATUS WINAPI RtlSubscribeWnfStateChangeNotification( PWNF_USER_SUBSCRIPTION *ret, WNF_STATE_NAME name,
                                                        WNF_CHANGE_STAMP stamp, PWNF_USER_CALLBACK callback,
                                                        void *context, const WNF_TYPE_ID *type,
                                                        ULONG group, ULONG flags )
{
    struct wnf_user_subscription *sub;
    NTSTATUS status;

    TRACE( "(%p, %s, %u, %p, %p, %p, %u, %#x)\n", ret, wine_dbgstr_longlong( name.Value ), (int)stamp,
           callback, context, type, (int)group, (int)flags );

    if (!ret || !callback) return STATUS_INVALID_PARAMETER;
    *ret = NULL;
    if (!(sub = RtlAllocateHeap( GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*sub) ))) return STATUS_NO_MEMORY;
    sub->name = name;
    sub->callback = callback;
    sub->context = context;
    RtlInitializeCriticalSection( &sub->cs );

    SERVER_START_REQ( wnf_subscribe )
    {
        req->name  = name.Value;
        req->stamp = stamp;
        if (!(status = wine_server_call( req ))) sub->handle = wine_server_ptr_handle( reply->handle );
    }
    SERVER_END_REQ;

    if (!status && (status = RtlRegisterWait( &sub->wait, sub->handle, subscription_callback, sub,
                                              INFINITE, WT_EXECUTEINWAITTHREAD )))
        NtClose( sub->handle );
    if (status)
    {
        RtlDeleteCriticalSection( &sub->cs );
        RtlFreeHeap( GetProcessHeap(), 0, sub );
        return status;
    }
    *ret = (PWNF_USER_SUBSCRIPTION)sub;
    return STATUS_SUCCESS;
}


/***********************************************************************
 *             RtlUnsubscribeWnfStateChangeNotification  (NTDLL.@)
 */
NTSTATUS WINAPI RtlUnsubscribeWnfStateChangeNotification( PWNF_USER_SUBSCRIPTION handle )
{
    struct wnf_user_subscription *sub = (struct wnf_user_subscription *)handle;

    TRACE( "(%p)\n", handle );

    if (!sub) return STATUS_INVALID_PARAMETER;
    if (sub->cb_thread == GetCurrentThreadId())
    {
        /* from inside its own callback: the callback frees it on the way out */
        sub->dead = TRUE;
        RtlDeregisterWaitEx( sub->wait, NULL );
        return STATUS_SUCCESS;
    }
    RtlDeregisterWaitEx( sub->wait, INVALID_HANDLE_VALUE );
    free_subscription( sub );
    return STATUS_SUCCESS;
}
