/*
 * REG SAVE, REG RESTORE, REG LOAD and REG UNLOAD
 *
 * Copyright 2026 Setu contributors
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

#include "reg.h"

static void enable_privilege(const WCHAR *name)
{
    TOKEN_PRIVILEGES tp;
    HANDLE token;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &token)) return;
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    if (LookupPrivilegeValueW(NULL, name, &tp.Privileges[0].Luid))
        AdjustTokenPrivileges(token, FALSE, &tp, 0, NULL, NULL);
    CloseHandle(token);
}

/* Parses "<key> [<file>] [switches]": the switches allowed are /y (save only)
 * and /reg:32 or /reg:64. */
static BOOL parse_args(int argc, WCHAR *argvW[], int nargs, BOOL allow_y, HKEY *root, WCHAR **path,
                       WCHAR **file, BOOL *overwrite, REGSAM *sam)
{
    int i;

    *file = NULL;
    *overwrite = FALSE;
    *sam = 0;

    if (argc < 2 + nargs) { syntax_error(); return FALSE; }

    for (i = 2 + nargs; i < argc; i++)
    {
        const WCHAR *str = argvW[i];

        if (str[0] != '/' && str[0] != '-') { syntax_error(); return FALSE; }
        str++;
        if (allow_y && !lstrcmpiW(str, L"y") && !*overwrite) *overwrite = TRUE;
        else if (!lstrcmpiW(str, L"reg:32") && !(*sam & KEY_WOW64_32KEY)) *sam |= KEY_WOW64_32KEY;
        else if (!lstrcmpiW(str, L"reg:64") && !(*sam & KEY_WOW64_64KEY)) *sam |= KEY_WOW64_64KEY;
        else { syntax_error(); return FALSE; }
    }
    if (*sam == (KEY_WOW64_32KEY|KEY_WOW64_64KEY)) { syntax_error(); return FALSE; }

    if (!parse_registry_key(argvW[2], root, path)) return FALSE;
    if (nargs > 1) *file = argvW[3];
    return TRUE;
}

int reg_save(int argc, WCHAR *argvW[])
{
    HKEY root, hkey;
    WCHAR *path, *file;
    BOOL overwrite;
    REGSAM sam;
    LONG rc;

    if (!parse_args(argc, argvW, 2, TRUE, &root, &path, &file, &overwrite, &sam))
        return 1;

    if ((rc = RegOpenKeyExW(root, path, 0, KEY_READ|sam, &hkey)))
    {
        output_reg_error(rc);
        return 1;
    }

    if (GetFileAttributesW(file) != INVALID_FILE_ATTRIBUTES)
    {
        if (!overwrite && !ask_confirm(STRING_OVERWRITE_FILE, file))
        {
            RegCloseKey(hkey);
            output_message(STRING_CANCELLED);
            return 0;
        }
        DeleteFileW(file);
    }

    enable_privilege(L"SeBackupPrivilege");
    rc = RegSaveKeyExW(hkey, file, NULL, REG_LATEST_FORMAT);
    RegCloseKey(hkey);
    if (rc)
    {
        output_reg_error(rc);
        return 1;
    }
    output_message(STRING_SUCCESS);
    return 0;
}

int reg_restore(int argc, WCHAR *argvW[])
{
    HKEY root, hkey;
    WCHAR *path, *file;
    BOOL overwrite;
    REGSAM sam;
    LONG rc;

    if (!parse_args(argc, argvW, 2, FALSE, &root, &path, &file, &overwrite, &sam))
        return 1;

    if ((rc = RegOpenKeyExW(root, path, 0, KEY_ALL_ACCESS|sam, &hkey)))
    {
        output_reg_error(rc);
        return 1;
    }

    enable_privilege(L"SeRestorePrivilege");
    enable_privilege(L"SeBackupPrivilege");
    rc = RegRestoreKeyW(hkey, file, 0);
    RegCloseKey(hkey);
    if (rc)
    {
        output_reg_error(rc);
        return 1;
    }
    output_message(STRING_SUCCESS);
    return 0;
}

int reg_load(int argc, WCHAR *argvW[])
{
    HKEY root;
    WCHAR *path, *file;
    BOOL overwrite;
    REGSAM sam;
    LONG rc;

    if (!parse_args(argc, argvW, 2, FALSE, &root, &path, &file, &overwrite, &sam))
        return 1;
    if (root != HKEY_LOCAL_MACHINE && root != HKEY_USERS)
        return usage_error(STRING_HIVE_ROOT);

    enable_privilege(L"SeRestorePrivilege");
    enable_privilege(L"SeBackupPrivilege");
    rc = RegLoadKeyW(root, path, file);
    if (rc)
    {
        output_reg_error(rc);
        return 1;
    }
    output_message(STRING_SUCCESS);
    return 0;
}

int reg_unload(int argc, WCHAR *argvW[])
{
    HKEY root;
    WCHAR *path, *file;
    BOOL overwrite;
    REGSAM sam;
    LONG rc;

    if (!parse_args(argc, argvW, 1, FALSE, &root, &path, &file, &overwrite, &sam))
        return 1;
    if (root != HKEY_LOCAL_MACHINE && root != HKEY_USERS)
        return usage_error(STRING_HIVE_ROOT);

    enable_privilege(L"SeRestorePrivilege");
    enable_privilege(L"SeBackupPrivilege");
    rc = RegUnLoadKeyW(root, path);
    if (rc)
    {
        output_reg_error(rc);
        return 1;
    }
    output_message(STRING_SUCCESS);
    return 0;
}
