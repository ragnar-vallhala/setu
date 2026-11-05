/*
 * Win32 advapi functions
 *
 * Copyright 1995 Sven Verdoolaege
 * Copyright 2005 Mike McCormack
 * Copyright 2007 Rolf Kalbermatter
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

#include <stdarg.h>
#include "windef.h"
#include "winbase.h"
#include "winsvc.h"
#include "wine/debug.h"

#include "advapi32_misc.h"

WINE_DEFAULT_DEBUG_CHANNEL(service);

/******************************************************************************
 * EnumServicesStatusA [ADVAPI32.@]
 */
BOOL WINAPI
EnumServicesStatusA( SC_HANDLE hmngr, DWORD type, DWORD state, LPENUM_SERVICE_STATUSA
                     services, DWORD size, LPDWORD needed, LPDWORD returned,
                     LPDWORD resume_handle )
{
    BOOL ret;
    unsigned int i;
    ENUM_SERVICE_STATUSW *servicesW = NULL;
    DWORD sz, n;
    char *p;

    TRACE("%p 0x%lx 0x%lx %p %lu %p %p %p\n", hmngr, type, state, services, size, needed,
          returned, resume_handle);

    if (!hmngr)
    {
        SetLastError( ERROR_INVALID_HANDLE );
        return FALSE;
    }
    if (!needed || !returned)
    {
        SetLastError( ERROR_INVALID_ADDRESS );
        return FALSE;
    }

    sz = max( 2 * size, sizeof(*servicesW) );
    if (!(servicesW = malloc( sz )))
    {
        SetLastError( ERROR_NOT_ENOUGH_MEMORY );
        return FALSE;
    }

    ret = EnumServicesStatusW( hmngr, type, state, servicesW, sz, needed, returned, resume_handle );
    if (!ret) goto done;

    p = (char *)services + *returned * sizeof(ENUM_SERVICE_STATUSA);
    n = size - (p - (char *)services);
    ret = FALSE;
    for (i = 0; i < *returned; i++)
    {
        sz = WideCharToMultiByte( CP_ACP, 0, servicesW[i].lpServiceName, -1, p, n, NULL, NULL );
        if (!sz) goto done;
        services[i].lpServiceName = p;
        p += sz;
        n -= sz;
        if (servicesW[i].lpDisplayName)
        {
            sz = WideCharToMultiByte( CP_ACP, 0, servicesW[i].lpDisplayName, -1, p, n, NULL, NULL );
            if (!sz) goto done;
            services[i].lpDisplayName = p;
            p += sz;
            n -= sz;
        }
        else services[i].lpDisplayName = NULL;
        services[i].ServiceStatus = servicesW[i].ServiceStatus;
    }

    ret = TRUE;

done:
    free( servicesW );
    return ret;
}

/******************************************************************************
 * EnumServicesStatusW [ADVAPI32.@]
 */
