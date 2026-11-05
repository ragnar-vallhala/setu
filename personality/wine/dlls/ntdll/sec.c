/*
 *	Security functions
 *
 *	Copyright 1996-1998 Marcus Meissner
 * 	Copyright 2003 CodeWeavers Inc. (Ulrich Czekalla)
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
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <math.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winbase.h"
#include "tomcrypt.h"
#include "ntdll_misc.h"
#include "ddk/ntddk.h"
#include "wine/exception.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(ntdll);

#define SELF_RELATIVE_FIELD(sd,field) ((BYTE *)(sd) + ((SECURITY_DESCRIPTOR_RELATIVE *)(sd))->field)

static const SID world_sid = { SID_REVISION, 1, { SECURITY_WORLD_SID_AUTHORITY} , { SECURITY_WORLD_RID } };
static const DWORD world_access_acl_size = sizeof(ACL) + sizeof(ACCESS_ALLOWED_ACE) + sizeof(world_sid) - sizeof(DWORD);

static void get_world_access_acl( PACL acl )
{
    PACCESS_ALLOWED_ACE ace = (PACCESS_ALLOWED_ACE)(acl + 1);

    acl->AclRevision = ACL_REVISION;
    acl->Sbz1 = 0;
    acl->AclSize = world_access_acl_size;
    acl->AceCount = 1;
    acl->Sbz2 = 0;
    ace->Header.AceType = ACCESS_ALLOWED_ACE_TYPE;
    ace->Header.AceFlags = CONTAINER_INHERIT_ACE;
    ace->Header.AceSize = sizeof(ACCESS_ALLOWED_ACE) + sizeof(world_sid) - sizeof(DWORD);
    ace->Mask = 0xf3ffffff; /* Everything except reserved bits */
    memcpy( &ace->SidStart, &world_sid, sizeof(world_sid) );
}

/* helper function to retrieve active length of an ACL */
static size_t acl_bytesInUse(PACL pAcl)
{
    int i;
    size_t bytesInUse = sizeof(ACL);
    PACE_HEADER ace = (PACE_HEADER) (pAcl + 1);
    for (i = 0; i < pAcl->AceCount; i++)
    {
	bytesInUse += ace->AceSize;
	ace = (PACE_HEADER)(((BYTE*)ace)+ace->AceSize);
    }
    return bytesInUse;
}

/* helper function to copy an ACL */
static BOOLEAN copy_acl(DWORD nDestinationAclLength, PACL pDestinationAcl, PACL pSourceAcl)
{
    DWORD size;

    if (!pSourceAcl || !RtlValidAcl(pSourceAcl))
        return FALSE;

    size = pSourceAcl->AclSize;
    if (nDestinationAclLength < size)
        return FALSE;

    memmove(pDestinationAcl, pSourceAcl, size);
    return TRUE;
}

/* generically adds an ACE to an ACL */
static NTSTATUS add_access_ace(PACL pAcl, DWORD dwAceRevision, DWORD dwAceFlags,
                               DWORD dwAccessMask, PSID pSid, DWORD dwAceType)
{
    ACE_HEADER *pAceHeader;
    DWORD dwLengthSid;
    DWORD dwAceSize;
    DWORD *pAccessMask;
    DWORD *pSidStart;

    if (!RtlValidSid(pSid))
        return STATUS_INVALID_SID;

    if (pAcl->AclRevision > MAX_ACL_REVISION || dwAceRevision > MAX_ACL_REVISION)
        return STATUS_REVISION_MISMATCH;

    if (!RtlValidAcl(pAcl))
        return STATUS_INVALID_ACL;

    if (!RtlFirstFreeAce(pAcl, &pAceHeader))
        return STATUS_INVALID_ACL;

    if (!pAceHeader)
        return STATUS_ALLOTTED_SPACE_EXCEEDED;

    /* calculate generic size of the ACE */
    dwLengthSid = RtlLengthSid(pSid);
    dwAceSize = sizeof(ACE_HEADER) + sizeof(DWORD) + dwLengthSid;
    if ((char *)pAceHeader + dwAceSize > (char *)pAcl + pAcl->AclSize)
        return STATUS_ALLOTTED_SPACE_EXCEEDED;

    /* fill the new ACE */
    pAceHeader->AceType = dwAceType;
    pAceHeader->AceFlags = dwAceFlags;
    pAceHeader->AceSize = dwAceSize;

    /* skip past the ACE_HEADER of the ACE */
    pAccessMask = (DWORD *)(pAceHeader + 1);
    *pAccessMask = dwAccessMask;

    /* skip past ACE->Mask */
    pSidStart = pAccessMask + 1;
    RtlCopySid(dwLengthSid, pSidStart, pSid);

    pAcl->AclRevision = max(pAcl->AclRevision, dwAceRevision);
    pAcl->AceCount++;

    return STATUS_SUCCESS;
}

/*
 *	SID FUNCTIONS
 */

/******************************************************************************
 *  RtlAllocateAndInitializeSid		[NTDLL.@]
 *
 */
NTSTATUS WINAPI RtlAllocateAndInitializeSid (
	PSID_IDENTIFIER_AUTHORITY pIdentifierAuthority,
	BYTE nSubAuthorityCount,
	DWORD nSubAuthority0, DWORD nSubAuthority1,
	DWORD nSubAuthority2, DWORD nSubAuthority3,
	DWORD nSubAuthority4, DWORD nSubAuthority5,
	DWORD nSubAuthority6, DWORD nSubAuthority7,
	PSID *pSid )
{
    SID *tmp_sid;

    TRACE("(%p, 0x%04x,0x%08lx,0x%08lx,0x%08lx,0x%08lx,0x%08lx,0x%08lx,0x%08lx,0x%08lx,%p)\n",
		pIdentifierAuthority,nSubAuthorityCount,
		nSubAuthority0, nSubAuthority1,	nSubAuthority2, nSubAuthority3,
		nSubAuthority4, nSubAuthority5,	nSubAuthority6, nSubAuthority7, pSid);

    if (nSubAuthorityCount > 8) return STATUS_INVALID_SID;

    if (!(tmp_sid= RtlAllocateHeap( GetProcessHeap(), 0,
                                    RtlLengthRequiredSid(nSubAuthorityCount))))
        return STATUS_NO_MEMORY;

    tmp_sid->Revision = SID_REVISION;

    if (pIdentifierAuthority)
        tmp_sid->IdentifierAuthority = *pIdentifierAuthority;
    tmp_sid->SubAuthorityCount = nSubAuthorityCount;

    switch( nSubAuthorityCount )
    {
        case 8: tmp_sid->SubAuthority[7]= nSubAuthority7;
            /* fall through */
        case 7: tmp_sid->SubAuthority[6]= nSubAuthority6;
            /* fall through */
        case 6: tmp_sid->SubAuthority[5]= nSubAuthority5;
            /* fall through */
        case 5: tmp_sid->SubAuthority[4]= nSubAuthority4;
            /* fall through */
        case 4: tmp_sid->SubAuthority[3]= nSubAuthority3;
            /* fall through */
        case 3: tmp_sid->SubAuthority[2]= nSubAuthority2;
            /* fall through */
        case 2: tmp_sid->SubAuthority[1]= nSubAuthority1;
            /* fall through */
        case 1: tmp_sid->SubAuthority[0]= nSubAuthority0;
        break;
    }
    *pSid = tmp_sid;
    return STATUS_SUCCESS;
}

/******************************************************************************
 *  RtlEqualSid		[NTDLL.@]
 *
 * Determine if two SIDs are equal.
 *
 * PARAMS
 *  pSid1 [I] Source SID
 *  pSid2 [I] SID to compare with
 *
 * RETURNS
 *  TRUE, if pSid1 is equal to pSid2,
 *  FALSE otherwise.
 */
BOOL WINAPI RtlEqualSid( PSID pSid1, PSID pSid2 )
{
    if (!RtlValidSid(pSid1) || !RtlValidSid(pSid2))
        return FALSE;

    if (*RtlSubAuthorityCountSid(pSid1) != *RtlSubAuthorityCountSid(pSid2))
        return FALSE;

    if (memcmp(pSid1, pSid2, RtlLengthSid(pSid1)) != 0)
        return FALSE;

    return TRUE;
}

/******************************************************************************
 * RtlEqualPrefixSid	[NTDLL.@]
 */
BOOL WINAPI RtlEqualPrefixSid (PSID pSid1, PSID pSid2)
{
    if (!RtlValidSid(pSid1) || !RtlValidSid(pSid2))
        return FALSE;

    if (*RtlSubAuthorityCountSid(pSid1) != *RtlSubAuthorityCountSid(pSid2))
        return FALSE;

    if (memcmp(pSid1, pSid2, RtlLengthRequiredSid(((SID*)pSid1)->SubAuthorityCount - 1)) != 0)
        return FALSE;

    return TRUE;
}


/******************************************************************************
 *  RtlFreeSid		[NTDLL.@]
 *
 * Free the resources used by a SID.
 *
 * PARAMS
 *  pSid [I] SID to Free.
 *
 * RETURNS
 *  STATUS_SUCCESS.
 */
DWORD WINAPI RtlFreeSid(PSID pSid)
{
	TRACE("(%p)\n", pSid);
	RtlFreeHeap( GetProcessHeap(), 0, pSid );
	return STATUS_SUCCESS;
}

/**************************************************************************
 * RtlLengthRequiredSid	[NTDLL.@]
 *
 * Determine the amount of memory a SID will use
 *
 * PARAMS
 *   nrofsubauths [I] Number of Sub Authorities in the SID.
 *
 * RETURNS
 *   The size, in bytes, of a SID with nrofsubauths Sub Authorities.
 */
DWORD WINAPI RtlLengthRequiredSid(DWORD nrofsubauths)
{
	return (nrofsubauths-1)*sizeof(DWORD) + sizeof(SID);
}

/**************************************************************************
 *                 RtlLengthSid				[NTDLL.@]
 *
 * Determine the amount of memory a SID is using
 *
 * PARAMS
 *  pSid [I] SID to get the size of.
 *
 * RETURNS
 *  The size, in bytes, of pSid.
 */
DWORD WINAPI RtlLengthSid(PSID pSid)
{
	TRACE("sid=%p\n",pSid);
	if (!pSid) return 0;
	return RtlLengthRequiredSid(*RtlSubAuthorityCountSid(pSid));
}

/**************************************************************************
 *                 RtlInitializeSid			[NTDLL.@]
 */
NTSTATUS WINAPI RtlInitializeSid(
	PSID pSid,
	PSID_IDENTIFIER_AUTHORITY pIdentifierAuthority,
	BYTE nSubAuthorityCount)
{
	int i;
	SID* pisid=pSid;

	if (nSubAuthorityCount > SID_MAX_SUB_AUTHORITIES)
	  return STATUS_INVALID_PARAMETER;

	pisid->Revision = SID_REVISION;
	pisid->SubAuthorityCount = nSubAuthorityCount;
	if (pIdentifierAuthority)
	  pisid->IdentifierAuthority = *pIdentifierAuthority;

	for (i = 0; i < nSubAuthorityCount; i++)
	  *RtlSubAuthoritySid(pSid, i) = 0;

	return STATUS_SUCCESS;
}

/**************************************************************************
 *                 RtlSubAuthoritySid			[NTDLL.@]
 *
 * Return the Sub Authority of a SID
 *
 * PARAMS
 *   pSid          [I] SID to get the Sub Authority from.
 *   nSubAuthority [I] Sub Authority number.
 *
 * RETURNS
 *   A pointer to The Sub Authority value of pSid.
 */
LPDWORD WINAPI RtlSubAuthoritySid( PSID pSid, DWORD nSubAuthority )
{
    return &(((SID*)pSid)->SubAuthority[nSubAuthority]);
}

/**************************************************************************
 * RtlIdentifierAuthoritySid	[NTDLL.@]
 *
 * Return the Identifier Authority of a SID.
 *
 * PARAMS
 *   pSid [I] SID to get the Identifier Authority from.
 *
 * RETURNS
 *   A pointer to the Identifier Authority value of pSid.
 */
PSID_IDENTIFIER_AUTHORITY WINAPI RtlIdentifierAuthoritySid( PSID pSid )
{
    return &(((SID*)pSid)->IdentifierAuthority);
}

/**************************************************************************
 *                 RtlSubAuthorityCountSid		[NTDLL.@]
 *
 * Get the number of Sub Authorities in a SID.
 *
 * PARAMS
 *   pSid [I] SID to get the count from.
 *
 * RETURNS
 *  A pointer to the Sub Authority count of pSid.
 */
LPBYTE WINAPI RtlSubAuthorityCountSid(PSID pSid)
{
    return &(((SID*)pSid)->SubAuthorityCount);
}

/**************************************************************************
 *                 RtlCopySid				[NTDLL.@]
 */
BOOLEAN WINAPI RtlCopySid( DWORD nDestinationSidLength, PSID pDestinationSid, PSID pSourceSid )
{
	if (!pSourceSid || !RtlValidSid(pSourceSid) ||
	    (nDestinationSidLength < RtlLengthSid(pSourceSid)))
          return FALSE;

	if (nDestinationSidLength < (((SID*)pSourceSid)->SubAuthorityCount*4+8))
	  return FALSE;

	memmove(pDestinationSid, pSourceSid, ((SID*)pSourceSid)->SubAuthorityCount*4+8);
	return TRUE;
}
/******************************************************************************
 * RtlValidSid [NTDLL.@]
 *
 * Determine if a SID is valid.
 *
 * PARAMS
 *   pSid [I] SID to check
 *
 * RETURNS
 *   TRUE if pSid is valid,
 *   FALSE otherwise.
 */
