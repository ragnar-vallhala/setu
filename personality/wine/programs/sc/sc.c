/*
 * Copyright 2010 Hans Leidekker
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

#define WIN32_LEAN_AND_MEAN

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <windows.h>
#include <winsvc.h>
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(sc);

static BOOL parse_string_param( int argc, const WCHAR *argv[], unsigned int *index,
                                const WCHAR *param_name, size_t name_len, const WCHAR **out )
{
    if (!wcsnicmp( argv[*index], param_name, name_len ))
    {
        if (argv[*index][name_len])
        {
            *out = &argv[*index][name_len];
            return TRUE;
        }
        else if (*index < argc - 1)
        {
            *index += 1;
            *out = argv[*index];
            return TRUE;
        }
    }
    return FALSE;
}

struct create_params
{
    const WCHAR *displayname;
    const WCHAR *binpath;
    const WCHAR *group;
    const WCHAR *depend;
    const WCHAR *obj;
    const WCHAR *password;
    DWORD type;
    DWORD start;
    DWORD error;
    BOOL tag;
    BOOL type_set;
    BOOL start_set;
    BOOL error_set;
};

/* depend= takes names separated by '/'; the API wants a multi-string */
static const WCHAR *make_depend( const WCHAR *arg )
{
    WCHAR *ret, *p;
    size_t len = wcslen( arg );

    if (!(ret = calloc( len + 2, sizeof(WCHAR) ))) return NULL;
    memcpy( ret, arg, len * sizeof(WCHAR) );
    for (p = ret; *p; p++) if (*p == '/') *p = 0;
    return ret;
}

static BOOL parse_create_params( int argc, const WCHAR *argv[], struct create_params *cp )
{
    unsigned int i;

    cp->displayname = NULL;
    cp->type        = SERVICE_WIN32_OWN_PROCESS;
    cp->start       = SERVICE_DEMAND_START;
    cp->error       = SERVICE_ERROR_NORMAL;
    cp->binpath     = NULL;
    cp->group       = NULL;
    cp->tag         = FALSE;
    cp->depend      = NULL;
    cp->obj         = NULL;
    cp->password    = NULL;
    cp->type_set    = FALSE;
    cp->start_set   = FALSE;
    cp->error_set   = FALSE;

#define PARSE(x, y) parse_string_param( (argc), (argv), &(i), (x), ARRAY_SIZE(x) - 1, (y) )
    for (i = 0; i < argc; i++)
    {
        const WCHAR *tag, *type, *start, *error;

        if (PARSE( L"displayname=", &cp->displayname )) continue;
        if (PARSE( L"binpath=", &cp->binpath )) continue;
        if (PARSE( L"group=", &cp->group )) continue;
        if (PARSE( L"depend=", &cp->depend ))
        {
            cp->depend = make_depend( cp->depend );
            continue;
        }
        if (PARSE( L"obj=", &cp->obj )) continue;
        if (PARSE( L"password=", &cp->password )) continue;

        if (PARSE( L"tag=", &tag ))
        {
            if (!wcsicmp( tag, L"yes" ))
            {
                WINE_FIXME("tag argument not supported\n");
                cp->tag = TRUE;
            }
            continue;
        }
        if (PARSE( L"type=", &type ))
        {
            cp->type_set = TRUE;
            if (!wcsicmp( type, L"own" )) cp->type = SERVICE_WIN32_OWN_PROCESS;
            else if (!wcsicmp( type, L"share" )) cp->type = SERVICE_WIN32_SHARE_PROCESS;
            else if (!wcsicmp( type, L"kernel" )) cp->type = SERVICE_KERNEL_DRIVER;
            else if (!wcsicmp( type, L"filesys" )) cp->type = SERVICE_FILE_SYSTEM_DRIVER;
            else if (!wcsicmp( type, L"rec" )) cp->type = SERVICE_RECOGNIZER_DRIVER;
            else if (!wcsicmp( type, L"interact" )) cp->type |= SERVICE_INTERACTIVE_PROCESS;
            continue;
        }
        if (PARSE( L"start=", &start ))
        {
            cp->start_set = TRUE;
            if (!wcsicmp( start, L"boot" )) cp->start = SERVICE_BOOT_START;
            else if (!wcsicmp( start, L"system" )) cp->start = SERVICE_SYSTEM_START;
            else if (!wcsicmp( start, L"auto" )) cp->start = SERVICE_AUTO_START;
            else if (!wcsicmp( start, L"demand" )) cp->start = SERVICE_DEMAND_START;
            else if (!wcsicmp( start, L"disabled" )) cp->start = SERVICE_DISABLED;
            continue;
        }
        if (PARSE( L"error=", &error ))
        {
            cp->error_set = TRUE;
            if (!wcsicmp( error, L"normal" )) cp->error = SERVICE_ERROR_NORMAL;
            else if (!wcsicmp( error, L"severe" )) cp->error = SERVICE_ERROR_SEVERE;
            else if (!wcsicmp( error, L"critical" )) cp->error = SERVICE_ERROR_CRITICAL;
            else if (!wcsicmp( error, L"ignore" )) cp->error = SERVICE_ERROR_IGNORE;
            continue;
        }
    }
#undef PARSE
    if (!cp->binpath) return FALSE;
    return TRUE;
}

