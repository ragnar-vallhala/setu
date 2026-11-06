/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * setu-files-probe: drives setu-files.exe for tools/files-smoke.sh.
 *
 *   send ARG...                 hand ARGs to the running setu-files window
 *                               (WM_COPYDATA; see apply_commands() there)
 *   key SPEC...                 press keys with SendInput: F2, Return, Delete,
 *                               Escape, Tab, Left, Up, Back, a-z, 0-9, with
 *                               ctrl+ / shift+ / alt+ prefixes
 *   type TEXT                   type TEXT (KEYEVENTF_UNICODE)
 *   click X Y | dblclick X Y | rclick X Y
 *   drag X1 Y1 X2 Y2            press at 1, move in steps, release at 2
 *   down X Y | move X Y | up X Y   the parts of a drag
 *   find TEXT [SECONDS]         wait for a visible top-level window whose
 *                               title contains TEXT; print class|title
 *   close TEXT                  WM_CLOSE every top-level window whose title
 *                               contains TEXT
 *   setfg TEXT                  SetForegroundWindow on that window
 *   recycle                     print SHQueryRecycleBinW's item count
 *   assoc EXT                   print AssocQueryStringW(ASSOCSTR_EXECUTABLE)
 *
 * Exit code 0 on success.
 */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <stdio.h>
#include <wchar.h>
#include <stdlib.h>

#define COPYDATA_MAGIC 0x53454c46 /* "FLES" */
#define CMD_SEP        L'\x1f'

static int send_args(int argc, WCHAR **argv)
{
    HWND w = FindWindowW(L"SetuFiles", NULL);
    COPYDATASTRUCT cds;
    WCHAR buf[8192] = L"";
    DWORD_PTR result = 0;
    int i;

    if (!w) { wprintf(L"no setu-files window\n"); return 1; }
    for (i = 0; i < argc; i++)
    {
        if (i) { size_t l = wcslen(buf); buf[l] = CMD_SEP; buf[l + 1] = 0; }
        wcsncat(buf, argv[i], ARRAYSIZE(buf) - wcslen(buf) - 2);
    }
    cds.dwData = COPYDATA_MAGIC;
    cds.cbData = (DWORD)(wcslen(buf) * sizeof(WCHAR));
    cds.lpData = buf;
    AllowSetForegroundWindow(ASFW_ANY);
    if (!SendMessageTimeoutW(w, WM_COPYDATA, 0, (LPARAM)&cds, SMTO_ABORTIFHUNG, 10000, &result) || !result)
    {
        wprintf(L"send failed\n");
        return 2;
    }
    return 0;
}

static void key_event(WORD vk, BOOL up)
{
    INPUT in = { INPUT_KEYBOARD };
    in.ki.wVk = vk;
    in.ki.wScan = (WORD)MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    in.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
    if (vk == VK_LEFT || vk == VK_RIGHT || vk == VK_UP || vk == VK_DOWN || vk == VK_DELETE || vk == VK_HOME ||
        vk == VK_END || vk == VK_APPS)
        in.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    SendInput(1, &in, sizeof(in));
    Sleep(20);
}

static WORD vk_of(const WCHAR *name)
{
    static const struct { const WCHAR *n; WORD vk; } names[] =
    {
        { L"Return", VK_RETURN }, { L"Enter", VK_RETURN }, { L"Escape", VK_ESCAPE }, { L"Tab", VK_TAB },
        { L"Delete", VK_DELETE }, { L"Back", VK_BACK }, { L"Left", VK_LEFT }, { L"Right", VK_RIGHT },
        { L"Up", VK_UP }, { L"Down", VK_DOWN }, { L"Home", VK_HOME }, { L"End", VK_END }, { L"Space", VK_SPACE },
        { L"Apps", VK_APPS },
    };
    int i;
    for (i = 0; i < (int)ARRAYSIZE(names); i++)
        if (!lstrcmpiW(names[i].n, name)) return names[i].vk;
    if ((name[0] == L'F' || name[0] == L'f') && name[1] >= L'1' && name[1] <= L'9')
        return (WORD)(VK_F1 + _wtoi(name + 1) - 1);
    if (!name[1] && iswalnum(name[0])) return (WORD)towupper(name[0]);
    return 0;
}

