/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: which image of a multi-size icon is used for a requested
 * size. An icon file or icon group resource holds several images (16, 32,
 * 48 pixels, ...); LookupIconIdFromDirectoryEx names the one it would use
 * and LoadImage (from a file) and CreateIconFromResourceEx scale it to the
 * size asked for.
 *
 * Cases: the id chosen from group directories for a range of requested
 * sizes (24 pixels sits between 16 and 32, 40 between 32 and 48, 64 between
 * 48 and 256, ...), with different image sets, bit depths and directory
 * orders, and with LR_DEFAULTSIZE; then LoadImage of .ico files, recording
 * the size of the bitmap that comes back and the centre pixel of a
 * solid-colour image, which says which source image was scaled.
 *
 * Setu's shell asks for 24 pixel icons (the taskbar and the app list); this
 * test records which source image they come from.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-lookupiconidfromdirectoryex
 *       https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-loadimagew
 *       https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-createiconfromresourceex
 *       https://learn.microsoft.com/en-us/windows/win32/menurc/icon-resources
 *       https://learn.microsoft.com/en-us/windows/win32/menurc/icondir
 */
#include "oracle.h"

#define DECL(f) static __typeof__(f) *p##f
#define LOAD(m, f) (*(FARPROC *)&p##f = GetProcAddress(m, #f))

DECL(LookupIconIdFromDirectoryEx);
DECL(LoadImageW);
DECL(CreateIconFromResourceEx);
DECL(GetIconInfo);
DECL(DestroyIcon);
DECL(GetDC);
DECL(ReleaseDC);
DECL(GetDIBits);
DECL(GetObjectW);
DECL(DeleteObject);

#pragma pack(push, 1)
struct dirent { BYTE w, h, colors, reserved; WORD planes, bits; DWORD bytes; DWORD offset_or_id; };
#pragma pack(pop)

struct spec { int size; int bits; };

/* the colour of a source image, so a scaled copy says where it came from */
static DWORD color_of(int size)
{
    switch (size)
    {
    case 16: return 0xffff0000;   /* red */
    case 24: return 0xffffff00;   /* yellow */
    case 32: return 0xff00ff00;   /* green */
    case 48: return 0xff0000ff;   /* blue */
    case 64: return 0xffff00ff;   /* magenta */
    default: return 0xffffffff;   /* white */
    }
}

/* One image: BITMAPINFOHEADER, 32 bit pixels (or 8 bit indexed), AND mask. */
static BYTE *make_image(int size, int bits, DWORD *len)
{
    int stride = ((size * (bits == 8 ? 8 : 32) + 31) / 32) * 4, mstride = ((size + 31) / 32) * 4, i;
    DWORD pal = bits == 8 ? 256 * 4 : 0;
    DWORD n = 40 + pal + stride * size + mstride * size;
    BYTE *b = calloc(1, n), *p;
    BITMAPINFOHEADER *h = (BITMAPINFOHEADER *)b;
    h->biSize = 40;
    h->biWidth = size;
    h->biHeight = size * 2;
    h->biPlanes = 1;
    h->biBitCount = bits == 8 ? 8 : 32;
    h->biSizeImage = stride * size + mstride * size;
    p = b + 40;
    if (bits == 8)
    {
        DWORD c = color_of(size);
        ((DWORD *)p)[1] = c & 0xffffff;
        p += pal;
        memset(p, 1, stride * size);
    }
    else
        for (i = 0; i < size * size; i++) ((DWORD *)p)[i] = color_of(size);
    *len = n;
    return b;
}

/* An .ico file, or with GROUP an RT_GROUP_ICON directory whose ids are the sizes. */
static BYTE *make_dir(const struct spec *s, int n, int group, DWORD *total)
{
    DWORD hdr = 6 + n * (group ? 14 : 16), off = hdr, sum = hdr;
    BYTE *img[16], *out;
    DWORD lens[16];
    int i;
    for (i = 0; i < n; i++)
    {
        img[i] = make_image(s[i].size, s[i].bits, &lens[i]);
        sum += group ? 0 : lens[i];
    }
    out = calloc(1, sum);
    ((WORD *)out)[1] = 1;
    ((WORD *)out)[2] = n;
    for (i = 0; i < n; i++)
    {
        BYTE *e = out + 6 + i * (group ? 14 : 16);
        e[0] = e[1] = s[i].size >= 256 ? 0 : s[i].size;
        *(WORD *)(e + 4) = 1;
        *(WORD *)(e + 6) = s[i].bits;
        *(DWORD *)(e + 8) = lens[i];
        if (group) *(WORD *)(e + 12) = s[i].size * 100 + s[i].bits;  /* id */
        else
        {
            *(DWORD *)(e + 12) = off;
            memcpy(out + off, img[i], lens[i]);
            off += lens[i];
        }
        free(img[i]);
    }
    *total = sum;
    return out;
}

