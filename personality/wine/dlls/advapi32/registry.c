/*
 * Registry management
 *
 * Copyright (C) 1999 Alexandre Julliard
 * Copyright (C) 2017 Dmitry Timoshkov
 *
 * Based on misc/registry.c code
 * Copyright (C) 1996 Marcus Meissner
 * Copyright (C) 1998 Matthew Becker
 * Copyright (C) 1999 Sylvain St-Germain
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

#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winbase.h"
#include "winreg.h"
#include "winerror.h"
#include "winternl.h"

#include "wine/debug.h"
#include "wine/list.h"

WINE_DEFAULT_DEBUG_CHANNEL(reg);

NTSTATUS WINAPI RemapPredefinedHandleInternal( HKEY hkey, HKEY override );
NTSTATUS WINAPI DisablePredefinedHandleTableInternal( HKEY hkey );


/******************************************************************************
 * RegOverridePredefKey   [ADVAPI32.@]
 */
LSTATUS WINAPI RegOverridePredefKey( HKEY hkey, HKEY override )
{
    return RtlNtStatusToDosError( RemapPredefinedHandleInternal( hkey, override ));
}


/******************************************************************************
 * RegCreateKeyW   [ADVAPI32.@]
 *
 * Creates the specified reg key.
 *
 * PARAMS
 *  hKey      [I] Handle to an open key.
 *  lpSubKey  [I] Name of a key that will be opened or created.
 *  phkResult [O] Receives a handle to the opened or created key.
 *
 * RETURNS
 *  Success: ERROR_SUCCESS
 *  Failure: nonzero error code defined in Winerror.h
 */
/* RegCreateKeyA/W with no subkey name only accept a predefined key, which is returned as is */
static LSTATUS create_key_no_subkey( HKEY hkey, PHKEY phkResult )
{
    if ((LONG_PTR)hkey == (LONG)HandleToUlong( hkey ) &&
        HandleToUlong( hkey ) >= HandleToUlong( HKEY_CLASSES_ROOT ) &&
        HandleToUlong( hkey ) <= HandleToUlong( HKEY_CURRENT_USER_LOCAL_SETTINGS ))
    {
        *phkResult = hkey;
        return ERROR_SUCCESS;
    }
    return ERROR_BADKEY;
}

LSTATUS WINAPI RegCreateKeyW( HKEY hkey, LPCWSTR lpSubKey, PHKEY phkResult )
{
    if (!phkResult)
        return ERROR_INVALID_PARAMETER;
    if (!lpSubKey || !*lpSubKey)
        return create_key_no_subkey( hkey, phkResult );

    return RegCreateKeyExW( hkey, lpSubKey, 0, NULL, REG_OPTION_NON_VOLATILE,
                            MAXIMUM_ALLOWED, NULL, phkResult, NULL );
}


/******************************************************************************
 * RegCreateKeyA   [ADVAPI32.@]
 *
 * See RegCreateKeyW.
 */
LSTATUS WINAPI RegCreateKeyA( HKEY hkey, LPCSTR lpSubKey, PHKEY phkResult )
{
    if (!phkResult)
        return ERROR_INVALID_PARAMETER;
    if (!lpSubKey || !*lpSubKey)
        return create_key_no_subkey( hkey, phkResult );

    return RegCreateKeyExA( hkey, lpSubKey, 0, NULL, REG_OPTION_NON_VOLATILE,
                            MAXIMUM_ALLOWED, NULL, phkResult, NULL );
}


/* Check the transaction handle passed to the Reg*Transacted functions. */
static LSTATUS check_transaction( HANDLE transaction )
{
    if (!transaction || transaction == INVALID_HANDLE_VALUE) return ERROR_INVALID_TRANSACTION;
    /* FIXME: there are no kernel transaction objects, so no other handle is a transaction */
    FIXME( "transaction %p not supported\n", transaction );
    return ERROR_INVALID_HANDLE;
}


/******************************************************************************
 * RegCreateKeyTransactedW   [ADVAPI32.@]
 */
