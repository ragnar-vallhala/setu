/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: window classes: RegisterClassExW/RegisterClassW error codes
 * and duplicates, GetClassInfoExW for own and system classes,
 * GetClassNameW sizing, GetClassLongPtrW/SetClassLongPtrW, UnregisterClassW,
 * and CreateWindowExW failures caused by bad classes or parents.
 *
 * Only message-only (HWND_MESSAGE) and hidden windows are created; nothing
 * depends on the screen, fonts or the window manager. Atoms and handles are
 * compared or range-checked, never printed.
 *
 * user32 is loaded with LoadLibraryW and every function is fetched with
 * GetProcAddress (tests link only kernel32/advapi32 explicitly).
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerclassexw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerclassw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-wndclassexw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getclassinfoexw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getclassnamew
 *       https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getclasslongptrw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setclasslongptrw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-unregisterclassw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-createwindowexw
 *       https://learn.microsoft.com/en-us/windows/win32/winmsg/about-window-classes
 *       https://learn.microsoft.com/en-us/windows/win32/winmsg/window-class-styles
 *       https://learn.microsoft.com/en-us/windows/win32/debug/system-error-codes--1300-1699-
 */
#include "oracle.h"

#define DECL(f) static __typeof__(f) *p##f
#define LOAD(m, f) (*(FARPROC *)&p##f = GetProcAddress(m, #f))

DECL(RegisterClassExW);
DECL(RegisterClassW);
DECL(RegisterClassA);
DECL(UnregisterClassW);
DECL(GetClassInfoExW);
DECL(GetClassNameW);
DECL(GetClassLongPtrW);
DECL(SetClassLongPtrW);
DECL(CreateWindowExW);
DECL(DestroyWindow);
DECL(DefWindowProcW);
DECL(IsWindow);

static HINSTANCE self;

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    return pDefWindowProcW(h, m, w, l);
}

static WNDCLASSEXW mkclass(const wchar_t *name)
{
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = wndproc;
    wc.hInstance = self;
    wc.lpszClassName = name;
    return wc;
}

static int is_class_atom(ATOM a) { return a >= 0xC000; }

static ATOM reg_case(const char *name, const WNDCLASSEXW *wc)
{
    ATOM a;
    ORACLE_CALL(a, pRegisterClassExW(wc));
    oracle_case(name);
    o_bool("ret_nonzero", a != 0);
    o_bool("ret_is_class_atom", is_class_atom(a));
    o_lasterr();
    oracle_case_end();
    return a;
}

static void info_case(const char *name, HINSTANCE inst, const wchar_t *cls, ATOM expect)
{
    WNDCLASSEXW wc;
    BOOL r;
    memset(&wc, 0xcc, sizeof wc);
    wc.cbSize = sizeof wc;
    ORACLE_CALL(r, pGetClassInfoExW(inst, cls, &wc));
    oracle_case(name);
    o_bool("ret_nonzero", r != 0);
    if (expect) o_bool("ret_is_atom", (ATOM)r == expect);
    else o_bool("ret_is_class_atom", r && is_class_atom((ATOM)r));
    o_lasterr();
    if (r) {
        o_u32("cbSize", wc.cbSize);
        o_hex32("style", wc.style);
        o_i64("cbClsExtra", wc.cbClsExtra);
        o_i64("cbWndExtra", wc.cbWndExtra);
        o_bool("hinstance_is_self", wc.hInstance == self);
        o_bool("hinstance_null", wc.hInstance == NULL);
        o_bool("wndproc_is_ours", wc.lpfnWndProc == wndproc);
        o_bool("classname_ptr_is_input", wc.lpszClassName == cls);
        o_bool("menuname_null", wc.lpszMenuName == NULL);
    }
    oracle_case_end();
}

static HWND create_case(const char *name, const wchar_t *cls, DWORD style, HWND parent)
{
    HWND h;
    ORACLE_CALL(h, pCreateWindowExW(0, cls, L"t", style, 0, 0, 10, 10, parent, NULL, self, NULL));
    oracle_case(name);
    o_handle("ret", h);
    o_lasterr();
    oracle_case_end();
    return h;
}

static void unreg_case(const char *name, const wchar_t *cls, HINSTANCE inst)
{
    BOOL r;
    ORACLE_CALL(r, pUnregisterClassW(cls, inst));
    oracle_case(name);
    o_bool("ret", r);
    o_lasterr();
    oracle_case_end();
}