static const int sizes[] = { 16, 20, 24, 28, 32, 36, 40, 48, 56, 64, 96, 128, 256 };

static void lookup_set(const char *name, const struct spec *s, int n, UINT flags)
{
    DWORD len;
    BYTE *dir = make_dir(s, n, 1, &len);
    size_t i;
    oracle_case(name);
    for (i = 0; i < sizeof sizes / sizeof sizes[0]; i++)
    {
        int id;
        char key[16];
        ORACLE_CALL(id, pLookupIconIdFromDirectoryEx(dir, TRUE, sizes[i], sizes[i], flags));
        snprintf(key, sizeof key, "at_%d", sizes[i]);
        o_i64(key, id);
    }
    oracle_case_end();
    free(dir);
}

static void lookup_odd(const char *name, const struct spec *s, int n, int cx, int cy, UINT flags)
{
    DWORD len;
    BYTE *dir = make_dir(s, n, 1, &len);
    int id;
    ORACLE_CALL(id, pLookupIconIdFromDirectoryEx(dir, TRUE, cx, cy, flags));
    oracle_case(name);
    o_i64("id", id);
    o_lasterr();
    oracle_case_end();
    free(dir);
}

static void icon_result(HICON icon)
{
    ICONINFO ii;
    BITMAP bm;
    DWORD px = 0;
    int ok;
    o_bool("icon", icon != NULL);
    if (!icon) return;
    ok = pGetIconInfo(icon, &ii);
    o_bool("info", ok);
    if (!ok) return;
    memset(&bm, 0, sizeof bm);
    pGetObjectW(ii.hbmColor ? ii.hbmColor : ii.hbmMask, sizeof bm, &bm);
    o_i64("w", bm.bmWidth);
    o_i64("h", bm.bmHeight);
    if (ii.hbmColor)
    {
        BITMAPINFO bi;
        HDC dc = pGetDC(NULL);
        DWORD *pix = calloc(bm.bmWidth * bm.bmHeight, 4);
        memset(&bi, 0, sizeof bi);
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = bm.bmWidth;
        bi.bmiHeader.biHeight = -bm.bmHeight;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        if (pGetDIBits(dc, ii.hbmColor, 0, bm.bmHeight, pix, &bi, DIB_RGB_COLORS))
            px = pix[(bm.bmHeight / 2) * bm.bmWidth + bm.bmWidth / 2];
        /* scaling may change a channel by one: name the source instead */
        {
            static const int srcs[] = { 16, 24, 32, 48, 64, 256 };
            int k, best = 0, bd = 1 << 30;
            for (k = 0; k < 6; k++)
            {
                DWORD c = color_of(srcs[k]);
                int d = 0, sh;
                for (sh = 0; sh < 32; sh += 8) d += abs((int)((px >> sh) & 255) - (int)((c >> sh) & 255));
                if (d < bd) { bd = d; best = srcs[k]; }
            }
            o_i64("source", bd <= 12 ? best : -1);
            o_bool("opaque", (px >> 24) >= 0xf0);
        }
        free(pix);
        pReleaseDC(NULL, dc);
        pDeleteObject(ii.hbmColor);
    }
    if (ii.hbmMask) pDeleteObject(ii.hbmMask);
}