LSTATUS WINAPI RegCreateKeyTransactedW( HKEY hkey, LPCWSTR name, DWORD reserved, LPWSTR class,
                                        DWORD options, REGSAM access, SECURITY_ATTRIBUTES *sa,
                                        PHKEY retkey, LPDWORD dispos, HANDLE transaction, PVOID reserved2 )
{
    TRACE( "(%p,%s,%lu,%s,%lu,%lu,%p,%p,%p,%p,%p)\n", hkey, debugstr_w(name), reserved,
           debugstr_w(class), options, access, sa, retkey, dispos, transaction, reserved2 );
    return check_transaction( transaction );
}


/******************************************************************************
 * RegCreateKeyTransactedA   [ADVAPI32.@]
 */
LSTATUS WINAPI RegCreateKeyTransactedA( HKEY hkey, LPCSTR name, DWORD reserved, LPSTR class,
                                        DWORD options, REGSAM access, SECURITY_ATTRIBUTES *sa,
                                        PHKEY retkey, LPDWORD dispos, HANDLE transaction, PVOID reserved2 )
{
    TRACE( "(%p,%s,%lu,%s,%lu,%lu,%p,%p,%p,%p,%p)\n", hkey, debugstr_a(name), reserved,
           debugstr_a(class), options, access, sa, retkey, dispos, transaction, reserved2 );
    return check_transaction( transaction );
}


/******************************************************************************
 * RegOpenKeyTransactedW   [ADVAPI32.@]
 */
LSTATUS WINAPI RegOpenKeyTransactedW( HKEY hkey, LPCWSTR name, DWORD options, REGSAM access,
                                      PHKEY retkey, HANDLE transaction, PVOID reserved )
{
    TRACE( "(%p,%s,%lu,%lu,%p,%p,%p)\n", hkey, debugstr_w(name), options, access, retkey, transaction, reserved );
    return check_transaction( transaction );
}


/******************************************************************************
 * RegOpenKeyTransactedA   [ADVAPI32.@]
 */
LSTATUS WINAPI RegOpenKeyTransactedA( HKEY hkey, LPCSTR name, DWORD options, REGSAM access,
                                      PHKEY retkey, HANDLE transaction, PVOID reserved )
{
    TRACE( "(%p,%s,%lu,%lu,%p,%p,%p)\n", hkey, debugstr_a(name), options, access, retkey, transaction, reserved );
    return check_transaction( transaction );
}


/******************************************************************************
 * RegDeleteKeyTransactedW   [ADVAPI32.@]
 */
LSTATUS WINAPI RegDeleteKeyTransactedW( HKEY hkey, LPCWSTR name, REGSAM access, DWORD reserved,
                                        HANDLE transaction, PVOID reserved2 )
{
    TRACE( "(%p,%s,%lu,%lu,%p,%p)\n", hkey, debugstr_w(name), access, reserved, transaction, reserved2 );
    return check_transaction( transaction );
}


/******************************************************************************
 * RegDeleteKeyTransactedA   [ADVAPI32.@]
 */
LSTATUS WINAPI RegDeleteKeyTransactedA( HKEY hkey, LPCSTR name, REGSAM access, DWORD reserved,
                                        HANDLE transaction, PVOID reserved2 )
{
    TRACE( "(%p,%s,%lu,%lu,%p,%p)\n", hkey, debugstr_a(name), access, reserved, transaction, reserved2 );
    return check_transaction( transaction );
}


/******************************************************************************
 * RegOpenKeyW   [ADVAPI32.@]
 *
 * See RegOpenKeyA.
 */
LSTATUS WINAPI RegOpenKeyW( HKEY hkey, LPCWSTR name, PHKEY retkey )
{
    if (!retkey)
        return ERROR_INVALID_PARAMETER;

    if (!name || !*name)
    {
        *retkey = hkey;
        return ERROR_SUCCESS;
    }
    return RegOpenKeyExW( hkey, name, 0, MAXIMUM_ALLOWED, retkey );
}


/******************************************************************************
 * RegOpenKeyA   [ADVAPI32.@]
 *
 * Open a registry key.
 *
 * PARAMS
 *  hkey    [I] Handle of parent key to open the new key under
 *  name    [I] Name of the key under hkey to open
 *  retkey  [O] Destination for the resulting Handle
 *
 * RETURNS
 *  Success: ERROR_SUCCESS
 *  Failure: A standard Win32 error code. When retkey is valid, *retkey is set to 0.
 */
