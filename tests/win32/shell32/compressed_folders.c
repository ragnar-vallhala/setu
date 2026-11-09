/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: archives as compressed folders. Which extensions the shell
 * treats as folders (registry ProgIDs and their CLSID, SHGetFileInfo type
 * names and attributes, IShellFolder display names), and what binding to
 * an archive gives: IShellFolder::EnumObjects over its entries (nested
 * and implicit directories), the entries' names and attributes,
 * ParseDisplayName inside the archive and through the Desktop, and the
 * data object used to copy entries out (FILEDESCRIPTORW / FILECONTENTS).
 * Also: the canonical verbs on an archive's context menu, and whether
 * the common file dialog returns a typed archive name as a file.
 *
 * The archives are built by the test (stored zip, ustar tar, gzip with
 * stored deflate blocks), so no third-party data is involved. Other
 * formats (.7z, .rar, ...) are files with junk content: only their names
 * and attributes are checked, which the shell computes without reading
 * the file.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nn-shobjidl_core-ishellfolder
 *       https://learn.microsoft.com/en-us/windows/win32/shell/sfgao
 *       https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-shgetfileinfow
 *       https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nn-shobjidl_core-ipersistfolder2
 *       https://learn.microsoft.com/en-us/windows/win32/shell/clipboard (CFSTR_FILEDESCRIPTOR, CFSTR_FILECONTENTS)
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/ns-shlobj_core-filedescriptorw
 *       https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-icontextmenu-getcommandstring
 *       https://learn.microsoft.com/en-us/windows/win32/api/commdlg/nf-commdlg-getopenfilenamew
 *       https://pkware.cachefly.net/webdocs/casestudies/APPNOTE.TXT (zip), RFC 1951/1952 (deflate, gzip),
 *       https://www.gnu.org/software/tar/manual/html_node/Standard.html (ustar)
 */
#define INITGUID
#include "oracle.h"
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <commdlg.h>

static HRESULT(WINAPI *pSHGetDesktopFolder)(IShellFolder **);
static DWORD_PTR(WINAPI *pSHGetFileInfoW)(LPCWSTR, DWORD, SHFILEINFOW *, UINT, UINT);
static HRESULT(WINAPI *pStrRetToBufW)(STRRET *, PCUITEMID_CHILD, LPWSTR, UINT);
static HRESULT(WINAPI *pCoInitializeEx)(void *, DWORD);
static void(WINAPI *pCoTaskMemFree)(void *);
static void(WINAPI *pReleaseStgMedium)(STGMEDIUM *);
static BOOL(WINAPI *pILIsEqual)(PCIDLIST_ABSOLUTE, PCIDLIST_ABSOLUTE);
static PIDLIST_ABSOLUTE(WINAPI *pILCombine)(PCIDLIST_ABSOLUTE, PCUIDLIST_RELATIVE);
static BOOL(WINAPI *pSHGetPathFromIDListW)(PCIDLIST_ABSOLUTE, LPWSTR);
static HRESULT(WINAPI *pSHCreateItemFromParsingName)(PCWSTR, IBindCtx *, REFIID, void **);
static BOOL(WINAPI *pGetOpenFileNameW)(OPENFILENAMEW *);
static DWORD(WINAPI *pCommDlgExtendedError)(void);

#define LOAD(m, f) (*(FARPROC *)&p##f = GetProcAddress(m, #f))

static const GUID iid_extract_icon_w = {0x000214fa, 0, 0, {0xc0, 0, 0, 0, 0, 0, 0, 0x46}};

#define ATTR_MASK (SFGAO_CANCOPY | SFGAO_CANMOVE | SFGAO_CANLINK | SFGAO_CANRENAME | SFGAO_CANDELETE | \
                   SFGAO_HASPROPSHEET | SFGAO_DROPTARGET | SFGAO_LINK | SFGAO_READONLY | SFGAO_HIDDEN | \
                   SFGAO_FOLDER | SFGAO_FILESYSTEM | SFGAO_HASSUBFOLDER | SFGAO_STREAM | SFGAO_FILESYSANCESTOR | \
                   SFGAO_STORAGE | SFGAO_STORAGEANCESTOR | SFGAO_BROWSABLE | SFGAO_COMPRESSED | SFGAO_ENCRYPTED)

static IShellFolder *desktop, *tdir_sf;
static wchar_t cwd[MAX_PATH];

static void o_path(const char *k, const wchar_t *p)
{
    wchar_t out[2 * MAX_PATH];
    size_t n = wcslen(cwd);
    if (!_wcsnicmp(p, cwd, n)) swprintf(out, 2 * MAX_PATH, L"<cwd>%ls", p + n);
    else wcscpy(out, p);
    o_wstr(k, out);
}

/* ---- archive builders ---- */

static unsigned char buf[65536];
static size_t blen;

static void put(const void *p, size_t n) { memcpy(buf + blen, p, n); blen += n; }
static void put16(unsigned v) { unsigned char b[2] = {v & 0xff, (v >> 8) & 0xff}; put(b, 2); }
static void put32(DWORD v) { put16(v & 0xffff); put16(v >> 16); }

static DWORD crc32(const unsigned char *p, size_t n)
{
    DWORD c = 0xffffffff;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xedb88320 & (0 - (c & 1)));
    }
    return ~c;
}

/* attrs: zip external attributes (0 = from the name); utf8: set general purpose bit 11. */
struct entry { const char *name; const char *data; DWORD attrs; int utf8; };

/* 2026-01-02 03:04:06 in DOS format. */
#define DOS_TIME ((3 << 11) | (4 << 5) | 3)
#define DOS_DATE (((2026 - 1980) << 9) | (1 << 5) | 2)

static void build_zip(const struct entry *e, int n)
{
    DWORD offs[32];
    blen = 0;
    for (int i = 0; i < n; i++) {
        size_t dl = e[i].data ? strlen(e[i].data) : 0;
        offs[i] = (DWORD)blen;
        put32(0x04034b50); put16(10); put16(e[i].utf8 ? 0x800 : 0); put16(0); put16(DOS_TIME); put16(DOS_DATE);
        put32(crc32((const unsigned char *)(e[i].data ? e[i].data : ""), dl));
        put32((DWORD)dl); put32((DWORD)dl); put16((unsigned)strlen(e[i].name)); put16(0);
        put(e[i].name, strlen(e[i].name));
        if (dl) put(e[i].data, dl);
    }
    DWORD cd = (DWORD)blen;
    for (int i = 0; i < n; i++) {
        size_t dl = e[i].data ? strlen(e[i].data) : 0;
        int dir = e[i].name[strlen(e[i].name) - 1] == '/';
        put32(0x02014b50); put16(20); put16(10); put16(e[i].utf8 ? 0x800 : 0); put16(0); put16(DOS_TIME); put16(DOS_DATE);
        put32(crc32((const unsigned char *)(e[i].data ? e[i].data : ""), dl));
        put32((DWORD)dl); put32((DWORD)dl); put16((unsigned)strlen(e[i].name)); put16(0); put16(0);
        put16(0); put16(0); put32(e[i].attrs ? e[i].attrs : dir ? 0x10 : 0x20); put32(offs[i]);
        put(e[i].name, strlen(e[i].name));
    }
    DWORD cdlen = (DWORD)blen - cd;
    put32(0x06054b50); put16(0); put16(0); put16(n); put16(n); put32(cdlen); put32(cd); put16(0);
}

