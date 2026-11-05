/*
 * Security API
 *
 * Copyright 2003 CodeWeavers Inc. (Ulrich Czekalla)
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

#include <assert.h>
#include <stdarg.h>

#define WINADVAPI
#include "windef.h"
#include "winbase.h"
#include "sddl.h"
#include "iads.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(security);

static const struct
{
    WCHAR str[3];
    DWORD value;
}
ace_rights[] =
{
    { L"GA", GENERIC_ALL },
    { L"GR", GENERIC_READ },
    { L"GW", GENERIC_WRITE },
    { L"GX", GENERIC_EXECUTE },

    { L"RC", READ_CONTROL },
    { L"SD", DELETE },
    { L"WD", WRITE_DAC },
    { L"WO", WRITE_OWNER },

    { L"RP", ADS_RIGHT_DS_READ_PROP },
    { L"WP", ADS_RIGHT_DS_WRITE_PROP },
    { L"CC", ADS_RIGHT_DS_CREATE_CHILD },
    { L"DC", ADS_RIGHT_DS_DELETE_CHILD },
    { L"LC", ADS_RIGHT_ACTRL_DS_LIST },
    { L"SW", ADS_RIGHT_DS_SELF },
    { L"LO", ADS_RIGHT_DS_LIST_OBJECT },
    { L"DT", ADS_RIGHT_DS_DELETE_TREE },
    { L"CR", ADS_RIGHT_DS_CONTROL_ACCESS },

    { L"FA", FILE_ALL_ACCESS },
    { L"FR", FILE_GENERIC_READ },
    { L"FW", FILE_GENERIC_WRITE },
    { L"FX", FILE_GENERIC_EXECUTE },

    { L"KA", KEY_ALL_ACCESS },
    { L"KR", KEY_READ },
    { L"KW", KEY_WRITE },
    { L"KX", KEY_EXECUTE },

    { L"NR", SYSTEM_MANDATORY_LABEL_NO_READ_UP },
    { L"NW", SYSTEM_MANDATORY_LABEL_NO_WRITE_UP },
    { L"NX", SYSTEM_MANDATORY_LABEL_NO_EXECUTE_UP },
};

static const struct
{
    const WCHAR *str;
    BYTE value;
}
ace_types[] =
{
    { L"AL", SYSTEM_ALARM_ACE_TYPE },
    { L"AU", SYSTEM_AUDIT_ACE_TYPE },
    { L"OA", ACCESS_ALLOWED_OBJECT_ACE_TYPE },
    { L"OD", ACCESS_DENIED_OBJECT_ACE_TYPE },
    { L"OU", ACCESS_AUDIT_OBJECT_ACE_TYPE },
    { L"OL", ACCESS_ALARM_OBJECT_ACE_TYPE },
    { L"ML", SYSTEM_MANDATORY_LABEL_ACE_TYPE },
    { L"A",  ACCESS_ALLOWED_ACE_TYPE },
    { L"D",  ACCESS_DENIED_ACE_TYPE },
};

struct max_sid
{
    /* same fields as struct _SID */
    BYTE Revision;
    BYTE SubAuthorityCount;
    SID_IDENTIFIER_AUTHORITY IdentifierAuthority;
    DWORD SubAuthority[SID_MAX_SUB_AUTHORITIES];
};