BOOLEAN WINAPI RtlValidSid( PSID pSid )
{
    BOOL ret;
    __TRY
    {
        ret = TRUE;
        if (!pSid || ((SID*)pSid)->Revision != SID_REVISION ||
            ((SID*)pSid)->SubAuthorityCount > SID_MAX_SUB_AUTHORITIES)
        {
            ret = FALSE;
        }
    }
    __EXCEPT_PAGE_FAULT
    {
        WARN("(%p): invalid pointer!\n", pSid);
        return FALSE;
    }
    __ENDTRY
    return ret;
}


/*
 *	security descriptor functions
 */

/**************************************************************************
 * RtlCreateSecurityDescriptor			[NTDLL.@]
 *
 * Initialise a SECURITY_DESCRIPTOR.
 *
 * PARAMS
 *  lpsd [O] Descriptor to initialise.
 *  rev  [I] Revision, must be set to SECURITY_DESCRIPTOR_REVISION.
 *
 * RETURNS
 *  Success: STATUS_SUCCESS.
 *  Failure: STATUS_UNKNOWN_REVISION if rev is incorrect.
 */
NTSTATUS WINAPI RtlCreateSecurityDescriptor(
	PSECURITY_DESCRIPTOR lpsd,
	DWORD rev)
{
	if (rev!=SECURITY_DESCRIPTOR_REVISION)
		return STATUS_UNKNOWN_REVISION;
	memset(lpsd,'\0',sizeof(SECURITY_DESCRIPTOR));
	((SECURITY_DESCRIPTOR*)lpsd)->Revision = SECURITY_DESCRIPTOR_REVISION;
	return STATUS_SUCCESS;
}

/**************************************************************************
 * RtlCopySecurityDescriptor            [NTDLL.@]
 *
 * Copies an absolute or self-relative SECURITY_DESCRIPTOR.
 *
 * PARAMS
 *  pSourceSD      [O] SD to copy from.
 *  pDestinationSD [I] Destination SD.
 *
 * RETURNS
 *  Success: STATUS_SUCCESS.
 *  Failure: STATUS_UNKNOWN_REVISION if rev is incorrect.
 */
NTSTATUS WINAPI RtlCopySecurityDescriptor(PSECURITY_DESCRIPTOR pSourceSD, PSECURITY_DESCRIPTOR pDestinationSD)
{
    PSID Owner, Group;
    PACL Dacl, Sacl;
    DWORD length;

    if (((SECURITY_DESCRIPTOR *)pSourceSD)->Control & SE_SELF_RELATIVE)
    {
        SECURITY_DESCRIPTOR_RELATIVE *src = pSourceSD;
        SECURITY_DESCRIPTOR_RELATIVE *dst = pDestinationSD;

        if (src->Revision != SECURITY_DESCRIPTOR_REVISION)
            return STATUS_UNKNOWN_REVISION;

        *dst = *src;
        if (src->Owner)
        {
            Owner = (PSID)SELF_RELATIVE_FIELD( src, Owner );
            length = RtlLengthSid( Owner );
            RtlCopySid(length, SELF_RELATIVE_FIELD( dst, Owner ), Owner);
        }
        if (src->Group)
        {
            Group = (PSID)SELF_RELATIVE_FIELD( src, Group );
            length = RtlLengthSid( Group );
            RtlCopySid(length, SELF_RELATIVE_FIELD( dst, Group ), Group);
        }
        if ((src->Control & SE_SACL_PRESENT) && src->Sacl)
        {
            Sacl = (PACL)SELF_RELATIVE_FIELD( src, Sacl );
            copy_acl(Sacl->AclSize, (PACL)SELF_RELATIVE_FIELD( dst, Sacl ), Sacl);
        }
        if ((src->Control & SE_DACL_PRESENT) && src->Dacl)
        {
            Dacl = (PACL)SELF_RELATIVE_FIELD( src, Dacl );
            copy_acl(Dacl->AclSize, (PACL)SELF_RELATIVE_FIELD( dst, Dacl ), Dacl);
        }
    }
    else
    {
        SECURITY_DESCRIPTOR *src = pSourceSD;
        SECURITY_DESCRIPTOR *dst = pDestinationSD;

        if (src->Revision != SECURITY_DESCRIPTOR_REVISION)
            return STATUS_UNKNOWN_REVISION;

        *dst = *src;
        if (src->Owner)
        {
            length = RtlLengthSid( src->Owner );
            dst->Owner = RtlAllocateHeap(GetProcessHeap(), 0, length);
            RtlCopySid(length, dst->Owner, src->Owner);
        }
        if (src->Group)
        {
            length = RtlLengthSid( src->Group );
            dst->Group = RtlAllocateHeap(GetProcessHeap(), 0, length);
            RtlCopySid(length, dst->Group, src->Group);
        }
        if (src->Control & SE_SACL_PRESENT)
        {
            length = src->Sacl->AclSize;
            dst->Sacl = RtlAllocateHeap(GetProcessHeap(), 0, length);
            copy_acl(length, dst->Sacl, src->Sacl);
        }
        if (src->Control & SE_DACL_PRESENT)
        {
            length = src->Dacl->AclSize;
            dst->Dacl = RtlAllocateHeap(GetProcessHeap(), 0, length);
            copy_acl(length, dst->Dacl, src->Dacl);
        }
    }

    return STATUS_SUCCESS;
}

/**************************************************************************
 * RtlValidSecurityDescriptor			[NTDLL.@]
 */
BOOLEAN WINAPI RtlValidSecurityDescriptor(PSECURITY_DESCRIPTOR descriptor)
{
    SECURITY_DESCRIPTOR *sd = descriptor;
    return sd && sd->Revision == SECURITY_DESCRIPTOR_REVISION;
}

/**************************************************************************
 * RtlValidRelativeSecurityDescriptor		[NTDLL.@]
 */
BOOLEAN WINAPI RtlValidRelativeSecurityDescriptor(PSECURITY_DESCRIPTOR descriptor,
    ULONG length, SECURITY_INFORMATION info)
{
    FIXME("%p,%lu,%ld: semi-stub\n", descriptor, length, info);
    return RtlValidSecurityDescriptor(descriptor) == STATUS_SUCCESS;
}

/**************************************************************************
 *  RtlLengthSecurityDescriptor			[NTDLL.@]
 */
ULONG WINAPI RtlLengthSecurityDescriptor(
	PSECURITY_DESCRIPTOR pSecurityDescriptor)
{
	ULONG size;

	if ( pSecurityDescriptor == NULL )
		return 0;

	if (((SECURITY_DESCRIPTOR *)pSecurityDescriptor)->Control & SE_SELF_RELATIVE)
        {
            SECURITY_DESCRIPTOR_RELATIVE *sd = pSecurityDescriptor;
            size = sizeof(*sd);
            if (sd->Owner) size += RtlLengthSid((PSID)SELF_RELATIVE_FIELD(sd,Owner));
            if (sd->Group) size += RtlLengthSid((PSID)SELF_RELATIVE_FIELD(sd,Group));
            if ((sd->Control & SE_SACL_PRESENT) && sd->Sacl)
		size += ((PACL)SELF_RELATIVE_FIELD(sd,Sacl))->AclSize;
            if ((sd->Control & SE_DACL_PRESENT) && sd->Dacl)
		size += ((PACL)SELF_RELATIVE_FIELD(sd,Dacl))->AclSize;
        }
        else
        {
            SECURITY_DESCRIPTOR *sd = pSecurityDescriptor;
            size = sizeof(*sd);
            if (sd->Owner) size += RtlLengthSid( sd->Owner );
            if (sd->Group) size += RtlLengthSid( sd->Group );
            if ((sd->Control & SE_SACL_PRESENT) && sd->Sacl) size += sd->Sacl->AclSize;
            if ((sd->Control & SE_DACL_PRESENT) && sd->Dacl) size += sd->Dacl->AclSize;
        }
	return size;
}

/******************************************************************************
 *  RtlGetDaclSecurityDescriptor		[NTDLL.@]
 *
 */
NTSTATUS WINAPI RtlGetDaclSecurityDescriptor(
	IN PSECURITY_DESCRIPTOR pSecurityDescriptor,
	OUT PBOOLEAN lpbDaclPresent,
	OUT PACL *pDacl,
	OUT PBOOLEAN lpbDaclDefaulted)
{
	SECURITY_DESCRIPTOR* lpsd=pSecurityDescriptor;

	TRACE("(%p,%p,%p,%p)\n",
	pSecurityDescriptor, lpbDaclPresent, pDacl, lpbDaclDefaulted);

	if (lpsd->Revision != SECURITY_DESCRIPTOR_REVISION)
	  return STATUS_UNKNOWN_REVISION ;

	if ( (*lpbDaclPresent = (SE_DACL_PRESENT & lpsd->Control) ? 1 : 0) )
	{
            if (lpsd->Control & SE_SELF_RELATIVE)
            {
                SECURITY_DESCRIPTOR_RELATIVE *sdr = pSecurityDescriptor;
                if (sdr->Dacl) *pDacl = (PACL)SELF_RELATIVE_FIELD( sdr, Dacl );
                else *pDacl = NULL;
            }
            else *pDacl = lpsd->Dacl;

            *lpbDaclDefaulted = (lpsd->Control & SE_DACL_DEFAULTED) != 0;
        }
        else
        {
            *pDacl = NULL;
            *lpbDaclDefaulted = 0;
        }

	return STATUS_SUCCESS;
}

/**************************************************************************
 *  RtlSetDaclSecurityDescriptor		[NTDLL.@]
 */
NTSTATUS WINAPI RtlSetDaclSecurityDescriptor (
	PSECURITY_DESCRIPTOR pSecurityDescriptor,
	BOOLEAN daclpresent,
	PACL dacl,
	BOOLEAN dacldefaulted )
{
	SECURITY_DESCRIPTOR* lpsd=pSecurityDescriptor;

	if (lpsd->Revision!=SECURITY_DESCRIPTOR_REVISION)
		return STATUS_UNKNOWN_REVISION;
	if (lpsd->Control & SE_SELF_RELATIVE)
		return STATUS_INVALID_SECURITY_DESCR;

	if (!daclpresent)
	{
		lpsd->Control &= ~SE_DACL_PRESENT;
		return STATUS_SUCCESS;
	}

	lpsd->Control |= SE_DACL_PRESENT;
	lpsd->Dacl = dacl;

	if (dacldefaulted)
		lpsd->Control |= SE_DACL_DEFAULTED;
	else
		lpsd->Control &= ~SE_DACL_DEFAULTED;

	return STATUS_SUCCESS;
}

/******************************************************************************
 *  RtlGetSaclSecurityDescriptor		[NTDLL.@]
 *
 */
NTSTATUS WINAPI RtlGetSaclSecurityDescriptor(
	IN PSECURITY_DESCRIPTOR pSecurityDescriptor,
	OUT PBOOLEAN lpbSaclPresent,
	OUT PACL *pSacl,
	OUT PBOOLEAN lpbSaclDefaulted)
{
	SECURITY_DESCRIPTOR* lpsd=pSecurityDescriptor;

	TRACE("(%p,%p,%p,%p)\n",
	pSecurityDescriptor, lpbSaclPresent, pSacl, lpbSaclDefaulted);

	if (lpsd->Revision != SECURITY_DESCRIPTOR_REVISION)
	  return STATUS_UNKNOWN_REVISION;

	if ( (*lpbSaclPresent = (SE_SACL_PRESENT & lpsd->Control) ? 1 : 0) )
	{
            if (lpsd->Control & SE_SELF_RELATIVE)
            {
                SECURITY_DESCRIPTOR_RELATIVE *sdr = pSecurityDescriptor;
                if (sdr->Sacl) *pSacl = (PACL)SELF_RELATIVE_FIELD( sdr, Sacl );
                else *pSacl = NULL;
            }
            else *pSacl = lpsd->Sacl;

            *lpbSaclDefaulted = (lpsd->Control & SE_SACL_DEFAULTED) != 0;
	}
	return STATUS_SUCCESS;
}

/**************************************************************************
 * RtlSetSaclSecurityDescriptor			[NTDLL.@]
 */
NTSTATUS WINAPI RtlSetSaclSecurityDescriptor (
	PSECURITY_DESCRIPTOR pSecurityDescriptor,
	BOOLEAN saclpresent,
	PACL sacl,
	BOOLEAN sacldefaulted)
{
	SECURITY_DESCRIPTOR* lpsd=pSecurityDescriptor;

	if (lpsd->Revision!=SECURITY_DESCRIPTOR_REVISION)
		return STATUS_UNKNOWN_REVISION;
	if (lpsd->Control & SE_SELF_RELATIVE)
		return STATUS_INVALID_SECURITY_DESCR;
	if (!saclpresent) {
		lpsd->Control &= ~SE_SACL_PRESENT;
		return 0;
	}
	lpsd->Control |= SE_SACL_PRESENT;
	lpsd->Sacl = sacl;
	if (sacldefaulted)
		lpsd->Control |= SE_SACL_DEFAULTED;
	else
		lpsd->Control &= ~SE_SACL_DEFAULTED;
	return STATUS_SUCCESS;
}

