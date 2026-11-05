/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: CreateFileW, DeleteFileW, MoveFileW, CreateDirectoryW,
 * FindFirstFileW and GetFileAttributesW honouring files' and directories'
 * DACLs. Files get protected DACLs that deny or omit FILE_READ_DATA,
 * FILE_WRITE_DATA, DELETE, etc. for the user; the test then opens them with
 * various desired access masks (and MAXIMUM_ALLOWED, whose granted mask is
 * read with NtQueryObject), deletes them with and without FILE_DELETE_CHILD
 * on the parent, creates entries in directories denying FILE_ADD_FILE /
 * FILE_ADD_SUBDIRECTORY, lists directories denying FILE_LIST_DIRECTORY, and
 * checks the owner's implicit READ_CONTROL / WRITE_DAC on an empty DACL.
 * FILE_FLAG_BACKUP_SEMANTICS with SeBackupPrivilege enabled is checked last.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/fileio/file-security-and-access-rights
 *       https://learn.microsoft.com/en-us/windows/win32/fileio/file-access-rights-constants
 *       https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew
 *       https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-deletefilew
 *       https://learn.microsoft.com/en-us/windows/win32/api/ntifs/nf-ntifs-ntqueryobject
 *       https://learn.microsoft.com/en-us/windows/win32/secauthz/privilege-constants
 */
#include "oracle.h"
#include <sddl.h>
#include <wctype.h>

/* ---- shared ACL test helpers (kept inline: one file per test) ---------- */
static HANDLE g_prim, g_imp;
static wchar_t g_user[192];

static void acl_init(void)
{
    BYTE buf[512];
    DWORD n = 0;
    LPWSTR s = NULL;
    OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_DUPLICATE | TOKEN_ADJUST_PRIVILEGES, &g_prim);
    DuplicateToken(g_prim, SecurityImpersonation, &g_imp);
    GetTokenInformation(g_prim, TokenUser, buf, sizeof buf, &n);
    ConvertSidToStringSidW(((TOKEN_USER *)buf)->User.Sid, &s);
    wcscpy(g_user, s);
    LocalFree(s);
}

/* Expand $U (the user's SID string) in an SDDL template. */
static void expand(const wchar_t *in, wchar_t *out)
{
    while (*in) {
        if (in[0] == L'$' && in[1] == L'U') { wcscpy(out, g_user); out += wcslen(g_user); in += 2; }
        else *out++ = *in++;
    }
    *out = 0;
}

static PSECURITY_DESCRIPTOR sd_from(const wchar_t *tmpl)
{
    wchar_t buf[2048];
    PSECURITY_DESCRIPTOR sd = NULL;
    expand(tmpl, buf);
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(buf, SDDL_REVISION_1, &sd, NULL)) return NULL;
    return sd;
}
/* ----------------------------------------------------------------------- */
/* SDDL output with machine-specific SIDs replaced: the user's SID by <user>,
 * other SIDs of the user's domain by <dom>-RID, logon SIDs by <logon>. */
static void tokenize(const wchar_t *in, wchar_t *out)
{
    wchar_t dom[192];
    size_t ul = wcslen(g_user), dl;
    wcscpy(dom, g_user);
    *wcsrchr(dom, L'-') = 0;
    dl = wcslen(dom);
    while (*in) {
        if (!wcsncmp(in, g_user, ul) && !iswdigit(in[ul])) {
            wcscpy(out, L"<user>"); out += 6; in += ul;
        } else if (!wcsncmp(in, dom, dl) && in[dl] == L'-') {
            wcscpy(out, L"<dom>"); out += 5; in += dl;
        } else if (!wcsncmp(in, L"S-1-5-5-", 8)) {
            wcscpy(out, L"<logon>"); out += 7; in += 8;
            while (iswdigit(*in) || *in == L'-') in++;
        } else *out++ = *in++;
    }
    *out = 0;
}