static BOOL parse_failure_actions( const WCHAR *arg, SERVICE_FAILURE_ACTIONSW *fa )
{
    unsigned int i, count;
    WCHAR *actions, *p;

    actions = wcsdup( arg );
    if (!actions) return FALSE;

    for (p = actions, count = 0; *p; p++)
    {
        if (*p == '/')
        {
            count++;
            *p = 0;
        }
    }
    count = count / 2 + 1;

    fa->cActions = count;
    fa->lpsaActions = malloc( fa->cActions * sizeof(SC_ACTION) );
    if (!fa->lpsaActions)
    {
        free( actions );
        return FALSE;
    }

    p = actions;
    for (i = 0; i < count; i++)
    {
        if (!wcsicmp( p, L"run" )) fa->lpsaActions[i].Type = SC_ACTION_RUN_COMMAND;
        else if (!wcsicmp( p, L"restart" )) fa->lpsaActions[i].Type = SC_ACTION_RESTART;
        else if (!wcsicmp( p, L"reboot" )) fa->lpsaActions[i].Type = SC_ACTION_REBOOT;
        else fa->lpsaActions[i].Type = SC_ACTION_NONE;

        p += lstrlenW( p ) + 1;
        fa->lpsaActions[i].Delay = wcstol( p, NULL, 10 );
        p += lstrlenW( p ) + 1;
    }

    free( actions );
    return TRUE;
}

static BOOL parse_failure_params( int argc, const WCHAR *argv[], SERVICE_FAILURE_ACTIONSW *fa )
{
    unsigned int i;

    fa->dwResetPeriod = 0;
    fa->lpRebootMsg   = NULL;
    fa->lpCommand     = NULL;
    fa->cActions      = 0;
    fa->lpsaActions   = NULL;

#define PARSE(x, y) parse_string_param( (argc), (argv), &(i), (x), ARRAY_SIZE(x) - 1, (y) )
    for (i = 0; i < argc; i++)
    {
        const WCHAR *reset, *actions;

        if (PARSE( L"reset=", &reset ))
        {
            fa->dwResetPeriod = wcstol( reset, NULL, 10 );
            continue;
        }
        if (PARSE( L"reboot=", (const WCHAR **)&fa->lpRebootMsg )) continue;
        if (PARSE( L"command=", (const WCHAR **)&fa->lpCommand )) continue;
        if (PARSE( L"actions=", &actions ))
        {
            if (!parse_failure_actions( actions, fa )) return FALSE;
            continue;
        }
    }
#undef PARSE
    return TRUE;
}

static void usage( void )
{
    WINE_MESSAGE( "Usage: sc command servicename [parameter= value ...]\n" );
    exit( ERROR_INVALID_COMMAND_LINE );
}

static void create_usage( void )
{
    /* our own wording; like Windows, it ends with ERROR_INVALID_COMMAND_LINE */
    printf( "Usage: sc create <name> binPath= <path> [type= ...] [start= ...] [error= ...]\n"
            "                 [group= ...] [tag= yes|no] [depend= a/b] [obj= ...]\n"
            "                 [DisplayName= ...] [password= ...]\n" );
    exit( ERROR_INVALID_COMMAND_LINE );
}