/**************************************************************************
 * RtlGetOwnerSecurityDescriptor		[NTDLL.@]
 */
NTSTATUS WINAPI RtlGetOwnerSecurityDescriptor(
	PSECURITY_DESCRIPTOR pSecurityDescriptor,
	PSID *Owner,
	PBOOLEAN OwnerDefaulted)
{
	SECURITY_DESCRIPTOR* lpsd=pSecurityDescriptor;

	if ( !lpsd  || !Owner || !OwnerDefaulted )
		return STATUS_INVALID_PARAMETER;

        if ( lpsd->Control & SE_OWNER_DEFAULTED )
            *OwnerDefaulted = TRUE;
        else
            *OwnerDefaulted = FALSE;

        if (lpsd->Control & SE_SELF_RELATIVE)
        {
            SECURITY_DESCRIPTOR_RELATIVE *sd = pSecurityDescriptor;
            if (sd->Owner) *Owner = (PSID)SELF_RELATIVE_FIELD( sd, Owner );
            else *Owner = NULL;
        }
        else
            *Owner = lpsd->Owner;

	return STATUS_SUCCESS;
}

/**************************************************************************
 *                 RtlSetOwnerSecurityDescriptor		[NTDLL.@]
 */
NTSTATUS WINAPI RtlSetOwnerSecurityDescriptor(
	PSECURITY_DESCRIPTOR pSecurityDescriptor,
	PSID owner,
	BOOLEAN ownerdefaulted)
{
	SECURITY_DESCRIPTOR* lpsd=pSecurityDescriptor;

	if (lpsd->Revision!=SECURITY_DESCRIPTOR_REVISION)
		return STATUS_UNKNOWN_REVISION;
	if (lpsd->Control & SE_SELF_RELATIVE)
		return STATUS_INVALID_SECURITY_DESCR;

	lpsd->Owner = owner;
	if (ownerdefaulted)
		lpsd->Control |= SE_OWNER_DEFAULTED;
	else
		lpsd->Control &= ~SE_OWNER_DEFAULTED;
	return STATUS_SUCCESS;
}

/**************************************************************************
 *                 RtlSetGroupSecurityDescriptor		[NTDLL.@]
 */
NTSTATUS WINAPI RtlSetGroupSecurityDescriptor (
	PSECURITY_DESCRIPTOR pSecurityDescriptor,
	PSID group,
	BOOLEAN groupdefaulted)
{
	SECURITY_DESCRIPTOR* lpsd=pSecurityDescriptor;

	if (lpsd->Revision!=SECURITY_DESCRIPTOR_REVISION)
		return STATUS_UNKNOWN_REVISION;
	if (lpsd->Control & SE_SELF_RELATIVE)
		return STATUS_INVALID_SECURITY_DESCR;

	lpsd->Group = group;
	if (groupdefaulted)
		lpsd->Control |= SE_GROUP_DEFAULTED;
	else
		lpsd->Control &= ~SE_GROUP_DEFAULTED;
	return STATUS_SUCCESS;
}

/**************************************************************************
 *                 RtlGetGroupSecurityDescriptor		[NTDLL.@]
 */
NTSTATUS WINAPI RtlGetGroupSecurityDescriptor(
	PSECURITY_DESCRIPTOR pSecurityDescriptor,
	PSID *Group,
	PBOOLEAN GroupDefaulted)
{
	SECURITY_DESCRIPTOR* lpsd=pSecurityDescriptor;

	if ( !lpsd || !Group || !GroupDefaulted )
		return STATUS_INVALID_PARAMETER;

        if ( lpsd->Control & SE_GROUP_DEFAULTED )
            *GroupDefaulted = TRUE;
        else
            *GroupDefaulted = FALSE;

        if (lpsd->Control & SE_SELF_RELATIVE)
        {
            SECURITY_DESCRIPTOR_RELATIVE *sd = pSecurityDescriptor;
            if (sd->Group) *Group = (PSID)SELF_RELATIVE_FIELD( sd, Group );
            else *Group = NULL;
        }
        else
            *Group = lpsd->Group;

	return STATUS_SUCCESS;
}

/**************************************************************************
 *                 RtlMakeSelfRelativeSD		[NTDLL.@]
 */
NTSTATUS WINAPI RtlMakeSelfRelativeSD(
	IN PSECURITY_DESCRIPTOR pAbsoluteSecurityDescriptor,
	IN PSECURITY_DESCRIPTOR pSelfRelativeSecurityDescriptor,
	IN OUT LPDWORD lpdwBufferLength)
{
    DWORD offsetRel;
    ULONG length;
    SECURITY_DESCRIPTOR* pAbs = pAbsoluteSecurityDescriptor;
    SECURITY_DESCRIPTOR_RELATIVE *pRel = pSelfRelativeSecurityDescriptor;

    TRACE(" %p %p %p(%ld)\n", pAbs, pRel, lpdwBufferLength,
        lpdwBufferLength ? *lpdwBufferLength: -1);

    if (!lpdwBufferLength || !pAbs)
        return STATUS_INVALID_PARAMETER;

    length = RtlLengthSecurityDescriptor(pAbs);
    if (!(pAbs->Control & SE_SELF_RELATIVE)) length -= (sizeof(*pAbs) - sizeof(*pRel));
    if (*lpdwBufferLength < length)
    {
        *lpdwBufferLength = length;
        return STATUS_BUFFER_TOO_SMALL;
    }

    if (!pRel)
        return STATUS_INVALID_PARAMETER;

    if (pAbs->Control & SE_SELF_RELATIVE)
    {
        memcpy(pRel, pAbs, length);
        return STATUS_SUCCESS;
    }

    pRel->Revision = pAbs->Revision;
    pRel->Sbz1 = pAbs->Sbz1;
    pRel->Control = pAbs->Control | SE_SELF_RELATIVE;

    offsetRel = sizeof(SECURITY_DESCRIPTOR_RELATIVE);
    if (pAbs->Owner)
    {
        pRel->Owner = offsetRel;
        length = RtlLengthSid(pAbs->Owner);
        memcpy((LPBYTE)pRel + offsetRel, pAbs->Owner, length);
        offsetRel += length;
    }
    else
    {
        pRel->Owner = 0;
    }

    if (pAbs->Group)
    {
        pRel->Group = offsetRel;
        length = RtlLengthSid(pAbs->Group);
        memcpy((LPBYTE)pRel + offsetRel, pAbs->Group, length);
        offsetRel += length;
    }
    else
    {
        pRel->Group = 0;
    }

    if (pAbs->Sacl)
    {
        pRel->Sacl = offsetRel;
        length = pAbs->Sacl->AclSize;
        memcpy((LPBYTE)pRel + offsetRel, pAbs->Sacl, length);
        offsetRel += length;
    }
    else
    {
        pRel->Sacl = 0;
    }

    if (pAbs->Dacl)
    {
        pRel->Dacl = offsetRel;
        length = pAbs->Dacl->AclSize;
        memcpy((LPBYTE)pRel + offsetRel, pAbs->Dacl, length);
    }
    else
    {
        pRel->Dacl = 0;
    }

    return STATUS_SUCCESS;
}


/**************************************************************************
 *                 RtlSelfRelativeToAbsoluteSD [NTDLL.@]
 */
NTSTATUS WINAPI RtlSelfRelativeToAbsoluteSD(
        IN PSECURITY_DESCRIPTOR pSelfRelativeSecurityDescriptor,
	OUT PSECURITY_DESCRIPTOR pAbsoluteSecurityDescriptor,
	OUT LPDWORD lpdwAbsoluteSecurityDescriptorSize,
	OUT PACL pDacl,
	OUT LPDWORD lpdwDaclSize,
	OUT PACL pSacl,
	OUT LPDWORD lpdwSaclSize,
	OUT PSID pOwner,
	OUT LPDWORD lpdwOwnerSize,
	OUT PSID pPrimaryGroup,
	OUT LPDWORD lpdwPrimaryGroupSize)
{
    NTSTATUS status = STATUS_SUCCESS;
    SECURITY_DESCRIPTOR* pAbs = pAbsoluteSecurityDescriptor;
    SECURITY_DESCRIPTOR_RELATIVE* pRel = pSelfRelativeSecurityDescriptor;

    if (!pRel ||
        !lpdwAbsoluteSecurityDescriptorSize ||
        !lpdwDaclSize ||
        !lpdwSaclSize ||
        !lpdwOwnerSize ||
        !lpdwPrimaryGroupSize ||
        ~pRel->Control & SE_SELF_RELATIVE)
        return STATUS_INVALID_PARAMETER;

    /* Confirm buffers are sufficiently large */
    if (*lpdwAbsoluteSecurityDescriptorSize < sizeof(SECURITY_DESCRIPTOR))
    {
        *lpdwAbsoluteSecurityDescriptorSize = sizeof(SECURITY_DESCRIPTOR);
        status = STATUS_BUFFER_TOO_SMALL;
    }

    if ((pRel->Control & SE_DACL_PRESENT) && pRel->Dacl &&
        *lpdwDaclSize  < ((PACL)SELF_RELATIVE_FIELD(pRel,Dacl))->AclSize)
    {
        *lpdwDaclSize = ((PACL)SELF_RELATIVE_FIELD(pRel,Dacl))->AclSize;
        status = STATUS_BUFFER_TOO_SMALL;
    }

    if ((pRel->Control & SE_SACL_PRESENT) && pRel->Sacl &&
        *lpdwSaclSize  < ((PACL)SELF_RELATIVE_FIELD(pRel,Sacl))->AclSize)
    {
        *lpdwSaclSize = ((PACL)SELF_RELATIVE_FIELD(pRel,Sacl))->AclSize;
        status = STATUS_BUFFER_TOO_SMALL;
    }

    if (pRel->Owner &&
        *lpdwOwnerSize < RtlLengthSid((PSID)SELF_RELATIVE_FIELD(pRel,Owner)))
    {
        *lpdwOwnerSize = RtlLengthSid((PSID)SELF_RELATIVE_FIELD(pRel,Owner));
        status = STATUS_BUFFER_TOO_SMALL;
    }

    if (pRel->Group &&
        *lpdwPrimaryGroupSize < RtlLengthSid((PSID)SELF_RELATIVE_FIELD(pRel,Group)))
    {
        *lpdwPrimaryGroupSize = RtlLengthSid((PSID)SELF_RELATIVE_FIELD(pRel,Group));
        status = STATUS_BUFFER_TOO_SMALL;
    }

    if (status != STATUS_SUCCESS)
        return status;

    /* Copy structures, and clear the ones we don't set */
    pAbs->Revision = pRel->Revision;
    pAbs->Control = pRel->Control & ~SE_SELF_RELATIVE;
    pAbs->Sacl = NULL;
    pAbs->Dacl = NULL;
    pAbs->Owner = NULL;
    pAbs->Group = NULL;

    if ((pRel->Control & SE_SACL_PRESENT) && pRel->Sacl)
    {
        PACL pAcl = (PACL)SELF_RELATIVE_FIELD( pRel, Sacl );

        memcpy(pSacl, pAcl, pAcl->AclSize);
        pAbs->Sacl = pSacl;
    }

    if ((pRel->Control & SE_DACL_PRESENT) && pRel->Dacl)
    {
        PACL pAcl = (PACL)SELF_RELATIVE_FIELD( pRel, Dacl );
        memcpy(pDacl, pAcl, pAcl->AclSize);
        pAbs->Dacl = pDacl;
    }

    if (pRel->Owner)
    {
        PSID psid = (PSID)SELF_RELATIVE_FIELD( pRel, Owner );
        memcpy(pOwner, psid, RtlLengthSid(psid));
        pAbs->Owner = pOwner;
    }

    if (pRel->Group)
    {
        PSID psid = (PSID)SELF_RELATIVE_FIELD( pRel, Group );
        memcpy(pPrimaryGroup, psid, RtlLengthSid(psid));
        pAbs->Group = pPrimaryGroup;
    }

    return status;
}

/******************************************************************************
 * RtlGetControlSecurityDescriptor (NTDLL.@)
 */
NTSTATUS WINAPI RtlGetControlSecurityDescriptor(
    PSECURITY_DESCRIPTOR pSecurityDescriptor,
    PSECURITY_DESCRIPTOR_CONTROL pControl,
    LPDWORD lpdwRevision)
{
    SECURITY_DESCRIPTOR *lpsd = pSecurityDescriptor;

    TRACE("(%p,%p,%p)\n",pSecurityDescriptor,pControl,lpdwRevision);

    *lpdwRevision = lpsd->Revision;

    if (*lpdwRevision != SECURITY_DESCRIPTOR_REVISION)
        return STATUS_UNKNOWN_REVISION;

    *pControl = lpsd->Control;

    return STATUS_SUCCESS;
}

/******************************************************************************
 * RtlSetControlSecurityDescriptor (NTDLL.@)
 */
