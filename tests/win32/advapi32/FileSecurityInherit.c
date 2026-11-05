/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: inheritance of a directory's inheritable ACEs into files and
 * directories created inside it. The parent gets a protected DACL with
 * OI/CI/IO/NP ACEs and CREATOR OWNER / CREATOR GROUP ACEs; new children
 * created by CreateFileW and CreateDirectoryW (with no security attributes,
 * with an explicit DACL, with a protected DACL, with a NULL DACL) are read
 * back with GetFileSecurityW. Also grandchildren (NP stops propagation),
 * the owner and group of new objects, a directory without inheritable ACEs,
 * a file moved between directories (keeps its own DACL) and a copied file
 * (inherits from its new parent), and CREATE_ALWAYS over an existing file.
 *
 * SDs are printed as SDDL with the user's SID as <user> and other SIDs of the
 * user's domain as <dom>-RID.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/secauthz/ace-inheritance-rules
 *       https://learn.microsoft.com/en-us/windows/win32/secauthz/dacl-for-a-new-object
 *       https://learn.microsoft.com/en-us/windows/win32/secauthz/security-descriptors-for-new-objects
 *       https://learn.microsoft.com/en-us/windows/win32/secauthz/automatic-propagation-of-inheritable-aces
 *       https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew
 *       https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createdirectoryw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-movefilew
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
static void show(const char *name, const wchar_t *path)
{
    oracle_case(name);
    o_file_sd("sd", path, SI_ALL);
    oracle_case_end();
}

static void mkfile(const wchar_t *path, const wchar_t *sddl)
{
    SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, FALSE };
    if (sddl) sa.lpSecurityDescriptor = sd_from(sddl);
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, sddl ? &sa : NULL, CREATE_NEW, 0, NULL);
    CloseHandle(h);
    LocalFree(sa.lpSecurityDescriptor);
}

static void mkdir_(const wchar_t *path, const wchar_t *sddl)
{
    SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, FALSE };
    if (sddl) sa.lpSecurityDescriptor = sd_from(sddl);
    CreateDirectoryW(path, sddl ? &sa : NULL);
    LocalFree(sa.lpSecurityDescriptor);
}

