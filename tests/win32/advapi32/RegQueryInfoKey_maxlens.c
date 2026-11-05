/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: RegQueryInfoKeyW subkey count and maximum subkey name and
 * class lengths as subkeys come and go: does the maximum shrink when the
 * longest subkey is deleted or renamed, are volatile subkeys counted, are
 * surrogate pairs counted as two characters, and what a predefined key
 * reports (booleans only).
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regqueryinfokeyw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regrenamekey
 */
#include "oracle.h"

typedef LONG(WINAPI *rename_fn)(HKEY, LPCWSTR, LPCWSTR);
static HKEY root, g;

static void info(const char *name)
{
    DWORD nsub = 0xAAAA, maxsub = 0xAAAA, maxcls = 0xAAAA;
    LONG st = RegQueryInfoKeyW(g, NULL, NULL, NULL, &nsub, &maxsub, &maxcls, NULL, NULL, NULL, NULL, NULL);
    oracle_case(name);
    o_status("ret", st);
    o_u32("subkeys", nsub);
    o_u32("max_subkey_len", maxsub);
    o_u32("max_class_len", maxcls);
    oracle_case_end();
}

static void mk(const wchar_t *name, const wchar_t *cls, DWORD options)
{
    HKEY k;
    if (!RegCreateKeyExW(g, name, 0, (wchar_t *)cls, options, KEY_ALL_ACCESS, NULL, &k, NULL)) RegCloseKey(k);
}

int main(void)
{
    oracle_begin("advapi32/RegQueryInfoKey_maxlens");
    root = oracle_reg_root();
    rename_fn pRename = (rename_fn)(void (*)(void))GetProcAddress(GetModuleHandleW(L"advapi32.dll"), "RegRenameKey");
    RegCreateKeyExW(root, L"g", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &g, NULL);

    info("empty");
    mk(L"abc", L"c1", 0);
    info("one_3");
    mk(L"abcdefghij", L"class_ten_", 0);
    info("add_10");
    mk(L"twenty_chars_name_xx", L"cl", 0);
    info("add_20");
    RegDeleteKeyW(g, L"twenty_chars_name_xx");
    info("delete_longest");
    RegDeleteKeyW(g, L"abcdefghij");
    info("delete_second_longest");
    RegDeleteKeyW(g, L"abc");
    info("delete_all");
    mk(L"short", NULL, 0);
    info("after_delete_all_add_5");
    if (pRename) {
        mk(L"averyveryverylongname_30chars_", NULL, 0);
        info("add_30");
        pRename(g, L"averyveryverylongname_30chars_", L"tiny");
        info("rename_longest_to_short");
        pRename(g, L"tiny", L"a_name_that_is_40_characters_long_xxxxxx");
        info("rename_short_to_40");
    }
    mk(L"volatile_key_with_45_characters_in_its_name_", L"volclass_12", REG_OPTION_VOLATILE);
    info("add_volatile_45");
    mk(L"\xd83d\xde00\xd83d\xde00", NULL, 0);
    mk(L"éé", NULL, 0);
    info("add_surrogates");
    {
        wchar_t n255[256];
        wmemset(n255, L'x', 255); n255[255] = 0;
        mk(n255, NULL, 0);
        info("add_255");
        RegDeleteKeyW(g, n255);
        info("delete_255");
    }
    {   /* Grandchildren do not count. */
        HKEY k;
        RegCreateKeyExW(g, L"short\\grandchild_with_a_really_long_name_over_50_chars_xxxxxx", 0, L"grandclass_long_name_xxxx",
                        0, KEY_ALL_ACCESS, NULL, &k, NULL);
        RegCloseKey(k);
        info("grandchild_added");
    }
    {   /* Predefined key: plausible values only. */
        DWORD nsub = 0, maxsub = 0;
        LONG st = RegQueryInfoKeyW(HKEY_CURRENT_USER, NULL, NULL, NULL, &nsub, &maxsub, NULL, NULL, NULL, NULL, NULL, NULL);
        oracle_case("hkcu_plausible");
        o_status("ret", st);
        o_bool("has_subkeys", nsub > 0);
        o_bool("max_len_at_least_8", maxsub >= 8);
        oracle_case_end();
    }

    RegCloseKey(g);
    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