static const struct
{
    WCHAR str[2];
    WELL_KNOWN_SID_TYPE Type;
    struct max_sid sid;
}
well_known_sids[] =
{
    { {0,0}, WinNullSid, { SID_REVISION, 1, { SECURITY_NULL_SID_AUTHORITY }, { SECURITY_NULL_RID } } },
    { {'W','D'}, WinWorldSid, { SID_REVISION, 1, { SECURITY_WORLD_SID_AUTHORITY }, { SECURITY_WORLD_RID } } },
    { {0,0}, WinLocalSid, { SID_REVISION, 1, { SECURITY_LOCAL_SID_AUTHORITY }, { SECURITY_LOCAL_RID } } },
    { {'C','O'}, WinCreatorOwnerSid, { SID_REVISION, 1, { SECURITY_CREATOR_SID_AUTHORITY }, { SECURITY_CREATOR_OWNER_RID } } },
    { {'C','G'}, WinCreatorGroupSid, { SID_REVISION, 1, { SECURITY_CREATOR_SID_AUTHORITY }, { SECURITY_CREATOR_GROUP_RID } } },
    { {'O','W'}, WinCreatorOwnerRightsSid, { SID_REVISION, 1, { SECURITY_CREATOR_SID_AUTHORITY }, { SECURITY_CREATOR_OWNER_RIGHTS_RID } } },
    { {0,0}, WinCreatorOwnerServerSid, { SID_REVISION, 1, { SECURITY_CREATOR_SID_AUTHORITY }, { SECURITY_CREATOR_OWNER_SERVER_RID } } },
    { {0,0}, WinCreatorGroupServerSid, { SID_REVISION, 1, { SECURITY_CREATOR_SID_AUTHORITY }, { SECURITY_CREATOR_GROUP_SERVER_RID } } },
    { {0,0}, WinNtAuthoritySid, { SID_REVISION, 0, { SECURITY_NT_AUTHORITY }, { SECURITY_NULL_RID } } },
    { {0,0}, WinDialupSid, { SID_REVISION, 1, { SECURITY_NT_AUTHORITY }, { SECURITY_DIALUP_RID } } },
    { {'N','U'}, WinNetworkSid, { SID_REVISION, 1, { SECURITY_NT_AUTHORITY }, { SECURITY_NETWORK_RID } } },
    { {0,0}, WinBatchSid, { SID_REVISION, 1, { SECURITY_NT_AUTHORITY }, { SECURITY_BATCH_RID } } },
    { {'I','U'}, WinInteractiveSid, { SID_REVISION, 1, { SECURITY_NT_AUTHORITY }, { SECURITY_INTERACTIVE_RID } } },
    { {'S','U'}, WinServiceSid, { SID_REVISION, 1, { SECURITY_NT_AUTHORITY }, { SECURITY_SERVICE_RID } } },
    { {'A','N'}, WinAnonymousSid, { SID_REVISION, 1, { SECURITY_NT_AUTHORITY }, { SECURITY_ANONYMOUS_LOGON_RID } } },
    { {0,0}, WinProxySid, { SID_REVISION, 1, { SECURITY_NT_AUTHORITY }, { SECURITY_PROXY_RID } } },
    { {'E','D'}, WinEnterpriseControllersSid, { SID_REVISION, 1, { SECURITY_NT_AUTHORITY }, { SECURITY_ENTERPRISE_CONTROLLERS_RID } } },
    { {'P','S'}, WinSelfSid, { SID_REVISION, 1, { SECURITY_NT_AUTHORITY }, { SECURITY_PRINCIPAL_SELF_RID } } },
    { {'A','U'}, WinAuthenticatedUserSid, { SID_REVISION, 1, { SECURITY_NT_AUTHORITY }, { SECURITY_AUTHENTICATED_USER_RID } } },
    { {'R','C'}, WinRestrictedCodeSid, { SID_REVISION, 1, { SECURITY_NT_AUTHORITY }, { SECURITY_RESTRICTED_CODE_RID } } },
    { {0,0}, WinTerminalServerSid, { SID_REVISION, 1, { SECURITY_NT_AUTHORITY }, { SECURITY_TERMINAL_SERVER_RID } } },
    { {0,0}, WinRemoteLogonIdSid, { SID_REVISION, 1, { SECURITY_NT_AUTHORITY }, { SECURITY_REMOTE_LOGON_RID } } },
    { {0,0}, WinLogonIdsSid, { SID_REVISION, SECURITY_LOGON_IDS_RID_COUNT, { SECURITY_NT_AUTHORITY }, { SECURITY_LOGON_IDS_RID } } },
    { {'S','Y'}, WinLocalSystemSid, { SID_REVISION, 1, { SECURITY_NT_AUTHORITY }, { SECURITY_LOCAL_SYSTEM_RID } } },
    { {'L','S'}, WinLocalServiceSid, { SID_REVISION, 1, { SECURITY_NT_AUTHORITY }, { SECURITY_LOCAL_SERVICE_RID } } },
    { {'N','S'}, WinNetworkServiceSid, { SID_REVISION, 1, { SECURITY_NT_AUTHORITY }, { SECURITY_NETWORK_SERVICE_RID } } },
    { {0,0}, WinBuiltinDomainSid, { SID_REVISION, 1, { SECURITY_NT_AUTHORITY }, { SECURITY_BUILTIN_DOMAIN_RID } } },
    { {'B','A'}, WinBuiltinAdministratorsSid, { SID_REVISION, 2, { SECURITY_NT_AUTHORITY }, { SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS } } },
    { {'B','U'}, WinBuiltinUsersSid, { SID_REVISION, 2, { SECURITY_NT_AUTHORITY }, { SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_USERS } } },
    { {'B','G'}, WinBuiltinGuestsSid, { SID_REVISION, 2, { SECURITY_NT_AUTHORITY }, { SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_GUESTS } } },
    { {'P','U'}, WinBuiltinPowerUsersSid, { SID_REVISION, 2, { SECURITY_NT_AUTHORITY }, { SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_POWER_USERS } } },
    { {'A','O'}, WinBuiltinAccountOperatorsSid, { SID_REVISION, 2, { SECURITY_NT_AUTHORITY }, { SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ACCOUNT_OPS } } },
    { {'S','O'}, WinBuiltinSystemOperatorsSid, { SID_REVISION, 2, { SECURITY_NT_AUTHORITY }, { SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_SYSTEM_OPS } } },
    { {'P','O'}, WinBuiltinPrintOperatorsSid, { SID_REVISION, 2, { SECURITY_NT_AUTHORITY }, { SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_PRINT_OPS } } },
    { {'B','O'}, WinBuiltinBackupOperatorsSid, { SID_REVISION, 2, { SECURITY_NT_AUTHORITY }, { SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_BACKUP_OPS } } },
    { {'R','E'}, WinBuiltinReplicatorSid, { SID_REVISION, 2, { SECURITY_NT_AUTHORITY }, { SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_REPLICATOR } } },
    { {'R','U'}, WinBuiltinPreWindows2000CompatibleAccessSid, { SID_REVISION, 2, { SECURITY_NT_AUTHORITY }, { SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_PREW2KCOMPACCESS } } },
    { {'R','D'}, WinBuiltinRemoteDesktopUsersSid, { SID_REVISION, 2, { SECURITY_NT_AUTHORITY }, { SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_REMOTE_DESKTOP_USERS } } },
    { {'N','O'}, WinBuiltinNetworkConfigurationOperatorsSid, { SID_REVISION, 2, { SECURITY_NT_AUTHORITY }, { SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_NETWORK_CONFIGURATION_OPS } } },
    { {0,0}, WinNTLMAuthenticationSid, { SID_REVISION, 2, { SECURITY_NT_AUTHORITY }, { SECURITY_PACKAGE_BASE_RID, SECURITY_PACKAGE_NTLM_RID } } },
    { {0,0}, WinDigestAuthenticationSid, { SID_REVISION, 2, { SECURITY_NT_AUTHORITY }, { SECURITY_PACKAGE_BASE_RID, SECURITY_PACKAGE_DIGEST_RID } } },
    { {0,0}, WinSChannelAuthenticationSid, { SID_REVISION, 2, { SECURITY_NT_AUTHORITY }, { SECURITY_PACKAGE_BASE_RID, SECURITY_PACKAGE_SCHANNEL_RID } } },
    { {0,0}, WinThisOrganizationSid, { SID_REVISION, 1, { SECURITY_NT_AUTHORITY }, { SECURITY_THIS_ORGANIZATION_RID } } },
    { {0,0}, WinOtherOrganizationSid, { SID_REVISION, 1, { SECURITY_NT_AUTHORITY }, { SECURITY_OTHER_ORGANIZATION_RID } } },
    { {0,0}, WinBuiltinIncomingForestTrustBuildersSid, { SID_REVISION, 2, { SECURITY_NT_AUTHORITY }, { SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_INCOMING_FOREST_TRUST_BUILDERS  } } },
    { {0,0}, WinBuiltinPerfMonitoringUsersSid, { SID_REVISION, 2, { SECURITY_NT_AUTHORITY }, { SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_MONITORING_USERS } } },
    { {0,0}, WinBuiltinPerfLoggingUsersSid, { SID_REVISION, 2, { SECURITY_NT_AUTHORITY }, { SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_LOGGING_USERS } } },
    { {0,0}, WinBuiltinAuthorizationAccessSid, { SID_REVISION, 2, { SECURITY_NT_AUTHORITY }, { SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_AUTHORIZATIONACCESS } } },
    { {0,0}, WinBuiltinTerminalServerLicenseServersSid, { SID_REVISION, 2, { SECURITY_NT_AUTHORITY }, { SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_TS_LICENSE_SERVERS } } },
    { {0,0}, WinBuiltinDCOMUsersSid, { SID_REVISION, 2, { SECURITY_NT_AUTHORITY }, { SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_DCOM_USERS } } },
    { {'L','W'}, WinLowLabelSid, { SID_REVISION, 1, { SECURITY_MANDATORY_LABEL_AUTHORITY}, { SECURITY_MANDATORY_LOW_RID} } },
    { {'M','E'}, WinMediumLabelSid, { SID_REVISION, 1, { SECURITY_MANDATORY_LABEL_AUTHORITY}, { SECURITY_MANDATORY_MEDIUM_RID } } },
    { {'H','I'}, WinHighLabelSid, { SID_REVISION, 1, { SECURITY_MANDATORY_LABEL_AUTHORITY}, { SECURITY_MANDATORY_HIGH_RID } } },
    { {'S','I'}, WinSystemLabelSid, { SID_REVISION, 1, { SECURITY_MANDATORY_LABEL_AUTHORITY}, { SECURITY_MANDATORY_SYSTEM_RID } } },
    { {'A','C'}, WinBuiltinAnyPackageSid, { SID_REVISION, 2, { SECURITY_APP_PACKAGE_AUTHORITY }, { SECURITY_APP_PACKAGE_BASE_RID, SECURITY_BUILTIN_PACKAGE_ANY_PACKAGE } } },
};

