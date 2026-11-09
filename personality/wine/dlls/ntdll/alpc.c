/*
 * ALPC message attribute helpers
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
 * The behaviour follows the oracle test ntdll/alpc_attributes (Windows 11).
 */

#include <stdarg.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winternl.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(alpc);

/* the attribute bodies follow the header, from the highest flag down */
static SIZE_T attribute_size( ULONG flag )
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

/* size of the header plus the bodies of the FLAGS bits above LIMIT */
static SIZE_T attributes_size( ULONG flags, ULONG limit )
{
    SIZE_T size = sizeof(ALPC_MESSAGE_ATTRIBUTES);
    ULONG bit;

    for (bit = 0x80000000; bit > limit; bit >>= 1)
        if (flags & bit) size += attribute_size( bit );
    return size;
}

/***********************************************************************
 *             AlpcMaxAllowedMessageLength  (NTDLL.@)
 */
ULONG WINAPI AlpcMaxAllowedMessageLength(void)
{
    return 0xffff;
}

/***********************************************************************
 *             AlpcGetHeaderSize  (NTDLL.@)
 */
ULONG WINAPI AlpcGetHeaderSize( ULONG flags )
{
    return attributes_size( flags, 0 );
}

/***********************************************************************
 *             AlpcInitializeMessageAttribute  (NTDLL.@)
 */
NTSTATUS WINAPI AlpcInitializeMessageAttribute( ULONG flags, ALPC_MESSAGE_ATTRIBUTES *attrs, SIZE_T size,
                                                SIZE_T *required )
{
    SIZE_T needed = attributes_size( flags, 0 );

    TRACE( "(%#x, %p, %Iu, %p)\n", (int)flags, attrs, size, required );

    *required = needed;
    if (size < needed) return STATUS_BUFFER_TOO_SMALL;
    if (attrs)
    {
        attrs->AllocatedAttributes = flags;
        attrs->ValidAttributes = 0;
    }
    return STATUS_SUCCESS;
}

/***********************************************************************
 *             AlpcGetMessageAttribute  (NTDLL.@)
 */
void * WINAPI AlpcGetMessageAttribute( ALPC_MESSAGE_ATTRIBUTES *attrs, ULONG flag )
{
    if (!flag || (flag & (flag - 1))) return NULL;
    if (!(attrs->AllocatedAttributes & flag)) return NULL;
    return (char *)attrs + attributes_size( attrs->AllocatedAttributes, flag );
}
