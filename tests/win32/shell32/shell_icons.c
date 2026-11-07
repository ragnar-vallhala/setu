/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: the HKLM ...\Explorer\Shell Icons overrides, which replace
 * the shell's standard icons (index -> "path,index", the index being the
 * shell's stock icon number: 0 document, 3 folder, 4 open folder,
 * 8 fixed drive, ...). Setu theme packs set them so Windows programs show
 * the pack's folder, drive and file icons (docs/THEMES.md).
 *
 * The test writes a red icon file, points Shell Icons 0, 3, 4 and 8 at it
 * before shell32 builds its icon lists, then looks at what programs get:
 * SHGetStockIconInfo (location, icon, system image list index),
 * SHGetFileInfo (SHGFI_ICON, SHGFI_SYSICONINDEX, SHGFI_ICONLOCATION) for a
 * folder, the drive root, a file with an unknown extension and a file
 * of a type registered by the test with its own DefaultIcon, and the system image list entries. Icons are printed only as
 * "red" (drawn from the override), "other" or "none", never as pixels,
 * handles or index values. Paths are printed only as "ours" booleans.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-shgetstockiconinfo
 *       https://learn.microsoft.com/en-us/windows/win32/api/shellapi/ne-shellapi-shstockiconid
 *       https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-shgetfileinfow
 *       https://learn.microsoft.com/en-us/windows/win32/api/commctrl/nf-commctrl-imagelist_geticon
 *       https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-drawiconex
 */
#include "oracle.h"
#include <shellapi.h>
#include <shlobj.h>
#include <commctrl.h>

#define DECL(f) static __typeof__(f) *p##f
#define LOAD(m, f) (*(FARPROC *)&p##f = GetProcAddress(m, #f))

DECL(SHGetStockIconInfo);
DECL(SHGetFileInfoW);
DECL(SHChangeNotify);
DECL(ImageList_GetIcon);
DECL(DrawIconEx);
DECL(DestroyIcon);
DECL(CreateCompatibleDC);
DECL(CreateDIBSection);
DECL(SelectObject);
DECL(DeleteDC);
DECL(DeleteObject);
static HRESULT(WINAPI *pCoInitializeEx)(void *, DWORD);

#define SHELL_ICONS L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Shell Icons"

static wchar_t ico_path[MAX_PATH];
static HIMAGELIST small_list; /* the system image list, small icons */

/* A 16x16 and a 32x32 32-bpp icon, opaque red. */
static void write_red_icon(const wchar_t *path)
{
    static const int sizes[2] = {16, 32};
    unsigned char *buf = calloc(1, 16384);
    size_t off = 6 + 16 * 2;
    buf[2] = 1; /* type: icon */
    buf[4] = 2; /* two images */
    for (int i = 0; i < 2; i++) {
        int s = sizes[i];
        DWORD xor_size = s * s * 4, and_size = ((s + 31) / 32) * 4 * s;
        DWORD bytes = 40 + xor_size + and_size;
        unsigned char *e = buf + 6 + 16 * i;
        e[0] = (unsigned char)s;
        e[1] = (unsigned char)s;
        e[4] = 1;  /* planes */
        e[6] = 32; /* bpp */
        memcpy(e + 8, &bytes, 4);
        DWORD o = (DWORD)off;
        memcpy(e + 12, &o, 4);
        BITMAPINFOHEADER bih = {sizeof bih, s, s * 2, 1, 32, BI_RGB, xor_size + and_size, 0, 0, 0, 0};
        memcpy(buf + off, &bih, sizeof bih);
        unsigned char *px = buf + off + 40;
        for (int p = 0; p < s * s; p++) {
            px[p * 4 + 0] = 0x00;
            px[p * 4 + 1] = 0x00;
            px[p * 4 + 2] = 0xff;
            px[p * 4 + 3] = 0xff;
        }
        off += bytes; /* the AND mask stays zero: opaque */
    }
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    DWORD n;
    WriteFile(h, buf, (DWORD)off, &n, NULL);
    CloseHandle(h);
    free(buf);
}

/* A file type of our own with its own DefaultIcon: an associated type
 * keeps its icon whatever Shell Icons says. */