static void tar_octal(char *dst, size_t width, unsigned long long v)
{
    char t[24];
    snprintf(t, sizeof t, "%0*llo", (int)(width - 1), v);
    memcpy(dst, t, width - 1);
    dst[width - 1] = 0;
}

static void build_tar(const struct entry *e, int n)
{
    blen = 0;
    for (int i = 0; i < n; i++) {
        char h[512];
        size_t dl = e[i].data ? strlen(e[i].data) : 0;
        int dir = e[i].name[strlen(e[i].name) - 1] == '/';
        memset(h, 0, sizeof h);
        strcpy(h, e[i].name);
        tar_octal(h + 100, 8, dir ? 0755 : 0644);
        tar_octal(h + 108, 8, 0);
        tar_octal(h + 116, 8, 0);
        tar_octal(h + 124, 12, dl);
        tar_octal(h + 136, 12, 1767322446ull); /* 2026-01-02 03:04:06 UTC */
        memset(h + 148, ' ', 8);
        h[156] = dir ? '5' : '0';
        memcpy(h + 257, "ustar", 6);
        memcpy(h + 263, "00", 2);
        unsigned sum = 0;
        for (int k = 0; k < 512; k++) sum += (unsigned char)h[k];
        snprintf(h + 148, 8, "%06o", sum);
        h[155] = ' ';
        put(h, 512);
        if (dl) {
            put(e[i].data, dl);
            size_t pad = (512 - dl % 512) % 512;
            memset(buf + blen, 0, pad);
            blen += pad;
        }
    }
    memset(buf + blen, 0, 1024);
    blen += 1024;
}

/* Wraps buf[0..blen) in gzip (one stored deflate block). */
static void gzip_buf(const char *fname)
{
    static unsigned char raw[65536];
    size_t n = blen;
    memcpy(raw, buf, n);
    blen = 0;
    unsigned char hdr[10] = {0x1f, 0x8b, 8, fname ? 8 : 0, 0, 0, 0, 0, 0, 0xff};
    put(hdr, 10);
    if (fname) put(fname, strlen(fname) + 1);
    unsigned char b = 1;
    put(&b, 1);
    put16((unsigned)n);
    put16((unsigned)~n & 0xffff);
    put(raw, n);
    put32(crc32(raw, n));
    put32((DWORD)n);
}

