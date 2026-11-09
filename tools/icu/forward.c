/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * icuuc.dll and icuin.dll: the ICU DLLs Windows had before icu.dll. Their
 * exports forward to icu.dll (tools/build-icu.sh, icuuc.def / icuin.def),
 * so a process that uses both gets one ICU with one set of data and caches.
 * Built without the C runtime: this entry point is all the code they have.
 *
 * Docs: https://learn.microsoft.com/windows/win32/intl/international-components-for-unicode--icu-
 *       https://learn.microsoft.com/windows/win32/dlls/dllmain
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, void *reserved)
{
    (void)inst;
    (void)reason;
    (void)reserved;
    return TRUE;
}