/* these SIDs must be constructed as relative to some domain - only the RID is well-known */
static const struct
{
    WCHAR str[2];
    WELL_KNOWN_SID_TYPE type;
    DWORD rid;
}
well_known_rids[] =
{
    { {'L','A'}, WinAccountAdministratorSid,    DOMAIN_USER_RID_ADMIN },
    { {'L','G'}, WinAccountGuestSid,            DOMAIN_USER_RID_GUEST },
    { {0,0},     WinAccountKrbtgtSid,           DOMAIN_USER_RID_KRBTGT },
    { {'D','A'}, WinAccountDomainAdminsSid,     DOMAIN_GROUP_RID_ADMINS },
    { {'D','U'}, WinAccountDomainUsersSid,      DOMAIN_GROUP_RID_USERS },
    { {'D','G'}, WinAccountDomainGuestsSid,     DOMAIN_GROUP_RID_GUESTS },
    { {'D','C'}, WinAccountComputersSid,        DOMAIN_GROUP_RID_COMPUTERS },
    { {'D','D'}, WinAccountControllersSid,      DOMAIN_GROUP_RID_CONTROLLERS },
    { {'C','A'}, WinAccountCertAdminsSid,       DOMAIN_GROUP_RID_CERT_ADMINS },
    { {'S','A'}, WinAccountSchemaAdminsSid,     DOMAIN_GROUP_RID_SCHEMA_ADMINS },
    { {'E','A'}, WinAccountEnterpriseAdminsSid, DOMAIN_GROUP_RID_ENTERPRISE_ADMINS },
    { {'P','A'}, WinAccountPolicyAdminsSid,     DOMAIN_GROUP_RID_POLICY_ADMINS },
    { {'R','S'}, WinAccountRasAndIasServersSid, DOMAIN_ALIAS_RID_RAS_SERVERS },
};


static void print_string(const WCHAR *string, int cch, WCHAR **pwptr, ULONG *plen)
{
    if (cch == -1)
        cch = wcslen(string);

    if (plen)
        *plen += cch;

    if (pwptr)
    {
        memcpy(*pwptr, string, sizeof(WCHAR)*cch);
        *pwptr += cch;
    }
}

static BOOL print_sid_numeric(PSID psid, WCHAR **pwptr, ULONG *plen)
{
    DWORD i;
    WCHAR buf[26];
    SID *pisid = psid;
    ULONGLONG authority;

    if( !IsValidSid( psid ) || pisid->Revision != SDDL_REVISION)
    {
        SetLastError(ERROR_INVALID_SID);
        return FALSE;
    }

    authority = 0;
    for (i = 0; i < 6; i++)
        authority = (authority << 8) | pisid->IdentifierAuthority.Value[i];

    /* authorities of 2^32 and above are printed in hexadecimal */
    if (authority >> 32)
        swprintf( buf, ARRAY_SIZE(buf), L"S-%u-0x%I64X", pisid->Revision, authority );
    else
        swprintf( buf, ARRAY_SIZE(buf), L"S-%u-%u", pisid->Revision, (DWORD)authority );
    print_string(buf, -1, pwptr, plen);

    for( i=0; i<pisid->SubAuthorityCount; i++ )
    {
        swprintf( buf, ARRAY_SIZE(buf), L"-%u", pisid->SubAuthority[i] );
        print_string(buf, -1, pwptr, plen);
    }
    return TRUE;
}

/******************************************************************************
 *     ConvertSidToStringSidW   (sechost.@)
 */
BOOL WINAPI DECLSPEC_HOTPATCH ConvertSidToStringSidW( PSID sid, WCHAR **pstr )
{
    DWORD len = 0;
    WCHAR *wstr, *wptr;

    TRACE("%p %p\n", sid, pstr );

    len = 0;
    if (!print_sid_numeric( sid, NULL, &len ))
        return FALSE;
    wstr = wptr = LocalAlloc( 0, (len + 1) * sizeof(WCHAR) );
    print_sid_numeric( sid, &wptr, NULL );
    *wptr = 0;

    *pstr = wstr;
    return TRUE;
}

static BOOL print_sid(PSID psid, WCHAR **pwptr, ULONG *plen)
{
    size_t i;
    for (i = 0; i < ARRAY_SIZE(well_known_sids); i++)
    {
        if (well_known_sids[i].str[0] && EqualSid(psid, (PSID)&well_known_sids[i].sid.Revision))
        {
            print_string(well_known_sids[i].str, 2, pwptr, plen);
            return TRUE;
        }
    }

    return print_sid_numeric(psid, pwptr, plen);
}

static void print_rights(DWORD mask, WCHAR **pwptr, ULONG *plen)
{
    static const WCHAR *bit_names[32] =
    {
            L"CC",  /*  0 */
            L"DC",
            L"LC",
            L"SW",
            L"RP",  /*  4 */
            L"WP",
            L"DT",
            L"LO",
            L"CR",  /*  8 */
            NULL,
            NULL,
            NULL,
            NULL,   /* 12 */
            NULL,
            NULL,
            NULL,
            L"SD",  /* 16 */
            L"RC",
            L"WD",
            L"WO",
            NULL,   /* 20 */
            NULL,
            NULL,
            NULL,
            NULL,   /* 24 */
            NULL,
            NULL,
            NULL,
            L"GA",  /* 28 */
            L"GX",
            L"GW",
            L"GR",
    };

    WCHAR buf[15];
    size_t i;

    if (mask == 0)
        return;

    /* first check if the right have name */
    for (i = 0; i < ARRAY_SIZE(ace_rights); i++)
    {
        if (mask == ace_rights[i].value)
        {
            print_string(ace_rights[i].str, -1, pwptr, plen);
            return;
        }
    }

    /* then check if it can be built from bit names */
    for (i = 0; i < 32; i++)
    {
        if ((mask & (1 << i)) && !bit_names[i])
        {
            /* can't be built from bit names */
            swprintf(buf, ARRAY_SIZE(buf), L"0x%x", mask);
            print_string(buf, -1, pwptr, plen);
            return;
        }
    }

    /* build from bit names */
    for (i = 0; i < 32; i++)
        if (mask & (1 << i))
            print_string(bit_names[i], -1, pwptr, plen);
}

