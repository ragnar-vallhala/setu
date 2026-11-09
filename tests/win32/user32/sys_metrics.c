/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: the window-frame and control metrics an application can
 * query at 96 DPI: GetSystemMetrics for captions, caption buttons, frames,
 * borders, menus and scroll bars; SystemParametersInfo NONCLIENTMETRICS
 * (sizes and the caption, menu, status and message font faces), the icon
 * title font, flat menus and gradient captions; the stock DEFAULT_GUI_FONT;
 * and AdjustWindowRectEx for common frame styles.
 *
 * Probe cases at the end show how the frame and menu-check sizes are
 * derived: the WindowMetrics registry values, the *ForDpi variants at 96,
 * 120 and 144 DPI, the menu font's text metrics, and the derived metrics
 * after session-only SPI_SETNONCLIENTMETRICS changes (each restored).
 *
 * Setu draws its own window frames and control theme; this test keeps the
 * values apps see equal to Windows' while the drawing changes.
 * System colours are deliberately not recorded (Setu uses its own palette).
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getsystemmetrics
 *       https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-systemparametersinfow
 *       https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-nonclientmetricsw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-adjustwindowrectex
 *       https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-getstockobject
 *       https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getsystemmetricsfordpi
 *       https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-systemparametersinfofordpi
 *       https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-adjustwindowrectexfordpi
 */
#include "oracle.h"

#define DECL(f) static __typeof__(f) *p##f
#define LOAD(m, f) (*(FARPROC *)&p##f = GetProcAddress(m, #f))

DECL(GetSystemMetrics);
DECL(SystemParametersInfoW);
DECL(AdjustWindowRectEx);
DECL(GetDC);
DECL(ReleaseDC);
DECL(GetStockObject);
DECL(GetObjectW);
DECL(GetDeviceCaps);
DECL(CreateFontIndirectW);
DECL(SelectObject);
DECL(DeleteObject);
DECL(GetTextMetricsW);
static int (WINAPI *pGetSystemMetricsForDpi)(int, UINT);
static BOOL (WINAPI *pSystemParametersInfoForDpi)(UINT, UINT, void *, UINT, UINT);
static BOOL (WINAPI *pAdjustWindowRectExForDpi)(RECT *, DWORD, BOOL, DWORD, UINT);
static DPI_AWARENESS_CONTEXT (WINAPI *pSetThreadDpiAwarenessContext)(DPI_AWARENESS_CONTEXT);

static void metric_case(const char *name, int index)
{
    int v;
    ORACLE_CALL(v, pGetSystemMetrics(index));
    oracle_case(name);
    o_i64("value", v);
    oracle_case_end();
}

static void font_fields(const char *prefix, const LOGFONTW *lf)
{
    char key[64];
    snprintf(key, sizeof key, "%s_face", prefix);
    o_wstr(key, lf->lfFaceName);
    snprintf(key, sizeof key, "%s_height", prefix);
    o_i64(key, lf->lfHeight);
    snprintf(key, sizeof key, "%s_weight", prefix);
    o_i64(key, lf->lfWeight);
    snprintf(key, sizeof key, "%s_italic", prefix);
    o_u32(key, lf->lfItalic);
    snprintf(key, sizeof key, "%s_charset", prefix);
    o_u32(key, lf->lfCharSet);
}

static void adjust_case(const char *name, DWORD style, DWORD ex, BOOL menu)
{
    RECT r = { 100, 100, 300, 200 };
    BOOL ok;
    ORACLE_CALL(ok, pAdjustWindowRectEx(&r, style, menu, ex));
    oracle_case(name);
    o_bool("ret", ok);
    o_lasterr();
    o_i64("left", 100 - r.left);
    o_i64("top", 100 - r.top);
    o_i64("right", r.right - 300);
    o_i64("bottom", r.bottom - 200);
    oracle_case_end();
}

/* ---- probes: how the frame and menu-check sizes are derived ---------- */

