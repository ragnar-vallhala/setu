/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: the clipboard: OpenClipboard/CloseClipboard/EmptyClipboard
 * errors when not open, SetClipboardData/GetClipboardData round trips,
 * synthesized text formats (CF_UNICODETEXT -> CF_TEXT/CF_OEMTEXT/CF_LOCALE),
 * EnumClipboardFormats order, CountClipboardFormats, IsClipboardFormatAvailable,
 * RegisterClipboardFormatW / GetClipboardFormatNameW sizing, delayed rendering
 * (WM_RENDERFORMAT, WM_RENDERALLFORMATS, WM_DESTROYCLIPBOARD), owner tracking
 * and GetClipboardSequenceNumber changes.
 *
 * The clipboard is global to the window station, so the test empties it
 * first, retries OpenClipboard for a while if another program holds it, only
 * records formats it put there itself, and empties it again at the end.
 * Only ASCII text is used so the ANSI/OEM code pages do not matter.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-openclipboard
 *       https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-closeclipboard
 *       https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-emptyclipboard
 *       https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setclipboarddata
 *       https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getclipboarddata
 *       https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-enumclipboardformats
 *       https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-countclipboardformats
 *       https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-isclipboardformatavailable
 *       https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerclipboardformatw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getclipboardformatnamew
 *       https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getclipboardowner
 *       https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getopenclipboardwindow
 *       https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getclipboardsequencenumber
 *       https://learn.microsoft.com/en-us/windows/win32/dataxchg/standard-clipboard-formats
 *       https://learn.microsoft.com/en-us/windows/win32/dataxchg/clipboard-operations
 *       https://learn.microsoft.com/en-us/windows/win32/dataxchg/wm-renderformat
 *       https://learn.microsoft.com/en-us/windows/win32/dataxchg/wm-renderallformats
 *       https://learn.microsoft.com/en-us/windows/win32/dataxchg/wm-destroyclipboard
 */
#include "oracle.h"

#define DECL(f) static __typeof__(f) *p##f
#define LOAD(m, f) (*(FARPROC *)&p##f = GetProcAddress(m, #f))

DECL(RegisterClassExW);
DECL(CreateWindowExW);
DECL(DestroyWindow);
DECL(DefWindowProcW);
DECL(OpenClipboard);
DECL(CloseClipboard);
DECL(EmptyClipboard);
DECL(SetClipboardData);
DECL(GetClipboardData);
DECL(EnumClipboardFormats);
DECL(CountClipboardFormats);
DECL(IsClipboardFormatAvailable);
DECL(RegisterClipboardFormatW);
DECL(GetClipboardFormatNameW);
DECL(GetClipboardOwner);
DECL(GetOpenClipboardWindow);
DECL(GetClipboardSequenceNumber);

static HINSTANCE self;
static HWND owner, other;
static UINT fmt_custom, fmt_delayed;
static char log_[1024];

static void logs(const char *s)
{
    if (log_[0]) strncat(log_, ",", sizeof log_ - strlen(log_) - 1);
    strncat(log_, s, sizeof log_ - strlen(log_) - 1);
}

static HGLOBAL mem_from(const void *data, size_t n)
{
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, n);
    void *p = GlobalLock(g);
    memcpy(p, data, n);
    GlobalUnlock(g);
    return g;
}

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    const char *who = h == owner ? "owner" : h == other ? "other" : "?";
    char e[64];
    switch (m) {
    case WM_RENDERFORMAT:
        snprintf(e, sizeof e, "%s:RENDERFORMAT:%s", who, (UINT)w == fmt_delayed ? "delayed" : "unknown");
        logs(e);
        pSetClipboardData((UINT)w, mem_from("rendered", 9));
        return 0;
    case WM_RENDERALLFORMATS:
        snprintf(e, sizeof e, "%s:RENDERALLFORMATS", who);
        logs(e);
        if (pOpenClipboard(h)) {
            if (pGetClipboardOwner() == h) pSetClipboardData(fmt_delayed, mem_from("all", 4));
            pCloseClipboard();
        }
        return 0;
    case WM_DESTROYCLIPBOARD:
        snprintf(e, sizeof e, "%s:DESTROYCLIPBOARD", who);
        logs(e);
        return 0;
    }
    return pDefWindowProcW(h, m, w, l);
}