static void o_sd(const char *key, PSECURITY_DESCRIPTOR sd, SECURITY_INFORMATION si)
{
    LPWSTR s = NULL;
    static wchar_t buf[8192];
    if (!sd) { o_str(key, NULL); return; }
    if (!ConvertSecurityDescriptorToStringSecurityDescriptorW(sd, SDDL_REVISION_1, si, &s, NULL)) {
        char k[64];
        snprintf(k, sizeof k, "%s_error", key);
        o_u32(k, GetLastError());
        return;
    }
    tokenize(s, buf);
    o_wstr(key, buf);
    LocalFree(s);
}
#define SI_ALL (OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION)
/* File security helpers. */
static void o_file_sd(const char *key, const wchar_t *path, SECURITY_INFORMATION si)
{
    static BYTE buf[16384];
    DWORD need = 0;
    if (!GetFileSecurityW(path, si, buf, sizeof buf, &need)) {
        char k[64];
        snprintf(k, sizeof k, "%s_error", key);
        o_u32(k, GetLastError());
        return;
    }
    SECURITY_DESCRIPTOR_CONTROL c = 0;
    DWORD rev = 0;
    char k[64];
    GetSecurityDescriptorControl(buf, &c, &rev);
    snprintf(k, sizeof k, "%s_control", key);
    o_hex32(k, c & ~SE_SELF_RELATIVE);
    o_sd(key, buf, si);
}

static __attribute__((unused)) BOOL set_file_sd(const wchar_t *path, SECURITY_INFORMATION si, const wchar_t *sddl)
{
    PSECURITY_DESCRIPTOR sd = sd_from(sddl);
    BOOL r = SetFileSecurityW(path, si, sd);
    LocalFree(sd);
    return r;
}

static __attribute__((unused)) BOOL enable_priv(const wchar_t *name, BOOL on)
{
    TOKEN_PRIVILEGES tp;
    tp.PrivilegeCount = 1;
    LookupPrivilegeValueW(NULL, name, &tp.Privileges[0].Luid);
    tp.Privileges[0].Attributes = on ? SE_PRIVILEGE_ENABLED : 0;
    return AdjustTokenPrivileges(g_prim, FALSE, &tp, 0, NULL, NULL) && GetLastError() == ERROR_SUCCESS;
}
typedef LONG(WINAPI *pNtQueryObject)(HANDLE, int, void *, ULONG, ULONG *);
static pNtQueryObject NtQueryObject_;

static void granted(HANDLE h)
{
    /* OBJECT_BASIC_INFORMATION is 56 bytes; Windows wants that exact size */
    ULONG info[14] = { 0 };
    ULONG len = 0;
    if (h == INVALID_HANDLE_VALUE || !NtQueryObject_) return;
    if (!NtQueryObject_(h, 0 /* ObjectBasicInformation */, info, sizeof info, &len)) o_hex32("granted", info[1]);
}

static void open_case(const char *name, const wchar_t *path, DWORD access, DWORD disp, DWORD flags)
{
    HANDLE h;
    ORACLE_CALL(h, CreateFileW(path, access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, disp,
                               flags, NULL));
    oracle_case(name);
    o_handle("ret", h);
    o_lasterr();
    granted(h);
    oracle_case_end();
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
}

static void mk(const wchar_t *path, const wchar_t *sddl)
{
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    WriteFile(h, "data", 4, NULL, NULL);
    CloseHandle(h);
    if (sddl) set_file_sd(path, DACL_SECURITY_INFORMATION, sddl);
}

static void bool_case(const char *name, BOOL r)
{
    oracle_case(name);
    o_bool("ret", r);
    o_lasterr();
    oracle_case_end();
}

