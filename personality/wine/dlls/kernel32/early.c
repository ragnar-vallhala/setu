/*
 * Kernel32 exports that must work before kernel32's own imports are bound
 *
 * Applications such as the Firefox launcher record the export address of
 * FlushInstructionCache, GetModuleHandleW, GetSystemInfo and VirtualProtect
 * and call them from a hook that runs while kernel32's dependencies are still
 * being mapped.  On Windows the import tables of system DLLs are pre-bound, so
 * the exports are plain code.  Here the exports used to be thunks through
 * kernel32's import table, which at that point still holds unresolved import
 * names, so the call jumped to a bogus address.
 *
 * The functions below only use the PEB and ntdll exports found by walking the
 * loader list, and fall back to the regular implementation if anything fails.
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

#include <stdarg.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winbase.h"
#include "winternl.h"

#include "kernel_private.h"

static BOOL is_ntdll_name( const UNICODE_STRING *name )
{
    static const char ntdll[] = "ntdll.dll";
    unsigned int i;

    if (name->Length != (sizeof(ntdll) - 1) * sizeof(WCHAR)) return FALSE;
    for (i = 0; i < sizeof(ntdll) - 1; i++)
        if ((name->Buffer[i] | 0x20) != ntdll[i]) return FALSE;
    return TRUE;
}

static BOOL names_equal( const char *a, const char *b )
{
    while (*a && *a == *b) a++, b++;
    return *a == *b;
}

/* find an ntdll export without using any import table */
static void *early_ntdll_proc( const char *name )
{
    LIST_ENTRY *head = &NtCurrentTeb()->Peb->LdrData->InMemoryOrderModuleList, *entry;

    for (entry = head->Flink; entry != head; entry = entry->Flink)
    {
        LDR_DATA_TABLE_ENTRY *mod = CONTAINING_RECORD( entry, LDR_DATA_TABLE_ENTRY, InMemoryOrderLinks );
        BYTE *base = mod->DllBase;
        IMAGE_NT_HEADERS *nt;
        IMAGE_DATA_DIRECTORY *dir;
        IMAGE_EXPORT_DIRECTORY *exp;
        const DWORD *names, *funcs;
        const WORD *ordinals;
        DWORD i;

        if (!is_ntdll_name( &mod->BaseDllName )) continue;
        nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
        dir = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
        if (!dir->VirtualAddress) return NULL;
        exp = (IMAGE_EXPORT_DIRECTORY *)(base + dir->VirtualAddress);
        names = (const DWORD *)(base + exp->AddressOfNames);
        ordinals = (const WORD *)(base + exp->AddressOfNameOrdinals);
        funcs = (const DWORD *)(base + exp->AddressOfFunctions);
        for (i = 0; i < exp->NumberOfNames; i++)
        {
            DWORD rva;
            if (!names_equal( name, (const char *)(base + names[i]) )) continue;
            rva = funcs[ordinals[i]];
            if (rva >= dir->VirtualAddress && rva < dir->VirtualAddress + dir->Size) return NULL; /* forwarder */
            return base + rva;
        }
        return NULL;
    }
    return NULL;
}


/***********************************************************************
 *           early_GetModuleHandleW
 */
HMODULE WINAPI early_GetModuleHandleW( LPCWSTR module )
{
    if (!module) return NtCurrentTeb()->Peb->ImageBaseAddress;
    return GetModuleHandleW( module );
}


/***********************************************************************
 *           early_FlushInstructionCache
 */
BOOL WINAPI early_FlushInstructionCache( HANDLE process, LPCVOID addr, SIZE_T size )
{
    NTSTATUS (WINAPI *flush)( HANDLE, LPCVOID, SIZE_T );

    if (process == GetCurrentProcess() && (flush = early_ntdll_proc( "NtFlushInstructionCache" )))
        if (!flush( process, addr, size )) return TRUE;
    return FlushInstructionCache( process, addr, size );
}


/***********************************************************************
 *           early_VirtualProtect
 */
BOOL WINAPI early_VirtualProtect( void *addr, SIZE_T size, DWORD new_prot, DWORD *old_prot )
{
    NTSTATUS (WINAPI *protect)( HANDLE, void **, SIZE_T *, ULONG, ULONG * );

    if (old_prot && (protect = early_ntdll_proc( "NtProtectVirtualMemory" )))
    {
        void *base = addr;
        SIZE_T len = size;
        ULONG old;

        if (!protect( GetCurrentProcess(), &base, &len, new_prot, &old ))
        {
            *old_prot = old;
            return TRUE;
        }
    }
    return VirtualProtect( addr, size, new_prot, old_prot );
}


/***********************************************************************
 *           early_GetSystemInfo
 */
void WINAPI early_GetSystemInfo( SYSTEM_INFO *si )
{
    NTSTATUS (WINAPI *query)( SYSTEM_INFORMATION_CLASS, void *, ULONG, ULONG * );
    SYSTEM_BASIC_INFORMATION basic;
    SYSTEM_CPU_INFORMATION cpu;

#if defined(__i386__) || defined(__x86_64__)
    if ((query = early_ntdll_proc( "NtQuerySystemInformation" )) &&
        !query( SystemBasicInformation, &basic, sizeof(basic), NULL ) &&
        !query( SystemCpuInformation, &cpu, sizeof(cpu), NULL ) &&
        (cpu.ProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 ||
         cpu.ProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL))
    {
        si->wProcessorArchitecture      = cpu.ProcessorArchitecture;
        si->wReserved                   = 0;
        si->dwPageSize                  = basic.PageSize;
        si->lpMinimumApplicationAddress = basic.LowestUserAddress;
        si->lpMaximumApplicationAddress = basic.HighestUserAddress;
        si->dwActiveProcessorMask       = basic.ActiveProcessorsAffinityMask;
        si->dwAllocationGranularity     = basic.AllocationGranularity;
        si->dwNumberOfProcessors        = cpu.MaximumProcessors;
        si->wProcessorLevel             = cpu.ProcessorLevel;
        si->wProcessorRevision          = cpu.ProcessorRevision;
        if (cpu.ProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64)
            si->dwProcessorType = PROCESSOR_AMD_X8664;
        else if (cpu.ProcessorLevel == 3)
            si->dwProcessorType = PROCESSOR_INTEL_386;
        else if (cpu.ProcessorLevel == 4)
            si->dwProcessorType = PROCESSOR_INTEL_486;
        else
            si->dwProcessorType = PROCESSOR_INTEL_PENTIUM;
        return;
    }
#endif
    GetSystemInfo( si );
}
