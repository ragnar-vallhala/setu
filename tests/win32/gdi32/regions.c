/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: GDI regions: CreateRectRgn normalisation, CombineRgn with
 * every mode (result type, bounding box and the exact rectangle list from
 * GetRegionData), OffsetRgn, SetRectRgn, EqualRgn, PtInRegion, RectInRegion,
 * polygon regions (ALTERNATE vs WINDING), small elliptic/round-rect regions,
 * ExtCreateRegion round trips, and clipping on a memory DC (SelectClipRgn,
 * ExtSelectClipRgn, IntersectClipRect, ExcludeClipRect, GetClipBox,
 * GetClipRgn).
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-createrectrgn
 *       https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-combinergn
 *       https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-getrgnbox
 *       https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-getregiondata
 *       https://learn.microsoft.com/en-us/windows/win32/api/wingdi/ns-wingdi-rgndata
 *       https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-offsetrgn
 *       https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-setrectrgn
 *       https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-equalrgn
 *       https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-ptinregion
 *       https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-rectinregion
 *       https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-createpolygonrgn
 *       https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-createellipticrgn
 *       https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-createroundrectrgn
 *       https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-extcreateregion
 *       https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-selectcliprgn
 *       https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-extselectcliprgn
 *       https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-intersectcliprect
 *       https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-excludecliprect
 *       https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-getclipbox
 *       https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-getcliprgn
 */
#include "oracle.h"

#define DECL(f) static __typeof__(f) *p##f
#define LOAD(m, f) (*(FARPROC *)&p##f = GetProcAddress(m, #f))

DECL(CreateRectRgn);
DECL(CreateRectRgnIndirect);
DECL(CreateEllipticRgn);
DECL(CreateRoundRectRgn);
DECL(CreatePolygonRgn);
DECL(CombineRgn);
DECL(GetRgnBox);
DECL(GetRegionData);
DECL(OffsetRgn);
DECL(SetRectRgn);
DECL(EqualRgn);
DECL(PtInRegion);
DECL(RectInRegion);
DECL(ExtCreateRegion);
DECL(DeleteObject);
DECL(GetObjectType);
DECL(CreateCompatibleDC);
DECL(DeleteDC);
DECL(SelectClipRgn);
DECL(ExtSelectClipRgn);
DECL(IntersectClipRect);
DECL(ExcludeClipRect);
DECL(GetClipBox);
DECL(GetClipRgn);
DECL(CreateDIBSection);
DECL(SelectObject);
DECL(SelectClipPath);

/* Region as "type box [r1;r2;...]" fields. */
static void rgn_fields(HRGN r)
{
    RECT box;
    memset(&box, 0, sizeof box);
    int t = pGetRgnBox(r, &box);
    char b[64];
    o_i64("type", t);
    snprintf(b, sizeof b, "%ld,%ld,%ld,%ld", box.left, box.top, box.right, box.bottom);
    o_str("box", b);
    DWORD sz = pGetRegionData(r, 0, NULL);
    o_u32("data_size", sz);
    if (sz && sz < 65536) {
        RGNDATA *d = malloc(sz);
        if (pGetRegionData(r, sz, d) == sz) {
            char out[2048] = "";
            RECT *rc = (RECT *)d->Buffer;
            o_u32("nCount", d->rdh.nCount);
            o_u32("iType", d->rdh.iType);
            o_u32("nRgnSize", d->rdh.nRgnSize);
            for (DWORD i = 0; i < d->rdh.nCount && i < 64; i++) {
                char e[64];
                snprintf(e, sizeof e, "%s%ld,%ld,%ld,%ld", i ? ";" : "", rc[i].left, rc[i].top, rc[i].right,
                         rc[i].bottom);
                strncat(out, e, sizeof out - strlen(out) - 1);
            }
            if (d->rdh.nCount > 64) strncat(out, ";...", sizeof out - strlen(out) - 1);
            o_str("rects", out);
        } else {
            o_str("rects", "getregiondata failed");
        }
        free(d);
    }
}

static void rgn_case(const char *name, HRGN r)
{
    oracle_case(name);
    o_handle("handle", r);
    if (r) rgn_fields(r);
    oracle_case_end();
}

static HRGN R(int l, int t, int r, int b) { return pCreateRectRgn(l, t, r, b); }