LSTATUS WINAPI RegOpenKeyA( HKEY hkey, LPCSTR name, PHKEY retkey )
{
    if (!retkey)
        return ERROR_INVALID_PARAMETER;

    if (!name || !*name)
    {
        *retkey = hkey;
        return ERROR_SUCCESS;
    }
    return RegOpenKeyExA( hkey, name, 0, MAXIMUM_ALLOWED, retkey );
}


/******************************************************************************
 * RegEnumKeyW   [ADVAPI32.@]
 *
 * Enumerates subkeys of the specified open reg key.
 *
 * PARAMS
 *  hKey    [I] Handle to an open key.
 *  dwIndex [I] Index of the subkey of hKey to retrieve.
 *  lpName  [O] Name of the subkey.
 *  cchName [I] Size of lpName in TCHARS.
 *
 * RETURNS
 *  Success: ERROR_SUCCESS
 *  Failure: system error code. If there are no more subkeys available, the
 *           function returns ERROR_NO_MORE_ITEMS.
 */
LSTATUS WINAPI RegEnumKeyW( HKEY hkey, DWORD index, LPWSTR name, DWORD name_len )
{
    return RegEnumKeyExW( hkey, index, name, &name_len, NULL, NULL, NULL, NULL );
}


/******************************************************************************
 * RegEnumKeyA   [ADVAPI32.@]
 *
 * See RegEnumKeyW.
 */
LSTATUS WINAPI RegEnumKeyA( HKEY hkey, DWORD index, LPSTR name, DWORD name_len )
{
    return RegEnumKeyExA( hkey, index, name, &name_len, NULL, NULL, NULL, NULL );
}


/******************************************************************************
 * RegQueryMultipleValuesA   [ADVAPI32.@]
 *
 * Retrieves the type and data for a list of value names associated with a key.
 *
 * PARAMS
 *  hKey       [I] Handle to an open key.
 *  val_list   [O] Array of VALENT structures that describes the entries.
 *  num_vals   [I] Number of elements in val_list.
 *  lpValueBuf [O] Pointer to a buffer that receives the data for each value.
 *  ldwTotsize [I/O] Size of lpValueBuf.
 *
 * RETURNS
 *  Success: ERROR_SUCCESS. ldwTotsize contains num bytes copied.
 *  Failure: nonzero error code from Winerror.h ldwTotsize contains num needed
 *           bytes.
 */
LSTATUS WINAPI RegQueryMultipleValuesA( HKEY hkey, PVALENTA val_list, DWORD num_vals,
                                        LPSTR lpValueBuf, LPDWORD ldwTotsize )
{
    unsigned int i;
    DWORD maxBytes = *ldwTotsize;
    LSTATUS status;
    LPSTR bufptr = lpValueBuf;
    *ldwTotsize = 0;

    TRACE("(%p,%p,%ld,%p,%p=%ld)\n", hkey, val_list, num_vals, lpValueBuf, ldwTotsize, *ldwTotsize);

    for(i=0; i < num_vals; ++i)
    {

        val_list[i].ve_valuelen=0;
        status = RegQueryValueExA(hkey, val_list[i].ve_valuename, NULL, NULL, NULL, &val_list[i].ve_valuelen);
        if(status != ERROR_SUCCESS)
        {
            return status;
        }

        if(lpValueBuf != NULL && *ldwTotsize + val_list[i].ve_valuelen <= maxBytes)
        {
            status = RegQueryValueExA(hkey, val_list[i].ve_valuename, NULL, &val_list[i].ve_type,
                                      (LPBYTE)bufptr, &val_list[i].ve_valuelen);
            if(status != ERROR_SUCCESS)
            {
                return status;
            }

            val_list[i].ve_valueptr = (DWORD_PTR)bufptr;

            bufptr += val_list[i].ve_valuelen;
        }

        *ldwTotsize += val_list[i].ve_valuelen;
    }
    return lpValueBuf != NULL && *ldwTotsize <= maxBytes ? ERROR_SUCCESS : ERROR_MORE_DATA;
}


