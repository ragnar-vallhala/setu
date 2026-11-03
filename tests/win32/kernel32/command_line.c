/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: GetCommandLineW / GetCommandLineA and GetStartupInfoW /
 * GetStartupInfoA, seen by children that this test starts with chosen
 * command lines and STARTUPINFO values.
 *
 * The child writes what it saw to child.txt (UTF-8) in the current
 * directory; the parent prints that text. The path of the .exe is replaced
 * by "<self>" so nothing machine-specific is printed.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/processenv/nf-processenv-getcommandlinew
 *       https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getstartupinfow
 *       https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/ns-processthreadsapi-startupinfow
 *       https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessw
 */
#include "oracle.h"

static wchar_t self[MAX_PATH];

/* ---- child ------------------------------------------------------------ */

static void put(FILE *f, const char *k, const char *v) { fprintf(f, "%s=%s\n", k, v ? v : "(null)"); }

static int child(void)
{
    FILE *f = _wfopen(L"child.txt", L"wb");
    const wchar_t *cl = GetCommandLineW();
    const char *cla = GetCommandLineA();
    put(f, "cmdW", oracle_utf8(cl));
    put(f, "cmdA", cla);
    fprintf(f, "same_pointer_twice=%d\n", GetCommandLineW() == cl);

    STARTUPINFOW si;
    memset(&si, 0xcc, sizeof si);
    SetLastError(ORACLE_SENTINEL);
    GetStartupInfoW(&si);
    fprintf(f, "last_error_unchanged=%d\n", GetLastError() == ORACLE_SENTINEL);
    fprintf(f, "cb=%lu\n", si.cb);
    put(f, "desktop_null", si.lpDesktop ? "0" : "1");
    put(f, "title", si.lpTitle ? oracle_utf8(si.lpTitle) : "(null)");
    fprintf(f, "reserved_null=%d\n", si.lpReserved == NULL);
    fprintf(f, "x=%lu y=%lu xs=%lu ys=%lu\n", si.dwX, si.dwY, si.dwXSize, si.dwYSize);
    fprintf(f, "xcc=%lu ycc=%lu fill=%lu\n", si.dwXCountChars, si.dwYCountChars, si.dwFillAttribute);
    fprintf(f, "flags=0x%lx show=%u cbres2=%u res2_null=%d\n", si.dwFlags, si.wShowWindow, si.cbReserved2,
            si.lpReserved2 == NULL);
    fprintf(f, "std_handles_set=%d\n", (si.dwFlags & STARTF_USESTDHANDLES) != 0);

    STARTUPINFOA sa;
    memset(&sa, 0xcc, sizeof sa);
    GetStartupInfoA(&sa);
    fprintf(f, "A.cb=%lu A.flags=0x%lx A.show=%u\n", sa.cb, sa.dwFlags, sa.wShowWindow);
    put(f, "A.title", sa.lpTitle ? sa.lpTitle : "(null)");
    fclose(f);
    return 0;
}

/* ---- parent ----------------------------------------------------------- */

static void replace_self(char *out, size_t n, const char *in)
{
    char selfu[MAX_PATH * 3];
    strcpy(selfu, oracle_utf8(self));
    size_t sl = strlen(selfu);
    out[0] = 0;
    while (*in && strlen(out) + 8 < n) {
        if (!strncmp(in, selfu, sl)) {
            strcat(out, "<self>");
            in += sl;
        } else {
            size_t l = strlen(out);
            out[l] = *in++;
            out[l + 1] = 0;
        }
    }
}

static void report(const char *name, BOOL ok, DWORD err)
{
    oracle_case(name);
    o_bool("spawned", ok);
    if (!ok) o_u32("error", err);
    FILE *f = _wfopen(L"child.txt", L"rb");
    if (f) {
        char line[1024], clean[1200];
        int i = 0;
        while (fgets(line, sizeof line, f)) {
            line[strcspn(line, "\n")] = 0;
            char k[16];
            snprintf(k, sizeof k, "l%02d", i++);
            replace_self(clean, sizeof clean, line);
            o_str(k, clean);
        }
        fclose(f);
        DeleteFileW(L"child.txt");
    }
    oracle_case_end();
}

