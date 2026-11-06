/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: AssocQueryStringW (shlwapi) for file types the test
 * registers itself under HKCU\Software\Classes: ASSOCSTR_COMMAND,
 * ASSOCSTR_EXECUTABLE, ASSOCSTR_FRIENDLYDOCNAME, ASSOCSTR_FRIENDLYAPPNAME,
 * ASSOCSTR_DEFAULTICON and ASSOCSTR_SHELLNEWVALUE with no verb, "open",
 * "edit", a default verb set through the shell key's default value and a
 * missing verb; the size-query form (pszOut = NULL), short buffers with
 * and without ASSOCF_NOTRUNCATE, an extension that is not registered,
 * ASSOCF_INIT_IGNOREUNKNOWN, a ProgID passed directly, an extension
 * without a dot, and a document's full path. Also the system's own .txt
 * and .exe associations.
 *
 * The registered handler is a copy of this test program in the working
 * directory (handler.exe, the same path on every machine). Its full path
 * prints as "<exe>", the Windows directory as "<windir>". The test removes
 * its keys before and after.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/nf-shlwapi-assocquerystringw
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/ne-shlwapi-assocstr
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/ne-shlwapi-assocf
 *       https://learn.microsoft.com/en-us/windows/win32/shell/fa-progids
 *       https://learn.microsoft.com/en-us/windows/win32/shell/fa-verbs
 *       https://learn.microsoft.com/en-us/windows/win32/shell/how-to-register-a-file-type-for-a-new-application
 */
#include "oracle.h"
#include <shlwapi.h>
#include <shlobj.h>

static HRESULT(WINAPI *pAssocQueryStringW)(ASSOCF, ASSOCSTR, LPCWSTR, LPCWSTR, LPWSTR, DWORD *);
static void(WINAPI *pSHChangeNotify)(LONG, UINT, LPCVOID, LPCVOID);

#define LOAD(m, f) (*(FARPROC *)&p##f = GetProcAddress(m, #f))

static wchar_t exe[MAX_PATH], windir[MAX_PATH], cwd[MAX_PATH];

/* Replace every occurrence of the exe path, the cwd and the Windows dir. */
static void o_tok(const char *k, const wchar_t *s)
{
    static wchar_t out[4096];
    const wchar_t *p = s;
    size_t o = 0;
    while (*p && o < 4000) {
        size_t le = wcslen(exe), lc = wcslen(cwd), lw = wcslen(windir);
        if (!_wcsnicmp(p, exe, le)) { o += swprintf(out + o, 4096 - o, L"<exe>"); p += le; }
        else if (!_wcsnicmp(p, cwd, lc)) { o += swprintf(out + o, 4096 - o, L"<cwd>"); p += lc; }
        else if (!_wcsnicmp(p, windir, lw)) { o += swprintf(out + o, 4096 - o, L"<windir>"); p += lw; }
        else out[o++] = *p++;
    }
    out[o] = 0;
    o_wstr(k, out);
}

static void set_str(const wchar_t *key, const wchar_t *name, const wchar_t *val)
{
    HKEY h;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, key, 0, NULL, 0, KEY_WRITE, NULL, &h, NULL)) return;
    RegSetValueExW(h, name, 0, REG_SZ, (const BYTE *)val, (DWORD)(wcslen(val) + 1) * sizeof(wchar_t));
    RegCloseKey(h);
}

#define CLS L"Software\\Classes\\"

static void cleanup(void)
{
    static const wchar_t *keys[] = {
        CLS L".setuoracle1", CLS L".setuoracle2", CLS L".setuoracle3", CLS L"SetuOracle.Doc1", CLS L"SetuOracle.Doc2",
        CLS L"SetuOracle.Doc3",
    };
    for (size_t i = 0; i < sizeof keys / sizeof keys[0]; i++) {
        RegDeleteTreeW(HKEY_CURRENT_USER, keys[i]);
        RegDeleteKeyW(HKEY_CURRENT_USER, keys[i]);
    }
}