static const char *who(HWND h) { return h == owner ? "owner" : h == other ? "other" : h ? "foreign" : "null"; }

static BOOL open_retry(HWND h)
{
    for (int i = 0; i < 200; i++) {
        if (pOpenClipboard(h)) return TRUE;
        Sleep(25);
    }
    return FALSE;
}

/* Comma-separated list of formats on the clipboard that this test knows. */
static void formats_field(const char *k)
{
    char out[512] = "";
    UINT f = 0;
    int guard = 0;
    while ((f = pEnumClipboardFormats(f)) && guard++ < 64) {
        char n[32];
        if (f == fmt_custom) snprintf(n, sizeof n, "custom");
        else if (f == fmt_delayed) snprintf(n, sizeof n, "delayed");
        else if (f < 0xC000) snprintf(n, sizeof n, "%u", f);
        else snprintf(n, sizeof n, "registered");
        if (out[0]) strncat(out, ",", sizeof out - strlen(out) - 1);
        strncat(out, n, sizeof out - strlen(out) - 1);
    }
    o_str(k, out);
}

static void bool_case(const char *name, BOOL (*fn)(void))
{
    BOOL r;
    ORACLE_CALL(r, fn());
    oracle_case(name);
    o_bool("ret", r);
    o_lasterr();
    oracle_case_end();
}

static void avail_case(const char *name)
{
    oracle_case(name);
    o_bool("unicodetext", pIsClipboardFormatAvailable(CF_UNICODETEXT));
    o_bool("text", pIsClipboardFormatAvailable(CF_TEXT));
    o_bool("oemtext", pIsClipboardFormatAvailable(CF_OEMTEXT));
    o_bool("locale", pIsClipboardFormatAvailable(CF_LOCALE));
    o_bool("custom", pIsClipboardFormatAvailable(fmt_custom));
    o_bool("bitmap", pIsClipboardFormatAvailable(CF_BITMAP));
    o_i64("count", pCountClipboardFormats());
    oracle_case_end();
}

static void getdata_case(const char *name, UINT fmt, int as_wide)
{
    HANDLE h;
    ORACLE_CALL(h, pGetClipboardData(fmt));
    oracle_case(name);
    o_handle("ret", h);
    o_lasterr();
    if (h) {
        SIZE_T sz = GlobalSize(h);
        const void *p = GlobalLock(h);
        if (p) {
            if (as_wide) {
                o_wstr("text", (const wchar_t *)p);
                o_u64("size", sz);
            } else {
                o_bytes("data", p, sz > 32 ? 32 : sz);
                o_u64("size", sz);
            }
            GlobalUnlock(h);
        } else {
            o_str("lock", "failed");
        }
    }
    oracle_case_end();
}

