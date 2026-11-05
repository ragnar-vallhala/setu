/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: key last-write times, as relations only (never raw times):
 * which operations update a key's time and its parent's time (creating,
 * opening, deleting subkeys, setting and deleting values, RegFlushKey,
 * renaming, enumeration), whether RegEnumKeyExW and RegQueryInfoKeyW agree,
 * and whether the time lies between GetSystemTimeAsFileTime readings taken
 * around the change. Steps are separated by sleeps longer than the clock
 * granularity.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regenumkeyexw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regqueryinfokeyw
 *       https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-getsystemtimeasfiletime
 */
#include "oracle.h"

typedef LONG(WINAPI *rename_fn)(HKEY, LPCWSTR, LPCWSTR);
static HKEY root, parent;

static ULONGLONG ft64(FILETIME f) { return ((ULONGLONG)f.dwHighDateTime << 32) | f.dwLowDateTime; }

static ULONGLONG info_time(HKEY k)
{
    FILETIME f = {0, 0};
    RegQueryInfoKeyW(k, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &f);
    return ft64(f);
}

static ULONGLONG child_time(const wchar_t *path)
{
    HKEY k;
    ULONGLONG t = 0;
    if (!RegOpenKeyExW(parent, path, 0, KEY_READ, &k)) {
        t = info_time(k);
        RegCloseKey(k);
    }
    return t;
}

static const char *rel(ULONGLONG after, ULONGLONG before)
{
    return after > before ? "later" : after == before ? "same" : "earlier";
}

static void pause_(void) { Sleep(60); }

#define STEP(name, ...)                                            \
    do {                                                          \
        ULONGLONG p0 = info_time(parent), c0 = child_time(L"c");  \
        pause_();                                                 \
        __VA_ARGS__;                                                 \
        pause_();                                                 \
        ULONGLONG p1 = info_time(parent), c1 = child_time(L"c");  \
        oracle_case(name);                                        \
        o_str("parent", rel(p1, p0));                             \
        o_str("child", rel(c1, c0));                              \
        oracle_case_end();                                        \
    } while (0)

int main(void)
{
    oracle_begin("advapi32/RegEnumKeyEx_lastwrite");
    root = oracle_reg_root();
    rename_fn pRename = (rename_fn)(void (*)(void))GetProcAddress(GetModuleHandleW(L"advapi32.dll"), "RegRenameKey");

    FILETIME before, after;
    GetSystemTimeAsFileTime(&before);
    pause_();
    RegCreateKeyExW(root, L"p", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &parent, NULL);
    HKEY c;
    RegCreateKeyExW(parent, L"c", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &c, NULL);
    pause_();
    GetSystemTimeAsFileTime(&after);
    {
        ULONGLONG t = child_time(L"c");
        oracle_case("creation_time_in_window");
        o_bool("nonzero", t != 0);
        o_bool("after_start", t >= ft64(before));
        o_bool("before_end", t <= ft64(after));
        oracle_case_end();
    }
    {   /* RegEnumKeyExW's time equals RegQueryInfoKeyW's time on the subkey. */
        wchar_t nm[16];
        DWORD n = 16;
        FILETIME f;
        RegEnumKeyExW(parent, 0, nm, &n, NULL, NULL, NULL, &f);
        oracle_case("enum_time_equals_query_info");
        o_bool("equal", ft64(f) == child_time(L"c"));
        oracle_case_end();
    }
    DWORD v = 1;
    STEP("set_value_on_child", RegSetValueExW(c, L"v", 0, REG_DWORD, (const BYTE *)&v, sizeof v));
    STEP("set_same_value_again", RegSetValueExW(c, L"v", 0, REG_DWORD, (const BYTE *)&v, sizeof v));
    STEP("query_value", { DWORD d, s = 4; RegQueryValueExW(c, L"v", NULL, NULL, (BYTE *)&d, &s); });
    STEP("delete_value", RegDeleteValueW(c, L"v"));
    STEP("delete_missing_value", RegDeleteValueW(c, L"v"));
    STEP("create_grandchild", { HKEY g; RegCreateKeyExW(c, L"g", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &g, NULL); RegCloseKey(g); });
    STEP("open_existing_grandchild_with_create",
         { HKEY g; RegCreateKeyExW(c, L"g", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &g, NULL); RegCloseKey(g); });
    STEP("set_value_on_grandchild", {
        HKEY g;
        RegOpenKeyExW(c, L"g", 0, KEY_ALL_ACCESS, &g);
        RegSetValueExW(g, L"x", 0, REG_DWORD, (const BYTE *)&v, sizeof v);
        RegCloseKey(g);
    });
    STEP("delete_grandchild", RegDeleteKeyW(c, L"g"));
    STEP("flush_child", RegFlushKey(c));
    STEP("enumerate_child", { wchar_t nm[16]; DWORD n = 16; RegEnumKeyExW(parent, 0, nm, &n, NULL, NULL, NULL, NULL); });
    STEP("create_sibling", { HKEY s; RegCreateKeyExW(parent, L"s", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &s, NULL); RegCloseKey(s); });
    STEP("delete_sibling", RegDeleteKeyW(parent, L"s"));
    STEP("set_value_on_parent", RegSetValueExW(parent, L"pv", 0, REG_DWORD, (const BYTE *)&v, sizeof v));
    STEP("create_volatile_grandchild",
         { HKEY g; RegCreateKeyExW(c, L"vg", 0, NULL, REG_OPTION_VOLATILE, KEY_ALL_ACCESS, NULL, &g, NULL); RegCloseKey(g); });
    if (pRename) STEP("rename_grandchild", pRename(c, L"vg", L"vg2"));
    STEP("delete_tree_of_child_contents", RegDeleteTreeW(c, NULL));
    if (pRename) {
        HKEY s;
        RegCreateKeyExW(parent, L"r1", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &s, NULL);
        ULONGLONG t0 = info_time(s);
        pause_();
        ULONGLONG p0 = info_time(parent);
        pRename(parent, L"r1", L"r2");
        pause_();
        oracle_case("rename_key_own_time");
        o_str("renamed", rel(info_time(s), t0));
        o_str("parent", rel(info_time(parent), p0));
        oracle_case_end();
        RegCloseKey(s);
    }

    RegCloseKey(c);
    RegCloseKey(parent);
    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