/* mandatory label ACEs use their own names for the low three bits */
static void print_label_rights(DWORD mask, WCHAR **pwptr, ULONG *plen)
{
    static const WCHAR *names[3] = { L"NW", L"NR", L"NX" };
    WCHAR buf[15];
    unsigned int i;

    if (mask & ~7)
    {
        swprintf(buf, ARRAY_SIZE(buf), L"0x%x", mask);
        print_string(buf, -1, pwptr, plen);
        return;
    }
    for (i = 0; i < 3; i++)
        if (mask & (1 << i))
            print_string(names[i], -1, pwptr, plen);
}

static void print_guid(const GUID *guid, WCHAR **pwptr, ULONG *plen)
{
    WCHAR buf[37];

    swprintf(buf, ARRAY_SIZE(buf), L"%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             guid->Data1, guid->Data2, guid->Data3, guid->Data4[0], guid->Data4[1], guid->Data4[2],
             guid->Data4[3], guid->Data4[4], guid->Data4[5], guid->Data4[6], guid->Data4[7]);
    print_string(buf, -1, pwptr, plen);
}

static inline BOOL is_object_ace(BYTE type)
{
    switch (type)
    {
    case ACCESS_ALLOWED_OBJECT_ACE_TYPE:
    case ACCESS_DENIED_OBJECT_ACE_TYPE:
    case ACCESS_AUDIT_OBJECT_ACE_TYPE:
    case ACCESS_ALARM_OBJECT_ACE_TYPE:
    case ACCESS_ALLOWED_CALLBACK_OBJECT_ACE_TYPE:
    case ACCESS_DENIED_CALLBACK_OBJECT_ACE_TYPE:
    case SYSTEM_AUDIT_CALLBACK_OBJECT_ACE_TYPE:
    case SYSTEM_ALARM_CALLBACK_OBJECT_ACE_TYPE:
        return TRUE;

    default:
        return FALSE;
    }
}

static BOOL print_ace(void *pace, WCHAR **pwptr, ULONG *plen)
{
    ACCESS_ALLOWED_ACE *piace; /* all the supported ACEs have the same memory layout */
    DWORD *sid_start;
    unsigned int i;
    BYTE type;

    piace = pace;

    if (piace->Header.AceType > ACCESS_MAX_MS_V5_ACE_TYPE || piace->Header.AceSize < sizeof(ACCESS_ALLOWED_ACE))
    {
        SetLastError(ERROR_INVALID_ACL);
        return FALSE;
    }

    type = piace->Header.AceType;
    /* an object ACE without object types is printed as the plain ACE type */
    if (is_object_ace(type) && !((ACCESS_ALLOWED_OBJECT_ACE *)pace)->Flags)
    {
        switch (type)
        {
        case ACCESS_ALLOWED_OBJECT_ACE_TYPE: type = ACCESS_ALLOWED_ACE_TYPE; break;
        case ACCESS_DENIED_OBJECT_ACE_TYPE: type = ACCESS_DENIED_ACE_TYPE; break;
        case ACCESS_AUDIT_OBJECT_ACE_TYPE: type = SYSTEM_AUDIT_ACE_TYPE; break;
        case ACCESS_ALARM_OBJECT_ACE_TYPE: type = SYSTEM_ALARM_ACE_TYPE; break;
        }
    }

    print_string(L"(", -1, pwptr, plen);
    for (i = 0; i < ARRAY_SIZE(ace_types); i++)
    {
        if (ace_types[i].value == type)
        {
            print_string(ace_types[i].str, -1, pwptr, plen);
            break;
        }
    }
    print_string(L";", -1, pwptr, plen);

    if (piace->Header.AceFlags & OBJECT_INHERIT_ACE)
        print_string(L"OI", -1, pwptr, plen);
    if (piace->Header.AceFlags & CONTAINER_INHERIT_ACE)
        print_string(L"CI", -1, pwptr, plen);
    if (piace->Header.AceFlags & NO_PROPAGATE_INHERIT_ACE)
        print_string(L"NP", -1, pwptr, plen);
    if (piace->Header.AceFlags & INHERIT_ONLY_ACE)
        print_string(L"IO", -1, pwptr, plen);
    if (piace->Header.AceFlags & INHERITED_ACE)
        print_string(L"ID", -1, pwptr, plen);
    if (piace->Header.AceFlags & SUCCESSFUL_ACCESS_ACE_FLAG)
        print_string(L"SA", -1, pwptr, plen);
    if (piace->Header.AceFlags & FAILED_ACCESS_ACE_FLAG)
        print_string(L"FA", -1, pwptr, plen);
    print_string(L";", -1, pwptr, plen);
    if (piace->Header.AceType == SYSTEM_MANDATORY_LABEL_ACE_TYPE)
        print_label_rights(piace->Mask, pwptr, plen);
    else
        print_rights(piace->Mask, pwptr, plen);
    print_string(L";", -1, pwptr, plen);
    sid_start = &piace->SidStart;
    if (is_object_ace(piace->Header.AceType))
    {
        ACCESS_ALLOWED_OBJECT_ACE *objace = pace;
        const GUID *guid = &objace->ObjectType;

        sid_start++; /* Flags */
        if (objace->Flags & ACE_OBJECT_TYPE_PRESENT)
        {
            print_guid(guid++, pwptr, plen);
            sid_start += sizeof(GUID) / sizeof(*sid_start); /* ObjectType */
        }
        print_string(L";", -1, pwptr, plen);
        if (objace->Flags & ACE_INHERITED_OBJECT_TYPE_PRESENT)
        {
            print_guid(guid, pwptr, plen);
            sid_start += sizeof(GUID) / sizeof(*sid_start); /* InheritedObjectType */
        }
        print_string(L";", -1, pwptr, plen);
    }
    else
        print_string(L";;", -1, pwptr, plen);
    if (!print_sid(sid_start, pwptr, plen))
        return FALSE;
    print_string(L")", -1, pwptr, plen);
    return TRUE;
}