static void q(const char *name, ASSOCF f, ASSOCSTR s, const wchar_t *assoc, const wchar_t *verb)
{
    wchar_t buf[1024];
    DWORD n = 1024;
    buf[0] = L'#'; buf[1] = 0;
    HRESULT hr = pAssocQueryStringW(f, s, assoc, verb, buf, &n);
    oracle_case(name);
    o_hex32("hr", (DWORD)hr);
    if (SUCCEEDED(hr)) {
        o_tok("out", buf);
        o_u32("len_out", n);
    } else {
        o_bool("out_untouched", buf[0] == L'#');
    }
    oracle_case_end();
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    oracle_begin("shell32/assoc_query");
    HMODULE shl = LoadLibraryW(L"shlwapi.dll");
    HMODULE sh = LoadLibraryW(L"shell32.dll");
    LOAD(shl, AssocQueryStringW);
    LOAD(sh, SHChangeNotify);
    oracle_case("setup");
    o_bool("loaded", pAssocQueryStringW && pSHChangeNotify);
    oracle_case_end();
    if (!pAssocQueryStringW || !pSHChangeNotify) return oracle_end();
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(NULL, self, MAX_PATH);
    GetWindowsDirectoryW(windir, MAX_PATH);
    GetCurrentDirectoryW(MAX_PATH, cwd);
    /* The handler is a copy of this program in the working directory, so its
     * path (and length) is the same on every machine. */
    swprintf(exe, MAX_PATH, L"%ls\\handler.exe", cwd);
    CopyFileW(self, exe, FALSE);

    cleanup();
    wchar_t cmd[2 * MAX_PATH];
    /* .setuoracle1 -> SetuOracle.Doc1: open + edit verbs, default icon, friendly name. */
    set_str(CLS L".setuoracle1", NULL, L"SetuOracle.Doc1");
    set_str(CLS L".setuoracle1\\ShellNew", L"NullFile", L"");
    set_str(CLS L"SetuOracle.Doc1", NULL, L"Setu Oracle Document");
    set_str(CLS L"SetuOracle.Doc1\\DefaultIcon", NULL, L"C:\\setu-icons\\doc.ico,2");
    swprintf(cmd, 2 * MAX_PATH, L"\"%ls\" --child open \"%%1\"", exe);
    set_str(CLS L"SetuOracle.Doc1\\shell\\open\\command", NULL, cmd);
    swprintf(cmd, 2 * MAX_PATH, L"\"%ls\" --child edit \"%%1\" %%*", exe);
    set_str(CLS L"SetuOracle.Doc1\\shell\\edit\\command", NULL, cmd);
    /* .setuoracle2 -> SetuOracle.Doc2: default verb "edit" via shell's default value,
     * FriendlyTypeName overriding the default value, unquoted command. */
    set_str(CLS L".setuoracle2", NULL, L"SetuOracle.Doc2");
    set_str(CLS L"SetuOracle.Doc2", NULL, L"Doc2 default value");
    set_str(CLS L"SetuOracle.Doc2", L"FriendlyTypeName", L"Doc2 Friendly Type");
    set_str(CLS L"SetuOracle.Doc2\\shell", NULL, L"edit");
    swprintf(cmd, 2 * MAX_PATH, L"\"%ls\" --child open2 %%1", exe);
    set_str(CLS L"SetuOracle.Doc2\\shell\\open\\command", NULL, cmd);
    swprintf(cmd, 2 * MAX_PATH, L"\"%ls\" --child edit2 \"%%1\"", exe);
    set_str(CLS L"SetuOracle.Doc2\\shell\\edit\\command", NULL, cmd);
    /* .setuoracle3 -> SetuOracle.Doc3: only a custom verb, no open. */
    set_str(CLS L".setuoracle3", NULL, L"SetuOracle.Doc3");
    swprintf(cmd, 2 * MAX_PATH, L"\"%ls\" --child custom \"%%1\"", exe);
    set_str(CLS L"SetuOracle.Doc3\\shell\\custom\\command", NULL, cmd);
    pSHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);

    static const struct { const char *name; ASSOCSTR s; } strs[] = {
        {"command", ASSOCSTR_COMMAND},
        {"executable", ASSOCSTR_EXECUTABLE},
        {"friendlydocname", ASSOCSTR_FRIENDLYDOCNAME},
        {"friendlyappname", ASSOCSTR_FRIENDLYAPPNAME},
        {"defaulticon", ASSOCSTR_DEFAULTICON},
    };
    static const struct { const char *name; const wchar_t *verb; } verbs[] = {
        {"noverb", NULL}, {"open", L"open"}, {"edit", L"edit"}, {"missing", L"print"},
    };
    char nm[96];
    for (size_t i = 0; i < 5; i++)
        for (size_t j = 0; j < 4; j++) {
            snprintf(nm, sizeof nm, "doc1_%s_%s", strs[i].name, verbs[j].name);
            q(nm, 0, strs[i].s, L".setuoracle1", verbs[j].verb);
        }
    for (size_t i = 0; i < 4; i++) {
        snprintf(nm, sizeof nm, "doc2_%s_noverb", strs[i].name);
        q(nm, 0, strs[i].s, L".setuoracle2", NULL);
    }
    q("doc2_command_open", 0, ASSOCSTR_COMMAND, L".setuoracle2", L"open");
    q("doc3_command_noverb", 0, ASSOCSTR_COMMAND, L".setuoracle3", NULL);
    q("doc3_command_open", 0, ASSOCSTR_COMMAND, L".setuoracle3", L"open");
    q("doc3_friendlydocname", 0, ASSOCSTR_FRIENDLYDOCNAME, L".setuoracle3", NULL);
    q("doc1_shellnewvalue", 0, ASSOCSTR_SHELLNEWVALUE, L".setuoracle1", NULL);
    q("doc1_progid_direct", 0, ASSOCSTR_COMMAND, L"SetuOracle.Doc1", L"open");
    q("doc1_no_dot", 0, ASSOCSTR_COMMAND, L"setuoracle1", L"open");
    q("doc1_upper_case_ext", 0, ASSOCSTR_COMMAND, L".SETUORACLE1", L"open");
    {
        wchar_t full[MAX_PATH];
        swprintf(full, MAX_PATH, L"%ls\\some file.setuoracle1", cwd);
        q("doc1_full_path", 0, ASSOCSTR_EXECUTABLE, full, NULL);
        q("doc1_full_path_command", 0, ASSOCSTR_COMMAND, full, L"open");
    }
    q("unknown_command", 0, ASSOCSTR_COMMAND, L".setunoassoc", NULL);
    q("unknown_executable", 0, ASSOCSTR_EXECUTABLE, L".setunoassoc", NULL);
    q("unknown_friendlydocname", 0, ASSOCSTR_FRIENDLYDOCNAME, L".setunoassoc", NULL);
    q("unknown_ignoreunknown", ASSOCF_INIT_IGNOREUNKNOWN, ASSOCSTR_COMMAND, L".setunoassoc", NULL);
    q("unknown_friendlydocname_ignoreunknown", ASSOCF_INIT_IGNOREUNKNOWN, ASSOCSTR_FRIENDLYDOCNAME, L".setunoassoc", NULL);
    q("empty_assoc", 0, ASSOCSTR_COMMAND, L"", NULL);
    q("bogus_assocstr", 0, (ASSOCSTR)0x7777, L".setuoracle1", NULL);

    /* Size query and short buffers. */
    {
        DWORD n = 0;
        HRESULT hr = pAssocQueryStringW(0, ASSOCSTR_FRIENDLYDOCNAME, L".setuoracle1", NULL, NULL, &n);
        oracle_case("size_query_null_buffer");
        o_hex32("hr", (DWORD)hr);
        o_u32("needed", n);
        oracle_case_end();
        wchar_t small[8];
        wmemset(small, L'#', 8);
        n = 5;
        hr = pAssocQueryStringW(0, ASSOCSTR_FRIENDLYDOCNAME, L".setuoracle1", NULL, small, &n);
        oracle_case("short_buffer");
        o_hex32("hr", (DWORD)hr);
        o_u32("n", n);
        o_bool("terminated", wmemchr(small, 0, 5) != NULL);
        if (wmemchr(small, 0, 5)) o_wstr("out", small);
        oracle_case_end();
        wmemset(small, L'#', 8);
        n = 5;
        hr = pAssocQueryStringW(ASSOCF_NOTRUNCATE, ASSOCSTR_FRIENDLYDOCNAME, L".setuoracle1", NULL, small, &n);
        oracle_case("short_buffer_notruncate");
        o_hex32("hr", (DWORD)hr);
        o_u32("n", n);
        o_bool("untouched", small[0] == L'#');
        oracle_case_end();
        n = 0;
        hr = pAssocQueryStringW(0, ASSOCSTR_EXECUTABLE, L".setuoracle1", NULL, NULL, &n);
        oracle_case("size_query_executable");
        o_hex32("hr", (DWORD)hr);
        o_bool("needed_is_exe_len_plus_1", n == wcslen(exe) + 1);
        oracle_case_end();
    }

    /* The system's own associations. */
    q("txt_executable", 0, ASSOCSTR_EXECUTABLE, L".txt", NULL);
    q("txt_command", 0, ASSOCSTR_COMMAND, L".txt", L"open");
    q("exe_command", 0, ASSOCSTR_COMMAND, L".exe", L"open");
    q("exe_friendlydocname", 0, ASSOCSTR_FRIENDLYDOCNAME, L".exe", NULL);
    q("folder_command", 0, ASSOCSTR_COMMAND, L"Folder", L"open");

    cleanup();
    pSHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
    q("after_cleanup", 0, ASSOCSTR_COMMAND, L".setuoracle1", NULL);
    return oracle_end();
}
