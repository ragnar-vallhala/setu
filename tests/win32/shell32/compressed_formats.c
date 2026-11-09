/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: the archive formats beyond zip/tar/gzip that the shell
 * browses as compressed folders. For .7z, .rar (RAR5), .cab, .cpio,
 * .tar.bz2, .tar.xz and .tar.zst: binding to the archive, enumerating
 * its entries (with a nested directory), the entries' names and
 * attributes, and copying an entry out through the data object and
 * through BindToObject(IStream). For single-file .bz2, .xz and .zst
 * streams: what the one entry is called. For a RAR5 archive with an
 * encrypted entry: what enumerating and reading it gives. And the
 * canonical verbs on each archive's context menu.
 *
 * Every archive is built here from the public format descriptions,
 * using only stored (uncompressed) data where the format allows it:
 *   bzip2   - no stored mode, so a small Huffman encoder (BWT, MTF/RLE,
 *             two identical fixed-length tables)
 *   xz      - LZMA2 uncompressed chunks, check type None
 *   zstd    - one Raw block
 *   7z      - the Copy coder
 *   RAR5    - stored files (compression method 0)
 *   cab     - one folder without compression
 *   cpio    - the "newc" (SVR4) header
 * so no third-party data or tool is involved.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nn-shobjidl_core-ishellfolder
 *       https://learn.microsoft.com/en-us/windows/win32/shell/clipboard (CFSTR_FILEDESCRIPTOR, CFSTR_FILECONTENTS)
 *       https://learn.microsoft.com/en-us/openspecs/exchange_server_protocols/ms-cab/ ([MS-CAB])
 *       https://sourceware.org/bzip2/ (bzip2), https://tukaani.org/xz/xz-file-format.txt (xz),
 *       RFC 8878 (zstd), https://www.7-zip.org/recover.html and 7zFormat.txt in the 7-Zip/LZMA SDK (7z),
 *       https://www.rarlab.com/technote.htm (RAR 5.0), https://man.freebsd.org/cgi/man.cgi?query=cpio&sektion=5 (cpio)
 */
#define INITGUID
#include "oracle.h"
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>

static HRESULT(WINAPI *pSHGetDesktopFolder)(IShellFolder **);
static DWORD_PTR(WINAPI *pSHGetFileInfoW)(LPCWSTR, DWORD, SHFILEINFOW *, UINT, UINT);
static HRESULT(WINAPI *pStrRetToBufW)(STRRET *, PCUITEMID_CHILD, LPWSTR, UINT);
static HRESULT(WINAPI *pCoInitializeEx)(void *, DWORD);
static void(WINAPI *pCoTaskMemFree)(void *);
static void(WINAPI *pReleaseStgMedium)(STGMEDIUM *);
static HRESULT(WINAPI *pCoCreateInstance)(REFCLSID, IUnknown *, DWORD, REFIID, void **);
static HRESULT(WINAPI *pSHCreateItemFromParsingName)(PCWSTR, IBindCtx *, REFIID, void **);

#define LOAD(m, f) (*(FARPROC *)&p##f = GetProcAddress(m, #f))

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

/* ---- byte buffer ---- */

static unsigned char buf[65536];
static size_t blen;

static void put(const void *p, size_t n) { memcpy(buf + blen, p, n); blen += n; }
static void put8(unsigned v) { buf[blen++] = (unsigned char)v; }
static void put16(unsigned v) { put8(v & 0xff); put8((v >> 8) & 0xff); }
static void put32(DWORD v) { put16(v & 0xffff); put16(v >> 16); }
static void put64(unsigned long long v) { put32((DWORD)v); put32((DWORD)(v >> 32)); }
static void set32(size_t at, DWORD v) { for (int i = 0; i < 4; i++) buf[at + i] = (unsigned char)(v >> (8 * i)); }

static DWORD crc32(const unsigned char *p, size_t n)
{
    DWORD c = 0xffffffff;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xedb88320 & (0 - (c & 1)));
    }
    return ~c;
}