static BOOL print_acl(ACL *pacl, WCHAR **pwptr, ULONG *plen, SECURITY_DESCRIPTOR_CONTROL control)
{
    WORD count;
    UINT i;

    if (control & SE_DACL_PROTECTED)
        print_string(L"P", -1, pwptr, plen);
    if (control & SE_DACL_AUTO_INHERIT_REQ)
        print_string(L"AR", -1, pwptr, plen);
    if (control & SE_DACL_AUTO_INHERITED)
        print_string(L"AI", -1, pwptr, plen);

    if (pacl == NULL)
    {
        print_string(L"NO_ACCESS_CONTROL", -1, pwptr, plen);
        return TRUE;
    }

    if (!IsValidAcl(pacl))
        return FALSE;

    count = pacl->AceCount;
    for (i = 0; i < count; i++)
    {
        void *ace;
        if (!GetAce(pacl, i, &ace))
            return FALSE;
        if (!print_ace(ace, pwptr, plen))
            return FALSE;
    }

    return TRUE;
}

static BOOL print_owner(PSECURITY_DESCRIPTOR sd, WCHAR **pwptr, ULONG *plen)
{
    BOOL defaulted;
    PSID psid;

    if (!GetSecurityDescriptorOwner(sd, &psid, &defaulted))
        return FALSE;

    if (psid == NULL)
        return TRUE;

    print_string(L"O:", -1, pwptr, plen);
    if (!print_sid(psid, pwptr, plen))
        return FALSE;
    return TRUE;
}

static BOOL print_group(PSECURITY_DESCRIPTOR sd, WCHAR **pwptr, ULONG *plen)
{
    BOOL defaulted;
    PSID psid;

    if (!GetSecurityDescriptorGroup(sd, &psid, &defaulted))
        return FALSE;

    if (psid == NULL)
        return TRUE;

    print_string(L"G:", -1, pwptr, plen);
    if (!print_sid(psid, pwptr, plen))
        return FALSE;
    return TRUE;
}

static BOOL print_dacl(PSECURITY_DESCRIPTOR sd, WCHAR **pwptr, ULONG *plen)
{
    SECURITY_DESCRIPTOR_CONTROL control;
    BOOL present, defaulted;
    DWORD revision;
    ACL *pacl;

    if (!GetSecurityDescriptorDacl(sd, &present, &pacl, &defaulted))
        return FALSE;

    if (!GetSecurityDescriptorControl(sd, &control, &revision))
        return FALSE;

    if (!present)
        return TRUE;

    print_string(L"D:", -1, pwptr, plen);
    if (!print_acl(pacl, pwptr, plen, control))
        return FALSE;
    return TRUE;
}

static BOOL print_sacl(PSECURITY_DESCRIPTOR sd, WCHAR **pwptr, ULONG *plen)
{
    SECURITY_DESCRIPTOR_CONTROL control;
    BOOL present, defaulted;
    DWORD revision;
    ACL *pacl;

    if (!GetSecurityDescriptorSacl(sd, &present, &pacl, &defaulted))
        return FALSE;

    if (!GetSecurityDescriptorControl(sd, &control, &revision))
        return FALSE;

    if (!present)
        return TRUE;

    print_string(L"S:", -1, pwptr, plen);
    /* the SE_SACL_* flags are the SE_DACL_* flags shifted left by one */
    if (!print_acl(pacl, pwptr, plen, control >> 1))
        return FALSE;
    return TRUE;
}

/******************************************************************************
 *     ConvertSecurityDescriptorToStringSecurityDescriptorW   (sechost.@)
 */
BOOL WINAPI DECLSPEC_HOTPATCH ConvertSecurityDescriptorToStringSecurityDescriptorW( PSECURITY_DESCRIPTOR sd,
        DWORD revision, SECURITY_INFORMATION flags, WCHAR **string, ULONG *ret_len )
{
    ULONG len = 0;
    WCHAR *wptr, *wstr;

    if (revision != SDDL_REVISION_1)
    {
        ERR("Unhandled SDDL revision %ld\n", revision);
        SetLastError( ERROR_UNKNOWN_REVISION );
        return FALSE;
    }

    if ((flags & OWNER_SECURITY_INFORMATION) && !print_owner(sd, NULL, &len))
        return FALSE;
    if ((flags & GROUP_SECURITY_INFORMATION) && !print_group(sd, NULL, &len))
        return FALSE;
    if ((flags & DACL_SECURITY_INFORMATION) && !print_dacl(sd, NULL, &len))
        return FALSE;
    if ((flags & (SACL_SECURITY_INFORMATION | LABEL_SECURITY_INFORMATION)) && !print_sacl(sd, NULL, &len))
        return FALSE;

    wstr = wptr = LocalAlloc( 0, (len + 1) * sizeof(WCHAR) );
    if ((flags & OWNER_SECURITY_INFORMATION) && !print_owner(sd, &wptr, NULL))
    {
        LocalFree(wstr);
        return FALSE;
    }
    if ((flags & GROUP_SECURITY_INFORMATION) && !print_group(sd, &wptr, NULL))
    {
        LocalFree(wstr);
        return FALSE;
    }
    if ((flags & DACL_SECURITY_INFORMATION) && !print_dacl(sd, &wptr, NULL))
    {
        LocalFree(wstr);
        return FALSE;
    }
    if ((flags & (SACL_SECURITY_INFORMATION | LABEL_SECURITY_INFORMATION)) && !print_sacl(sd, &wptr, NULL))
    {
        LocalFree(wstr);
        return FALSE;
    }
    *wptr = 0;

    TRACE("ret: %s, %ld\n", wine_dbgstr_w(wstr), len);
    *string = wstr;
    if (ret_len) *ret_len = wcslen(*string) + 1;
    return TRUE;
}

static BOOL get_computer_sid( PSID sid )
{
    static const struct /* same fields as struct SID */
    {
        BYTE Revision;
        BYTE SubAuthorityCount;
        SID_IDENTIFIER_AUTHORITY IdentifierAuthority;
        DWORD SubAuthority[4];
    } computer_sid =
    { SID_REVISION, 4, { SECURITY_NT_AUTHORITY }, { SECURITY_NT_NON_UNIQUE, 0, 0, 0 } };

    memcpy( sid, &computer_sid, sizeof(computer_sid) );
    return TRUE;
}

/* Parse one decimal or "0x" hexadecimal number of a numeric SID string.
 * Values that do not fit in 64 bits are returned as ~0. */