/******************************************************************************
 * RegQueryMultipleValuesW   [ADVAPI32.@]
 *
 * See RegQueryMultipleValuesA.
 */
LSTATUS WINAPI RegQueryMultipleValuesW( HKEY hkey, PVALENTW val_list, DWORD num_vals,
                                        LPWSTR lpValueBuf, LPDWORD ldwTotsize )
{
    struct { DWORD type, len, offset; } *vals;
    DWORD maxBytes = *ldwTotsize, total = 0;
    LSTATUS status = ERROR_SUCCESS;
    BYTE *buf = (BYTE *)lpValueBuf;
    unsigned int i;

    TRACE("(%p,%p,%ld,%p,%p=%ld)\n", hkey, val_list, num_vals, lpValueBuf, ldwTotsize, *ldwTotsize);

    /* a size without a buffer is an error */
    if (!buf && maxBytes)
    {
        *ldwTotsize = 0;
        return ERROR_NOACCESS;
    }
    if (!(vals = calloc( num_vals ? num_vals : 1, sizeof(*vals) ))) return ERROR_NOT_ENOUGH_MEMORY;

    /* the sizes are all read first, so a missing value leaves the buffer
     * untouched; each value starts DWORD-aligned */
    for (i = 0; i < num_vals; ++i)
    {
        vals[i].len = 0;
        status = RegQueryValueExW( hkey, val_list[i].ve_valuename, NULL, &vals[i].type, NULL, &vals[i].len );
        if (status) break;
        vals[i].offset = (total + 3) & ~3;
        total = vals[i].offset + vals[i].len;
    }

    /* the values that fit are copied, but the entries are only filled in
     * when all of them do */
    for (i = 0; !status && buf && i < num_vals; ++i)
    {
        if (vals[i].offset + vals[i].len > maxBytes) break;
        /* the alignment padding before a value is zeroed */
        if (i) memset( buf + vals[i - 1].offset + vals[i - 1].len, 0,
                       vals[i].offset - vals[i - 1].offset - vals[i - 1].len );
        status = RegQueryValueExW( hkey, val_list[i].ve_valuename, NULL, &vals[i].type,
                                   buf + vals[i].offset, &vals[i].len );
    }

    if (status) total = 0;
    else if (!buf || total > maxBytes) status = ERROR_MORE_DATA;
    else for (i = 0; i < num_vals; ++i)
    {
        val_list[i].ve_type = vals[i].type;
        val_list[i].ve_valuelen = vals[i].len;
        val_list[i].ve_valueptr = (DWORD_PTR)(buf + vals[i].offset);
    }
    free( vals );
    *ldwTotsize = total;
    return status;
}


/******************************************************************************
 * RegQueryReflectionKey   [ADVAPI32.@]
 */
LONG WINAPI RegQueryReflectionKey( HKEY hkey, BOOL *is_reflection_disabled )
{
    /* registry reflection no longer exists; every key reports it disabled */
    TRACE( "%p, %p\n", hkey, is_reflection_disabled );
    *is_reflection_disabled = TRUE;
    return ERROR_SUCCESS;
}


/******************************************************************************
 * RegDeleteKeyW   [ADVAPI32.@]
 *
 * See RegDeleteKeyA.
 */
LSTATUS WINAPI RegDeleteKeyW( HKEY hkey, LPCWSTR name )
{
    return RegDeleteKeyExW( hkey, name, 0, 0 );
}


/******************************************************************************
 * RegDeleteKeyA   [ADVAPI32.@]
 *
 * Delete a registry key.
 *
 * PARAMS
 *  hkey   [I] Handle to parent key containing the key to delete
 *  name   [I] Name of the key user hkey to delete
 *
 * NOTES
 *
 * MSDN is wrong when it says that hkey must be opened with the DELETE access
 * right. In reality, it opens a new handle with DELETE access.
 *
 * RETURNS
 *  Success: ERROR_SUCCESS
 *  Failure: Error code
 */