static void run(const char *name, const wchar_t *app, const wchar_t *cmdfmt, STARTUPINFOW *si)
{
    wchar_t cmd[1024];
    STARTUPINFOW def = {.cb = sizeof def};
    PROCESS_INFORMATION pi;
    if (!si) si = &def;
    swprintf(cmd, 1024, cmdfmt, self);
    BOOL ok = CreateProcessW(app, cmd, NULL, NULL, FALSE, 0, NULL, NULL, si, &pi);
    DWORD err = GetLastError();
    if (ok) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    report(name, ok, err);
}

int main(int argc, char **argv)
{
    for (int i = 0; i < argc; i++)
        if (!strcmp(argv[i], "child")) return child();
    oracle_begin("kernel32/command_line");
    GetModuleFileNameW(NULL, self, MAX_PATH);

    /* The parent's own command line, as the harness started it. */
    {
        const wchar_t *cl = GetCommandLineW();
        size_t n = wcslen(cl);
        oracle_case("parent_cmdline_shape");
        o_bool("nonempty", n > 0);
        o_bool("same_pointer_twice", GetCommandLineW() == cl);
        o_bool("a_matches_w", !strcmp(GetCommandLineA(), oracle_utf8(cl)));
        oracle_case_end();
    }

    run("quoted_self", NULL, L"\"%ls\" child", NULL);
    run("extra_spaces", NULL, L"\"%ls\"   child    a   b  ", NULL);
    run("tabs_and_quotes", NULL, L"\"%ls\" child\t\"x y\"\t\"\"", NULL);
    run("unicode_arg", NULL, L"\"%ls\" child \x00e9\x4e2d\x0416", NULL);
    run("app_name_and_cmdline_differ", self, L"whatever.exe child", NULL);
    run("app_name_only_arg", self, L"child", NULL);
    {
        STARTUPINFOW si = {.cb = sizeof si};
        si.lpTitle = L"My Title";
        si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USEPOSITION | STARTF_USESIZE | STARTF_USECOUNTCHARS |
                     STARTF_USEFILLATTRIBUTE;
        si.wShowWindow = SW_SHOWMINNOACTIVE;
        si.dwX = 11;
        si.dwY = 22;
        si.dwXSize = 333;
        si.dwYSize = 444;
        si.dwXCountChars = 80;
        si.dwYCountChars = 25;
        si.dwFillAttribute = 0x1f;
        run("startupinfo_fields", NULL, L"\"%ls\" child", &si);
    }
    {
        STARTUPINFOW si = {.cb = sizeof si};
        si.dwFlags = 0x40000000 | STARTF_FORCEOFFFEEDBACK;
        si.wShowWindow = 7;
        run("startupinfo_unknown_flag_and_show_ignored", NULL, L"\"%ls\" child", &si);
    }
    {
        STARTUPINFOW si = {.cb = sizeof si};
        si.lpDesktop = L"";
        si.lpTitle = L"";
        run("startupinfo_empty_strings", NULL, L"\"%ls\" child", &si);
    }
    {
        STARTUPINFOW si = {.cb = sizeof si};
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = si.hStdOutput = si.hStdError = NULL;
        run("startupinfo_usestdhandles_null", NULL, L"\"%ls\" child", &si);
    }
    {
        STARTUPINFOW si = {.cb = sizeof si - 4};
        run("startupinfo_short_cb", NULL, L"\"%ls\" child", &si);
    }
    {
        STARTUPINFOW si = {.cb = 0};
        run("startupinfo_cb_zero", NULL, L"\"%ls\" child", &si);
    }

    return oracle_end();
}