NTSTATUS WINAPI RtlSetControlSecurityDescriptor(
    PSECURITY_DESCRIPTOR SecurityDescriptor,
    SECURITY_DESCRIPTOR_CONTROL ControlBitsOfInterest,
    SECURITY_DESCRIPTOR_CONTROL ControlBitsToSet)
{
    SECURITY_DESCRIPTOR_CONTROL const immutable
       = SE_OWNER_DEFAULTED  | SE_GROUP_DEFAULTED
       | SE_DACL_PRESENT     | SE_DACL_DEFAULTED
       | SE_SACL_PRESENT     | SE_SACL_DEFAULTED
       | SE_RM_CONTROL_VALID | SE_SELF_RELATIVE
       ;

    SECURITY_DESCRIPTOR *lpsd = SecurityDescriptor;

    TRACE("(%p 0x%04x 0x%04x)\n", SecurityDescriptor,
          ControlBitsOfInterest, ControlBitsToSet);

    if ((ControlBitsOfInterest | ControlBitsToSet) & immutable)
        return STATUS_INVALID_PARAMETER;

    lpsd->Control |=  (ControlBitsOfInterest &  ControlBitsToSet);
    lpsd->Control &= ~(ControlBitsOfInterest & ~ControlBitsToSet);

    return STATUS_SUCCESS;
}


/**************************************************************************
 *                 RtlAbsoluteToSelfRelativeSD [NTDLL.@]
 */
NTSTATUS WINAPI RtlAbsoluteToSelfRelativeSD(
    PSECURITY_DESCRIPTOR AbsoluteSecurityDescriptor,
    PSECURITY_DESCRIPTOR SelfRelativeSecurityDescriptor,
    PULONG BufferLength)
{
    SECURITY_DESCRIPTOR *abs = AbsoluteSecurityDescriptor;

    TRACE("%p %p %p\n", AbsoluteSecurityDescriptor,
          SelfRelativeSecurityDescriptor, BufferLength);

    if (abs->Control & SE_SELF_RELATIVE)
        return STATUS_BAD_DESCRIPTOR_FORMAT;

    return RtlMakeSelfRelativeSD(AbsoluteSecurityDescriptor, 
        SelfRelativeSecurityDescriptor, BufferLength);
}

/******************************************************************************
 *  RtlNewSecurityObject		[NTDLL.@]
 */
NTSTATUS WINAPI RtlNewSecurityObject(PSECURITY_DESCRIPTOR parent, PSECURITY_DESCRIPTOR creator,
    PSECURITY_DESCRIPTOR *descr, BOOLEAN is_container, HANDLE token, PGENERIC_MAPPING mapping)
{
    return RtlNewSecurityObjectEx(parent, creator, descr, NULL, is_container, 0, token, mapping);
}

/* Private object security (MS docs: CreatePrivateObjectSecurityEx and
 * "ACE Inheritance Rules"). ACLs are built in a growing buffer. */

#ifndef SEF_DACL_AUTO_INHERIT
#define SEF_DACL_AUTO_INHERIT             0x01
#define SEF_SACL_AUTO_INHERIT             0x02
#define SEF_DEFAULT_DESCRIPTOR_FOR_OBJECT 0x04
#define SEF_AVOID_PRIVILEGE_CHECK         0x08
#define SEF_AVOID_OWNER_CHECK             0x10
#define SEF_DEFAULT_OWNER_FROM_PARENT     0x20
#define SEF_DEFAULT_GROUP_FROM_PARENT     0x40
#endif

struct acl_builder
{
    BYTE  *buf;
    DWORD  size;   /* bytes used, including the ACL header */
    DWORD  alloc;
    DWORD  count;
};

static BOOL acl_builder_add( struct acl_builder *b, const ACE_HEADER *ace )
{
    if (b->size + ace->AceSize > b->alloc)
    {
        DWORD alloc = max( b->alloc * 2, b->size + ace->AceSize + 256 );
        BYTE *buf = b->buf ? RtlReAllocateHeap( GetProcessHeap(), 0, b->buf, alloc )
                           : RtlAllocateHeap( GetProcessHeap(), 0, alloc );
        if (!buf) return FALSE;
        b->buf = buf;
        b->alloc = alloc;
    }
    memcpy( b->buf + b->size, ace, ace->AceSize );
    b->size += ace->AceSize;
    b->count++;
    return TRUE;
}

static BOOL is_object_ace( BYTE type )
{
    return type == ACCESS_ALLOWED_OBJECT_ACE_TYPE || type == ACCESS_DENIED_OBJECT_ACE_TYPE ||
           type == ACCESS_AUDIT_OBJECT_ACE_TYPE || type == ACCESS_ALARM_OBJECT_ACE_TYPE;
}

/* the SID of an ACE, and where its GUIDs are for object ACEs */
static SID *ace_sid( const ACE_HEADER *ace, GUID **object_type, GUID **inherited_type )
{
    const BYTE *ptr = (const BYTE *)(ace + 1) + sizeof(DWORD);  /* past the mask */
    DWORD flags;

    if (object_type) *object_type = NULL;
    if (inherited_type) *inherited_type = NULL;
    if (!is_object_ace( ace->AceType )) return (SID *)ptr;
    memcpy( &flags, ptr, sizeof(flags) );
    ptr += sizeof(flags);
    if (flags & ACE_OBJECT_TYPE_PRESENT)
    {
        if (object_type) *object_type = (GUID *)ptr;
        ptr += sizeof(GUID);
    }
    if (flags & ACE_INHERITED_OBJECT_TYPE_PRESENT)
    {
        if (inherited_type) *inherited_type = (GUID *)ptr;
        ptr += sizeof(GUID);
    }
    return (SID *)ptr;
}

/* a copy of an ACE with new flags, a new SID and a new mask */
static BOOL add_ace_copy( struct acl_builder *b, const ACE_HEADER *ace, BYTE flags, const SID *sid,
                          ACCESS_MASK mask )
{
    const SID *old_sid = ace_sid( ace, NULL, NULL );
    DWORD head = (const BYTE *)old_sid - (const BYTE *)ace;
    DWORD sid_len = RtlLengthSid( (PSID)sid );
    BYTE buf[sizeof(ACCESS_ALLOWED_OBJECT_ACE) + 2 * sizeof(GUID) + SECURITY_MAX_SID_SIZE];
    ACE_HEADER *out = (ACE_HEADER *)buf;

    if (head + sid_len > sizeof(buf)) return FALSE;
    memcpy( buf, ace, head );
    memcpy( buf + head, sid, sid_len );
    out->AceFlags = flags;
    out->AceSize = head + sid_len;
    *(ACCESS_MASK *)(out + 1) = mask;
    return acl_builder_add( b, out );
}

static BOOL acl_builder_add_flags( struct acl_builder *b, const ACE_HEADER *ace, BYTE flags )
{
    return add_ace_copy( b, ace, flags, ace_sid( ace, NULL, NULL ), *(const ACCESS_MASK *)(ace + 1) );
}

static const SID creator_owner = { SID_REVISION, 1, { SECURITY_CREATOR_SID_AUTHORITY }, { SECURITY_CREATOR_OWNER_RID } };
static const SID creator_group = { SID_REVISION, 1, { SECURITY_CREATOR_SID_AUTHORITY }, { SECURITY_CREATOR_GROUP_RID } };

static ACCESS_MASK map_generic( ACCESS_MASK mask, const GENERIC_MAPPING *mapping )
{
    RtlMapGenericMask( &mask, mapping );
    return mask;
}

#define GENERIC_RIGHTS (GENERIC_READ | GENERIC_WRITE | GENERIC_EXECUTE | GENERIC_ALL)
#define INHERIT_FLAGS  (OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE | NO_PROPAGATE_INHERIT_ACE | INHERIT_ONLY_ACE)

/* add the ACE an effective ACE becomes in the new object: creator SIDs
 * replaced, generic rights mapped; if it also propagates and needed changes,
 * an inherit-only copy of the original follows */
static BOOL add_effective_ace( struct acl_builder *b, const ACE_HEADER *ace, BYTE flags, const SID *owner,
                               const SID *group, const GENERIC_MAPPING *mapping )
{
    const SID *sid = ace_sid( ace, NULL, NULL ), *eff = sid;
    ACCESS_MASK mask = *(const ACCESS_MASK *)(ace + 1);

    if (RtlEqualSid( (PSID)sid, (PSID)&creator_owner )) eff = owner;
    else if (RtlEqualSid( (PSID)sid, (PSID)&creator_group )) eff = group;

    if (eff == sid && !(mask & GENERIC_RIGHTS)) return add_ace_copy( b, ace, flags, sid, mask );
    if (!add_ace_copy( b, ace, flags & ~INHERIT_FLAGS, eff, map_generic( mask, mapping ) )) return FALSE;
    if (flags & (OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE))
        return add_ace_copy( b, ace, flags | INHERIT_ONLY_ACE, sid, mask );
    return TRUE;
}

/* an explicit ACE of a creator's or caller's ACL: generic rights are mapped
 * in place (Windows 10 oracle: PrivateObjectSecurity creator_generic_mapped) */
static BOOL add_explicit_ace( struct acl_builder *b, const ACE_HEADER *ace, const GENERIC_MAPPING *mapping )
{
    ACCESS_MASK mask = *(const ACCESS_MASK *)(ace + 1);

    if ((ace->AceFlags & INHERIT_ONLY_ACE) || !(mask & GENERIC_RIGHTS)) return acl_builder_add( b, ace );
    return add_ace_copy( b, ace, ace->AceFlags, ace_sid( ace, NULL, NULL ), map_generic( mask, mapping ) );
}

/* the ACEs a new object inherits from one parent ACE */
static BOOL inherit_parent_ace( struct acl_builder *b, const ACE_HEADER *ace, BOOL is_container, BOOL auto_inherit,
                                GUID *type,
                                const SID *owner, const SID *group, const GENERIC_MAPPING *mapping, BOOL *any )
{
    BYTE flags = ace->AceFlags, inh;
    GUID *inherited_type;

    ace_sid( ace, NULL, &inherited_type );
    if (is_container)
    {
        if (flags & CONTAINER_INHERIT_ACE)
        {
            inh = (flags & ~INHERIT_ONLY_ACE) | INHERITED_ACE;
            if (flags & NO_PROPAGATE_INHERIT_ACE) inh &= ~INHERIT_FLAGS;
        }
        else if ((flags & OBJECT_INHERIT_ACE) && !(flags & NO_PROPAGATE_INHERIT_ACE))
            inh = flags | INHERIT_ONLY_ACE | INHERITED_ACE;
        else return TRUE;
    }
    else
    {
        if (!(flags & OBJECT_INHERIT_ACE)) return TRUE;
        inh = (flags & ~INHERIT_FLAGS) | INHERITED_ACE;
    }
    /* inherited ACEs are only marked as such in auto-inherit mode; mandatory
     * labels always are (Windows 10 oracle: PrivateObjectSecurity) */
    if (!auto_inherit && ace->AceType != SYSTEM_MANDATORY_LABEL_ACE_TYPE) inh &= ~INHERITED_ACE;
    /* an ACE for another object type is passed on but not effective */
    if (inherited_type && (!type || !IsEqualGUID( inherited_type, type )))
    {
        if (!is_container || !(inh & (OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE))) return TRUE;
        inh |= INHERIT_ONLY_ACE;
    }
    *any = TRUE;
    if (inh & INHERIT_ONLY_ACE) return acl_builder_add_flags( b, ace, inh );
    return add_effective_ace( b, ace, inh, owner, group, mapping );
}

/* explicit ACEs of the creator's ACL (generic rights of effective ACEs mapped),
 * followed by the ACEs inherited from the parent's ACL */
static NTSTATUS build_new_acl( const ACL *explicit, const ACL *parent, BOOL is_container, BOOL auto_inherit,
                               GUID *type,
                               const SID *owner, const SID *group, const GENERIC_MAPPING *mapping,
                               ACL **ret, BOOL *inherited )
{
    struct acl_builder b = { NULL, sizeof(ACL), 0, 0 };
    ACE_HEADER *ace;
    DWORD i;
    ACL *acl;

    *inherited = FALSE;
    b.alloc = sizeof(ACL) + 256;
    if (!(b.buf = RtlAllocateHeap( GetProcessHeap(), 0, b.alloc ))) return STATUS_NO_MEMORY;
    if (explicit)
    {
        for (i = 0, ace = (ACE_HEADER *)(explicit + 1); i < explicit->AceCount;
             i++, ace = (ACE_HEADER *)((BYTE *)ace + ace->AceSize))
        {
            BOOL ok;
            if (ace->AceFlags & INHERITED_ACE) continue;
            ok = add_explicit_ace( &b, ace, mapping );
            if (!ok) goto oom;
        }
    }
    if (parent)
    {
        for (i = 0, ace = (ACE_HEADER *)(parent + 1); i < parent->AceCount;
             i++, ace = (ACE_HEADER *)((BYTE *)ace + ace->AceSize))
            if (!inherit_parent_ace( &b, ace, is_container, auto_inherit, type, owner, group, mapping, inherited ))
                goto oom;
    }
    acl = (ACL *)b.buf;
    acl->AclRevision = ACL_REVISION;
    for (i = 0, ace = (ACE_HEADER *)(acl + 1); i < b.count; i++, ace = (ACE_HEADER *)((BYTE *)ace + ace->AceSize))
        if (is_object_ace( ace->AceType )) acl->AclRevision = ACL_REVISION4;
    acl->Sbz1 = 0;
    acl->AclSize = b.size;
    acl->AceCount = b.count;
    acl->Sbz2 = 0;
    *ret = acl;
    return STATUS_SUCCESS;
oom:
    RtlFreeHeap( GetProcessHeap(), 0, b.buf );
    return STATUS_NO_MEMORY;
}