LSTATUS WINAPI RegDeleteKeyA( HKEY hkey, LPCSTR name )
{
    return RegDeleteKeyExA( hkey, name, 0, 0 );
}


/******************************************************************************
 * RegSetValueW   [ADVAPI32.@]
 *
 * Sets the data for the default or unnamed value of a reg key.
 *
 * PARAMS
 *  hkey     [I] Handle to an open key.
 *  subkey   [I] Name of a subkey of hKey.
 *  type     [I] Type of information to store.
 *  data     [I] String that contains the data to set for the default value.
 *  count    [I] Ignored.
 *
 * RETURNS
 *  Success: ERROR_SUCCESS
 *  Failure: nonzero error code from Winerror.h
 */
LSTATUS WINAPI RegSetValueW( HKEY hkey, LPCWSTR subkey, DWORD type, LPCWSTR data, DWORD count )
{
    TRACE("(%p,%s,%ld,%s,%ld)\n", hkey, debugstr_w(subkey), type, debugstr_w(data), count );

    if (type != REG_SZ || !data) return ERROR_INVALID_PARAMETER;

    return RegSetKeyValueW( hkey, subkey, NULL, type, data, (lstrlenW(data) + 1)*sizeof(WCHAR) );
}

/******************************************************************************
 * RegSetValueA   [ADVAPI32.@]
 *
 * See RegSetValueW.
 */
LSTATUS WINAPI RegSetValueA( HKEY hkey, LPCSTR subkey, DWORD type, LPCSTR data, DWORD count )
{
    TRACE("(%p,%s,%ld,%s,%ld)\n", hkey, debugstr_a(subkey), type, debugstr_a(data), count );

    if (type != REG_SZ || !data) return ERROR_INVALID_PARAMETER;

    return RegSetKeyValueA( hkey, subkey, NULL, type, data, strlen(data) + 1 );
}


/******************************************************************************
 * RegQueryValueW   [ADVAPI32.@]
 *
 * Retrieves the data associated with the default or unnamed value of a key.
 *
 * PARAMS
 *  hkey      [I] Handle to an open key.
 *  name      [I] Name of the subkey of hKey.
 *  data      [O] Receives the string associated with the default value
 *                of the key.
 *  count     [I/O] Size of lpValue in bytes.
 *
 *  RETURNS
 *   Success: ERROR_SUCCESS
 *   Failure: nonzero error code from Winerror.h
 */
LSTATUS WINAPI RegQueryValueW( HKEY hkey, LPCWSTR name, LPWSTR data, LPLONG count )
{
    DWORD ret, type = REG_NONE, count_in = count ? *count : 0;
    HKEY subkey = hkey;

    TRACE("(%p,%s,%p,%ld)\n", hkey, debugstr_w(name), data, count ? *count : 0 );

    if (name && name[0])
    {
        if ((ret = RegOpenKeyW( hkey, name, &subkey )) != ERROR_SUCCESS) return ret;
    }
    ret = RegQueryValueExW( subkey, NULL, NULL, &type, (LPBYTE)data, (LPDWORD)count );
    if (subkey != hkey) RegCloseKey( subkey );
    if (!ret && type == REG_EXPAND_SZ && data)
    {
        /* the string is expanded in the buffer, and the count reported is
         * the expanded length in characters; when it does not fit, as much
         * as fits is returned, terminated */
        DWORD cap, len;
        WCHAR *raw = NULL, *exp = NULL;

        if (count && (raw = malloc( *count + sizeof(WCHAR) )))
        {
            memcpy( raw, data, *count );
            raw[*count / sizeof(WCHAR)] = 0;
            len = ExpandEnvironmentStringsW( raw, NULL, 0 );
            if (len && (exp = malloc( len * sizeof(WCHAR) )))
            {
                ExpandEnvironmentStringsW( raw, exp, len );
                cap = (DWORD)(count_in / sizeof(WCHAR));
                if (len <= cap) memcpy( data, exp, len * sizeof(WCHAR) );
                else if (cap)
                {
                    memcpy( data, exp, (cap - 1) * sizeof(WCHAR) );
                    data[cap - 1] = 0;
                    ret = ERROR_MORE_DATA;
                }
                else ret = ERROR_MORE_DATA;
                *count = len;
            }
        }
        free( exp );
        free( raw );
    }
    /* a default value that is not a string is still copied, but fails */
    if (!ret && type != REG_SZ && type != REG_EXPAND_SZ && type != REG_MULTI_SZ)
        ret = ERROR_INVALID_DATA;
    if (ret == ERROR_FILE_NOT_FOUND)
    {
        /* return empty string if default value not found */
        if (data) *data = 0;
        if (count) *count = sizeof(WCHAR);
        ret = ERROR_SUCCESS;
    }
    return ret;
}


