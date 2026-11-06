/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * compat-probe: helper for tools/compat/run.py, run inside the test prefix.
 *
 *   compat-probe windows      top-level windows, top of the z-order first:
 *                             window=HWND|pid|exe|class|title|L,T,R,B|visible|hidden[,minimized]
 *   compat-probe procs        processes: proc=pid|parent pid|exe
 *   compat-probe close HWND   PostMessageW(HWND, WM_CLOSE)
 *   compat-probe children HWND
 *                             the child windows (class, text) of that window
 *   compat-probe click HWND TEXT
 *                             BM_CLICK on the first button in that window whose
 *                             text, without '&', starts with TEXT (to answer a
 *                             first-run prompt that has no Cancel button)
 *   compat-probe uninstall    the Uninstall keys of HKLM (64- and 32-bit views)
 *                             and HKCU:
 *                             uninstall=root|view|key|DisplayName|UninstallString|QuietUninstallString
 *
 * Text is printed as UTF-8. Exit code 0 on success.
 *
 * Docs:
 *   https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-enumwindows
 *   https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-internalgetwindowtext
 *   https://learn.microsoft.com/en-us/windows/win32/api/tlhelp32/nf-tlhelp32-createtoolhelp32snapshot
 *   https://learn.microsoft.com/en-us/windows/win32/msi/uninstall-registry-key
 *   https://learn.microsoft.com/en-us/windows/win32/controls/bm-click
 */
#ifndef UNICODE
#define UNICODE
#endif
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

static void put(const WCHAR *s)
{
    char buf[4096];
    int n = WideCharToMultiByte(CP_UTF8, 0, s, -1, buf, sizeof(buf), NULL, NULL);
    if (n > 0) fputs(buf, stdout);
}

/* the field, with '|' and line breaks replaced so the line stays parseable */
static void field(const WCHAR *s)
{
    WCHAR tmp[1024];
    int i;
    for (i = 0; s[i] && i < 1023; i++)
        tmp[i] = (s[i] == L'|' || s[i] == L'\r' || s[i] == L'\n') ? L' ' : s[i];
    tmp[i] = 0;
    put(tmp);
}

static void exe_of(DWORD pid, WCHAR *out, DWORD len)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W pe;
    out[0] = 0;
    pe.dwSize = sizeof(pe);
    if (snap == INVALID_HANDLE_VALUE) return;
    if (Process32FirstW(snap, &pe))
        do
            if (pe.th32ProcessID == pid) { lstrcpynW(out, pe.szExeFile, len); break; }
        while (Process32NextW(snap, &pe));
    CloseHandle(snap);
}

static BOOL CALLBACK list_window(HWND hwnd, LPARAM lp)
{
    WCHAR title[256] = L"", cls[128] = L"", exe[MAX_PATH], line[64];
    RECT rc;
    DWORD pid = 0;

    (void)lp;
    InternalGetWindowText(hwnd, title, ARRAYSIZE(title)); /* never blocks on a hung window */
    GetClassNameW(hwnd, cls, ARRAYSIZE(cls));
    GetWindowRect(hwnd, &rc);
    GetWindowThreadProcessId(hwnd, &pid);
    exe_of(pid, exe, MAX_PATH);
    swprintf(line, ARRAYSIZE(line), L"window=%p|%lu|", hwnd, pid);
    put(line);
    field(exe); put(L"|");
    field(cls); put(L"|");
    field(title);
    swprintf(line, ARRAYSIZE(line), L"|%ld,%ld,%ld,%ld|%ls%ls\n", rc.left, rc.top, rc.right, rc.bottom,
             IsWindowVisible(hwnd) ? L"visible" : L"hidden", IsIconic(hwnd) ? L",minimized" : L"");
    put(line);
    return TRUE;
}

static int procs(void)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W pe;
    WCHAR line[64];
    pe.dwSize = sizeof(pe);
    if (snap == INVALID_HANDLE_VALUE) return 1;
    if (Process32FirstW(snap, &pe))
        do
        {
            swprintf(line, ARRAYSIZE(line), L"proc=%lu|%lu|", pe.th32ProcessID, pe.th32ParentProcessID);
            put(line);
            field(pe.szExeFile);
            put(L"\n");
        } while (Process32NextW(snap, &pe));
    CloseHandle(snap);
    return 0;
}

