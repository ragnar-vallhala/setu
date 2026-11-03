/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: WOW64 file-system redirection APIs called from a native
 * 64-bit process (Wow64DisableWow64FsRedirection / Revert / Enable,
 * IsWow64Process, IsWow64Process2, GetSystemWow64DirectoryW/2W). Directory
 * paths are tokenised to their last component in lower case.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/wow64apiset/nf-wow64apiset-wow64disablewow64fsredirection
 *       https://learn.microsoft.com/en-us/windows/win32/api/wow64apiset/nf-wow64apiset-wow64revertwow64fsredirection
 *       https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-wow64enablewow64fsredirection
 *       https://learn.microsoft.com/en-us/windows/win32/api/wow64apiset/nf-wow64apiset-iswow64process2
 *       https://learn.microsoft.com/en-us/windows/win32/api/wow64apiset/nf-wow64apiset-getsystemwow64directoryw
 *       https://learn.microsoft.com/en-us/windows/win32/api/wow64apiset/nf-wow64apiset-getsystemwow64directory2w
 */
#include "oracle.h"

typedef BOOL(WINAPI *pDisable)(PVOID *);
typedef BOOL(WINAPI *pRevert)(PVOID);
typedef BOOLEAN(WINAPI *pEnable)(BOOLEAN);
typedef BOOL(WINAPI *pIsWow2)(HANDLE, USHORT *, USHORT *);
typedef UINT(WINAPI *pSysWow2)(LPWSTR, UINT, WORD);

static void tail_lower(const wchar_t *path, char *out, size_t n)
{
    const wchar_t *s = wcsrchr(path, L'\\');
    wchar_t t[MAX_PATH];
    wcsncpy(t, s ? s + 1 : path, MAX_PATH - 1);
    t[MAX_PATH - 1] = 0;
    for (wchar_t *p = t; *p; p++) *p = towlower(*p);
    snprintf(out, n, "%s", oracle_utf8(t));
}