int main(void)
{
    oracle_begin("advapi32/FileSecurityInherit");
    acl_init();

    CreateDirectoryW(L"p", NULL);
    BOOL r;
    PSECURITY_DESCRIPTOR psd = sd_from(L"D:P(A;OICI;FA;;;$U)(A;OI;FR;;;WD)(A;CI;FX;;;BU)(A;OICIIO;GA;;;CO)"
                                       L"(A;OICINP;0x1200a9;;;AU)(A;OICIIO;GR;;;CG)(A;;FA;;;SY)");
    ORACLE_CALL(r, SetFileSecurityW(L"p", DACL_SECURITY_INFORMATION, psd));
    oracle_case("parent_set");
    o_bool("ret", r);
    o_lasterr();
    o_file_sd("sd", L"p", DACL_SECURITY_INFORMATION);
    oracle_case_end();
    LocalFree(psd);

    mkfile(L"p\\f", NULL);
    show("child_file", L"p\\f");
    mkdir_(L"p\\d", NULL);
    show("child_dir", L"p\\d");
    mkfile(L"p\\d\\g", NULL);
    show("grandchild_file", L"p\\d\\g");
    mkdir_(L"p\\d\\e", NULL);
    show("grandchild_dir", L"p\\d\\e");

    /* Explicit security descriptors at creation. */
    mkfile(L"p\\explicit", L"D:(A;;FR;;;BG)");
    show("child_file_explicit_dacl", L"p\\explicit");
    mkfile(L"p\\protected", L"D:P(A;;FA;;;$U)");
    show("child_file_protected_dacl", L"p\\protected");
    mkfile(L"p\\nulldacl", L"D:NO_ACCESS_CONTROL");
    show("child_file_null_dacl", L"p\\nulldacl");
    mkfile(L"p\\owner_admins", L"O:BA");
    show("child_file_owner_admins", L"p\\owner_admins");
    mkfile(L"p\\generic", L"D:(A;;GA;;;BG)");
    show("child_file_generic_dacl", L"p\\generic");
    mkdir_(L"p\\dexplicit", L"D:(A;OICI;FR;;;BG)");
    show("child_dir_explicit_dacl", L"p\\dexplicit");
    mkdir_(L"p\\dprotected", L"D:P(A;OICI;FA;;;$U)");
    show("child_dir_protected_dacl", L"p\\dprotected");
    mkfile(L"p\\dprotected\\x", NULL);
    show("child_of_protected_dir", L"p\\dprotected\\x");
    {
        SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, FALSE };
        HANDLE h;
        sa.lpSecurityDescriptor = sd_from(L"O:SY");
        ORACLE_CALL(h, CreateFileW(L"p\\owner_system", GENERIC_WRITE, 0, &sa, CREATE_NEW, 0, NULL));
        oracle_case("child_file_owner_system");
        o_handle("ret", h);
        o_lasterr();
        oracle_case_end();
        CloseHandle(h);
        LocalFree(sa.lpSecurityDescriptor);
    }

    /* A parent without inheritable ACEs. */
    mkdir_(L"q", NULL);
    set_file_sd(L"q", DACL_SECURITY_INFORMATION, L"D:P(A;;FA;;;$U)(A;;FA;;;SY)");
    mkfile(L"q\\f", NULL);
    show("no_inheritable_child_file", L"q\\f");
    mkdir_(L"q\\d", NULL);
    show("no_inheritable_child_dir", L"q\\d");

    /* Move keeps the DACL, copy inherits from the destination. */
    mkfile(L"q\\moved", NULL);
    MoveFileW(L"q\\moved", L"p\\moved");
    show("moved_into_p", L"p\\moved");
    CopyFileW(L"q\\f", L"p\\copied", TRUE);
    show("copied_into_p", L"p\\copied");
    MoveFileW(L"p\\f", L"q\\f_from_p");
    show("moved_into_q", L"q\\f_from_p");

    /* Overwriting an existing file keeps its security. */
    set_file_sd(L"p\\explicit", DACL_SECURITY_INFORMATION, L"D:P(A;;FA;;;$U)(A;;FR;;;BG)");
    {
        HANDLE h = CreateFileW(L"p\\explicit", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        CloseHandle(h);
    }
    show("create_always_existing", L"p\\explicit");
    {
        SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, FALSE };
        sa.lpSecurityDescriptor = sd_from(L"D:P(A;;FA;;;$U)");
        HANDLE h = CreateFileW(L"p\\explicit", GENERIC_WRITE, 0, &sa, CREATE_ALWAYS, 0, NULL);
        CloseHandle(h);
        LocalFree(sa.lpSecurityDescriptor);
    }
    show("create_always_existing_with_sd", L"p\\explicit");
    {
        HANDLE h = CreateFileW(L"p\\explicit", GENERIC_WRITE, 0, NULL, TRUNCATE_EXISTING, 0, NULL);
        CloseHandle(h);
    }
    show("truncate_existing", L"p\\explicit");

    /* Deleting and recreating a name gives fresh inheritance. */
    DeleteFileW(L"p\\protected");
    mkfile(L"p\\protected", NULL);
    show("recreated_after_delete", L"p\\protected");

    /* Changing the parent later doesn't touch existing children (SetFileSecurity). */
    set_file_sd(L"p", DACL_SECURITY_INFORMATION, L"D:P(A;OICI;FA;;;$U)(A;OICI;FA;;;SY)");
    show("child_after_parent_change", L"p\\protected");
    mkfile(L"p\\after", NULL);
    show("new_child_after_parent_change", L"p\\after");
    return oracle_end();
}