/* "[SC] <api> FAILED <code>:", a blank line, the system message and another blank line */
static DWORD print_failure( const char *api, DWORD err )
{
    WCHAR *msg = NULL;

    FormatMessageW( FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS,
                    NULL, err, 0, (WCHAR *)&msg, 0, NULL );
    printf( "[SC] %s FAILED %lu:\n\n%ls\n", api, err, msg ? msg : L"\n" );
    LocalFree( msg );
    return err;
}

static DWORD print_success( const char *api )
{
    printf( "[SC] %s SUCCESS\n", api );
    return ERROR_SUCCESS;
}

static const WCHAR *service_type_string( DWORD type )
{
    switch (type & ~SERVICE_INTERACTIVE_PROCESS)
    {
        case SERVICE_KERNEL_DRIVER: return L"KERNEL_DRIVER";
        case SERVICE_FILE_SYSTEM_DRIVER: return L"FILE_SYSTEM_DRIVER";
        case SERVICE_WIN32_OWN_PROCESS: return L"WIN32_OWN_PROCESS";
        case SERVICE_WIN32_SHARE_PROCESS: return L"WIN32_SHARE_PROCESS";
        case SERVICE_WIN32: return L"WIN32";
        default: return L"";
    }
}

static const WCHAR *service_state_string( DWORD state )
{
    static const WCHAR * const state_str[] = { L"", L"STOPPED", L"START_PENDING",
        L"STOP_PENDING", L"RUNNING", L"CONTINUE_PENDING", L"PAUSE_PENDING", L"PAUSED" };

    if (state < ARRAY_SIZE( state_str )) return state_str[ state ];
    return L"";
}

static const WCHAR *start_type_string( DWORD start )
{
    static const WCHAR * const start_str[] = { L"BOOT_START", L"SYSTEM_START", L"AUTO_START",
        L"DEMAND_START", L"DISABLED" };

    if (start < ARRAY_SIZE( start_str )) return start_str[ start ];
    return L"";
}

static const WCHAR *error_control_string( DWORD error )
{
    static const WCHAR * const error_str[] = { L"IGNORE", L"NORMAL", L"SEVERE", L"CRITICAL" };

    if (error < ARRAY_SIZE( error_str )) return error_str[ error ];
    return L"";
}

/* The status block printed by query, queryex, start and stop. */
static void print_status( const WCHAR *name, const SERVICE_STATUS_PROCESS *status, BOOL ex )
{
    if (name) printf( "\nSERVICE_NAME: %ls\n", name );
    printf( "        TYPE               : %lx  %ls%ls\n", status->dwServiceType,
            service_type_string( status->dwServiceType ),
            status->dwServiceType & SERVICE_INTERACTIVE_PROCESS ? L" (interactive)" : L"" );
    printf( "        STATE              : %lu  %ls\n", status->dwCurrentState,
            service_state_string( status->dwCurrentState ) );
    if (status->dwCurrentState != SERVICE_STOPPED)
        printf( "                                (%ls, %ls, %ls)\n",
                status->dwControlsAccepted & SERVICE_ACCEPT_STOP ? L"STOPPABLE" : L"NOT_STOPPABLE",
                status->dwControlsAccepted & SERVICE_ACCEPT_PAUSE_CONTINUE ? L"PAUSABLE" : L"NOT_PAUSABLE",
                status->dwControlsAccepted & SERVICE_ACCEPT_SHUTDOWN ? L"ACCEPTS_SHUTDOWN" : L"IGNORES_SHUTDOWN" );
    printf( "        WIN32_EXIT_CODE    : %lu  (0x%lx)\n", status->dwWin32ExitCode, status->dwWin32ExitCode );
    printf( "        SERVICE_EXIT_CODE  : %lu  (0x%lx)\n", status->dwServiceSpecificExitCode,
            status->dwServiceSpecificExitCode );
    printf( "        CHECKPOINT         : 0x%lx\n", status->dwCheckPoint );
    printf( "        WAIT_HINT          : 0x%lx\n", status->dwWaitHint );
    if (ex)
    {
        printf( "        PID                : %lu\n", status->dwProcessId );
        printf( "        FLAGS              : %ls\n",
                status->dwServiceFlags & SERVICE_RUNS_IN_SYSTEM_PROCESS ? L"RUNS_IN_SYSTEM_PROCESS" : L"" );
    }
}

static BOOL get_status( SC_HANDLE service, SERVICE_STATUS_PROCESS *status )
{
    DWORD size;
    return QueryServiceStatusEx( service, SC_STATUS_PROCESS_INFO, (BYTE *)status, sizeof(*status), &size );
}