BOOL WINAPI
EnumServicesStatusW( SC_HANDLE manager, DWORD type, DWORD state, ENUM_SERVICE_STATUSW *status,
                     DWORD size, DWORD *ret_size, DWORD *ret_count, DWORD *resume_handle )
{
    ENUM_SERVICE_STATUS_PROCESSW *status_ex;
    DWORD alloc_size, count, i;
    WCHAR *p;

    TRACE("%p 0x%lx 0x%lx %p %lu %p %p %p\n", manager, type, state, status, size,
          ret_size, ret_count, resume_handle);

    if (!manager)
    {
        SetLastError( ERROR_INVALID_HANDLE );
        return FALSE;
    }

    if (!ret_size || !ret_count)
    {
        SetLastError( ERROR_INVALID_PARAMETER );
        return FALSE;
    }

    *ret_size = 0;
    *ret_count = 0;
    if (!EnumServicesStatusExW( manager, SC_ENUM_PROCESS_INFO, type, state,
                                NULL, 0, &alloc_size, &count, resume_handle, NULL )
            && GetLastError() != ERROR_MORE_DATA)
        return FALSE;

    if (!(status_ex = malloc( alloc_size )))
    {
        SetLastError( ERROR_NOT_ENOUGH_MEMORY );
        return FALSE;
    }

    if (!EnumServicesStatusExW( manager, SC_ENUM_PROCESS_INFO, type, state, (BYTE *)status_ex,
                                alloc_size, &alloc_size, &count, resume_handle, NULL )
            && GetLastError() != ERROR_MORE_DATA)
    {
        free( status_ex );
        return FALSE;
    }

    for (i = 0; i < count; i++)
    {
        *ret_size += sizeof(ENUM_SERVICE_STATUSW);
        *ret_size += (lstrlenW( status_ex[i].lpServiceName ) + 1) * sizeof(WCHAR);
        if (status_ex[i].lpDisplayName)
            *ret_size += (lstrlenW( status_ex[i].lpDisplayName ) + 1) * sizeof(WCHAR);

        if (*ret_size <= size)
            ++*ret_count;
    }

    p = (WCHAR *)(status + *ret_count);
    for (i = 0; i < *ret_count; i++)
    {
        lstrcpyW( p, status_ex[i].lpServiceName );
        status[i].lpServiceName = p;
        p += lstrlenW( p ) + 1;
        if (status_ex[i].lpDisplayName)
        {
            lstrcpyW( p, status_ex[i].lpDisplayName );
            status[i].lpDisplayName = p;
            p += lstrlenW( p ) + 1;
        }
        else status[i].lpDisplayName = NULL;

        status[i].ServiceStatus.dwServiceType             = status_ex[i].ServiceStatusProcess.dwServiceType;
        status[i].ServiceStatus.dwCurrentState            = status_ex[i].ServiceStatusProcess.dwCurrentState;
        status[i].ServiceStatus.dwControlsAccepted        = status_ex[i].ServiceStatusProcess.dwControlsAccepted;
        status[i].ServiceStatus.dwWin32ExitCode           = status_ex[i].ServiceStatusProcess.dwWin32ExitCode;
        status[i].ServiceStatus.dwServiceSpecificExitCode = status_ex[i].ServiceStatusProcess.dwServiceSpecificExitCode;
        status[i].ServiceStatus.dwCheckPoint              = status_ex[i].ServiceStatusProcess.dwCheckPoint;
        status[i].ServiceStatus.dwWaitHint                = status_ex[i].ServiceStatusProcess.dwWaitHint;
    }

    free( status_ex );
    if (*ret_size > size)
    {
        SetLastError( ERROR_MORE_DATA );
        return FALSE;
    }

    *ret_size = 0;
    return TRUE;
}

/******************************************************************************
 * EnumServicesStatusExA [ADVAPI32.@]
 */
BOOL WINAPI
EnumServicesStatusExA( SC_HANDLE hmngr, SC_ENUM_TYPE level, DWORD type, DWORD state,
                       LPBYTE buffer, DWORD size, LPDWORD needed, LPDWORD returned,
                       LPDWORD resume_handle, LPCSTR group )
{
    BOOL ret;
    unsigned int i;
    ENUM_SERVICE_STATUS_PROCESSA *services = (ENUM_SERVICE_STATUS_PROCESSA *)buffer;
    ENUM_SERVICE_STATUS_PROCESSW *servicesW = NULL;
    WCHAR *groupW = NULL;
    DWORD sz, n;
    char *p;

    TRACE("%p %u 0x%lx 0x%lx %p %lu %p %p %p %s\n", hmngr, level, type, state, buffer,
          size, needed, returned, resume_handle, debugstr_a(group));

    sz = max( 2 * size, sizeof(*servicesW) );
    if (!(servicesW = malloc( sz )))
    {
        SetLastError( ERROR_NOT_ENOUGH_MEMORY );
        return FALSE;
    }
    if (group)
    {
        int len = MultiByteToWideChar( CP_ACP, 0, group, -1, NULL, 0 );
        if (!(groupW = malloc( len * sizeof(WCHAR) )))
        {
            SetLastError( ERROR_NOT_ENOUGH_MEMORY );
            free( servicesW );
            return FALSE;
        }
        MultiByteToWideChar( CP_ACP, 0, group, -1, groupW, len * sizeof(WCHAR) );
    }

    ret = EnumServicesStatusExW( hmngr, level, type, state, (BYTE *)servicesW, sz,
                                 needed, returned, resume_handle, groupW );
    if (!ret) goto done;

    p = (char *)services + *returned * sizeof(ENUM_SERVICE_STATUS_PROCESSA);
    n = size - (p - (char *)services);
    ret = FALSE;
    for (i = 0; i < *returned; i++)
    {
        sz = WideCharToMultiByte( CP_ACP, 0, servicesW[i].lpServiceName, -1, p, n, NULL, NULL );
        if (!sz) goto done;
        services[i].lpServiceName = p;
        p += sz;
        n -= sz;
        if (servicesW[i].lpDisplayName)
        {
            sz = WideCharToMultiByte( CP_ACP, 0, servicesW[i].lpDisplayName, -1, p, n, NULL, NULL );
            if (!sz) goto done;
            services[i].lpDisplayName = p;
            p += sz;
            n -= sz;
        }
        else services[i].lpDisplayName = NULL;
        services[i].ServiceStatusProcess = servicesW[i].ServiceStatusProcess;
    }

    ret = TRUE;

done:
    free( servicesW );
    free( groupW );
    return ret;
}

