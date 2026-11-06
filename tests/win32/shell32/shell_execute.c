/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: ShellExecuteExW, ShellExecuteW and FindExecutableW with
 * file types the test registers under HKCU\Software\Classes. The handler
 * is a copy of this program (handler.exe in the working directory); when
 * started with "--child" it appends its command line and current
 * directory to the file named by %ORACLE_CHILD_OUT% and exits, and the
 * parent prints what the child saw.
 *
 * Covered: the default verb, "open", "edit", a verb set as the default by
 * the shell key, a verb that doesn't exist, %1 / %L / %* / %2 expansion
 * with lpParameters, a command without %1, lpDirectory, SEE_MASK_DOENVSUBST,
 * SEE_MASK_CLASSNAME, running an .exe directly with parameters, file names
 * with spaces, missing files and files without an association (error
 * codes, hInstApp, GetLastError), and FindExecutableW for each.
 *
 * No UI: SEE_MASK_FLAG_NO_UI | SEE_MASK_NOASYNC | SEE_MASK_NOCLOSEPROCESS
 * on every ShellExecuteExW call; the parent waits for each child. Paths
 * print as "<cwd>" and "<exe>". The test removes its keys before and after.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-shellexecuteexw
 *       https://learn.microsoft.com/en-us/windows/win32/api/shellapi/ns-shellapi-shellexecuteinfow
 *       https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-shellexecutew
 *       https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-findexecutablew
 *       https://learn.microsoft.com/en-us/windows/win32/shell/launch
 *       https://learn.microsoft.com/en-us/windows/win32/shell/fa-verbs
 */
#include "oracle.h"
#include <shellapi.h>
#include <shlobj.h>

static BOOL(WINAPI *pShellExecuteExW)(SHELLEXECUTEINFOW *);
static HINSTANCE(WINAPI *pShellExecuteW)(HWND, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, INT);
static HINSTANCE(WINAPI *pFindExecutableW)(LPCWSTR, LPCWSTR, LPWSTR);
static void(WINAPI *pSHChangeNotify)(LONG, UINT, LPCVOID, LPCVOID);
static HRESULT(WINAPI *pCoInitializeEx)(void *, DWORD);

#define LOAD(m, f) (*(FARPROC *)&p##f = GetProcAddress(m, #f))
#define CLS L"Software\\Classes\\"

static wchar_t exe[MAX_PATH], cwd[MAX_PATH], outfile[MAX_PATH], shortcwd[MAX_PATH];

/* ---- child mode ---- */
static int child_main(void)
{
    wchar_t out[MAX_PATH], dir[MAX_PATH];
    if (!GetEnvironmentVariableW(L"ORACLE_CHILD_OUT", out, MAX_PATH)) return 3;
    GetCurrentDirectoryW(MAX_PATH, dir);
    HANDLE h = CreateFileW(out, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return 4;
    const wchar_t *cl = GetCommandLineW();
    DWORD n;
    WriteFile(h, cl, (DWORD)wcslen(cl) * 2, &n, NULL);
    WriteFile(h, L"\n", 2, &n, NULL);
    WriteFile(h, dir, (DWORD)wcslen(dir) * 2, &n, NULL);
    WriteFile(h, L"\n", 2, &n, NULL);
    CloseHandle(h);
    return 0;
}

/* Replace the handler path, the cwd (long and 8.3 forms) everywhere. */
static void o_tok(const char *k, const wchar_t *s)
{
    static wchar_t out[4096];
    const wchar_t *p = s;
    size_t o = 0;
    size_t le = wcslen(exe), lc = wcslen(cwd), ls = wcslen(shortcwd);
    while (*p && o < 4000) {
        if (!_wcsnicmp(p, exe, le)) { o += swprintf(out + o, 4096 - o, L"<exe>"); p += le; }
        else if (!_wcsnicmp(p, cwd, lc)) { o += swprintf(out + o, 4096 - o, L"<cwd>"); p += lc; }
        else if (ls && !_wcsnicmp(p, shortcwd, ls)) { o += swprintf(out + o, 4096 - o, L"<shortcwd>"); p += ls; }
        else out[o++] = *p++;
    }
    out[o] = 0;
    o_wstr(k, out);
}

/* Print what the child(ren) wrote, then reset the file. */
static void o_child(void)
{
    static wchar_t buf[8192];
    HANDLE h = CreateFileW(outfile, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    DWORD n = 0;
    if (h == INVALID_HANDLE_VALUE) { o_bool("child_ran", 0); return; }
    ReadFile(h, buf, sizeof buf - 2, &n, NULL);
    CloseHandle(h);
    DeleteFileW(outfile);
    buf[n / 2] = 0;
    o_bool("child_ran", n > 0);
    wchar_t *nl = wcschr(buf, L'\n');
    if (nl) {
        *nl = 0;
        o_tok("child_cmdline", buf);
        wchar_t *d = nl + 1, *e = wcschr(d, L'\n');
        if (e) *e = 0;
        o_tok("child_cwd", d);
    }
}

static void set_str(const wchar_t *key, const wchar_t *name, const wchar_t *val)
{
    HKEY h;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, key, 0, NULL, 0, KEY_WRITE, NULL, &h, NULL)) return;
    RegSetValueExW(h, name, 0, REG_SZ, (const BYTE *)val, (DWORD)(wcslen(val) + 1) * sizeof(wchar_t));
    RegCloseKey(h);
}