static DWORD query_service( SC_HANDLE manager, const WCHAR *name, BOOL ex )
{
    SERVICE_STATUS_PROCESS status;
    SC_HANDLE service;
    DWORD ret = ERROR_SUCCESS;

    if (!(service = OpenServiceW( manager, name, SERVICE_QUERY_STATUS )))
        return print_failure( "EnumQueryServicesStatus:OpenService", GetLastError() );
    if (get_status( service, &status )) print_status( name, &status, ex );
    else ret = print_failure( "QueryServiceStatus", GetLastError() );
    CloseServiceHandle( service );
    return ret;
}

static DWORD query_config( SC_HANDLE service, const WCHAR *name )
{
    QUERY_SERVICE_CONFIGW *config;
    const WCHAR *dep;
    DWORD size = 0;

    QueryServiceConfigW( service, NULL, 0, &size );
    if (!(config = malloc( size ))) return ERROR_NOT_ENOUGH_MEMORY;
    if (!QueryServiceConfigW( service, config, size, &size ))
    {
        free( config );
        return print_failure( "QueryServiceConfig", GetLastError() );
    }
    print_success( "QueryServiceConfig" );
    printf( "\nSERVICE_NAME: %ls\n", name );
    printf( "        TYPE               : %-3lx %ls%ls\n", config->dwServiceType,
            service_type_string( config->dwServiceType ),
            config->dwServiceType & SERVICE_INTERACTIVE_PROCESS ? L" (interactive)" : L"" );
    printf( "        START_TYPE         : %-3lu %ls\n", config->dwStartType, start_type_string( config->dwStartType ) );
    printf( "        ERROR_CONTROL      : %-3lu %ls\n", config->dwErrorControl,
            error_control_string( config->dwErrorControl ) );
    printf( "        BINARY_PATH_NAME   : %ls\n", config->lpBinaryPathName ? config->lpBinaryPathName : L"" );
    printf( "        LOAD_ORDER_GROUP   : %ls\n", config->lpLoadOrderGroup ? config->lpLoadOrderGroup : L"" );
    printf( "        TAG                : %lu\n", config->dwTagId );
    printf( "        DISPLAY_NAME       : %ls\n", config->lpDisplayName ? config->lpDisplayName : L"" );
    dep = config->lpDependencies;
    printf( "        DEPENDENCIES       : %ls\n", dep ? dep : L"" );
    if (dep && *dep)
        for (dep += wcslen( dep ) + 1; *dep; dep += wcslen( dep ) + 1)
            printf( "                           : %ls\n", dep );
    printf( "        SERVICE_START_NAME : %ls\n", config->lpServiceStartName ? config->lpServiceStartName : L"" );
    free( config );
    return ERROR_SUCCESS;
}

static BYTE *query_config2( SC_HANDLE service, DWORD level, DWORD *err )
{
    DWORD size = 0;
    BYTE *buf;

    *err = ERROR_SUCCESS;
    if (!QueryServiceConfig2W( service, level, NULL, 0, &size ) && GetLastError() != ERROR_INSUFFICIENT_BUFFER)
    {
        *err = GetLastError();
        return NULL;
    }
    if (!(buf = malloc( size )))
    {
        *err = ERROR_NOT_ENOUGH_MEMORY;
        return NULL;
    }
    if (!QueryServiceConfig2W( service, level, buf, size, &size ))
    {
        *err = GetLastError();
        free( buf );
        return NULL;
    }
    return buf;
}

static DWORD query_description( SC_HANDLE service, const WCHAR *name )
{
    SERVICE_DESCRIPTIONW *desc;
    DWORD err;

    if (!(desc = (SERVICE_DESCRIPTIONW *)query_config2( service, SERVICE_CONFIG_DESCRIPTION, &err )))
        return print_failure( "QueryServiceConfig2", err );
    print_success( "QueryServiceConfig2" );
    printf( "\nSERVICE_NAME: %ls\nDESCRIPTION:  %ls\n", name, desc->lpDescription ? desc->lpDescription : L"" );
    free( desc );
    return ERROR_SUCCESS;
}