static void font_tm_case(const char *name, const LOGFONTW *lf)
{
    HDC dc = pGetDC(NULL);
    HFONT f = pCreateFontIndirectW(lf);
    TEXTMETRICW tm;
    BOOL ok = FALSE;
    memset(&tm, 0, sizeof tm);
    if (dc && f)
    {
        HGDIOBJ old = pSelectObject(dc, f);
        ok = pGetTextMetricsW(dc, &tm);
        pSelectObject(dc, old);
    }
    if (f) pDeleteObject(f);
    if (dc) pReleaseDC(NULL, dc);
    oracle_case(name);
    o_bool("ret", ok);
    o_i64("height", tm.tmHeight);
    o_i64("ascent", tm.tmAscent);
    o_i64("descent", tm.tmDescent);
    o_i64("internal_leading", tm.tmInternalLeading);
    o_i64("external_leading", tm.tmExternalLeading);
    oracle_case_end();
}

static void reg_metric(const char *key, const wchar_t *value)
{
    wchar_t buf[64];
    DWORD size = sizeof buf - sizeof(wchar_t), type = 0;
    LONG rc = RegGetValueW(HKEY_CURRENT_USER, L"Control Panel\\Desktop\\WindowMetrics", value,
                           RRF_RT_ANY | RRF_NOEXPAND, &type, buf, &size);
    if (rc == ERROR_SUCCESS && type == REG_SZ) o_wstr(key, buf);
    else if (rc == ERROR_SUCCESS) o_str(key, "(not REG_SZ)");
    else o_str(key, NULL);
}

static void frame_state(void)
{
    NONCLIENTMETRICSW ncm;
    RECT r = { 100, 100, 300, 200 };
    memset(&ncm, 0, sizeof ncm);
    ncm.cbSize = sizeof ncm;
    pSystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
    pAdjustWindowRectEx(&r, WS_OVERLAPPEDWINDOW, FALSE, 0);
    o_i64("sizeframe", pGetSystemMetrics(SM_CXSIZEFRAME));
    o_i64("paddedborder", pGetSystemMetrics(SM_CXPADDEDBORDER));
    o_i64("menucheck", pGetSystemMetrics(SM_CXMENUCHECK));
    o_i64("cymenu", pGetSystemMetrics(SM_CYMENU));
    o_i64("ncm_border", ncm.iBorderWidth);
    o_i64("ncm_padded", ncm.iPaddedBorderWidth);
    o_i64("ncm_menu_height", ncm.iMenuHeight);
    o_i64("ncm_menu_font_height", ncm.lfMenuFont.lfHeight);
    o_i64("adjust_left", 100 - r.left);
}

/* The values a child copy of this program prints (see subsystem_case). */
static int child_main(void)
{
    NONCLIENTMETRICSW ncm;
    RECT r = { 100, 100, 300, 200 };
    memset(&ncm, 0, sizeof ncm);
    ncm.cbSize = sizeof ncm;
    pSystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
    pAdjustWindowRectEx(&r, WS_OVERLAPPEDWINDOW, FALSE, 0);
    printf("%d %d %d %d %d %d %d %d\n", pGetSystemMetrics(SM_CXSIZEFRAME),
           pGetSystemMetrics(SM_CXPADDEDBORDER), ncm.iBorderWidth, ncm.iPaddedBorderWidth,
           (int)(100 - r.left), pGetSystemMetrics(SM_CXMENUCHECK),
           pGetSystemMetrics(SM_CXMIN), pGetSystemMetrics(SM_CYMIN));
    fflush(stdout);
    return 0;
}

/* Run a copy of this program whose PE header says it was linked for
 * subsystem version major.minor and OS version os_major.os_minor (the
 * mingw defaults are 5.2 and 4.0), and record what it sees. Only our own
 * executable is copied and patched. */
