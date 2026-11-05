/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: key class strings: set at creation by RegCreateKeyExW only
 * (re-creating keeps the first class), empty class vs no class, long
 * classes, classes with non-ASCII characters, and how RegQueryInfoKeyW and
 * RegEnumKeyExW report them, including the max class length of a parent.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regqueryinfokeyw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regcreatekeyexw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regenumkeyexw
 */
#include "oracle.h"

static HKEY root;
static wchar_t big[40000];

static void class_case(const char *name, const wchar_t *sub, const wchar_t *cls_at_create)
{
    HKEY k = NULL;
    DWORD disp = 0;
    LONG cst = RegCreateKeyExW(root, sub, 0, (wchar_t *)cls_at_create, 0, KEY_ALL_ACCESS, NULL, &k, &disp);
    static wchar_t cls[40000];
    DWORD cn = 40000;
    oracle_case(name);
    o_status("create", cst);
    o_u32("disposition", disp);
    if (!cst) {
        LONG st = RegQueryInfoKeyW(k, cls, &cn, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        o_status("query", st);
        o_u32("class_len", cn);
        if (!st && cn < 60) o_wstr("class", cls);
        RegCloseKey(k);
    }
    oracle_case_end();
}

int main(void)
{
    oracle_begin("advapi32/RegQueryInfoKey_class");
    root = oracle_reg_root();

    class_case("no_class", L"a", NULL);
    class_case("empty_class", L"b", L"");
    class_case("simple_class", L"c", L"Simple");
    class_case("recreate_other_class", L"c", L"Other");
    class_case("recreate_null_class", L"c", NULL);
    class_case("recreate_no_class_key_with_class", L"a", L"Late");
    class_case("class_with_spaces_backslash", L"d", L" a\\b c ");
    class_case("class_non_ascii", L"e", L"éα中\xd83d\xde00");
    wmemset(big, L'c', 255); big[255] = 0;
    class_case("class_255", L"f", big);
    wmemset(big, L'c', 256); big[256] = 0;
    class_case("class_256", L"g", big);
    wmemset(big, L'c', 32767); big[32767] = 0;
    class_case("class_32767", L"h", big);

    {   /* Parent's max class length over its subkeys. */
        DWORD maxcls = 0, maxsub = 0, nsub = 0;
        LONG st = RegQueryInfoKeyW(root, NULL, NULL, NULL, &nsub, &maxsub, &maxcls, NULL, NULL, NULL, NULL, NULL);
        oracle_case("parent_max_class_len");
        o_status("ret", st);
        o_u32("subkeys", nsub);
        o_u32("max_class_len", maxcls);
        oracle_case_end();
        RegDeleteKeyW(root, L"h");
        RegDeleteKeyW(root, L"g");
        st = RegQueryInfoKeyW(root, NULL, NULL, NULL, &nsub, &maxsub, &maxcls, NULL, NULL, NULL, NULL, NULL);
        oracle_case("parent_max_class_len_after_delete");
        o_status("ret", st);
        o_u32("subkeys", nsub);
        o_u32("max_class_len", maxcls);
        oracle_case_end();
    }
    {   /* RegEnumKeyExW class for each kind. */
        for (DWORD i = 0; i < 6; i++) {
            wchar_t nm[64], cls[300];
            DWORD n = 64, cn = 300;
            char cname[32];
            LONG st = RegEnumKeyExW(root, i, nm, &n, NULL, cls, &cn, NULL);
            snprintf(cname, sizeof cname, "enum_class_%lu", (unsigned long)i);
            oracle_case(cname);
            o_status("ret", st);
            if (!st) {
                o_wstr("name", nm);
                o_u32("class_len", cn);
                if (cn < 60) o_wstr("class", cls);
            }
            oracle_case_end();
        }
    }
    {   /* Class buffer sizes in RegQueryInfoKeyW on "c" (class "Simple"). */
        static const DWORD caps[] = {0, 1, 5, 6, 7, 100};
        HKEY k;
        RegOpenKeyExW(root, L"c", 0, KEY_READ, &k);
        for (size_t i = 0; i < sizeof caps / sizeof *caps; i++) {
            wchar_t cls[128];
            DWORD cn = caps[i];
            char cname[32];
            wmemset(cls, L'#', 127); cls[127] = 0;
            LONG st = RegQueryInfoKeyW(k, cls, &cn, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
            snprintf(cname, sizeof cname, "query_cap_%lu", (unsigned long)caps[i]);
            oracle_case(cname);
            o_status("ret", st);
            o_u32("class_len", cn);
            cls[12] = 0;
            o_wstr("buf", cls);
            oracle_case_end();
        }
        RegCloseKey(k);
    }

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