/******************************************************************************
 * GetServiceKeyNameA [ADVAPI32.@]
 */
BOOL WINAPI GetServiceKeyNameA( SC_HANDLE hSCManager, LPCSTR lpDisplayName,
                                LPSTR lpServiceName, LPDWORD lpcchBuffer )
{
    LPWSTR lpDisplayNameW, lpServiceNameW;
    DWORD sizeW;
    BOOL ret = FALSE;

    TRACE("%p %s %p %p\n", hSCManager,
          debugstr_a(lpDisplayName), lpServiceName, lpcchBuffer);

    lpDisplayNameW = strdupAW(lpDisplayName);
    if (lpServiceName)
        lpServiceNameW = malloc(*lpcchBuffer * sizeof(WCHAR));
    else
        lpServiceNameW = NULL;

    sizeW = *lpcchBuffer;
    if (!GetServiceKeyNameW(hSCManager, lpDisplayNameW, lpServiceNameW, &sizeW))
    {
        if (lpServiceName && *lpcchBuffer)
            lpServiceName[0] = 0;
        /* only a too-small buffer reports a size (an upper estimate) */
        if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) *lpcchBuffer = sizeW*2;
        goto cleanup;
    }

    if (!WideCharToMultiByte(CP_ACP, 0, lpServiceNameW, (sizeW + 1), lpServiceName,
                        *lpcchBuffer, NULL, NULL ))
    {
        if (*lpcchBuffer && lpServiceName)
            lpServiceName[0] = 0;
        *lpcchBuffer = WideCharToMultiByte(CP_ACP, 0, lpServiceNameW, -1, NULL, 0, NULL, NULL);
        goto cleanup;
    }

    /* lpcchBuffer not updated - same as in GetServiceDisplayNameA */
    ret = TRUE;

cleanup:
    free(lpServiceNameW);
    free(lpDisplayNameW);
    return ret;
}

/******************************************************************************
 * GetServiceDisplayNameA  [ADVAPI32.@]
 */
BOOL WINAPI GetServiceDisplayNameA( SC_HANDLE hSCManager, LPCSTR lpServiceName,
  LPSTR lpDisplayName, LPDWORD lpcchBuffer)
{
    LPWSTR lpServiceNameW, lpDisplayNameW;
    DWORD sizeW;
    BOOL ret = FALSE;

    TRACE("%p %s %p %p\n", hSCManager,
          debugstr_a(lpServiceName), lpDisplayName, lpcchBuffer);

    lpServiceNameW = strdupAW(lpServiceName);
    if (lpDisplayName)
        lpDisplayNameW = malloc(*lpcchBuffer * sizeof(WCHAR));
    else
        lpDisplayNameW = NULL;

    sizeW = *lpcchBuffer;
    if (!GetServiceDisplayNameW(hSCManager, lpServiceNameW, lpDisplayNameW, &sizeW))
    {
        if (lpDisplayName && *lpcchBuffer)
            lpDisplayName[0] = 0;
        /* only a too-small buffer reports a size (an upper estimate) */
        if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) *lpcchBuffer = sizeW*2;
        goto cleanup;
    }

    if (!WideCharToMultiByte(CP_ACP, 0, lpDisplayNameW, (sizeW + 1), lpDisplayName,
                        *lpcchBuffer, NULL, NULL ))
    {
        if (*lpcchBuffer && lpDisplayName)
            lpDisplayName[0] = 0;
        *lpcchBuffer = WideCharToMultiByte(CP_ACP, 0, lpDisplayNameW, -1, NULL, 0, NULL, NULL);
        goto cleanup;
    }

    /* probably due to a bug GetServiceDisplayNameA doesn't modify lpcchBuffer on success.
     * (but if the function succeeded it means that is a good upper estimation of the size) */
    ret = TRUE;