/******************************************************************************
 * RegQueryValueA   [ADVAPI32.@]
 *
 * See RegQueryValueW.
 */
LSTATUS WINAPI RegQueryValueA( HKEY hkey, LPCSTR name, LPSTR data, LPLONG count )
{
    DWORD ret, type = REG_NONE, count_in = count ? *count : 0;
    HKEY subkey = hkey;

    TRACE("(%p,%s,%p,%ld)\n", hkey, debugstr_a(name), data, count ? *count : 0 );

    if (name && name[0])
    {
        if ((ret = RegOpenKeyA( hkey, name, &subkey )) != ERROR_SUCCESS) return ret;
    }
    ret = RegQueryValueExA( subkey, NULL, NULL, &type, (LPBYTE)data, (LPDWORD)count );
    if (subkey != hkey) RegCloseKey( subkey );
    if (!ret && type == REG_EXPAND_SZ && data)
    {
        /* the string is expanded in the buffer and the count reported is the
         * expanded length; when it does not fit, the buffer is left with an
         * empty string and the count asks for one byte more, as documented
         * for ExpandEnvironmentStringsA */
        DWORD cap = count_in, len;
        char *raw = NULL, *exp = NULL;

        if (count && (raw = malloc( *count + 1 )))
        {
            memcpy( raw, data, *count );
            raw[*count] = 0;
            len = ExpandEnvironmentStringsA( raw, NULL, 0 );
            if (len && (exp = malloc( len )))
            {
                len = ExpandEnvironmentStringsA( raw, exp, len );
                if (len <= cap) memcpy( data, exp, len );
                else
                {
                    if (cap) data[0] = 0;
                    len++;
                    ret = ERROR_MORE_DATA;
                }
                *count = len;
            }
        }
        free( exp );
        free( raw );
    }
    /* a default value that is not a string is still copied, but fails */
    if (!ret && type != REG_SZ && type != REG_EXPAND_SZ && type != REG_MULTI_SZ)
        ret = ERROR_INVALID_DATA;
    if (ret == ERROR_FILE_NOT_FOUND)
    {
        /* return empty string if default value not found */
        if (data) *data = 0;
        if (count) *count = 1;
        ret = ERROR_SUCCESS;
    }
    return ret;
}


/******************************************************************************
 * RegSaveKeyW   [ADVAPI32.@]
 *
 * Save a key and all of its subkeys and values to a new file in the standard format.
 *
 * PARAMS
 *  hkey   [I] Handle of key where save begins
 *  lpFile [I] Address of filename to save to
 *  sa     [I] Address of security structure
 *
 * RETURNS
 *  Success: ERROR_SUCCESS
 *  Failure: nonzero error code from Winerror.h
 */
LSTATUS WINAPI RegSaveKeyW( HKEY hkey, LPCWSTR file, LPSECURITY_ATTRIBUTES sa )
{
    return RegSaveKeyExW(hkey, file, sa, REG_STANDARD_FORMAT);
}


/******************************************************************************
 * RegSaveKeyA  [ADVAPI32.@]
 *
 * See RegSaveKeyW.
 */
LSTATUS WINAPI RegSaveKeyA( HKEY hkey, LPCSTR file, LPSECURITY_ATTRIBUTES sa )
{
    return RegSaveKeyExA(hkey, file, sa, REG_STANDARD_FORMAT);
}