static BOOL acl_has_inheritable( const ACL *acl, BOOL is_container )
{
    const ACE_HEADER *ace;
    DWORD i;

    if (!acl) return FALSE;
    for (i = 0, ace = (const ACE_HEADER *)(acl + 1); i < acl->AceCount;
         i++, ace = (const ACE_HEADER *)((const BYTE *)ace + ace->AceSize))
    {
        if (is_container && (ace->AceFlags & CONTAINER_INHERIT_ACE)) return TRUE;
        if (ace->AceFlags & OBJECT_INHERIT_ACE)
        {
            if (!is_container || !(ace->AceFlags & NO_PROPAGATE_INHERIT_ACE)) return TRUE;
        }
    }
    return FALSE;
}

static void *query_token( HANDLE token, TOKEN_INFORMATION_CLASS class )
{
    ULONG len = 0;
    void *buf;

    NtQueryInformationToken( token, class, NULL, 0, &len );
    if (!len || !(buf = RtlAllocateHeap( GetProcessHeap(), 0, len ))) return NULL;
    if (NtQueryInformationToken( token, class, buf, len, &len ))
    {
        RtlFreeHeap( GetProcessHeap(), 0, buf );
        return NULL;
    }
    return buf;
}

static const ACL *sd_acl( PSECURITY_DESCRIPTOR sd, BOOL dacl, BOOL *present, BOOL *defaulted )
{
    BOOLEAN p = FALSE, d = FALSE;
    PACL acl = NULL;

    if (sd)
    {
        if (dacl) RtlGetDaclSecurityDescriptor( sd, &p, &acl, &d );
        else RtlGetSaclSecurityDescriptor( sd, &p, &acl, &d );
    }
    *present = p;
    *defaulted = d;
    return p ? acl : NULL;
}

/* the new DACL or SACL: control receives the matching control bits */
static NTSTATUS new_object_acl( PSECURITY_DESCRIPTOR parent, PSECURITY_DESCRIPTOR creator, BOOL dacl,
                                BOOL auto_inherit, BOOL is_container, GUID *type,
                                const SID *owner, const SID *group, const GENERIC_MAPPING *mapping,
                                const ACL *token_default, ACL **ret, BOOL *present, SECURITY_DESCRIPTOR_CONTROL *control )
{
    SECURITY_DESCRIPTOR_CONTROL cctl = 0, pctl = 0;
    BOOL cpresent, cdefaulted, ppresent, pdefaulted, inherited;
    const ACL *cacl = sd_acl( creator, dacl, &cpresent, &cdefaulted );
    const ACL *pacl = sd_acl( parent, dacl, &ppresent, &pdefaulted );
    SECURITY_DESCRIPTOR_CONTROL protected_bit = dacl ? SE_DACL_PROTECTED : SE_SACL_PROTECTED;
    SECURITY_DESCRIPTOR_CONTROL auto_bit = dacl ? SE_DACL_AUTO_INHERITED : SE_SACL_AUTO_INHERITED;
    DWORD rev;
    NTSTATUS status;

    *ret = NULL;
    *present = FALSE;
    *control = 0;
    if (creator) RtlGetControlSecurityDescriptor( creator, &cctl, &rev );
    if (parent) RtlGetControlSecurityDescriptor( parent, &pctl, &rev );

    /* a defaulted creator ACL only applies when nothing is inherited */
    if (cpresent && cdefaulted && acl_has_inheritable( pacl, is_container )) cpresent = FALSE;

    if (cpresent && (!auto_inherit || (cctl & protected_bit) || !cacl))
    {
        /* the creator's ACL as it is, generic rights mapped */
        *present = TRUE;
        if (cacl && (status = build_new_acl( cacl, NULL, is_container, FALSE, type, owner, group, mapping, ret, &inherited )))
            return status;
        *control = cctl & protected_bit;
        if (auto_inherit) *control |= auto_bit;
        return STATUS_SUCCESS;
    }
    if (cpresent || acl_has_inheritable( pacl, is_container ))
    {
        if ((status = build_new_acl( cpresent ? cacl : NULL, pacl, is_container, auto_inherit, type, owner, group, mapping,
                                     ret, &inherited )))
            return status;
        *present = TRUE;
        if (auto_inherit) *control |= auto_bit;
        return STATUS_SUCCESS;
    }
    if (dacl && token_default)
    {
        if ((status = build_new_acl( token_default, NULL, is_container, FALSE, type, owner, group, mapping, ret,
                                     &inherited )))
            return status;
        *present = TRUE;
    }
    return STATUS_SUCCESS;
}

/******************************************************************************
 *  RtlNewSecurityObjectEx              [NTDLL.@]
 */
NTSTATUS WINAPI RtlNewSecurityObjectEx(PSECURITY_DESCRIPTOR parent, PSECURITY_DESCRIPTOR creator,
    PSECURITY_DESCRIPTOR *descr, GUID *type, BOOLEAN is_container, ULONG flags, HANDLE token, PGENERIC_MAPPING mapping )
{
    SECURITY_DESCRIPTOR_CONTROL dctl = 0, sctl = 0;
    PSID owner = NULL, group = NULL;
    BOOLEAN defaulted;
    TOKEN_OWNER *towner = NULL;
    TOKEN_PRIMARY_GROUP *tgroup = NULL;
    TOKEN_DEFAULT_DACL *tdacl = NULL;
    ACL *dacl = NULL, *sacl = NULL;
    BOOL dpresent, spresent;
    SECURITY_DESCRIPTOR abs;
    NTSTATUS status;
    ULONG len = 0;

    TRACE("%p, %p, %p, %p, %d, %#lx, %p %p\n", parent, creator, descr, type, is_container, flags, token, mapping);

    if (!descr || !mapping) return STATUS_INVALID_PARAMETER;
    if (!token) return STATUS_NO_TOKEN;

    if (creator) RtlGetOwnerSecurityDescriptor( creator, &owner, &defaulted );
    if (!owner && parent && (flags & SEF_DEFAULT_OWNER_FROM_PARENT))
        RtlGetOwnerSecurityDescriptor( parent, &owner, &defaulted );
    if (!owner)
    {
        status = STATUS_NO_TOKEN;
        if (!(towner = query_token( token, TokenOwner ))) goto done;
        owner = towner->Owner;
    }

    if (creator) RtlGetGroupSecurityDescriptor( creator, &group, &defaulted );
    if (!group && parent && (flags & SEF_DEFAULT_GROUP_FROM_PARENT))
        RtlGetGroupSecurityDescriptor( parent, &group, &defaulted );
    if (!group)
    {
        status = STATUS_NO_TOKEN;
        if (!(tgroup = query_token( token, TokenPrimaryGroup ))) goto done;
        group = tgroup->PrimaryGroup;
    }

    tdacl = query_token( token, TokenDefaultDacl );
    if ((status = new_object_acl( parent, creator, TRUE, !!(flags & SEF_DACL_AUTO_INHERIT),
                                  is_container, type, owner, group,
                                  mapping, tdacl ? tdacl->DefaultDacl : NULL, &dacl, &dpresent, &dctl )))
        goto done;
    if ((status = new_object_acl( parent, creator, FALSE, !!(flags & SEF_SACL_AUTO_INHERIT),
                                  is_container, type, owner, group,
                                  mapping, NULL, &sacl, &spresent, &sctl )))
        goto done;

    /* a SACL holding a mandatory label is auto-inherited */
    if (spresent && sacl)
    {
        const ACE_HEADER *ace;
        DWORD i;
        for (i = 0, ace = (const ACE_HEADER *)(sacl + 1); i < sacl->AceCount;
             i++, ace = (const ACE_HEADER *)((const BYTE *)ace + ace->AceSize))
            if (ace->AceType == SYSTEM_MANDATORY_LABEL_ACE_TYPE) sctl |= SE_SACL_AUTO_INHERITED;
    }

    RtlCreateSecurityDescriptor( &abs, SECURITY_DESCRIPTOR_REVISION );
    RtlSetOwnerSecurityDescriptor( &abs, owner, FALSE );
    RtlSetGroupSecurityDescriptor( &abs, group, FALSE );
    if (dpresent) RtlSetDaclSecurityDescriptor( &abs, TRUE, dacl, FALSE );
    if (spresent) RtlSetSaclSecurityDescriptor( &abs, TRUE, sacl, FALSE );
    abs.Control |= dctl | sctl;

    RtlMakeSelfRelativeSD( &abs, NULL, &len );
    status = STATUS_NO_MEMORY;
    if (!(*descr = RtlAllocateHeap( GetProcessHeap(), 0, len ))) goto done;
    if ((status = RtlMakeSelfRelativeSD( &abs, *descr, &len )))
    {
        RtlFreeHeap( GetProcessHeap(), 0, *descr );
        *descr = NULL;
    }

done:
    RtlFreeHeap( GetProcessHeap(), 0, dacl );
    RtlFreeHeap( GetProcessHeap(), 0, sacl );
    RtlFreeHeap( GetProcessHeap(), 0, towner );
    RtlFreeHeap( GetProcessHeap(), 0, tgroup );
    RtlFreeHeap( GetProcessHeap(), 0, tdacl );
    return status;
}

/* the explicit (not inherited) ACEs of acl, generic rights mapped, followed
 * by the inherited ACEs of old_acl */
static NTSTATUS merge_inherited( const ACL *acl, const ACL *old_acl, const SID *owner, const SID *group,
                                 const GENERIC_MAPPING *mapping, ACL **ret )
{
    struct acl_builder b = { NULL, sizeof(ACL), sizeof(ACL) + 256, 0 };
    const ACE_HEADER *ace;
    DWORD i;
    ACL *out;

    if (!(b.buf = RtlAllocateHeap( GetProcessHeap(), 0, b.alloc ))) return STATUS_NO_MEMORY;
    for (i = 0, ace = acl ? (const ACE_HEADER *)(acl + 1) : NULL; acl && i < acl->AceCount;
         i++, ace = (const ACE_HEADER *)((const BYTE *)ace + ace->AceSize))
    {
        BOOL ok;
        if (ace->AceFlags & INHERITED_ACE) continue;
        ok = add_explicit_ace( &b, ace, mapping );
        if (!ok) goto oom;
    }
    for (i = 0, ace = old_acl ? (const ACE_HEADER *)(old_acl + 1) : NULL; old_acl && i < old_acl->AceCount;
         i++, ace = (const ACE_HEADER *)((const BYTE *)ace + ace->AceSize))
        if ((ace->AceFlags & INHERITED_ACE) && !acl_builder_add( &b, ace )) goto oom;
    out = (ACL *)b.buf;
    out->AclRevision = ACL_REVISION;
    for (i = 0, ace = (const ACE_HEADER *)(out + 1); i < b.count;
         i++, ace = (const ACE_HEADER *)((const BYTE *)ace + ace->AceSize))
        if (is_object_ace( ace->AceType )) out->AclRevision = ACL_REVISION4;
    out->Sbz1 = 0;
    out->AclSize = b.size;
    out->AceCount = b.count;
    out->Sbz2 = 0;
    *ret = out;
    return STATUS_SUCCESS;
oom:
    RtlFreeHeap( GetProcessHeap(), 0, b.buf );
    return STATUS_NO_MEMORY;
}

/* replace *descr with a self-relative copy of abs */
static NTSTATUS replace_descr( SECURITY_DESCRIPTOR *abs, PSECURITY_DESCRIPTOR *descr )
{
    PSECURITY_DESCRIPTOR sd;
    ULONG len = 0;
    NTSTATUS status;

    RtlMakeSelfRelativeSD( abs, NULL, &len );
    if (!(sd = RtlAllocateHeap( GetProcessHeap(), 0, len ))) return STATUS_NO_MEMORY;
    if ((status = RtlMakeSelfRelativeSD( abs, sd, &len )))
    {
        RtlFreeHeap( GetProcessHeap(), 0, sd );
        return status;
    }
    RtlFreeHeap( GetProcessHeap(), 0, *descr );
    *descr = sd;
    return STATUS_SUCCESS;
}

/******************************************************************************
 *  RtlSetSecurityObjectEx              [NTDLL.@]
 */
