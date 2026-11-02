/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: window relationships: parent, owner and child windows;
 * GetParent, GetAncestor (GA_PARENT/GA_ROOT/GA_ROOTOWNER), GetWindow
 * (GW_CHILD/GW_HWNDNEXT/GW_HWNDLAST/GW_OWNER), IsChild, EnumChildWindows
 * order, SetParent, FindWindowExW under HWND_MESSAGE, and the
 * creation/destruction message sequence (WM_NCCREATE ... WM_NCDESTROY) for
 * parents, children and owned windows, plus IsWindow after DestroyWindow.
 *
 * Windows are hidden or message-only. Handles are never printed: each test
 * window gets a small number (its GWLP_USERDATA), and results print that
 * name or "desktop" / "message_parent" / "null" / "other".
 *
 * Z-order is only recorded among siblings created by this test (children of
 * one parent), never among top-level windows, which share the desktop with
 * whatever else is running.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getparent
 *       https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getancestor
 *       https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getwindow
 *       https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-ischild
 *       https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-enumchildwindows
 *       https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setparent
 *       https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-findwindowexw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-destroywindow
 *       https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-iswindow
 *       https://learn.microsoft.com/en-us/windows/win32/winmsg/window-features
 *       https://learn.microsoft.com/en-us/windows/win32/winmsg/wm-destroy
 *       https://learn.microsoft.com/en-us/windows/win32/winmsg/wm-ncdestroy
 *       https://learn.microsoft.com/en-us/windows/win32/winmsg/wm-nccreate
 *       https://learn.microsoft.com/en-us/windows/win32/winmsg/wm-create
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
DECL(GetParent);
DECL(GetAncestor);
DECL(GetWindow);
DECL(IsChild);
DECL(IsWindow);
DECL(EnumChildWindows);
DECL(SetParent);
DECL(GetDesktopWindow);
DECL(FindWindowExW);
DECL(GetWindowLongW);

static HINSTANCE self;
static HWND desktop, msgparent;
static HWND wins[32];
static int nwins;
#define BAD_HWND ((HWND)(ULONG_PTR)0x0bad0bad)

/* ---- message log ---- */
static char msglog[4096];
static int logging;

static const char *msgname(UINT m)
{
    switch (m) {
    case WM_NCCREATE: return "NCCREATE";
    case WM_CREATE: return "CREATE";
    case WM_DESTROY: return "DESTROY";
    case WM_NCDESTROY: return "NCDESTROY";
    case WM_PARENTNOTIFY: return "PARENTNOTIFY";
    case WM_SHOWWINDOW: return "SHOWWINDOW";
    case WM_SIZE: return "SIZE";
    case WM_MOVE: return "MOVE";
    case WM_CLOSE: return "CLOSE";
    default: return NULL;
    }
}

static int win_index(HWND h)
{
    for (int i = 0; i < nwins; i++)
        if (wins[i] == h) return i;
    return -1;
}

static void name_of(HWND h, char *out, size_t n)
{
    int i;
    if (!h) snprintf(out, n, "null");
    else if (h == desktop) snprintf(out, n, "desktop");
    else if (msgparent && h == msgparent) snprintf(out, n, "message_parent");
    else if ((i = win_index(h)) >= 0) snprintf(out, n, "w%d", i);
    else snprintf(out, n, "other");
}

static void o_hwnd(const char *k, HWND h)
{
    char b[32];
    name_of(h, b, sizeof b);
    o_str(k, b);
}

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    const char *n = msgname(m);
    if (logging && n) {
        char e[64];
        int i = win_index(h);
        if (i < 0) i = nwins; /* being created: not yet in the table */
        if (m == WM_PARENTNOTIFY)
            snprintf(e, sizeof e, "%sw%d:%s:%s", msglog[0] ? "," : "", i, n,
                     LOWORD(w) == WM_CREATE ? "create" : LOWORD(w) == WM_DESTROY ? "destroy" : "other");
        else
            snprintf(e, sizeof e, "%sw%d:%s", msglog[0] ? "," : "", i, n);
        strncat(msglog, e, sizeof msglog - strlen(msglog) - 1);
    }
    return pDefWindowProcW(h, m, w, l);
}

static HWND mk(DWORD ex, DWORD style, HWND parent)
{
    HWND h = pCreateWindowExW(ex, L"SetuOracleWT", L"", style, 0, 0, 20, 20, parent,
                              (style & WS_CHILD) ? (HMENU)(ULONG_PTR)(100 + nwins) : NULL, self, NULL);
    if (h) wins[nwins++] = h;
    return h;
}

