/*
 * WNF (Windows Notification Facility) state names
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
 * The state store lives in the server (server/wnf.c). The behaviour follows
 * the oracle test ntdll/wnf_state (Windows 11).
 */

#if 0
#pragma makedep unix
#endif

#include "config.h"

#include <stdarg.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winternl.h"
#include "unix_private.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(wnf);


/***********************************************************************
 *             NtCreateWnfStateName  (NTDLL.@)
 */
NTSTATUS WINAPI NtCreateWnfStateName( WNF_STATE_NAME *name, WNF_STATE_NAME_LIFETIME lifetime,
                                      WNF_DATA_SCOPE scope, BOOLEAN persist, const WNF_TYPE_ID *type,
                                      ULONG max_size, PSECURITY_DESCRIPTOR sd )
{
    NTSTATUS status;

    TRACE( "(%p, %u, %u, %u, %p, %u, %p)\n", name, lifetime, scope, persist, type, (int)max_size, sd );

    if (!name) return STATUS_INVALID_PARAMETER;
    SERVER_START_REQ( wnf_create )
    {
        req->lifetime = lifetime;
        req->scope    = scope;
        req->max_size = max_size;
        if (!(status = wine_server_call( req ))) name->Value = reply->name;
    }
    SERVER_END_REQ;
    TRACE( "-> %#x %s\n", (int)status, wine_dbgstr_longlong( name->Value ));
    return status;
}


/***********************************************************************
 *             NtDeleteWnfStateName  (NTDLL.@)
 */
NTSTATUS WINAPI NtDeleteWnfStateName( const WNF_STATE_NAME *name )
{
    NTSTATUS status;

    TRACE( "(%s)\n", name ? wine_dbgstr_longlong( name->Value ) : "NULL" );

    if (!name) return STATUS_INVALID_PARAMETER;
    SERVER_START_REQ( wnf_delete )
    {
        req->name = name->Value;
        status = wine_server_call( req );
    }
    SERVER_END_REQ;
    return status;
}


/***********************************************************************
 *             NtUpdateWnfStateData  (NTDLL.@)
 */
NTSTATUS WINAPI NtUpdateWnfStateData( const WNF_STATE_NAME *name, const void *data, ULONG len,
                                      const WNF_TYPE_ID *type, const void *scope, WNF_CHANGE_STAMP match,
                                      ULONG check )
{
    NTSTATUS status;

    TRACE( "(%s, %p, %u, %p, %p, %u, %u)\n", name ? wine_dbgstr_longlong( name->Value ) : "NULL",
           data, (int)len, type, scope, (int)match, (int)check );

    if (!name) return STATUS_INVALID_PARAMETER;
    SERVER_START_REQ( wnf_update )
    {
        req->name        = name->Value;
        req->match_stamp = match;
        req->check_stamp = !!check;
        if (data) wine_server_add_data( req, data, len );
        status = wine_server_call( req );
    }
    SERVER_END_REQ;
    return status;
}


/***********************************************************************
 *             NtQueryWnfStateData  (NTDLL.@)
 */
NTSTATUS WINAPI NtQueryWnfStateData( const WNF_STATE_NAME *name, const WNF_TYPE_ID *type, const void *scope,
                                     WNF_CHANGE_STAMP *stamp, void *data, ULONG *len )
{
    NTSTATUS status;

    TRACE( "(%s, %p, %p, %p, %p, %p(%u))\n", name ? wine_dbgstr_longlong( name->Value ) : "NULL",
           type, scope, stamp, data, len, len ? (int)*len : 0 );

    if (!name || !len) return STATUS_INVALID_PARAMETER;
    SERVER_START_REQ( wnf_query )
    {
        req->name = name->Value;
        if (data) wine_server_set_reply( req, data, *len );
        status = wine_server_call( req );
        if (!status || status == STATUS_BUFFER_TOO_SMALL)
        {
            if (stamp) *stamp = reply->stamp;
            *len = reply->total;
        }
    }
    SERVER_END_REQ;
    TRACE( "-> %#x\n", (int)status );
    return status;
}


/***********************************************************************
 *             NtQueryWnfStateNameInformation  (NTDLL.@)
 */
NTSTATUS WINAPI NtQueryWnfStateNameInformation( const WNF_STATE_NAME *name, WNF_STATE_NAME_INFORMATION class,
                                                const void *scope, void *info, ULONG len )
{
    NTSTATUS status;

    TRACE( "(%s, %u, %p, %p, %u)\n", name ? wine_dbgstr_longlong( name->Value ) : "NULL",
           class, scope, info, (int)len );

    if (!name) return STATUS_INVALID_PARAMETER;
    if (len < sizeof(ULONG)) return STATUS_INFO_LENGTH_MISMATCH;
    SERVER_START_REQ( wnf_info )
    {
        req->name       = name->Value;
        req->info_class = class;
        if (!(status = wine_server_call( req ))) *(ULONG *)info = reply->value;
    }
    SERVER_END_REQ;
    return status;
}
