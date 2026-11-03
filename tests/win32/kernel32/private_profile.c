/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: the private profile (.ini) APIs against files the test writes
 * in its own directory: GetPrivateProfileStringW (defaults, trimming, quotes,
 * truncation, section and key lists), WritePrivateProfileStringW (create,
 * replace, delete key, delete section, file layout), GetPrivateProfileIntW,
 * GetPrivateProfileSectionW / WritePrivateProfileSectionW,
 * GetPrivateProfileSectionNamesW, the Struct functions and UTF-16 files.
 *
 * Files are addressed by full path (relative names mean the Windows
 * directory), and the path itself is never printed.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-getprivateprofilestringw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-writeprivateprofilestringw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-getprivateprofileintw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-getprivateprofilesectionw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-writeprivateprofilesectionw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-getprivateprofilesectionnamesw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-getprivateprofilestructw
 */
#include "oracle.h"

static wchar_t ini[MAX_PATH], ini2[MAX_PATH], uini[MAX_PATH], missing[MAX_PATH];

static void write_raw(const wchar_t *path, const void *data, DWORD n)
{
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    DWORD w;
    WriteFile(h, data, n, &w, NULL);
    CloseHandle(h);
}

static void file_bytes(const char *k, const wchar_t *path)
{
    unsigned char data[256];
    DWORD got = 0;
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        o_str(k, "missing");
        return;
    }
    ReadFile(h, data, sizeof data, &got, NULL);
    CloseHandle(h);
    o_bytes(k, data, got);
}

/* A double-NUL-terminated list (or a truncated one) as "a|b|c", exact length. */
static void o_multi(const char *k, const wchar_t *buf, DWORD n)
{
    wchar_t tmp[512];
    DWORD i;
    for (i = 0; i < n && i < 511; i++) tmp[i] = buf[i] ? buf[i] : L'|';
    tmp[i] = 0;
    o_wstr(k, tmp);
}

static void gps(const char *name, const wchar_t *path, const wchar_t *app, const wchar_t *key,
                 const wchar_t *def, DWORD size)
{
    wchar_t buf[512];
    DWORD r;
    wmemset(buf, L'#', 512);
    ORACLE_CALL(r, GetPrivateProfileStringW(app, key, def, buf, size, path));
    oracle_case(name);
    o_u32("ret", r);
    o_lasterr();
    if (app && key) o_wstr("value", buf);
    else o_multi("list", buf, r + 1 < size ? r + 1 : size);
    oracle_case_end();
}

static void geti(const char *name, const wchar_t *key, INT def)
{
    UINT r;
    ORACLE_CALL(r, GetPrivateProfileIntW(L"ints", key, def, ini));
    oracle_case(name);
    o_u32("ret", r);
    o_lasterr();
    oracle_case_end();
}

static void put(const char *name, const wchar_t *path, const wchar_t *app, const wchar_t *key, const wchar_t *val)
{
    BOOL r;
    ORACLE_CALL(r, WritePrivateProfileStringW(app, key, val, path));
    oracle_case(name);
    o_bool("ret", r);
    o_lasterr();
    file_bytes("file", path);
    oracle_case_end();
}