static void rel_case(const char *name, HWND h)
{
    oracle_case(name);
    o_hwnd("parent", pGetParent(h));
    o_hwnd("ga_parent", pGetAncestor(h, GA_PARENT));
    o_hwnd("ga_root", pGetAncestor(h, GA_ROOT));
    o_hwnd("ga_rootowner", pGetAncestor(h, GA_ROOTOWNER));
    o_hwnd("owner", pGetWindow(h, GW_OWNER));
    o_hwnd("first_child", pGetWindow(h, GW_CHILD));
    oracle_case_end();
}

static char enumbuf[1024];
static BOOL CALLBACK enum_cb(HWND h, LPARAM l)
{
    char b[32];
    (void)l;
    name_of(h, b, sizeof b);
    if (enumbuf[0]) strncat(enumbuf, ",", sizeof enumbuf - strlen(enumbuf) - 1);
    strncat(enumbuf, b, sizeof enumbuf - strlen(enumbuf) - 1);
    return TRUE;
}

static void children_case(const char *name, HWND parent)
{
    char order[1024] = "";
    HWND c = pGetWindow(parent, GW_CHILD);
    int guard = 0;
    while (c && guard++ < 32) {
        char b[32];
        name_of(c, b, sizeof b);
        if (order[0]) strncat(order, ",", sizeof order - strlen(order) - 1);
        strncat(order, b, sizeof order - strlen(order) - 1);
        c = pGetWindow(c, GW_HWNDNEXT);
    }
    enumbuf[0] = 0;
    BOOL r = pEnumChildWindows(parent, enum_cb, 0);
    oracle_case(name);
    o_str("zorder", order);
    o_str("enum", enumbuf);
    o_bool("enum_ret", r);
    HWND first = pGetWindow(parent, GW_CHILD);
    o_hwnd("last", first ? pGetWindow(first, GW_HWNDLAST) : NULL);
    o_hwnd("first_from_last", first ? pGetWindow(pGetWindow(first, GW_HWNDLAST), GW_HWNDFIRST) : NULL);
    o_hwnd("prev_of_first", first ? pGetWindow(first, GW_HWNDPREV) : NULL);
    oracle_case_end();
}

static struct { HWND h; BOOL r; DWORD err; } xthread;
static DWORD WINAPI destroy_thread(LPVOID p)
{
    (void)p;
    SetLastError(ORACLE_SENTINEL);
    xthread.r = pDestroyWindow(xthread.h);
    xthread.err = GetLastError();
    return 0;
}

static void log_begin(void) { msglog[0] = 0; logging = 1; }
static void log_case(const char *name)
{
    logging = 0;
    oracle_case(name);
    o_str("messages", msglog);
    oracle_case_end();
}