static void subsystem_case(const char *name, WORD major, WORD minor, WORD os_major, WORD os_minor)
{
    wchar_t self[MAX_PATH], dir[MAX_PATH], copy[MAX_PATH], cmd[MAX_PATH + 16];
    char out[256];
    DWORD got = 0, n;
    int v[8] = { -1, -1, -1, -1, -1, -1, -1, -1 }, fields = 0;
    BOOL patched = FALSE, ran = FALSE;
    HANDLE f, rd = NULL, wr = NULL;
    SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;

    GetModuleFileNameW(NULL, self, MAX_PATH);
    GetTempPathW(MAX_PATH, dir);
    swprintf(copy, MAX_PATH, L"%lssetu_sysmetrics_%u_%u_%u_%u.exe", dir, major, minor, os_major, os_minor);
    if (CopyFileW(self, copy, FALSE))
    {
        f = CreateFileW(copy, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (f != INVALID_HANDLE_VALUE)
        {
            IMAGE_DOS_HEADER dos;
            IMAGE_NT_HEADERS nt;
            if (ReadFile(f, &dos, sizeof dos, &n, NULL) && n == sizeof dos &&
                SetFilePointer(f, dos.e_lfanew, NULL, FILE_BEGIN) != INVALID_SET_FILE_POINTER &&
                ReadFile(f, &nt, sizeof nt, &n, NULL) && n == sizeof nt &&
                nt.Signature == IMAGE_NT_SIGNATURE)
            {
                nt.OptionalHeader.MajorSubsystemVersion = major;
                nt.OptionalHeader.MinorSubsystemVersion = minor;
                nt.OptionalHeader.MajorOperatingSystemVersion = os_major;
                nt.OptionalHeader.MinorOperatingSystemVersion = os_minor;
                patched = SetFilePointer(f, dos.e_lfanew, NULL, FILE_BEGIN) != INVALID_SET_FILE_POINTER &&
                          WriteFile(f, &nt, sizeof nt, &n, NULL) && n == sizeof nt;
            }
            CloseHandle(f);
        }
    }
    if (patched && CreatePipe(&rd, &wr, &sa, 0))
    {
        SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
        memset(&si, 0, sizeof si);
        si.cb = sizeof si;
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = wr;
        si.hStdError = wr;
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        swprintf(cmd, MAX_PATH + 16, L"\"%ls\" child", copy);
        if (CreateProcessW(copy, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi))
        {
            CloseHandle(wr);
            wr = NULL;
            while (got < sizeof out - 1 && ReadFile(rd, out + got, sizeof out - 1 - got, &n, NULL) && n)
                got += n;
            WaitForSingleObject(pi.hProcess, 10000);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            ran = TRUE;
        }
    }
    if (wr) CloseHandle(wr);
    if (rd) CloseHandle(rd);
    out[got] = 0;
    fields = sscanf(out, "%d %d %d %d %d %d %d %d", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5],
                    &v[6], &v[7]);
    DeleteFileW(copy);

    oracle_case(name);
    o_bool("patched", patched);
    o_bool("ran", ran);
    o_i64("fields", fields);
    o_i64("sizeframe", v[0]);
    o_i64("paddedborder", v[1]);
    o_i64("ncm_border", v[2]);
    o_i64("ncm_padded", v[3]);
    o_i64("adjust_left", v[4]);
    o_i64("menucheck", v[5]);
    o_i64("cxmin", v[6]);
    o_i64("cymin", v[7]);
    oracle_case_end();
}