int main(void)
{
    oracle_begin("kernel32/wow64_fs_redirection");
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    pDisable disable = (pDisable)(void *)GetProcAddress(k32, "Wow64DisableWow64FsRedirection");
    pRevert revert = (pRevert)(void *)GetProcAddress(k32, "Wow64RevertWow64FsRedirection");
    pEnable enable = (pEnable)(void *)GetProcAddress(k32, "Wow64EnableWow64FsRedirection");
    pIsWow2 iswow2 = (pIsWow2)(void *)GetProcAddress(k32, "IsWow64Process2");
    pSysWow2 syswow2 = (pSysWow2)(void *)GetProcAddress(k32, "GetSystemWow64Directory2W");

    oracle_case("exports");
    o_bool("disable", disable != NULL);
    o_bool("revert", revert != NULL);
    o_bool("enable", enable != NULL);
    o_bool("is_wow64_process2", iswow2 != NULL);
    o_bool("get_system_wow64_directory2w", syswow2 != NULL);
    {
        HMODULE kb = GetModuleHandleW(L"kernelbase.dll");
        pIsWow2 kb_iswow2 = kb ? (pIsWow2)(void *)GetProcAddress(kb, "IsWow64Process2") : NULL;
        pSysWow2 kb_syswow2 = kb ? (pSysWow2)(void *)GetProcAddress(kb, "GetSystemWow64Directory2W") : NULL;
        o_bool("kernelbase_is_wow64_process2", kb_iswow2 != NULL);
        o_bool("kernelbase_get_system_wow64_directory2w", kb_syswow2 != NULL);
        if (!iswow2) iswow2 = kb_iswow2;
        if (!syswow2) syswow2 = kb_syswow2;
    }
    oracle_case_end();

    BOOL r;
    PVOID old = (PVOID)(ULONG_PTR)0x1234;
    if (disable) {
        ORACLE_CALL(r, disable(&old));
        oracle_case("disable");
        o_bool("ret", r);
        o_lasterr();
        o_bool("old_untouched", old == (PVOID)(ULONG_PTR)0x1234);
        o_bool("old_null", old == NULL);
        oracle_case_end();
        ORACLE_CALL(r, disable(&old));
        oracle_case("disable_twice");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
    }
    if (revert) {
        ORACLE_CALL(r, revert(old));
        oracle_case("revert_old");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, revert(NULL));
        oracle_case("revert_null");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, revert((PVOID)(ULONG_PTR)1));
        oracle_case("revert_one");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
    }
    if (enable) {
        BOOLEAN b;
        ORACLE_CALL(b, enable(FALSE));
        oracle_case("enable_false");
        o_bool("ret", b);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(b, enable(TRUE));
        oracle_case("enable_true");
        o_bool("ret", b);
        o_lasterr();
        oracle_case_end();
    }

    /* Redirection never applies to a 64-bit process: System32 is System32. */
    {
        wchar_t sys[MAX_PATH], p[MAX_PATH + 32];
        GetSystemDirectoryW(sys, MAX_PATH);
        swprintf(p, MAX_PATH + 32, L"%ls\\kernel32.dll", sys);
        DWORD a1 = GetFileAttributesW(p);
        if (disable) disable(&old);
        DWORD a2 = GetFileAttributesW(p);
        if (revert) revert(old);
        oracle_case("system32_visible_either_way");
        o_bool("before", a1 != INVALID_FILE_ATTRIBUTES);
        o_bool("disabled", a2 != INVALID_FILE_ATTRIBUTES);
        oracle_case_end();
    }

    {
        BOOL w = 7;
        ORACLE_CALL(r, IsWow64Process(GetCurrentProcess(), &w));
        oracle_case("is_wow64_process_self");
        o_bool("ret", r);
        o_lasterr();
        o_u32("wow64", (DWORD)w);
        oracle_case_end();
        ORACLE_CALL(r, IsWow64Process(NULL, &w));
        oracle_case("is_wow64_process_null");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        HANDLE ev = CreateEventW(NULL, FALSE, FALSE, NULL);
        ORACLE_CALL(r, IsWow64Process(ev, &w));
        oracle_case("is_wow64_process_event");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        CloseHandle(ev);
    }
    if (iswow2) {
        USHORT pm = 0xAAAA, nm = 0xAAAA;
        ORACLE_CALL(r, iswow2(GetCurrentProcess(), &pm, &nm));
        oracle_case("is_wow64_process2_self");
        o_bool("ret", r);
        o_lasterr();
        o_hex32("process_machine", pm);
        o_hex32("native_machine", nm);
        oracle_case_end();
        pm = 0xAAAA;
        ORACLE_CALL(r, iswow2(GetCurrentProcess(), &pm, NULL));
        oracle_case("is_wow64_process2_null_native");
        o_bool("ret", r);
        o_lasterr();
        o_hex32("process_machine", pm);
        oracle_case_end();
    }

    {
        wchar_t buf[MAX_PATH];
        char t[MAX_PATH];
        UINT n, need;
        ORACLE_CALL(n, GetSystemWow64DirectoryW(buf, MAX_PATH));
        oracle_case("syswow64_dir");
        o_bool("ret_nonzero", n != 0);
        o_lasterr();
        if (n && n < MAX_PATH) {
            tail_lower(buf, t, sizeof t);
            o_str("tail", t);
            o_bool("ret_is_len", n == wcslen(buf));
            wchar_t win[MAX_PATH];
            GetWindowsDirectoryW(win, MAX_PATH);
            o_bool("under_windows_dir", !_wcsnicmp(buf, win, wcslen(win)));
        }
        oracle_case_end();
        ORACLE_CALL(need, GetSystemWow64DirectoryW(NULL, 0));
        oracle_case("syswow64_size_query");
        o_bool("need_is_len_plus_1", need == n + 1);
        o_lasterr();
        oracle_case_end();
        wmemset(buf, L'#', MAX_PATH);
        ORACLE_CALL(need, GetSystemWow64DirectoryW(buf, n));
        oracle_case("syswow64_buffer_len");
        o_bool("need_is_len_plus_1", need == n + 1);
        o_bool("untouched", buf[0] == L'#');
        o_lasterr();
        oracle_case_end();
        char abuf[MAX_PATH];
        ORACLE_CALL(need, GetSystemWow64DirectoryA(abuf, MAX_PATH));
        oracle_case("syswow64_ansi");
        o_bool("same_len", need == n);
        o_lasterr();
        oracle_case_end();
    }
    if (syswow2) {
        static const struct { const char *name; WORD m; } ms[] = {
            {"syswow64_2_i386", IMAGE_FILE_MACHINE_I386},
            {"syswow64_2_amd64", IMAGE_FILE_MACHINE_AMD64},
            {"syswow64_2_armnt", IMAGE_FILE_MACHINE_ARMNT},
            {"syswow64_2_arm64", IMAGE_FILE_MACHINE_ARM64},
            {"syswow64_2_unknown", IMAGE_FILE_MACHINE_UNKNOWN},
            {"syswow64_2_bogus", 0x1234},
        };
        for (unsigned i = 0; i < sizeof ms / sizeof ms[0]; i++) {
            wchar_t buf[MAX_PATH];
            char t[MAX_PATH];
            UINT n;
            ORACLE_CALL(n, syswow2(buf, MAX_PATH, ms[i].m));
            oracle_case(ms[i].name);
            o_bool("ret_nonzero", n != 0);
            o_lasterr();
            if (n && n < MAX_PATH) {
                tail_lower(buf, t, sizeof t);
                o_str("tail", t);
            }
            oracle_case_end();
        }
    }

    return oracle_end();
}
