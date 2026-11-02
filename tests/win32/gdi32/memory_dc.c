/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: GDI on memory DCs only: GetStockObject types and GetObjectW
 * sizes, CreateCompatibleDC defaults (attributes, the stock 1x1 bitmap),
 * CreateDIBSection/GetObjectW(DIBSECTION), SelectObject return values,
 * SetPixel/GetPixel, PatBlt and BitBlt raster operations on 32bpp DIBs
 * (pixel bytes read straight from the DIB memory, including the unused
 * top byte), mono <-> colour blits, StretchBlt (COLORONCOLOR, integer
 * scales and mirroring), GetDIBits, SaveDC/RestoreDC and DeleteDC/DeleteObject.
 *
 * No text, fonts, lines or anything depending on the screen's colour depth.
 *
 * gdi32 is loaded with LoadLibraryW and functions come from GetProcAddress.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-getstockobject
 *       https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-getobjectw
 *       https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-getobjecttype
 *       https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-createcompatibledc
 *       https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-createdibsection
 *       https://learn.microsoft.com/en-us/windows/win32/api/wingdi/ns-wingdi-dibsection
 *       https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-selectobject
 *       https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-setpixel
 *       https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-getpixel
 *       https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-patblt
 *       https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-bitblt
 *       https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-stretchblt
 *       https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-getdibits
 *       https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-createcompatiblebitmap
 *       https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-createbitmap
 *       https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-savedc
 *       https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-restoredc
 *       https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-deletedc
 *       https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-deleteobject
 *       https://learn.microsoft.com/en-us/windows/win32/gdi/ternary-raster-operations
 *       https://learn.microsoft.com/en-us/windows/win32/gdi/bitmaps-color-conversion (mono/colour blits)
 */
#include "oracle.h"

#define DECL(f) static __typeof__(f) *p##f
#define LOAD(m, f) (*(FARPROC *)&p##f = GetProcAddress(m, #f))

DECL(GetStockObject);
DECL(GetObjectW);
DECL(GetObjectType);
DECL(CreateCompatibleDC);
DECL(CreateDIBSection);
DECL(SelectObject);
DECL(GetCurrentObject);
DECL(SetPixel);
DECL(GetPixel);
DECL(PatBlt);
DECL(BitBlt);
DECL(StretchBlt);
DECL(SetStretchBltMode);
DECL(GetStretchBltMode);
DECL(CreateSolidBrush);
DECL(CreatePatternBrush);
DECL(CreateHatchBrush);
DECL(CreatePen);
DECL(CreateBitmap);
DECL(CreateCompatibleBitmap);
DECL(GetDIBits);
DECL(DeleteDC);
DECL(DeleteObject);
DECL(SetTextColor);
DECL(SetBkColor);
DECL(GetTextColor);
DECL(GetBkColor);
DECL(GetBkMode);
DECL(GetROP2);
DECL(SetROP2);
DECL(GetMapMode);
DECL(GetPolyFillMode);
DECL(GetGraphicsMode);
DECL(SaveDC);
DECL(RestoreDC);
DECL(GetDCBrushColor);
DECL(SetDCBrushColor);
DECL(GetBitmapBits);
DECL(SetBkMode);
DECL(CreateRectRgn);
DECL(SelectClipRgn);

#define N 4 /* blit tests use 4x4 32bpp DIBs = 64 bytes */

static HBITMAP dib32(int w, int h, DWORD **bits)
{
    BITMAPINFO bi;
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h; /* top-down */
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    return pCreateDIBSection(NULL, &bi, DIB_RGB_COLORS, (void **)bits, NULL, 0);
}

static HDC dsrc, ddst;
static DWORD *bsrc, *bdst;
static HBITMAP hsrc, hdst;

static void fill_src(void)
{
    for (int i = 0; i < N * N; i++)
        bsrc[i] = 0x80000000u | ((DWORD)(i * 16) << 16) | ((DWORD)(255 - i * 16) << 8) | (DWORD)(i * 5);
}
static void fill_dst(void)
{
    for (int i = 0; i < N * N; i++)
        bdst[i] = (i & 1) ? 0x40ff00ffu : 0x0000ff00u;
}

static void stock_case(const char *name, int idx)
{
    HGDIOBJ o;
    ORACLE_CALL(o, pGetStockObject(idx));
    oracle_case(name);
    o_handle("ret", o);
    o_lasterr();
    if (o) {
        o_u32("type", pGetObjectType(o));
        o_i64("getobject_size", pGetObjectW(o, 0, NULL));
        DWORD t = pGetObjectType(o);
        if (t == OBJ_BRUSH) {
            LOGBRUSH lb;
            memset(&lb, 0, sizeof lb);
            pGetObjectW(o, sizeof lb, &lb);
            o_u32("lb_style", lb.lbStyle);
            o_hex32("lb_color", lb.lbColor);
        } else if (t == OBJ_PEN) {
            LOGPEN lp;
            memset(&lp, 0, sizeof lp);
            pGetObjectW(o, sizeof lp, &lp);
            o_u32("lp_style", lp.lopnStyle);
            o_i64("lp_width", lp.lopnWidth.x);
            o_hex32("lp_color", lp.lopnColor);
        } else if (t == OBJ_PAL) {
            WORD n = 0;
            pGetObjectW(o, sizeof n, &n);
            o_u32("pal_entries", n);
        } else if (t == OBJ_BITMAP) {
            BITMAP bm;
            memset(&bm, 0, sizeof bm);
            pGetObjectW(o, sizeof bm, &bm);
            o_i64("bm_width", bm.bmWidth);
            o_i64("bm_height", bm.bmHeight);
            o_u32("bm_bpp", bm.bmBitsPixel);
        }
    }
    oracle_case_end();
}