static void register_assoc(void)
{
    HKEY k;
    static const wchar_t progid[] = L"SetuQq.Assoc";
    wchar_t icon[MAX_PATH + 8];
    GetSystemDirectoryW(icon, MAX_PATH);
    wcscat(icon, L"\\shell32.dll,1");
    if (!RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\.setuqqassoc", 0, NULL, 0, KEY_WRITE, NULL, &k, NULL)) {
        RegSetValueExW(k, NULL, 0, REG_SZ, (const BYTE *)progid, sizeof progid);
        RegCloseKey(k);
    }
    if (!RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\SetuQq.Assoc\\DefaultIcon", 0, NULL, 0, KEY_WRITE, NULL,
                         &k, NULL)) {
        RegSetValueExW(k, NULL, 0, REG_SZ, (const BYTE *)icon, (DWORD)(wcslen(icon) + 1) * sizeof(wchar_t));
        RegCloseKey(k);
    }
}

static void set_shell_icon(const wchar_t *index, const wchar_t *value)
{
    HKEY h;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, SHELL_ICONS, 0, NULL, 0, KEY_WRITE, NULL, &h, NULL)) return;
    if (value)
        RegSetValueExW(h, index, 0, REG_SZ, (const BYTE *)value, (DWORD)(wcslen(value) + 1) * sizeof(wchar_t));
    else
        RegDeleteValueW(h, index);
    RegCloseKey(h);
}

/* "red" when the icon draws the override's red, "other" when it draws
 * something else, "none" when there's no icon or it can't be drawn. */
static const char *icon_color(HICON icon)
{
    if (!icon) return "none";
    BITMAPINFO bi;
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = 48;
    bi.bmiHeader.biHeight = -48;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    DWORD *bits = NULL;
    HDC dc = pCreateCompatibleDC(NULL);
    HBITMAP bmp = pCreateDIBSection(dc, &bi, DIB_RGB_COLORS, (void **)&bits, NULL, 0);
    const char *ret = "none";
    if (bmp && bits) {
        HGDIOBJ old = pSelectObject(dc, bmp);
        for (int i = 0; i < 48 * 48; i++) bits[i] = 0x00ffffff;
        if (pDrawIconEx(dc, 0, 0, icon, 0, 0, 0, NULL, DI_NORMAL)) {
            int red = 0;
            /* sample the middle of a 16 px icon */
            for (int y = 5; y < 11; y++)
                for (int x = 5; x < 11; x++) {
                    DWORD c = bits[y * 48 + x] & 0xffffff;
                    if (c == 0xff0000) red++;
                }
            ret = red == 36 ? "red" : "other";
        }
        pSelectObject(dc, old);
    }
    if (bmp) pDeleteObject(bmp);
    pDeleteDC(dc);
    return ret;
}

static int path_is_ours(const wchar_t *path)
{
    return path && !lstrcmpiW(path, ico_path);
}

static void stock(const char *name, SHSTOCKICONID id)
{
    SHSTOCKICONINFO sii;
    HRESULT hr;
    char nm[96];

    memset(&sii, 0, sizeof sii);
    sii.cbSize = sizeof sii;
    ORACLE_CALL(hr, pSHGetStockIconInfo(id, SHGSI_ICONLOCATION, &sii));
    snprintf(nm, sizeof nm, "stock_%s_location", name);
    oracle_case(nm);
    o_hex32("hr", (DWORD)hr);
    o_bool("path_ours", path_is_ours(sii.szPath));
    o_bool("path_set", sii.szPath[0] != 0);
    if (path_is_ours(sii.szPath)) o_i64("icon", sii.iIcon);
    oracle_case_end();

    static const struct { const char *name; UINT flags; } kinds[] = {
        {"large", SHGSI_ICON | SHGSI_LARGEICON},
        {"small", SHGSI_ICON | SHGSI_SMALLICON},
        {"shellsize", SHGSI_ICON | SHGSI_SHELLICONSIZE},
    };
    for (size_t k = 0; k < sizeof kinds / sizeof kinds[0]; k++) {
        memset(&sii, 0, sizeof sii);
        sii.cbSize = sizeof sii;
        ORACLE_CALL(hr, pSHGetStockIconInfo(id, kinds[k].flags, &sii));
        snprintf(nm, sizeof nm, "stock_%s_icon_%s", name, kinds[k].name);
        oracle_case(nm);
        o_hex32("hr", (DWORD)hr);
        o_str("icon", icon_color(sii.hIcon));
        oracle_case_end();
        if (sii.hIcon) pDestroyIcon(sii.hIcon);
    }

    memset(&sii, 0, sizeof sii);
    sii.cbSize = sizeof sii;
    sii.iSysImageIndex = -77;
    ORACLE_CALL(hr, pSHGetStockIconInfo(id, SHGSI_SYSICONINDEX, &sii));
    snprintf(nm, sizeof nm, "stock_%s_sysindex", name);
    oracle_case(nm);
    o_hex32("hr", (DWORD)hr);
    o_bool("index_valid", sii.iSysImageIndex >= 0);
    HICON icon = NULL;
    if (sii.iSysImageIndex >= 0 && small_list)
        icon = pImageList_GetIcon(small_list, sii.iSysImageIndex, ILD_NORMAL);
    o_str("image_list_icon", icon_color(icon));
    oracle_case_end();
    if (icon) pDestroyIcon(icon);
}

