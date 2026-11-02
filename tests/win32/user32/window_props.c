/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: per-window data: GetWindowLongPtrW/SetWindowLongPtrW (user
 * data, extra bytes, ids, styles, bad indexes), the styles the system adds
 * at creation (WS_CAPTION and WS_CLIPSIBLINGS for overlapped windows,
 * WS_EX_WINDOWEDGE, ...), and window text: SetWindowTextW, GetWindowTextW
 * and GetWindowTextLengthW buffer sizing, WM_GETTEXT / WM_GETTEXTLENGTH.
 *
 * Only hidden or message-only windows are created; no positions, sizes,
 * fonts or DPI are recorded.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getwindowlongptrw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowlongptrw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-createwindowexw
 *       https://learn.microsoft.com/en-us/windows/win32/winmsg/window-styles
 *       https://learn.microsoft.com/en-us/windows/win32/winmsg/extended-window-styles
 *       https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowtextw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getwindowtextw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getwindowtextlengthw
 *       https://learn.microsoft.com/en-us/windows/win32/winmsg/wm-gettext
 *       https://learn.microsoft.com/en-us/windows/win32/winmsg/wm-gettextlength
 *       https://learn.microsoft.com/en-us/windows/win32/winmsg/wm-settext
 */
#include "oracle.h"

#define DECL(f) static __typeof__(f) *p##f
#define LOAD(m, f) (*(FARPROC *)&p##f = GetProcAddress(m, #f))

DECL(RegisterClassExW);
DECL(CreateWindowExW);
DECL(DestroyWindow);
DECL(DefWindowProcW);
DECL(GetWindowLongPtrW);
DECL(SetWindowLongPtrW);
DECL(GetWindowLongW);
DECL(SetWindowLongW);
DECL(SetWindowTextW);
DECL(GetWindowTextW);
DECL(GetWindowTextA);
DECL(GetWindowTextLengthW);
DECL(GetWindowTextLengthA);
DECL(SendMessageW);
DECL(IsWindowUnicode);
DECL(IsWindowVisible);

static HINSTANCE self;
#define BAD_HWND ((HWND)(ULONG_PTR)0x0bad0bad)

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    return pDefWindowProcW(h, m, w, l);
}

static HWND mk(DWORD ex, DWORD style, HWND parent, HMENU id, const wchar_t *text)
{
    return pCreateWindowExW(ex, L"SetuOracleWP", text, style, 0, 0, 50, 50, parent, id, self, NULL);
}

static void styles_case(const char *name, DWORD ex, DWORD style, HWND parent)
{
    HWND h;
    ORACLE_CALL(h, mk(ex, style, parent, NULL, L"s"));
    oracle_case(name);
    o_handle("ret", h);
    o_lasterr();
    if (h) {
        o_hex32("style", (DWORD)pGetWindowLongPtrW(h, GWL_STYLE));
        o_hex32("exstyle", (DWORD)pGetWindowLongPtrW(h, GWL_EXSTYLE));
        o_hex32("style_requested", style);
        o_hex32("exstyle_requested", ex);
        pDestroyWindow(h);
    }
    oracle_case_end();
}

static void getlong_case(const char *name, HWND h, int idx)
{
    LONG_PTR v;
    ORACLE_CALL(v, pGetWindowLongPtrW(h, idx));
    oracle_case(name);
    o_i64("ret", v);
    o_lasterr();
    oracle_case_end();
}

static void setlong_case(const char *name, HWND h, int idx, LONG_PTR nv)
{
    LONG_PTR v;
    ORACLE_CALL(v, pSetWindowLongPtrW(h, idx, nv));
    oracle_case(name);
    o_i64("ret", v);
    o_lasterr();
    oracle_case_end();
}

static void gettext_case(const char *name, HWND h, int cch)
{
    wchar_t buf[2100];
    int r;
    wmemset(buf, L'#', 2100);
    ORACLE_CALL(r, pGetWindowTextW(h, buf, cch));
    oracle_case(name);
    o_i64("ret", r);
    o_lasterr();
    buf[2099] = 0;
    if (cch > 0 && cch < 2099) buf[cch] = 0; /* show where the API stopped */
    if (wcslen(buf) <= 64) o_wstr("buf", buf);
    else o_u64("buf_strlen", wcslen(buf));
    oracle_case_end();
}