static void probes(void)
{
    {
        UINT border = 0;
        BOOL ok = pSystemParametersInfoW(SPI_GETBORDER, 0, &border, 0);
        oracle_case("probe_metrics");
        o_i64("cxframe", pGetSystemMetrics(SM_CXFRAME));
        o_i64("cyframe", pGetSystemMetrics(SM_CYFRAME));
        o_i64("cxdlgframe", pGetSystemMetrics(SM_CXDLGFRAME));
        o_i64("cymenucheck", pGetSystemMetrics(SM_CYMENUCHECK));
        o_bool("spi_getborder_ret", ok);
        o_u32("spi_getborder", border);
        oracle_case_end();
    }

    oracle_case("registry_window_metrics");
    reg_metric("border_width", L"BorderWidth");
    reg_metric("padded_border_width", L"PaddedBorderWidth");
    reg_metric("menu_height", L"MenuHeight");
    reg_metric("menu_width", L"MenuWidth");
    reg_metric("caption_height", L"CaptionHeight");
    oracle_case_end();

    {
        /* the pre-Vista structure size, without iPaddedBorderWidth */
        NONCLIENTMETRICSW ncm;
        BOOL ok;
        memset(&ncm, 0, sizeof ncm);
        ncm.cbSize = FIELD_OFFSET(NONCLIENTMETRICSW, iPaddedBorderWidth);
        ok = pSystemParametersInfoW(SPI_GETNONCLIENTMETRICS, ncm.cbSize, &ncm, 0);
        oracle_case("ncm_old_size");
        o_bool("ret", ok);
        o_i64("border", ncm.iBorderWidth);
        o_i64("menu_height", ncm.iMenuHeight);
        oracle_case_end();
    }

    adjust_case("adjust_thickframe", WS_POPUP | WS_THICKFRAME, 0, FALSE);
    adjust_case("adjust_dlgframe", WS_POPUP | WS_DLGFRAME, 0, FALSE);
    adjust_case("adjust_thickframe_caption", WS_CAPTION | WS_THICKFRAME, 0, FALSE);

    {
        NONCLIENTMETRICSW ncm;
        LOGFONTW lf;
        memset(&ncm, 0, sizeof ncm);
        ncm.cbSize = sizeof ncm;
        pSystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
        font_tm_case("menu_font_metrics", &ncm.lfMenuFont);
        lf = ncm.lfMenuFont;
        wcscpy(lf.lfFaceName, L"Tahoma");
        lf.lfHeight = -11;
        font_tm_case("tahoma_11_metrics", &lf);
        lf.lfHeight = -12;
        font_tm_case("tahoma_12_metrics", &lf);
    }

    if (pGetSystemMetricsForDpi && pSystemParametersInfoForDpi && pAdjustWindowRectExForDpi)
    {
        static const UINT dpis[] = { 96, 120, 144 };
        unsigned i;
        for (i = 0; i < sizeof dpis / sizeof dpis[0]; i++)
        {
            UINT dpi = dpis[i];
            NONCLIENTMETRICSW ncm;
            RECT r = { 100, 100, 300, 200 };
            BOOL ok, ok2;
            char name[32];
            memset(&ncm, 0, sizeof ncm);
            ncm.cbSize = sizeof ncm;
            ok = pSystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0, dpi);
            ok2 = pAdjustWindowRectExForDpi(&r, WS_OVERLAPPEDWINDOW, FALSE, 0, dpi);
            snprintf(name, sizeof name, "for_dpi_%u", dpi);
            oracle_case(name);
            o_i64("sizeframe", pGetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi));
            o_i64("cysizeframe", pGetSystemMetricsForDpi(SM_CYSIZEFRAME, dpi));
            o_i64("paddedborder", pGetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi));
            o_i64("border", pGetSystemMetricsForDpi(SM_CXBORDER, dpi));
            o_i64("dlgframe", pGetSystemMetricsForDpi(SM_CXDLGFRAME, dpi));
            o_i64("edge", pGetSystemMetricsForDpi(SM_CXEDGE, dpi));
            o_i64("menucheck", pGetSystemMetricsForDpi(SM_CXMENUCHECK, dpi));
            o_i64("cymenucheck", pGetSystemMetricsForDpi(SM_CYMENUCHECK, dpi));
            o_i64("cymenu", pGetSystemMetricsForDpi(SM_CYMENU, dpi));
            o_i64("cymenusize", pGetSystemMetricsForDpi(SM_CYMENUSIZE, dpi));
            o_bool("ncm_ret", ok);
            o_i64("ncm_border", ncm.iBorderWidth);
            o_i64("ncm_padded", ncm.iPaddedBorderWidth);
            o_i64("ncm_menu_height", ncm.iMenuHeight);
            o_i64("ncm_menu_font_height", ncm.lfMenuFont.lfHeight);
            o_bool("adjust_ret", ok2);
            o_i64("adjust_left", 100 - r.left);
            o_i64("adjust_top", 100 - r.top);
            o_i64("adjust_bottom", r.bottom - 200);
            oracle_case_end();
        }
    }

    {
        /* Thread DPI awareness: does the plain API stop folding the padded
         * border into the frame for a DPI-aware thread? */
        static const struct { const char *name; DPI_AWARENESS_CONTEXT ctx; } aw[] = {
            { "aware_system", DPI_AWARENESS_CONTEXT_SYSTEM_AWARE },
            { "aware_per_monitor_v2", DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 },
        };
        unsigned i;
        for (i = 0; pSetThreadDpiAwarenessContext && i < sizeof aw / sizeof aw[0]; i++)
        {
            DPI_AWARENESS_CONTEXT old = pSetThreadDpiAwarenessContext(aw[i].ctx);
            oracle_case(aw[i].name);
            o_bool("set", old != NULL);
            frame_state();
            oracle_case_end();
            if (old) pSetThreadDpiAwarenessContext(old);
        }
    }

    {
        LOGFONTW lf;
        NONCLIENTMETRICSW ncm;
        memset(&ncm, 0, sizeof ncm);
        ncm.cbSize = sizeof ncm;
        pSystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
        lf = ncm.lfMenuFont;
        lf.lfHeight = -15;  /* the menu font at 120 DPI */
        font_tm_case("menu_font_m15_metrics", &lf);
        lf.lfHeight = -18;  /* the menu font at 144 DPI */
        font_tm_case("menu_font_m18_metrics", &lf);
    }

    subsystem_case("subsystem_5_2", 5, 2, 4, 0);
    subsystem_case("subsystem_6_0", 6, 0, 6, 0);
    subsystem_case("subsystem_6_0_os_4_0", 6, 0, 4, 0);
    subsystem_case("subsystem_5_2_os_6_0", 5, 2, 6, 0);
    subsystem_case("subsystem_6_2", 6, 2, 6, 2);

    oracle_case("after_probes");
    frame_state();
    oracle_case_end();
}