cleanup:
    free(lpDisplayNameW);
    free(lpServiceNameW);
    return ret;
}

/******************************************************************************
 * SetServiceBits [ADVAPI32.@]
 */
BOOL WINAPI SetServiceBits( SERVICE_STATUS_HANDLE hServiceStatus,
        DWORD dwServiceBits,
        BOOL bSetBitsOn,
        BOOL bUpdateImmediately)
{
    FIXME("%p %08lx %x %x\n", hServiceStatus, dwServiceBits,
          bSetBitsOn, bUpdateImmediately);
    return TRUE;
}

/******************************************************************************
 * EnumDependentServicesA [ADVAPI32.@]
 */
BOOL WINAPI EnumDependentServicesA( SC_HANDLE hService, DWORD dwServiceState,
                                    LPENUM_SERVICE_STATUSA lpServices, DWORD cbBufSize,
        LPDWORD pcbBytesNeeded, LPDWORD lpServicesReturned )
{
    ENUM_SERVICE_STATUSW *servicesW = NULL;
    DWORD sizeW = 0, needW = 0, count = 0, total = 0, i, n, last_error = GetLastError();
    char *p;

    TRACE("%p 0x%08lx %p 0x%08lx %p %p\n", hService, dwServiceState,
          lpServices, cbBufSize, pcbBytesNeeded, lpServicesReturned);

    if (!EnumDependentServicesW( hService, dwServiceState, NULL, 0, &sizeW, &count ))
    {
        if (GetLastError() != ERROR_MORE_DATA) return FALSE;
        needW = sizeW;
        if (!(servicesW = malloc( sizeW )))
        {
            SetLastError( ERROR_NOT_ENOUGH_MEMORY );
            return FALSE;
        }
        if (!EnumDependentServicesW( hService, dwServiceState, servicesW, sizeW, &sizeW, &count ))
        {
            free( servicesW );
            return FALSE;
        }
    }

    for (i = 0; i < count; i++)
    {
        total += sizeof(*lpServices);
        total += WideCharToMultiByte( CP_ACP, 0, servicesW[i].lpServiceName, -1, NULL, 0, NULL, NULL );
        if (servicesW[i].lpDisplayName)
            total += WideCharToMultiByte( CP_ACP, 0, servicesW[i].lpDisplayName, -1, NULL, 0, NULL, NULL );
    }
    *lpServicesReturned = 0;
    if (total > cbBufSize)
    {
        free( servicesW );
        /* Windows reports the size the W function needs */
        *pcbBytesNeeded = max( total, needW );
        SetLastError( ERROR_MORE_DATA );
        return FALSE;
    }

    p = (char *)(lpServices + count);
    for (i = 0; i < count; i++)
    {
        n = WideCharToMultiByte( CP_ACP, 0, servicesW[i].lpServiceName, -1, p, total, NULL, NULL );
        lpServices[i].lpServiceName = p;
        p += n;
        if (servicesW[i].lpDisplayName)
        {
            n = WideCharToMultiByte( CP_ACP, 0, servicesW[i].lpDisplayName, -1, p, total, NULL, NULL );
            lpServices[i].lpDisplayName = p;
            p += n;
        }
        else lpServices[i].lpDisplayName = NULL;
        lpServices[i].ServiceStatus = servicesW[i].ServiceStatus;
    }
    *lpServicesReturned = count;
    *pcbBytesNeeded = 0;
    free( servicesW );
    /* the sizing call above doesn't show in the last error */
    SetLastError( last_error );
    return TRUE;
}
