/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * encoding_cases.h: .reg file encodings, shared by reg/import_encoding and
 * regedit/import_encoding. The including file defines EC_TEST (the test id),
 * EC_SUB (its key under HKCU, as a wide string), EC_KEY (the same key as a
 * UTF-8 HKEY_CURRENT_USER\... path) and EC_CMD (the import command line for
 * "in.reg").
 *
 * Every case writes in.reg byte by byte in one encoding, imports it into its
 * own subkey and prints the exit code, output and that subkey's tree.
 */
#ifndef SETU_ENCODING_CASES_H
#define SETU_ENCODING_CASES_H

#include "regtool.h"

static unsigned char ec_buf[8192];
static size_t ec_len;

static void ec_bytes(const void *p, size_t n)
{
    if (ec_len + n > sizeof ec_buf) n = sizeof ec_buf - ec_len;
    memcpy(ec_buf + ec_len, p, n);
    ec_len += n;
}

/* Appends UTF-8 text converted to the given code page / UTF-16 form. */
enum ec_form { EC_UTF8, EC_ANSI, EC_UTF16LE, EC_UTF16BE };

static void ec_text(enum ec_form form, const char *utf8)
{
    static wchar_t w[4096];
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w, 4096) - 1;
    if (n < 0) n = 0;
    switch (form) {
    case EC_UTF8: ec_bytes(utf8, strlen(utf8)); break;
    case EC_ANSI: {
        static char a[8192];
        int m = WideCharToMultiByte(1252, 0, w, n, a, sizeof a, NULL, NULL);
        ec_bytes(a, (size_t)m);
        break;
    }
    case EC_UTF16LE: ec_bytes(w, (size_t)n * 2); break;
    case EC_UTF16BE:
        for (int i = 0; i < n; i++) {
            unsigned char b[2] = {(unsigned char)(w[i] >> 8), (unsigned char)w[i]};
            ec_bytes(b, 2);
        }
        break;
    }
}

static void ec_case(const char *name, const char *sub)
{
    wchar_t wsub[256];
    rt_write_file(L"in.reg", ec_buf, (DWORD)ec_len);
    ec_len = 0;
    rt_run(EC_CMD);
    oracle_case(name);
    o_u32("exit", rt_exit);
    o_str("out", rt_out);
    o_str("err", rt_err);
    swprintf(wsub, 256, L"%ls\\%hs", EC_SUB, sub);
    o_regtree("tree", HKEY_CURRENT_USER, wsub);
    oracle_case_end();
}

#define V5H "Windows Registry Editor Version 5.00\r\n\r\n"
#define R4H "REGEDIT4\r\n\r\n"

static void ec_run_all(void)
{
    static const unsigned char bom8[] = {0xef, 0xbb, 0xbf}, bomle[] = {0xff, 0xfe}, bombe[] = {0xfe, 0xff};

    ec_bytes(bomle, 2);
    ec_text(EC_UTF16LE, V5H "[" EC_KEY "\\le]\r\n\"caf\xc3\xa9\"=\"\xe4\xb8\xad\xe6\x96\x87\"\r\n"
                        "\"emoji\"=\"\xf0\x9f\x98\x80\"\r\n\r\n");
    ec_case("utf16le_bom_v5", "le");

    ec_bytes(bomle, 2);
    ec_text(EC_UTF16LE, "[" EC_KEY "\\noheader]\r\n\"a\"=\"1\"\r\n");
    ec_case("utf16le_bom_no_header", "noheader");

    ec_text(EC_UTF16LE, V5H "[" EC_KEY "\\lenobom]\r\n\"a\"=\"1\"\r\n\r\n");
    ec_case("utf16le_no_bom_v5", "lenobom");

    ec_bytes(bombe, 2);
    ec_text(EC_UTF16BE, V5H "[" EC_KEY "\\be]\r\n\"a\"=\"1\"\r\n\r\n");
    ec_case("utf16be_bom_v5", "be");

    ec_bytes(bomle, 2);
    ec_text(EC_UTF16LE, R4H "[" EC_KEY "\\le4]\r\n\"a\"=\"\xc3\xa9\"\r\n\r\n");
    ec_case("utf16le_bom_regedit4", "le4");

    ec_bytes(bom8, 3);
    ec_text(EC_UTF8, V5H "[" EC_KEY "\\u8bom]\r\n\"a\"=\"\xc3\xa9\"\r\n\r\n");
    ec_case("utf8_bom_v5", "u8bom");

    ec_bytes(bom8, 3);
    ec_text(EC_UTF8, R4H "[" EC_KEY "\\u8bom4]\r\n\"a\"=\"\xc3\xa9\"\r\n\r\n");
    ec_case("utf8_bom_regedit4", "u8bom4");

    ec_text(EC_UTF8, R4H "[" EC_KEY "\\u8]\r\n\"a\"=\"\xc3\xa9\"\r\n\r\n");
    ec_case("utf8_no_bom_regedit4", "u8");

    ec_text(EC_ANSI, R4H "[" EC_KEY "\\ansi]\r\n\"caf\xc3\xa9\"=\"\xc3\xa9\xe2\x82\xac\"\r\n\r\n");
    ec_case("cp1252_regedit4", "ansi");

    ec_text(EC_ANSI, V5H "[" EC_KEY "\\ansi5]\r\n\"a\"=\"\xc3\xa9\"\r\n\r\n");
    ec_case("cp1252_v5_header", "ansi5");

    ec_bytes(bomle, 2);
    ec_text(EC_UTF16LE, V5H "[" EC_KEY "\\cr]\r\"a\"=\"1\"\r\"b\"=\"2\"\r\r");
    ec_case("utf16le_cr_only_line_ends", "cr");

    ec_bytes(bomle, 2);
    ec_text(EC_UTF16LE, V5H "[" EC_KEY "\\lf]\n\"a\"=\"1\"\n\"b\"=\"2\"\n\n");
    ec_case("utf16le_lf_only_line_ends", "lf");

    ec_bytes(bomle, 2);
    ec_text(EC_UTF16LE, V5H "[" EC_KEY "\\\xe4\xb8\xad\xe6\x96\x87]\r\n\"k\"=\"cjk key\"\r\n\r\n");
    ec_case("utf16le_cjk_key_name", "\xe4\xb8\xad\xe6\x96\x87");

    ec_bytes(bomle, 2);
    ec_text(EC_UTF16LE, V5H "[" EC_KEY "\\hexstr]\r\n\"a\"=hex(1):e9,00,00,00\r\n\"b\"=hex(2):41,00\r\n\r\n");
    ec_case("utf16le_hex_strings", "hexstr");

    ec_bytes(bomle, 2);
    ec_case("bom_only", "none");

    ec_bytes(bomle, 2);
    ec_text(EC_UTF16LE, V5H "[" EC_KEY "\\eof]\r\n\"a\"=\"1\"");
    ec_case("utf16le_no_final_newline", "eof");
}

#endif /* SETU_ENCODING_CASES_H */