static void combine_case(const char *name, HRGN a, HRGN b, int mode)
{
    HRGN dst = R(0, 0, 0, 0);
    int r;
    ORACLE_CALL(r, pCombineRgn(dst, a, b, mode));
    oracle_case(name);
    o_i64("ret", r);
    o_lasterr();
    rgn_fields(dst);
    oracle_case_end();
    pDeleteObject(dst);
}

static void clip_fields(HDC dc)
{
    RECT box;
    char b[64];
    memset(&box, 0, sizeof box);
    int t = pGetClipBox(dc, &box);
    o_i64("clipbox_type", t);
    snprintf(b, sizeof b, "%ld,%ld,%ld,%ld", box.left, box.top, box.right, box.bottom);
    o_str("clipbox", b);
    HRGN q = R(0, 0, 0, 0);
    o_i64("getcliprgn", pGetClipRgn(dc, q));
    pDeleteObject(q);
}

int main(void)
{
    oracle_begin("gdi32/regions");
    HMODULE g;
    ORACLE_CALL(g, LoadLibraryW(L"gdi32.dll"));
    oracle_case("load_gdi32");
    o_handle("ret", g);
    oracle_case_end();
    if (!g) return oracle_end();
    LOAD(g, CreateRectRgn);
    LOAD(g, CreateRectRgnIndirect);
    LOAD(g, CreateEllipticRgn);
    LOAD(g, CreateRoundRectRgn);
    LOAD(g, CreatePolygonRgn);
    LOAD(g, CombineRgn);
    LOAD(g, GetRgnBox);
    LOAD(g, GetRegionData);
    LOAD(g, OffsetRgn);
    LOAD(g, SetRectRgn);
    LOAD(g, EqualRgn);
    LOAD(g, PtInRegion);
    LOAD(g, RectInRegion);
    LOAD(g, ExtCreateRegion);
    LOAD(g, DeleteObject);
    LOAD(g, GetObjectType);
    LOAD(g, CreateCompatibleDC);
    LOAD(g, DeleteDC);
    LOAD(g, SelectClipRgn);
    LOAD(g, ExtSelectClipRgn);
    LOAD(g, IntersectClipRect);
    LOAD(g, ExcludeClipRect);
    LOAD(g, GetClipBox);
    LOAD(g, GetClipRgn);
    LOAD(g, CreateDIBSection);
    LOAD(g, SelectObject);
    LOAD(g, SelectClipPath);

    /* ---- creation ---- */
    HRGN a = R(0, 0, 10, 10);
    rgn_case("rect_basic", a);
    oracle_case("rect_object_type");
    o_u32("type", pGetObjectType(a));
    oracle_case_end();
    rgn_case("rect_reversed_coords", R(10, 10, 0, 0));
    rgn_case("rect_empty_width", R(5, 5, 5, 10));
    rgn_case("rect_empty_height", R(5, 5, 10, 5));
    rgn_case("rect_negative", R(-20, -10, -5, -1));
    rgn_case("rect_huge", R(-0x7fffffff, -0x7fffffff, 0x7fffffff, 0x7fffffff));
    rgn_case("rect_large_2_27", R(-(1 << 27), -(1 << 27), 1 << 27, 1 << 27));
    {
        RECT rc = {3, 4, 1, 2};
        rgn_case("rect_indirect_reversed", pCreateRectRgnIndirect(&rc));
    }

    /* ---- CombineRgn ---- */
    HRGN b = R(5, 5, 15, 15);
    HRGN far_ = R(20, 20, 30, 30);
    HRGN empty = R(0, 0, 0, 0);
    combine_case("combine_and_overlap", a, b, RGN_AND);
    combine_case("combine_or_overlap", a, b, RGN_OR);
    combine_case("combine_xor_overlap", a, b, RGN_XOR);
    combine_case("combine_diff_overlap", a, b, RGN_DIFF);
    combine_case("combine_diff_reverse", b, a, RGN_DIFF);
    combine_case("combine_copy", a, NULL, RGN_COPY);
    combine_case("combine_and_disjoint", a, far_, RGN_AND);
    combine_case("combine_or_disjoint", a, far_, RGN_OR);
    combine_case("combine_xor_identical", a, a, RGN_XOR);
    combine_case("combine_diff_contained", R(0, 0, 10, 10), R(3, 3, 6, 6), RGN_DIFF);
    combine_case("combine_or_adjacent_horizontal", R(0, 0, 5, 5), R(5, 0, 10, 5), RGN_OR);
    combine_case("combine_or_adjacent_vertical", R(0, 0, 5, 5), R(0, 5, 5, 10), RGN_OR);
    combine_case("combine_or_with_empty", a, empty, RGN_OR);
    combine_case("combine_and_with_empty", a, empty, RGN_AND);
    combine_case("combine_mode_0", a, b, 0);
    combine_case("combine_mode_6", a, b, 6);
    {
        int r;
        HRGN dead = R(0, 0, 1, 1);
        pDeleteObject(dead);
        HRGN dst = R(0, 0, 0, 0);
        ORACLE_CALL(r, pCombineRgn(dst, a, dead, RGN_OR));
        oracle_case("combine_deleted_source");
        o_i64("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, pCombineRgn(dst, a, NULL, RGN_OR));
        oracle_case("combine_null_source_or");
        o_i64("ret", r);
        o_lasterr();
        oracle_case_end();
        /* destination identical to a source */
        HRGN s = R(0, 0, 4, 4);
        ORACLE_CALL(r, pCombineRgn(s, s, R(2, 2, 6, 6), RGN_OR));
        oracle_case("combine_dest_is_source");
        o_i64("ret", r);
        rgn_fields(s);
        oracle_case_end();
        /* L shape then more: canonical banding */
        HRGN l = R(0, 0, 0, 0);
        pCombineRgn(l, R(0, 0, 10, 2), R(0, 2, 2, 10), RGN_OR);
        pCombineRgn(l, l, R(8, 8, 12, 12), RGN_OR);
        pCombineRgn(l, l, R(4, 4, 6, 6), RGN_XOR);
        rgn_case("combine_complex_banding", l);
        pDeleteObject(dst);
    }

    /* ---- OffsetRgn / SetRectRgn / EqualRgn / PtInRegion / RectInRegion ---- */
    {
        int r;
        HRGN o = R(0, 0, 10, 10);
        ORACLE_CALL(r, pOffsetRgn(o, 5, -3));
        oracle_case("offset");
        o_i64("ret", r);
        rgn_fields(o);
        oracle_case_end();
        HRGN e = R(0, 0, 0, 0);
        ORACLE_CALL(r, pOffsetRgn(e, 5, 5));
        oracle_case("offset_empty");
        o_i64("ret", r);
        rgn_fields(e);
        oracle_case_end();
        BOOL ok;
        ORACLE_CALL(ok, pSetRectRgn(o, 9, 9, 1, 1));
        oracle_case("setrectrgn_reversed");
        o_bool("ret", ok);
        rgn_fields(o);
        oracle_case_end();
        ORACLE_CALL(ok, pSetRectRgn(o, 1, 1, 1, 5));
        oracle_case("setrectrgn_empty");
        o_bool("ret", ok);
        rgn_fields(o);
        oracle_case_end();
        oracle_case("equalrgn");
        o_bool("same_rect", pEqualRgn(R(0, 0, 3, 3), R(0, 0, 3, 3)));
        o_bool("different", pEqualRgn(R(0, 0, 3, 3), R(0, 0, 3, 4)));
        o_bool("two_empties", pEqualRgn(R(0, 0, 0, 0), R(5, 5, 5, 5)));
        o_bool("reversed_vs_normal", pEqualRgn(R(3, 3, 0, 0), R(0, 0, 3, 3)));
        o_i64("bad_handle", pEqualRgn(R(0, 0, 1, 1), (HRGN)(ULONG_PTR)0x1234));
        oracle_case_end();
        oracle_case("ptinregion");
        o_bool("inside", pPtInRegion(a, 5, 5));
        o_bool("top_left_corner", pPtInRegion(a, 0, 0));
        o_bool("right_edge", pPtInRegion(a, 10, 5));
        o_bool("bottom_edge", pPtInRegion(a, 5, 10));
        o_bool("last_pixel", pPtInRegion(a, 9, 9));
        o_bool("negative", pPtInRegion(a, -1, 0));
        oracle_case_end();
        RECT in = {2, 2, 4, 4}, part = {8, 8, 12, 12}, out = {10, 10, 12, 12}, rev = {4, 4, 2, 2},
             emptyr = {3, 3, 3, 3};
        oracle_case("rectinregion");
        o_bool("inside", pRectInRegion(a, &in));
        o_bool("partial", pRectInRegion(a, &part));
        o_bool("touching_outside", pRectInRegion(a, &out));
        o_bool("reversed", pRectInRegion(a, &rev));
        o_bool("empty_rect", pRectInRegion(a, &emptyr));
        oracle_case_end();
    }

    /* ---- polygons, ellipses ---- */
    {
        POINT tri[3] = {{0, 0}, {8, 0}, {0, 8}};
        rgn_case("polygon_triangle", pCreatePolygonRgn(tri, 3, ALTERNATE));
        POINT star[5] = {{5, 0}, {8, 10}, {0, 4}, {10, 4}, {2, 10}};
        rgn_case("polygon_star_alternate", pCreatePolygonRgn(star, 5, ALTERNATE));
        rgn_case("polygon_star_winding", pCreatePolygonRgn(star, 5, WINDING));
        POINT sq[4] = {{0, 0}, {4, 0}, {4, 4}, {0, 4}};
        rgn_case("polygon_square", pCreatePolygonRgn(sq, 4, ALTERNATE));
        HRGN r;
        ORACLE_CALL(r, pCreatePolygonRgn(sq, 2, ALTERNATE));
        oracle_case("polygon_two_points");
        o_handle("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, pCreatePolygonRgn(sq, 4, 7));
        oracle_case("polygon_bad_fill_mode");
        o_handle("ret", r);
        o_lasterr();
        oracle_case_end();
        rgn_case("ellipse_8x8", pCreateEllipticRgn(0, 0, 8, 8));
        rgn_case("ellipse_6x4", pCreateEllipticRgn(0, 0, 6, 4));
        rgn_case("ellipse_empty", pCreateEllipticRgn(0, 0, 0, 5));
        rgn_case("roundrect_10x10_r4", pCreateRoundRectRgn(0, 0, 10, 10, 4, 4));
        rgn_case("roundrect_zero_radius", pCreateRoundRectRgn(0, 0, 6, 6, 0, 0));
    }

    /* ---- ExtCreateRegion ---- */
    {
        HRGN src = R(0, 0, 0, 0);
        pCombineRgn(src, R(0, 0, 4, 4), R(6, 0, 8, 2), RGN_OR);
        DWORD sz = pGetRegionData(src, 0, NULL);
        RGNDATA *d = malloc(sz);
        pGetRegionData(src, sz, d);
        HRGN copy;
        ORACLE_CALL(copy, pExtCreateRegion(NULL, sz, d));
        oracle_case("extcreateregion_roundtrip");
        o_handle("ret", copy);
        o_lasterr();
        o_bool("equal", copy && pEqualRgn(copy, src));
        oracle_case_end();
        XFORM x = {2, 0, 0, 1, 10, 0};
        ORACLE_CALL(copy, pExtCreateRegion(&x, sz, d));
        oracle_case("extcreateregion_xform_scale");
        o_handle("ret", copy);
        if (copy) rgn_fields(copy);
        oracle_case_end();
        d->rdh.iType = 2;
        ORACLE_CALL(copy, pExtCreateRegion(NULL, sz, d));
        oracle_case("extcreateregion_bad_itype");
        o_handle("ret", copy);
        o_lasterr();
        oracle_case_end();
        d->rdh.iType = RDH_RECTANGLES;
        ORACLE_CALL(copy, pExtCreateRegion(NULL, sizeof(RGNDATAHEADER) - 1, d));
        oracle_case("extcreateregion_short_size");
        o_handle("ret", copy);
        o_lasterr();
        oracle_case_end();
        /* unsorted / overlapping input rectangles are accepted and normalised */
        struct { RGNDATAHEADER h; RECT r[3]; } u;
        memset(&u, 0, sizeof u);
        u.h.dwSize = sizeof u.h;
        u.h.iType = RDH_RECTANGLES;
        u.h.nCount = 3;
        u.r[0] = (RECT){5, 5, 9, 9};
        u.r[1] = (RECT){0, 0, 6, 6};
        u.r[2] = (RECT){7, 0, 9, 2};
        ORACLE_CALL(copy, pExtCreateRegion(NULL, sizeof u, (RGNDATA *)&u));
        oracle_case("extcreateregion_unsorted_overlapping");
        o_handle("ret", copy);
        if (copy) rgn_fields(copy);
        oracle_case_end();
        DWORD got;
        ORACLE_CALL(got, pGetRegionData(src, 8, d));
        oracle_case("getregiondata_buffer_too_small");
        o_u32("ret", got);
        o_lasterr();
        oracle_case_end();
        free(d);
    }

    /* ---- clipping on a memory DC with a 16x16 DIB ---- */
    {
        BITMAPINFO bi;
        void *bits;
        memset(&bi, 0, sizeof bi);
        bi.bmiHeader.biSize = sizeof bi.bmiHeader;
        bi.bmiHeader.biWidth = 16;
        bi.bmiHeader.biHeight = 16;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        HDC dc = pCreateCompatibleDC(NULL);
        pSelectObject(dc, pCreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0));
        int r;
        oracle_case("clip_initial");
        clip_fields(dc);
        oracle_case_end();
        ORACLE_CALL(r, pSelectClipRgn(dc, R(2, 2, 8, 8)));
        oracle_case("selectcliprgn_rect");
        o_i64("ret", r);
        clip_fields(dc);
        oracle_case_end();
        ORACLE_CALL(r, pIntersectClipRect(dc, 4, 4, 20, 20));
        oracle_case("intersectcliprect");
        o_i64("ret", r);
        clip_fields(dc);
        oracle_case_end();
        ORACLE_CALL(r, pExcludeClipRect(dc, 5, 5, 6, 6));
        oracle_case("excludecliprect");
        o_i64("ret", r);
        clip_fields(dc);
        oracle_case_end();
        ORACLE_CALL(r, pExtSelectClipRgn(dc, R(0, 0, 5, 16), RGN_OR));
        oracle_case("extselectcliprgn_or");
        o_i64("ret", r);
        clip_fields(dc);
        oracle_case_end();
        ORACLE_CALL(r, pExtSelectClipRgn(dc, R(100, 100, 101, 101), RGN_AND));
        oracle_case("extselectcliprgn_and_disjoint");
        o_i64("ret", r);
        clip_fields(dc);
        oracle_case_end();
        ORACLE_CALL(r, pExtSelectClipRgn(dc, NULL, RGN_COPY));
        oracle_case("extselectcliprgn_null_copy_resets");
        o_i64("ret", r);
        clip_fields(dc);
        oracle_case_end();
        ORACLE_CALL(r, pExtSelectClipRgn(dc, NULL, RGN_AND));
        oracle_case("extselectcliprgn_null_and");
        o_i64("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, pExtSelectClipRgn(dc, R(0, 0, 1, 1), 9));
        oracle_case("extselectcliprgn_bad_mode");
        o_i64("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, pSelectClipRgn(dc, R(-5, -5, 40, 40)));
        oracle_case("selectcliprgn_larger_than_surface");
        o_i64("ret", r);
        clip_fields(dc);
        oracle_case_end();
        ORACLE_CALL(r, pSelectClipRgn(dc, NULL));
        oracle_case("selectcliprgn_null");
        o_i64("ret", r);
        clip_fields(dc);
        oracle_case_end();
        ORACLE_CALL(r, pExcludeClipRect(dc, 0, 0, 16, 16));
        oracle_case("excludecliprect_everything");
        o_i64("ret", r);
        clip_fields(dc);
        oracle_case_end();
        BOOL ok;
        ORACLE_CALL(ok, pSelectClipPath(dc, RGN_AND));
        oracle_case("selectclippath_without_path");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        pDeleteDC(dc);
    }

    /* ---- invalid handles ---- */
    {
        RECT box;
        int r;
        HRGN dead = R(0, 0, 1, 1);
        pDeleteObject(dead);
        ORACLE_CALL(r, pGetRgnBox(dead, &box));
        oracle_case("getrgnbox_deleted");
        o_i64("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, pOffsetRgn(dead, 1, 1));
        oracle_case("offsetrgn_deleted");
        o_i64("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, pPtInRegion(dead, 0, 0));
        oracle_case("ptinregion_deleted");
        o_i64("ret", r);
        o_lasterr();
        oracle_case_end();
    }
    return oracle_end();
}