NTSTATUS WINAPI RtlSetSecurityObjectEx( SECURITY_INFORMATION info, PSECURITY_DESCRIPTOR modify,
                                        PSECURITY_DESCRIPTOR *descr, ULONG flags, PGENERIC_MAPPING mapping,
                                        HANDLE token )
{
    SECURITY_DESCRIPTOR_CONTROL old_ctl = 0, new_ctl = 0, ctl = 0;
    BOOLEAN defaulted;
    PSID owner = NULL, group = NULL;
    PACL old_dacl = NULL, old_sacl = NULL, dacl = NULL, sacl = NULL;
    ACL *new_dacl = NULL, *new_sacl = NULL;
    BOOLEAN dpresent = FALSE, spresent = FALSE;
    SECURITY_DESCRIPTOR abs;
    NTSTATUS status;
    DWORD rev;

    TRACE( "%#lx %p %p %#lx %p %p\n", info, modify, descr, flags, mapping, token );

    if (!descr || !*descr || !modify || !mapping) return STATUS_INVALID_PARAMETER;

    RtlGetControlSecurityDescriptor( *descr, &old_ctl, &rev );
    RtlGetControlSecurityDescriptor( modify, &new_ctl, &rev );

    RtlGetOwnerSecurityDescriptor( *descr, &owner, &defaulted );
    if (info & OWNER_SECURITY_INFORMATION)
    {
        PSID new_owner = NULL;
        RtlGetOwnerSecurityDescriptor( modify, &new_owner, &defaulted );
        status = STATUS_INVALID_OWNER;
        if (!new_owner) goto done;
        owner = new_owner;
    }
    RtlGetGroupSecurityDescriptor( *descr, &group, &defaulted );
    if (info & GROUP_SECURITY_INFORMATION)
    {
        PSID new_group = NULL;
        RtlGetGroupSecurityDescriptor( modify, &new_group, &defaulted );
        status = STATUS_INVALID_PRIMARY_GROUP;
        if (!new_group) goto done;
        group = new_group;
    }

    RtlGetDaclSecurityDescriptor( *descr, &dpresent, &old_dacl, &defaulted );
    ctl |= old_ctl & (SE_DACL_PROTECTED | SE_DACL_AUTO_INHERITED);
    if (info & DACL_SECURITY_INFORMATION)
    {
        RtlGetDaclSecurityDescriptor( modify, &dpresent, &dacl, &defaulted );
        ctl &= ~(SE_DACL_PROTECTED | SE_DACL_AUTO_INHERITED);
        ctl |= new_ctl & SE_DACL_PROTECTED;
        if (dpresent && dacl)
        {
            BOOL keep = (flags & SEF_DACL_AUTO_INHERIT) && !(new_ctl & SE_DACL_PROTECTED);
            if ((status = merge_inherited( dacl, keep ? old_dacl : NULL, owner, group, mapping, &new_dacl )))
                goto done;
            if (flags & SEF_DACL_AUTO_INHERIT) ctl |= (old_ctl | new_ctl) & SE_DACL_AUTO_INHERITED;
        }
    }

    RtlGetSaclSecurityDescriptor( *descr, &spresent, &old_sacl, &defaulted );
    ctl |= old_ctl & (SE_SACL_PROTECTED | SE_SACL_AUTO_INHERITED);
    if (info & SACL_SECURITY_INFORMATION)
    {
        RtlGetSaclSecurityDescriptor( modify, &spresent, &sacl, &defaulted );
        ctl &= ~(SE_SACL_PROTECTED | SE_SACL_AUTO_INHERITED);
        ctl |= new_ctl & SE_SACL_PROTECTED;
        if (spresent && sacl)
        {
            BOOL keep = (flags & SEF_SACL_AUTO_INHERIT) && !(new_ctl & SE_SACL_PROTECTED);
            if ((status = merge_inherited( sacl, keep ? old_sacl : NULL, owner, group, mapping, &new_sacl )))
                goto done;
            if (flags & SEF_SACL_AUTO_INHERIT) ctl |= (old_ctl | new_ctl) & SE_SACL_AUTO_INHERITED;
        }
    }

    RtlCreateSecurityDescriptor( &abs, SECURITY_DESCRIPTOR_REVISION );
    RtlSetOwnerSecurityDescriptor( &abs, owner, FALSE );
    RtlSetGroupSecurityDescriptor( &abs, group, FALSE );
    if (dpresent) RtlSetDaclSecurityDescriptor( &abs, TRUE, new_dacl ? new_dacl : (info & DACL_SECURITY_INFORMATION) ? dacl : old_dacl, FALSE );
    if (spresent) RtlSetSaclSecurityDescriptor( &abs, TRUE, new_sacl ? new_sacl : (info & SACL_SECURITY_INFORMATION) ? sacl : old_sacl, FALSE );
    abs.Control |= ctl;
    status = replace_descr( &abs, descr );

done:
    RtlFreeHeap( GetProcessHeap(), 0, new_dacl );
    RtlFreeHeap( GetProcessHeap(), 0, new_sacl );
    return status;
}

/******************************************************************************
 *  RtlSetSecurityObject              [NTDLL.@]
 */
NTSTATUS WINAPI RtlSetSecurityObject( SECURITY_INFORMATION info, PSECURITY_DESCRIPTOR modify,
                                      PSECURITY_DESCRIPTOR *descr, PGENERIC_MAPPING mapping, HANDLE token )
{
    return RtlSetSecurityObjectEx( info, modify, descr, 0, mapping, token );
}

static BOOL ace_equal_effective( const ACE_HEADER *a, const ACE_HEADER *b )
{
    return a->AceType == b->AceType && a->AceSize == b->AceSize &&
           !memcmp( a + 1, b + 1, a->AceSize - sizeof(*a) ) &&
           (a->AceFlags & ~INHERITED_ACE) == (b->AceFlags & ~INHERITED_ACE);
}

/******************************************************************************
 *  RtlConvertToAutoInheritSecurityObject              [NTDLL.@]
 *
 * Builds the auto-inherit form of a descriptor written by a pre-auto-inherit
 * system: ACEs that are what the parent would pass on become inherited ACEs,
 * the rest stay explicit, and the DACL is marked auto-inherited.
 */
NTSTATUS WINAPI RtlConvertToAutoInheritSecurityObject( PSECURITY_DESCRIPTOR parent, PSECURITY_DESCRIPTOR current,
                                                       PSECURITY_DESCRIPTOR *descr, GUID *type, BOOL is_dir,
                                                       PGENERIC_MAPPING mapping )
{
    SECURITY_DESCRIPTOR_CONTROL ctl = 0, cur_ctl = 0;
    BOOLEAN present, defaulted, spresent;
    PSID owner = NULL, group = NULL;
    PACL dacl = NULL, sacl = NULL, pdacl = NULL;
    ACL *expect = NULL, *result = NULL;
    SECURITY_DESCRIPTOR abs;
    struct acl_builder b = { NULL, sizeof(ACL), 0, 0 };
    NTSTATUS status;
    BOOL inherited;
    DWORD rev, i, j;

    TRACE( "%p %p %p %p %d %p\n", parent, current, descr, type, is_dir, mapping );

    if (!current || !descr || !mapping) return STATUS_INVALID_PARAMETER;
    RtlGetControlSecurityDescriptor( current, &cur_ctl, &rev );
    RtlGetOwnerSecurityDescriptor( current, &owner, &defaulted );
    RtlGetGroupSecurityDescriptor( current, &group, &defaulted );
    RtlGetDaclSecurityDescriptor( current, &present, &dacl, &defaulted );
    RtlGetSaclSecurityDescriptor( current, &spresent, &sacl, &defaulted );
    ctl = cur_ctl & (SE_DACL_PROTECTED | SE_SACL_PROTECTED | SE_SACL_AUTO_INHERITED);

    if (present && dacl && !(cur_ctl & SE_DACL_PROTECTED))
    {
        BOOLEAN pp = FALSE, pd;
        if (parent) RtlGetDaclSecurityDescriptor( parent, &pp, &pdacl, &pd );
        if ((status = build_new_acl( NULL, pp ? pdacl : NULL, is_dir, TRUE, type, owner, group, mapping,
                                     &expect, &inherited )))
            return status;
        /* when every ACE the parent passes on is in the current DACL, those
         * become inherited and the remaining ones stay explicit */
        {
            const ACE_HEADER *e, *c;
            BOOL all = expect->AceCount > 0;
            /* with nothing to inherit the DACL stays as it is */
            if (!expect->AceCount) all = FALSE;
            for (i = 0, e = (const ACE_HEADER *)(expect + 1); all && i < expect->AceCount;
                 i++, e = (const ACE_HEADER *)((const BYTE *)e + e->AceSize))
            {
                BOOL found = FALSE;
                for (j = 0, c = (const ACE_HEADER *)(dacl + 1); !found && j < dacl->AceCount;
                     j++, c = (const ACE_HEADER *)((const BYTE *)c + c->AceSize))
                    found = ace_equal_effective( e, c );
                all = found;
            }
            b.alloc = sizeof(ACL) + dacl->AclSize + expect->AclSize;
            status = STATUS_NO_MEMORY;
            if (!(b.buf = RtlAllocateHeap( GetProcessHeap(), 0, b.alloc ))) goto done;
            for (j = 0, c = (const ACE_HEADER *)(dacl + 1); j < dacl->AceCount;
                 j++, c = (const ACE_HEADER *)((const BYTE *)c + c->AceSize))
            {
                BOOL match = FALSE;
                if (all)
                    for (i = 0, e = (const ACE_HEADER *)(expect + 1); !match && i < expect->AceCount;
                         i++, e = (const ACE_HEADER *)((const BYTE *)e + e->AceSize))
                        match = ace_equal_effective( e, c );
                if (!match && !add_effective_ace( &b, c, c->AceFlags, owner, group, mapping )) goto done;
            }
            if (all)
                for (i = 0, e = (const ACE_HEADER *)(expect + 1); i < expect->AceCount;
                     i++, e = (const ACE_HEADER *)((const BYTE *)e + e->AceSize))
                    if (!acl_builder_add( &b, e )) goto done;
            result = (ACL *)b.buf;
            b.buf = NULL;
            result->AclRevision = dacl->AclRevision;
            result->Sbz1 = 0;
            result->AclSize = b.size;
            result->AceCount = b.count;
            result->Sbz2 = 0;
            /* the parent's ACEs are not all there: the DACL is protected */
            if (!all && expect->AceCount) ctl |= SE_DACL_PROTECTED;
        }
    }
    else if (present && !dacl) ctl |= SE_DACL_PROTECTED;
    ctl |= SE_DACL_AUTO_INHERITED | SE_SACL_AUTO_INHERITED;

    RtlCreateSecurityDescriptor( &abs, SECURITY_DESCRIPTOR_REVISION );
    RtlSetOwnerSecurityDescriptor( &abs, owner, FALSE );
    RtlSetGroupSecurityDescriptor( &abs, group, FALSE );
    if (present) RtlSetDaclSecurityDescriptor( &abs, TRUE, result ? result : dacl, FALSE );
    if (spresent) RtlSetSaclSecurityDescriptor( &abs, TRUE, sacl, FALSE );
    abs.Control |= ctl;
    *descr = NULL;
    status = replace_descr( &abs, descr );

done:
    RtlFreeHeap( GetProcessHeap(), 0, b.buf );
    RtlFreeHeap( GetProcessHeap(), 0, expect );
    RtlFreeHeap( GetProcessHeap(), 0, result );
    return status;
}

/******************************************************************************
 *  RtlNewSecurityObjectWithMultipleInheritance        [NTDLL.@]
 */
NTSTATUS WINAPI RtlNewSecurityObjectWithMultipleInheritance(PSECURITY_DESCRIPTOR parent, PSECURITY_DESCRIPTOR creator,
    PSECURITY_DESCRIPTOR *descr, GUID **types, ULONG count, BOOLEAN is_container, ULONG flags,
    HANDLE token, PGENERIC_MAPPING mapping )
{
    FIXME("semi-stub\n");
    return RtlNewSecurityObjectEx(parent, creator, descr, NULL, is_container, flags, token, mapping);
}

/******************************************************************************
 *  RtlDeleteSecurityObject		[NTDLL.@]
 */
NTSTATUS WINAPI RtlDeleteSecurityObject( PSECURITY_DESCRIPTOR *descr )
{
    TRACE("%p\n", descr);
    RtlFreeHeap( GetProcessHeap(), 0, *descr );
    return STATUS_SUCCESS;
}

/*
 *	access control list's
 */

/**************************************************************************
 *                 RtlCreateAcl				[NTDLL.@]
 *
 * NOTES
 *    This should return NTSTATUS
 */
NTSTATUS WINAPI RtlCreateAcl(PACL acl,DWORD size,DWORD rev)
{
	TRACE("%p 0x%08lx 0x%08lx\n", acl, size, rev);

	if (rev < MIN_ACL_REVISION || rev > MAX_ACL_REVISION)
		return STATUS_INVALID_PARAMETER;
	if (size<sizeof(ACL))
		return STATUS_BUFFER_TOO_SMALL;
	if (size>0xFFFF)
		return STATUS_INVALID_PARAMETER;

	memset(acl,'\0',sizeof(ACL));
	acl->AclRevision	= rev;
	acl->AclSize		= size;
	acl->AceCount		= 0;
	return STATUS_SUCCESS;
}

/**************************************************************************
 *                 RtlFirstFreeAce			[NTDLL.@]
 * looks for the AceCount+1 ACE, and if it is still within the alloced
 * ACL, return a pointer to it
 */
BOOLEAN WINAPI RtlFirstFreeAce(
	PACL acl,
	PACE_HEADER *x)
{
	PACE_HEADER	ace;
	int		i;

	*x = 0;
	ace = (PACE_HEADER)(acl+1);
	for (i=0;i<acl->AceCount;i++) {
		if ((BYTE *)ace >= (BYTE *)acl + acl->AclSize)
			return FALSE;
		ace = (PACE_HEADER)(((BYTE*)ace)+ace->AceSize);
	}
	if ((BYTE *)ace <= (BYTE *)acl + acl->AclSize)
		*x = ace;
	return TRUE;
}

/**************************************************************************
 *                 RtlAddAce				[NTDLL.@]
 */
