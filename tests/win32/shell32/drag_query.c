/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: reading a CF_HDROP file list, as a desktop or file manager
 * does when files are dropped or pasted. The test builds DROPFILES blocks
 * by hand (wide and ANSI file lists, a drop point, client and non-client
 * flags) and reads them with DragQueryFileW / DragQueryFileA (count with
 * 0xFFFFFFFF, length with a NULL buffer, an index past the end, short
 * buffers) and DragQueryPoint. No window or drag loop is involved.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-dragqueryfilew
 *       https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-dragquerypoint
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/ns-shlobj_core-dropfiles
 *       https://learn.microsoft.com/en-us/windows/win32/shell/clipboard#cf_hdrop
 */
#include "oracle.h"
#include <shlobj.h>
#include <shellapi.h>

static UINT(WINAPI *pDragQueryFileW)(HDROP, UINT, LPWSTR, UINT);
static UINT(WINAPI *pDragQueryFileA)(HDROP, UINT, LPSTR, UINT);
static BOOL(WINAPI *pDragQueryPoint)(HDROP, POINT *);

#define LOAD(m, f) (*(FARPROC *)&p##f = GetProcAddress(m, #f))

/* A DROPFILES block with the double-NUL terminated list LIST (LEN bytes). */
static HDROP make_drop(const void *list, size_t len, BOOL wide, LONG x, LONG y, BOOL nc)
{
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DROPFILES) + len);
    DROPFILES *df = GlobalLock(h);
    df->pFiles = sizeof(DROPFILES);
    df->pt.x = x;
    df->pt.y = y;
    df->fNC = nc;
    df->fWide = wide;
    memcpy((char *)df + sizeof(DROPFILES), list, len);
    GlobalUnlock(h);
    return (HDROP)h;
}

static const wchar_t wlist[] = L"C:\\dir\\one.txt\0C:\\My Files\\two words.doc\0\\\\srv\\share\\x\0";
static const char alist[] = "C:\\a\\b.txt\0D:\\c.dat\0";

int main(void)
{
    oracle_begin("shell32/drag_query");
    HMODULE sh = LoadLibraryW(L"shell32.dll");
    LOAD(sh, DragQueryFileW);
    LOAD(sh, DragQueryFileA);
    LOAD(sh, DragQueryPoint);
    BOOL loaded = pDragQueryFileW && pDragQueryFileA && pDragQueryPoint;
    oracle_case("setup");
    o_bool("loaded", loaded);
    oracle_case_end();
    if (!loaded) return oracle_end();

    HDROP w = make_drop(wlist, sizeof wlist, TRUE, 10, 20, FALSE);
    HDROP a = make_drop(alist, sizeof alist, FALSE, -5, 300, TRUE);
    wchar_t buf[MAX_PATH];
    char abuf[MAX_PATH];
    UINT r;
    POINT pt;
    BOOL b;

    oracle_case("wide_count");
    o_u32("count", pDragQueryFileW(w, 0xFFFFFFFF, NULL, 0));
    o_u32("count_with_buffer", pDragQueryFileW(w, 0xFFFFFFFF, buf, MAX_PATH));
    oracle_case_end();

    for (UINT i = 0; i < 4; i++) {
        char n[32];
        snprintf(n, sizeof n, "wide_file_%u", i);
        oracle_case(n);
        o_u32("len", pDragQueryFileW(w, i, NULL, 0));
        wmemset(buf, L'#', MAX_PATH);
        r = pDragQueryFileW(w, i, buf, MAX_PATH);
        o_u32("ret", r);
        o_wstr("name", r ? buf : NULL);
        oracle_case_end();
    }

    oracle_case("wide_short_buffer");
    wmemset(buf, L'#', MAX_PATH);
    r = pDragQueryFileW(w, 0, buf, 5);
    o_u32("ret", r);
    buf[10] = 0;
    o_wstr("buf", buf);
    oracle_case_end();

    oracle_case("wide_read_ansi");
    memset(abuf, '#', MAX_PATH);
    r = pDragQueryFileA(w, 1, abuf, MAX_PATH);
    o_u32("ret", r);
    o_str("name", r ? abuf : NULL);
    o_u32("count", pDragQueryFileA(w, 0xFFFFFFFF, NULL, 0));
    oracle_case_end();

    oracle_case("ansi_list");
    o_u32("count", pDragQueryFileW(a, 0xFFFFFFFF, NULL, 0));
    wmemset(buf, L'#', MAX_PATH);
    r = pDragQueryFileW(a, 1, buf, MAX_PATH);
    o_u32("ret", r);
    o_wstr("name_w", r ? buf : NULL);
    memset(abuf, '#', MAX_PATH);
    r = pDragQueryFileA(a, 0, abuf, MAX_PATH);
    o_u32("ret_a", r);
    o_str("name_a", r ? abuf : NULL);
    o_u32("len_past_end", pDragQueryFileW(a, 2, NULL, 0));
    oracle_case_end();

    oracle_case("point_client");
    memset(&pt, 0x55, sizeof pt);
    b = pDragQueryPoint(w, &pt);
    o_bool("in_client", b);
    o_i64("x", pt.x);
    o_i64("y", pt.y);
    oracle_case_end();

    oracle_case("point_nonclient");
    memset(&pt, 0x55, sizeof pt);
    b = pDragQueryPoint(a, &pt);
    o_bool("in_client", b);
    o_i64("x", pt.x);
    o_i64("y", pt.y);
    oracle_case_end();

    {
        static const wchar_t empty[] = L"\0";
        HDROP e = make_drop(empty, sizeof empty, TRUE, 0, 0, FALSE);
        oracle_case("empty_list");
        o_u32("count", pDragQueryFileW(e, 0xFFFFFFFF, NULL, 0));
        o_u32("len_0", pDragQueryFileW(e, 0, NULL, 0));
        oracle_case_end();
        GlobalFree(e);
    }

    GlobalFree(w);
    GlobalFree(a);
    return oracle_end();
}