int main(int argc, char **argv)
{
    int child = argc > 1 && !strcmp(argv[1], "child");
    if (!child) oracle_begin("user32/sys_metrics");
    HMODULE u32, g32;
    ORACLE_CALL(u32, LoadLibraryW(L"user32.dll"));
    g32 = LoadLibraryW(L"gdi32.dll");
    if (child)
    {
        if (!u32 || !g32) return 1;
    }
    else
    {
        oracle_case("load_user32");
        o_handle("ret", u32);
        o_bool("gdi32", g32 != NULL);
        oracle_case_end();
        if (!u32 || !g32) return oracle_end();
    }
    LOAD(u32, GetSystemMetrics);
    LOAD(u32, SystemParametersInfoW);
    LOAD(u32, AdjustWindowRectEx);
    LOAD(u32, GetDC);
    LOAD(u32, ReleaseDC);
    LOAD(g32, GetStockObject);
    LOAD(g32, GetObjectW);
    LOAD(g32, GetDeviceCaps);
    LOAD(g32, CreateFontIndirectW);
    LOAD(g32, SelectObject);
    LOAD(g32, DeleteObject);
    LOAD(g32, GetTextMetricsW);
    *(FARPROC *)&pGetSystemMetricsForDpi = GetProcAddress(u32, "GetSystemMetricsForDpi");
    *(FARPROC *)&pSystemParametersInfoForDpi = GetProcAddress(u32, "SystemParametersInfoForDpi");
    *(FARPROC *)&pAdjustWindowRectExForDpi = GetProcAddress(u32, "AdjustWindowRectExForDpi");
    *(FARPROC *)&pSetThreadDpiAwarenessContext = GetProcAddress(u32, "SetThreadDpiAwarenessContext");
    if (child) return child_main();

    /* Everything below is for the screen DPI; record it so a golden from a
     * scaled session is recognisable. */
    {
        HDC dc = pGetDC(NULL);
        oracle_case("screen_dpi");
        o_i64("logpixelsy", dc ? pGetDeviceCaps(dc, LOGPIXELSY) : -1);
        oracle_case_end();
        if (dc) pReleaseDC(NULL, dc);
    }

    metric_case("cycaption", SM_CYCAPTION);
    metric_case("cxsize", SM_CXSIZE);
    metric_case("cysize", SM_CYSIZE);
    metric_case("cysmcaption", SM_CYSMCAPTION);
    metric_case("cxsmsize", SM_CXSMSIZE);
    metric_case("cysmsize", SM_CYSMSIZE);
    metric_case("cxborder", SM_CXBORDER);
    metric_case("cyborder", SM_CYBORDER);
    metric_case("cxedge", SM_CXEDGE);
    metric_case("cyedge", SM_CYEDGE);
    metric_case("cxfixedframe", SM_CXFIXEDFRAME);
    metric_case("cyfixedframe", SM_CYFIXEDFRAME);
    metric_case("cxsizeframe", SM_CXSIZEFRAME);
    metric_case("cysizeframe", SM_CYSIZEFRAME);
    metric_case("cxpaddedborder", SM_CXPADDEDBORDER);
    metric_case("cymenu", SM_CYMENU);
    metric_case("cxmenusize", SM_CXMENUSIZE);
    metric_case("cymenusize", SM_CYMENUSIZE);
    metric_case("cxmenucheck", SM_CXMENUCHECK);
    metric_case("cxvscroll", SM_CXVSCROLL);
    metric_case("cyhscroll", SM_CYHSCROLL);
    metric_case("cyvthumb", SM_CYVTHUMB);
    metric_case("cxhthumb", SM_CXHTHUMB);
    metric_case("cxicon", SM_CXICON);
    metric_case("cxsmicon", SM_CXSMICON);
    metric_case("cxmintrack", SM_CXMINTRACK);
    metric_case("cymintrack", SM_CYMINTRACK);
    metric_case("cxmin", SM_CXMIN);
    metric_case("cymin", SM_CYMIN);
    metric_case("cxfocusborder", SM_CXFOCUSBORDER);

    {
        NONCLIENTMETRICSW ncm;
        BOOL ok;
        memset(&ncm, 0, sizeof ncm);
        ncm.cbSize = sizeof ncm;
        ORACLE_CALL(ok, pSystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0));
        oracle_case("ncm_sizes");
        o_bool("ret", ok);
        o_lasterr();
        o_i64("border", ncm.iBorderWidth);
        o_i64("scroll_width", ncm.iScrollWidth);
        o_i64("scroll_height", ncm.iScrollHeight);
        o_i64("caption_width", ncm.iCaptionWidth);
        o_i64("caption_height", ncm.iCaptionHeight);
        o_i64("smcaption_width", ncm.iSmCaptionWidth);
        o_i64("smcaption_height", ncm.iSmCaptionHeight);
        o_i64("menu_width", ncm.iMenuWidth);
        o_i64("menu_height", ncm.iMenuHeight);
        o_i64("padded_border", ncm.iPaddedBorderWidth);
        oracle_case_end();

        oracle_case("ncm_caption_font");
        font_fields("caption", &ncm.lfCaptionFont);
        oracle_case_end();
        oracle_case("ncm_smcaption_font");
        font_fields("smcaption", &ncm.lfSmCaptionFont);
        oracle_case_end();
        oracle_case("ncm_menu_font");
        font_fields("menu", &ncm.lfMenuFont);
        oracle_case_end();
        oracle_case("ncm_status_font");
        font_fields("status", &ncm.lfStatusFont);
        oracle_case_end();
        oracle_case("ncm_message_font");
        font_fields("message", &ncm.lfMessageFont);
        oracle_case_end();
    }

    {
        LOGFONTW lf;
        BOOL ok;
        memset(&lf, 0, sizeof lf);
        ORACLE_CALL(ok, pSystemParametersInfoW(SPI_GETICONTITLELOGFONT, sizeof lf, &lf, 0));
        oracle_case("icon_title_font");
        o_bool("ret", ok);
        font_fields("icon", &lf);
        oracle_case_end();
    }

    {
        BOOL flat = 2, gradient = 2, ok1, ok2;
        ok1 = pSystemParametersInfoW(SPI_GETFLATMENU, 0, &flat, 0);
        ok2 = pSystemParametersInfoW(SPI_GETGRADIENTCAPTIONS, 0, &gradient, 0);
        oracle_case("flat_menu_gradient_captions");
        o_bool("flat_ret", ok1);
        o_u32("flat", flat);
        o_bool("gradient_ret", ok2);
        o_u32("gradient", gradient);
        oracle_case_end();
    }

    {
        HGDIOBJ f;
        LOGFONTW lf;
        int n;
        ORACLE_CALL(f, pGetStockObject(DEFAULT_GUI_FONT));
        memset(&lf, 0, sizeof lf);
        n = f ? pGetObjectW(f, sizeof lf, &lf) : 0;
        oracle_case("default_gui_font");
        o_bool("ret", f != NULL);
        o_i64("size", n);
        font_fields("gui", &lf);
        oracle_case_end();
    }

    adjust_case("adjust_overlapped", WS_OVERLAPPEDWINDOW, 0, FALSE);
    adjust_case("adjust_overlapped_menu", WS_OVERLAPPEDWINDOW, 0, TRUE);
    adjust_case("adjust_dialog", WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME,
                WS_EX_DLGMODALFRAME, FALSE);
    adjust_case("adjust_caption_only", WS_CAPTION, 0, FALSE);
    adjust_case("adjust_toolwindow", WS_OVERLAPPEDWINDOW, WS_EX_TOOLWINDOW, FALSE);
    adjust_case("adjust_border", WS_POPUP | WS_BORDER, 0, FALSE);
    adjust_case("adjust_clientedge", WS_OVERLAPPEDWINDOW, WS_EX_CLIENTEDGE, FALSE);

    probes();
    return oracle_end();
}