/******************************************************************************
 * RegReplaceKeyW [ADVAPI32.@]
 *
 * Replace the file backing a registry key and all its subkeys with another file.
 *
 * PARAMS
 *  hkey      [I] Handle of open key
 *  lpSubKey  [I] Address of name of subkey
 *  lpNewFile [I] Address of filename for file with new data
 *  lpOldFile [I] Address of filename for backup file
 *
 * RETURNS
 *  Success: ERROR_SUCCESS
 *  Failure: nonzero error code from Winerror.h
 */
LSTATUS WINAPI RegReplaceKeyW( HKEY hkey, LPCWSTR lpSubKey, LPCWSTR lpNewFile,
                               LPCWSTR lpOldFile )
{
    UNICODE_STRING new_name, old_name;
    OBJECT_ATTRIBUTES new_attr, old_attr;
    NTSTATUS status;
    HKEY subkey;
    LSTATUS ret;

    TRACE("(%p,%s,%s,%s)\n", hkey, debugstr_w(lpSubKey), debugstr_w(lpNewFile), debugstr_w(lpOldFile));

    if (!lpNewFile || !lpOldFile) return ERROR_INVALID_PARAMETER;

    /* only the root of a loaded hive can be replaced */
    if ((ret = RegOpenKeyExW( hkey, lpSubKey, 0, KEY_READ, &subkey )))
        return ret == ERROR_FILE_NOT_FOUND ? ERROR_INVALID_PARAMETER : ret;

    if (!RtlDosPathNameToNtPathName_U( lpNewFile, &new_name, NULL, NULL ))
    {
        RegCloseKey( subkey );
        return ERROR_INVALID_PARAMETER;
    }
    if (!RtlDosPathNameToNtPathName_U( lpOldFile, &old_name, NULL, NULL ))
    {
        RtlFreeUnicodeString( &new_name );
        RegCloseKey( subkey );
        return ERROR_INVALID_PARAMETER;
    }
    InitializeObjectAttributes( &new_attr, &new_name, OBJ_CASE_INSENSITIVE, 0, NULL );
    InitializeObjectAttributes( &old_attr, &old_name, OBJ_CASE_INSENSITIVE, 0, NULL );
    status = NtReplaceKey( &new_attr, subkey, &old_attr );
    RtlFreeUnicodeString( &new_name );
    RtlFreeUnicodeString( &old_name );
    RegCloseKey( subkey );
    return RtlNtStatusToDosError( status );
}


/******************************************************************************
 * RegRenameKey [ADVAPI32.@]
 *
 */
LSTATUS WINAPI RegRenameKey( HKEY hkey, LPCWSTR subkey_name, LPCWSTR new_name )
{
    UNICODE_STRING str;
    LSTATUS ret;
    HKEY subkey;

    TRACE("%p, %s, %s.\n", hkey, debugstr_w(subkey_name), debugstr_w(new_name));

    RtlInitUnicodeString(&str, new_name);

    if (!subkey_name)
        return RtlNtStatusToDosError( NtRenameKey( hkey, &str ));

    if ((ret = RegOpenKeyExW( hkey, subkey_name, 0, KEY_WRITE, &subkey )))
        return ret;

    ret = RtlNtStatusToDosError( NtRenameKey( subkey, &str ));
    RegCloseKey( subkey );

    return ret;
}


/******************************************************************************
 * RegReplaceKeyA [ADVAPI32.@]
 *
 * See RegReplaceKeyW.
 */
LSTATUS WINAPI RegReplaceKeyA( HKEY hkey, LPCSTR lpSubKey, LPCSTR lpNewFile,
                               LPCSTR lpOldFile )
{
    UNICODE_STRING lpSubKeyW;
    UNICODE_STRING lpNewFileW;
    UNICODE_STRING lpOldFileW;
    LONG ret;

    RtlCreateUnicodeStringFromAsciiz( &lpSubKeyW, lpSubKey );
    RtlCreateUnicodeStringFromAsciiz( &lpOldFileW, lpOldFile );
    RtlCreateUnicodeStringFromAsciiz( &lpNewFileW, lpNewFile );
    ret = RegReplaceKeyW( hkey, lpSubKeyW.Buffer, lpNewFileW.Buffer, lpOldFileW.Buffer );
    RtlFreeUnicodeString( &lpOldFileW );
    RtlFreeUnicodeString( &lpNewFileW );
    RtlFreeUnicodeString( &lpSubKeyW );
    return ret;
}