static void file_set(const char *name, const struct spec *s, int n, int cx, int cy, UINT flags)
{
    DWORD len, w;
    BYTE *ico = make_dir(s, n, 0, &len);
    HANDLE f = CreateFileW(L"sel.ico", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    HICON icon;
    WriteFile(f, ico, len, &w, NULL);
    CloseHandle(f);
    ORACLE_CALL(icon, (HICON)pLoadImageW(NULL, L"sel.ico", IMAGE_ICON, cx, cy, LR_LOADFROMFILE | flags));
    oracle_case(name);
    icon_result(icon);
    oracle_case_end();
    if (icon) pDestroyIcon(icon);
    DeleteFileW(L"sel.ico");
    free(ico);
}

static void resource_image(const char *name, int size, int cx, int cy)
{
    DWORD len;
    BYTE *img = make_image(size, 32, &len);
    HICON icon;
    ORACLE_CALL(icon, pCreateIconFromResourceEx(img, len, TRUE, 0x00030000, cx, cy, 0));
    oracle_case(name);
    icon_result(icon);
    oracle_case_end();
    if (icon) pDestroyIcon(icon);
    free(img);
}

int main(void)
{
    static const struct spec s3[] = { { 16, 32 }, { 32, 32 }, { 48, 32 } };
    static const struct spec s3r[] = { { 48, 32 }, { 32, 32 }, { 16, 32 } };
    static const struct spec s2[] = { { 16, 32 }, { 48, 32 } };
    static const struct spec s4[] = { { 16, 32 }, { 32, 32 }, { 48, 32 }, { 256, 32 } };
    static const struct spec s16_24_32[] = { { 16, 32 }, { 24, 32 }, { 32, 32 } };
    static const struct spec sd[] = { { 16, 32 }, { 32, 8 }, { 32, 32 } };
    static const struct spec s16_64[] = { { 16, 32 }, { 64, 32 } };
    static const struct spec s64_16[] = { { 64, 32 }, { 16, 32 } };
    static const struct spec s1[] = { { 32, 32 } };
    static const struct spec s16only[] = { { 16, 32 } };
    HMODULE u32, g32;

    oracle_begin("user32/icon_select");
    u32 = LoadLibraryW(L"user32.dll");
    g32 = LoadLibraryW(L"gdi32.dll");
    LOAD(u32, LookupIconIdFromDirectoryEx);
    LOAD(u32, LoadImageW);
    LOAD(u32, CreateIconFromResourceEx);
    LOAD(u32, GetIconInfo);
    LOAD(u32, DestroyIcon);
    LOAD(u32, GetDC);
    LOAD(u32, ReleaseDC);
    LOAD(g32, GetDIBits);
    LOAD(g32, GetObjectW);
    LOAD(g32, DeleteObject);

    /* the id is size * 100 + bits */
    lookup_set("lookup_16_32_48", s3, 3, 0);
    lookup_set("lookup_48_32_16_reversed", s3r, 3, 0);
    lookup_set("lookup_16_48", s2, 2, 0);
    lookup_set("lookup_16_32_48_256", s4, 4, 0);
    lookup_set("lookup_16_24_32", s16_24_32, 3, 0);
    lookup_set("lookup_32only", s1, 1, 0);
    lookup_set("lookup_16only", s16only, 1, 0);
    lookup_set("lookup_depths", sd, 3, 0);
    lookup_set("lookup_16_32_48_monochrome", s3, 3, LR_MONOCHROME);
    lookup_odd("lookup_default_size", s3, 3, 0, 0, LR_DEFAULTSIZE);
    lookup_odd("lookup_zero_size", s3, 3, 0, 0, 0);
    lookup_odd("lookup_wide_24x40", s3, 3, 24, 40, 0);
    lookup_odd("lookup_wide_16x48", s3, 3, 16, 48, 0);
    lookup_odd("lookup_cx_only_24", s3, 3, 24, 0, 0);

    /* probes: an exact tie in ratio (32 is twice 16 and half of 64) and
     * requests that are not square */
    lookup_odd("probe_tie_16_64_at_32", s16_64, 2, 32, 32, 0);
    lookup_odd("probe_tie_64_16_at_32", s64_16, 2, 32, 32, 0);
    lookup_odd("probe_24x32", s3, 3, 24, 32, 0);
    lookup_odd("probe_32x24", s3, 3, 32, 24, 0);
    lookup_odd("probe_16x32", s3, 3, 16, 32, 0);
    lookup_odd("probe_32x16", s3, 3, 32, 16, 0);
    lookup_odd("probe_40x24", s3, 3, 40, 24, 0);
    lookup_odd("probe_24x24_cy0_cx0", s3, 3, 0, 24, 0);
    lookup_odd("probe_20x28", s3, 3, 20, 28, 0);
    lookup_odd("probe_28x20", s3, 3, 28, 20, 0);
    lookup_odd("probe_30x26", s3, 3, 30, 26, 0);
    lookup_odd("probe_26x30", s3, 3, 26, 30, 0);
    lookup_odd("probe_24x40_16_48", s2, 2, 24, 40, 0);

    file_set("file_24_from_16_32_48", s3, 3, 24, 24, 0);
    file_set("file_20_from_16_32_48", s3, 3, 20, 20, 0);
    file_set("file_28_from_16_32_48", s3, 3, 28, 28, 0);
    file_set("file_40_from_16_32_48", s3, 3, 40, 40, 0);
    file_set("file_64_from_16_32_48", s3, 3, 64, 64, 0);
    file_set("file_24_from_16_48", s2, 2, 24, 24, 0);
    file_set("file_24_from_16_24_32", s16_24_32, 3, 24, 24, 0);
    file_set("file_24_from_16", s16only, 1, 24, 24, 0);
    file_set("file_24_from_32", s1, 1, 24, 24, 0);
    file_set("file_24_from_256", s4, 4, 24, 24, 0);
    file_set("file_default_size", s3, 3, 0, 0, LR_DEFAULTSIZE);
    file_set("file_zero_size", s3, 3, 0, 0, 0);

    resource_image("resource_32_as_24", 32, 24, 24);
    resource_image("resource_16_as_24", 16, 24, 24);
    resource_image("resource_48_as_24", 48, 24, 24);
    resource_image("resource_32_as_0", 32, 0, 0);

    return oracle_end();
}
