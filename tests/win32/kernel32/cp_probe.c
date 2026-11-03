/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle probe test: code pages that Windows implements with converter DLLs
 * rather than NLS tables (ISO-2022-JP 50220-50222, GB18030 54936 and
 * friends): which ones IsValidCodePage / GetCPInfo accept, the full GB18030
 * mapping of the BMP, and ISO-2022-JP encodings of sample strings.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/intl/code-page-identifiers
 *       https://learn.microsoft.com/en-us/windows/win32/api/winnls/nf-winnls-isvalidcodepage
 *       https://learn.microsoft.com/en-us/windows/win32/api/stringapiset/nf-stringapiset-widechartomultibyte
 *       https://learn.microsoft.com/en-us/windows/win32/api/stringapiset/nf-stringapiset-multibytetowidechar
 */
#include "oracle.h"

static char big[60000];

static void cat(const char *s)
{
    strncat(big, s, sizeof big - strlen(big) - 1);
}

static void valid_cases(void)
{
    static const UINT cps[] = { 50220, 50221, 50222, 50225, 50227, 50229, 51932, 51936, 51949,
                                52936, 54936, 57002, 57003, 57004, 57005, 57006, 57007, 57008,
                                57009, 57010, 57011, 20000, 20932, 20936, 20949, 1047, 1361,
                                65000, 65001, 1200, 1201, 12000, 12001, 708, 720, 10001, 10008 };
    char t[200];
    big[0] = 0;
    for (size_t i = 0; i < sizeof cps / sizeof cps[0]; i++) {
        CPINFO ci;
        BOOL v = IsValidCodePage(cps[i]);
        SetLastError(0);
        BOOL r = GetCPInfo(cps[i], &ci);
        snprintf(t, sizeof t, "%u:%d,%d", cps[i], v, r);
        cat(t);
        if (r) {
            snprintf(t, sizeof t, ",%u,%02x%02x,", ci.MaxCharSize, ci.DefaultChar[0], ci.DefaultChar[1]);
            cat(t);
            for (int j = 0; j < MAX_LEADBYTES && ci.LeadByte[j]; j++) {
                snprintf(t, sizeof t, "%02x", ci.LeadByte[j]);
                cat(t);
            }
        } else {
            snprintf(t, sizeof t, ",err%lu", GetLastError());
            cat(t);
        }
        cat(";");
    }
    oracle_case("valid_and_cpinfo");
    o_str("cps", big);
    oracle_case_end();
}

/* GB18030: for every BMP code point, the bytes WideCharToMultiByte gives,
 * as runs of code points whose byte sequences (read as big-endian numbers
 * of the same length) increase by one. */
static void gb_block(unsigned base)
{
    char name[32], t[64];
    unsigned run_start = 0, run_len = 0;
    unsigned long long run_code = 0;
    int run_n = 0;
    big[0] = 0;
    for (unsigned c = base; c <= base + 0x1000; c++) {
        unsigned char out[8];
        int n = 0;
        unsigned long long code = 0;
        if (c < base + 0x1000 && (c < 0xd800 || c > 0xdfff)) {
            WCHAR w = (WCHAR)c;
            BOOL used = FALSE;
            n = WideCharToMultiByte(54936, 0, &w, 1, (char *)out, sizeof out, NULL, NULL);
            (void)used;
            for (int i = 0; i < n; i++) code = code << 8 | out[i];
        }
        if (run_len && n == run_n && code == run_code + run_len) {
            run_len++;
            continue;
        }
        if (run_len) {
            snprintf(t, sizeof t, "%04x:%llx+%u,", run_start, run_code, run_len);
            cat(t);
        }
        run_len = n ? 1 : 0;
        run_start = c;
        run_code = code;
        run_n = n;
    }
    snprintf(name, sizeof name, "gb18030_%04x", base);
    oracle_case(name);
    o_str("runs", big);
    oracle_case_end();
}

static void mb_case(const char *name, UINT cp, DWORD flags, const char *in, int len)
{
    WCHAR out[64];
    int r;
    SetLastError(0);
    r = MultiByteToWideChar(cp, flags, in, len, out, 64);
    oracle_case(name);
    o_i64("ret", r);
    o_u32("err", GetLastError());
    if (r > 0) o_bytes("out", out, r * sizeof(WCHAR));
    oracle_case_end();
}

static void wc_case(const char *name, UINT cp, const WCHAR *in, int len)
{
    char out[128];
    int r, r0;
    SetLastError(0);
    r0 = WideCharToMultiByte(cp, 0, in, len, NULL, 0, NULL, NULL);
    r = WideCharToMultiByte(cp, 0, in, len, out, sizeof out, NULL, NULL);
    oracle_case(name);
    o_i64("size", r0);
    o_i64("ret", r);
    o_u32("err", GetLastError());
    if (r > 0) o_bytes("out", out, r);
    oracle_case_end();
}

int main(void)
{
    oracle_begin("kernel32/cp_probe");
    valid_cases();
    for (unsigned b = 0; b < 0x10000; b += 0x1000)
        if (b != 0xd000) gb_block(b);
    gb_block(0xd000); /* surrogates skipped inside */

    wc_case("gb_supplementary", 54936, L"\xd840\xdc00\xdbff\xdfff", 4);
    wc_case("gb_lone_surrogate", 54936, L"a\xd800" L"b", 3);
    mb_case("gb_decode_4byte", 54936, 0, "\x81\x30\x81\x30\x84\x31\xa4\x39\x90\x30\x81\x30", 12);
    mb_case("gb_decode_2byte", 54936, 0, "\xb0\xa1\xa1\xa1\x80\xff", 6);
    mb_case("gb_decode_invalid_strict", 54936, MB_ERR_INVALID_CHARS, "\x81\x30\x81", 3);
    mb_case("gb_decode_invalid", 54936, 0, "\x81\x30\x81", 3);
    mb_case("gb_decode_trailing_lead", 54936, 0, "a\x81", 2);

    static const WCHAR mixed[] = L"A\x3042\x30a2\xff71\x4e00Z";
    for (UINT cp = 50220; cp <= 50222; cp++) {
        char n[64];
        snprintf(n, sizeof n, "jis_%u_ascii", cp);
        wc_case(n, cp, L"abc", 3);
        snprintf(n, sizeof n, "jis_%u_mixed", cp);
        wc_case(n, cp, mixed, 6);
        snprintf(n, sizeof n, "jis_%u_halfwidth_kana", cp);
        wc_case(n, cp, L"\xff71\xff72\xff9e", 3);
        snprintf(n, sizeof n, "jis_%u_nec_ibm", cp);
        wc_case(n, cp, L"\x2460\x2170\x7e8a\xffe2", 4);
        snprintf(n, sizeof n, "jis_%u_unmappable", cp);
        wc_case(n, cp, L"\x00e9\x0100", 2);
        snprintf(n, sizeof n, "jis_%u_yen_tilde", cp);
        wc_case(n, cp, L"\x00a5\x203e\x005c\x007e", 4);
        snprintf(n, sizeof n, "jis_%u_decode", cp);
        mb_case(n, cp, 0, "a\x1b$B$\"\x1b(Bb\x1b(I1\x1b(B\x0e\x31\x0f", 17);
    }
    mb_case("jis_decode_jisx0212", 50220, 0, "\x1b$(D\x22\x2f\x1b(B", 9);
    mb_case("jis_decode_bad_escape", 50220, 0, "\x1b$Zab", 5);
    return oracle_end();
}