static void rop_case(const char *name, DWORD rop)
{
    BOOL r;
    fill_src();
    fill_dst();
    ORACLE_CALL(r, pBitBlt(ddst, 0, 0, N, N, dsrc, 0, 0, rop));
    oracle_case(name);
    o_bool("ret", r);
    o_lasterr();
    o_bytes("dst", bdst, N * N * 4);
    oracle_case_end();
}

static void patblt_case(const char *name, int x, int y, int w, int h, DWORD rop)
{
    BOOL r;
    fill_dst();
    ORACLE_CALL(r, pPatBlt(ddst, x, y, w, h, rop));
    oracle_case(name);
    o_bool("ret", r);
    o_lasterr();
    o_bytes("dst", bdst, N * N * 4);
    oracle_case_end();
}

int main(void)
{
    oracle_begin("gdi32/memory_dc");
    HMODULE g;
    ORACLE_CALL(g, LoadLibraryW(L"gdi32.dll"));
    oracle_case("load_gdi32");
    o_handle("ret", g);
    oracle_case_end();
    if (!g) return oracle_end();
    LOAD(g, GetStockObject);
    LOAD(g, GetObjectW);
    LOAD(g, GetObjectType);
    LOAD(g, CreateCompatibleDC);
    LOAD(g, CreateDIBSection);
    LOAD(g, SelectObject);
    LOAD(g, GetCurrentObject);
    LOAD(g, SetPixel);
    LOAD(g, GetPixel);
    LOAD(g, PatBlt);
    LOAD(g, BitBlt);
    LOAD(g, StretchBlt);
    LOAD(g, SetStretchBltMode);
    LOAD(g, GetStretchBltMode);
    LOAD(g, CreateSolidBrush);
    LOAD(g, CreatePatternBrush);
    LOAD(g, CreateHatchBrush);
    LOAD(g, CreatePen);
    LOAD(g, CreateBitmap);
    LOAD(g, CreateCompatibleBitmap);
    LOAD(g, GetDIBits);
    LOAD(g, DeleteDC);
    LOAD(g, DeleteObject);
    LOAD(g, SetTextColor);
    LOAD(g, SetBkColor);
    LOAD(g, GetTextColor);
    LOAD(g, GetBkColor);
    LOAD(g, GetBkMode);
    LOAD(g, GetROP2);
    LOAD(g, SetROP2);
    LOAD(g, GetMapMode);
    LOAD(g, GetPolyFillMode);
    LOAD(g, GetGraphicsMode);
    LOAD(g, SaveDC);
    LOAD(g, RestoreDC);
    LOAD(g, CreateRectRgn);
    LOAD(g, SelectClipRgn);
    LOAD(g, GetDCBrushColor);
    LOAD(g, SetDCBrushColor);
    LOAD(g, GetBitmapBits);
    LOAD(g, SetBkMode);

    /* ---- stock objects ---- */
    stock_case("stock_white_brush", WHITE_BRUSH);
    stock_case("stock_ltgray_brush", LTGRAY_BRUSH);
    stock_case("stock_gray_brush", GRAY_BRUSH);
    stock_case("stock_dkgray_brush", DKGRAY_BRUSH);
    stock_case("stock_black_brush", BLACK_BRUSH);
    stock_case("stock_null_brush", NULL_BRUSH);
    stock_case("stock_white_pen", WHITE_PEN);
    stock_case("stock_black_pen", BLACK_PEN);
    stock_case("stock_null_pen", NULL_PEN);
    stock_case("stock_dc_brush", DC_BRUSH);
    stock_case("stock_dc_pen", DC_PEN);
    stock_case("stock_default_palette", DEFAULT_PALETTE);
    stock_case("stock_system_font_type_only", SYSTEM_FONT);
    stock_case("stock_index_9_unused", 9);
    stock_case("stock_index_100", 100);
    stock_case("stock_index_negative", -1);

    /* ---- memory DC defaults ---- */
    HDC dc;
    ORACLE_CALL(dc, pCreateCompatibleDC(NULL));
    oracle_case("create_compatible_dc_null");
    o_handle("ret", dc);
    o_lasterr();
    o_u32("type", pGetObjectType(dc));
    {
        HGDIOBJ bm = pGetCurrentObject(dc, OBJ_BITMAP);
        BITMAP b;
        memset(&b, 0, sizeof b);
        o_i64("bitmap_getobject", pGetObjectW(bm, sizeof b, &b));
        o_i64("bitmap_width", b.bmWidth);
        o_i64("bitmap_height", b.bmHeight);
        o_u32("bitmap_bpp", b.bmBitsPixel);
        o_u32("bitmap_planes", b.bmPlanes);
        o_i64("bitmap_widthbytes", b.bmWidthBytes);
        o_bool("brush_is_white", pGetCurrentObject(dc, OBJ_BRUSH) == pGetStockObject(WHITE_BRUSH));
        o_bool("pen_is_black", pGetCurrentObject(dc, OBJ_PEN) == pGetStockObject(BLACK_PEN));
        o_bool("palette_is_default", pGetCurrentObject(dc, OBJ_PAL) == pGetStockObject(DEFAULT_PALETTE));
    }
    o_hex32("text_color", pGetTextColor(dc));
    o_hex32("bk_color", pGetBkColor(dc));
    o_i64("bk_mode", pGetBkMode(dc));
    o_i64("rop2", pGetROP2(dc));
    o_i64("map_mode", pGetMapMode(dc));
    o_i64("poly_fill_mode", pGetPolyFillMode(dc));
    o_i64("stretch_mode", pGetStretchBltMode(dc));
    o_i64("graphics_mode", pGetGraphicsMode(dc));
    o_hex32("dc_brush_color", pGetDCBrushColor(dc));
    oracle_case_end();

    /* ---- DIB sections ---- */
    DWORD *bits = NULL;
    HBITMAP dib;
    ORACLE_CALL(dib, dib32(8, 8, &bits));
    oracle_case("dibsection_32bpp_topdown");
    o_handle("ret", dib);
    o_lasterr();
    o_bool("bits_nonnull", bits != NULL);
    o_u32("type", pGetObjectType(dib));
    o_i64("getobject_size_null", pGetObjectW(dib, 0, NULL));
    {
        DIBSECTION ds;
        memset(&ds, 0, sizeof ds);
        o_i64("getobject_dibsection", pGetObjectW(dib, sizeof ds, &ds));
        o_i64("bm_width", ds.dsBm.bmWidth);
        o_i64("bm_height", ds.dsBm.bmHeight);
        o_i64("bm_widthbytes", ds.dsBm.bmWidthBytes);
        o_u32("bm_bpp", ds.dsBm.bmBitsPixel);
        o_bool("bm_bits_is_bits", ds.dsBm.bmBits == bits);
        o_u32("bih_size", ds.dsBmih.biSize);
        o_i64("bih_height", ds.dsBmih.biHeight);
        o_u32("bih_compression", ds.dsBmih.biCompression);
        o_u32("bih_sizeimage", ds.dsBmih.biSizeImage);
        o_hex32("bitfields_0", ds.dsBitfields[0]);
        o_hex32("bitfields_1", ds.dsBitfields[1]);
        o_hex32("bitfields_2", ds.dsBitfields[2]);
        o_bool("section_null", ds.dshSection == NULL);
        BITMAP bm;
        o_i64("getobject_bitmap_only", pGetObjectW(dib, sizeof bm, &bm));
        o_i64("getobject_small_buf", pGetObjectW(dib, 4, &bm));
        o_i64("getobject_between", pGetObjectW(dib, sizeof bm + 4, &ds));
    }
    oracle_case_end();
    {
        BITMAPINFO bi;
        void *b2 = NULL;
        HBITMAP h;
        memset(&bi, 0, sizeof bi);
        bi.bmiHeader.biSize = sizeof bi.bmiHeader;
        bi.bmiHeader.biWidth = 4;
        bi.bmiHeader.biHeight = 4;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 7;
        ORACLE_CALL(h, pCreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &b2, NULL, 0));
        oracle_case("dibsection_bad_bpp");
        o_handle("ret", h);
        o_lasterr();
        oracle_case_end();
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biHeight = 0;
        ORACLE_CALL(h, pCreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &b2, NULL, 0));
        oracle_case("dibsection_zero_height");
        o_handle("ret", h);
        o_lasterr();
        oracle_case_end();
        if (h) pDeleteObject(h);
        bi.bmiHeader.biHeight = 4;
        bi.bmiHeader.biBitCount = 24;
        ORACLE_CALL(h, pCreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &b2, NULL, 0));
        oracle_case("dibsection_24bpp_bottomup");
        o_handle("ret", h);
        o_lasterr();
        if (h) {
            DIBSECTION ds;
            memset(&ds, 0, sizeof ds);
            pGetObjectW(h, sizeof ds, &ds);
            o_i64("bm_widthbytes", ds.dsBm.bmWidthBytes);
            o_u32("bih_sizeimage", ds.dsBmih.biSizeImage);
            pDeleteObject(h);
        }
        oracle_case_end();
    }

    /* ---- SelectObject ---- */
    HGDIOBJ prev;
    HGDIOBJ stockbm = pGetCurrentObject(dc, OBJ_BITMAP);
    ORACLE_CALL(prev, pSelectObject(dc, dib));
    oracle_case("select_dib");
    o_bool("prev_is_stock_bitmap", prev == stockbm);
    o_lasterr();
    oracle_case_end();
    {
        HDC dc2 = pCreateCompatibleDC(NULL);
        ORACLE_CALL(prev, pSelectObject(dc2, dib));
        oracle_case("select_dib_into_second_dc");
        o_handle("ret", prev);
        o_lasterr();
        oracle_case_end();
        pDeleteDC(dc2);
        HBRUSH br = pCreateSolidBrush(RGB(1, 2, 3));
        ORACLE_CALL(prev, pSelectObject(dc, br));
        oracle_case("select_brush");
        o_bool("prev_is_white_brush", prev == pGetStockObject(WHITE_BRUSH));
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(prev, pSelectObject(dc, pGetStockObject(WHITE_BRUSH)));
        oracle_case("select_brush_back");
        o_bool("prev_is_ours", prev == br);
        oracle_case_end();
        pDeleteObject(br);
        ORACLE_CALL(prev, pSelectObject(dc, (HGDIOBJ)(ULONG_PTR)0x12345678));
        oracle_case("select_bogus_handle");
        o_handle("ret", prev);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(prev, pSelectObject(NULL, pGetStockObject(BLACK_PEN)));
        oracle_case("select_null_dc");
        o_handle("ret", prev);
        o_lasterr();
        oracle_case_end();
        HBITMAP mono = pCreateBitmap(4, 4, 1, 1, NULL);
        HDC dc3 = pCreateCompatibleDC(NULL);
        ORACLE_CALL(prev, pSelectObject(dc3, mono));
        oracle_case("select_mono_bitmap");
        o_bool("prev_is_stock_bitmap", prev == stockbm);
        o_lasterr();
        oracle_case_end();
        BOOL r;
        ORACLE_CALL(r, pDeleteObject(mono));
        oracle_case("delete_selected_bitmap");
        o_bool("ret", r);
        o_lasterr();
        o_u32("type_after", pGetObjectType(mono));
        oracle_case_end();
        pDeleteDC(dc3);
        oracle_case("bitmap_after_dc_deleted");
        o_u32("type", pGetObjectType(mono));
        oracle_case_end();
        pDeleteObject(mono);
    }

    /* ---- SetPixel / GetPixel ---- */
    {
        COLORREF c;
        ORACLE_CALL(c, pSetPixel(dc, 0, 0, RGB(1, 2, 3)));
        oracle_case("setpixel");
        o_hex32("ret", c);
        o_lasterr();
        o_hex32("raw", bits[0]);
        oracle_case_end();
        ORACLE_CALL(c, pGetPixel(dc, 0, 0));
        oracle_case("getpixel");
        o_hex32("ret", c);
        o_lasterr();
        oracle_case_end();
        bits[1] = 0xff102030;
        ORACLE_CALL(c, pGetPixel(dc, 1, 0));
        oracle_case("getpixel_raw_alpha");
        o_hex32("ret", c);
        oracle_case_end();
        ORACLE_CALL(c, pSetPixel(dc, 8, 0, RGB(9, 9, 9)));
        oracle_case("setpixel_out_of_bounds");
        o_hex32("ret", c);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(c, pGetPixel(dc, -1, 0));
        oracle_case("getpixel_out_of_bounds");
        o_hex32("ret", c);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(c, pSetPixel(dc, 2, 0, PALETTEINDEX(1)));
        oracle_case("setpixel_paletteindex");
        o_hex32("ret", c);
        o_hex32("raw", bits[2]);
        oracle_case_end();
        ORACLE_CALL(c, pSetPixel(dc, 3, 0, 0x02000000 | RGB(7, 8, 9)));
        oracle_case("setpixel_palettergb");
        o_hex32("ret", c);
        o_hex32("raw", bits[3]);
        oracle_case_end();
        ORACLE_CALL(c, pGetPixel(NULL, 0, 0));
        oracle_case("getpixel_null_dc");
        o_hex32("ret", c);
        o_lasterr();
        oracle_case_end();
    }

    /* ---- PatBlt and BitBlt ROPs on 4x4 32bpp DIBs ---- */
    dsrc = pCreateCompatibleDC(NULL);
    ddst = pCreateCompatibleDC(NULL);
    hsrc = dib32(N, N, &bsrc);
    hdst = dib32(N, N, &bdst);
    pSelectObject(dsrc, hsrc);
    pSelectObject(ddst, hdst);
    HBRUSH brush = pCreateSolidBrush(RGB(0x11, 0x22, 0x33));
    pSelectObject(ddst, brush);

    patblt_case("patblt_patcopy", 0, 0, N, N, PATCOPY);
    patblt_case("patblt_patinvert", 0, 0, N, N, PATINVERT);
    patblt_case("patblt_dstinvert", 0, 0, N, N, DSTINVERT);
    patblt_case("patblt_blackness", 0, 0, N, N, BLACKNESS);
    patblt_case("patblt_whiteness", 0, 0, N, N, WHITENESS);
    patblt_case("patblt_partial_rect", 1, 1, 2, 2, PATCOPY);
    patblt_case("patblt_negative_extent", 3, 3, -2, -2, PATCOPY);
    patblt_case("patblt_clipped", -2, -2, 4, 4, PATCOPY);
    patblt_case("patblt_srccopy_rop_needs_source", 0, 0, N, N, SRCCOPY);
    patblt_case("patblt_ternary_0x5a0049", 0, 0, N, N, 0x005A0049);
    {
        HBRUSH hb = pCreateHatchBrush(HS_CROSS, RGB(0xff, 0, 0));
        HGDIOBJ old = pSelectObject(ddst, hb);
        pSetBkColor(ddst, RGB(0, 0, 0xff));
        patblt_case("patblt_hatch_cross", 0, 0, N, N, PATCOPY);
        pSetBkMode(ddst, TRANSPARENT);
        patblt_case("patblt_hatch_transparent", 0, 0, N, N, PATCOPY);
        pSetBkMode(ddst, OPAQUE);
        pSetBkColor(ddst, RGB(0xff, 0xff, 0xff));
        pSelectObject(ddst, old);
        pDeleteObject(hb);
        WORD pat[8] = {0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa};
        HBITMAP pb = pCreateBitmap(8, 8, 1, 1, pat);
        HBRUSH pbr = pCreatePatternBrush(pb);
        old = pSelectObject(ddst, pbr);
        pSetTextColor(ddst, RGB(0x10, 0x20, 0x30));
        pSetBkColor(ddst, RGB(0xa0, 0xb0, 0xc0));
        patblt_case("patblt_mono_pattern_brush", 0, 0, N, N, PATCOPY);
        pSetTextColor(ddst, 0);
        pSetBkColor(ddst, RGB(0xff, 0xff, 0xff));
        pSelectObject(ddst, old);
        pDeleteObject(pbr);
        pDeleteObject(pb);
        old = pSelectObject(ddst, pGetStockObject(DC_BRUSH));
        pSetDCBrushColor(ddst, RGB(0x44, 0x55, 0x66));
        patblt_case("patblt_dc_brush", 0, 0, N, N, PATCOPY);
        pSelectObject(ddst, old);
    }

    rop_case("bitblt_srccopy", SRCCOPY);
    rop_case("bitblt_srcand", SRCAND);
    rop_case("bitblt_srcpaint", SRCPAINT);
    rop_case("bitblt_srcinvert", SRCINVERT);
    rop_case("bitblt_srcerase", SRCERASE);
    rop_case("bitblt_notsrccopy", NOTSRCCOPY);
    rop_case("bitblt_notsrcerase", NOTSRCERASE);
    rop_case("bitblt_mergecopy", MERGECOPY);
    rop_case("bitblt_mergepaint", MERGEPAINT);
    rop_case("bitblt_patpaint", PATPAINT);
    rop_case("bitblt_dst_nop_0xaa0029", 0x00AA0029);
    rop_case("bitblt_ternary_0xb8074a", 0x00B8074A);
    rop_case("bitblt_ternary_0xe20746", 0x00E20746);
    rop_case("bitblt_rop_low_word_ignored", 0x00CC1234);
    {
        BOOL r;
        fill_src();
        fill_dst();
        ORACLE_CALL(r, pBitBlt(ddst, 1, 1, N, N, dsrc, 0, 0, SRCCOPY));
        oracle_case("bitblt_offset_clipped");
        o_bool("ret", r);
        o_bytes("dst", bdst, N * N * 4);
        oracle_case_end();
        fill_dst();
        ORACLE_CALL(r, pBitBlt(ddst, 0, 0, N, N, dsrc, 2, 2, SRCCOPY));
        oracle_case("bitblt_source_out_of_bounds");
        o_bool("ret", r);
        o_bytes("dst", bdst, N * N * 4);
        oracle_case_end();
        fill_dst();
        ORACLE_CALL(r, pBitBlt(ddst, 0, 0, N, N, NULL, 0, 0, SRCCOPY));
        oracle_case("bitblt_null_source_dc");
        o_bool("ret", r);
        o_lasterr();
        o_bytes("dst", bdst, N * N * 4);
        oracle_case_end();
        fill_src();
        ORACLE_CALL(r, pBitBlt(dsrc, 1, 0, 3, N, dsrc, 0, 0, SRCCOPY));
        oracle_case("bitblt_overlapping_same_dc");
        o_bool("ret", r);
        o_bytes("src", bsrc, N * N * 4);
        oracle_case_end();
    }

    /* ---- StretchBlt ---- */
    {
        BOOL r;
        oracle_case("stretchblt_mode_default");
        o_i64("mode", pGetStretchBltMode(ddst));
        o_i64("set_ret", pSetStretchBltMode(ddst, COLORONCOLOR));
        oracle_case_end();
        fill_src();
        fill_dst();
        ORACLE_CALL(r, pStretchBlt(ddst, 0, 0, N, N, dsrc, 0, 0, 2, 2, SRCCOPY));
        oracle_case("stretchblt_2x_up");
        o_bool("ret", r);
        o_bytes("dst", bdst, N * N * 4);
        oracle_case_end();
        fill_dst();
        ORACLE_CALL(r, pStretchBlt(ddst, 0, 0, 2, 2, dsrc, 0, 0, N, N, SRCCOPY));
        oracle_case("stretchblt_2x_down");
        o_bool("ret", r);
        o_bytes("dst", bdst, N * N * 4);
        oracle_case_end();
        fill_dst();
        ORACLE_CALL(r, pStretchBlt(ddst, N - 1, 0, -N, N, dsrc, 0, 0, N, N, SRCCOPY));
        oracle_case("stretchblt_mirror_x");
        o_bool("ret", r);
        o_bytes("dst", bdst, N * N * 4);
        oracle_case_end();
        fill_dst();
        ORACLE_CALL(r, pStretchBlt(ddst, 0, N - 1, N, -N, dsrc, 0, 0, N, N, SRCCOPY));
        oracle_case("stretchblt_mirror_y");
        o_bool("ret", r);
        o_bytes("dst", bdst, N * N * 4);
        oracle_case_end();
        fill_dst();
        ORACLE_CALL(r, pStretchBlt(ddst, 0, 0, N, N, dsrc, 0, 0, N, N, SRCINVERT));
        oracle_case("stretchblt_identity_srcinvert");
        o_bool("ret", r);
        o_bytes("dst", bdst, N * N * 4);
        oracle_case_end();
        pSetStretchBltMode(ddst, BLACKONWHITE);
    }

    /* ---- mono <-> colour ---- */
    {
        BYTE mbits[N * 2] = {0xa0, 0, 0x50, 0, 0xf0, 0, 0x00, 0}; /* 4x4, word-aligned rows */
        HBITMAP mono = pCreateBitmap(N, N, 1, 1, mbits);
        HDC dm = pCreateCompatibleDC(NULL);
        pSelectObject(dm, mono);
        pSetTextColor(ddst, RGB(0x11, 0x22, 0x33));
        pSetBkColor(ddst, RGB(0xaa, 0xbb, 0xcc));
        fill_dst();
        BOOL r;
        ORACLE_CALL(r, pBitBlt(ddst, 0, 0, N, N, dm, 0, 0, SRCCOPY));
        oracle_case("bitblt_mono_to_color");
        o_bool("ret", r);
        o_bytes("dst", bdst, N * N * 4);
        oracle_case_end();
        pSetTextColor(ddst, 0);
        pSetBkColor(ddst, RGB(0xff, 0xff, 0xff));

        fill_src();
        bsrc[0] = bsrc[5] = 0x00aabbcc; /* equal to the source DC's background colour */
        pSetBkColor(dsrc, RGB(0xaa, 0xbb, 0xcc));
        ORACLE_CALL(r, pBitBlt(dm, 0, 0, N, N, dsrc, 0, 0, SRCCOPY));
        BYTE out[N * 2];
        memset(out, 0xee, sizeof out);
        LONG got = pGetBitmapBits(mono, sizeof out, out);
        oracle_case("bitblt_color_to_mono");
        o_bool("ret", r);
        o_i64("getbitmapbits", got);
        o_bytes("mono", out, sizeof out);
        oracle_case_end();
        pSetBkColor(dsrc, RGB(0xff, 0xff, 0xff));
        pDeleteDC(dm);
        pDeleteObject(mono);
    }

    /* ---- compatible bitmaps ---- */
    {
        HBITMAP h;
        BITMAP b;
        ORACLE_CALL(h, pCreateCompatibleBitmap(ddst, 0, 0));
        memset(&b, 0, sizeof b);
        pGetObjectW(h, sizeof b, &b);
        oracle_case("compatible_bitmap_zero_size");
        o_handle("ret", h);
        o_i64("width", b.bmWidth);
        o_i64("height", b.bmHeight);
        o_u32("bpp", b.bmBitsPixel);
        oracle_case_end();
        pDeleteObject(h);
        ORACLE_CALL(h, pCreateCompatibleBitmap(ddst, 3, 2));
        memset(&b, 0, sizeof b);
        oracle_case("compatible_bitmap_from_dib_dc");
        o_handle("ret", h);
        o_i64("getobject_size_null", pGetObjectW(h, 0, NULL));
        pGetObjectW(h, sizeof b, &b);
        o_i64("width", b.bmWidth);
        o_i64("height", b.bmHeight);
        o_u32("bpp", b.bmBitsPixel);
        o_i64("widthbytes", b.bmWidthBytes);
        oracle_case_end();
        pDeleteObject(h);
        ORACLE_CALL(h, pCreateCompatibleBitmap(dc = pCreateCompatibleDC(NULL), 3, 2));
        memset(&b, 0, sizeof b);
        pGetObjectW(h, sizeof b, &b);
        oracle_case("compatible_bitmap_from_fresh_memdc_is_mono");
        o_handle("ret", h);
        o_u32("bpp", b.bmBitsPixel);
        o_i64("widthbytes", b.bmWidthBytes);
        oracle_case_end();
        pDeleteObject(h);
        pDeleteDC(dc);
        ORACLE_CALL(h, pCreateBitmap(0, 0, 1, 1, NULL));
        oracle_case("createbitmap_zero_size");
        o_handle("ret", h);
        o_bool("is_stock_bitmap", h == stockbm);
        oracle_case_end();
        ORACLE_CALL(h, pCreateBitmap(4, 4, 1, 33, NULL));
        oracle_case("createbitmap_bpp_33");
        o_handle("ret", h);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(h, pCreateBitmap(4, 4, 1, 24, NULL));
        memset(&b, 0, sizeof b);
        pGetObjectW(h, sizeof b, &b);
        oracle_case("createbitmap_24bpp");
        o_handle("ret", h);
        o_u32("bpp", b.bmBitsPixel);
        o_i64("widthbytes", b.bmWidthBytes);
        oracle_case_end();
        pDeleteObject(h);
    }

    /* ---- GetDIBits (the bitmap must not be selected into a DC) ---- */
    {
        fill_src();
        pSelectObject(dsrc, stockbm);
        BITMAPINFO bi;
        memset(&bi, 0, sizeof bi);
        bi.bmiHeader.biSize = sizeof bi.bmiHeader;
        int r;
        ORACLE_CALL(r, pGetDIBits(ddst, hsrc, 0, N, NULL, &bi, DIB_RGB_COLORS));
        oracle_case("getdibits_query_header");
        o_i64("ret", r);
        o_i64("width", bi.bmiHeader.biWidth);
        o_i64("height", bi.bmiHeader.biHeight);
        o_u32("bpp", bi.bmiHeader.biBitCount);
        o_u32("compression", bi.bmiHeader.biCompression);
        o_u32("sizeimage", bi.bmiHeader.biSizeImage);
        oracle_case_end();
        DWORD out[N * N];
        memset(out, 0xee, sizeof out);
        bi.bmiHeader.biHeight = N; /* bottom-up copy */
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        ORACLE_CALL(r, pGetDIBits(ddst, hsrc, 0, N, out, &bi, DIB_RGB_COLORS));
        oracle_case("getdibits_bottom_up_32");
        o_i64("ret", r);
        o_bytes("bits", out, sizeof out);
        oracle_case_end();
        memset(out, 0xee, sizeof out);
        bi.bmiHeader.biHeight = -N;
        ORACLE_CALL(r, pGetDIBits(ddst, hsrc, 1, 2, out, &bi, DIB_RGB_COLORS));
        oracle_case("getdibits_top_down_lines_1_2");
        o_i64("ret", r);
        o_bytes("bits", out, sizeof out);
        oracle_case_end();
    }

    /* ---- SaveDC / RestoreDC ---- */
    {
        HDC d = pCreateCompatibleDC(NULL);
        int s1, s2, s3;
        pSetTextColor(d, RGB(1, 1, 1));
        s1 = pSaveDC(d);
        pSetTextColor(d, RGB(2, 2, 2));
        s2 = pSaveDC(d);
        pSetTextColor(d, RGB(3, 3, 3));
        s3 = pSaveDC(d);
        oracle_case("savedc_levels");
        o_i64("s1", s1);
        o_i64("s2", s2);
        o_i64("s3", s3);
        oracle_case_end();
        BOOL r;
        ORACLE_CALL(r, pRestoreDC(d, -2));
        oracle_case("restoredc_minus2");
        o_bool("ret", r);
        o_hex32("text_color", pGetTextColor(d));
        oracle_case_end();
        ORACLE_CALL(r, pRestoreDC(d, 5));
        oracle_case("restoredc_too_deep");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, pRestoreDC(d, 1));
        oracle_case("restoredc_1");
        o_bool("ret", r);
        o_hex32("text_color", pGetTextColor(d));
        o_i64("next_save", pSaveDC(d));
        oracle_case_end();
        ORACLE_CALL(r, pRestoreDC(d, 0));
        oracle_case("restoredc_0");
        o_bool("ret", r);
        oracle_case_end();
        ORACLE_CALL(r, pRestoreDC(d, -5));
        oracle_case("restoredc_minus5");
        o_bool("ret", r);
        oracle_case_end();
        pDeleteDC(d);
    }

    /* ---- deletion ---- */
    {
        BOOL r;
        HBRUSH b = pCreateSolidBrush(0);
        ORACLE_CALL(r, pDeleteObject(b));
        oracle_case("deleteobject_brush");
        o_bool("ret", r);
        o_u32("type_after", pGetObjectType(b));
        oracle_case_end();
        ORACLE_CALL(r, pDeleteObject(b));
        oracle_case("deleteobject_twice");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, pDeleteObject(pGetStockObject(BLACK_BRUSH)));
        oracle_case("deleteobject_stock");
        o_bool("ret", r);
        o_u32("type_after", pGetObjectType(pGetStockObject(BLACK_BRUSH)));
        oracle_case_end();
        ORACLE_CALL(r, pDeleteObject(NULL));
        oracle_case("deleteobject_null");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        HDC d = pCreateCompatibleDC(NULL);
        ORACLE_CALL(r, pDeleteDC(d));
        oracle_case("deletedc");
        o_bool("ret", r);
        o_u32("type_after", pGetObjectType(d));
        oracle_case_end();
        ORACLE_CALL(r, pDeleteDC(d));
        oracle_case("deletedc_twice");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, pDeleteObject(hdst));
        oracle_case("deleteobject_selected_dib");
        o_bool("ret", r);
        o_u32("type_after", pGetObjectType(hdst));
        oracle_case_end();
    }

    /* ---- probes: deleting objects, selected or not ---- */
    {
        BOOL r;
        HGDIOBJ objs[6];
        static const char *names[6] = {"del_unselected_solid_brush", "del_unselected_hatch_brush",
                                       "del_unselected_pen", "del_unselected_bitmap",
                                       "del_unselected_dib", "del_unselected_region"};
        DWORD *db;
        objs[0] = pCreateSolidBrush(RGB(1, 2, 3));
        objs[1] = pCreateHatchBrush(HS_CROSS, RGB(1, 2, 3));
        objs[2] = pCreatePen(PS_SOLID, 1, RGB(1, 2, 3));
        objs[3] = pCreateBitmap(4, 4, 1, 1, NULL);
        objs[4] = dib32(4, 4, &db);
        objs[5] = pCreateRectRgn(0, 0, 4, 4);
        for (int i = 0; i < 6; i++) {
            ORACLE_CALL(r, pDeleteObject(objs[i]));
            oracle_case(names[i]);
            o_bool("ret", r);
            o_lasterr();
            o_u32("type_after", pGetObjectType(objs[i]));
            ORACLE_CALL(r, pDeleteObject(objs[i]));
            o_bool("second_ret", r);
            o_u32("second_type_after", pGetObjectType(objs[i]));
            oracle_case_end();
        }

        /* objects selected into a DC, deleted, then deselected */
        HDC d = pCreateCompatibleDC(NULL);
        HGDIOBJ sel[4];
        static const char *snames[4] = {"del_selected_solid_brush", "del_selected_pen",
                                        "del_selected_bitmap", "del_selected_dib"};
        sel[0] = pCreateSolidBrush(RGB(4, 5, 6));
        sel[1] = pCreatePen(PS_SOLID, 1, RGB(4, 5, 6));
        sel[2] = pCreateBitmap(4, 4, 1, 1, NULL);
        sel[3] = dib32(4, 4, &db);
        for (int i = 0; i < 4; i++) {
            HGDIOBJ old = pSelectObject(d, sel[i]);
            ORACLE_CALL(r, pDeleteObject(sel[i]));
            oracle_case(snames[i]);
            o_bool("ret", r);
            o_lasterr();
            o_u32("type_after", pGetObjectType(sel[i]));
            o_bool("still_current", pGetCurrentObject(d, pGetObjectType(old)) == sel[i]);
            HGDIOBJ back = pSelectObject(d, old);
            o_bool("deselect_returns_it", back == sel[i]);
            o_u32("type_after_deselect", pGetObjectType(sel[i]));
            HGDIOBJ again = pSelectObject(d, sel[i]);
            o_bool("reselect_ok", again != NULL);
            if (again) pSelectObject(d, old);
            ORACLE_CALL(r, pDeleteObject(sel[i]));
            o_bool("delete_after_deselect_ret", r);
            o_u32("type_after_second_delete", pGetObjectType(sel[i]));
            oracle_case_end();
        }
        pDeleteDC(d);

        /* deleting the DC an object is selected into */
        d = pCreateCompatibleDC(NULL);
        HBRUSH br = pCreateSolidBrush(RGB(7, 8, 9));
        HBITMAP bm = pCreateBitmap(4, 4, 1, 1, NULL);
        pSelectObject(d, br);
        pSelectObject(d, bm);
        pDeleteObject(br);
        pDeleteObject(bm);
        ORACLE_CALL(r, pDeleteDC(d));
        oracle_case("del_dc_with_deleted_selected_objects");
        o_bool("ret", r);
        o_u32("brush_type_after", pGetObjectType(br));
        o_u32("bitmap_type_after", pGetObjectType(bm));
        ORACLE_CALL(r, pDeleteObject(bm));
        o_bool("bitmap_delete_ret", r);
        o_u32("bitmap_type_after_delete", pGetObjectType(bm));
        oracle_case_end();

        /* DeleteDC variants */
        d = pCreateCompatibleDC(NULL);
        pSaveDC(d);
        ORACLE_CALL(r, pDeleteDC(d));
        oracle_case("deletedc_with_saved_state");
        o_bool("ret", r);
        o_u32("type_after", pGetObjectType(d));
        oracle_case_end();
        ORACLE_CALL(r, pDeleteDC((HDC)(ULONG_PTR)0x12345678));
        oracle_case("deletedc_bogus");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, pDeleteDC(NULL));
        oracle_case("deletedc_null");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
    }

    /* ---- probes: SetPixel/PatBlt/RestoreDC/GetDIBits edge cases ---- */
    {
        HDC d = pCreateCompatibleDC(NULL);
        DWORD *db;
        HBITMAP h = dib32(4, 4, &db);
        HGDIOBJ old = pSelectObject(d, h);
        COLORREF c;
        ORACLE_CALL(c, pSetPixel(d, -1, 0, RGB(1, 1, 1)));
        oracle_case("setpixel_negative_x");
        o_hex32("ret", c);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(c, pSetPixel(d, 0, 4, RGB(1, 1, 1)));
        oracle_case("setpixel_past_bottom");
        o_hex32("ret", c);
        o_lasterr();
        oracle_case_end();
        HRGN clip = pCreateRectRgn(0, 0, 1, 1);
        pSelectClipRgn(d, clip);
        pDeleteObject(clip);
        ORACLE_CALL(c, pSetPixel(d, 2, 2, RGB(1, 1, 1)));
        oracle_case("setpixel_clipped_out");
        o_hex32("ret", c);
        o_lasterr();
        o_hex32("raw", db[2 * 4 + 2]);
        oracle_case_end();
        pSelectClipRgn(d, NULL);
        ORACLE_CALL(c, pSetPixel(NULL, 0, 0, RGB(1, 1, 1)));
        oracle_case("setpixel_null_dc");
        o_hex32("ret", c);
        o_lasterr();
        oracle_case_end();

        static const struct { const char *name; int x, y, w, h; } pb[] = {
            {"patblt_neg_width", 3, 0, -2, 1},
            {"patblt_neg_height", 0, 3, 1, -2},
            {"patblt_neg_width_1", 2, 0, -1, 1},
            {"patblt_zero_width", 1, 1, 0, 2},
        };
        pSelectObject(d, pGetStockObject(BLACK_BRUSH));
        for (size_t i = 0; i < sizeof pb / sizeof pb[0]; i++) {
            BOOL r;
            for (int k = 0; k < 16; k++) db[k] = 0x00ffffff;
            ORACLE_CALL(r, pPatBlt(d, pb[i].x, pb[i].y, pb[i].w, pb[i].h, PATCOPY));
            oracle_case(pb[i].name);
            o_bool("ret", r);
            o_bytes("dst", db, 64);
            oracle_case_end();
        }

        int s = pSaveDC(d);
        BOOL r;
        ORACLE_CALL(r, pRestoreDC(d, s + 1));
        oracle_case("restoredc_one_past");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, pRestoreDC(d, -2));
        oracle_case("restoredc_minus_too_deep");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, pRestoreDC(d, 0));
        oracle_case("restoredc_zero_lasterr");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, pRestoreDC(d, s));
        oracle_case("restoredc_exact");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, pRestoreDC(d, 1));
        oracle_case("restoredc_nothing_saved");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        pSelectObject(d, old);

        BITMAPINFO bi;
        int n;
        memset(&bi, 0, sizeof bi);
        bi.bmiHeader.biSize = sizeof bi.bmiHeader;
        ORACLE_CALL(n, pGetDIBits(d, h, 0, 2, NULL, &bi, DIB_RGB_COLORS));
        oracle_case("getdibits_query_header_2_lines");
        o_i64("ret", n);
        o_lasterr();
        o_u32("bpp", bi.bmiHeader.biBitCount);
        oracle_case_end();
        memset(&bi, 0, sizeof bi);
        bi.bmiHeader.biSize = sizeof bi.bmiHeader;
        ORACLE_CALL(n, pGetDIBits(d, h, 0, 0, NULL, &bi, DIB_RGB_COLORS));
        oracle_case("getdibits_query_header_0_lines");
        o_i64("ret", n);
        o_lasterr();
        oracle_case_end();
        HBITMAP mono = pCreateBitmap(4, 4, 1, 1, NULL);
        memset(&bi, 0, sizeof bi);
        bi.bmiHeader.biSize = sizeof bi.bmiHeader;
        ORACLE_CALL(n, pGetDIBits(d, mono, 0, 4, NULL, &bi, DIB_RGB_COLORS));
        oracle_case("getdibits_query_header_mono");
        o_i64("ret", n);
        o_u32("bpp", bi.bmiHeader.biBitCount);
        o_u32("sizeimage", bi.bmiHeader.biSizeImage);
        oracle_case_end();
        memset(&bi, 0, sizeof bi);
        bi.bmiHeader.biSize = sizeof bi.bmiHeader;
        bi.bmiHeader.biWidth = 4;
        bi.bmiHeader.biHeight = 4;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        ORACLE_CALL(n, pGetDIBits(d, h, 0, 4, NULL, &bi, DIB_RGB_COLORS));
        oracle_case("getdibits_null_bits_with_bpp");
        o_i64("ret", n);
        o_u32("sizeimage", bi.bmiHeader.biSizeImage);
        oracle_case_end();
        pDeleteObject(mono);
        pDeleteDC(d);
        pDeleteObject(h);
    }

    /* ---- probes: StretchBlt sampling ---- */
    {
        HDC s8 = pCreateCompatibleDC(NULL), d8 = pCreateCompatibleDC(NULL);
        DWORD *sb, *dbb;
        HBITMAP hs = dib32(8, 1, &sb), hd = dib32(8, 1, &dbb);
        pSelectObject(s8, hs);
        pSelectObject(d8, hd);
        static const struct { const char *name; int mode, dw, sw; } st[] = {
            {"stretch_8_to_4_coloroncolor", COLORONCOLOR, 4, 8},
            {"stretch_8_to_2_coloroncolor", COLORONCOLOR, 2, 8},
            {"stretch_8_to_3_coloroncolor", COLORONCOLOR, 3, 8},
            {"stretch_8_to_5_coloroncolor", COLORONCOLOR, 5, 8},
            {"stretch_3_to_8_coloroncolor", COLORONCOLOR, 8, 3},
            {"stretch_8_to_4_blackonwhite", BLACKONWHITE, 4, 8},
            {"stretch_8_to_4_whiteonblack", WHITEONBLACK, 4, 8},
        };
        for (size_t i = 0; i < sizeof st / sizeof st[0]; i++) {
            BOOL r;
            for (int k = 0; k < 8; k++) {
                sb[k] = 0x00010101u * (DWORD)(k * 0x11 + 1);
                dbb[k] = 0x00eeeeee;
            }
            pSetStretchBltMode(d8, st[i].mode);
            ORACLE_CALL(r, pStretchBlt(d8, 0, 0, st[i].dw, 1, s8, 0, 0, st[i].sw, 1, SRCCOPY));
            oracle_case(st[i].name);
            o_bool("ret", r);
            o_bytes("dst", dbb, 32);
            oracle_case_end();
        }
        pDeleteDC(s8);
        pDeleteDC(d8);
        pDeleteObject(hs);
        pDeleteObject(hd);
    }
    return oracle_end();
}