NTSTATUS WINAPI RtlAddAce(
	PACL acl,
	DWORD rev,
	DWORD xnrofaces,
	PACE_HEADER acestart,
	DWORD acelen)
{
	PACE_HEADER	ace,targetace;
	int		nrofaces;

	if (!RtlValidAcl(acl))
		return STATUS_INVALID_PARAMETER;
	if (!RtlFirstFreeAce(acl,&targetace))
		return STATUS_INVALID_PARAMETER;
	if (!targetace)
		return STATUS_ALLOTTED_SPACE_EXCEEDED;
	nrofaces=0;ace=acestart;
	while (((BYTE *)ace - (BYTE *)acestart) < acelen) {
		nrofaces++;
		ace = (PACE_HEADER)(((BYTE*)ace)+ace->AceSize);
	}
	if ((BYTE *)targetace + acelen > (BYTE *)acl + acl->AclSize) /* too much aces */
		return STATUS_INVALID_PARAMETER;
	memcpy(targetace,acestart,acelen);
	acl->AceCount+=nrofaces;
	if (rev > acl->AclRevision)
		acl->AclRevision = rev;
	return STATUS_SUCCESS;
}

/**************************************************************************
 *                 RtlDeleteAce				[NTDLL.@]
 */
NTSTATUS  WINAPI RtlDeleteAce(PACL pAcl, DWORD dwAceIndex)
{
	NTSTATUS status;
	PACE_HEADER pAce;

	status = RtlGetAce(pAcl,dwAceIndex,(LPVOID*)&pAce);

	if (STATUS_SUCCESS == status)
	{
		PACE_HEADER pcAce;
		DWORD len = 0;

		/* skip over the ACE we are deleting */
		pcAce = (PACE_HEADER)(((BYTE*)pAce)+pAce->AceSize);
		dwAceIndex++;

		/* calculate the length of the rest */
		for (; dwAceIndex < pAcl->AceCount; dwAceIndex++)
		{
			len += pcAce->AceSize;
			pcAce = (PACE_HEADER)(((BYTE*)pcAce) + pcAce->AceSize);
		}

		/* slide them all backwards */
		memmove(pAce, ((BYTE*)pAce)+pAce->AceSize, len);
		pAcl->AceCount--;
	}

	TRACE("pAcl=%p dwAceIndex=%ld status=0x%08lx\n", pAcl, dwAceIndex, status);

	return status;
}

/******************************************************************************
 *  RtlAddAccessAllowedAce		[NTDLL.@]
 */
NTSTATUS WINAPI RtlAddAccessAllowedAce(
	IN OUT PACL pAcl,
	IN DWORD dwAceRevision,
	IN DWORD AccessMask,
	IN PSID pSid)
{
	return RtlAddAccessAllowedAceEx( pAcl, dwAceRevision, 0, AccessMask, pSid);
}
 
/******************************************************************************
 *  RtlAddAccessAllowedAceEx		[NTDLL.@]
 */
NTSTATUS WINAPI RtlAddAccessAllowedAceEx(
	IN OUT PACL pAcl,
	IN DWORD dwAceRevision,
	IN DWORD AceFlags,
	IN DWORD AccessMask,
	IN PSID pSid)
{
   TRACE("(%p,0x%08lx,0x%08lx,%p)\n", pAcl, dwAceRevision, AccessMask, pSid);

    return add_access_ace(pAcl, dwAceRevision, AceFlags,
                          AccessMask, pSid, ACCESS_ALLOWED_ACE_TYPE);
}

/******************************************************************************
 *  RtlAddAccessAllowedObjectAce		[NTDLL.@]
 */
NTSTATUS WINAPI RtlAddAccessAllowedObjectAce(
    IN OUT PACL pAcl,
    IN DWORD dwAceRevision,
    IN DWORD dwAceFlags,
    IN DWORD dwAccessMask,
    IN GUID* pObjectTypeGuid,
    IN GUID* pInheritedObjectTypeGuid,
    IN PSID pSid)
{
    FIXME("%p %lx %lx %lx %p %p %p - stub\n", pAcl, dwAceRevision, dwAceFlags, dwAccessMask,
          pObjectTypeGuid, pInheritedObjectTypeGuid, pSid);
    return STATUS_NOT_IMPLEMENTED;
}

/******************************************************************************
 *  RtlAddAccessDeniedAce		[NTDLL.@]
 */
NTSTATUS WINAPI RtlAddAccessDeniedAce(
	IN OUT PACL pAcl,
	IN DWORD dwAceRevision,
	IN DWORD AccessMask,
	IN PSID pSid)
{
	return RtlAddAccessDeniedAceEx( pAcl, dwAceRevision, 0, AccessMask, pSid);
}

/******************************************************************************
 *  RtlAddAccessDeniedAceEx		[NTDLL.@]
 */
NTSTATUS WINAPI RtlAddAccessDeniedAceEx(
	IN OUT PACL pAcl,
	IN DWORD dwAceRevision,
	IN DWORD AceFlags,
	IN DWORD AccessMask,
	IN PSID pSid)
{
   TRACE("(%p,0x%08lx,0x%08lx,%p)\n", pAcl, dwAceRevision, AccessMask, pSid);

    return add_access_ace(pAcl, dwAceRevision, AceFlags,
                          AccessMask, pSid, ACCESS_DENIED_ACE_TYPE);
}

/******************************************************************************
 *  RtlAddAccessDeniedObjectAce [NTDLL.@]
 */
NTSTATUS WINAPI RtlAddAccessDeniedObjectAce(
    IN OUT PACL pAcl,
    IN DWORD dwAceRevision,
    IN DWORD dwAceFlags,
    IN DWORD dwAccessMask,
    IN GUID* pObjectTypeGuid,
    IN GUID* pInheritedObjectTypeGuid,
    IN PSID pSid)
{
    FIXME("%p %lx %lx %lx %p %p %p - stub\n", pAcl, dwAceRevision, dwAceFlags, dwAccessMask,
          pObjectTypeGuid, pInheritedObjectTypeGuid, pSid);
    return STATUS_NOT_IMPLEMENTED;
}

/************************************************************************** 
 *  RtlAddAuditAccessAce     [NTDLL.@] 
 */ 
NTSTATUS WINAPI RtlAddAuditAccessAceEx(
    IN OUT PACL pAcl, 
    IN DWORD dwAceRevision, 
    IN DWORD dwAceFlags,
    IN DWORD dwAccessMask, 
    IN PSID pSid, 
    IN BOOL bAuditSuccess, 
    IN BOOL bAuditFailure) 
{ 
    TRACE("(%p,%ld,0x%08lx,0x%08lx,%p,%u,%u)\n",pAcl,dwAceRevision,dwAceFlags,dwAccessMask,
          pSid,bAuditSuccess,bAuditFailure);

    if (bAuditSuccess)
        dwAceFlags |= SUCCESSFUL_ACCESS_ACE_FLAG;

    if (bAuditFailure)
        dwAceFlags |= FAILED_ACCESS_ACE_FLAG;

    return add_access_ace(pAcl, dwAceRevision, dwAceFlags,
                          dwAccessMask, pSid, SYSTEM_AUDIT_ACE_TYPE);
} 

/**************************************************************************
 *  RtlAddAuditAccessAce     [NTDLL.@]
 */
NTSTATUS WINAPI RtlAddAuditAccessAce(
    IN OUT PACL pAcl,
    IN DWORD dwAceRevision,
    IN DWORD dwAccessMask,
    IN PSID pSid,
    IN BOOL bAuditSuccess,
    IN BOOL bAuditFailure)
{
    return RtlAddAuditAccessAceEx(pAcl, dwAceRevision, 0, dwAccessMask, pSid, bAuditSuccess, bAuditFailure);
}

/******************************************************************************
 *  RtlAddAuditAccessObjectAce [NTDLL.@]
 */
NTSTATUS WINAPI RtlAddAuditAccessObjectAce(
    IN OUT PACL pAcl,
    IN DWORD dwAceRevision,
    IN DWORD dwAceFlags,
    IN DWORD dwAccessMask,
    IN GUID* pObjectTypeGuid,
    IN GUID* pInheritedObjectTypeGuid,
    IN PSID pSid,
    IN BOOL bAuditSuccess,
    IN BOOL bAuditFailure)
{
    FIXME("%p %lx %lx %lx %p %p %p %d %d - stub\n", pAcl, dwAceRevision, dwAceFlags, dwAccessMask,
          pObjectTypeGuid, pInheritedObjectTypeGuid, pSid, bAuditSuccess, bAuditFailure);
    return STATUS_NOT_IMPLEMENTED;
}

/**************************************************************************
 *  RtlAddMandatoryAce     [NTDLL.@]
 */
NTSTATUS WINAPI RtlAddMandatoryAce(
    IN OUT PACL pAcl,
    IN DWORD dwAceRevision,
    IN DWORD dwAceFlags,
    IN DWORD dwMandatoryFlags,
    IN DWORD dwAceType,
    IN PSID pSid)
{
    TRACE("(%p, %lu, 0x%08lx, 0x%08lx, %lu, %p)\n",
          pAcl, dwAceRevision, dwAceFlags, dwMandatoryFlags, dwAceType, pSid);

    if (dwAceType != SYSTEM_MANDATORY_LABEL_ACE_TYPE)
        return STATUS_INVALID_PARAMETER;
    if (dwMandatoryFlags & ~SYSTEM_MANDATORY_LABEL_VALID_MASK)
        return STATUS_INVALID_PARAMETER;

    return add_access_ace(pAcl, dwAceRevision, dwAceFlags, dwMandatoryFlags, pSid, dwAceType);
}

/**************************************************************************
 *  RtlAddProcessTrustLabelAce		[NTDLL.@]
 */
NTSTATUS WINAPI RtlAddProcessTrustLabelAce( ACL *acl, DWORD revision, DWORD flags,
                                            PSID sid, DWORD type, DWORD mask )
{
    TRACE( "%p %lx %lx %p %lx %lx\n", acl, revision, flags, sid, type, mask );

    if (type != SYSTEM_PROCESS_TRUST_LABEL_ACE_TYPE) return STATUS_INVALID_PARAMETER;
    if (mask & ~SYSTEM_PROCESS_TRUST_LABEL_VALID_MASK) return STATUS_INVALID_PARAMETER;

    return add_access_ace( acl, revision, flags, mask, sid, type );
}

/******************************************************************************
 *  RtlValidAcl		[NTDLL.@]
 */
BOOLEAN WINAPI RtlValidAcl(PACL pAcl)
{
        BOOLEAN ret;
	TRACE("(%p)\n", pAcl);

	__TRY
	{
		PACE_HEADER	ace;
		int		i;

                if (pAcl->AclRevision < MIN_ACL_REVISION ||
                    pAcl->AclRevision > MAX_ACL_REVISION)
                    ret = FALSE;
                else
                {
                    ace = (PACE_HEADER)(pAcl+1);
                    ret = TRUE;
                    for (i=0;i<=pAcl->AceCount;i++)
                    {
                        if ((char *)ace > (char *)pAcl + pAcl->AclSize)
                        {
                            ret = FALSE;
                            break;
                        }
                        if (i != pAcl->AceCount)
                            ace = (PACE_HEADER)(((BYTE*)ace)+ace->AceSize);
                    }
                }
	}
	__EXCEPT_PAGE_FAULT
	{
		WARN("(%p): invalid pointer!\n", pAcl);
		return FALSE;
	}
	__ENDTRY
        return ret;
}

/******************************************************************************
 *  RtlGetAce		[NTDLL.@]
 */
NTSTATUS WINAPI RtlGetAce(PACL pAcl,DWORD dwAceIndex,LPVOID *pAce )
{
	PACE_HEADER ace;

	TRACE("(%p,%ld,%p)\n",pAcl,dwAceIndex,pAce);

	if (dwAceIndex >= pAcl->AceCount)
		return STATUS_INVALID_PARAMETER;

	ace = (PACE_HEADER)(pAcl + 1);
	for (;dwAceIndex;dwAceIndex--)
		ace = (PACE_HEADER)(((BYTE*)ace)+ace->AceSize);

	*pAce = ace;

	return STATUS_SUCCESS;
}

/*************************************************************************
 * RtlAreAllAccessesGranted   [NTDLL.@]
 */
BOOLEAN WINAPI RtlAreAllAccessesGranted( ACCESS_MASK granted, ACCESS_MASK desired )
{
    return (granted & desired) == desired;
}

/*************************************************************************
 * RtlAreAnyAccessesGranted   [NTDLL.@]
 */
BOOLEAN WINAPI RtlAreAnyAccessesGranted( ACCESS_MASK granted, ACCESS_MASK desired )
{
    return (granted & desired) != 0;
}

/*************************************************************************
 * RtlMapGenericMask   [NTDLL.@]
 */
void WINAPI RtlMapGenericMask( ACCESS_MASK *mask, const GENERIC_MAPPING *mapping )
{
    if (*mask & GENERIC_READ) *mask |= mapping->GenericRead;
    if (*mask & GENERIC_WRITE) *mask |= mapping->GenericWrite;
    if (*mask & GENERIC_EXECUTE) *mask |= mapping->GenericExecute;
    if (*mask & GENERIC_ALL) *mask |= mapping->GenericAll;
    *mask &= ~(GENERIC_READ | GENERIC_WRITE | GENERIC_EXECUTE | GENERIC_ALL);
}

/*************************************************************************
 * RtlCopyLuid   [NTDLL.@]
 */
void WINAPI RtlCopyLuid( LUID *dest, const LUID *src )
{
    *dest = *src;
}