int main(void)
{
    oracle_begin("user32/window_class");
    HMODULE u32;
    ORACLE_CALL(u32, LoadLibraryW(L"user32.dll"));
    oracle_case("load_user32");
    o_handle("ret", u32);
    oracle_case_end();
    if (!u32) return oracle_end();
    LOAD(u32, RegisterClassExW);
    LOAD(u32, RegisterClassW);
    LOAD(u32, RegisterClassA);
    LOAD(u32, UnregisterClassW);
    LOAD(u32, GetClassInfoExW);
    LOAD(u32, GetClassNameW);
    LOAD(u32, GetClassLongPtrW);
    LOAD(u32, SetClassLongPtrW);
    LOAD(u32, CreateWindowExW);
    LOAD(u32, DestroyWindow);
    LOAD(u32, DefWindowProcW);
    LOAD(u32, IsWindow);
    self = GetModuleHandleW(NULL);

    /* ---- registration ---- */
    WNDCLASSEXW wc = mkclass(L"SetuOracleWC_Main");
    wc.style = CS_DBLCLKS | CS_HREDRAW;
    wc.cbClsExtra = 16;
    wc.cbWndExtra = 24;
    ATOM main_atom = reg_case("register_basic", &wc);
    reg_case("register_duplicate", &wc);
    WNDCLASSEXW wcu = mkclass(L"SETUORACLEWC_MAIN");
    reg_case("register_duplicate_other_case", &wcu);

    {
        WNDCLASSW w1;
        ATOM a;
        memset(&w1, 0, sizeof w1);
        w1.lpfnWndProc = wndproc;
        w1.hInstance = self;
        w1.lpszClassName = L"SetuOracleWC_Main";
        ORACLE_CALL(a, pRegisterClassW(&w1));
        oracle_case("register_w_duplicate_of_ex");
        o_bool("ret_nonzero", a != 0);
        o_lasterr();
        oracle_case_end();
    }
    {
        WNDCLASSA wa;
        ATOM a;
        memset(&wa, 0, sizeof wa);
        wa.lpfnWndProc = wndproc;
        wa.hInstance = self;
        wa.lpszClassName = "SetuOracleWC_Main";
        ORACLE_CALL(a, pRegisterClassA(&wa));
        oracle_case("register_a_duplicate_of_w");
        o_bool("ret_nonzero", a != 0);
        o_lasterr();
        oracle_case_end();
    }

    WNDCLASSEXW bad = mkclass(L"SetuOracleWC_BadSize");
    bad.cbSize = 0;
    reg_case("register_cbsize_zero", &bad);
    bad.cbSize = sizeof bad + 4;
    reg_case("register_cbsize_too_big", &bad);
    bad.cbSize = sizeof bad - 4;
    reg_case("register_cbsize_too_small", &bad);

    WNDCLASSEXW neg = mkclass(L"SetuOracleWC_NegCls");
    neg.cbClsExtra = -1;
    reg_case("register_negative_cbclsextra", &neg);
    neg = mkclass(L"SetuOracleWC_NegWnd");
    neg.cbWndExtra = -1;
    reg_case("register_negative_cbwndextra", &neg);
    WNDCLASSEXW big = mkclass(L"SetuOracleWC_BigExtra");
    big.cbWndExtra = 40000;
    ATOM big_atom = reg_case("register_cbwndextra_40000", &big);

    WNDCLASSEXW empty = mkclass(L"");
    ATOM empty_atom = reg_case("register_empty_name", &empty);

    wchar_t longname[300];
    for (int i = 0; i < 299; i++) longname[i] = L'a' + (i % 26);
    longname[299] = 0;
    WNDCLASSEXW lng = mkclass(longname);
    ATOM long_atom = reg_case("register_name_299_chars", &lng);
    longname[255] = 0;
    WNDCLASSEXW lng2 = mkclass(longname);
    ATOM long2_atom = reg_case("register_name_255_chars", &lng2);

    WNDCLASSEXW null_inst = mkclass(L"SetuOracleWC_NullInst");
    null_inst.hInstance = NULL;
    ATOM ni_atom = reg_case("register_null_hinstance", &null_inst);
    info_case("info_null_hinstance_class_by_self", self, L"SetuOracleWC_NullInst", ni_atom);
    info_case("info_null_hinstance_class_by_null", NULL, L"SetuOracleWC_NullInst", ni_atom);

    WNDCLASSEXW button = mkclass(L"Button");
    ATOM button_atom = reg_case("register_local_named_button", &button);

    WNDCLASSEXW glob = mkclass(L"SetuOracleWC_Global");
    glob.style = CS_GLOBALCLASS;
    ATOM glob_atom = reg_case("register_globalclass", &glob);

    /* ---- GetClassInfoExW ---- */
    info_case("info_own_class", self, L"SetuOracleWC_Main", main_atom);
    info_case("info_own_class_other_case", self, L"setuoraclewc_main", main_atom);
    info_case("info_own_class_by_atom", self, (LPCWSTR)MAKEINTATOM(main_atom), main_atom);
    info_case("info_own_class_null_hinstance", NULL, L"SetuOracleWC_Main", main_atom);
    info_case("info_missing_class", self, L"SetuOracleWC_Missing", 0);
    info_case("info_global_class_null_hinstance", NULL, L"SetuOracleWC_Global", glob_atom);
    info_case("info_button_null_hinstance", NULL, L"Button", 0);
    info_case("info_button_self_local_override", self, L"Button", button_atom);
    info_case("info_static_null_hinstance", NULL, L"Static", 0);
    info_case("info_edit_null_hinstance", NULL, L"Edit", 0);
    info_case("info_dialog_null_hinstance", NULL, L"#32770", 0);
    info_case("info_bogus_hinstance", (HINSTANCE)(ULONG_PTR)0x12340000, L"SetuOracleWC_Main", main_atom);
    {
        WNDCLASSEXW w;
        BOOL r;
        memset(&w, 0, sizeof w);
        w.cbSize = 0;
        ORACLE_CALL(r, pGetClassInfoExW(self, L"SetuOracleWC_Main", &w));
        oracle_case("info_cbsize_zero");
        o_bool("ret_nonzero", r != 0);
        o_lasterr();
        o_u32("cbSize_after", w.cbSize);
        oracle_case_end();
    }

    /* ---- windows of the class ---- */
    HWND w1 = create_case("create_message_only", L"SetuOracleWC_Main", 0, HWND_MESSAGE);
    create_case("create_missing_class", L"SetuOracleWC_Missing", 0, HWND_MESSAGE);
    create_case("create_missing_class_atom", (LPCWSTR)MAKEINTATOM(0xBEEF), 0, HWND_MESSAGE);
    HWND w2 = create_case("create_by_atom", (LPCWSTR)MAKEINTATOM(main_atom), 0, HWND_MESSAGE);
    create_case("create_child_null_parent", L"SetuOracleWC_Main", WS_CHILD, NULL);
    create_case("create_bogus_parent", L"SetuOracleWC_Main", WS_CHILD, (HWND)(ULONG_PTR)0x0bad0bad);
    create_case("create_popup_bogus_owner", L"SetuOracleWC_Main", WS_POPUP, (HWND)(ULONG_PTR)0x0bad0bad);
    HWND child = create_case("create_child_of_message_window", L"SetuOracleWC_Main", WS_CHILD, w1);
    HWND w3 = create_case("create_empty_name_class", L"", 0, HWND_MESSAGE);
    HWND w4 = create_case("create_big_extra_class", L"SetuOracleWC_BigExtra", 0, HWND_MESSAGE);
    HWND wb = create_case("create_local_button", L"Button", 0, HWND_MESSAGE);
    HWND top = create_case("create_hidden_toplevel", L"SetuOracleWC_Main", WS_OVERLAPPEDWINDOW, NULL);

    {
        HWND dead;
        dead = pCreateWindowExW(0, L"SetuOracleWC_Main", L"d", 0, 0, 0, 1, 1, HWND_MESSAGE, NULL, self, NULL);
        pDestroyWindow(dead);
        create_case("create_destroyed_parent", L"SetuOracleWC_Main", WS_CHILD, dead);
    }

    /* ---- GetClassNameW ---- */
    {
        static const int sizes[] = {256, 18, 17, 2, 1};
        for (size_t i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
            wchar_t buf[300];
            int r;
            char nm[64];
            wmemset(buf, L'#', 300);
            ORACLE_CALL(r, pGetClassNameW(w1, buf, sizes[i]));
            snprintf(nm, sizeof nm, "classname_buf_%d", sizes[i]);
            oracle_case(nm);
            o_i64("ret", r);
            o_lasterr();
            buf[299] = 0;
            if (sizes[i] > 0) buf[sizes[i]] = 0; /* show the terminator position */
            o_wstr("buf", buf);
            oracle_case_end();
        }
        wchar_t buf[300];
        int r;
        wmemset(buf, L'#', 300);
        ORACLE_CALL(r, pGetClassNameW(w1, buf, 0));
        oracle_case("classname_buf_0");
        o_i64("ret", r);
        o_lasterr();
        o_bool("buf_untouched", buf[0] == L'#');
        oracle_case_end();

        ORACLE_CALL(r, pGetClassNameW((HWND)(ULONG_PTR)0x0bad0bad, buf, 300));
        oracle_case("classname_bad_hwnd");
        o_i64("ret", r);
        o_lasterr();
        oracle_case_end();

        ORACLE_CALL(r, pGetClassNameW(w3, buf, 300));
        oracle_case("classname_empty_class");
        o_i64("ret", r);
        o_lasterr();
        if (r >= 0) { buf[r] = 0; o_wstr("buf", buf); }
        oracle_case_end();

        ORACLE_CALL(r, pGetClassNameW(wb, buf, 300));
        oracle_case("classname_local_button");
        o_i64("ret", r);
        o_lasterr();
        if (r >= 0) { buf[r] = 0; o_wstr("buf", buf); }
        oracle_case_end();
    }

    /* ---- GetClassLongPtrW / SetClassLongPtrW ---- */
    {
        static const struct { const char *name; int idx; } idx[] = {
            {"classlong_atom", GCW_ATOM},
            {"classlong_style", GCL_STYLE},
            {"classlong_cbclsextra", GCL_CBCLSEXTRA},
            {"classlong_cbwndextra", GCL_CBWNDEXTRA},
            {"classlong_extra_0", 0},
            {"classlong_extra_8", 8},
            {"classlong_extra_9", 9},
            {"classlong_extra_16", 16},
            {"classlong_bad_negative", -100},
            {"classlong_bad_large", 4096},
        };
        for (size_t i = 0; i < sizeof idx / sizeof idx[0]; i++) {
            ULONG_PTR v;
            ORACLE_CALL(v, pGetClassLongPtrW(w1, idx[i].idx));
            oracle_case(idx[i].name);
            if (idx[i].idx == GCW_ATOM) o_bool("ret_is_atom", v == main_atom);
            else o_u64("ret", v);
            o_lasterr();
            oracle_case_end();
        }
        ULONG_PTR v;
        ORACLE_CALL(v, pGetClassLongPtrW(w1, GCLP_WNDPROC));
        oracle_case("classlong_wndproc");
        o_bool("ret_is_ours", v == (ULONG_PTR)wndproc);
        o_lasterr();
        oracle_case_end();

        ORACLE_CALL(v, pGetClassLongPtrW(w1, GCLP_HMODULE));
        oracle_case("classlong_hmodule");
        o_bool("ret_is_self", v == (ULONG_PTR)self);
        o_lasterr();
        oracle_case_end();

        ORACLE_CALL(v, pSetClassLongPtrW(w1, 0, 0x1122334455667788ull));
        oracle_case("set_classlong_extra_0");
        o_u64("ret", v);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(v, pGetClassLongPtrW(w2, 0));
        oracle_case("classlong_extra_0_shared_by_other_window");
        o_hex32("ret_lo", (DWORD)v);
        o_hex32("ret_hi", (DWORD)(v >> 32));
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(v, pSetClassLongPtrW(w1, 0, 5));
        oracle_case("set_classlong_extra_0_again");
        o_hex32("ret_lo", (DWORD)v);
        o_hex32("ret_hi", (DWORD)(v >> 32));
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(v, pSetClassLongPtrW(w1, GCL_STYLE, CS_DBLCLKS));
        oracle_case("set_classlong_style");
        o_hex32("ret", (DWORD)v);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(v, pGetClassLongPtrW(w2, GCL_STYLE));
        oracle_case("classlong_style_after_set");
        o_hex32("ret", (DWORD)v);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(v, pGetClassLongPtrW((HWND)(ULONG_PTR)0x0bad0bad, GCL_STYLE));
        oracle_case("classlong_bad_hwnd");
        o_u64("ret", v);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(v, pGetClassLongPtrW(w4, GCL_CBWNDEXTRA));
        oracle_case("classlong_big_extra_cbwndextra");
        o_u64("ret", v);
        o_lasterr();
        oracle_case_end();
    }

    /* ---- UnregisterClassW ---- */
    unreg_case("unregister_with_windows", L"SetuOracleWC_Main", self);
    unreg_case("unregister_missing", L"SetuOracleWC_Missing", self);
    unreg_case("unregister_system_button_null_inst", L"Static", NULL);
    unreg_case("unregister_wrong_hinstance", L"SetuOracleWC_BigExtra", (HINSTANCE)(ULONG_PTR)0x12340000);
    pDestroyWindow(w4);
    unreg_case("unregister_after_destroy", L"SetuOracleWC_BigExtra", self);
    unreg_case("unregister_twice", L"SetuOracleWC_BigExtra", self);
    (void)big_atom;
    {
        WNDCLASSEXW again = mkclass(L"SetuOracleWC_BigExtra");
        reg_case("register_after_unregister", &again);
        unreg_case("unregister_by_atom_of_new", L"SetuOracleWC_BigExtra", self);
    }
    pDestroyWindow(wb);
    unreg_case("unregister_local_button", L"Button", self);
    (void)button_atom;
    info_case("info_button_after_local_unregister", self, L"Button", 0);
    pDestroyWindow(w3);
    unreg_case("unregister_empty_name", L"", self);
    (void)empty_atom;
    unreg_case("unregister_global", L"SetuOracleWC_Global", NULL);
    unreg_case("unregister_null_hinst_class", L"SetuOracleWC_NullInst", NULL);
    unreg_case("unregister_long_255", longname, self);
    (void)long_atom;
    (void)long2_atom;

    oracle_case("destroy_parent_then_child_gone");
    o_bool("destroy_top", pDestroyWindow(top));
    o_bool("destroy_w1", pDestroyWindow(w1));
    o_bool("child_is_window", pIsWindow(child));
    oracle_case_end();
    pDestroyWindow(w2);
    unreg_case("unregister_after_all_destroyed", L"SetuOracleWC_Main", self);
    info_case("info_after_unregister", self, L"SetuOracleWC_Main", 0);

    /* ---- large window extra bytes ---- */
    {
        static const struct { const char *reg, *create, *get; int extra; } big_cases[] = {
            {"register_cbwndextra_65536", "create_cbwndextra_65536", "classlong_cbwndextra_65536", 65536},
            {"register_cbwndextra_1m", "create_cbwndextra_1m", "classlong_cbwndextra_1m", 1 << 20},
        };
        for (size_t i = 0; i < sizeof big_cases / sizeof big_cases[0]; i++) {
            WNDCLASSEXW bc = mkclass(L"SetuOracleWC_Huge");
            bc.cbWndExtra = big_cases[i].extra;
            reg_case(big_cases[i].reg, &bc);
            HWND hw = create_case(big_cases[i].create, L"SetuOracleWC_Huge", 0, HWND_MESSAGE);
            if (hw) {
                ULONG_PTR v;
                ORACLE_CALL(v, pGetClassLongPtrW(hw, GCL_CBWNDEXTRA));
                oracle_case(big_cases[i].get);
                o_u64("ret", v);
                o_lasterr();
                oracle_case_end();
                pDestroyWindow(hw);
            }
            pUnregisterClassW(L"SetuOracleWC_Huge", self);
        }
    }

    /* ---- SetClassLongPtrW rejections (last: these once crashed Setu's wineserver) ---- */
    {
        WNDCLASSEXW lc = mkclass(L"SetuOracleWC_Last");
        lc.cbClsExtra = 16;
        lc.cbWndExtra = 8;
        pRegisterClassExW(&lc);
        HWND hl = pCreateWindowExW(0, L"SetuOracleWC_Last", L"l", 0, 0, 0, 1, 1, HWND_MESSAGE, NULL, self, NULL);
        ULONG_PTR v;

        ORACLE_CALL(v, pSetClassLongPtrW(hl, 4096, 1));
        oracle_case("set_classlong_bad_offset");
        o_u64("ret", v);
        o_lasterr();
        oracle_case_end();

        ORACLE_CALL(v, pSetClassLongPtrW(hl, 12, 1));
        oracle_case("set_classlong_offset_straddles_end");
        o_u64("ret", v);
        o_lasterr();
        oracle_case_end();

        ORACLE_CALL(v, pSetClassLongPtrW(hl, GCL_CBWNDEXTRA, 5000));
        oracle_case("set_classlong_cbwndextra_5000");
        o_u64("ret", v);
        o_lasterr();
        oracle_case_end();

        ORACLE_CALL(v, pGetClassLongPtrW(hl, GCL_CBWNDEXTRA));
        oracle_case("classlong_cbwndextra_after_set_5000");
        o_u64("ret", v);
        o_lasterr();
        oracle_case_end();

        ORACLE_CALL(v, pSetClassLongPtrW(hl, GCL_STYLE, CS_DBLCLKS));
        oracle_case("set_classlong_style_after_rejections");
        o_hex32("ret", (DWORD)v);
        o_lasterr();
        oracle_case_end();

        ORACLE_CALL(v, pSetClassLongPtrW(hl, 8, 7));
        oracle_case("set_classlong_extra_8_after_rejections");
        o_u64("ret", v);
        o_lasterr();
        o_u64("get_after", pGetClassLongPtrW(hl, 8));
        oracle_case_end();

        pDestroyWindow(hl);
        unreg_case("unregister_last", L"SetuOracleWC_Last", self);
    }

    return oracle_end();
}