static void file_info(const char *name, const wchar_t *path, DWORD fattrs, UINT extra)
{
    SHFILEINFOW fi;
    DWORD_PTR r;
    char nm[96];

    memset(&fi, 0, sizeof fi);
    ORACLE_CALL(r, pSHGetFileInfoW(path, fattrs, &fi, sizeof fi, SHGFI_ICON | SHGFI_SMALLICON | extra));
    snprintf(nm, sizeof nm, "fileinfo_%s_icon", name);
    oracle_case(nm);
    o_bool("ret_nonzero", r != 0);
    o_str("icon", icon_color(fi.hIcon));
    oracle_case_end();
    if (fi.hIcon) pDestroyIcon(fi.hIcon);

    memset(&fi, 0, sizeof fi);
    ORACLE_CALL(r, pSHGetFileInfoW(path, fattrs, &fi, sizeof fi, SHGFI_ICON | SHGFI_LARGEICON | extra));
    snprintf(nm, sizeof nm, "fileinfo_%s_icon_large", name);
    oracle_case(nm);
    o_bool("ret_nonzero", r != 0);
    o_str("icon", icon_color(fi.hIcon));
    oracle_case_end();
    if (fi.hIcon) pDestroyIcon(fi.hIcon);

    memset(&fi, 0, sizeof fi);
    fi.iIcon = -77;
    ORACLE_CALL(r, pSHGetFileInfoW(path, fattrs, &fi, sizeof fi, SHGFI_SYSICONINDEX | SHGFI_SMALLICON | extra));
    snprintf(nm, sizeof nm, "fileinfo_%s_sysindex", name);
    oracle_case(nm);
    o_bool("ret_nonzero", r != 0);
    HICON icon = NULL;
    if (r && fi.iIcon >= 0) icon = pImageList_GetIcon((HIMAGELIST)r, fi.iIcon, ILD_NORMAL);
    o_str("image_list_icon", icon_color(icon));
    oracle_case_end();
    if (icon) pDestroyIcon(icon);

    memset(&fi, 0, sizeof fi);
    ORACLE_CALL(r, pSHGetFileInfoW(path, fattrs, &fi, sizeof fi, SHGFI_ICONLOCATION | extra));
    snprintf(nm, sizeof nm, "fileinfo_%s_location", name);
    oracle_case(nm);
    o_bool("ret_nonzero", r != 0);
    o_bool("path_ours", path_is_ours(fi.szDisplayName));
    if (path_is_ours(fi.szDisplayName)) o_i64("icon", fi.iIcon);
    oracle_case_end();
}