/*************************************************************************
 * RtlEqualLuid   [NTDLL.@]
 */
BOOLEAN WINAPI RtlEqualLuid( const LUID *luid1, const LUID *luid2 )
{
  return (luid1->LowPart == luid2->LowPart && luid1->HighPart == luid2->HighPart);
}

/*************************************************************************
 * RtlCopyLuidAndAttributesArray   [NTDLL.@]
 */
void WINAPI RtlCopyLuidAndAttributesArray( ULONG count, const LUID_AND_ATTRIBUTES *src, PLUID_AND_ATTRIBUTES dest )
{
    ULONG i;

    for (i = 0; i < count; i++) dest[i] = src[i];
}

/*
 *	misc
 */

/******************************************************************************
 *  RtlAdjustPrivilege		[NTDLL.@]
 *
 * Enables or disables a privilege from the calling thread or process.
 *
 * PARAMS
 *  Privilege     [I] Privilege index to change.
 *  Enable        [I] If TRUE, then enable the privilege otherwise disable.
 *  CurrentThread [I] If TRUE, then enable in calling thread, otherwise process.
 *  Enabled       [O] Whether privilege was previously enabled or disabled.
 *
 * RETURNS
 *  Success: STATUS_SUCCESS.
 *  Failure: NTSTATUS code.
 *
 * SEE ALSO
 *  NtAdjustPrivilegesToken, NtOpenThreadToken, NtOpenProcessToken.
 *
 */
NTSTATUS WINAPI
RtlAdjustPrivilege(ULONG Privilege,
                   BOOLEAN Enable,
                   BOOLEAN CurrentThread,
                   PBOOLEAN Enabled)
{
    TOKEN_PRIVILEGES NewState;
    TOKEN_PRIVILEGES OldState;
    ULONG ReturnLength;
    HANDLE TokenHandle;
    NTSTATUS Status;

    TRACE("(%ld, %s, %s, %p)\n", Privilege, Enable ? "TRUE" : "FALSE",
        CurrentThread ? "TRUE" : "FALSE", Enabled);

    if (CurrentThread)
    {
        Status = NtOpenThreadToken(GetCurrentThread(),
                                   TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY,
                                   FALSE,
                                   &TokenHandle);
    }
    else
    {
        Status = NtOpenProcessToken(GetCurrentProcess(),
                                    TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY,
                                    &TokenHandle);
    }

    if (Status)
    {
        WARN("Retrieving token handle failed (Status %lx)\n", Status);
        return Status;
    }

    OldState.PrivilegeCount = 1;

    NewState.PrivilegeCount = 1;
    NewState.Privileges[0].Luid.LowPart = Privilege;
    NewState.Privileges[0].Luid.HighPart = 0;
    NewState.Privileges[0].Attributes = (Enable) ? SE_PRIVILEGE_ENABLED : 0;

    Status = NtAdjustPrivilegesToken(TokenHandle,
                                     FALSE,
                                     &NewState,
                                     sizeof(TOKEN_PRIVILEGES),
                                     &OldState,
                                     &ReturnLength);
    NtClose (TokenHandle);
    if (Status == STATUS_NOT_ALL_ASSIGNED)
    {
        TRACE("Failed to assign all privileges\n");
        return STATUS_PRIVILEGE_NOT_HELD;
    }
    if (Status)
    {
        WARN("NtAdjustPrivilegesToken() failed (Status %lx)\n", Status);
        return Status;
    }

    if (OldState.PrivilegeCount == 0)
        *Enabled = Enable;
    else
        *Enabled = (OldState.Privileges[0].Attributes & SE_PRIVILEGE_ENABLED);

    return STATUS_SUCCESS;
}

/******************************************************************************
 *  RtlImpersonateSelf		[NTDLL.@]
 *
 * Makes an impersonation token that represents the process user and assigns
 * to the current thread.
 *
 * PARAMS
 *  ImpersonationLevel [I] Level at which to impersonate.
 *
 * RETURNS
 *  Success: STATUS_SUCCESS.
 *  Failure: NTSTATUS code.
 */
NTSTATUS WINAPI
RtlImpersonateSelf(SECURITY_IMPERSONATION_LEVEL ImpersonationLevel)
{
    SECURITY_QUALITY_OF_SERVICE qos;
    NTSTATUS Status;
    OBJECT_ATTRIBUTES attr;
    HANDLE ProcessToken;
    HANDLE ImpersonationToken;

    TRACE("(%08x)\n", ImpersonationLevel);

    Status = NtOpenProcessToken( NtCurrentProcess(), TOKEN_DUPLICATE,
                                 &ProcessToken);
    if (Status != STATUS_SUCCESS)
        return Status;

    qos.Length = sizeof(qos);
    qos.ImpersonationLevel = ImpersonationLevel;
    qos.ContextTrackingMode = SECURITY_STATIC_TRACKING;
    qos.EffectiveOnly = FALSE;
    InitializeObjectAttributes( &attr, NULL, 0, NULL, NULL );
    attr.SecurityQualityOfService = &qos;

    Status = NtDuplicateToken( ProcessToken, TOKEN_IMPERSONATE, &attr, FALSE,
                               TokenImpersonation, &ImpersonationToken );
    if (Status != STATUS_SUCCESS)
    {
        NtClose( ProcessToken );
        return Status;
    }

    Status = NtSetInformationThread( GetCurrentThread(),
                                     ThreadImpersonationToken,
                                     &ImpersonationToken,
                                     sizeof(ImpersonationToken) );

    NtClose( ImpersonationToken );
    NtClose( ProcessToken );

    return Status;
}


/******************************************************************************
 * RtlConvertSidToUnicodeString (NTDLL.@)
 *
 * The returned SID is used to access the USER registry hive usually
 *
 * the native function returns something like
 * "S-1-5-21-0000000000-000000000-0000000000-500";
 */
NTSTATUS WINAPI RtlConvertSidToUnicodeString(
       PUNICODE_STRING String,
       PSID pSid,
       BOOLEAN AllocateString)
{
    WCHAR buffer[2 + 10 + 10 + 10 * SID_MAX_SUB_AUTHORITIES];
    WCHAR *p = buffer;
    const SID *sid = pSid;
    DWORD i, len;

    *p++ = 'S';
    p += swprintf( p, ARRAY_SIZE(buffer) - (p - buffer), L"-%u", sid->Revision );
    p += swprintf( p, ARRAY_SIZE(buffer) - (p - buffer), L"-%u",
                   MAKELONG( MAKEWORD( sid->IdentifierAuthority.Value[5],
                                       sid->IdentifierAuthority.Value[4] ),
                             MAKEWORD( sid->IdentifierAuthority.Value[3],
                                       sid->IdentifierAuthority.Value[2] )));
    for (i = 0; i < sid->SubAuthorityCount; i++)
        p += swprintf( p, ARRAY_SIZE(buffer) - (p - buffer), L"-%u", sid->SubAuthority[i] );

    len = (p + 1 - buffer) * sizeof(WCHAR);

    String->Length = len - sizeof(WCHAR);
    if (AllocateString)
    {
        String->MaximumLength = len;
        if (!(String->Buffer = RtlAllocateHeap( GetProcessHeap(), 0, len )))
            return STATUS_NO_MEMORY;
    }
    else if (len > String->MaximumLength) return STATUS_BUFFER_OVERFLOW;

    memcpy( String->Buffer, buffer, len );
    return STATUS_SUCCESS;
}

/******************************************************************************
 * RtlQueryInformationAcl (NTDLL.@)
 */
NTSTATUS WINAPI RtlQueryInformationAcl(
    PACL pAcl,
    LPVOID pAclInformation,
    DWORD nAclInformationLength,
    ACL_INFORMATION_CLASS dwAclInformationClass)
{
    NTSTATUS status = STATUS_SUCCESS;

    TRACE("pAcl=%p pAclInfo=%p len=%ld, class=%d\n",
        pAcl, pAclInformation, nAclInformationLength, dwAclInformationClass);

    switch (dwAclInformationClass)
    {
        case AclRevisionInformation:
        {
            PACL_REVISION_INFORMATION paclrev = pAclInformation;

            if (nAclInformationLength < sizeof(ACL_REVISION_INFORMATION))
                status = STATUS_INVALID_PARAMETER;
            else
                paclrev->AclRevision = pAcl->AclRevision;

            break;
        }

        case AclSizeInformation:
        {
            PACL_SIZE_INFORMATION paclsize = pAclInformation;

            if (nAclInformationLength < sizeof(ACL_SIZE_INFORMATION))
                status = STATUS_INVALID_PARAMETER;
            else
            {
                paclsize->AceCount = pAcl->AceCount;
                paclsize->AclBytesInUse = acl_bytesInUse(pAcl);
		if (pAcl->AclSize < paclsize->AclBytesInUse)
                {
                    WARN("Acl uses %d bytes, but only has %ld allocated!  Returning smaller of the two values.\n", pAcl->AclSize, paclsize->AclBytesInUse);
                    paclsize->AclBytesFree = 0;
                    paclsize->AclBytesInUse = pAcl->AclSize;
                }
                else
                    paclsize->AclBytesFree = pAcl->AclSize - paclsize->AclBytesInUse;
            }

            break;
        }

        default:
            WARN("Unknown AclInformationClass value: %d\n", dwAclInformationClass);
            status = STATUS_INVALID_PARAMETER;
    }

    return status;
}

/******************************************************************************
 * RtlDefaultNpAcl (NTDLL.@)
 */
NTSTATUS WINAPI RtlDefaultNpAcl(PACL *pAcl)
{
    FIXME("%p - stub\n", pAcl);

    *pAcl = NULL;
    return STATUS_SUCCESS;
}

/******************************************************************************
 * RtlCreateServiceSid [NTDLL.@]
 */
NTSTATUS WINAPI RtlCreateServiceSid( PUNICODE_STRING name, PSID pSid, LPDWORD len )
{
    static const SID_IDENTIFIER_AUTHORITY nt_authority = { SECURITY_NT_AUTHORITY };
    DWORD sid_length;
    NTSTATUS status;
    UNICODE_STRING name_upper;
    hash_state hash_ctx;
    ULONG hash[5];
    SID *sid = (SID *)pSid;

    if (name == NULL || len == NULL) return STATUS_INVALID_PARAMETER;

    sid_length = RtlLengthRequiredSid( 1 + ARRAY_SIZE(hash) );
    if (*len < sid_length)
    {
        *len = sid_length;
        return STATUS_BUFFER_TOO_SMALL;
    }

    sid->Revision = SID_REVISION;
    sid->IdentifierAuthority = nt_authority;
    sid->SubAuthorityCount = 1 + ARRAY_SIZE(hash);
    sid->SubAuthority[0] = SECURITY_SERVICE_ID_BASE_RID;
    *len = sid_length;

    if ((status = RtlUpcaseUnicodeString( &name_upper, name, TRUE ))) return status;

    sha1_init( &hash_ctx );
    sha1_process( &hash_ctx, (UCHAR *)name_upper.Buffer, name_upper.Length );
    sha1_done( &hash_ctx, (UCHAR *)hash );
    RtlFreeUnicodeString( &name_upper );
    memcpy( sid->SubAuthority + 1, hash, sizeof(hash) );
    return STATUS_SUCCESS;
}

/******************************************************************************
 * RtlDeriveCapabilitySidsFromName (NTDLL.@)
 */
NTSTATUS WINAPI RtlDeriveCapabilitySidsFromName( UNICODE_STRING *cap_name, PSID cap_group_sid, PSID cap_sid )
{
    static const SID_IDENTIFIER_AUTHORITY app_authority = { SECURITY_APP_PACKAGE_AUTHORITY };
    static const SID_IDENTIFIER_AUTHORITY nt_authority = { SECURITY_NT_AUTHORITY };
    UNICODE_STRING cap_upcase;
    hash_state hash_ctx;
    NTSTATUS status;
    ULONG hash[8];
    SID *sid;

    TRACE( "cap_name %s, cap_group_sid %p, cap_sid %p.\n", debugstr_us(cap_name), cap_group_sid, cap_sid );

    if ((status = RtlUpcaseUnicodeString( &cap_upcase, cap_name, TRUE ))) return status;
    sha256_init( &hash_ctx );
    sha256_process( &hash_ctx, (UCHAR *)cap_upcase.Buffer, cap_upcase.Length );
    sha256_done( &hash_ctx, (UCHAR *)hash );
    RtlFreeUnicodeString( &cap_upcase );

    sid = cap_sid;
    sid->Revision = SID_REVISION;
    sid->IdentifierAuthority = app_authority;
    sid->SubAuthorityCount = 2 + ARRAY_SIZE(hash);
    sid->SubAuthority[0] = SECURITY_BATCH_RID;
    sid->SubAuthority[1] = SECURITY_CAPABILITY_APP_RID;
    memcpy( sid->SubAuthority + 2, hash, sizeof(hash) );

    sid = cap_group_sid;
    sid->Revision = SID_REVISION;
    sid->IdentifierAuthority = nt_authority;
    sid->SubAuthorityCount = 1 + ARRAY_SIZE(hash);
    sid->SubAuthority[0] = SECURITY_BUILTIN_DOMAIN_RID;
    memcpy( sid->SubAuthority + 1, hash, sizeof(hash) );

    return STATUS_SUCCESS;
}