static int press(const WCHAR *spec)
{
    BOOL ctrl = FALSE, shift = FALSE, alt = FALSE;
    WORD vk;
    for (;;)
    {
        if (!_wcsnicmp(spec, L"ctrl+", 5)) { ctrl = TRUE; spec += 5; }
        else if (!_wcsnicmp(spec, L"shift+", 6)) { shift = TRUE; spec += 6; }
        else if (!_wcsnicmp(spec, L"alt+", 4)) { alt = TRUE; spec += 4; }
        else break;
    }
    if (!(vk = vk_of(spec))) { wprintf(L"unknown key %ls\n", spec); return 1; }
    if (ctrl) key_event(VK_CONTROL, FALSE);
    if (shift) key_event(VK_SHIFT, FALSE);
    if (alt) key_event(VK_MENU, FALSE);
    key_event(vk, FALSE);
    key_event(vk, TRUE);
    if (alt) key_event(VK_MENU, TRUE);
    if (shift) key_event(VK_SHIFT, TRUE);
    if (ctrl) key_event(VK_CONTROL, TRUE);
    return 0;
}

static void type_text(const WCHAR *s)
{
    for (; *s; s++)
    {
        INPUT in[2] = { { INPUT_KEYBOARD }, { INPUT_KEYBOARD } };
        in[0].ki.wScan = *s;
        in[0].ki.dwFlags = KEYEVENTF_UNICODE;
        in[1].ki.wScan = *s;
        in[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
        SendInput(2, in, sizeof(INPUT));
        Sleep(15);
    }
}

/* Move the pointer and wait until the system agrees it's there: under
 * Xvfb a pending X motion event can put it back where it was. */
static void mouse_to(int x, int y)
{
    INPUT in = { INPUT_MOUSE };
    int w = GetSystemMetrics(SM_CXSCREEN), h = GetSystemMetrics(SM_CYSCREEN), i;
    POINT p;
    in.mi.dx = MulDiv(x, 65535, w - 1);
    in.mi.dy = MulDiv(y, 65535, h - 1);
    in.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
    for (i = 0; i < 20; i++)
    {
        SendInput(1, &in, sizeof(in));
        Sleep(30);
        if (GetCursorPos(&p) && abs(p.x - x) <= 1 && abs(p.y - y) <= 1) break;
    }
}

static void button(DWORD flag)
{
    INPUT in = { INPUT_MOUSE };
    in.mi.dwFlags = flag;
    SendInput(1, &in, sizeof(in));
}

struct find { const WCHAR *text; HWND found; BOOL close; int closed; };

static BOOL CALLBACK find_proc(HWND hwnd, LPARAM lp)
{
    struct find *f = (struct find *)lp;
    WCHAR title[256] = L"";
    if (!IsWindowVisible(hwnd)) return TRUE;
    GetWindowTextW(hwnd, title, ARRAYSIZE(title));
    if (!StrStrIW(title, f->text)) return TRUE;
    if (f->close) { PostMessageW(hwnd, WM_CLOSE, 0, 0); f->closed++; return TRUE; }
    f->found = hwnd;
    return FALSE;
}

int wmain(int argc, WCHAR **argv)
{
    if (argc < 2) return 2;
    if (!wcscmp(argv[1], L"send")) return send_args(argc - 2, argv + 2);
    if (!wcscmp(argv[1], L"key"))
    {
        int i;
        for (i = 2; i < argc; i++) if (press(argv[i])) return 1;
        return 0;
    }
    if (!wcscmp(argv[1], L"type") && argc > 2) { type_text(argv[2]); return 0; }
    if ((!wcscmp(argv[1], L"click") || !wcscmp(argv[1], L"dblclick") || !wcscmp(argv[1], L"rclick")) && argc > 3)
    {
        BOOL right = argv[1][0] == L'r';
        int n = argv[1][0] == L'd' ? 2 : 1;
        mouse_to(_wtoi(argv[2]), _wtoi(argv[3]));
        Sleep(50);
        while (n--)
        {
            button(right ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_LEFTDOWN);
            button(right ? MOUSEEVENTF_RIGHTUP : MOUSEEVENTF_LEFTUP);
            Sleep(30);
        }
        return 0;
    }
    if ((!wcscmp(argv[1], L"down") || !wcscmp(argv[1], L"move") || !wcscmp(argv[1], L"up")) && argc > 3)
    {
        mouse_to(_wtoi(argv[2]), _wtoi(argv[3]));
        Sleep(50);
        if (argv[1][0] == L'd') button(MOUSEEVENTF_LEFTDOWN);
        if (argv[1][0] == L'u') button(MOUSEEVENTF_LEFTUP);
        return 0;
    }
    if (!wcscmp(argv[1], L"drag") && argc > 5)
    {
        int x1 = _wtoi(argv[2]), y1 = _wtoi(argv[3]), x2 = _wtoi(argv[4]), y2 = _wtoi(argv[5]), i;
        mouse_to(x1, y1);
        Sleep(100);
        button(MOUSEEVENTF_LEFTDOWN);
        Sleep(100);
        for (i = 1; i <= 20; i++)
        {
            mouse_to(x1 + (x2 - x1) * i / 20, y1 + (y2 - y1) * i / 20);
            Sleep(30);
        }
        Sleep(200);
        button(MOUSEEVENTF_LEFTUP);
        Sleep(100);
        return 0;
    }
    if ((!wcscmp(argv[1], L"find") || !wcscmp(argv[1], L"setfg")) && argc > 2)
    {
        struct find f = { argv[2] };
        int t = argc > 3 ? _wtoi(argv[3]) : 10, i;
        for (i = 0; i < t * 10; i++)
        {
            f.found = NULL;
            EnumWindows(find_proc, (LPARAM)&f);
            if (f.found) break;
            Sleep(100);
        }
        if (!f.found) { wprintf(L"not found: %ls\n", argv[2]); return 1; }
        {
            WCHAR cls[64], title[256];
            GetClassNameW(f.found, cls, ARRAYSIZE(cls));
            GetWindowTextW(f.found, title, ARRAYSIZE(title));
            wprintf(L"%ls|%ls\n", cls, title);
        }
        if (argv[1][0] == L's') SetForegroundWindow(f.found);
        return 0;
    }
    if (!wcscmp(argv[1], L"close") && argc > 2)
    {
        struct find f = { argv[2], NULL, TRUE };
        EnumWindows(find_proc, (LPARAM)&f);
        wprintf(L"closed %d\n", f.closed);
        return 0;
    }
    if (!wcscmp(argv[1], L"recycle"))
    {
        SHQUERYRBINFO qi = { sizeof(qi) };
        HRESULT hr = SHQueryRecycleBinW(NULL, &qi);
        if (FAILED(hr)) { wprintf(L"failed %08lx\n", hr); return 1; }
        wprintf(L"%I64d\n", qi.i64NumItems);
        return 0;
    }
    if (!wcscmp(argv[1], L"assoc") && argc > 2)
    {
        WCHAR out[MAX_PATH];
        DWORD n = ARRAYSIZE(out);
        HRESULT hr = AssocQueryStringW(0, ASSOCSTR_EXECUTABLE, argv[2], L"open", out, &n);
        if (FAILED(hr)) { wprintf(L"none %08lx\n", hr); return 1; }
        wprintf(L"%ls\n", out);
        return 0;
    }
    wprintf(L"unknown command\n");
    return 2;
}