/******************************************************************************
 * RegConnectRegistryW [ADVAPI32.@]
 *
 * Establish a connection to a predefined registry key on another computer.
 *
 * PARAMS
 *  lpMachineName [I] Address of name of remote computer
 *  hHey          [I] Predefined registry handle
 *  phkResult     [I] Address of buffer for remote registry handle
 *
 * RETURNS
 *  Success: ERROR_SUCCESS
 *  Failure: nonzero error code from Winerror.h
 */
LSTATUS WINAPI RegConnectRegistryW( LPCWSTR lpMachineName, HKEY hKey,
                                    PHKEY phkResult )
{
    LONG ret;

    TRACE("(%s,%p,%p)\n",debugstr_w(lpMachineName), hKey, phkResult);

    if (!lpMachineName || !*lpMachineName) {
        /* Use the local machine name */
        ret = RegOpenKeyW( hKey, NULL, phkResult );
    }
    else {
        WCHAR compName[MAX_COMPUTERNAME_LENGTH + 1];
        DWORD len = ARRAY_SIZE( compName );

        /* MSDN says lpMachineName must start with \\ : not so */
        if( lpMachineName[0] == '\\' &&  lpMachineName[1] == '\\')
            lpMachineName += 2;
        if (GetComputerNameW(compName, &len))
        {
            if (!wcsicmp(lpMachineName, compName))
                ret = RegOpenKeyW(hKey, NULL, phkResult);
            else
            {
                FIXME("Connect to %s is not supported.\n",debugstr_w(lpMachineName));
                ret = ERROR_BAD_NETPATH;
            }
        }
        else
            ret = GetLastError();
    }
    return ret;
}


/******************************************************************************
 * RegConnectRegistryA [ADVAPI32.@]
 *
 * See RegConnectRegistryW.
 */
LSTATUS WINAPI RegConnectRegistryA( LPCSTR machine, HKEY hkey, PHKEY reskey )
{
    UNICODE_STRING machineW;
    LONG ret;

    RtlCreateUnicodeStringFromAsciiz( &machineW, machine );
    ret = RegConnectRegistryW( machineW.Buffer, hkey, reskey );
    RtlFreeUnicodeString( &machineW );
    return ret;
}


/******************************************************************************
 * RegDisablePredefinedCache [ADVAPI32.@]
 *
 * Disables the caching of the HKEY_CURRENT_USER key for the process.
 *
 * PARAMS
 *  None.
 *
 * RETURNS
 *  Success: ERROR_SUCCESS
 *  Failure: nonzero error code from Winerror.h
 *
 * NOTES
 *  This is useful for services that use impersonation.
 */
LSTATUS WINAPI RegDisablePredefinedCache(void)
{
    return RtlNtStatusToDosError( DisablePredefinedHandleTableInternal( HKEY_CURRENT_USER ));
}


/******************************************************************************
 * RegCopyTreeA [ADVAPI32.@]
 *
 */
LONG WINAPI RegCopyTreeA( HKEY hsrc, const char *subkey, HKEY hdst )
{
    UNICODE_STRING subkeyW;
    LONG ret;

    if (subkey) RtlCreateUnicodeStringFromAsciiz( &subkeyW, subkey );
    else subkeyW.Buffer = NULL;
    ret = RegCopyTreeW( hsrc, subkeyW.Buffer, hdst );
    RtlFreeUnicodeString( &subkeyW );
    return ret;
}


/******************************************************************************
 * RegEnableReflectionKey [ADVAPI32.@]
 *
 */
LONG WINAPI RegEnableReflectionKey(HKEY base)
{
    /* registry reflection no longer exists; this does nothing */
    TRACE("%p\n", base);
    return ERROR_SUCCESS;
}


/******************************************************************************
 * RegDisableReflectionKey [ADVAPI32.@]
 *
 */
LONG WINAPI RegDisableReflectionKey(HKEY base)
{
    TRACE("%p\n", base);
    return ERROR_SUCCESS;
}