int main(void)
{
    oracle_begin("shell32/shell_icons");
    HMODULE sh = LoadLibraryW(L"shell32.dll");
    HMODULE cc = LoadLibraryW(L"comctl32.dll");
    HMODULE us = LoadLibraryW(L"user32.dll");
    HMODULE gd = LoadLibraryW(L"gdi32.dll");
    HMODULE ole = LoadLibraryW(L"ole32.dll");
    LOAD(sh, SHGetStockIconInfo);
    LOAD(sh, SHGetFileInfoW);
    LOAD(sh, SHChangeNotify);
    LOAD(cc, ImageList_GetIcon);
    LOAD(us, DrawIconEx);
    LOAD(us, DestroyIcon);
    LOAD(gd, CreateCompatibleDC);
    LOAD(gd, CreateDIBSection);
    LOAD(gd, SelectObject);
    LOAD(gd, DeleteDC);
    LOAD(gd, DeleteObject);
    LOAD(ole, CoInitializeEx);
    int ok = pSHGetStockIconInfo && pSHGetFileInfoW && pSHChangeNotify && pImageList_GetIcon &&
             pDrawIconEx && pDestroyIcon && pCreateCompatibleDC && pCreateDIBSection && pSelectObject &&
             pDeleteDC && pDeleteObject && pCoInitializeEx;
    oracle_case("setup");
    o_bool("loaded", ok);
    oracle_case_end();
    if (!ok) return oracle_end();
    pCoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    GetFullPathNameW(L"setu-red.ico", MAX_PATH, ico_path, NULL);
    write_red_icon(ico_path);
    CreateDirectoryW(L"folder", NULL);
    HANDLE h = CreateFileW(L"plain.setuqqassoc", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    CloseHandle(h);
    register_assoc();

    /* The red icon itself, so a "red" below means the override was used. */
    HICON own = NULL;
    {
        HICON(WINAPI * pExtractIconW)(HINSTANCE, LPCWSTR, UINT);
        *(FARPROC *)&pExtractIconW = GetProcAddress(sh, "ExtractIconW");
        if (pExtractIconW) own = pExtractIconW(NULL, ico_path, 0);
        oracle_case("own_icon");
        o_str("icon", icon_color(own));
        oracle_case_end();
        if (own && own != (HICON)1) pDestroyIcon(own);
    }

    /* Before shell32 builds any icon list: point 0 (document), 3 (folder),
     * 4 (open folder) and 8 (fixed drive) at the red icon. */
    wchar_t value[MAX_PATH + 8];
    swprintf(value, MAX_PATH + 8, L"%ls,0", ico_path);
    set_shell_icon(L"0", value);
    set_shell_icon(L"3", value);
    set_shell_icon(L"4", value);
    set_shell_icon(L"8", value);

    {
        SHFILEINFOW fi;
        memset(&fi, 0, sizeof fi);
        small_list = (HIMAGELIST)pSHGetFileInfoW(L"x.setuqqzz", FILE_ATTRIBUTE_NORMAL, &fi, sizeof fi,
                                                 SHGFI_USEFILEATTRIBUTES | SHGFI_SYSICONINDEX | SHGFI_SMALLICON);
    }

    stock("folder", SIID_FOLDER);
    stock("folder_open", SIID_FOLDEROPEN);
    stock("drive_fixed", SIID_DRIVEFIXED);
    stock("doc_no_assoc", SIID_DOCNOASSOC);
    /* not overridden */
    stock("drive_cd", SIID_DRIVECD);
    stock("recycler", SIID_RECYCLER);

    file_info("folder", L"folder", 0, 0);
    file_info("folder_ufa", L"nothing_dir", FILE_ATTRIBUTE_DIRECTORY, SHGFI_USEFILEATTRIBUTES);
    file_info("drive_root", L"C:\\", 0, 0);
    file_info("unknown_ext_ufa", L"nothing.setuqqzz", FILE_ATTRIBUTE_NORMAL, SHGFI_USEFILEATTRIBUTES);
    file_info("assoc", L"plain.setuqqassoc", 0, 0);

    /* Open folder: SHGFI_OPENICON. */
    {
        SHFILEINFOW fi;
        DWORD_PTR r;
        memset(&fi, 0, sizeof fi);
        ORACLE_CALL(r, pSHGetFileInfoW(L"folder", 0, &fi, sizeof fi, SHGFI_ICON | SHGFI_SMALLICON | SHGFI_OPENICON));
        oracle_case("fileinfo_folder_openicon");
        o_bool("ret_nonzero", r != 0);
        o_str("icon", icon_color(fi.hIcon));
        oracle_case_end();
        if (fi.hIcon) pDestroyIcon(fi.hIcon);
    }

    set_shell_icon(L"0", NULL);
    set_shell_icon(L"3", NULL);
    set_shell_icon(L"4", NULL);
    set_shell_icon(L"8", NULL);
    DeleteFileW(L"plain.setuqqassoc");
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\.setuqqassoc");
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\SetuQq.Assoc");
    RemoveDirectoryW(L"folder");
    DeleteFileW(ico_path);
    return oracle_end();
}