static BOOL parse_sid_number( const WCHAR **string, ULONGLONG *result )
{
    const WCHAR *p = *string;
    ULONGLONG value = 0;
    BOOL overflow = FALSE;
    unsigned int digits = 0, d;

    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
    {
        for (p += 2;; p++, digits++)
        {
            if (*p >= '0' && *p <= '9') d = *p - '0';
            else if (*p >= 'a' && *p <= 'f') d = *p - 'a' + 10;
            else if (*p >= 'A' && *p <= 'F') d = *p - 'A' + 10;
            else break;
            if (value >> 60) overflow = TRUE;
            value = value * 16 + d;
        }
    }
    else
    {
        for (;; p++, digits++)
        {
            if (*p >= '0' && *p <= '9') d = *p - '0';
            else break;
            if (value > (~(ULONGLONG)0 - d) / 10) overflow = TRUE;
            value = value * 10 + d;
        }
    }
    if (!digits) return FALSE;
    *result = overflow ? ~(ULONGLONG)0 : value;
    *string = p;
    return TRUE;
}

/* S-R-I-S[-S...]: the revision must fit in a byte, the identifier authority
 * in 48 bits, and each '-' must be followed by a number. Sub-authorities
 * that do not fit in 32 bits are clamped to 0xffffffff. */
static BOOL parse_numeric_sid( const WCHAR *string, const WCHAR **end, SID *pisid, DWORD *size )
{
    const WCHAR *p = string + 2;
    ULONGLONG revision, authority, value;
    DWORD count = 0;

    if (!parse_sid_number( &p, &revision ) || revision > 0xff || *p != '-')
        goto bad;
    p++;
    if (!parse_sid_number( &p, &authority ) || authority > 0xffffffffffff)
        goto bad;
    while (*p == '-')
    {
        p++;
        if (!parse_sid_number( &p, &value ) || count == 0xff)
            goto bad;
        if (pisid) pisid->SubAuthority[count] = value > 0xffffffff ? 0xffffffff : value;
        count++;
    }
    if (!count)
        goto bad;

    *size = GetSidLengthRequired( count );
    if (pisid)
    {
        pisid->Revision = revision;
        pisid->SubAuthorityCount = count;
        pisid->IdentifierAuthority.Value[0] = authority >> 40;
        pisid->IdentifierAuthority.Value[1] = authority >> 32;
        pisid->IdentifierAuthority.Value[2] = authority >> 24;
        pisid->IdentifierAuthority.Value[3] = authority >> 16;
        pisid->IdentifierAuthority.Value[4] = authority >> 8;
        pisid->IdentifierAuthority.Value[5] = authority;
    }
    if (end) *end = p;
    return TRUE;

bad:
    SetLastError( ERROR_INVALID_SID );
    return FALSE;
}

/* Parse a numeric SID or a two-letter SDDL alias. When pisid is NULL, only
 * the size is computed. */
static BOOL parse_sid( const WCHAR *string, const WCHAR **end, SID *pisid, DWORD *size )
{
    unsigned int i;

    if ((string[0] == 'S' || string[0] == 's') && string[1] == '-')
        return parse_numeric_sid( string, end, pisid, size );

    /* String constant format  - Only available in winxp and above */
    if (string[0] && string[1])
    {
        for (i = 0; i < ARRAY_SIZE(well_known_sids); i++)
        {
            if (!well_known_sids[i].str[0] || wcsnicmp( well_known_sids[i].str, string, 2 ))
                continue;
            *size = GetSidLengthRequired( well_known_sids[i].sid.SubAuthorityCount );
            if (pisid)
                memcpy( pisid, &well_known_sids[i].sid, *size );
            if (end) *end = string + 2;
            return TRUE;
        }

        for (i = 0; i < ARRAY_SIZE(well_known_rids); i++)
        {
            struct max_sid local;

            if (!well_known_rids[i].str[0] || wcsnicmp( well_known_rids[i].str, string, 2 ))
                continue;
            get_computer_sid( &local );
            local.SubAuthority[local.SubAuthorityCount++] = well_known_rids[i].rid;
            *size = GetSidLengthRequired( local.SubAuthorityCount );
            if (pisid)
                memcpy( pisid, &local, *size );
            if (end) *end = string + 2;
            return TRUE;
        }
    }

    TRACE("String constant not supported: %s\n", debugstr_wn(string, 2));
    SetLastError( ERROR_INVALID_SID );
    return FALSE;
}

/******************************************************************************
 *     ConvertStringSidToSidW   (sechost.@)
 */
BOOL WINAPI DECLSPEC_HOTPATCH ConvertStringSidToSidW( const WCHAR *string, PSID *sid )
{
    DWORD size;
    const WCHAR *string_end;

    TRACE("%s, %p\n", debugstr_w(string), sid);

    if (GetVersion() & 0x80000000)
    {
        SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
        return FALSE;
    }

    if (!string || !sid)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (!parse_sid( string, &string_end, NULL, &size ))
        return FALSE;

    if (*string_end)
    {
        SetLastError(ERROR_INVALID_SID);
        return FALSE;
    }

    if (!(*sid = LocalAlloc( 0, size )))
    {
        SetLastError( ERROR_NOT_ENOUGH_MEMORY );
        return FALSE;
    }

    parse_sid( string, NULL, *sid, &size );
    SetLastError( ERROR_SUCCESS );
    return TRUE;
}

/* ACL flags: P, AR, AI, and NO_ACCESS_CONTROL for a NULL ACL. */
static BOOL parse_acl_flags( const WCHAR **string_ptr, DWORD *flags, BOOL *null_acl )
{
    const WCHAR *string = *string_ptr;

    *flags = 0;
    *null_acl = FALSE;

    while (*string && *string != '(')
    {
        if (*string == ' ')
            string++;
        else if (*string == 'P')
        {
            *flags |= SE_DACL_PROTECTED;
            string++;
        }
        else if (string[0] == 'A' && string[1] == 'R')
        {
            *flags |= SE_DACL_AUTO_INHERIT_REQ;
            string += 2;
        }
        else if (string[0] == 'A' && string[1] == 'I')
        {
            *flags |= SE_DACL_AUTO_INHERITED;
            string += 2;
        }
        else if (!wcsncmp( string, L"NO_ACCESS_CONTROL", 17 ))
        {
            *null_acl = TRUE;
            string += 17;
        }
        else
        {
            SetLastError( ERROR_INVALID_DATATYPE );
            return FALSE;
        }
    }

    *string_ptr = string;
    return TRUE;
}

/* The ACE type is matched as a case-insensitive prefix; the caller checks
 * that a ';' follows it. */
static BOOL parse_ace_type( const WCHAR **string_ptr, BYTE *type )
{
    const WCHAR *string = *string_ptr;
    unsigned int i;

    while (*string == ' ')
        string++;

    for (i = 0; i < ARRAY_SIZE(ace_types); ++i)
    {
        size_t len = wcslen( ace_types[i].str );
        if (!wcsnicmp( string, ace_types[i].str, len ))
        {
            *string_ptr = string + len;
            *type = ace_types[i].value;
            return TRUE;
        }
    }
    return FALSE;
}