int main(void)
{
    oracle_begin("advapi32/FileSecurityAccess");
    acl_init();
    /* Start from the same privileges everywhere: some logons (an SSH session
     * of an administrator, for one) have these enabled by default. */
    enable_priv(L"SeBackupPrivilege", FALSE);
    enable_priv(L"SeRestorePrivilege", FALSE);
    enable_priv(L"SeTakeOwnershipPrivilege", FALSE);
    enable_priv(L"SeSecurityPrivilege", FALSE);
    NtQueryObject_ = (pNtQueryObject)(void (*)(void))GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryObject");
    BOOL r;

    /* A directory the user fully controls. */
    CreateDirectoryW(L"a", NULL);
    set_file_sd(L"a", DACL_SECURITY_INFORMATION, L"D:P(A;OICI;FA;;;$U)(A;OICI;FA;;;SY)");

    /* Allowed everything. */
    mk(L"a\\full", L"D:P(A;;FA;;;$U)");
    open_case("full_read", L"a\\full", GENERIC_READ, OPEN_EXISTING, 0);
    open_case("full_write", L"a\\full", GENERIC_WRITE, OPEN_EXISTING, 0);
    open_case("full_max", L"a\\full", MAXIMUM_ALLOWED, OPEN_EXISTING, 0);
    open_case("full_all", L"a\\full", GENERIC_ALL, OPEN_EXISTING, 0);

    /* Read-only DACL. */
    mk(L"a\\ro", L"D:P(A;;FR;;;$U)");
    open_case("ro_read", L"a\\ro", GENERIC_READ, OPEN_EXISTING, 0);
    open_case("ro_write", L"a\\ro", GENERIC_WRITE, OPEN_EXISTING, 0);
    open_case("ro_read_write", L"a\\ro", GENERIC_READ | GENERIC_WRITE, OPEN_EXISTING, 0);
    open_case("ro_append", L"a\\ro", FILE_APPEND_DATA, OPEN_EXISTING, 0);
    open_case("ro_max", L"a\\ro", MAXIMUM_ALLOWED, OPEN_EXISTING, 0);
    open_case("ro_delete", L"a\\ro", DELETE, OPEN_EXISTING, 0);
    open_case("ro_write_attributes", L"a\\ro", FILE_WRITE_ATTRIBUTES, OPEN_EXISTING, 0);
    open_case("ro_read_attributes", L"a\\ro", FILE_READ_ATTRIBUTES, OPEN_EXISTING, 0);
    open_case("ro_zero_access", L"a\\ro", 0, OPEN_EXISTING, 0);
    open_case("ro_create_always", L"a\\ro", GENERIC_WRITE, CREATE_ALWAYS, 0);
    open_case("ro_open_always_read", L"a\\ro", GENERIC_READ, OPEN_ALWAYS, 0);
    open_case("ro_truncate", L"a\\ro", GENERIC_WRITE, TRUNCATE_EXISTING, 0);

    /* Deny ACEs. */
    mk(L"a\\deny_read", L"D:P(D;;0x1;;;$U)(A;;FA;;;$U)");
    open_case("deny_read_read", L"a\\deny_read", FILE_READ_DATA, OPEN_EXISTING, 0);
    open_case("deny_read_generic_read", L"a\\deny_read", GENERIC_READ, OPEN_EXISTING, 0);
    open_case("deny_read_write", L"a\\deny_read", GENERIC_WRITE, OPEN_EXISTING, 0);
    open_case("deny_read_max", L"a\\deny_read", MAXIMUM_ALLOWED, OPEN_EXISTING, 0);
    mk(L"a\\deny_write_group", L"D:P(D;;FW;;;BA)(A;;FA;;;$U)");
    open_case("deny_write_group_write", L"a\\deny_write_group", GENERIC_WRITE, OPEN_EXISTING, 0);
    open_case("deny_write_group_read", L"a\\deny_write_group", GENERIC_READ, OPEN_EXISTING, 0);
    mk(L"a\\allow_then_deny", L"D:P(A;;FA;;;$U)(D;;FA;;;$U)");
    open_case("allow_then_deny_read", L"a\\allow_then_deny", GENERIC_READ, OPEN_EXISTING, 0);

    /* Empty DACL: only the owner's implicit rights. */
    mk(L"a\\empty", L"D:P");
    open_case("empty_read", L"a\\empty", GENERIC_READ, OPEN_EXISTING, 0);
    open_case("empty_read_control", L"a\\empty", READ_CONTROL, OPEN_EXISTING, 0);
    open_case("empty_write_dac", L"a\\empty", WRITE_DAC, OPEN_EXISTING, 0);
    open_case("empty_max", L"a\\empty", MAXIMUM_ALLOWED, OPEN_EXISTING, 0);
    open_case("empty_read_attributes", L"a\\empty", FILE_READ_ATTRIBUTES, OPEN_EXISTING, 0);
    oracle_case("empty_attributes");
    o_hex32("attrs", GetFileAttributesW(L"a\\empty"));
    oracle_case_end();
    oracle_case("empty_getsecurity");
    o_file_sd("sd", L"a\\empty", DACL_SECURITY_INFORMATION);
    oracle_case_end();
    {
        HANDLE h = CreateFileW(L"a\\empty", WRITE_DAC, 0, NULL, OPEN_EXISTING, 0, NULL);
        PSECURITY_DESCRIPTOR sd = sd_from(L"D:P(A;;FA;;;$U)");
        ORACLE_CALL(r, SetKernelObjectSecurity(h, DACL_SECURITY_INFORMATION, sd));
        oracle_case("empty_fix_with_write_dac");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        LocalFree(sd);
        CloseHandle(h);
    }
    open_case("empty_after_fix_read", L"a\\empty", GENERIC_READ, OPEN_EXISTING, 0);

    /* NULL DACL. */
    mk(L"a\\null", L"D:NO_ACCESS_CONTROL");
    open_case("null_dacl_all", L"a\\null", GENERIC_ALL, OPEN_EXISTING, 0);
    open_case("null_dacl_max", L"a\\null", MAXIMUM_ALLOWED, OPEN_EXISTING, 0);

    /* Open handles keep their access after the DACL changes. */
    {
        HANDLE h;
        DWORD n = 0;
        char b[8];
        mk(L"a\\keep", L"D:P(A;;FA;;;$U)");
        h = CreateFileW(L"a\\keep", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        set_file_sd(L"a\\keep", DACL_SECURITY_INFORMATION, L"D:P(D;;FA;;;$U)");
        ORACLE_CALL(r, ReadFile(h, b, 4, &n, NULL));
        oracle_case("handle_survives_dacl_change");
        o_bool("ret", r);
        o_lasterr();
        o_u32("read", n);
        oracle_case_end();
        CloseHandle(h);
        open_case("reopen_after_dacl_change", L"a\\keep", GENERIC_READ, OPEN_EXISTING, 0);
        open_case("reopen_after_dacl_change_rc", L"a\\keep", READ_CONTROL, OPEN_EXISTING, 0);
        open_case("reopen_after_dacl_change_wdac", L"a\\keep", WRITE_DAC, OPEN_EXISTING, 0);
    }

    /* Deleting: DELETE on the file or FILE_DELETE_CHILD on the parent. */
    ORACLE_CALL(r, DeleteFileW(L"a\\ro"));
    bool_case("delete_ro_parent_has_delete_child", r);
    CreateDirectoryW(L"b", NULL);
    set_file_sd(L"b", DACL_SECURITY_INFORMATION, L"D:P(A;;0x1201bf;;;$U)(A;OICIIO;FA;;;$U)");
    mk(L"b\\nodelete", L"D:P(A;;FR;;;$U)");
    ORACLE_CALL(r, DeleteFileW(L"b\\nodelete"));
    bool_case("delete_no_delete_no_delete_child", r);
    mk(L"b\\withdelete", L"D:P(A;;0x1301bf;;;$U)");
    ORACLE_CALL(r, DeleteFileW(L"b\\withdelete"));
    bool_case("delete_has_delete", r);
    mk(L"b\\denydelete", L"D:P(D;;SD;;;$U)(A;;FA;;;$U)");
    ORACLE_CALL(r, DeleteFileW(L"b\\denydelete"));
    bool_case("delete_denied_delete", r);
    ORACLE_CALL(r, MoveFileW(L"b\\nodelete", L"b\\renamed"));
    bool_case("rename_no_delete", r);
    ORACLE_CALL(r, MoveFileW(L"a\\full", L"a\\renamed"));
    bool_case("rename_full", r);
    open_case("open_for_delete_no_delete", L"b\\nodelete", DELETE, OPEN_EXISTING, 0);

    /* Creating entries in directories. */
    CreateDirectoryW(L"c", NULL);
    set_file_sd(L"c", DACL_SECURITY_INFORMATION, L"D:P(D;;0x2;;;$U)(A;OICI;FA;;;$U)");
    open_case("create_in_no_add_file", L"c\\new", GENERIC_WRITE, CREATE_NEW, 0);
    ORACLE_CALL(r, CreateDirectoryW(L"c\\sub", NULL));
    bool_case("mkdir_in_no_add_file", r);
    CreateDirectoryW(L"e", NULL);
    set_file_sd(L"e", DACL_SECURITY_INFORMATION, L"D:P(D;;0x4;;;$U)(A;OICI;FA;;;$U)");
    open_case("create_in_no_add_subdir", L"e\\new", GENERIC_WRITE, CREATE_NEW, 0);
    ORACLE_CALL(r, CreateDirectoryW(L"e\\sub", NULL));
    bool_case("mkdir_in_no_add_subdir", r);

    /* Listing. */
    CreateDirectoryW(L"g", NULL);
    mk(L"g\\x", NULL);
    set_file_sd(L"g", DACL_SECURITY_INFORMATION, L"D:P(D;;0x1;;;$U)(A;OICI;FA;;;$U)");
    {
        WIN32_FIND_DATAW fd;
        HANDLE h;
        ORACLE_CALL(h, FindFirstFileW(L"g\\*", &fd));
        oracle_case("list_denied");
        o_handle("ret", h);
        o_lasterr();
        oracle_case_end();
        if (h != INVALID_HANDLE_VALUE) FindClose(h);
        ORACLE_CALL(h, FindFirstFileW(L"g\\x", &fd));
        oracle_case("find_exact_name_in_denied");
        o_handle("ret", h);
        o_lasterr();
        oracle_case_end();
        if (h != INVALID_HANDLE_VALUE) FindClose(h);
    }
    open_case("open_child_of_unlistable", L"g\\x", GENERIC_READ, OPEN_EXISTING, 0);
    open_case("open_dir_denied_list", L"g", FILE_LIST_DIRECTORY, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS);
    open_case("open_dir_denied_list_traverse", L"g", FILE_TRAVERSE, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS);

    /* Backup semantics with SeBackupPrivilege bypass the DACL for reading. */
    mk(L"a\\locked", L"D:P(D;;FA;;;$U)");
    open_case("locked_read", L"a\\locked", GENERIC_READ, OPEN_EXISTING, 0);
    open_case("locked_read_backup_no_priv", L"a\\locked", GENERIC_READ, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS);
    oracle_case("enable_backup");
    o_bool("ret", enable_priv(L"SeBackupPrivilege", TRUE));
    oracle_case_end();
    open_case("locked_read_backup", L"a\\locked", GENERIC_READ, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS);
    open_case("locked_read_backup_no_flag", L"a\\locked", GENERIC_READ, OPEN_EXISTING, 0);
    open_case("locked_write_backup", L"a\\locked", GENERIC_WRITE, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS);
    enable_priv(L"SeBackupPrivilege", FALSE);

    /* Leave everything deletable. */
    set_file_sd(L"a\\locked", DACL_SECURITY_INFORMATION, L"D:P(A;;FA;;;WD)");
    set_file_sd(L"a\\keep", DACL_SECURITY_INFORMATION, L"D:P(A;;FA;;;WD)");
    set_file_sd(L"g", DACL_SECURITY_INFORMATION, L"D:P(A;OICI;FA;;;WD)");
    set_file_sd(L"c", DACL_SECURITY_INFORMATION, L"D:P(A;OICI;FA;;;WD)");
    set_file_sd(L"e", DACL_SECURITY_INFORMATION, L"D:P(A;OICI;FA;;;WD)");
    set_file_sd(L"b", DACL_SECURITY_INFORMATION, L"D:P(A;OICI;FA;;;WD)");
    return oracle_end();
}
