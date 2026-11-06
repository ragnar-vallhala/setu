/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: the window-frame and control metrics an application can
 * query at 96 DPI: GetSystemMetrics for captions, caption buttons, frames,
 * borders, menus and scroll bars; SystemParametersInfo NONCLIENTMETRICS
 * (sizes and the caption, menu, status and message font faces), the icon
 * title font, flat menus and gradient captions; the stock DEFAULT_GUI_FONT;
 * and AdjustWindowRectEx for common frame styles.
 *
 * Setu draws its own window frames and control theme; this test keeps the
 * values apps see equal to Windows 10's while the drawing changes.
 * System colours are deliberately not recorded (Setu uses its own palette).
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getsystemmetrics
 *       https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-systemparametersinfow
 *       https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-nonclientmetricsw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-adjustwindowrectex
 *       https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-getstockobject
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

int main(void)
{
    oracle_begin("user32/sys_metrics");
    HMODULE u32, g32;
    ORACLE_CALL(u32, LoadLibraryW(L"user32.dll"));
    g32 = LoadLibraryW(L"gdi32.dll");
    oracle_case("load_user32");
    o_handle("ret", u32);
    o_bool("gdi32", g32 != NULL);
    oracle_case_end();
    if (!u32 || !g32) return oracle_end();
    LOAD(u32, GetSystemMetrics);
    LOAD(u32, SystemParametersInfoW);
    LOAD(u32, AdjustWindowRectEx);
    LOAD(u32, GetDC);
    LOAD(u32, ReleaseDC);
    LOAD(g32, GetStockObject);
    LOAD(g32, GetObjectW);
    LOAD(g32, GetDeviceCaps);

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

    return oracle_end();
}
