/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: RegEnumKeyExW buffer handling: name buffer sizes around the
 * name length (with and without room for the NUL), zero size, what *lpcName
 * and the buffer hold after ERROR_MORE_DATA, class buffers (size, too small,
 * NULL buffer with a size, size without buffer), the last-write-time pointer,
 * the reserved parameter, and indexes past the end.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regenumkeyexw
 *       https://learn.microsoft.com/en-us/windows/win32/sysinfo/registry-element-size-limits
 */
#include "oracle.h"

static HKEY root;

/* name of 7 chars "Charlie" with class "MyClass1" (8 chars). */
static void enum_case(const char *name, DWORD index, DWORD name_cap, int name_null, DWORD class_cap, int class_buf,
                      int want_class, int want_time, DWORD *reserved)
{
    wchar_t nm[64], cls[64];
    DWORD n = name_cap, cn = class_cap;
    FILETIME ft = {0xAAAAAAAAu, 0xAAAAAAAAu};
    wmemset(nm, L'#', 63); nm[63] = 0;
    wmemset(cls, L'#', 63); cls[63] = 0;
    LONG st;
    ORACLE_CALL(st, RegEnumKeyExW(root, index, name_null ? NULL : nm, &n, reserved,
                                  want_class && class_buf ? cls : NULL, want_class ? &cn : NULL, want_time ? &ft : NULL));
    oracle_case(name);
    o_status("ret", st);
    o_lasterr();
    o_u32("name_len", n);
    if (!name_null) {
        nm[20] = 0;
        o_wstr("name_buf", nm);
    }
    if (want_class) {
        o_u32("class_len", cn);
        if (class_buf) {
            cls[20] = 0;
            o_wstr("class_buf", cls);
        }
    }
    if (want_time) o_bool("time_written", ft.dwLowDateTime != 0xAAAAAAAAu || ft.dwHighDateTime != 0xAAAAAAAAu);
    oracle_case_end();
}

int main(void)
{
    oracle_begin("advapi32/RegEnumKeyEx_buffers");
    root = oracle_reg_root();
    HKEY k;
    RegCreateKeyExW(root, L"Charlie", 0, L"MyClass1", 0, KEY_ALL_ACCESS, NULL, &k, NULL);
    RegCloseKey(k);

    enum_case("name_cap_64", 0, 64, 0, 0, 0, 0, 0, NULL);
    enum_case("name_cap_8_exact_plus_nul", 0, 8, 0, 0, 0, 0, 0, NULL);
    enum_case("name_cap_7_no_nul_room", 0, 7, 0, 0, 0, 0, 0, NULL);
    enum_case("name_cap_3", 0, 3, 0, 0, 0, 0, 0, NULL);
    enum_case("name_cap_1", 0, 1, 0, 0, 0, 0, 0, NULL);
    enum_case("name_cap_0", 0, 0, 0, 0, 0, 0, 0, NULL);
    enum_case("class_cap_64", 0, 64, 0, 64, 1, 1, 0, NULL);
    enum_case("class_cap_9_exact_plus_nul", 0, 64, 0, 9, 1, 1, 0, NULL);
    enum_case("class_cap_8_no_nul_room", 0, 64, 0, 8, 1, 1, 0, NULL);
    enum_case("class_cap_2", 0, 64, 0, 2, 1, 1, 0, NULL);
    enum_case("class_cap_0", 0, 64, 0, 0, 1, 1, 0, NULL);
    enum_case("class_size_without_buffer", 0, 64, 0, 64, 0, 1, 0, NULL);
    enum_case("class_and_name_too_small", 0, 3, 0, 2, 1, 1, 0, NULL);
    enum_case("with_time", 0, 64, 0, 0, 0, 0, 1, NULL);
    enum_case("with_time_name_too_small", 0, 3, 0, 0, 0, 0, 1, NULL);
    {
        DWORD res = 5;
        enum_case("reserved_nonnull", 0, 64, 0, 0, 0, 0, 0, &res);
    }
    enum_case("index_1_past_end", 1, 64, 0, 0, 0, 0, 0, NULL);
    enum_case("index_past_end_small_buffer", 1, 1, 0, 0, 0, 0, 1, NULL);
    enum_case("index_max", 0xFFFFFFFFu, 64, 0, 0, 0, 0, 0, NULL);

    {   /* A key without a class: class length 0, buffer written? */
        RegCreateKeyExW(root, L"Delta", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL);
        RegCloseKey(k);
        enum_case("no_class_key_class_cap_64", 1, 64, 0, 64, 1, 1, 0, NULL);
        enum_case("no_class_key_class_cap_0", 1, 64, 0, 0, 1, 1, 0, NULL);
    }
    {   /* A 255-char name: buffer of 255 vs 256. */
        wchar_t n255[256], buf[300];
        wmemset(n255, L'z', 255); n255[255] = 0;
        RegCreateKeyExW(root, n255, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL);
        RegCloseKey(k);
        DWORD n = 255;
        LONG st = RegEnumKeyExW(root, 2, buf, &n, NULL, NULL, NULL, NULL);
        oracle_case("name_255_cap_255");
        o_status("ret", st);
        o_u32("name_len", n);
        oracle_case_end();
        n = 256;
        st = RegEnumKeyExW(root, 2, buf, &n, NULL, NULL, NULL, NULL);
        oracle_case("name_255_cap_256");
        o_status("ret", st);
        o_u32("name_len", n);
        oracle_case_end();
    }
    {   /* Legacy RegEnumKeyW buffer sizes are in characters too. */
        wchar_t buf[64];
        wmemset(buf, L'#', 63); buf[63] = 0;
        LONG st = RegEnumKeyW(root, 0, buf, 7);
        oracle_case("regenumkeyw_cap_7");
        o_status("ret", st);
        buf[20] = 0;
        o_wstr("buf", buf);
        oracle_case_end();
        st = RegEnumKeyW(root, 0, buf, 8);
        oracle_case("regenumkeyw_cap_8");
        o_status("ret", st);
        buf[20] = 0;
        o_wstr("buf", buf);
        oracle_case_end();
    }

    enum_case("name_null_buffer", 0, 64, 1, 0, 0, 0, 0, NULL); /* may fault: last */

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
