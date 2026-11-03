/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: GetWindowsDirectoryW/A, GetSystemWindowsDirectoryW,
 * GetSystemDirectoryW/A, GetSystemWow64DirectoryW/A and
 * GetSystemWow64Directory2W: tokenised shape (the Windows directory itself is
 * printed only as "<windir>", the rest of each path is printed as is) and
 * the buffer rules (size query, exact size, one too small).
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-getwindowsdirectoryw
 *       https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-getsystemwindowsdirectoryw
 *       https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-getsystemdirectoryw
 *       https://learn.microsoft.com/en-us/windows/win32/api/wow64apiset/nf-wow64apiset-getsystemwow64directoryw
 *       https://learn.microsoft.com/en-us/windows/win32/api/wow64apiset/nf-wow64apiset-getsystemwow64directory2w
 */
#include "oracle.h"

typedef UINT (WINAPI *dir_fn)(LPWSTR, UINT);
typedef UINT (WINAPI *dir_fn_a)(LPSTR, UINT);
typedef UINT (WINAPI *wow2_fn)(LPWSTR, UINT, WORD);

static wchar_t windir[MAX_PATH];

static void tok(const char *k, const wchar_t *p)
{
    size_t n = wcslen(windir);
    wchar_t t[MAX_PATH + 16];
    if (n && !_wcsnicmp(p, windir, n)) swprintf(t, MAX_PATH + 16, L"<windir>%ls", p + n);
    else swprintf(t, MAX_PATH + 16, L"<other>");
    o_wstr(k, t);
}

/* Shape plus buffer rules for one W function. */
static void dir_cases(const char *base, dir_fn f)
{
    wchar_t buf[MAX_PATH], small[MAX_PATH];
    char n[64];
    UINT len, r;

    ORACLE_CALL(len, f(buf, MAX_PATH));
    snprintf(n, sizeof n, "%s_full", base);
    oracle_case(n);
    o_bool("ret_is_length", len == wcslen(buf));
    o_lasterr();
    tok("path", buf);
    o_bool("no_trailing_backslash", len && buf[len - 1] != L'\\');
    oracle_case_end();

    ORACLE_CALL(r, f(NULL, 0));
    snprintf(n, sizeof n, "%s_size_query", base);
    oracle_case(n);
    o_i64("ret_minus_len", (long long)r - len);
    o_lasterr();
    oracle_case_end();

    wmemset(small, L'#', MAX_PATH);
    ORACLE_CALL(r, f(small, len));
    snprintf(n, sizeof n, "%s_one_too_small", base);
    oracle_case(n);
    o_i64("ret_minus_len", (long long)r - len);
    o_lasterr();
    o_bool("untouched", small[0] == L'#');
    oracle_case_end();

    wmemset(small, L'#', MAX_PATH);
    ORACLE_CALL(r, f(small, len + 1));
    snprintf(n, sizeof n, "%s_exact", base);
    oracle_case(n);
    o_i64("ret_minus_len", (long long)r - len);
    o_lasterr();
    o_bool("same", !wcscmp(small, buf));
    oracle_case_end();
}

static void dir_cases_a(const char *base, dir_fn_a f, dir_fn wf)
{
    char buf[MAX_PATH], small[MAX_PATH], n[64];
    wchar_t wbuf[MAX_PATH], conv[MAX_PATH];
    UINT len, r;
    ORACLE_CALL(len, f(buf, MAX_PATH));
    wf(wbuf, MAX_PATH);
    MultiByteToWideChar(CP_ACP, 0, buf, -1, conv, MAX_PATH);
    snprintf(n, sizeof n, "%s_full", base);
    oracle_case(n);
    o_bool("ret_is_length", len == strlen(buf));
    o_lasterr();
    o_bool("matches_wide", !wcscmp(conv, wbuf));
    oracle_case_end();
    ORACLE_CALL(r, f(NULL, 0));
    snprintf(n, sizeof n, "%s_size_query", base);
    oracle_case(n);
    o_i64("ret_minus_len", (long long)r - len);
    o_lasterr();
    oracle_case_end();
    memset(small, '#', sizeof small);
    ORACLE_CALL(r, f(small, len));
    snprintf(n, sizeof n, "%s_one_too_small", base);
    oracle_case(n);
    o_i64("ret_minus_len", (long long)r - len);
    o_lasterr();
    o_bool("untouched", small[0] == '#');
    oracle_case_end();
}

int main(void)
{
    oracle_begin("kernel32/system_dirs");

    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    GetWindowsDirectoryW(windir, MAX_PATH);

    {
        wchar_t sroot[MAX_PATH], swin[MAX_PATH];
        DWORD n = GetEnvironmentVariableW(L"SystemRoot", sroot, MAX_PATH);
        GetSystemWindowsDirectoryW(swin, MAX_PATH);
        oracle_case("windir_shape");
        o_bool("drive_root_form", windir[0] && windir[1] == L':' && windir[2] == L'\\');
        o_bool("single_component", !wcschr(windir + 3, L'\\'));
        o_bool("component_is_windows_any_case", !_wcsicmp(windir + 3, L"Windows"));
        o_bool("systemroot_env_matches_any_case", n && !_wcsicmp(sroot, windir));
        o_bool("system_windows_dir_same", !wcscmp(swin, windir));
        oracle_case_end();
    }

    dir_cases("windows", GetWindowsDirectoryW);
    dir_cases("system_windows", GetSystemWindowsDirectoryW);
    dir_cases("system", GetSystemDirectoryW);
    dir_cases("wow64", (dir_fn)(void *)GetProcAddress(k32, "GetSystemWow64DirectoryW"));
    dir_cases_a("windows_a", GetWindowsDirectoryA, GetWindowsDirectoryW);
    dir_cases_a("system_a", GetSystemDirectoryA, GetSystemDirectoryW);
    dir_cases_a("wow64_a", (dir_fn_a)(void *)GetProcAddress(k32, "GetSystemWow64DirectoryA"),
                (dir_fn)(void *)GetProcAddress(k32, "GetSystemWow64DirectoryW"));

    {
        wow2_fn f = (wow2_fn)(void *)GetProcAddress(k32, "GetSystemWow64Directory2W");
        if (!f) f = (wow2_fn)(void *)GetProcAddress(GetModuleHandleW(L"kernelbase.dll"), "GetSystemWow64Directory2W");
        static const struct { const char *n; WORD m; } machines[] = {
            { "wow64_2_i386", IMAGE_FILE_MACHINE_I386 },
            { "wow64_2_amd64", IMAGE_FILE_MACHINE_AMD64 },
            { "wow64_2_armnt", IMAGE_FILE_MACHINE_ARMNT },
            { "wow64_2_arm64", IMAGE_FILE_MACHINE_ARM64 },
            { "wow64_2_unknown", IMAGE_FILE_MACHINE_UNKNOWN },
            { "wow64_2_bogus", 0x1234 },
        };
        for (size_t i = 0; i < sizeof machines / sizeof machines[0]; i++) {
            wchar_t buf[MAX_PATH];
            UINT r = 0;
            oracle_case(machines[i].n);
            o_bool("have_fn", f != NULL);
            if (f) {
                wmemset(buf, 0, MAX_PATH);
                SetLastError(ORACLE_SENTINEL);
                r = f(buf, MAX_PATH, machines[i].m);
                oracle_last_error = GetLastError();
                o_bool("ret_is_length", r == wcslen(buf));
                o_bool("nonzero", r != 0);
                o_lasterr();
                if (r) tok("path", buf);
            }
            oracle_case_end();
        }
    }

    return oracle_end();
}