int main(void)
{
    oracle_begin("kernel32/private_profile");

    GetFullPathNameW(L"t.ini", MAX_PATH, ini, NULL);
    GetFullPathNameW(L"w.ini", MAX_PATH, ini2, NULL);
    GetFullPathNameW(L"u.ini", MAX_PATH, uini, NULL);
    GetFullPathNameW(L"missing.ini", MAX_PATH, missing, NULL);

    static const char text[] =
        "; comment line\r\n"
        "[main]\r\n"
        "key=value\r\n"
        "  spaced  =  padded value  \r\n"
        "quoted=\"in quotes\"\r\n"
        "single='single'\r\n"
        "half=\"open\r\n"
        "empty=\r\n"
        "dup=first\r\n"
        "dup=second\r\n"
        "noequals\r\n"
        ";key2=commented\r\n"
        "Mixed=Case\r\n"
        "\r\n"
        "[ spaced section ]\r\n"
        "a=1\r\n"
        "[ints]\r\n"
        "dec=42\r\n"
        "neg=  -7\r\n"
        "hex=0x10\r\n"
        "trail=12abc\r\n"
        "big=4294967295\r\n"
        "over=4294967296\r\n"
        "plus=+5\r\n"
        "blank=\r\n"
        "[main]\r\n"
        "late=second main section\r\n";
    write_raw(ini, text, sizeof text - 1);

    gps("get_value", ini, L"main", L"key", L"def", 64);
    gps("get_case_insensitive", ini, L"MAIN", L"KEY", L"def", 64);
    gps("get_trimmed", ini, L"main", L"spaced", L"def", 64);
    gps("get_quotes_stripped", ini, L"main", L"quoted", L"def", 64);
    gps("get_single_quotes_stripped", ini, L"main", L"single", L"def", 64);
    gps("get_unbalanced_quote", ini, L"main", L"half", L"def", 64);
    gps("get_empty_value", ini, L"main", L"empty", L"def", 64);
    gps("get_duplicate_first_wins", ini, L"main", L"dup", L"def", 64);
    gps("get_no_equals_line", ini, L"main", L"noequals", L"def", 64);
    gps("get_commented_key", ini, L"main", L";key2", L"def", 64);
    gps("get_second_section_block", ini, L"main", L"late", L"def", 64);
    gps("get_spaced_section", ini, L"spaced section", L"a", L"def", 64);
    gps("get_missing_key_default", ini, L"main", L"nope", L"the default", 64);
    gps("get_default_trailing_spaces", ini, L"main", L"nope", L"dflt   ", 64);
    gps("get_default_null", ini, L"main", L"nope", NULL, 64);
    gps("get_missing_section", ini, L"nosuch", L"key", L"d", 64);
    gps("get_truncated", ini, L"main", L"spaced", L"def", 5);
    gps("get_size_1", ini, L"main", L"key", L"def", 1);
    gps("get_default_truncated", ini, L"main", L"nope", L"longdefault", 4);
    gps("get_missing_file", missing, L"main", L"key", L"d", 64);
    gps("list_keys", ini, L"main", NULL, L"d", 512);
    gps("list_keys_truncated", ini, L"main", NULL, L"d", 10);
    gps("list_sections", ini, NULL, NULL, L"d", 512);
    gps("list_sections_truncated", ini, NULL, NULL, L"d", 8);
    gps("list_keys_missing_section", ini, L"nosuch", NULL, L"dflt", 64);

    geti("int_decimal", L"dec", 5);
    geti("int_leading_spaces_negative", L"neg", 5);
    geti("int_hex_text", L"hex", 5);
    geti("int_trailing_letters", L"trail", 5);
    geti("int_max_u32", L"big", 5);
    geti("int_overflow", L"over", 5);
    geti("int_plus_sign", L"plus", 5);
    geti("int_blank", L"blank", 5);
    geti("int_missing", L"nope", -3);

    {
        wchar_t buf[512];
        DWORD r;
        ORACLE_CALL(r, GetPrivateProfileSectionW(L"ints", buf, 512, ini));
        oracle_case("section_ints");
        o_u32("ret", r);
        o_lasterr();
        o_multi("list", buf, r + 1);
        oracle_case_end();
        ORACLE_CALL(r, GetPrivateProfileSectionW(L"main", buf, 512, ini));
        oracle_case("section_main_raw_lines");
        o_u32("ret", r);
        o_lasterr();
        o_multi("list", buf, r + 1);
        oracle_case_end();
        wmemset(buf, L'#', 512);
        ORACLE_CALL(r, GetPrivateProfileSectionW(L"ints", buf, 12, ini));
        oracle_case("section_truncated");
        o_u32("ret", r);
        o_lasterr();
        o_multi("list", buf, 12);
        oracle_case_end();
        ORACLE_CALL(r, GetPrivateProfileSectionW(L"nosuch", buf, 512, ini));
        oracle_case("section_missing");
        o_u32("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, GetPrivateProfileSectionNamesW(buf, 512, ini));
        oracle_case("section_names");
        o_u32("ret", r);
        o_lasterr();
        o_multi("list", buf, r + 1);
        oracle_case_end();
        wmemset(buf, L'#', 512);
        ORACLE_CALL(r, GetPrivateProfileSectionNamesW(buf, 7, ini));
        oracle_case("section_names_truncated");
        o_u32("ret", r);
        o_lasterr();
        o_multi("list", buf, 7);
        oracle_case_end();
    }

    /* Writing. */
    put("write_creates_file", ini2, L"sec", L"k", L"v");
    put("write_second_key", ini2, L"sec", L"k2", L"v2");
    put("write_replace_value", ini2, L"SEC", L"K", L"new");
    put("write_new_section", ini2, L"other", L"x", L"1");
    put("write_value_with_spaces", ini2, L"sec", L"sp", L"  a b  ");
    put("write_empty_value", ini2, L"sec", L"e", L"");
    put("write_delete_key", ini2, L"sec", L"k2", NULL);
    put("write_delete_missing_key", ini2, L"sec", L"nope", NULL);
    put("write_delete_section", ini2, L"other", NULL, NULL);
    put("write_null_section", ini2, NULL, L"k", L"v");
    gps("read_back_spaces", ini2, L"sec", L"sp", L"d", 64);
    {
        BOOL r;
        ORACLE_CALL(r, WritePrivateProfileSectionW(L"sec", L"a=1\0b = 2\0junk\0", ini2));
        oracle_case("write_section_replaces");
        o_bool("ret", r);
        o_lasterr();
        file_bytes("file", ini2);
        oracle_case_end();
        ORACLE_CALL(r, WritePrivateProfileSectionW(L"sec", NULL, ini2));
        oracle_case("write_section_null_deletes");
        o_bool("ret", r);
        o_lasterr();
        file_bytes("file", ini2);
        oracle_case_end();
    }

    /* Structs. */
    {
        BYTE data[4] = { 0x01, 0xab, 0x00, 0xff }, back[4] = { 0 };
        BOOL r;
        DeleteFileW(ini2);
        ORACLE_CALL(r, WritePrivateProfileStructW(L"st", L"blob", data, 4, ini2));
        oracle_case("struct_write");
        o_bool("ret", r);
        o_lasterr();
        file_bytes("file", ini2);
        oracle_case_end();
        ORACLE_CALL(r, GetPrivateProfileStructW(L"st", L"blob", back, 4, ini2));
        oracle_case("struct_read");
        o_bool("ret", r);
        o_lasterr();
        o_bytes("data", back, 4);
        oracle_case_end();
        ORACLE_CALL(r, GetPrivateProfileStructW(L"st", L"blob", back, 3, ini2));
        oracle_case("struct_read_wrong_size");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        put("struct_corrupt_checksum", ini2, L"st", L"blob", L"01AB00FF00");
        ORACLE_CALL(r, GetPrivateProfileStructW(L"st", L"blob", back, 4, ini2));
        oracle_case("struct_read_bad_checksum");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
    }

    /* UTF-16 file. */
    {
        static const wchar_t utext[] = L"\xfeff[u]\r\nname=caf\x00e9 \x4e2d\r\n";
        write_raw(uini, utext, sizeof utext - sizeof(wchar_t));
        gps("utf16_read", uini, L"u", L"name", L"d", 64);
        put("utf16_write_keeps_encoding", uini, L"u", L"new", L"\x00e9");
        DeleteFileW(ini2);
        put("ansi_write_non_latin", ini2, L"a", L"k", L"\x4e2d\x00e9");
        gps("ansi_read_back", ini2, L"a", L"k", L"d", 64);
    }

    return oracle_end();
}