static void value(HKEY key, const WCHAR *name)
{
    WCHAR data[1024];
    DWORD size = sizeof(data) - sizeof(WCHAR), type;
    if (RegQueryValueExW(key, name, NULL, &type, (BYTE *)data, &size) || (type != REG_SZ && type != REG_EXPAND_SZ))
        size = 0;
    data[size / sizeof(WCHAR)] = 0;
    field(data);
}

static void uninstall_keys(HKEY root, const WCHAR *rootname, REGSAM view, const WCHAR *viewname)
{
    HKEY key, sub;
    WCHAR name[256];
    DWORD i, len;
    if (RegOpenKeyExW(root, L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall", 0, KEY_READ | view, &key))
        return;
    for (i = 0; len = ARRAYSIZE(name), !RegEnumKeyExW(key, i, name, &len, NULL, NULL, NULL, NULL); i++)
    {
        if (RegOpenKeyExW(key, name, 0, KEY_READ | view, &sub)) continue;
        put(L"uninstall="); put(rootname); put(L"|"); put(viewname); put(L"|");
        field(name); put(L"|");
        value(sub, L"DisplayName"); put(L"|");
        value(sub, L"UninstallString"); put(L"|");
        value(sub, L"QuietUninstallString"); put(L"\n");
        RegCloseKey(sub);
    }
    RegCloseKey(key);
}

static BOOL CALLBACK print_child(HWND hwnd, LPARAM lp)
{
    WCHAR cls[128], text[256], line[512];
    (void)lp;
    GetClassNameW(hwnd, cls, ARRAYSIZE(cls));
    InternalGetWindowText(hwnd, text, ARRAYSIZE(text));
    swprintf(line, ARRAYSIZE(line), L"child=%p|%ls|%ls|%ls\n", (void *)hwnd, cls, text,
             IsWindowVisible(hwnd) ? L"visible" : L"hidden");
    put(line);
    return TRUE;
}

struct click { const WCHAR *text; HWND found; };

static BOOL CALLBACK find_button(HWND hwnd, LPARAM lp)
{
    struct click *c = (struct click *)lp;
    WCHAR cls[32], text[256], plain[256];
    int i, j;

    GetClassNameW(hwnd, cls, ARRAYSIZE(cls));
    if (lstrcmpiW(cls, L"Button") || !IsWindowVisible(hwnd)) return TRUE;
    InternalGetWindowText(hwnd, text, ARRAYSIZE(text));
    for (i = j = 0; text[i]; i++) if (text[i] != L'&') plain[j++] = text[i];
    plain[j] = 0;
    if (!wcsncmp(plain, c->text, wcslen(c->text))) { c->found = hwnd; return FALSE; }
    return TRUE;
}

static int click(HWND top, const WCHAR *text)
{
    struct click c = { text, NULL };
    EnumChildWindows(top, find_button, (LPARAM)&c);
    if (!c.found) { fputs("no such button\n", stderr); return 1; }
    fprintf(stdout, "clicked %p\n", (void *)c.found);
    PostMessageW(c.found, BM_CLICK, 0, 0);
    return 0;
}

int wmain(int argc, WCHAR **argv)
{
    int ret = 2;

    if (argc >= 2 && !wcscmp(argv[1], L"windows"))
        ret = !EnumWindows(list_window, 0);
    else if (argc >= 2 && !wcscmp(argv[1], L"procs"))
        ret = procs();
    else if (argc >= 3 && !wcscmp(argv[1], L"close"))
        ret = !PostMessageW((HWND)(ULONG_PTR)wcstoull(argv[2], NULL, 16), WM_CLOSE, 0, 0);
    else if (argc >= 3 && !wcscmp(argv[1], L"children"))
        ret = !EnumChildWindows((HWND)(ULONG_PTR)wcstoull(argv[2], NULL, 16), print_child, 0);
    else if (argc >= 4 && !wcscmp(argv[1], L"click"))
        ret = click((HWND)(ULONG_PTR)wcstoull(argv[2], NULL, 16), argv[3]);
    else if (argc >= 2 && !wcscmp(argv[1], L"uninstall"))
    {
        uninstall_keys(HKEY_LOCAL_MACHINE, L"HKLM", KEY_WOW64_64KEY, L"64");
        uninstall_keys(HKEY_LOCAL_MACHINE, L"HKLM", KEY_WOW64_32KEY, L"32");
        uninstall_keys(HKEY_CURRENT_USER, L"HKCU", 0, L"-");
        ret = 0;
    }
    else
        fputs("usage: compat-probe windows | procs | close HWND | children HWND | click HWND TEXT | uninstall\n", stderr);
    fflush(stdout);
    return ret;
}