static void write_buf(const wchar_t *name)
{
    HANDLE h = CreateFileW(name, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    DWORD w;
    WriteFile(h, buf, (DWORD)blen, &w, NULL);
    CloseHandle(h);
}

/* The archives' contents: one file at the top, a directory, a file in it. */
struct entry { const char *name; const char *data; };
static const struct entry entries[] = {
    {"readme.txt", "hello archive\n"},
    {"docs/", NULL},
    {"docs/inner.txt", "inner text"},
};
#define NENTRIES 3
#define IS_DIR(e) ((e)->name[strlen((e)->name) - 1] == '/')
#define DLEN(e) ((e)->data ? strlen((e)->data) : 0)

/* 2026-01-02 03:04:06 UTC */
#define UNIX_TIME 1767322446ull
#define FILETIME_OF(t) ((t) * 10000000ull + 116444736000000000ull)
#define DOS_TIME ((3 << 11) | (4 << 5) | 3)
#define DOS_DATE (((2026 - 1980) << 9) | (1 << 5) | 2)

/* ---- ustar (as the payload of the compressed tars) ---- */

static void tar_octal(char *dst, size_t width, unsigned long long v)
{
    char t[24];
    snprintf(t, sizeof t, "%0*llo", (int)(width - 1), v);
    memcpy(dst, t, width - 1);
    dst[width - 1] = 0;
}

static void build_tar(void)
{
    blen = 0;
    for (int i = 0; i < NENTRIES; i++) {
        const struct entry *e = &entries[i];
        char h[512];
        size_t dl = DLEN(e);
        memset(h, 0, sizeof h);
        strcpy(h, e->name);
        tar_octal(h + 100, 8, IS_DIR(e) ? 0755 : 0644);
        tar_octal(h + 108, 8, 0);
        tar_octal(h + 116, 8, 0);
        tar_octal(h + 124, 12, dl);
        tar_octal(h + 136, 12, UNIX_TIME);
        memset(h + 148, ' ', 8);
        h[156] = IS_DIR(e) ? '5' : '0';
        memcpy(h + 257, "ustar", 6);
        memcpy(h + 263, "00", 2);
        unsigned sum = 0;
        for (int k = 0; k < 512; k++) sum += (unsigned char)h[k];
        snprintf(h + 148, 8, "%06o", sum);
        h[155] = ' ';
        put(h, 512);
        if (dl) {
            put(e->data, dl);
            size_t pad = (512 - dl % 512) % 512;
            memset(buf + blen, 0, pad);
            blen += pad;
        }
    }
    memset(buf + blen, 0, 1024);
    blen += 1024;
}

/* ---- bzip2 ---- */

static unsigned char raw[65536];
static size_t rawlen;

/* buf[0..blen) -> raw (the input of a stream wrapper) */
static void take_raw(void) { memcpy(raw, buf, blen); rawlen = blen; blen = 0; }

static unsigned bitacc, bitcnt;
static void bits(unsigned n, unsigned long v) /* MSB first */
{
    while (n--) {
        bitacc = (bitacc << 1) | ((v >> n) & 1);
        if (++bitcnt == 8) { put8(bitacc); bitacc = bitcnt = 0; }
    }
}
static void bits_flush(void) { if (bitcnt) bits(8 - bitcnt, 0); }

static DWORD crc32_msb(const unsigned char *p, size_t n)
{
    DWORD c = 0xffffffff;
    for (size_t i = 0; i < n; i++) {
        c ^= (DWORD)p[i] << 24;
        for (int k = 0; k < 8; k++) c = (c << 1) ^ (0x04c11db7 & (0 - (c >> 31)));
    }
    return ~c;
}

static unsigned char bwt_in[2 * 32768];
static size_t bwt_n;
static int rot_cmp(const void *a, const void *b)
{
    return memcmp(bwt_in + *(const unsigned *)a, bwt_in + *(const unsigned *)b, bwt_n);
}

/* raw -> buf as a one-block bzip2 stream. */
static void bzip2_raw(void)
{
    static unsigned char rle[32768], last[32768];
    static unsigned rot[32768];
    static unsigned short syms[65536];
    size_t n = 0, nsyms = 0;
    /* run-length stage: runs of 4..255 become 4 bytes and a count */
    for (size_t i = 0; i < rawlen;) {
        size_t run = 1;
        while (i + run < rawlen && raw[i + run] == raw[i] && run < 255) run++;
        if (run >= 4) {
            memset(rle + n, raw[i], 4);
            rle[n + 4] = (unsigned char)(run - 4);
            n += 5;
        } else {
            memset(rle + n, raw[i], run);
            n += run;
        }
        i += run;
    }
    /* Burrows-Wheeler transform over the rotations */
    bwt_n = n;
    memcpy(bwt_in, rle, n);
    memcpy(bwt_in + n, rle, n);
    for (unsigned i = 0; i < n; i++) rot[i] = i;
    qsort(rot, n, sizeof rot[0], rot_cmp);
    unsigned orig = 0;
    for (unsigned i = 0; i < n; i++) {
        if (!rot[i]) orig = i;
        last[i] = rle[(rot[i] + n - 1) % n];
    }
    /* symbol map, move-to-front, zero runs as RUNA/RUNB */
    int inuse[256] = {0}, ninuse = 0;
    unsigned char mtf[256];
    for (size_t i = 0; i < n; i++) inuse[rle[i]] = 1;
    for (int c = 0; c < 256; c++)
        if (inuse[c]) mtf[ninuse++] = (unsigned char)c;
    unsigned zrun = 0;
    for (size_t i = 0; i <= n; i++) {
        int j = 0;
        if (i < n)
            while (mtf[j] != last[i]) j++;
        if (i < n && !j) { zrun++; continue; }
        if (zrun) {
            unsigned z = zrun - 1;
            for (;;) {
                syms[nsyms++] = z & 1; /* RUNA 0, RUNB 1 */
                if (z < 2) break;
                z = (z - 2) / 2;
            }
            zrun = 0;
        }
        if (i == n) break;
        syms[nsyms++] = (unsigned short)(j + 1);
        unsigned char c = mtf[j];
        memmove(mtf + 1, mtf, j);
        mtf[0] = c;
    }
    int alpha = ninuse + 2;
    syms[nsyms++] = (unsigned short)(alpha - 1); /* EOB */
    /* a complete fixed-length code: 2^k - alpha codes of k-1 bits, the rest k bits */
    int k = 1;
    while ((1 << k) < alpha) k++;
    int nshort = (1 << k) - alpha, len[258];
    unsigned code[258], next = 0;
    for (int s = 0; s < alpha; s++) len[s] = s < nshort ? k - 1 : k;
    for (int l = 1; l <= k; l++, next <<= 1)
        for (int s = 0; s < alpha; s++)
            if (len[s] == l) code[s] = next++;

    blen = 0;
    bitacc = bitcnt = 0;
    put("BZh9", 4);
    DWORD bcrc = crc32_msb(raw, rawlen);
    bits(24, 0x314159); bits(24, 0x265359);
    bits(32, bcrc);
    bits(1, 0);
    bits(24, orig);
    unsigned ranges = 0;
    for (int r = 0; r < 16; r++)
        for (int c = 0; c < 16; c++)
            if (inuse[r * 16 + c]) ranges |= 0x8000 >> r;
    bits(16, ranges);
    for (int r = 0; r < 16; r++) {
        if (!(ranges & (0x8000 >> r))) continue;
        unsigned m = 0;
        for (int c = 0; c < 16; c++)
            if (inuse[r * 16 + c]) m |= 0x8000 >> c;
        bits(16, m);
    }
    size_t nsel = (nsyms + 49) / 50;
    bits(3, 2);
    bits(15, nsel);
    for (size_t i = 0; i < nsel; i++) bits(1, 0); /* every group uses table 0 */
    for (int t = 0; t < 2; t++) {
        int cur = len[0];
        bits(5, cur);
        for (int s = 0; s < alpha; s++) {
            while (cur < len[s]) { bits(2, 2); cur++; }
            while (cur > len[s]) { bits(2, 3); cur--; }
            bits(1, 0);
        }
    }
    for (size_t i = 0; i < nsyms; i++) bits(len[syms[i]], code[syms[i]]);
    bits(24, 0x177245); bits(24, 0x385090);
    bits(32, bcrc); /* the stream CRC of a single block is the block CRC */
    bits_flush();
}

/* ---- xz ---- */

static unsigned vli(unsigned char *o, unsigned long long v)
{
    unsigned n = 0;
    while (v >= 0x80) { o[n++] = (unsigned char)(v | 0x80); v >>= 7; }
    o[n++] = (unsigned char)v;
    return n;
}

static void xz_raw(void)
{
    unsigned char t[16];
    blen = 0;
    put("\xfd" "7zXZ\0", 6);
    put16(0); /* stream flags: check None */
    put32(crc32(buf + 6, 2));
    size_t bh = blen;
    /* block header: size, flags (one filter, no sizes), LZMA2 with dictionary byte 0 */
    put8(2); put8(0); put8(0x21); put8(1); put8(0); put8(0); put8(0); put8(0);
    put32(crc32(buf + bh, 8));
    size_t data = blen;
    for (size_t off = 0; off < rawlen; off += 65536) {
        size_t c = rawlen - off < 65536 ? rawlen - off : 65536;
        put8(off ? 2 : 1); /* uncompressed chunk, the first resets the dictionary */
        put8((unsigned)((c - 1) >> 8)); put8((unsigned)((c - 1) & 0xff));
        put(raw + off, c);
    }
    put8(0);
    unsigned long long unpadded = (blen - data) + 12;
    while ((blen - data) % 4) put8(0);
    size_t idx = blen;
    put8(0);
    put(t, vli(t, 1));
    put(t, vli(t, unpadded));
    put(t, vli(t, rawlen));
    while ((blen - idx) % 4) put8(0);
    put32(crc32(buf + idx, blen - idx));
    DWORD backward = (DWORD)((blen - idx) / 4 - 1);
    size_t foot = blen;
    put32(0);
    put32(backward);
    put16(0);
    set32(foot, crc32(buf + foot + 4, 6));
    put("YZ", 2);
}

/* ---- zstd ---- */

static void zstd_raw(void)
{
    blen = 0;
    put32(0xfd2fb528);
    put8(0xa0); /* single segment, 4-byte content size */
    put32((DWORD)rawlen);
    DWORD bh = 1 | (DWORD)(rawlen << 3); /* last block, Raw */
    put8(bh & 0xff); put8((bh >> 8) & 0xff); put8((bh >> 16) & 0xff);
    put(raw, rawlen);
}

/* ---- 7z ---- */

static void num7(unsigned long long v) /* 7z NUMBER */
{
    int extra = 0;
    while (extra < 8 && v >= (1ull << (7 * (extra + 1)))) extra++;
    if (extra == 8) { put8(0xff); put64(v); return; }
    unsigned first = (0xff00 >> extra) & 0xff;
    first |= (unsigned)(v >> (8 * extra));
    put8(first);
    for (int i = 0; i < extra; i++) put8((unsigned)(v >> (8 * i)) & 0xff);
}

static void build_7z(void)
{
    static unsigned char hdr[4096];
    size_t packed = 0, nfiles = 0;
    blen = 32;
    for (int i = 0; i < NENTRIES; i++)
        if (!IS_DIR(&entries[i])) { put(entries[i].data, DLEN(&entries[i])); packed += DLEN(&entries[i]); nfiles++; }
    size_t hstart = blen;
    put8(0x01); /* Header */
    put8(0x04); /* MainStreamsInfo */
    put8(0x06); num7(0); num7(1); put8(0x09); num7(packed); put8(0x00); /* PackInfo */
    put8(0x07); put8(0x0b); num7(1); put8(0); /* UnPackInfo, one folder */
    num7(1); put8(0x01); put8(0x00); /* one coder: Copy */
    put8(0x0c); num7(packed); put8(0x00);
    put8(0x08); put8(0x0d); num7(nfiles); /* SubStreamsInfo */
    put8(0x09);
    for (size_t i = 0, f = 0; i < NENTRIES; i++)
        if (!IS_DIR(&entries[i]) && ++f < nfiles) num7(DLEN(&entries[i]));
    put8(0x0a); put8(1);
    for (int i = 0; i < NENTRIES; i++)
        if (!IS_DIR(&entries[i])) put32(crc32((const unsigned char *)entries[i].data, DLEN(&entries[i])));
    put8(0x00);
    put8(0x00);
    put8(0x05); num7(NENTRIES); /* FilesInfo */
    unsigned empty = 0;
    for (int i = 0; i < NENTRIES; i++)
        if (IS_DIR(&entries[i])) empty |= 0x80 >> i;
    put8(0x0e); num7(1); put8(empty); /* EmptyStream */
    size_t nl = 1;
    for (int i = 0; i < NENTRIES; i++) nl += (strlen(entries[i].name) + 1 - IS_DIR(&entries[i])) * 2;
    put8(0x11); num7(nl); put8(0);
    for (int i = 0; i < NENTRIES; i++) {
        size_t l = strlen(entries[i].name) - IS_DIR(&entries[i]);
        for (size_t c = 0; c < l; c++) put16(entries[i].name[c] == '/' ? '/' : (unsigned char)entries[i].name[c]);
        put16(0);
    }
    put8(0x14); num7(2 + 8 * NENTRIES); put8(1); put8(0);
    for (int i = 0; i < NENTRIES; i++) put64(FILETIME_OF(UNIX_TIME));
    put8(0x15); num7(2 + 4 * NENTRIES); put8(1); put8(0);
    for (int i = 0; i < NENTRIES; i++) put32(IS_DIR(&entries[i]) ? 0x10 : 0x20);
    put8(0x00);
    put8(0x00);
    size_t hlen = blen - hstart;
    memcpy(hdr, buf + hstart, hlen);
    size_t total = blen;
    blen = 0;
    put("7z\xbc\xaf\x27\x1c", 6);
    put8(0); put8(4);
    put32(0);
    put64(hstart - 32);
    put64(hlen);
    put32(crc32(hdr, hlen));
    set32(8, crc32(buf + 12, 20));
    blen = total;
}

/* ---- RAR 5.0 ---- */

static void rar5_header(const unsigned char *h, size_t n)
{
    unsigned char t[16];
    unsigned sl = vli(t, n);
    size_t at = blen;
    put32(0);
    put(t, sl);
    put(h, n);
    set32(at, crc32(buf + at + 4, sl + n));
}

/* encrypted: give every file an encryption record (AES-256, junk salt/IV) */
static void build_rar5(int encrypted)
{
    unsigned char h[512];
    size_t n;
    blen = 0;
    put("Rar!\x1a\x07\x01\x00", 8);
    n = 0; h[n++] = 1; h[n++] = 0; h[n++] = 0; /* main header */
    rar5_header(h, n);
    for (int i = 0; i < NENTRIES; i++) {
        const struct entry *e = &entries[i];
        int dir = IS_DIR(e), enc = encrypted && !dir;
        size_t dl = DLEN(e), stored = enc ? (dl + 15) & ~15u : dl;
        size_t nl = strlen(e->name) - dir;
        unsigned char extra[64];
        size_t xl = 0;
        if (enc) {
            unsigned char rec[48];
            size_t rl = 0;
            rec[rl++] = 1; rec[rl++] = 0; rec[rl++] = 0; rec[rl++] = 15; /* type, AES-256, flags, KDF count */
            for (int k = 0; k < 32; k++) rec[rl++] = (unsigned char)(0x5a ^ k); /* salt, IV */
            xl = vli(extra, rl);
            memcpy(extra + xl, rec, rl);
            xl += rl;
        }
        n = 0;
        h[n++] = 2; /* file header */
        n += vli(h + n, (xl ? 1 : 0) | (dir ? 0 : 2));
        if (xl) n += vli(h + n, xl);
        if (!dir) n += vli(h + n, stored);
        n += vli(h + n, dir ? 3 : 6); /* directory|mtime, or mtime|crc */
        n += vli(h + n, dl);
        n += vli(h + n, dir ? 0x10 : 0x20);
        for (int k = 0; k < 4; k++) h[n++] = (unsigned char)(UNIX_TIME >> (8 * k));
        if (!dir) {
            DWORD c = crc32((const unsigned char *)e->data, dl);
            for (int k = 0; k < 4; k++) h[n++] = (unsigned char)(c >> (8 * k));
        }
        n += vli(h + n, 0); /* version 0, stored */
        n += vli(h + n, 0); /* host OS Windows */
        n += vli(h + n, nl);
        memcpy(h + n, e->name, nl);
        n += nl;
        memcpy(h + n, extra, xl);
        n += xl;
        rar5_header(h, n);
        if (enc) for (size_t k = 0; k < stored; k++) put8((unsigned)(k * 37 + 11));
        else if (dl) put(e->data, dl);
    }
    n = 0; h[n++] = 5; h[n++] = 0; h[n++] = 0; /* end of archive */
    rar5_header(h, n);
}

/* ---- cab ---- */

static void build_cab(void)
{
    size_t nfiles = 0, total = 0;
    blen = 0;
    put("MSCF", 4); put32(0); put32(0); put32(0); put32(0); put32(0);
    put8(3); put8(1); put16(1); put16(0); put16(0); put16(0x1234); put16(0);
    size_t folder = blen;
    put32(0); put16(1); put16(0);
    set32(16, (DWORD)blen); /* coffFiles */
    for (int i = 0; i < NENTRIES; i++) {
        const struct entry *e = &entries[i];
        if (IS_DIR(e)) continue;
        put32((DWORD)DLEN(e)); put32((DWORD)total); put16(0); put16(DOS_DATE); put16(DOS_TIME); put16(0x20);
        for (const char *c = e->name; *c; c++) put8(*c == '/' ? '\\' : *c);
        put8(0);
        total += DLEN(e);
        nfiles++;
    }
    buf[28] = (unsigned char)nfiles;
    set32(folder, (DWORD)blen);
    put32(0); put16((unsigned)total); put16((unsigned)total); /* one CFDATA, no checksum */
    for (int i = 0; i < NENTRIES; i++)
        if (!IS_DIR(&entries[i])) put(entries[i].data, DLEN(&entries[i]));
    set32(8, (DWORD)blen);
}

/* ---- cpio (newc) ---- */

static void cpio_member(const char *name, unsigned mode, const char *data, size_t dl, unsigned ino)
{
    char h[111];
    size_t nl = strlen(name) + 1;
    snprintf(h, sizeof h, "070701%08X%08X%08X%08X%08X%08X%08X%08X%08X%08X%08X%08X%08X", ino, mode, 0, 0,
             (mode & 0040000) ? 2 : 1, (unsigned)UNIX_TIME, (unsigned)dl, 0, 0, 0, 0, (unsigned)nl, 0);
    put(h, 110);
    put(name, nl);
    while (blen % 4) put8(0);
    if (dl) put(data, dl);
    while (blen % 4) put8(0);
}

static void build_cpio(void)
{
    blen = 0;
    for (int i = 0; i < NENTRIES; i++) {
        const struct entry *e = &entries[i];
        char name[64];
        strcpy(name, e->name);
        if (IS_DIR(e)) name[strlen(name) - 1] = 0;
        cpio_member(name, IS_DIR(e) ? 0040755 : 0100644, e->data, DLEN(e), i + 1);
    }
    cpio_member("TRAILER!!!", 0, NULL, 0, 0);
}

/* ---- shell probes ---- */

static void sfi(const char *name, const wchar_t *path)
{
    SHFILEINFOW fi;
    memset(&fi, 0, sizeof fi);
    DWORD_PTR r = pSHGetFileInfoW(path, 0, &fi, sizeof fi, SHGFI_DISPLAYNAME | SHGFI_TYPENAME | SHGFI_ATTRIBUTES);
    oracle_case(name);
    o_u64("ret", r);
    o_path("display", fi.szDisplayName);
    o_wstr("type", fi.szTypeName);
    o_hex32("attrs", fi.dwAttributes & ATTR_MASK);
    oracle_case_end();
}

static PIDLIST_RELATIVE parse(IShellFolder *f, const wchar_t *name, HRESULT *phr)
{
    wchar_t b[2 * MAX_PATH];
    PIDLIST_RELATIVE pidl = NULL;
    ULONG eaten = 0;
    wcscpy(b, name);
    HRESULT hr = f->lpVtbl->ParseDisplayName(f, NULL, NULL, b, &eaten, &pidl, NULL);
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
    name_field(f, pidl, SHGDN_INFOLDER | SHGDN_FORPARSING, "infolder_parsing");
    name_field(f, pidl, SHGDN_FORPARSING, "forparsing");
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

static void case_name(char *nm, size_t sz, const char *prefix, const char *suffix, const wchar_t *w)
{
    snprintf(nm, sz, "%s_%s%s", prefix, suffix, w ? oracle_utf8(w) : "");
    for (unsigned char *q = (unsigned char *)nm + strlen(prefix) + 1; *q; q++)
        if (!((*q >= 'a' && *q <= 'z') || (*q >= 'A' && *q <= 'Z') || (*q >= '0' && *q <= '9'))) *q = '_';
}

/* One case with the count and names, one case per item; recurses into folders once. */
static void enum_folder(const char *prefix, IShellFolder *f, int depth)
{
    IEnumIDList *en = NULL;
    char nm[128], names[1024] = "";
    int n = 0;
    PITEMID_CHILD items[32];
    HRESULT hr = f->lpVtbl->EnumObjects(f, NULL, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS | SHCONTF_INCLUDEHIDDEN, &en);
    if (hr == S_OK && en) {
        PITEMID_CHILD c;
        while (n < 32 && en->lpVtbl->Next(en, 1, &c, NULL) == S_OK) items[n++] = c;
        en->lpVtbl->Release(en);
    }
    wchar_t b[32][MAX_PATH];
    for (int i = 0; i < n; i++) {
        STRRET sr;
        wcscpy(b[i], L"?");
        if (SUCCEEDED(f->lpVtbl->GetDisplayNameOf(f, items[i], SHGDN_INFOLDER | SHGDN_FORPARSING, &sr)))
            pStrRetToBufW(&sr, items[i], b[i], MAX_PATH);
        if (*names) strcat(names, ",");
        if (strlen(names) + wcslen(b[i]) * 3 + 2 < sizeof names) strcat(names, oracle_utf8(b[i]));
    }
    snprintf(nm, sizeof nm, "%s_enum", prefix);
    oracle_case(nm);
    o_hex32("hr", (DWORD)hr);
    o_u32("count", n);
    o_str("names", names);
    oracle_case_end();
    for (int i = 0; i < n; i++) {
        case_name(nm, sizeof nm, prefix, "item_", b[i]);
        oracle_case(nm);
        item_fields(f, items[i]);
        oracle_case_end();
        SFGAOF a = SFGAO_FOLDER;
        PCUITEMID_CHILD c = items[i];
        if (depth < 1 && SUCCEEDED(f->lpVtbl->GetAttributesOf(f, 1, &c, &a)) && (a & SFGAO_FOLDER)) {
            IShellFolder *sub = bind_child(f, (PCUIDLIST_RELATIVE)items[i], NULL);
            if (sub) {
                case_name(nm, sizeof nm, prefix, "", b[i]);
                enum_folder(nm, sub, depth + 1);
                sub->lpVtbl->Release(sub);
            }
        }
        pCoTaskMemFree(items[i]);
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

static void stream_fields(IStream *st, const char *prefix)
{
    char data[64] = "", k[48];
    ULONG got = 0;
    HRESULT hr = st->lpVtbl->Read(st, data, sizeof data - 1, &got);
    data[got] = 0;
    snprintf(k, sizeof k, "%s_read_hr", prefix);
    o_hex32(k, (DWORD)hr);
    snprintf(k, sizeof k, "%s_data", prefix);
    o_str(k, data);
    STATSTG ss;
    memset(&ss, 0, sizeof ss);
    if (SUCCEEDED(st->lpVtbl->Stat(st, &ss, STATFLAG_DEFAULT))) {
        snprintf(k, sizeof k, "%s_size", prefix);
        o_u64(k, ss.cbSize.QuadPart);
        snprintf(k, sizeof k, "%s_name", prefix);
        o_wstr(k, ss.pwcsName);
        if (ss.pwcsName) pCoTaskMemFree(ss.pwcsName);
    }
}

/* Copying an entry out: the data object's descriptor and contents, and BindToObject(IStream). */
static void copyout_probe(const char *name, IShellFolder *f, const wchar_t *child)
{
    HRESULT hr;
    PIDLIST_RELATIVE p = parse(f, child, &hr);
    oracle_case(name);
    o_hex32("parse_hr", (DWORD)hr);
    if (!p) { oracle_case_end(); return; }
    PCUITEMID_CHILD c = (PCUITEMID_CHILD)p;
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
        if (SUCCEEDED(hr)) {
            FILEGROUPDESCRIPTORW *g = GlobalLock(sm.hGlobal);
            o_u32("fd_count", g->cItems);
            if (g->cItems) {
                o_wstr("fd0_name", g->fgd[0].cFileName);
                o_hex32("fd0_flags", g->fgd[0].dwFlags & ~(FD_CREATETIME | FD_ACCESSTIME | FD_WRITESTIME));
                o_bool("fd0_has_writetime", !!(g->fgd[0].dwFlags & FD_WRITESTIME));
                if (g->fgd[0].dwFlags & FD_ATTRIBUTES) o_hex32("fd0_attrs", g->fgd[0].dwFileAttributes);
                if (g->fgd[0].dwFlags & FD_FILESIZE) o_u32("fd0_size", g->fgd[0].nFileSizeLow);
            }
            GlobalUnlock(sm.hGlobal);
            pReleaseStgMedium(&sm);
            FORMATETC fc = {(CLIPFORMAT)RegisterClipboardFormatW(L"FileContents"), NULL, DVASPECT_CONTENT, 0,
                            TYMED_ISTREAM | TYMED_HGLOBAL};
            memset(&sm, 0, sizeof sm);
            hr = d->lpVtbl->GetData(d, &fc, &sm);
            o_hex32("contents_hr", (DWORD)hr);
            if (SUCCEEDED(hr)) {
                o_u32("contents_tymed", sm.tymed);
                if (sm.tymed == TYMED_ISTREAM) stream_fields(sm.pstm, "contents");
                pReleaseStgMedium(&sm);
            }
        }
        d->lpVtbl->Release(d);
    }
    IStream *st = NULL;
    hr = f->lpVtbl->BindToObject(f, p, NULL, &IID_IStream, (void **)&st);
    o_hex32("stream_hr", (DWORD)hr);
    if (SUCCEEDED(hr) && st) {
        stream_fields(st, "stream");
        st->lpVtbl->Release(st);
    }
    oracle_case_end();
    pCoTaskMemFree(p);
}

/* Canonical verbs on the archive's context menu. */
static void verbs_probe(const char *name, const wchar_t *file)
{
    PIDLIST_RELATIVE p = parse(tdir_sf, file, NULL);
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
        int n = GetMenuItemCount(m);
        for (int i = 0; i < n; i++) {
            MENUITEMINFOW mi;
            memset(&mi, 0, sizeof mi);
            mi.cbSize = sizeof mi;
            mi.fMask = MIIM_ID | MIIM_FTYPE;
            if (!GetMenuItemInfoW(m, i, TRUE, &mi) || (mi.fType & MFT_SEPARATOR)) continue;
            wchar_t verb[256] = L"";
            if (mi.wID < 1 || mi.wID > 0x7fff) continue;
            if (FAILED(cm->lpVtbl->GetCommandString(cm, mi.wID - 1, GCS_VERBW, NULL, (char *)verb, 256)) || !*verb)
                continue;
            if (*verbs) strcat(verbs, ",");
            if (strlen(verbs) + wcslen(verb) * 3 + 2 < sizeof verbs) strcat(verbs, oracle_utf8(verb));
        }
        o_str("verbs", verbs);
        DestroyMenu(m);
        cm->lpVtbl->Release(cm);
    }
    oracle_case_end();
    if (p) pCoTaskMemFree(p);
}

static const GUID bhid_transfer = {0xd5e346a1, 0xf753, 0x4932, {0xb4, 0x03, 0x45, 0x74, 0x80, 0x0e, 0x24, 0x98}};

static IShellItem *item_at(const wchar_t *rel, HRESULT *phr)
{
    wchar_t full[2 * MAX_PATH];
    IShellItem *si = NULL;
    swprintf(full, 2 * MAX_PATH, L"%ls\\%ls", cwd, rel);
    *phr = pSHCreateItemFromParsingName(full, NULL, &IID_IShellItem, (void **)&si);
    return SUCCEEDED(*phr) ? si : NULL;
}

/* Copying an entry out the way Explorer does: the archive's ITransferSource,
 * and IFileOperation::CopyItem into out\ (no UI). */
static void transfer_probe(const char *name, const wchar_t *rel, const wchar_t *leaf)
{
    HRESULT hr;
    IShellItem *si = item_at(rel, &hr);
    oracle_case(name);
    o_hex32("create_hr", (DWORD)hr);
    if (si) {
        ITransferSource *ts = NULL;
        hr = si->lpVtbl->BindToHandler(si, NULL, &bhid_transfer, &IID_ITransferSource, (void **)&ts);
        o_hex32("transfer_hr", (DWORD)hr);
        if (ts) {
            IStream *st = NULL;
            hr = ts->lpVtbl->OpenItem(ts, si, TSF_NORMAL, &IID_IStream, (void **)&st);
            o_hex32("openitem_hr", (DWORD)hr);
            if (SUCCEEDED(hr) && st) {
                stream_fields(st, "openitem");
                st->lpVtbl->Release(st);
            }
            ts->lpVtbl->Release(ts);
        }
        IFileOperation *op = NULL;
        CreateDirectoryW(L"out", NULL);
        hr = pCoCreateInstance(&CLSID_FileOperation, NULL, CLSCTX_ALL, &IID_IFileOperation, (void **)&op);
        o_hex32("fileop_hr", (DWORD)hr);
        if (op) {
            HRESULT hr2;
            IShellItem *dest = item_at(L"out", &hr2);
            op->lpVtbl->SetOperationFlags(op, FOF_NO_UI);
            hr = dest ? op->lpVtbl->CopyItem(op, si, dest, NULL, NULL) : hr2;
            o_hex32("copyitem_hr", (DWORD)hr);
            if (SUCCEEDED(hr)) {
                BOOL aborted = FALSE;
                hr = op->lpVtbl->PerformOperations(op);
                o_hex32("perform_hr", (DWORD)hr);
                op->lpVtbl->GetAnyOperationsAborted(op, &aborted);
                o_bool("aborted", aborted);
            }
            wchar_t path[MAX_PATH];
            swprintf(path, MAX_PATH, L"out\\%ls", leaf);
            HANDLE h = CreateFileW(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
            o_bool("copied", h != INVALID_HANDLE_VALUE);
            if (h != INVALID_HANDLE_VALUE) {
                char data[64] = "";
                DWORD got = 0;
                FILETIME ft;
                ReadFile(h, data, sizeof data - 1, &got, NULL);
                data[got] = 0;
                o_str("copied_data", data);
                if (GetFileTime(h, NULL, NULL, &ft))
                    o_bool("copied_time_from_archive",
                           (((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime) == FILETIME_OF(UNIX_TIME));
                CloseHandle(h);
                DeleteFileW(path);
            }
            if (dest) dest->lpVtbl->Release(dest);
            op->lpVtbl->Release(op);
        }
        si->lpVtbl->Release(si);
    }
    oracle_case_end();
}

/* Binds t\<file>, reports the bind, enumerates it, and copies <child> out. */
static void archive_probe(const char *prefix, const wchar_t *file, const wchar_t *child)
{
    char nm[128];
    HRESULT hr;
    PIDLIST_RELATIVE p = parse(tdir_sf, file, &hr);
    snprintf(nm, sizeof nm, "%s_item", prefix);
    oracle_case(nm);
    o_hex32("parse_hr", (DWORD)hr);
    if (p) item_fields(tdir_sf, (PCUITEMID_CHILD)p);
    oracle_case_end();
    if (!p) return;
    IShellFolder *sub = bind_child(tdir_sf, p, &hr);
    pCoTaskMemFree(p);
    snprintf(nm, sizeof nm, "%s_bind", prefix);
    oracle_case(nm);
    o_hex32("hr", (DWORD)hr);
    oracle_case_end();
    if (!sub) return;
    enum_folder(prefix, sub, 0);
    if (child) {
        snprintf(nm, sizeof nm, "%s_copyout", prefix);
        copyout_probe(nm, sub, child);
    }
    sub->lpVtbl->Release(sub);
}

/* Anything the shell shows (a password prompt, an error box) is closed after 10 s. */
static DWORD main_tid;
static volatile LONG watchdog_fired, watchdog_stop;

static BOOL CALLBACK close_window(HWND w, LPARAM lp)
{
    (void)lp;
    if (!IsWindowVisible(w)) return TRUE;
    InterlockedExchange(&watchdog_fired, 1);
    PostMessageW(w, WM_COMMAND, IDCANCEL, 0);
    PostMessageW(w, WM_CLOSE, 0, 0);
    return TRUE;
}

static DWORD WINAPI watchdog(void *arg)
{
    (void)arg;
    for (int i = 0; i < 100 && !watchdog_stop; i++) Sleep(100);
    while (!watchdog_stop) {
        EnumThreadWindows(main_tid, close_window, 0);
        Sleep(500);
    }
    return 0;
}

int main(void)
{
    oracle_begin("shell32/compressed_formats");
    HMODULE sh = LoadLibraryW(L"shell32.dll");
    HMODULE ole = LoadLibraryW(L"ole32.dll");
    HMODULE shl = LoadLibraryW(L"shlwapi.dll");
    LOAD(sh, SHGetDesktopFolder);
    LOAD(sh, SHGetFileInfoW);
    LOAD(shl, StrRetToBufW);
    LOAD(ole, CoInitializeEx);
    LOAD(ole, CoTaskMemFree);
    LOAD(ole, ReleaseStgMedium);
    LOAD(ole, CoCreateInstance);
    LOAD(sh, SHCreateItemFromParsingName);
    int ok = pSHGetDesktopFolder && pSHGetFileInfoW && pStrRetToBufW && pCoInitializeEx && pCoTaskMemFree &&
             pReleaseStgMedium && pCoCreateInstance && pSHCreateItemFromParsingName;
    oracle_case("setup");
    o_bool("loaded", ok);
    oracle_case_end();
    if (!ok) return oracle_end();
    pCoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    pSHGetDesktopFolder(&desktop);
    GetCurrentDirectoryW(MAX_PATH, cwd);
    CreateDirectoryW(L"t", NULL);

    build_7z(); write_buf(L"t\\a.7z");
    build_rar5(0); write_buf(L"t\\a.rar");
    build_rar5(1); write_buf(L"t\\enc.rar");
    build_cab(); write_buf(L"t\\a.cab");
    build_cpio(); write_buf(L"t\\a.cpio");
    build_tar(); take_raw(); bzip2_raw(); write_buf(L"t\\a.tar.bz2");
    build_tar(); take_raw(); xz_raw(); write_buf(L"t\\a.tar.xz");
    build_tar(); take_raw(); zstd_raw(); write_buf(L"t\\a.tar.zst");
    static const char single[] = "single stream text\n";
    memcpy(raw, single, sizeof single - 1); rawlen = sizeof single - 1;
    bzip2_raw(); write_buf(L"t\\s.txt.bz2"); write_buf(L"t\\s.bz2"); write_buf(L"t\\s.tbz2");
    xz_raw(); write_buf(L"t\\s.txt.xz"); write_buf(L"t\\s.xz");
    zstd_raw(); write_buf(L"t\\s.txt.zst"); write_buf(L"t\\s.zst");

    sfi("sfi_7z", L"t\\a.7z");
    sfi("sfi_rar", L"t\\a.rar");
    sfi("sfi_cab", L"t\\a.cab");
    sfi("sfi_tar_bz2", L"t\\a.tar.bz2");
    sfi("sfi_txt_xz", L"t\\s.txt.xz");

    {
        wchar_t tdir[MAX_PATH];
        swprintf(tdir, MAX_PATH, L"%ls\\t", cwd);
        PIDLIST_RELATIVE p = parse(desktop, tdir, NULL);
        if (p) tdir_sf = bind_child(desktop, p, NULL);
        if (p) pCoTaskMemFree(p);
    }
    oracle_case("tdir");
    o_bool("bound", tdir_sf != NULL);
    oracle_case_end();
    if (!tdir_sf) return oracle_end();

    archive_probe("7z", L"a.7z", L"readme.txt");
    archive_probe("rar", L"a.rar", L"readme.txt");
    archive_probe("cab", L"a.cab", L"readme.txt");
    archive_probe("cpio", L"a.cpio", L"readme.txt");
    archive_probe("tar_bz2", L"a.tar.bz2", L"readme.txt");
    archive_probe("tar_xz", L"a.tar.xz", L"readme.txt");
    archive_probe("tar_zst", L"a.tar.zst", L"readme.txt");
    archive_probe("txt_bz2", L"s.txt.bz2", NULL);
    archive_probe("bz2", L"s.bz2", NULL);
    archive_probe("tbz2", L"s.tbz2", NULL);
    archive_probe("txt_xz", L"s.txt.xz", NULL);
    archive_probe("xz", L"s.xz", NULL);
    archive_probe("txt_zst", L"s.txt.zst", NULL);
    archive_probe("zst", L"s.zst", NULL);
    /* the single entry, copied out by its parsing name */
    static const struct { const char *k; const wchar_t *file; } singles[] = {
        {"txt_bz2", L"s.txt.bz2"}, {"txt_xz", L"s.txt.xz"}, {"txt_zst", L"s.txt.zst"},
    };
    for (size_t i = 0; i < sizeof singles / sizeof singles[0]; i++) {
        char nm[64];
        PIDLIST_RELATIVE p = parse(tdir_sf, singles[i].file, NULL);
        IShellFolder *sub = p ? bind_child(tdir_sf, p, NULL) : NULL;
        if (p) pCoTaskMemFree(p);
        snprintf(nm, sizeof nm, "%s_copyout", singles[i].k);
        if (sub) {
            copyout_probe(nm, sub, L"s.txt");
            sub->lpVtbl->Release(sub);
        }
    }

    /* copying may show progress or error UI: closed by the watchdog */
    main_tid = GetCurrentThreadId();
    HANDLE th = CreateThread(NULL, 0, watchdog, NULL, 0, NULL);
    transfer_probe("7z_transfer", L"t\\a.7z\\readme.txt", L"readme.txt");
    transfer_probe("7z_transfer_nested", L"t\\a.7z\\docs\\inner.txt", L"inner.txt");
    transfer_probe("rar_transfer", L"t\\a.rar\\readme.txt", L"readme.txt");
    transfer_probe("cab_transfer", L"t\\a.cab\\readme.txt", L"readme.txt");
    transfer_probe("cpio_transfer", L"t\\a.cpio\\readme.txt", L"readme.txt");
    transfer_probe("tar_bz2_transfer", L"t\\a.tar.bz2\\readme.txt", L"readme.txt");
    transfer_probe("tar_xz_transfer", L"t\\a.tar.xz\\docs\\inner.txt", L"inner.txt");
    transfer_probe("tar_zst_transfer", L"t\\a.tar.zst\\readme.txt", L"readme.txt");
    transfer_probe("txt_bz2_transfer", L"t\\s.txt.bz2\\s.txt", L"s.txt");
    transfer_probe("txt_xz_transfer", L"t\\s.txt.xz\\s.txt", L"s.txt");
    transfer_probe("txt_zst_transfer", L"t\\s.txt.zst\\s.txt", L"s.txt");
    oracle_case("transfer_ui");
    o_bool("window_closed", watchdog_fired);
    oracle_case_end();

    /* encrypted entries (last: reading one may ask for a password) */
    watchdog_fired = 0;
    archive_probe("enc_rar", L"enc.rar", L"readme.txt");
    transfer_probe("enc_rar_transfer", L"t\\enc.rar\\readme.txt", L"readme.txt");
    InterlockedExchange(&watchdog_stop, 1);
    WaitForSingleObject(th, 5000);
    CloseHandle(th);
    oracle_case("enc_rar_ui");
    o_bool("window_closed", watchdog_fired);
    oracle_case_end();

    verbs_probe("verbs_7z", L"a.7z");
    verbs_probe("verbs_rar", L"a.rar");
    verbs_probe("verbs_tar_xz", L"a.tar.xz");
    verbs_probe("verbs_txt_zst", L"s.txt.zst");
    verbs_probe("verbs_cpio", L"a.cpio");
    verbs_probe("verbs_cab", L"a.cab");

    tdir_sf->lpVtbl->Release(tdir_sf);
    desktop->lpVtbl->Release(desktop);
    return oracle_end();
}