int main(void)
{
    oracle_begin("user32/clipboard");
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
    LOAD(u32, OpenClipboard);
    LOAD(u32, CloseClipboard);
    LOAD(u32, EmptyClipboard);
    LOAD(u32, SetClipboardData);
    LOAD(u32, GetClipboardData);
    LOAD(u32, EnumClipboardFormats);
    LOAD(u32, CountClipboardFormats);
    LOAD(u32, IsClipboardFormatAvailable);
    LOAD(u32, RegisterClipboardFormatW);
    LOAD(u32, GetClipboardFormatNameW);
    LOAD(u32, GetClipboardOwner);
    LOAD(u32, GetOpenClipboardWindow);
    LOAD(u32, GetClipboardSequenceNumber);
    self = GetModuleHandleW(NULL);

    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = wndproc;
    wc.hInstance = self;
    wc.lpszClassName = L"SetuOracleClip";
    pRegisterClassExW(&wc);
    owner = pCreateWindowExW(0, L"SetuOracleClip", L"o", 0, 0, 0, 1, 1, HWND_MESSAGE, NULL, self, NULL);
    other = pCreateWindowExW(0, L"SetuOracleClip", L"x", 0, 0, 0, 1, 1, HWND_MESSAGE, NULL, self, NULL);

    /* ---- registered formats (no clipboard access needed) ---- */
    {
        UINT a, b, c;
        ORACLE_CALL(a, pRegisterClipboardFormatW(L"SetuOracle Format"));
        b = pRegisterClipboardFormatW(L"SetuOracle Format");
        c = pRegisterClipboardFormatW(L"SETUORACLE FORMAT");
        oracle_case("registerformat");
        o_bool("in_range", a >= 0xC000 && a <= 0xFFFF);
        o_bool("repeat_same", a == b);
        o_bool("case_insensitive", a == c);
        o_lasterr();
        oracle_case_end();
        fmt_custom = a;
        fmt_delayed = pRegisterClipboardFormatW(L"SetuOracle Delayed");

        ORACLE_CALL(a, pRegisterClipboardFormatW(L""));
        oracle_case("registerformat_empty");
        o_u32("ret", a);
        o_lasterr();
        oracle_case_end();

        static const int sizes[] = {64, 18, 17, 2, 1, 0};
        for (size_t i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
            wchar_t buf[80];
            int r;
            char nm[48];
            wmemset(buf, L'#', 80);
            buf[79] = 0;
            ORACLE_CALL(r, pGetClipboardFormatNameW(fmt_custom, buf, sizes[i]));
            snprintf(nm, sizeof nm, "formatname_buf_%d", sizes[i]);
            oracle_case(nm);
            o_i64("ret", r);
            o_lasterr();
            if (sizes[i] > 0) buf[sizes[i]] = 0;
            else buf[4] = 0;
            o_wstr("buf", buf);
            oracle_case_end();
        }
        wchar_t buf[80];
        int r;
        ORACLE_CALL(r, pGetClipboardFormatNameW(CF_TEXT, buf, 80));
        oracle_case("formatname_predefined_cf_text");
        o_i64("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, pGetClipboardFormatNameW(0xBFFF, buf, 80));
        oracle_case("formatname_unregistered_0xbfff");
        o_i64("ret", r);
        o_lasterr();
        oracle_case_end();
    }

    /* ---- errors while the clipboard is not open ---- */
    bool_case("close_not_open", pCloseClipboard);
    bool_case("empty_not_open", pEmptyClipboard);
    {
        HANDLE h;
        UINT f;
        ORACLE_CALL(h, pSetClipboardData(CF_UNICODETEXT, mem_from(L"x", 4)));
        oracle_case("setdata_not_open");
        o_handle("ret", h);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(h, pGetClipboardData(CF_UNICODETEXT));
        oracle_case("getdata_not_open");
        o_handle("ret", h);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(f, pEnumClipboardFormats(0));
        oracle_case("enumformats_not_open");
        o_u32("ret", f);
        o_lasterr();
        oracle_case_end();
    }

    /* ---- open, empty, set ---- */
    {
        BOOL r;
        ORACLE_CALL(r, open_retry(owner));
        oracle_case("open");
        o_bool("ret", r);
        o_str("open_window", who(pGetOpenClipboardWindow()));
        oracle_case_end();
        ORACLE_CALL(r, pOpenClipboard(owner));
        oracle_case("open_again_same_window");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, pOpenClipboard(other));
        oracle_case("open_again_other_window_same_thread");
        o_bool("ret", r);
        o_lasterr();
        o_str("open_window", who(pGetOpenClipboardWindow()));
        oracle_case_end();
        DWORD seq0 = pGetClipboardSequenceNumber();
        ORACLE_CALL(r, pEmptyClipboard());
        oracle_case("empty");
        o_bool("ret", r);
        o_lasterr();
        o_str("owner", who(pGetClipboardOwner()));
        o_i64("count", pCountClipboardFormats());
        o_bool("seq_changed", pGetClipboardSequenceNumber() != seq0);
        oracle_case_end();

        HANDLE in = mem_from(L"Hello clip", 22), out;
        DWORD seq1 = pGetClipboardSequenceNumber();
        ORACLE_CALL(out, pSetClipboardData(CF_UNICODETEXT, in));
        oracle_case("setdata_unicodetext");
        o_bool("ret_is_input", out == in);
        o_lasterr();
        o_bool("seq_changed", pGetClipboardSequenceNumber() != seq1);
        oracle_case_end();

        unsigned char blob[] = {1, 2, 3, 4, 5, 0, 7};
        in = mem_from(blob, sizeof blob);
        ORACLE_CALL(out, pSetClipboardData(fmt_custom, in));
        oracle_case("setdata_custom");
        o_bool("ret_is_input", out == in);
        o_lasterr();
        oracle_case_end();

        ORACLE_CALL(out, pSetClipboardData(fmt_delayed, NULL));
        oracle_case("setdata_delayed_null");
        o_handle("ret", out);
        o_lasterr();
        oracle_case_end();

        ORACLE_CALL(out, pSetClipboardData(0, mem_from("z", 2)));
        oracle_case("setdata_format_0");
        o_handle("ret", out);
        o_lasterr();
        oracle_case_end();

        avail_case("available_while_open");
        oracle_case("enum_while_open");
        formats_field("formats");
        oracle_case_end();
        getdata_case("getdata_unicodetext_while_open", CF_UNICODETEXT, 1);
        getdata_case("getdata_custom_while_open", fmt_custom, 0);
        log_[0] = 0;
        getdata_case("getdata_delayed_while_open", fmt_delayed, 0);
        oracle_case("delayed_render_log");
        o_str("log", log_);
        oracle_case_end();
        log_[0] = 0;
        getdata_case("getdata_bitmap_absent", CF_BITMAP, 0);

        ORACLE_CALL(r, pCloseClipboard());
        oracle_case("close");
        o_bool("ret", r);
        o_lasterr();
        o_str("open_window", who(pGetOpenClipboardWindow()));
        o_str("owner", who(pGetClipboardOwner()));
        oracle_case_end();
        bool_case("close_twice", pCloseClipboard);
    }

    /* ---- after close: synthesized formats ---- */
    avail_case("available_after_close");
    {
        BOOL r;
        ORACLE_CALL(r, open_retry(NULL));
        oracle_case("open_null_window");
        o_bool("ret", r);
        o_str("open_window", who(pGetOpenClipboardWindow()));
        oracle_case_end();
        oracle_case("enum_after_close");
        formats_field("formats");
        oracle_case_end();
        getdata_case("getdata_text_synthesized", CF_TEXT, 0);
        getdata_case("getdata_oemtext_synthesized", CF_OEMTEXT, 0);
        getdata_case("getdata_unicodetext_again", CF_UNICODETEXT, 1);
        {
            HANDLE h = pGetClipboardData(CF_LOCALE);
            oracle_case("getdata_locale");
            o_handle("ret", h);
            if (h) o_u64("size", GlobalSize(h));
            oracle_case_end();
        }
        HANDLE out;
        ORACLE_CALL(out, pSetClipboardData(CF_TEXT, mem_from("x", 2)));
        oracle_case("setdata_without_empty_null_owner");
        o_handle("ret", out);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, pEmptyClipboard());
        oracle_case("empty_with_null_window");
        o_bool("ret", r);
        o_lasterr();
        o_str("owner", who(pGetClipboardOwner()));
        oracle_case_end();
        oracle_case("destroyclipboard_log");
        o_str("log", log_);
        oracle_case_end();
        log_[0] = 0;
        pCloseClipboard();
    }

    /* ---- delayed rendering of all formats when the owner is destroyed ---- */
    {
        open_retry(owner);
        pEmptyClipboard();
        pSetClipboardData(fmt_delayed, NULL);
        pSetClipboardData(CF_TEXT, mem_from("plain", 6));
        pCloseClipboard();
        log_[0] = 0;
        BOOL r;
        ORACLE_CALL(r, pDestroyWindow(owner));
        oracle_case("destroy_owner_renders_all");
        o_bool("ret", r);
        o_str("log", log_);
        o_str("owner_after", who(pGetClipboardOwner()));
        oracle_case_end();
        owner = NULL;
        log_[0] = 0;
        open_retry(other);
        getdata_case("getdata_delayed_after_owner_destroyed", fmt_delayed, 0);
        getdata_case("getdata_text_after_owner_destroyed", CF_TEXT, 0);
        getdata_case("getdata_unicode_from_text", CF_UNICODETEXT, 1);
        pEmptyClipboard();
        pCloseClipboard();
        oracle_case("final_log");
        o_str("log", log_);
        oracle_case_end();
    }

    pDestroyWindow(other);
    return oracle_end();
}
