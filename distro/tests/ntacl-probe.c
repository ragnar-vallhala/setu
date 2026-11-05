/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * ntacl-probe.exe: a file's security descriptor from the Windows side, for
 * distro/tests/ntacl-smoke.sh (Wine's icacls is a stub).
 *
 *   ntacl-probe set PATH SDDL    SetFileSecurityW(DACL) from an SDDL string
 *   ntacl-probe get PATH         print the owner, group and DACL as SDDL
 *   ntacl-probe open PATH read|write
 *                                CreateFileW for FILE_READ_DATA or FILE_WRITE_DATA
 *   ntacl-probe create PATH      create an empty file
 *
 * Prints "ok" (or the SDDL) and exits 0, or prints "error N" and exits 1.
 * Built by ntacl-smoke.sh with x86_64-w64-mingw32-gcc.
 */
#include <windows.h>
#include <sddl.h>
#include <stdio.h>
#include <wchar.h>

static int fail(void)
{
    printf("error %lu\n", GetLastError());
    return 1;
}

int wmain(int argc, wchar_t **argv)
{
    if (argc == 4 && !wcscmp(argv[1], L"set")) {
        PSECURITY_DESCRIPTOR sd;
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(argv[3], SDDL_REVISION_1, &sd, NULL)) return fail();
        if (!SetFileSecurityW(argv[2], DACL_SECURITY_INFORMATION, sd)) return fail();
        LocalFree(sd);
        printf("ok\n");
        return 0;
    }
    if (argc == 3 && !wcscmp(argv[1], L"get")) {
        SECURITY_INFORMATION si = OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION;
        static BYTE buf[65536];
        DWORD len;
        wchar_t *s;
        if (!GetFileSecurityW(argv[2], si, buf, sizeof(buf), &len)) return fail();
        if (!ConvertSecurityDescriptorToStringSecurityDescriptorW(buf, SDDL_REVISION_1, si, &s, NULL)) return fail();
        printf("%ls\n", s);
        LocalFree(s);
        return 0;
    }
    if (argc == 4 && !wcscmp(argv[1], L"open")) {
        DWORD access = !wcscmp(argv[3], L"write") ? FILE_WRITE_DATA : FILE_READ_DATA;
        HANDLE h = CreateFileW(argv[2], access, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (h == INVALID_HANDLE_VALUE) return fail();
        CloseHandle(h);
        printf("ok\n");
        return 0;
    }
    if (argc == 3 && !wcscmp(argv[1], L"create")) {
        HANDLE h = CreateFileW(argv[2], GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL);
        if (h == INVALID_HANDLE_VALUE) return fail();
        CloseHandle(h);
        printf("ok\n");
        return 0;
    }
    fprintf(stderr, "usage: ntacl-probe set PATH SDDL | get PATH | open PATH read|write | create PATH\n");
    return 2;
}