static void textlen_case(const char *name, HWND h)
{
    int r;
    ORACLE_CALL(r, pGetWindowTextLengthW(h));
    oracle_case(name);
    o_i64("ret", r);
    o_lasterr();
    oracle_case_end();
}

static void settext_case(const char *name, HWND h, const wchar_t *t)
{
    BOOL r;
    ORACLE_CALL(r, pSetWindowTextW(h, t));
    oracle_case(name);
    o_bool("ret", r);
    o_lasterr();
    o_i64("len_after", pGetWindowTextLengthW(h));
    oracle_case_end();
}

int main(void)
{
    oracle_begin("user32/window_props");
    HMODULE u32;
    ORACLE_CALL(u32, LoadLibraryW(L"user32.dll"));
    oracle_case("load_user32");
    o_handle("ret", u32);
    oracle_case_end();
    if (!u32) return oracle_end();
    LOAD(u32, RegisterClassExW);
    LOAD(u32, CreateWindowExW);
    LOAD(u32, DestroyWindow);
    LOAD(u32, DefWindowProcW);
    LOAD(u32, GetWindowLongPtrW);
    LOAD(u32, SetWindowLongPtrW);
    LOAD(u32, GetWindowLongW);
    LOAD(u32, SetWindowLongW);
    LOAD(u32, SetWindowTextW);
    LOAD(u32, GetWindowTextW);
    LOAD(u32, GetWindowTextA);
    LOAD(u32, GetWindowTextLengthW);
    LOAD(u32, GetWindowTextLengthA);
    LOAD(u32, SendMessageW);
    LOAD(u32, IsWindowUnicode);
    LOAD(u32, IsWindowVisible);
    self = GetModuleHandleW(NULL);

    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = wndproc;
    wc.hInstance = self;
    wc.cbWndExtra = 16;
    wc.lpszClassName = L"SetuOracleWP";
    oracle_case("register_class");
    o_bool("ret_nonzero", pRegisterClassExW(&wc) != 0);
    oracle_case_end();

    /* ---- styles adjusted at creation ---- */
    styles_case("style_overlapped_zero", 0, WS_OVERLAPPED, NULL);
    styles_case("style_overlappedwindow", 0, WS_OVERLAPPEDWINDOW, NULL);
    styles_case("style_popup", 0, WS_POPUP, NULL);
    styles_case("style_popup_caption", 0, WS_POPUP | WS_CAPTION, NULL);
    styles_case("style_popup_border", 0, WS_POPUP | WS_BORDER, NULL);
    styles_case("style_popup_dlgframe", 0, WS_POPUP | WS_DLGFRAME, NULL);
    styles_case("style_popup_thickframe", 0, WS_POPUP | WS_THICKFRAME, NULL);
    styles_case("style_overlapped_sysmenu", 0, WS_SYSMENU, NULL);
    styles_case("style_overlapped_clipchildren", 0, WS_CLIPCHILDREN, NULL);
    styles_case("exstyle_dlgmodalframe", WS_EX_DLGMODALFRAME, WS_POPUP, NULL);
    styles_case("exstyle_toolwindow", WS_EX_TOOLWINDOW, WS_POPUP, NULL);
    styles_case("exstyle_clientedge", WS_EX_CLIENTEDGE, WS_POPUP, NULL);
    styles_case("exstyle_staticedge", WS_EX_STATICEDGE, WS_POPUP, NULL);
    styles_case("exstyle_windowedge_popup", WS_EX_WINDOWEDGE, WS_POPUP, NULL);
    styles_case("exstyle_topmost", WS_EX_TOPMOST, WS_POPUP, NULL);
    styles_case("exstyle_overlappedwindow_ex", WS_EX_OVERLAPPEDWINDOW, WS_POPUP, NULL);
    styles_case("exstyle_appwindow", WS_EX_APPWINDOW, WS_POPUP, NULL);
    styles_case("exstyle_noactivate", WS_EX_NOACTIVATE, WS_POPUP, NULL);
    styles_case("exstyle_mdichild_no_client", WS_EX_MDICHILD, WS_POPUP, NULL);
    styles_case("exstyle_bogus_bits", 0x80000000u, WS_POPUP, NULL);
    styles_case("style_message_only", 0, 0, HWND_MESSAGE);
    styles_case("style_message_only_caption", 0, WS_CAPTION, HWND_MESSAGE);
    styles_case("exstyle_message_only_dlgmodalframe", WS_EX_DLGMODALFRAME, 0, HWND_MESSAGE);

    HWND top = mk(0, WS_OVERLAPPEDWINDOW, NULL, NULL, L"top");
    styles_case("style_child", 0, WS_CHILD, top);
    styles_case("style_child_caption", 0, WS_CHILD | WS_CAPTION, top);
    styles_case("exstyle_child_dlgmodalframe", WS_EX_DLGMODALFRAME, WS_CHILD, top);
    styles_case("style_child_and_popup", 0, WS_CHILD | WS_POPUP, top);
    styles_case("style_owned_overlapped", 0, WS_OVERLAPPED, top);

    /* ---- GetWindowLongPtrW / SetWindowLongPtrW ---- */
    HWND w = mk(0, 0, HWND_MESSAGE, NULL, L"props");
    getlong_case("userdata_initial", w, GWLP_USERDATA);
    setlong_case("userdata_set_first", w, GWLP_USERDATA, 0x1234);
    setlong_case("userdata_set_second", w, GWLP_USERDATA, (LONG_PTR)0x7766554433221100ll);
    getlong_case("userdata_get", w, GWLP_USERDATA);
    setlong_case("userdata_set_zero", w, GWLP_USERDATA, 0);
    setlong_case("userdata_set_after_zero", w, GWLP_USERDATA, 9);
    getlong_case("extra_0_initial", w, 0);
    getlong_case("extra_8_initial", w, 8);
    setlong_case("extra_0_set", w, 0, 0x42);
    setlong_case("extra_8_set", w, 8, -2);
    getlong_case("extra_8_get", w, 8);
    getlong_case("extra_4_straddle", w, 4);
    getlong_case("extra_9_overrun", w, 9);
    getlong_case("extra_16_past_end", w, 16);
    setlong_case("extra_16_set_past_end", w, 16, 1);
    {
        LONG v;
        ORACLE_CALL(v, pGetWindowLongW(w, 12));
        oracle_case("extra_12_dword");
        o_i64("ret", v);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(v, pGetWindowLongW(w, 13));
        oracle_case("extra_13_dword_overrun");
        o_i64("ret", v);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(v, pSetWindowLongW(w, 12, 77));
        oracle_case("extra_12_dword_set");
        o_i64("ret", v);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(v, pGetWindowLongW(w, 8));
        oracle_case("extra_8_dword_after_set");
        o_i64("ret", v);
        o_lasterr();
        oracle_case_end();
    }
    getlong_case("index_bogus_negative", w, -100);
    getlong_case("index_bogus_positive", w, 4096);
    setlong_case("set_index_bogus_negative", w, -100, 1);
    getlong_case("bad_hwnd_userdata", BAD_HWND, GWLP_USERDATA);
    setlong_case("bad_hwnd_set_userdata", BAD_HWND, GWLP_USERDATA, 1);
    getlong_case("null_hwnd_style", NULL, GWL_STYLE);
    {
        LONG_PTR v;
        ORACLE_CALL(v, pGetWindowLongPtrW(w, GWLP_HINSTANCE));
        oracle_case("hinstance_is_self");
        o_bool("ret_is_self", v == (LONG_PTR)self);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(v, pGetWindowLongPtrW(w, GWLP_WNDPROC));
        oracle_case("wndproc_is_class_proc");
        o_bool("ret_is_ours", v == (LONG_PTR)wndproc);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(v, pGetWindowLongPtrW(w, GWLP_HWNDPARENT));
        oracle_case("hwndparent_message_only");
        o_bool("ret_null", v == 0);
        o_lasterr();
        oracle_case_end();
        oracle_case("is_window_unicode");
        o_bool("ret", pIsWindowUnicode(w));
        oracle_case_end();
    }
    /* ids */
    HWND child = mk(0, WS_CHILD, top, (HMENU)(ULONG_PTR)1234, L"c");
    getlong_case("child_id", child, GWLP_ID);
    setlong_case("child_id_set", child, GWLP_ID, 99);
    getlong_case("child_id_after_set", child, GWLP_ID);
    getlong_case("toplevel_id", top, GWLP_ID);
    {
        LONG_PTR v;
        ORACLE_CALL(v, pGetWindowLongPtrW(child, GWLP_HWNDPARENT));
        oracle_case("child_hwndparent_is_parent");
        o_bool("ret_is_parent", v == (LONG_PTR)top);
        o_lasterr();
        oracle_case_end();
        HWND owned = mk(0, WS_POPUP, top, NULL, L"o");
        ORACLE_CALL(v, pGetWindowLongPtrW(owned, GWLP_HWNDPARENT));
        oracle_case("owned_hwndparent_is_owner");
        o_bool("ret_is_owner", v == (LONG_PTR)top);
        o_lasterr();
        oracle_case_end();
        pDestroyWindow(owned);
    }
    /* styles via SetWindowLongPtr */
    {
        HWND p = mk(0, WS_POPUP, NULL, NULL, L"p");
        LONG_PTR v;
        ORACLE_CALL(v, pSetWindowLongPtrW(p, GWL_STYLE, WS_POPUP | WS_CLIPCHILDREN | WS_DISABLED));
        oracle_case("set_style_popup_disabled");
        o_hex32("ret", (DWORD)v);
        o_lasterr();
        o_hex32("style_after", (DWORD)pGetWindowLongPtrW(p, GWL_STYLE));
        oracle_case_end();
        ORACLE_CALL(v, pSetWindowLongPtrW(p, GWL_STYLE, WS_POPUP | WS_CAPTION));
        oracle_case("set_style_caption");
        o_hex32("ret", (DWORD)v);
        o_lasterr();
        o_hex32("style_after", (DWORD)pGetWindowLongPtrW(p, GWL_STYLE));
        o_bool("visible", pIsWindowVisible(p));
        oracle_case_end();
        ORACLE_CALL(v, pSetWindowLongPtrW(p, GWL_EXSTYLE, WS_EX_TOOLWINDOW));
        oracle_case("set_exstyle_toolwindow");
        o_hex32("ret", (DWORD)v);
        o_lasterr();
        o_hex32("exstyle_after", (DWORD)pGetWindowLongPtrW(p, GWL_EXSTYLE));
        oracle_case_end();
        ORACLE_CALL(v, pSetWindowLongPtrW(p, GWL_EXSTYLE, WS_EX_DLGMODALFRAME));
        oracle_case("set_exstyle_dlgmodalframe");
        o_hex32("ret", (DWORD)v);
        o_lasterr();
        o_hex32("exstyle_after", (DWORD)pGetWindowLongPtrW(p, GWL_EXSTYLE));
        oracle_case_end();
        ORACLE_CALL(v, pSetWindowLongPtrW(p, GWL_EXSTYLE, 0));
        oracle_case("set_exstyle_zero");
        o_hex32("ret", (DWORD)v);
        o_lasterr();
        o_hex32("exstyle_after", (DWORD)pGetWindowLongPtrW(p, GWL_EXSTYLE));
        oracle_case_end();
        pDestroyWindow(p);
    }

    /* ---- window text ---- */
    HWND t = mk(0, 0, HWND_MESSAGE, NULL, NULL);
    textlen_case("text_null_at_create_len", t);
    gettext_case("text_null_at_create_get", t, 10);
    settext_case("settext_hello", t, L"Hello");
    textlen_case("text_hello_len", t);
    gettext_case("gettext_buf_10", t, 10);
    gettext_case("gettext_buf_6_exact", t, 6);
    gettext_case("gettext_buf_5_short", t, 5);
    gettext_case("gettext_buf_2", t, 2);
    gettext_case("gettext_buf_1", t, 1);
    gettext_case("gettext_buf_0", t, 0);
    gettext_case("gettext_buf_negative", t, -1);
    {
        char a[16];
        int r;
        memset(a, '#', sizeof a);
        ORACLE_CALL(r, pGetWindowTextA(t, a, 4));
        oracle_case("gettext_a_buf_4");
        o_i64("ret", r);
        o_lasterr();
        a[4] = 0;
        o_str("buf", a);
        oracle_case_end();
        ORACLE_CALL(r, pGetWindowTextLengthA(t));
        oracle_case("text_hello_len_a");
        o_i64("ret", r);
        o_lasterr();
        oracle_case_end();
    }
    settext_case("settext_empty", t, L"");
    textlen_case("text_empty_len", t);
    gettext_case("gettext_empty", t, 10);
    settext_case("settext_null", t, NULL);
    textlen_case("text_after_null_len", t);
    settext_case("settext_embedded_crlf_tab", t, L"a\r\nb\tc");
    gettext_case("gettext_crlf_tab", t, 20);
    {
        wchar_t big[2001];
        for (int i = 0; i < 2000; i++) big[i] = L'A' + (i % 26);
        big[2000] = 0;
        settext_case("settext_2000_chars", t, big);
        gettext_case("gettext_2000_buf_2100", t, 2100);
        gettext_case("gettext_2000_buf_1000", t, 1000);
    }
    settext_case("settext_unicode", t, L"\x00e9\x20ac\xd83d\xde00");
    gettext_case("gettext_unicode", t, 10);
    gettext_case("gettext_unicode_split_surrogate", t, 4);
    settext_case("settext_bad_hwnd", BAD_HWND, L"x");
    gettext_case("gettext_bad_hwnd", BAD_HWND, 10);
    textlen_case("textlen_bad_hwnd", BAD_HWND);

    /* WM_GETTEXT / WM_GETTEXTLENGTH directly */
    pSetWindowTextW(t, L"Message");
    {
        wchar_t buf[32];
        LRESULT r;
        wmemset(buf, L'#', 32);
        ORACLE_CALL(r, pSendMessageW(t, WM_GETTEXTLENGTH, 0, 0));
        oracle_case("wm_gettextlength");
        o_i64("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, pSendMessageW(t, WM_GETTEXT, 4, (LPARAM)buf));
        oracle_case("wm_gettext_buf_4");
        o_i64("ret", r);
        o_lasterr();
        buf[5] = 0;
        o_wstr("buf", buf);
        oracle_case_end();
        wmemset(buf, L'#', 32);
        ORACLE_CALL(r, pSendMessageW(t, WM_GETTEXT, 0, (LPARAM)buf));
        oracle_case("wm_gettext_buf_0");
        o_i64("ret", r);
        o_lasterr();
        o_bool("buf_untouched", buf[0] == L'#');
        oracle_case_end();
        ORACLE_CALL(r, pSendMessageW(t, WM_SETTEXT, 0, (LPARAM)L"Via WM_SETTEXT"));
        oracle_case("wm_settext");
        o_i64("ret", r);
        o_lasterr();
        o_i64("len_after", pGetWindowTextLengthW(t));
        oracle_case_end();
        ORACLE_CALL(r, pSendMessageW(t, WM_SETTEXT, 0, 0));
        oracle_case("wm_settext_null");
        o_i64("ret", r);
        o_lasterr();
        o_i64("len_after", pGetWindowTextLengthW(t));
        oracle_case_end();
    }

    /* After DestroyWindow everything fails with ERROR_INVALID_WINDOW_HANDLE. */
    pDestroyWindow(t);
    gettext_case("gettext_destroyed", t, 10);
    getlong_case("getlong_destroyed", t, GWLP_USERDATA);
    pDestroyWindow(w);
    pDestroyWindow(top);
    return oracle_end();
}