static void cleanup(void)
{
    static const wchar_t *keys[] = {
        CLS L".setuoracle1", CLS L".setuoracle2", CLS L".setuoracle3", CLS L"SetuOracle.Doc1",
        CLS L"SetuOracle.Doc2", CLS L"SetuOracle.Doc3",
    };
    for (size_t i = 0; i < sizeof keys / sizeof keys[0]; i++) {
        RegDeleteTreeW(HKEY_CURRENT_USER, keys[i]);
        RegDeleteKeyW(HKEY_CURRENT_USER, keys[i]);
    }
}

static void make_file(const wchar_t *name)
{
    HANDLE h = CreateFileW(name, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    DWORD n;
    WriteFile(h, "doc", 3, &n, NULL);
    CloseHandle(h);
}

static void see(const char *name, const wchar_t *verb, const wchar_t *file, const wchar_t *params,
                const wchar_t *dir, ULONG extra_mask, const wchar_t *cls)
{
    SHELLEXECUTEINFOW sei;
    memset(&sei, 0, sizeof sei);
    sei.cbSize = sizeof sei;
    sei.fMask = SEE_MASK_FLAG_NO_UI | SEE_MASK_NOASYNC | SEE_MASK_NOCLOSEPROCESS | extra_mask;
    sei.lpVerb = verb;
    sei.lpFile = file;
    sei.lpParameters = params;
    sei.lpDirectory = dir;
    sei.lpClass = cls;
    sei.nShow = SW_SHOWNORMAL;
    BOOL r;
    ORACLE_CALL(r, pShellExecuteExW(&sei));
    if (sei.hProcess) {
        WaitForSingleObject(sei.hProcess, 20000);
        DWORD code = 99;
        GetExitCodeProcess(sei.hProcess, &code);
        CloseHandle(sei.hProcess);
        oracle_case(name);
        o_u32("child_exit", code);
    } else {
        oracle_case(name);
    }
    o_bool("ret", r);
    o_lasterr();
    o_u64("hinstapp", (ULONG_PTR)sei.hInstApp);
    o_bool("has_process", sei.hProcess != NULL);
    o_child();
    oracle_case_end();
}

static void find_exe(const char *name, const wchar_t *file, const wchar_t *dir)
{
    wchar_t buf[MAX_PATH];
    wmemset(buf, L'#', 8);
    buf[8] = 0;
    HINSTANCE h;
    ORACLE_CALL(h, pFindExecutableW(file, dir, buf));
    oracle_case(name);
    o_u64("ret", (ULONG_PTR)h);
    o_lasterr();
    o_tok("result", buf);
    oracle_case_end();
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "--child")) return child_main();
    oracle_begin("shell32/shell_execute");
    HMODULE sh = LoadLibraryW(L"shell32.dll");
    HMODULE ole = LoadLibraryW(L"ole32.dll");
    LOAD(sh, ShellExecuteExW);
    LOAD(sh, ShellExecuteW);
    LOAD(sh, FindExecutableW);
    LOAD(sh, SHChangeNotify);
    LOAD(ole, CoInitializeEx);
    oracle_case("setup");
    o_bool("loaded", pShellExecuteExW && pShellExecuteW && pFindExecutableW && pSHChangeNotify && pCoInitializeEx);
    oracle_case_end();
    if (!pShellExecuteExW || !pShellExecuteW || !pFindExecutableW || !pSHChangeNotify || !pCoInitializeEx)
        return oracle_end();
    /* The documentation asks callers to initialise COM (apartment-threaded). */
    pCoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    wchar_t self[MAX_PATH];
    GetModuleFileNameW(NULL, self, MAX_PATH);
    GetCurrentDirectoryW(MAX_PATH, cwd);
    if (!GetShortPathNameW(cwd, shortcwd, MAX_PATH) || !_wcsicmp(shortcwd, cwd)) shortcwd[0] = 0;
    swprintf(exe, MAX_PATH, L"%ls\\handler.exe", cwd);
    CopyFileW(self, exe, FALSE);
    swprintf(outfile, MAX_PATH, L"%ls\\child_out.txt", cwd);
    SetEnvironmentVariableW(L"ORACLE_CHILD_OUT", outfile);
    CreateDirectoryW(L"wd", NULL);
    CreateDirectoryW(L"dir with space", NULL);
    make_file(L"doc.setuoracle1");
    make_file(L"dir with space\\my doc.setuoracle1");
    make_file(L"doc.setuoracle2");
    make_file(L"doc.setuoracle3");
    make_file(L"doc.setunoassoc");

    cleanup();
    wchar_t cmd[2 * MAX_PATH];
    set_str(CLS L".setuoracle1", NULL, L"SetuOracle.Doc1");
    set_str(CLS L"SetuOracle.Doc1", NULL, L"Setu Oracle Document");
    swprintf(cmd, 2 * MAX_PATH, L"\"%ls\" --child open \"%%1\"", exe);
    set_str(CLS L"SetuOracle.Doc1\\shell\\open\\command", NULL, cmd);
    swprintf(cmd, 2 * MAX_PATH, L"\"%ls\" --child edit \"%%1\" %%*", exe);
    set_str(CLS L"SetuOracle.Doc1\\shell\\edit\\command", NULL, cmd);
    swprintf(cmd, 2 * MAX_PATH, L"\"%ls\" --child long %%L", exe);
    set_str(CLS L"SetuOracle.Doc1\\shell\\long\\command", NULL, cmd);
    swprintf(cmd, 2 * MAX_PATH, L"\"%ls\" --child noarg", exe);
    set_str(CLS L"SetuOracle.Doc1\\shell\\noarg\\command", NULL, cmd);
    swprintf(cmd, 2 * MAX_PATH, L"\"%ls\" --child second \"%%2\" \"%%1\"", exe);
    set_str(CLS L"SetuOracle.Doc1\\shell\\second\\command", NULL, cmd);
    swprintf(cmd, 2 * MAX_PATH, L"\"%ls\" --child unquoted %%1", exe);
    set_str(CLS L"SetuOracle.Doc1\\shell\\unquoted\\command", NULL, cmd);
    set_str(CLS L".setuoracle2", NULL, L"SetuOracle.Doc2");
    set_str(CLS L"SetuOracle.Doc2\\shell", NULL, L"edit");
    swprintf(cmd, 2 * MAX_PATH, L"\"%ls\" --child open2 \"%%1\"", exe);
    set_str(CLS L"SetuOracle.Doc2\\shell\\open\\command", NULL, cmd);
    swprintf(cmd, 2 * MAX_PATH, L"\"%ls\" --child edit2 \"%%1\"", exe);
    set_str(CLS L"SetuOracle.Doc2\\shell\\edit\\command", NULL, cmd);
    set_str(CLS L".setuoracle3", NULL, L"SetuOracle.Doc3");
    swprintf(cmd, 2 * MAX_PATH, L"\"%ls\" --child custom \"%%1\"", exe);
    set_str(CLS L"SetuOracle.Doc3\\shell\\custom\\command", NULL, cmd);
    pSHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
    DeleteFileW(outfile);

    /* ---- ShellExecuteExW ---- */
    see("default_verb", NULL, L"doc.setuoracle1", NULL, NULL, 0, NULL);
    see("open_verb", L"open", L"doc.setuoracle1", NULL, NULL, 0, NULL);
    see("edit_verb_with_params", L"edit", L"doc.setuoracle1", L"p1 \"p 2\"", NULL, 0, NULL);
    see("long_name_verb", L"long", L"doc.setuoracle1", NULL, NULL, 0, NULL);
    see("noarg_verb", L"noarg", L"doc.setuoracle1", NULL, NULL, 0, NULL);
    see("second_param_verb", L"second", L"doc.setuoracle1", L"alpha beta", NULL, 0, NULL);
    see("unquoted_verb_space_in_path", L"unquoted", L"dir with space\\my doc.setuoracle1", NULL, NULL, 0, NULL);
    see("open_space_in_path", L"open", L"dir with space\\my doc.setuoracle1", NULL, NULL, 0, NULL);
    {
        wchar_t abs[MAX_PATH];
        swprintf(abs, MAX_PATH, L"%ls\\doc.setuoracle1", cwd);
        see("absolute_path", L"open", abs, NULL, NULL, 0, NULL);
    }
    SetEnvironmentVariableW(L"ORACLE_DOC", L"doc.setuoracle1");
    see("missing_verb", L"print", L"doc.setuoracle1", NULL, NULL, 0, NULL);
    see("shell_default_verb_edit", NULL, L"doc.setuoracle2", NULL, NULL, 0, NULL);
    see("only_custom_verb_default", NULL, L"doc.setuoracle3", NULL, NULL, 0, NULL);
    see("only_custom_verb_open", L"open", L"doc.setuoracle3", NULL, NULL, 0, NULL);
    see("with_directory", L"open", L"doc.setuoracle1", NULL, L"wd", 0, NULL);
    see("with_absolute_directory", L"open", L"doc.setuoracle1", NULL, cwd, 0, NULL);
    see("doenvsubst", L"open", L"%ORACLE_DOC%", NULL, NULL, SEE_MASK_DOENVSUBST, NULL);
    see("classname", L"open", L"doc.setunoassoc", NULL, NULL, SEE_MASK_CLASSNAME, L".setuoracle1");
    see("classname_progid", L"open", L"doc.setunoassoc", NULL, NULL, SEE_MASK_CLASSNAME, L"SetuOracle.Doc1");
    see("exe_direct_params", NULL, L"handler.exe", L"--child direct \"a b\" c", NULL, 0, NULL);
    see("exe_direct_open_verb", L"open", L"handler.exe", L"--child directopen", NULL, 0, NULL);
    see("exe_direct_absolute", NULL, exe, L"--child abs", L"wd", 0, NULL);
    see("missing_file", L"open", L"no_such.setuoracle1", NULL, NULL, 0, NULL);
    see("missing_exe", NULL, L"no_such_program.exe", NULL, NULL, 0, NULL);
    see("no_association", L"open", L"doc.setunoassoc", NULL, NULL, 0, NULL);
    see("no_association_default", NULL, L"doc.setunoassoc", NULL, NULL, 0, NULL);
    see("missing_directory", L"open", L"doc.setuoracle1", NULL, L"no_such_dir", 0, NULL);
    {
        SHELLEXECUTEINFOW sei;
        memset(&sei, 0, sizeof sei);
        sei.cbSize = sizeof sei - 4;
        sei.fMask = SEE_MASK_FLAG_NO_UI;
        sei.lpFile = L"doc.setuoracle1";
        BOOL r;
        ORACLE_CALL(r, pShellExecuteExW(&sei));
        oracle_case("bad_cbsize");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        DeleteFileW(outfile);
    }

    /* ---- ShellExecuteW: return value classes ---- */
    {
        static const struct { const char *name; const wchar_t *verb, *file; } se[] = {
            {"se_open_doc", L"open", L"doc.setuoracle1"},
            {"se_missing_file", L"open", L"no_such.setuoracle1"},
            {"se_no_association", L"open", L"doc.setunoassoc"},
            {"se_missing_verb", L"print", L"doc.setuoracle1"},
        };
        for (size_t i = 0; i < 4; i++) {
            HINSTANCE h;
            ORACLE_CALL(h, pShellExecuteW(NULL, se[i].verb, se[i].file, NULL, NULL, SW_SHOWNORMAL));
            ULONG_PTR v = (ULONG_PTR)h;
            /* Let a started child finish before the next case. */
            for (int t = 0; v > 32 && t < 100 && GetFileAttributesW(outfile) == INVALID_FILE_ATTRIBUTES; t++) Sleep(100);
            Sleep(v > 32 ? 300 : 0);
            oracle_case(se[i].name);
            o_bool("success", v > 32);
            if (v <= 32) o_u64("ret", v);
            o_lasterr();
            o_child();
            oracle_case_end();
        }
    }

    /* ---- FindExecutableW ---- */
    find_exe("find_doc", L"doc.setuoracle1", NULL);
    find_exe("find_doc_with_dir", L"my doc.setuoracle1", L"dir with space");
    find_exe("find_doc_default_edit", L"doc.setuoracle2", NULL);
    find_exe("find_doc_custom_only", L"doc.setuoracle3", NULL);
    find_exe("find_exe_itself", L"handler.exe", NULL);
    find_exe("find_missing_file", L"no_such.setuoracle1", NULL);
    find_exe("find_no_association", L"doc.setunoassoc", NULL);
    find_exe("find_missing_dir", L"doc.setuoracle1", L"no_such_dir");

    cleanup();
    pSHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
    return oracle_end();
}