static DWORD query_failure( SC_HANDLE service, const WCHAR *name )
{
    SERVICE_FAILURE_ACTIONSW *fa;
    DWORD err, i;
    BOOL first = TRUE;

    if (!(fa = (SERVICE_FAILURE_ACTIONSW *)query_config2( service, SERVICE_CONFIG_FAILURE_ACTIONS, &err )))
        return print_failure( "QueryServiceConfig2", err );
    print_success( "QueryServiceConfig2" );
    printf( "\nSERVICE_NAME: %ls\n", name );
    printf( "        RESET_PERIOD (in seconds)    : %lu\n", fa->dwResetPeriod );
    printf( "        REBOOT_MESSAGE               : %ls\n", fa->lpRebootMsg ? fa->lpRebootMsg : L"" );
    printf( "        COMMAND_LINE                 : %ls\n", fa->lpCommand ? fa->lpCommand : L"" );
    for (i = 0; i < fa->cActions; i++)
    {
        const char *action;

        switch (fa->lpsaActions[i].Type)
        {
        case SC_ACTION_RESTART: action = "RESTART"; break;
        case SC_ACTION_REBOOT: action = "REBOOT"; break;
        case SC_ACTION_RUN_COMMAND: action = "RUN PROCESS"; break;
        default: continue;
        }
        printf( "%s%s -- Delay = %lu milliseconds.\n",
                first ? "        FAILURE_ACTIONS              : " : "                                       ",
                action, fa->lpsaActions[i].Delay );
        first = FALSE;
    }
    printf( "\n" );
    free( fa );
    return ERROR_SUCCESS;
}

static DWORD change_config( SC_HANDLE service, int argc, const WCHAR *argv[] )
{
    struct create_params cp;

    parse_create_params( argc, argv, &cp );
    if (!ChangeServiceConfigW( service, cp.type_set ? cp.type : SERVICE_NO_CHANGE,
                               cp.start_set ? cp.start : SERVICE_NO_CHANGE,
                               cp.error_set ? cp.error : SERVICE_NO_CHANGE, cp.binpath, cp.group, NULL,
                               cp.depend, cp.obj, cp.password, cp.displayname ))
        return print_failure( "ChangeServiceConfig", GetLastError() );
    return print_success( "ChangeServiceConfig" );
}

static DWORD get_name( SC_HANDLE manager, const WCHAR *name, BOOL display )
{
    WCHAR buf[257];
    DWORD size = ARRAY_SIZE(buf);
    BOOL ret;

    if (display) ret = GetServiceDisplayNameW( manager, name, buf, &size );
    else ret = GetServiceKeyNameW( manager, name, buf, &size );
    if (!ret) return print_failure( display ? "GetServiceDisplayName" : "GetServiceKeyName", GetLastError() );
    print_success( display ? "GetServiceDisplayName" : "GetServiceKeyName" );
    printf( "Name = %ls\n", buf );
    return ERROR_SUCCESS;
}

