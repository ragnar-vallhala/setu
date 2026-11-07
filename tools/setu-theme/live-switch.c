/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Helper for tools/setu-theme/live-switch-test.sh: checks that switching
 * the theme pack (docs/THEMES.md) reaches programs that are already
 * running, without restarting them.
 *
 *   live-switch app STATEFILE
 *       A window with a menu bar and themed controls (comctl32 v6). It
 *       writes the screen rectangles of its caption buttons and controls
 *       to STATEFILE, and rewrites them with "seq=N" after every
 *       WM_THEMECHANGED / WM_SYSCOLORCHANGE / WM_SETTINGCHANGE it sees.
 *   live-switch apply THEME_INI MSSTYLES COLORS_INI WALLPAPER_BMP
 *       Applies a pack the way setu-theme does (steps 2-6 of "Applying a
 *       pack"): the [Colors]/[Metrics] tokens to HKCU\Software\Setu\Theme,
 *       the visual style to the ThemeManager values, SetSysColors from the
 *       .theme's [Control Panel\Colors], WM_THEMECHANGED to every window,
 *       SPI_SETDESKWALLPAPER, then WM_SETTINGCHANGE "SetuTheme".
 *   live-switch hover X Y
 *       Moves the pointer (to hot-track a caption button).
 */
#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <wchar.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static const wchar_t *state_path;
static HWND main_hwnd, button_hwnd, check_hwnd, progress_hwnd, edit_hwnd;
static int seq;

static void write_state(void)
{
    FILE *f = _wfopen(state_path, L"w");
    RECT r, c;

    if (!f) return;
    fprintf(f, "seq=%d\n", seq);
    GetWindowRect(main_hwnd, &r);
    fprintf(f, "window=%ld %ld %ld %ld\n", r.left, r.top, r.right, r.bottom);
    {
        /* the caption row and its close button, from the frame metrics */
        int frame = GetSystemMetrics(SM_CYFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
        int cap = GetSystemMetrics(SM_CYCAPTION), bw = GetSystemMetrics(SM_CXSIZE);
        fprintf(f, "titlebar=%ld %ld %ld %ld\n", r.left + frame, r.top + frame, r.right - frame,
                r.top + frame + cap - 1);
        fprintf(f, "close=%ld %ld %ld %ld\n", r.right - frame - bw, r.top + frame, r.right - frame,
                r.top + frame + cap - 1);
    }
    GetClientRect(main_hwnd, &c);
    MapWindowPoints(main_hwnd, NULL, (POINT *)&c, 2);
    fprintf(f, "client=%ld %ld %ld %ld\n", c.left, c.top, c.right, c.bottom);
    GetWindowRect(button_hwnd, &r);
    fprintf(f, "button=%ld %ld %ld %ld\n", r.left, r.top, r.right, r.bottom);
    GetWindowRect(check_hwnd, &r);
    fprintf(f, "check=%ld %ld %ld %ld\n", r.left, r.top, r.right, r.bottom);
    GetWindowRect(progress_hwnd, &r);
    fprintf(f, "progress=%ld %ld %ld %ld\n", r.left, r.top, r.right, r.bottom);
    GetWindowRect(edit_hwnd, &r);
    fprintf(f, "edit=%ld %ld %ld %ld\n", r.left, r.top, r.right, r.bottom);
    fprintf(f, "activecaption=%06lx\n", GetSysColor(COLOR_ACTIVECAPTION));
    fprintf(f, "hilight=%06lx\n", GetSysColor(COLOR_HIGHLIGHT));
    fclose(f);
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_THEMECHANGED:
        seq++;
        write_state();
        break;
    case WM_SYSCOLORCHANGE:
    case WM_SETTINGCHANGE:
        /* a top-level window passes these on to its common controls */
        {
            HWND child;
            for (child = GetWindow(hwnd, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT))
                SendMessageW(child, msg, wp, lp);
        }
        seq++;
        write_state();
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static int run_app(void)
{
    INITCOMMONCONTROLSEX icc = {sizeof icc, ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS};
    WNDCLASSW wc = {0};
    HMENU menu = CreateMenu(), file = CreatePopupMenu();
    MSG m;

    InitCommonControlsEx(&icc);
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"SetuLiveSwitch";
    RegisterClassW(&wc);
    AppendMenuW(file, MF_STRING, 1, L"&Open");
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)file, L"&File");
    AppendMenuW(menu, MF_STRING, 2, L"&Edit");
    AppendMenuW(menu, MF_STRING, 3, L"&Help");

    main_hwnd = CreateWindowExW(0, wc.lpszClassName, L"Live switch", WS_OVERLAPPEDWINDOW, 60, 60, 460, 300, NULL,
                                menu, wc.hInstance, NULL);
    button_hwnd = CreateWindowW(L"BUTTON", L"Button", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 20, 20, 120, 32,
                                main_hwnd, (HMENU)10, wc.hInstance, NULL);
    check_hwnd = CreateWindowW(L"BUTTON", L"Check", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 20, 70, 120, 24,
                               main_hwnd, (HMENU)11, wc.hInstance, NULL);
    SendMessageW(check_hwnd, BM_SETCHECK, BST_CHECKED, 0);
    progress_hwnd = CreateWindowW(PROGRESS_CLASSW, NULL, WS_CHILD | WS_VISIBLE, 20, 110, 300, 16, main_hwnd,
                                  (HMENU)12, wc.hInstance, NULL);
    SendMessageW(progress_hwnd, PBM_SETPOS, 60, 0);
    edit_hwnd = CreateWindowExW(0, L"EDIT", L"Selected text", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NOHIDESEL, 20, 140, 200, 26,
                                main_hwnd, (HMENU)13, wc.hInstance, NULL);
    ShowWindow(main_hwnd, SW_SHOW);
    UpdateWindow(main_hwnd);
    SetForegroundWindow(main_hwnd);
    /* the selection shows without focus (ES_NOHIDESEL); keep the
     * blinking caret out of the screenshots */
    SendMessageW(edit_hwnd, EM_SETSEL, 0, -1);
    SetFocus(button_hwnd);
    write_state();

    while (GetMessageW(&m, NULL, 0, 0))
    {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    return 0;
}

/* --- apply --------------------------------------------------------------- */

static void set_sz(HKEY root, const wchar_t *path, const wchar_t *name, const wchar_t *value)
{
    HKEY k;
    if (RegCreateKeyExW(root, path, 0, NULL, 0, KEY_WRITE, NULL, &k, NULL)) return;
    RegSetValueExW(k, name, 0, REG_SZ, (const BYTE *)value, (DWORD)(wcslen(value) + 1) * sizeof(wchar_t));
    RegCloseKey(k);
}

/* Copies a [section] of an ini file as "name=value" pairs into a key. */
static void tokens_to_registry(const wchar_t *ini, const wchar_t *section, const wchar_t *key, BOOL dword)
{
    static wchar_t buf[32768];
    HKEY k;

    RegDeleteTreeW(HKEY_CURRENT_USER, key);
    if (RegCreateKeyExW(HKEY_CURRENT_USER, key, 0, NULL, 0, KEY_WRITE, NULL, &k, NULL)) return;
    DWORD n = GetPrivateProfileSectionW(section, buf, ARRAY_SIZE(buf), ini);
    for (wchar_t *p = buf; n && *p; p += wcslen(p) + 1)
    {
        wchar_t *eq = wcschr(p, '='), *end;
        if (!eq) continue;
        *eq = 0;
        wchar_t *v = eq + 1;
        if ((end = wcschr(v, ';'))) *end = 0; /* inline comment */
        while (*v == ' ' || *v == '\t') v++;
        size_t len = wcslen(v);
        while (len && (v[len - 1] == ' ' || v[len - 1] == '\t')) v[--len] = 0;
        len = wcslen(p);
        while (len && (p[len - 1] == ' ' || p[len - 1] == '\t')) p[--len] = 0;
        if (dword)
        {
            DWORD d = wcstoul(v, NULL, 10);
            RegSetValueExW(k, p, 0, REG_DWORD, (const BYTE *)&d, sizeof d);
        }
        else
            RegSetValueExW(k, p, 0, REG_SZ, (const BYTE *)v, (DWORD)(wcslen(v) + 1) * sizeof(wchar_t));
    }
    RegCloseKey(k);
}

static const wchar_t *const color_names[] = {
    L"Scrollbar", L"Background", L"ActiveTitle", L"InactiveTitle", L"Menu", L"Window", L"WindowFrame",
    L"MenuText", L"WindowText", L"TitleText", L"ActiveBorder", L"InactiveBorder", L"AppWorkspace",
    L"Hilight", L"HilightText", L"ButtonFace", L"ButtonShadow", L"GrayText", L"ButtonText",
    L"InactiveTitleText", L"ButtonHilight", L"ButtonDkShadow", L"ButtonLight", L"InfoText", L"InfoWindow",
    L"ButtonAlternateFace", L"HotTrackingColor", L"GradientActiveTitle", L"GradientInactiveTitle",
    L"MenuHilight", L"MenuBar",
};

static void apply_sys_colors(const wchar_t *colors_ini)
{
    INT ids[ARRAY_SIZE(color_names)];
    COLORREF vals[ARRAY_SIZE(color_names)];
    int n = 0;

    for (int i = 0; i < (int)ARRAY_SIZE(color_names); i++)
    {
        wchar_t v[64];
        unsigned r, g, b;
        GetPrivateProfileStringW(L"Control Panel\\Colors", color_names[i], L"", v, ARRAY_SIZE(v), colors_ini);
        if (swscanf(v, L"%u %u %u", &r, &g, &b) != 3) continue;
        ids[n] = i;
        vals[n] = RGB(r, g, b);
        set_sz(HKEY_CURRENT_USER, L"Control Panel\\Colors", color_names[i], v);
        n++;
    }
    SetSysColors(n, ids, vals);
}

static BOOL CALLBACK send_theme_changed_child(HWND hwnd, LPARAM lp)
{
    DWORD_PTR res;
    (void)lp;
    SendMessageTimeoutW(hwnd, WM_THEMECHANGED, 0, 0, SMTO_ABORTIFHUNG, 2000, &res);
    return TRUE;
}

static BOOL CALLBACK send_theme_changed(HWND hwnd, LPARAM lp)
{
    DWORD_PTR res;
    SendMessageTimeoutW(hwnd, WM_THEMECHANGED, 0, 0, SMTO_ABORTIFHUNG, 2000, &res);
    EnumChildWindows(hwnd, send_theme_changed_child, lp);
    return TRUE;
}

static int run_apply(const wchar_t *theme_ini, const wchar_t *msstyles, const wchar_t *colors_ini,
                     const wchar_t *wallpaper)
{
    static const wchar_t tm[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\ThemeManager";
    DWORD_PTR res;

    tokens_to_registry(theme_ini, L"Colors", L"Software\\Setu\\Theme\\Colors", FALSE);
    tokens_to_registry(theme_ini, L"Metrics", L"Software\\Setu\\Theme\\Metrics", TRUE);
    set_sz(HKEY_CURRENT_USER, tm, L"DllName", msstyles);
    set_sz(HKEY_CURRENT_USER, tm, L"ColorName", L"NormalColor");
    set_sz(HKEY_CURRENT_USER, tm, L"SizeName", L"NormalSize");
    set_sz(HKEY_CURRENT_USER, tm, L"ThemeActive", L"1");
    apply_sys_colors(colors_ini);
    EnumWindows(send_theme_changed, 0);
    if (wallpaper[0] && wcscmp(wallpaper, L"-"))
    {
        set_sz(HKEY_CURRENT_USER, L"Control Panel\\Desktop", L"WallpaperStyle", L"10");
        SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, (void *)wallpaper, SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
    }
    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)L"SetuTheme", SMTO_ABORTIFHUNG, 2000, &res);
    return 0;
}

int wmain(int argc, wchar_t **argv)
{
    if (argc == 3 && !wcscmp(argv[1], L"app"))
    {
        state_path = argv[2];
        return run_app();
    }
    if (argc == 6 && !wcscmp(argv[1], L"apply")) return run_apply(argv[2], argv[3], argv[4], argv[5]);
    if (argc == 4 && !wcscmp(argv[1], L"hover"))
    {
        SetCursorPos(_wtoi(argv[2]), _wtoi(argv[3]));
        return 0;
    }
    fprintf(stderr, "usage: live-switch app STATE | apply THEME_INI MSSTYLES COLORS_INI WALLPAPER | hover X Y\n");
    return 2;
}