static DWORD parse_ace_flag( const WCHAR *string )
{
    static const struct
    {
        WCHAR str[3];
        DWORD value;
    }
    ace_flags[] =
    {
        { L"CI", CONTAINER_INHERIT_ACE },
        { L"FA", FAILED_ACCESS_ACE_FLAG },
        { L"ID", INHERITED_ACE },
        { L"IO", INHERIT_ONLY_ACE },
        { L"NP", NO_PROPAGATE_INHERIT_ACE },
        { L"OI", OBJECT_INHERIT_ACE },
        { L"SA", SUCCESSFUL_ACCESS_ACE_FLAG },
    };

    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(ace_flags); ++i)
    {
        if (!wcsncmp( string, ace_flags[i].str, 2 ))
            return ace_flags[i].value;
    }
    return 0;
}

static BOOL parse_ace_right( const WCHAR **string_ptr, DWORD *right )
{
    const WCHAR *string = *string_ptr;
    unsigned int i;

    /* a numeric mask may be zero (Windows 10 oracle: AccessCheck deny_zero_mask) */
    if (iswdigit( string[0] ))
    {
        *right = wcstoul( string, (WCHAR **)string_ptr, 0 );
        return TRUE;
    }

    for (i = 0; i < ARRAY_SIZE(ace_rights); ++i)
    {
        if (!wcsncmp( string, ace_rights[i].str, 2 ))
        {
            *string_ptr += 2;
            *right = ace_rights[i].value;
            return TRUE;
        }
    }
    return FALSE;
}

static BOOL parse_ace_flags( const WCHAR **string_ptr, BYTE *flags )
{
    const WCHAR *string = *string_ptr;

    *flags = 0;

    while (*string == ' ')
        string++;

    while (*string != ';')
    {
        DWORD flag = parse_ace_flag( string );
        if (!flag) return FALSE;
        *flags |= flag;
        string += 2;
    }

    *string_ptr = string;
    return TRUE;
}

static DWORD parse_ace_rights( const WCHAR **string_ptr )
{
    DWORD rights = 0;
    const WCHAR *string = *string_ptr;

    while (*string == ' ')
        string++;

    while (*string != ';')
    {
        DWORD right;
        if (!parse_ace_right( &string, &right )) return 0;
        rights |= right;
    }

    *string_ptr = string;
    return rights;
}

static int hex_value( WCHAR c )
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* A GUID without braces: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx */
static BOOL parse_guid( const WCHAR **string_ptr, GUID *guid )
{
    const WCHAR *string = *string_ptr;
    BYTE bytes[16];
    unsigned int i, n = 0;

    for (i = 0; i < 36; i++)
    {
        if (i == 8 || i == 13 || i == 18 || i == 23)
        {
            if (string[i] != '-') return FALSE;
            continue;
        }
        if (hex_value( string[i] ) < 0 || hex_value( string[i + 1] ) < 0) return FALSE;
        bytes[n++] = hex_value( string[i] ) << 4 | hex_value( string[i + 1] );
        i++;
    }

    guid->Data1 = (DWORD)bytes[0] << 24 | bytes[1] << 16 | bytes[2] << 8 | bytes[3];
    guid->Data2 = bytes[4] << 8 | bytes[5];
    guid->Data3 = bytes[6] << 8 | bytes[7];
    memcpy( guid->Data4, bytes + 8, 8 );
    *string_ptr = string + 36;
    return TRUE;
}

static BOOL parse_acl( const WCHAR *string, DWORD *flags, BOOL *null_acl, ACL *acl, DWORD *ret_size )
{
    DWORD length = sizeof(ACL);
    DWORD acecount = 0;
    BOOL has_object_aces = FALSE;
    BYTE *ace = NULL; /* pointer to current ACE */

    TRACE("%s\n", debugstr_w(string));

    if (acl) /* ace is only useful if we're setting values */
        ace = (BYTE *)(acl + 1);

    /* Parse ACL flags */
    if (!parse_acl_flags( &string, flags, null_acl ))
        return FALSE;

    if (*null_acl)
    {
        *ret_size = 0;
        return TRUE;
    }

    /* Parse ACE */
    while (*string == '(')
    {
        const WCHAR *p, *close = wcschr( string, ')' );
        unsigned int i, fields = 0;
        BYTE type, ace_flags;
        DWORD mask, sidlen, hdrsize, object_flags = 0;
        GUID guids[2];

        if (!close)
            goto err;

        /* an ACE has exactly six fields */
        for (p = string + 1; p < close; p++)
            if (*p == ';') fields++;
        if (fields != 5)
        {
            SetLastError( ERROR_INVALID_PARAMETER );
            return FALSE;
        }
        string++;

        /* Parse ACE type */
        if (!parse_ace_type( &string, &type ))
        {
            SetLastError( ERROR_INVALID_DATATYPE );
            return FALSE;
        }
        if (*string != ';')
            goto err;
        string++;

        /* Parse ACE flags; the audit flags belong to audit and alarm ACEs only
         * (Windows 10 oracle: AccessCheck audit_flags_on_allow) */
        if (!parse_ace_flags( &string, &ace_flags ) ||
            ((ace_flags & (SUCCESSFUL_ACCESS_ACE_FLAG | FAILED_ACCESS_ACE_FLAG)) &&
             type != SYSTEM_AUDIT_ACE_TYPE && type != SYSTEM_ALARM_ACE_TYPE &&
             type != ACCESS_AUDIT_OBJECT_ACE_TYPE && type != ACCESS_ALARM_OBJECT_ACE_TYPE))
        {
            SetLastError( ERROR_INVALID_FLAGS );
            return FALSE;
        }
        string++;

        /* Parse ACE rights */
        mask = parse_ace_rights( &string );
        if (*string != ';')
            goto err;
        string++;

        /* Parse ACE object guid and inherit object guid */
        for (i = 0; i < 2; i++)
        {
            while (*string == ' ')
                string++;
            if (*string != ';')
            {
                if (!is_object_ace( type ))
                    goto err;
                if (!parse_guid( &string, &guids[i] ) || *string != ';')
                {
                    SetLastError( RPC_S_INVALID_STRING_UUID );
                    return FALSE;
                }
                object_flags |= i ? ACE_INHERITED_OBJECT_TYPE_PRESENT : ACE_OBJECT_TYPE_PRESENT;
            }
            string++;
        }

        if (is_object_ace( type ))
        {
            hdrsize = offsetof( ACCESS_ALLOWED_OBJECT_ACE, ObjectType );
            if (object_flags & ACE_OBJECT_TYPE_PRESENT) hdrsize += sizeof(GUID);
            if (object_flags & ACE_INHERITED_OBJECT_TYPE_PRESENT) hdrsize += sizeof(GUID);
            has_object_aces = TRUE;
        }
        else
            hdrsize = offsetof( ACCESS_ALLOWED_ACE, SidStart );

        /* Parse ACE account sid */
        while (*string == ' ')
            string++;
        if (!parse_sid( string, &string, ace ? (SID *)(ace + hdrsize) : NULL, &sidlen ))
            return FALSE;

        while (*string == ' ')
            string++;

        if (*string != ')')
            goto err;
        string++;

        if (ace)
        {
            ACCESS_ALLOWED_ACE *plain = (ACCESS_ALLOWED_ACE *)ace;

            plain->Header.AceType = type;
            plain->Header.AceFlags = ace_flags;
            plain->Header.AceSize = hdrsize + sidlen;
            plain->Mask = mask;
            if (is_object_ace( type ))
            {
                ACCESS_ALLOWED_OBJECT_ACE *obj = (ACCESS_ALLOWED_OBJECT_ACE *)ace;
                GUID *guid = &obj->ObjectType;

                obj->Flags = object_flags;
                if (object_flags & ACE_OBJECT_TYPE_PRESENT) *guid++ = guids[0];
                if (object_flags & ACE_INHERITED_OBJECT_TYPE_PRESENT) *guid = guids[1];
            }
            ace += hdrsize + sidlen;
        }
        length += hdrsize + sidlen;
        acecount++;
    }

    *ret_size = length;

    if (length > 0xffff)
    {
        ERR("ACL too large\n");
        goto err;
    }

    if (acl)
    {
        acl->AclRevision = has_object_aces ? ACL_REVISION4 : ACL_REVISION;
        acl->Sbz1 = 0;
        acl->AclSize = length;
        acl->AceCount = acecount;
        acl->Sbz2 = 0;
    }
    return TRUE;

err:
    SetLastError( ERROR_INVALID_ACL );
    WARN("Invalid ACE string format\n");
    return FALSE;
}