int __cdecl wmain( int argc, const WCHAR *argv[] )
{
    SC_HANDLE manager, service;
    SERVICE_STATUS_PROCESS status;
    DWORD ret = ERROR_SUCCESS;
    const WCHAR *cmd;

    if (argc < 3)
    {
        if (argc == 2 && !wcsicmp( argv[1], L"create" )) create_usage();
        usage();
    }

    if (argv[2][0] == '\\' && argv[2][1] == '\\')
    {
        WINE_FIXME("server argument not supported\n");
        return ERROR_INVALID_COMMAND_LINE;
    }

    manager = OpenSCManagerW( NULL, NULL, SC_MANAGER_ALL_ACCESS );
    if (!manager) return print_failure( "OpenSCManager", GetLastError() );

    cmd = argv[1];
    if (!wcsicmp( cmd, L"create" ))
    {
        struct create_params cp;

        if (!parse_create_params( argc - 3, argv + 3, &cp ))
        {
            CloseServiceHandle( manager );
            create_usage();
        }
        service = CreateServiceW( manager, argv[2], cp.displayname, SERVICE_ALL_ACCESS,
                                  cp.type, cp.start, cp.error, cp.binpath, cp.group, NULL,
                                  cp.depend, cp.obj, cp.password );
        if (service)
        {
            CloseServiceHandle( service );
            ret = print_success( "CreateService" );
        }
        else ret = print_failure( "CreateService", GetLastError() );
    }
    else if (!wcsicmp( cmd, L"query" ) || !wcsicmp( cmd, L"queryex" ))
    {
        ret = query_service( manager, argv[2], !wcsicmp( cmd, L"queryex" ) );
    }
    else if (!wcsicmp( cmd, L"getdisplayname" ) || !wcsicmp( cmd, L"getkeyname" ))
    {
        ret = get_name( manager, argv[2], !wcsicmp( cmd, L"getdisplayname" ) );
    }
    else if (!wcsicmp( cmd, L"sdset" ))
    {
        WINE_FIXME("SdSet command not supported, faking success\n");
    }
    else
    {
        static const struct { const WCHAR *name; DWORD access; } commands[] =
        {
            { L"description", SERVICE_CHANGE_CONFIG },
            /* a restart action needs SERVICE_START */
            { L"failure", SERVICE_CHANGE_CONFIG | SERVICE_START },
            { L"config", SERVICE_CHANGE_CONFIG },
            { L"delete", DELETE },
            { L"start", SERVICE_START | SERVICE_QUERY_STATUS },
            { L"stop", SERVICE_STOP | SERVICE_QUERY_STATUS },
            { L"qc", SERVICE_QUERY_CONFIG },
            { L"qdescription", SERVICE_QUERY_CONFIG },
            { L"qfailure", SERVICE_QUERY_CONFIG },
        };
        unsigned int i;

        for (i = 0; i < ARRAY_SIZE(commands); i++)
            if (!wcsicmp( cmd, commands[i].name )) break;
        if (i == ARRAY_SIZE(commands))
        {
            WINE_FIXME("command not supported\n");
            CloseServiceHandle( manager );
            return ERROR_INVALID_COMMAND_LINE;
        }
        if (!(service = OpenServiceW( manager, argv[2], commands[i].access )))
        {
            ret = print_failure( "OpenService", GetLastError() );
            CloseServiceHandle( manager );
            return ret;
        }

        if (!wcsicmp( cmd, L"description" ))
        {
            SERVICE_DESCRIPTIONW sd;
            sd.lpDescription = argc > 3 ? (WCHAR *)argv[3] : (WCHAR *)L"";
            if (!ChangeServiceConfig2W( service, SERVICE_CONFIG_DESCRIPTION, &sd ))
                ret = print_failure( "ChangeServiceConfig2", GetLastError() );
            else ret = print_success( "ChangeServiceConfig2" );
        }
        else if (!wcsicmp( cmd, L"failure" ))
        {
            SERVICE_FAILURE_ACTIONSW sfa;
            if (parse_failure_params( argc - 3, argv + 3, &sfa ))
            {
                if (!ChangeServiceConfig2W( service, SERVICE_CONFIG_FAILURE_ACTIONS, &sfa ))
                    ret = print_failure( "ChangeServiceConfig2", GetLastError() );
                else ret = print_success( "ChangeServiceConfig2" );
                free( sfa.lpsaActions );
            }
            else ret = ERROR_INVALID_COMMAND_LINE;
        }
        else if (!wcsicmp( cmd, L"config" ))
            ret = change_config( service, argc - 3, argv + 3 );
        else if (!wcsicmp( cmd, L"delete" ))
        {
            if (!DeleteService( service )) ret = print_failure( "DeleteService", GetLastError() );
            else ret = print_success( "DeleteService" );
        }
        else if (!wcsicmp( cmd, L"start" ))
        {
            if (!StartServiceW( service, argc - 3, argv + 3 )) ret = print_failure( "StartService", GetLastError() );
            else if (get_status( service, &status )) print_status( argv[2], &status, TRUE );
        }
        else if (!wcsicmp( cmd, L"stop" ))
        {
            SERVICE_STATUS st;
            if (!ControlService( service, SERVICE_CONTROL_STOP, &st ))
                ret = print_failure( "ControlService", GetLastError() );
            else if (get_status( service, &status )) print_status( argv[2], &status, FALSE );
        }
        else if (!wcsicmp( cmd, L"qc" ))
            ret = query_config( service, argv[2] );
        else if (!wcsicmp( cmd, L"qdescription" ))
            ret = query_description( service, argv[2] );
        else if (!wcsicmp( cmd, L"qfailure" ))
            ret = query_failure( service, argv[2] );

        CloseServiceHandle( service );
    }

    CloseServiceHandle( manager );
    return ret;
}
