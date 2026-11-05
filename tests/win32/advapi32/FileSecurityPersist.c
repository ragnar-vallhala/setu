/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: a file's security descriptor is stored with the file. After
 * SetFileSecurityW, the SD reads back the same after the handle is closed and
 * the file reopened, from a second process (the test runs itself with a
 * "child" argument, which reads the SD and tries an open the DACL denies),
 * after a rename, through a hard link, and after the file's data is
 * rewritten; a deleted and re-created name doesn't keep the old SD.
 *
 * The child reports through its exit code: bit 0 set if the SD it read
 * matches the expected SDDL, bit 1 set if the denied open failed with
 * ERROR_ACCESS_DENIED, bit 2 set if an allowed open succeeded.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-setfilesecurityw
 *       https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-getfilesecurityw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-createhardlinkw
 *       https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessw
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
static const wchar_t *SD1 = L"D:P(D;;0x1;;;$U)(A;;FA;;;$U)(A;;FR;;;BU)";

static int sd_matches(const wchar_t *path, const wchar_t *tmpl)
{
    BYTE buf[4096];
    DWORD need = 0;
    LPWSTR s = NULL;
    wchar_t want[1024];
    int ok;
    if (!GetFileSecurityW(path, DACL_SECURITY_INFORMATION, buf, sizeof buf, &need)) return 0;
    if (!ConvertSecurityDescriptorToStringSecurityDescriptorW(buf, SDDL_REVISION_1, DACL_SECURITY_INFORMATION, &s,
                                                              NULL))
        return 0;
    PSECURITY_DESCRIPTOR w = sd_from(tmpl);
    LPWSTR ws = NULL;
    ConvertSecurityDescriptorToStringSecurityDescriptorW(w, SDDL_REVISION_1, DACL_SECURITY_INFORMATION, &ws, NULL);
    wcscpy(want, ws);
    ok = !wcscmp(s, want);
    LocalFree(ws);
    LocalFree(w);
    LocalFree(s);
    return ok;
}

static int child(void)
{
    int code = 0;
    HANDLE h;
    acl_init();
    if (sd_matches(L"f", SD1)) code |= 1;
    h = CreateFileW(L"f", FILE_READ_DATA, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE && GetLastError() == ERROR_ACCESS_DENIED) code |= 2;
    else if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    h = CreateFileW(L"f", FILE_WRITE_DATA, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        code |= 4;
        CloseHandle(h);
    }
    return code;
}

static void show(const char *name, const wchar_t *path)
{
    oracle_case(name);
    o_file_sd("sd", path, DACL_SECURITY_INFORMATION);
    oracle_case_end();
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "child")) return child();
    oracle_begin("advapi32/FileSecurityPersist");
    acl_init();
    HANDLE h;
    BOOL r;

    /* What a new file inherits from the work directory depends on the machine;
     * give the directory a known DACL first. */
    set_file_sd(L".", DACL_SECURITY_INFORMATION, L"D:P(A;OICI;FA;;;$U)(A;OICI;FA;;;SY)");

    h = CreateFileW(L"f", GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL);
    WriteFile(h, "data", 4, NULL, NULL);
    PSECURITY_DESCRIPTOR sd = sd_from(SD1);
    ORACLE_CALL(r, SetKernelObjectSecurity(h, DACL_SECURITY_INFORMATION, sd));
    oracle_case("set_through_open_handle");
    o_bool("ret", r);
    o_lasterr();
    oracle_case_end();
    LocalFree(sd);
    show("read_while_open", L"f");
    CloseHandle(h);
    show("read_after_close", L"f");

    h = CreateFileW(L"f", FILE_WRITE_DATA | READ_CONTROL, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    oracle_case("reopen_allowed");
    o_handle("ret", h);
    o_bool("matches", sd_matches(L"f", SD1));
    oracle_case_end();
    CloseHandle(h);
    h = CreateFileW(L"f", FILE_READ_DATA, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    oracle_case("reopen_denied");
    o_handle("ret", h);
    o_u32("error", h == INVALID_HANDLE_VALUE ? GetLastError() : 0);
    oracle_case_end();
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);

    /* Second process. */
    {
        wchar_t exe[MAX_PATH], cmd[MAX_PATH + 16];
        STARTUPINFOW si;
        memset(&si, 0, sizeof si);
        si.cb = sizeof si;
        PROCESS_INFORMATION pi;
        DWORD code = 0xDEAD;
        GetModuleFileNameW(NULL, exe, MAX_PATH);
        swprintf(cmd, MAX_PATH + 16, L"\"%ls\" child", exe);
        r = CreateProcessW(exe, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
        if (r) {
            WaitForSingleObject(pi.hProcess, 30000);
            GetExitCodeProcess(pi.hProcess, &code);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
        oracle_case("second_process");
        o_bool("started", r);
        o_bool("sd_matches", (code & 1) != 0);
        o_bool("denied_open_failed", (code & 2) != 0);
        o_bool("allowed_open_ok", (code & 4) != 0);
        oracle_case_end();
    }

    /* Rewriting data doesn't touch the SD. */
    h = CreateFileW(L"f", FILE_WRITE_DATA, 0, NULL, OPEN_EXISTING, 0, NULL);
    WriteFile(h, "more data", 9, NULL, NULL);
    SetEndOfFile(h);
    CloseHandle(h);
    oracle_case("after_rewrite");
    o_bool("matches", sd_matches(L"f", SD1));
    oracle_case_end();

    /* Rename and hard link. */
    MoveFileW(L"f", L"renamed");
    oracle_case("after_rename");
    o_bool("matches", sd_matches(L"renamed", SD1));
    oracle_case_end();
    ORACLE_CALL(r, CreateHardLinkW(L"link", L"renamed", NULL));
    oracle_case("hard_link");
    o_bool("ret", r);
    o_lasterr();
    o_bool("matches", sd_matches(L"link", SD1));
    oracle_case_end();
    set_file_sd(L"link", DACL_SECURITY_INFORMATION, L"D:P(A;;FA;;;$U)");
    oracle_case("change_through_link");
    o_bool("other_name_matches_new", sd_matches(L"renamed", L"D:P(A;;FA;;;$U)"));
    oracle_case_end();

    /* Delete and re-create. */
    CreateDirectoryW(L"d", NULL);
    set_file_sd(L"d", DACL_SECURITY_INFORMATION, L"D:P(A;OICI;FA;;;$U)");
    h = CreateFileW(L"d\\x", GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL);
    CloseHandle(h);
    set_file_sd(L"d\\x", DACL_SECURITY_INFORMATION, SD1);
    DeleteFileW(L"d\\x");
    h = CreateFileW(L"d\\x", GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL);
    CloseHandle(h);
    show("recreated", L"d\\x");
    RemoveDirectoryW(L"d\\x");
    return oracle_end();
}