static BOOL parse_sd( const WCHAR *string, SECURITY_DESCRIPTOR_RELATIVE *sd, DWORD *size)
{
    BOOL ret = FALSE;
    WCHAR toktype;
    WCHAR *tok;
    const WCHAR *lptoken;
    BYTE *next = NULL;
    DWORD len;

    *size = sizeof(SECURITY_DESCRIPTOR_RELATIVE);

    tok = malloc( (wcslen(string) + 1) * sizeof(WCHAR) );
    if (!tok)
    {
        SetLastError( ERROR_NOT_ENOUGH_MEMORY );
        return FALSE;
    }

    if (sd)
        next = (BYTE *)(sd + 1);

    while (*string == ' ')
        string++;

    while (*string)
    {
        toktype = *string;

        /* Expect char identifier followed by ':' */
        string++;
        if (*string != ':')
        {
            SetLastError( ERROR_INVALID_PARAMETER );
            goto out;
        }
        string++;

        /* Extract token */
        lptoken = string;
        while (*lptoken && *lptoken != ':')
            lptoken++;

        if (*lptoken)
            lptoken--;

        len = lptoken - string;
        memcpy( tok, string, len * sizeof(WCHAR) );
        tok[len] = 0;

        switch (toktype)
        {
            case 'O':
            {
                DWORD bytes;

                const WCHAR *sid_string = tok;

                while (*sid_string == ' ')
                    sid_string++;
                if (!parse_sid( sid_string, NULL, (SID *)next, &bytes ))
                    goto out;

                if (sd)
                {
                    sd->Owner = next - (BYTE *)sd;
                    next += bytes; /* Advance to next token */
                }

                *size += bytes;

                break;
            }

            case 'G':
            {
                DWORD bytes;

                const WCHAR *sid_string = tok;

                while (*sid_string == ' ')
                    sid_string++;
                if (!parse_sid( sid_string, NULL, (SID *)next, &bytes ))
                    goto out;

                if (sd)
                {
                    sd->Group = next - (BYTE *)sd;
                    next += bytes; /* Advance to next token */
                }

                *size += bytes;

                break;
            }

            case 'D':
            {
                DWORD flags;
                DWORD bytes;
                BOOL null_acl;

                if (!parse_acl( tok, &flags, &null_acl, (ACL *)next, &bytes ))
                    goto out;

                if (sd)
                {
                    sd->Control |= SE_DACL_PRESENT | (flags);
                    sd->Dacl = null_acl ? 0 : next - (BYTE *)sd;
                    next += bytes; /* Advance to next token */
                }

                *size += bytes;

                break;
            }

            case 'S':
            {
                DWORD flags;
                DWORD bytes;
                BOOL null_acl;

                if (!parse_acl( tok, &flags, &null_acl, (ACL *)next, &bytes ))
                    goto out;

                if (sd)
                {
                    sd->Control |= SE_SACL_PRESENT | (flags << 1);
                    sd->Sacl = null_acl ? 0 : next - (BYTE *)sd;
                    next += bytes; /* Advance to next token */
                }

                *size += bytes;

                break;
            }

            default:
                FIXME("Unknown token\n");
                SetLastError( ERROR_INVALID_PARAMETER );
                goto out;
        }

        string = lptoken;
    }

    ret = TRUE;

out:
    free(tok);
    return ret;
}

/******************************************************************************
 *     ConvertStringSecurityDescriptorToSecurityDescriptorW   (sechost.@)
 */
BOOL WINAPI DECLSPEC_HOTPATCH ConvertStringSecurityDescriptorToSecurityDescriptorW(
        const WCHAR *string, DWORD revision, PSECURITY_DESCRIPTOR *sd, ULONG *ret_size )
{
    DWORD size;
    SECURITY_DESCRIPTOR *psd;

    TRACE("%s, %lu, %p, %p\n", debugstr_w(string), revision, sd, ret_size);

    if (GetVersion() & 0x80000000)
    {
        SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
        return FALSE;
    }
    if (!string || !sd)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (revision != SID_REVISION)
    {
        SetLastError(ERROR_UNKNOWN_REVISION);
        return FALSE;
    }

    /* Compute security descriptor length */
    if (!parse_sd( string, NULL, &size ))
        return FALSE;

    psd = *sd = LocalAlloc( GMEM_ZEROINIT, size );
    if (!psd)
    {
        SetLastError( ERROR_NOT_ENOUGH_MEMORY );
        return FALSE;
    }

    psd->Revision = SID_REVISION;
    psd->Control |= SE_SELF_RELATIVE;

    if (!parse_sd( string, (SECURITY_DESCRIPTOR_RELATIVE *)psd, &size ))
    {
        LocalFree(psd);
        return FALSE;
    }

    if (ret_size) *ret_size = size;
    SetLastError( ERROR_SUCCESS );
    return TRUE;
}