int main(void)
{
    oracle_begin("user32/window_tree");
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
    LOAD(u32, GetParent);
    LOAD(u32, GetAncestor);
    LOAD(u32, GetWindow);
    LOAD(u32, IsChild);
    LOAD(u32, IsWindow);
    LOAD(u32, EnumChildWindows);
    LOAD(u32, SetParent);
    LOAD(u32, GetDesktopWindow);
    LOAD(u32, FindWindowExW);
    LOAD(u32, GetWindowLongW);
    self = GetModuleHandleW(NULL);
    desktop = pGetDesktopWindow();

    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = wndproc;
    wc.hInstance = self;
    wc.lpszClassName = L"SetuOracleWT";
    oracle_case("register_class");
    o_bool("ret_nonzero", pRegisterClassExW(&wc) != 0);
    o_bool("desktop_is_window", pIsWindow(desktop));
    o_hwnd("desktop_parent", pGetParent(desktop));
    o_hwnd("desktop_ga_parent", pGetAncestor(desktop, GA_PARENT));
    oracle_case_end();

    /* ---- creation messages ---- */
    log_begin();
    HWND a = mk(0, WS_OVERLAPPED, NULL);               /* w0: hidden top-level */
    log_case("create_messages_toplevel");
    log_begin();
    HWND c = mk(0, WS_CHILD, a);                       /* w1: child of a */
    log_case("create_messages_child");
    log_begin();
    HWND d = mk(0, WS_CHILD, c);                       /* w2: grandchild */
    log_case("create_messages_grandchild");
    log_begin();
    HWND b = mk(0, WS_POPUP, a);                       /* w3: owned popup */
    log_case("create_messages_owned_popup");
    HWND e = mk(0, WS_CHILD, a);                       /* w4: second child */
    HWND f = mk(0, WS_OVERLAPPED, a);                  /* w5: owned overlapped */
    log_begin();
    HWND m = mk(0, 0, HWND_MESSAGE);                   /* w6: message-only */
    log_case("create_messages_message_only");
    HWND mc = mk(0, WS_CHILD, m);                      /* w7: child of message-only */
    HWND np = mk(WS_EX_NOPARENTNOTIFY, WS_CHILD, a);   /* w8: child without parent notify */
    HWND g = mk(0, WS_CHILD, a);                       /* w9: third child */
    msgparent = pGetAncestor(m, GA_PARENT);
    oracle_case("all_created");
    o_bool("ok", a && b && c && d && e && f && m && mc && np && g);
    o_bool("message_parent_is_desktop", msgparent == desktop);
    o_bool("message_parent_is_null", msgparent == NULL);
    oracle_case_end();

    rel_case("rel_toplevel", a);
    rel_case("rel_child", c);
    rel_case("rel_grandchild", d);
    rel_case("rel_owned_popup", b);
    rel_case("rel_owned_overlapped", f);
    rel_case("rel_message_only", m);
    rel_case("rel_child_of_message_only", mc);
    {
        HWND r;
        ORACLE_CALL(r, pGetParent(BAD_HWND));
        oracle_case("getparent_bad_hwnd");
        o_hwnd("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, pGetAncestor(c, 0));
        oracle_case("getancestor_flag_0");
        o_hwnd("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, pGetAncestor(c, 4));
        oracle_case("getancestor_flag_4");
        o_hwnd("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, pGetWindow(a, 99));
        oracle_case("getwindow_bad_cmd");
        o_hwnd("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, pGetWindow(BAD_HWND, GW_CHILD));
        oracle_case("getwindow_bad_hwnd");
        o_hwnd("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, pGetWindow(d, GW_CHILD));
        oracle_case("getwindow_child_of_leaf");
        o_hwnd("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, pGetWindow(b, GW_ENABLEDPOPUP));
        oracle_case("getwindow_enabledpopup_of_popup");
        o_hwnd("ret", r);
        o_lasterr();
        oracle_case_end();
    }

    oracle_case("ischild");
    o_bool("a_c", pIsChild(a, c));
    o_bool("a_d", pIsChild(a, d));
    o_bool("a_b_owned", pIsChild(a, b));
    o_bool("c_a", pIsChild(c, a));
    o_bool("a_a", pIsChild(a, a));
    o_bool("desktop_a", pIsChild(desktop, a));
    o_bool("m_mc", pIsChild(m, mc));
    o_bool("bad_c", pIsChild(BAD_HWND, c));
    oracle_case_end();

    children_case("children_of_toplevel", a);
    children_case("children_of_child", c);
    children_case("children_of_leaf", d);

    {
        HWND r;
        ORACLE_CALL(r, pFindWindowExW(HWND_MESSAGE, NULL, L"SetuOracleWT", NULL));
        oracle_case("findwindowex_message");
        o_hwnd("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, pFindWindowExW(a, NULL, L"SetuOracleWT", NULL));
        oracle_case("findwindowex_first_child");
        o_hwnd("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, pFindWindowExW(a, r, L"SetuOracleWT", NULL));
        oracle_case("findwindowex_next_child");
        o_hwnd("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, pFindWindowExW(a, NULL, L"SetuOracleNoSuchClass", NULL));
        oracle_case("findwindowex_missing_class");
        o_hwnd("ret", r);
        o_lasterr();
        oracle_case_end();
    }

    /* ---- SetParent ---- */
    {
        HWND r;
        log_begin();
        ORACLE_CALL(r, pSetParent(d, e));
        log_case("setparent_messages");
        oracle_case("setparent_grandchild_to_sibling");
        o_hwnd("ret", r);
        o_lasterr();
        o_hwnd("parent_after", pGetParent(d));
        oracle_case_end();
        children_case("children_after_setparent", a);
        children_case("children_of_new_parent", e);

        ORACLE_CALL(r, pSetParent(a, d));
        oracle_case("setparent_to_own_descendant");
        o_hwnd("ret", r);
        o_lasterr();
        o_hwnd("parent_after", pGetParent(a));
        oracle_case_end();

        ORACLE_CALL(r, pSetParent(c, c));
        oracle_case("setparent_to_self");
        o_hwnd("ret", r);
        o_lasterr();
        oracle_case_end();

        ORACLE_CALL(r, pSetParent(c, BAD_HWND));
        oracle_case("setparent_bad_parent");
        o_hwnd("ret", r);
        o_lasterr();
        o_hwnd("parent_after", pGetParent(c));
        oracle_case_end();

        ORACLE_CALL(r, pSetParent(g, HWND_MESSAGE));
        oracle_case("setparent_child_to_message");
        o_hwnd("ret", r);
        o_lasterr();
        o_hwnd("ga_parent_after", pGetAncestor(g, GA_PARENT));
        o_hex32("style_after", (DWORD)pGetWindowLongPtrW(g, GWL_STYLE));
        oracle_case_end();

        ORACLE_CALL(r, pSetParent(g, NULL));
        oracle_case("setparent_to_null");
        o_hwnd("ret", r);
        o_lasterr();
        o_hwnd("ga_parent_after", pGetAncestor(g, GA_PARENT));
        o_hwnd("getparent_after", pGetParent(g));
        o_hex32("style_after", (DWORD)pGetWindowLongPtrW(g, GWL_STYLE));
        oracle_case_end();

        ORACLE_CALL(r, pSetParent(g, a));
        oracle_case("setparent_back");
        o_hwnd("ret", r);
        o_lasterr();
        o_hwnd("parent_after", pGetParent(g));
        oracle_case_end();
    }

    /* ---- destruction ---- */
    {
        BOOL r;
        log_begin();
        ORACLE_CALL(r, pDestroyWindow(np));
        log_case("destroy_messages_noparentnotify_child");
        oracle_case("destroy_noparentnotify_child");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();

        log_begin();
        ORACLE_CALL(r, pDestroyWindow(a));
        log_case("destroy_messages_toplevel_tree");
        oracle_case("destroy_toplevel_tree");
        o_bool("ret", r);
        o_lasterr();
        o_bool("a_alive", pIsWindow(a));
        o_bool("b_owned_alive", pIsWindow(b));
        o_bool("c_child_alive", pIsWindow(c));
        o_bool("d_grandchild_alive", pIsWindow(d));
        o_bool("e_child_alive", pIsWindow(e));
        o_bool("f_owned_alive", pIsWindow(f));
        o_bool("g_child_alive", pIsWindow(g));
        o_bool("m_unrelated_alive", pIsWindow(m));
        oracle_case_end();

        ORACLE_CALL(r, pDestroyWindow(a));
        oracle_case("destroy_twice");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();

        ORACLE_CALL(r, pIsWindow(NULL));
        oracle_case("iswindow_null");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();

        ORACLE_CALL(r, pIsWindow(BAD_HWND));
        oracle_case("iswindow_bad");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();

        ORACLE_CALL(r, pDestroyWindow(NULL));
        oracle_case("destroy_null");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();

        ORACLE_CALL(r, pDestroyWindow(desktop));
        oracle_case("destroy_desktop");
        o_bool("ret", r);
        o_lasterr();
        o_bool("desktop_alive", pIsWindow(desktop));
        oracle_case_end();

        log_begin();
        ORACLE_CALL(r, pDestroyWindow(m));
        log_case("destroy_messages_message_only");
        oracle_case("destroy_message_only");
        o_bool("ret", r);
        o_lasterr();
        o_bool("mc_alive", pIsWindow(mc));
        oracle_case_end();
    }

    /* DestroyWindow from another thread fails with ERROR_ACCESS_DENIED. */
    {
        HWND x = mk(0, 0, HWND_MESSAGE);
        xthread.h = x;
        HANDLE th = CreateThread(NULL, 0, destroy_thread, NULL, 0, NULL);
        WaitForSingleObject(th, 10000);
        CloseHandle(th);
        oracle_case("destroy_from_other_thread");
        o_bool("ret", xthread.r);
        if (xthread.err == ORACLE_SENTINEL) o_str("last_error", "unchanged");
        else o_u32("last_error", xthread.err);
        o_bool("alive_after", pIsWindow(x));
        oracle_case_end();
        pDestroyWindow(x);
    }

    return oracle_end();
}