static void write_buf(const wchar_t *name)
{
    HANDLE h = CreateFileW(name, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    DWORD w;
    WriteFile(h, buf, (DWORD)blen, &w, NULL);
    CloseHandle(h);
}

static void write_junk(const wchar_t *name)
{
    HANDLE h = CreateFileW(name, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    DWORD w;
    WriteFile(h, "data", 4, &w, NULL);
    CloseHandle(h);
}

/* ---- registry ---- */

static void reg_str(HKEY root, const wchar_t *path, const wchar_t *value, const char *k)
{
    wchar_t v[512];
    DWORD sz = sizeof v, type = 0;
    LONG r = RegGetValueW(root, path, value, RRF_RT_ANY | RRF_NOEXPAND, &type, v, &sz);
    if (r) { o_str(k, "<none>"); return; }
    if (type == REG_DWORD) { o_hex32(k, *(DWORD *)v); return; }
    if (type != REG_SZ && type != REG_EXPAND_SZ) { o_u32(k, type); return; }
    o_wstr(k, v);
}

/* The last path component of a REG_(EXPAND_)SZ, lower-cased (no paths printed). */
static void reg_basename(HKEY root, const wchar_t *path, const char *k)
{
    wchar_t v[512];
    DWORD sz = sizeof v;
    if (RegGetValueW(root, path, NULL, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND, NULL, v, &sz)) {
        o_str(k, "<none>");
        return;
    }
    wchar_t *b = wcsrchr(v, L'\\');
    b = b ? b + 1 : v;
    CharLowerW(b);
    o_wstr(k, b);
}

static void reg_subkeys(HKEY root, const wchar_t *path, const char *k)
{
    HKEY h;
    char out[2048] = "";
    if (RegOpenKeyExW(root, path, 0, KEY_READ, &h)) { o_str(k, "<none>"); return; }
    wchar_t name[256];
    for (DWORD i = 0;; i++) {
        DWORD n = 256;
        if (RegEnumKeyExW(h, i, name, &n, NULL, NULL, NULL, NULL)) break;
        if (*out) strcat(out, ",");
        if (strlen(out) + n + 2 < sizeof out) strcat(out, oracle_utf8(name));
    }
    RegCloseKey(h);
    o_str(k, out);
}

static void reg_values(HKEY root, const wchar_t *path, const char *k)
{
    HKEY h;
    char out[2048] = "";
    if (RegOpenKeyExW(root, path, 0, KEY_READ, &h)) { o_str(k, "<none>"); return; }
    wchar_t name[256];
    for (DWORD i = 0;; i++) {
        DWORD n = 256;
        if (RegEnumValueW(h, i, name, &n, NULL, NULL, NULL, NULL)) break;
        if (*out) strcat(out, ",");
        if (strlen(out) + n + 2 < sizeof out) strcat(out, n ? oracle_utf8(name) : "@");
    }
    RegCloseKey(h);
    o_str(k, out);
}

static void probe_ext(const wchar_t *ext)
{
    char nm[64];
    wchar_t progid[256], p[512];
    DWORD sz = sizeof progid;
    snprintf(nm, sizeof nm, "reg_ext_%s", oracle_utf8(ext + 1));
    oracle_case(nm);
    reg_str(HKEY_CLASSES_ROOT, ext, NULL, "progid");
    reg_str(HKEY_CLASSES_ROOT, ext, L"Content Type", "content_type");
    reg_str(HKEY_CLASSES_ROOT, ext, L"PerceivedType", "perceived_type");
    reg_subkeys(HKEY_CLASSES_ROOT, ext, "subkeys");
    swprintf(p, 512, L"%ls\\OpenWithProgids", ext);
    reg_values(HKEY_CLASSES_ROOT, p, "openwithprogids");
    oracle_case_end();
    if (RegGetValueW(HKEY_CLASSES_ROOT, ext, NULL, RRF_RT_REG_SZ, NULL, progid, &sz)) return;
    snprintf(nm, sizeof nm, "reg_progid_%s", oracle_utf8(ext + 1));
    oracle_case(nm);
    o_wstr("progid", progid);
    reg_str(HKEY_CLASSES_ROOT, progid, NULL, "default");
    reg_values(HKEY_CLASSES_ROOT, progid, "values");
    reg_subkeys(HKEY_CLASSES_ROOT, progid, "subkeys");
    swprintf(p, 512, L"%ls\\CLSID", progid);
    reg_str(HKEY_CLASSES_ROOT, p, NULL, "clsid");
    swprintf(p, 512, L"%ls\\shell", progid);
    reg_subkeys(HKEY_CLASSES_ROOT, p, "shell_verbs");
    reg_str(HKEY_CLASSES_ROOT, p, NULL, "shell_default");
    swprintf(p, 512, L"%ls\\shellex\\ContextMenuHandlers", progid);
    reg_subkeys(HKEY_CLASSES_ROOT, p, "contextmenu_handlers");
    oracle_case_end();
}

static void probe_clsid(const wchar_t *progid)
{
    wchar_t clsid[64], p[512];
    DWORD sz = sizeof clsid;
    char nm[96];
    swprintf(p, 512, L"%ls\\CLSID", progid);
    if (RegGetValueW(HKEY_CLASSES_ROOT, p, NULL, RRF_RT_REG_SZ, NULL, clsid, &sz)) return;
    snprintf(nm, sizeof nm, "reg_clsid_%s", oracle_utf8(progid));
    oracle_case(nm);
    swprintf(p, 512, L"CLSID\\%ls", clsid);
    reg_str(HKEY_CLASSES_ROOT, p, NULL, "default");
    reg_values(HKEY_CLASSES_ROOT, p, "values");
    reg_subkeys(HKEY_CLASSES_ROOT, p, "subkeys");
    swprintf(p, 512, L"CLSID\\%ls\\InProcServer32", clsid);
    reg_basename(HKEY_CLASSES_ROOT, p, "inproc");
    reg_str(HKEY_CLASSES_ROOT, p, L"ThreadingModel", "threading");
    swprintf(p, 512, L"CLSID\\%ls\\ShellFolder", clsid);
    reg_values(HKEY_CLASSES_ROOT, p, "shellfolder_values");
    reg_str(HKEY_CLASSES_ROOT, p, L"Attributes", "shellfolder_attributes");
    oracle_case_end();
}

/* ---- SHGetFileInfo ---- */

static void sfi(const char *name, const wchar_t *path, DWORD fattrs, UINT flags)
{
    SHFILEINFOW fi;
    memset(&fi, 0, sizeof fi);
    wcscpy(fi.szDisplayName, L"#");
    wcscpy(fi.szTypeName, L"#");
    DWORD_PTR r = pSHGetFileInfoW(path, fattrs, &fi, sizeof fi, flags | SHGFI_DISPLAYNAME | SHGFI_TYPENAME | SHGFI_ATTRIBUTES);
    oracle_case(name);
    o_u64("ret", r);
    o_path("display", fi.szDisplayName);
    o_wstr("type", fi.szTypeName);
    o_hex32("attrs", fi.dwAttributes & ATTR_MASK);
    oracle_case_end();
}

/* ---- IShellFolder ---- */

static PIDLIST_RELATIVE parse(IShellFolder *f, const wchar_t *name, HRESULT *phr, SFGAOF *attrs)
{
    wchar_t b[2 * MAX_PATH];
    PIDLIST_RELATIVE pidl = NULL;
    ULONG eaten = 0;
    wcscpy(b, name);
    HRESULT hr = f->lpVtbl->ParseDisplayName(f, NULL, NULL, b, &eaten, &pidl, attrs);
    if (phr) *phr = hr;
    if (FAILED(hr)) pidl = NULL;
    return pidl;
}

static void name_field(IShellFolder *f, PCUITEMID_CHILD pidl, SHGDNF fl, const char *k)
{
    STRRET sr;
    wchar_t b[2 * MAX_PATH];
    char key[64];
    HRESULT hr = f->lpVtbl->GetDisplayNameOf(f, pidl, fl, &sr);
    if (FAILED(hr)) {
        snprintf(key, sizeof key, "%s_hr", k);
        o_hex32(key, (DWORD)hr);
        return;
    }
    b[0] = 0;
    pStrRetToBufW(&sr, pidl, b, 2 * MAX_PATH);
    o_path(k, b);
}

static void item_fields(IShellFolder *f, PCUITEMID_CHILD pidl)
{
    name_field(f, pidl, SHGDN_NORMAL, "normal");
    name_field(f, pidl, SHGDN_INFOLDER, "infolder");
    name_field(f, pidl, SHGDN_INFOLDER | SHGDN_FOREDITING, "infolder_editing");
    name_field(f, pidl, SHGDN_INFOLDER | SHGDN_FORPARSING, "infolder_parsing");
    name_field(f, pidl, SHGDN_FORPARSING, "forparsing");
    name_field(f, pidl, SHGDN_FORPARSING | SHGDN_FORADDRESSBAR, "parsing_addressbar");
    name_field(f, pidl, SHGDN_FORADDRESSBAR, "foraddressbar");
    SFGAOF a = 0xffffffff;
    HRESULT hr = f->lpVtbl->GetAttributesOf(f, 1, &pidl, &a);
    o_hex32("attrs_hr", (DWORD)hr);
    o_hex32("attrs", a & ATTR_MASK);
}

static IShellFolder *bind_child(IShellFolder *f, PCUIDLIST_RELATIVE pidl, HRESULT *phr)
{
    IShellFolder *sub = NULL;
    HRESULT hr = f->lpVtbl->BindToObject(f, pidl, NULL, &IID_IShellFolder, (void **)&sub);
    if (phr) *phr = hr;
    if (FAILED(hr)) sub = NULL;
    return sub;
}

/* Enumerates a folder: one case with the count and names, one case per item. */
static void enum_folder(const char *prefix, IShellFolder *f, int detail)
{
    IEnumIDList *en = NULL;
    char nm[128], names[2048] = "";
    int n = 0;
    HRESULT hr = f->lpVtbl->EnumObjects(f, NULL, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS | SHCONTF_INCLUDEHIDDEN, &en);
    PITEMID_CHILD items[64];
    if (hr == S_OK && en) {
        PITEMID_CHILD c;
        while (n < 64 && en->lpVtbl->Next(en, 1, &c, NULL) == S_OK) items[n++] = c;
        en->lpVtbl->Release(en);
    }
    for (int i = 0; i < n; i++) {
        STRRET sr;
        wchar_t b[MAX_PATH] = L"?";
        if (SUCCEEDED(f->lpVtbl->GetDisplayNameOf(f, items[i], SHGDN_INFOLDER | SHGDN_FORPARSING, &sr)))
            pStrRetToBufW(&sr, items[i], b, MAX_PATH);
        if (*names) strcat(names, ",");
        if (strlen(names) + wcslen(b) * 3 + 2 < sizeof names) strcat(names, oracle_utf8(b));
    }
    snprintf(nm, sizeof nm, "%s_enum", prefix);
    oracle_case(nm);
    o_hex32("hr", (DWORD)hr);
    o_u32("count", n);
    o_str("names", names);
    for (int pass = 0; pass < 2; pass++) {
        SHCONTF fl = pass ? SHCONTF_NONFOLDERS : SHCONTF_FOLDERS;
        int m = 0;
        en = NULL;
        if (f->lpVtbl->EnumObjects(f, NULL, fl | SHCONTF_INCLUDEHIDDEN, &en) == S_OK && en) {
            PITEMID_CHILD c;
            while (en->lpVtbl->Next(en, 1, &c, NULL) == S_OK) { m++; pCoTaskMemFree(c); }
            en->lpVtbl->Release(en);
        }
        o_u32(pass ? "nonfolders_count" : "folders_count", m);
    }
    oracle_case_end();
    for (int i = 0; i < n; i++) {
        if (detail) {
            STRRET sr;
            wchar_t b[MAX_PATH] = L"?";
            if (SUCCEEDED(f->lpVtbl->GetDisplayNameOf(f, items[i], SHGDN_INFOLDER | SHGDN_FORPARSING, &sr)))
                pStrRetToBufW(&sr, items[i], b, MAX_PATH);
            snprintf(nm, sizeof nm, "%s_item_%s", prefix, oracle_utf8(b));
            for (unsigned char *q = (unsigned char *)nm; *q; q++)
                if (!((*q >= 'a' && *q <= 'z') || (*q >= 'A' && *q <= 'Z') || (*q >= '0' && *q <= '9'))) *q = '_';
            oracle_case(nm);
            item_fields(f, items[i]);
            oracle_case_end();
        }
        pCoTaskMemFree(items[i]);
    }
}

static void o_guid(const char *k, const GUID *g)
{
    char s[64];
    snprintf(s, sizeof s, "{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}", (unsigned long)g->Data1, g->Data2,
             g->Data3, g->Data4[0], g->Data4[1], g->Data4[2], g->Data4[3], g->Data4[4], g->Data4[5], g->Data4[6],
             g->Data4[7]);
    o_str(k, s);
}

static void archive_folder_probe(const char *prefix, const wchar_t *file)
{
    char nm[128];
    HRESULT hr;
    PIDLIST_RELATIVE p = parse(tdir_sf, file, &hr, NULL);
    snprintf(nm, sizeof nm, "%s_item", prefix);
    oracle_case(nm);
    o_hex32("parse_hr", (DWORD)hr);
    if (p) item_fields(tdir_sf, (PCUITEMID_CHILD)p);
    oracle_case_end();
    if (!p) return;
    IShellFolder *sub = bind_child(tdir_sf, p, &hr);
    snprintf(nm, sizeof nm, "%s_bind", prefix);
    oracle_case(nm);
    o_hex32("hr", (DWORD)hr);
    if (sub) {
        IPersistFolder2 *pf2 = NULL;
        GUID clsid;
        if (SUCCEEDED(sub->lpVtbl->QueryInterface(sub, &IID_IPersistFolder2, (void **)&pf2))) {
            o_bool("persistfolder2", 1);
            if (SUCCEEDED(pf2->lpVtbl->GetClassID(pf2, &clsid))) o_guid("clsid", &clsid);
            PIDLIST_ABSOLUTE cur = NULL;
            hr = pf2->lpVtbl->GetCurFolder(pf2, &cur);
            o_hex32("curfolder_hr", (DWORD)hr);
            if (cur) {
                wchar_t full[2 * MAX_PATH];
                swprintf(full, 2 * MAX_PATH, L"%ls\\t\\%ls", cwd, file);
                PIDLIST_RELATIVE fp = parse(desktop, full, NULL, NULL);
                o_bool("curfolder_is_archive", fp && pILIsEqual(cur, (PCIDLIST_ABSOLUTE)fp));
                if (fp) pCoTaskMemFree(fp);
                pCoTaskMemFree(cur);
            }
            pf2->lpVtbl->Release(pf2);
        } else o_bool("persistfolder2", 0);
        IUnknown *u = NULL;
        o_bool("shellfolder2", SUCCEEDED(sub->lpVtbl->QueryInterface(sub, &IID_IShellFolder2, (void **)&u)));
        if (u) { u->lpVtbl->Release(u); u = NULL; }
        o_bool("storage", SUCCEEDED(sub->lpVtbl->QueryInterface(sub, &IID_IStorage, (void **)&u)));
        if (u) { u->lpVtbl->Release(u); u = NULL; }
    }
    IStream *st = NULL;
    hr = tdir_sf->lpVtbl->BindToObject(tdir_sf, p, NULL, &IID_IStream, (void **)&st);
    o_hex32("stream_hr", (DWORD)hr);
    if (SUCCEEDED(hr) && st) st->lpVtbl->Release(st);
    oracle_case_end();
    pCoTaskMemFree(p);
    if (sub) {
        enum_folder(prefix, sub, 1);
        sub->lpVtbl->Release(sub);
    }
}

static void o_formats(IDataObject *d)
{
    IEnumFORMATETC *ef = NULL;
    char out[1024] = "";
    if (SUCCEEDED(d->lpVtbl->EnumFormatEtc(d, DATADIR_GET, &ef)) && ef) {
        FORMATETC fe;
        while (ef->lpVtbl->Next(ef, 1, &fe, NULL) == S_OK) {
            char n[128];
            if (!GetClipboardFormatNameA(fe.cfFormat, n, sizeof n)) snprintf(n, sizeof n, "cf%u", fe.cfFormat);
            if (*out) strcat(out, ",");
            if (strlen(out) + strlen(n) + 2 < sizeof out) strcat(out, n);
            if (fe.ptd) pCoTaskMemFree(fe.ptd);
        }
        ef->lpVtbl->Release(ef);
    }
    o_str("formats", out);
}

static void dataobject_probe(const char *name, IShellFolder *f, const wchar_t *child)
{
    HRESULT hr;
    PIDLIST_RELATIVE p = parse(f, child, &hr, NULL);
    oracle_case(name);
    o_hex32("parse_hr", (DWORD)hr);
    if (!p) { oracle_case_end(); return; }
    PCUITEMID_CHILD c = (PCUITEMID_CHILD)p;
    IUnknown *u = NULL;
    hr = f->lpVtbl->GetUIObjectOf(f, NULL, 1, &c, &iid_extract_icon_w, NULL, (void **)&u);
    o_hex32("extracticon_hr", (DWORD)hr);
    if (u) { u->lpVtbl->Release(u); u = NULL; }
    hr = f->lpVtbl->GetUIObjectOf(f, NULL, 1, &c, &IID_IContextMenu, NULL, (void **)&u);
    o_hex32("contextmenu_hr", (DWORD)hr);
    if (u) { u->lpVtbl->Release(u); u = NULL; }
    IDataObject *d = NULL;
    hr = f->lpVtbl->GetUIObjectOf(f, NULL, 1, &c, &IID_IDataObject, NULL, (void **)&d);
    o_hex32("dataobject_hr", (DWORD)hr);
    if (d) {
        o_formats(d);
        FORMATETC fe = {(CLIPFORMAT)RegisterClipboardFormatW(L"FileGroupDescriptorW"), NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        STGMEDIUM sm;
        memset(&sm, 0, sizeof sm);
        hr = d->lpVtbl->GetData(d, &fe, &sm);
        o_hex32("filedescriptor_hr", (DWORD)hr);
        UINT count = 0;
        if (SUCCEEDED(hr)) {
            FILEGROUPDESCRIPTORW *g = GlobalLock(sm.hGlobal);
            char names[1024] = "";
            count = g->cItems;
            o_u32("fd_count", g->cItems);
            for (UINT i = 0; i < g->cItems && i < 16; i++) {
                if (*names) strcat(names, ",");
                strcat(names, oracle_utf8(g->fgd[i].cFileName));
            }
            o_str("fd_names", names);
            o_hex32("fd0_flags", g->fgd[0].dwFlags & ~(FD_CREATETIME | FD_ACCESSTIME | FD_WRITESTIME));
            o_bool("fd0_has_writetime", !!(g->fgd[0].dwFlags & FD_WRITESTIME));
            if (g->fgd[0].dwFlags & FD_ATTRIBUTES) o_hex32("fd0_attrs", g->fgd[0].dwFileAttributes);
            if (g->fgd[0].dwFlags & FD_FILESIZE) o_u32("fd0_size", g->fgd[0].nFileSizeLow);
            GlobalUnlock(sm.hGlobal);
            pReleaseStgMedium(&sm);
        }
        for (UINT i = 0; i < count && i < 4; i++) {
            char k[48];
            FORMATETC fc = {(CLIPFORMAT)RegisterClipboardFormatW(L"FileContents"), NULL, DVASPECT_CONTENT, (LONG)i,
                            TYMED_ISTREAM | TYMED_HGLOBAL};
            memset(&sm, 0, sizeof sm);
            hr = d->lpVtbl->GetData(d, &fc, &sm);
            snprintf(k, sizeof k, "contents%u_hr", i);
            o_hex32(k, (DWORD)hr);
            if (FAILED(hr)) continue;
            snprintf(k, sizeof k, "contents%u_tymed", i);
            o_u32(k, sm.tymed);
            char data[64] = "";
            ULONG got = 0;
            if (sm.tymed == TYMED_ISTREAM) sm.pstm->lpVtbl->Read(sm.pstm, data, sizeof data - 1, &got);
            else if (sm.tymed == TYMED_HGLOBAL) {
                got = (ULONG)GlobalSize(sm.hGlobal);
                if (got > sizeof data - 1) got = sizeof data - 1;
                memcpy(data, GlobalLock(sm.hGlobal), got);
                GlobalUnlock(sm.hGlobal);
            }
            data[got] = 0;
            snprintf(k, sizeof k, "contents%u", i);
            o_str(k, data);
            pReleaseStgMedium(&sm);
        }
        d->lpVtbl->Release(d);
    }
    IStream *st = NULL;
    hr = f->lpVtbl->BindToStorage(f, p, NULL, &IID_IStream, (void **)&st);
    o_hex32("storage_stream_hr", (DWORD)hr);
    if (SUCCEEDED(hr) && st) {
        char data[64] = "";
        ULONG got = 0;
        st->lpVtbl->Read(st, data, sizeof data - 1, &got);
        data[got] = 0;
        o_str("storage_stream_data", data);
        st->lpVtbl->Release(st);
    }
    st = NULL;
    hr = f->lpVtbl->BindToObject(f, p, NULL, &IID_IStream, (void **)&st);
    o_hex32("stream_hr", (DWORD)hr);
    if (SUCCEEDED(hr) && st) {
        char data[64] = "";
        ULONG got = 0;
        st->lpVtbl->Read(st, data, sizeof data - 1, &got);
        data[got] = 0;
        o_str("stream_data", data);
        STATSTG ss;
        memset(&ss, 0, sizeof ss);
        if (SUCCEEDED(st->lpVtbl->Stat(st, &ss, STATFLAG_DEFAULT))) {
            o_u64("stream_size", ss.cbSize.QuadPart);
            o_wstr("stream_name", ss.pwcsName);
            if (ss.pwcsName) pCoTaskMemFree(ss.pwcsName);
        }
        st->lpVtbl->Release(st);
    }
    oracle_case_end();
    pCoTaskMemFree(p);
}

/* Canonical verbs on the context menu of an item in t\ (sorted, deduplicated by the menu). */
static void verbs_probe(const char *name, const wchar_t *file)
{
    PIDLIST_RELATIVE p = parse(tdir_sf, file, NULL, NULL);
    IContextMenu *cm = NULL;
    oracle_case(name);
    if (p) {
        PCUITEMID_CHILD c = (PCUITEMID_CHILD)p;
        HRESULT hr = tdir_sf->lpVtbl->GetUIObjectOf(tdir_sf, NULL, 1, &c, &IID_IContextMenu, NULL, (void **)&cm);
        o_hex32("hr", (DWORD)hr);
    }
    if (cm) {
        HMENU m = CreatePopupMenu();
        HRESULT hr = cm->lpVtbl->QueryContextMenu(cm, m, 0, 1, 0x7fff, CMF_NORMAL);
        o_bool("query_ok", SUCCEEDED(hr));
        char verbs[2048] = "";
        int extract_text = 0;
        int n = GetMenuItemCount(m);
        for (int i = 0; i < n; i++) {
            MENUITEMINFOW mi;
            wchar_t text[256] = L"";
            memset(&mi, 0, sizeof mi);
            mi.cbSize = sizeof mi;
            mi.fMask = MIIM_ID | MIIM_STRING | MIIM_FTYPE;
            mi.dwTypeData = text;
            mi.cch = 256;
            if (!GetMenuItemInfoW(m, i, TRUE, &mi) || (mi.fType & MFT_SEPARATOR)) continue;
            if (wcsstr(text, L"Extract") || wcsstr(text, L"extract")) extract_text = 1;
            wchar_t verb[256] = L"";
            if (mi.wID < 1 || mi.wID > 0x7fff) continue;
            if (FAILED(cm->lpVtbl->GetCommandString(cm, mi.wID - 1, GCS_VERBW, NULL, (char *)verb, 256)) || !*verb)
                continue;
            if (*verbs) strcat(verbs, ",");
            if (strlen(verbs) + wcslen(verb) * 3 + 2 < sizeof verbs) strcat(verbs, oracle_utf8(verb));
        }
        o_str("verbs", verbs);
        o_bool("has_extract_text", extract_text);
        DestroyMenu(m);
        cm->lpVtbl->Release(cm);
    }
    oracle_case_end();
    if (p) pCoTaskMemFree(p);
}

/* IShellItem handlers for an item inside an archive. */
static const GUID bhid_stream = {0x1cebb3ab, 0x7c10, 0x499a, {0xa4, 0x17, 0x92, 0xca, 0x16, 0xc4, 0xcb, 0x83}};
static const GUID bhid_transfer = {0xd5e346a1, 0xf753, 0x4932, {0xb4, 0x03, 0x45, 0x74, 0x80, 0x0e, 0x24, 0x98}};
static const GUID bhid_storage = {0x3981e227, 0xf559, 0x11d3, {0x8e, 0x3a, 0x00, 0xc0, 0x4f, 0x68, 0x37, 0xd5}};
static const GUID bhid_dataobject = {0xb8c0bd9f, 0xed24, 0x455c, {0x83, 0xe6, 0xd5, 0x39, 0x0c, 0x4f, 0xe8, 0xc4}};
static const GUID iid_transfer_source = {0x00adb003, 0xbde9, 0x45c6, {0x8e, 0x29, 0xd0, 0x9f, 0x93, 0x53, 0xe1, 0x08}};

static void shellitem_probe(const char *name, const wchar_t *rel)
{
    wchar_t full[2 * MAX_PATH];
    IShellItem *si = NULL;
    swprintf(full, 2 * MAX_PATH, L"%ls\\%ls", cwd, rel);
    HRESULT hr = pSHCreateItemFromParsingName(full, NULL, &IID_IShellItem, (void **)&si);
    oracle_case(name);
    o_hex32("create_hr", (DWORD)hr);
    if (si) {
        IUnknown *u = NULL;
        hr = si->lpVtbl->BindToHandler(si, NULL, &bhid_stream, &IID_IStream, (void **)&u);
        o_hex32("stream_hr", (DWORD)hr);
        if (SUCCEEDED(hr) && u) {
            char data[64] = "";
            ULONG got = 0;
            ((IStream *)u)->lpVtbl->Read((IStream *)u, data, sizeof data - 1, &got);
            data[got] = 0;
            o_str("stream_data", data);
        }
        if (u) { u->lpVtbl->Release(u); u = NULL; }
        hr = si->lpVtbl->BindToHandler(si, NULL, &bhid_transfer, &iid_transfer_source, (void **)&u);
        o_hex32("transfer_hr", (DWORD)hr);
        if (u) { u->lpVtbl->Release(u); u = NULL; }
        hr = si->lpVtbl->BindToHandler(si, NULL, &bhid_storage, &IID_IStorage, (void **)&u);
        o_hex32("storage_hr", (DWORD)hr);
        if (u) { u->lpVtbl->Release(u); u = NULL; }
        hr = si->lpVtbl->BindToHandler(si, NULL, &bhid_dataobject, &IID_IDataObject, (void **)&u);
        o_hex32("dataobject_hr", (DWORD)hr);
        if (u) { o_formats((IDataObject *)u); u->lpVtbl->Release(u); u = NULL; }
        SFGAOF a = 0;
        hr = si->lpVtbl->GetAttributes(si, SFGAO_FOLDER | SFGAO_STREAM | SFGAO_FILESYSTEM | SFGAO_CANCOPY, &a);
        o_hex32("attrs", a);
        si->lpVtbl->Release(si);
    }
    oracle_case_end();
}

/* ---- common file dialog ---- */

static int ofn_folder_changes, ofn_navigated, ofn_inits;
static HWND ofn_dlg;

static UINT_PTR CALLBACK ofn_hook(HWND hdlg, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)wp;
    if (msg == WM_NOTIFY) {
        OFNOTIFYW *n = (OFNOTIFYW *)lp;
        if (n->hdr.code == CDN_INITDONE) {
            ofn_inits++;
            ofn_dlg = GetParent(hdlg);
            PostMessageW(ofn_dlg, WM_COMMAND, IDOK, 0);
        } else if (n->hdr.code == CDN_FOLDERCHANGE) {
            if (++ofn_folder_changes > 1) {
                ofn_navigated = 1;
                PostMessageW(GetParent(hdlg), WM_COMMAND, IDCANCEL, 0);
            }
        }
    }
    return 0;
}

static DWORD main_tid;
static volatile LONG ofn_timeout;

static BOOL CALLBACK close_window(HWND w, LPARAM lp)
{
    (void)lp;
    PostMessageW(w, WM_COMMAND, IDCANCEL, 0);
    PostMessageW(w, WM_CLOSE, 0, 0);
    return TRUE;
}

/* If the dialog is still open after 10 s (a message box, or it never came
 * up), close every window of the main thread until it returns. */
static DWORD WINAPI ofn_watchdog(void *arg)
{
    (void)arg;
    Sleep(10000);
    for (int i = 0; i < 20; i++) {
        InterlockedExchange(&ofn_timeout, 1);
        EnumThreadWindows(main_tid, close_window, 0);
        Sleep(500);
    }
    return 0;
}

static void ofn_probe(const char *name, const wchar_t *file, const wchar_t *filter)
{
    OPENFILENAMEW ofn;
    wchar_t path[2 * MAX_PATH];
    swprintf(path, 2 * MAX_PATH, L"%ls\\t\\%ls", cwd, file);
    memset(&ofn, 0, sizeof ofn);
    ofn.lStructSize = sizeof ofn;
    ofn.lpstrFile = path;
    ofn.nMaxFile = 2 * MAX_PATH;
    ofn.lpstrFilter = filter;
    ofn.Flags = OFN_EXPLORER | OFN_ENABLEHOOK | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    ofn.lpfnHook = ofn_hook;
    ofn_folder_changes = ofn_navigated = ofn_inits = 0;
    ofn_dlg = NULL;
    ofn_timeout = 0;
    main_tid = GetCurrentThreadId();
    HANDLE th = CreateThread(NULL, 0, ofn_watchdog, NULL, 0, NULL);
    BOOL r = pGetOpenFileNameW(&ofn);
    TerminateThread(th, 0);
    oracle_case(name);
    o_bool("timeout", ofn_timeout);
    o_bool("ret", r);
    o_u32("ext_error", r ? 0 : pCommDlgExtendedError());
    o_bool("dialog_shown", ofn_inits > 0);
    o_bool("navigated_into", ofn_navigated);
    if (r) o_path("file", path);
    oracle_case_end();
    CloseHandle(th);
}

static volatile LONG nh_shown, nh_navigated;

static BOOL CALLBACK find_dialog(HWND w, LPARAM lp)
{
    wchar_t cls[32];
    if (GetClassNameW(w, cls, 32) && !wcscmp(cls, L"#32770")) {
        *(HWND *)lp = w;
        return FALSE;
    }
    return TRUE;
}

/* Drives a GetOpenFileName dialog without a hook: waits for it, presses
 * OK, and cancels it if it is still open 3 s later. */
static DWORD WINAPI nohook_driver(void *arg)
{
    (void)arg;
    HWND dlg = NULL;
    for (int i = 0; i < 150 && !dlg; i++) {
        Sleep(100);
        EnumThreadWindows(main_tid, find_dialog, (LPARAM)&dlg);
    }
    if (!dlg) return 0;
    InterlockedExchange(&nh_shown, 1);
    Sleep(2000);
    PostMessageW(dlg, WM_COMMAND, IDOK, 0);
    Sleep(3000);
    if (!IsWindow(dlg)) return 0;
    InterlockedExchange(&nh_navigated, 1);
    for (int i = 0; i < 20; i++) {
        EnumThreadWindows(main_tid, close_window, 0);
        Sleep(500);
    }
    return 0;
}

static void ofn_nohook_probe(const char *name, const wchar_t *file)
{
    OPENFILENAMEW ofn;
    wchar_t path[2 * MAX_PATH];
    swprintf(path, 2 * MAX_PATH, L"%ls\\t\\%ls", cwd, file);
    memset(&ofn, 0, sizeof ofn);
    ofn.lStructSize = sizeof ofn;
    ofn.lpstrFile = path;
    ofn.nMaxFile = 2 * MAX_PATH;
    ofn.lpstrFilter = L"All files\0*.*\0";
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    nh_shown = nh_navigated = 0;
    main_tid = GetCurrentThreadId();
    HANDLE th = CreateThread(NULL, 0, nohook_driver, NULL, 0, NULL);
    BOOL r = pGetOpenFileNameW(&ofn);
    WaitForSingleObject(th, 20000);
    oracle_case(name);
    o_bool("ret", r);
    o_bool("dialog_found", nh_shown);
    o_bool("still_open_after_ok", nh_navigated);
    if (r) o_path("file", path);
    oracle_case_end();
    CloseHandle(th);
}

int main(void)
{
    oracle_begin("shell32/compressed_folders");
    HMODULE sh = LoadLibraryW(L"shell32.dll");
    HMODULE ole = LoadLibraryW(L"ole32.dll");
    HMODULE shl = LoadLibraryW(L"shlwapi.dll");
    HMODULE cd = LoadLibraryW(L"comdlg32.dll");
    LOAD(sh, SHGetDesktopFolder);
    LOAD(sh, SHGetFileInfoW);
    LOAD(sh, ILIsEqual);
    LOAD(sh, ILCombine);
    LOAD(sh, SHGetPathFromIDListW);
    LOAD(sh, SHCreateItemFromParsingName);
    LOAD(shl, StrRetToBufW);
    LOAD(ole, CoInitializeEx);
    LOAD(ole, CoTaskMemFree);
    LOAD(ole, ReleaseStgMedium);
    LOAD(cd, GetOpenFileNameW);
    LOAD(cd, CommDlgExtendedError);
    int ok = pSHGetDesktopFolder && pSHGetFileInfoW && pILIsEqual && pILCombine && pSHGetPathFromIDListW && pSHCreateItemFromParsingName && pStrRetToBufW && pCoInitializeEx &&
             pCoTaskMemFree && pReleaseStgMedium && pGetOpenFileNameW && pCommDlgExtendedError;
    oracle_case("setup");
    o_bool("loaded", ok);
    oracle_case_end();
    if (!ok) return oracle_end();
    pCoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    pSHGetDesktopFolder(&desktop);
    GetCurrentDirectoryW(MAX_PATH, cwd);

    /* Registry: extensions and their ProgIDs. */
    static const wchar_t *exts[] = {
        L".zip", L".tar", L".gz", L".tgz", L".7z", L".rar", L".bz2", L".tbz2", L".tbz", L".xz", L".txz",
        L".zst", L".tzst", L".lz", L".lzma", L".z", L".taz", L".cab", L".iso", L".lzh", L".arj", L".cpio",
        L".wim", L".jar", L".lz4", L".tlz", L".lha", L".xar", L".pax", L".ar", L".deb", L".rpm", L".warc",
        L".lzo", L".uu", L".zipx", L".tb2",
    };
    for (size_t i = 0; i < sizeof exts / sizeof exts[0]; i++) probe_ext(exts[i]);
    probe_clsid(L"CompressedFolder");
    probe_clsid(L"ArchiveFolder");
    probe_clsid(L"CABFolder");

    CreateDirectoryW(L"t", NULL);

    /* Names and attributes of files with each extension (junk content). */
    static const wchar_t *names[] = {
        L"j.zip", L"j.tar", L"j.gz", L"j.tar.gz", L"j.tgz", L"j.7z", L"j.rar", L"j.bz2", L"j.tar.bz2",
        L"j.tbz2", L"j.xz", L"j.tar.xz", L"j.txz", L"j.zst", L"j.tar.zst", L"j.tzst", L"j.cab", L"j.lz",
        L"j.z", L"j.cpio", L"two.dots.zip",
    };
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        wchar_t p[MAX_PATH];
        char nm[64];
        swprintf(p, MAX_PATH, L"t\\%ls", names[i]);
        write_junk(p);
        snprintf(nm, sizeof nm, "sfi_%s", oracle_utf8(names[i]));
        for (char *q = nm; *q; q++)
            if (*q == '.') *q = '_';
        sfi(nm, p, 0, 0);
    }
    sfi("sfi_ufa_zip", L"nothing.zip", FILE_ATTRIBUTE_NORMAL, SHGFI_USEFILEATTRIBUTES);
    sfi("sfi_ufa_7z", L"nothing.7z", FILE_ATTRIBUTE_NORMAL, SHGFI_USEFILEATTRIBUTES);
    sfi("sfi_ufa_tar_gz", L"nothing.tar.gz", FILE_ATTRIBUTE_NORMAL, SHGFI_USEFILEATTRIBUTES);
    sfi("sfi_ufa_zip_dir", L"nothing.zip", FILE_ATTRIBUTE_DIRECTORY, SHGFI_USEFILEATTRIBUTES);
    CreateDirectoryW(L"t\\dir.zip", NULL);
    sfi("sfi_directory_named_zip", L"t\\dir.zip", 0, 0);

    /* Real archives. */
    static const struct entry zip_entries[] = {
        {"readme.txt", "hello zip\n", 0, 0},
        {"docs/", NULL, 0, 0},
        {"docs/inner.txt", "inner text", 0, 0},
        {"docs/deep/x.bin", "xyz", 0, 0},
        {"implicit/only.txt", "only", 0, 0},
        {"Upper.TXT", "UP", 0, 0},
        {"empty.dat", "", 0, 0},
    };
    build_zip(zip_entries, 7);
    write_buf(L"t\\a.zip");
    build_zip(NULL, 0);
    write_buf(L"t\\empty.zip");
    static const struct entry tar_entries[] = {
        {"readme.txt", "hello tar\n", 0, 0},
        {"dir/", NULL, 0, 0},
        {"dir/inner.txt", "inner tar", 0, 0},
    };
    build_tar(tar_entries, 3);
    write_buf(L"t\\a.tar");
    gzip_buf(NULL);
    write_buf(L"t\\a.tar.gz");
    write_buf(L"t\\a.tgz");
    blen = 0;
    put("plain gz\n", 9);
    gzip_buf("plain.txt");
    write_buf(L"t\\plain.gz");

    static const struct entry name_entries[] = {
        {"caf\xc3\xa9.txt", "u", 0, 1},
        {"oem\x82.txt", "o", 0, 0},
        {"ro.txt", "r", 0x21, 0},
        {"hid.txt", "h", 0x22, 0},
        {"b.txt", "b", 0, 0},
        {"a.txt", "a", 0, 0},
    };
    build_zip(name_entries, 6);
    write_buf(L"t\\names.zip");
    static const struct entry order_entries[] = {
        {"b.txt", "b", 0, 0},
        {"a.txt", "a", 0, 0},
        {"z/", NULL, 0, 0},
        {"c/x.txt", "x", 0, 0},
    };
    build_tar(order_entries, 4);
    write_buf(L"t\\order.tar");

    sfi("sfi_valid_zip", L"t\\a.zip", 0, 0);
    sfi("sfi_empty_zip", L"t\\empty.zip", 0, 0);
    sfi("sfi_valid_tar_gz", L"t\\a.tar.gz", 0, 0);

    {
        wchar_t tdir[MAX_PATH];
        HRESULT hr;
        swprintf(tdir, MAX_PATH, L"%ls\\t", cwd);
        PIDLIST_RELATIVE p = parse(desktop, tdir, &hr, NULL);
        if (p) tdir_sf = bind_child(desktop, p, NULL);
        if (p) pCoTaskMemFree(p);
    }
    oracle_case("tdir");
    o_bool("bound", tdir_sf != NULL);
    oracle_case_end();
    if (!tdir_sf) return oracle_end();

    /* Attribute masks on an archive item. */
    {
        static const struct { const char *k; SFGAOF m; } masks[] = {
            {"mask_folder", SFGAO_FOLDER},
            {"mask_hassubfolder", SFGAO_HASSUBFOLDER},
            {"mask_stream", SFGAO_STREAM},
            {"mask_browsable", SFGAO_BROWSABLE},
            {"mask_filesystem", SFGAO_FILESYSTEM},
            {"mask_storage", SFGAO_STORAGE},
            {"mask_validate", SFGAO_VALIDATE | SFGAO_FOLDER},
        };
        static const wchar_t *files[] = {L"a.zip", L"empty.zip", L"j.zip", L"j.7z", L"a.tar.gz"};
        for (size_t f = 0; f < sizeof files / sizeof files[0]; f++) {
            char nm[64];
            PIDLIST_RELATIVE p = parse(tdir_sf, files[f], NULL, NULL);
            snprintf(nm, sizeof nm, "masks_%s", oracle_utf8(files[f]));
            for (char *q = nm; *q; q++)
                if (*q == '.') *q = '_';
            oracle_case(nm);
            o_bool("parsed", p != NULL);
            for (size_t i = 0; p && i < sizeof masks / sizeof masks[0]; i++) {
                SFGAOF a = masks[i].m;
                PCUITEMID_CHILD c = (PCUITEMID_CHILD)p;
                tdir_sf->lpVtbl->GetAttributesOf(tdir_sf, 1, &c, &a);
                o_hex32(masks[i].k, a & ATTR_MASK);
            }
            oracle_case_end();
            if (p) pCoTaskMemFree(p);
        }
    }

    archive_folder_probe("zip", L"a.zip");
    archive_folder_probe("emptyzip", L"empty.zip");
    archive_folder_probe("junkzip", L"j.zip");
    archive_folder_probe("tar", L"a.tar");
    archive_folder_probe("targz", L"a.tar.gz");
    archive_folder_probe("tgz", L"a.tgz");
    archive_folder_probe("gz", L"plain.gz");
    archive_folder_probe("names", L"names.zip");
    archive_folder_probe("order_tar", L"order.tar");

    /* Inside the zip: nested folders, parsing, copy-out. */
    {
        PIDLIST_RELATIVE p = parse(tdir_sf, L"a.zip", NULL, NULL);
        IShellFolder *zf = p ? bind_child(tdir_sf, p, NULL) : NULL;
        if (p) pCoTaskMemFree(p);
        if (zf) {
            static const struct { const char *name; const wchar_t *s; } pd[] = {
                {"zparse_file", L"readme.txt"},
                {"zparse_dir", L"docs"},
                {"zparse_nested", L"docs\\inner.txt"},
                {"zparse_implicit_dir", L"implicit"},
                {"zparse_case", L"README.TXT"},
                {"zparse_missing", L"nope.txt"},
                {"zparse_forward_slash", L"docs/inner.txt"},
            };
            for (size_t i = 0; i < sizeof pd / sizeof pd[0]; i++) {
                HRESULT hr;
                SFGAOF a = SFGAO_FOLDER | SFGAO_STREAM;
                PIDLIST_RELATIVE c = parse(zf, pd[i].s, &hr, &a);
                oracle_case(pd[i].name);
                o_hex32("hr", (DWORD)hr);
                if (c) {
                    o_hex32("attrs", a & ATTR_MASK);
                    pCoTaskMemFree(c);
                }
                oracle_case_end();
            }
            PIDLIST_RELATIVE d = parse(zf, L"docs", NULL, NULL);
            IShellFolder *df = d ? bind_child(zf, d, NULL) : NULL;
            if (df) {
                enum_folder("zip_docs", df, 1);
                PIDLIST_RELATIVE dd = parse(df, L"deep", NULL, NULL);
                IShellFolder *deep = dd ? bind_child(df, dd, NULL) : NULL;
                if (deep) {
                    enum_folder("zip_docs_deep", deep, 1);
                    deep->lpVtbl->Release(deep);
                }
                if (dd) pCoTaskMemFree(dd);
                dataobject_probe("zip_copy_nested", df, L"inner.txt");
                df->lpVtbl->Release(df);
            }
            if (d) pCoTaskMemFree(d);
            dataobject_probe("zip_copy_file", zf, L"readme.txt");
            dataobject_probe("zip_copy_dir", zf, L"docs");
            dataobject_probe("zip_copy_empty", zf, L"empty.dat");
            zf->lpVtbl->Release(zf);
        }
    }
    {
        PIDLIST_RELATIVE p = parse(tdir_sf, L"a.tar.gz", NULL, NULL);
        IShellFolder *zf = p ? bind_child(tdir_sf, p, NULL) : NULL;
        if (p) pCoTaskMemFree(p);
        if (zf) {
            dataobject_probe("targz_copy_file", zf, L"readme.txt");
            dataobject_probe("targz_copy_dir", zf, L"dir");
            zf->lpVtbl->Release(zf);
        }
    }

    /* Full paths into the archive through the Desktop and SHGetFileInfo. */
    {
        static const struct { const char *name; const wchar_t *rel; } fp[] = {
            {"desktop_parse_in_zip", L"t\\a.zip\\docs\\inner.txt"},
            {"desktop_parse_zip_dir", L"t\\a.zip\\docs"},
            {"desktop_parse_in_zip_missing", L"t\\a.zip\\nope.txt"},
            {"desktop_parse_in_targz", L"t\\a.tar.gz\\dir\\inner.txt"},
        };
        for (size_t i = 0; i < sizeof fp / sizeof fp[0]; i++) {
            wchar_t full[2 * MAX_PATH];
            HRESULT hr;
            SFGAOF a = SFGAO_FOLDER | SFGAO_FILESYSTEM | SFGAO_STREAM;
            swprintf(full, 2 * MAX_PATH, L"%ls\\%ls", cwd, fp[i].rel);
            PIDLIST_RELATIVE p = parse(desktop, full, &hr, &a);
            oracle_case(fp[i].name);
            o_hex32("hr", (DWORD)hr);
            if (p) {
                o_hex32("attrs", a & ATTR_MASK);
                STRRET sr;
                wchar_t b[2 * MAX_PATH];
                if (SUCCEEDED(desktop->lpVtbl->GetDisplayNameOf(desktop, (PCUITEMID_CHILD)p, SHGDN_FORPARSING, &sr))) {
                    pStrRetToBufW(&sr, (PCUITEMID_CHILD)p, b, 2 * MAX_PATH);
                    o_path("forparsing", b);
                }
                if (SUCCEEDED(desktop->lpVtbl->GetDisplayNameOf(desktop, (PCUITEMID_CHILD)p,
                                                                 SHGDN_FORPARSING | SHGDN_FORADDRESSBAR, &sr))) {
                    pStrRetToBufW(&sr, (PCUITEMID_CHILD)p, b, 2 * MAX_PATH);
                    o_path("addressbar", b);
                }
                wchar_t fsp[2 * MAX_PATH] = L"";
                o_bool("has_fs_path", pSHGetPathFromIDListW((PCIDLIST_ABSOLUTE)p, fsp));
                pCoTaskMemFree(p);
            }
            oracle_case_end();
            char sn[96];
            snprintf(sn, sizeof sn, "sfi_path_%s", fp[i].name + 14);
            sfi(sn, full, 0, 0);
        }
    }

    shellitem_probe("item_zip_file", L"t\\a.zip\\readme.txt");
    shellitem_probe("item_targz_file", L"t\\a.tar.gz\\readme.txt");
    shellitem_probe("item_targz_dir", L"t\\a.tar.gz\\dir");
    shellitem_probe("item_tar_file", L"t\\a.tar\\dir\\inner.txt");
    shellitem_probe("item_zip_archive", L"t\\a.zip");

    verbs_probe("verbs_zip", L"a.zip");
    verbs_probe("verbs_targz", L"a.tar.gz");
    verbs_probe("verbs_cab", L"j.cab");

    /* Last: the common file dialog (may hang or crash on Setu). */
    ofn_probe("ofn_zip_all_files", L"a.zip", L"All files\0*.*\0");
    ofn_probe("ofn_zip_txt_filter", L"a.zip", L"Text\0*.txt\0");
    ofn_probe("ofn_targz", L"a.tar.gz", L"All files\0*.*\0");
    ofn_probe("ofn_into_zip", L"a.zip\\readme.txt", L"All files\0*.*\0");
    ofn_nohook_probe("ofn_nohook_zip", L"a.zip");
    ofn_nohook_probe("ofn_nohook_plain", L"j.lz");

    tdir_sf->lpVtbl->Release(tdir_sf);
    desktop->lpVtbl->Release(desktop);
    return oracle_end();
}
