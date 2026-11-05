/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: enumerating subkeys while the key changes. Index-based
 * enumeration sees the current sorted list, so deleting or adding keys
 * between RegEnumKeyExW calls shifts what each index returns. Covers
 * deleting each key as it is enumerated (forward and backward), adding keys
 * that sort before and after the current index, enumeration through two
 * handles to the same key, and through a handle opened before the changes.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regenumkeyexw
 *       https://learn.microsoft.com/en-us/windows/win32/sysinfo/enumerating-registry-subkeys
 */
#include "oracle.h"

static HKEY root;

static void fill(HKEY g, int count)
{
    for (int i = 0; i < count; i++) {
        wchar_t nm[8] = {L'k', (wchar_t)(L'a' + i), 0};
        HKEY k;
        RegCreateKeyExW(g, nm, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL);
        RegCloseKey(k);
    }
}

static void cat(char *out, const char *s)
{
    if (out[0]) strcat(out, ",");
    strcat(out, s);
}

static HKEY fresh(const wchar_t *name, int count)
{
    HKEY g;
    RegDeleteTreeW(root, name);
    RegDeleteKeyW(root, name);
    RegCreateKeyExW(root, name, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &g, NULL);
    fill(g, count);
    return g;
}

static void remaining(HKEY g, char *out)
{
    out[0] = 0;
    for (DWORD i = 0;; i++) {
        wchar_t nm[16];
        DWORD n = 16;
        if (RegEnumKeyExW(g, i, nm, &n, NULL, NULL, NULL, NULL)) break;
        cat(out, oracle_utf8(nm));
    }
}

int main(void)
{
    oracle_begin("advapi32/RegEnumKeyEx_modify");
    root = oracle_reg_root();
    char seen[512], left[512];

    {   /* Delete each key as it is returned, incrementing the index. */
        HKEY g = fresh(L"g1", 6);
        seen[0] = 0;
        for (DWORD i = 0; i < 20; i++) {
            wchar_t nm[16];
            DWORD n = 16;
            if (RegEnumKeyExW(g, i, nm, &n, NULL, NULL, NULL, NULL)) break;
            cat(seen, oracle_utf8(nm));
            RegDeleteKeyW(g, nm);
        }
        remaining(g, left);
        oracle_case("delete_while_enumerating_forward");
        o_str("seen", seen);
        o_str("remaining", left);
        oracle_case_end();
        RegCloseKey(g);
    }
    {   /* Always enumerate index 0 and delete it (the documented-safe pattern). */
        HKEY g = fresh(L"g2", 6);
        seen[0] = 0;
        for (int guard = 0; guard < 20; guard++) {
            wchar_t nm[16];
            DWORD n = 16;
            if (RegEnumKeyExW(g, 0, nm, &n, NULL, NULL, NULL, NULL)) break;
            cat(seen, oracle_utf8(nm));
            RegDeleteKeyW(g, nm);
        }
        remaining(g, left);
        oracle_case("delete_index_zero_repeatedly");
        o_str("seen", seen);
        o_str("remaining", left);
        oracle_case_end();
        RegCloseKey(g);
    }
    {   /* Backward enumeration while deleting. */
        HKEY g = fresh(L"g3", 6);
        DWORD nsub = 0;
        RegQueryInfoKeyW(g, NULL, NULL, NULL, &nsub, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        seen[0] = 0;
        for (DWORD i = nsub; i-- > 0;) {
            wchar_t nm[16];
            DWORD n = 16;
            if (RegEnumKeyExW(g, i, nm, &n, NULL, NULL, NULL, NULL)) break;
            cat(seen, oracle_utf8(nm));
            RegDeleteKeyW(g, nm);
        }
        remaining(g, left);
        oracle_case("delete_while_enumerating_backward");
        o_str("seen", seen);
        o_str("remaining", left);
        oracle_case_end();
        RegCloseKey(g);
    }
    {   /* Add a key that sorts before the current position, and one after. */
        HKEY g = fresh(L"g4", 4); /* ka kb kc kd */
        seen[0] = 0;
        for (DWORD i = 0; i < 20; i++) {
            wchar_t nm[16];
            DWORD n = 16;
            LONG st = RegEnumKeyExW(g, i, nm, &n, NULL, NULL, NULL, NULL);
            if (st) break;
            cat(seen, oracle_utf8(nm));
            if (i == 1) {
                HKEY k;
                RegCreateKeyExW(g, L"a_first", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL);
                RegCloseKey(k);
                RegCreateKeyExW(g, L"z_last", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL);
                RegCloseKey(k);
            }
        }
        oracle_case("add_while_enumerating");
        o_str("seen", seen);
        oracle_case_end();
        RegCloseKey(g);
    }
    {   /* A second handle, opened before the changes, sees them too. */
        HKEY g = fresh(L"g5", 3), h;
        RegOpenKeyExW(root, L"g5", 0, KEY_READ, &h);
        RegDeleteKeyW(g, L"ka");
        HKEY k;
        RegCreateKeyExW(g, L"new", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL);
        RegCloseKey(k);
        remaining(h, left);
        DWORD nsub = 0;
        RegQueryInfoKeyW(h, NULL, NULL, NULL, &nsub, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        oracle_case("second_handle_sees_changes");
        o_str("keys", left);
        o_u32("subkeys", nsub);
        oracle_case_end();
        RegCloseKey(h);
        RegCloseKey(g);
    }
    {   /* Query-info count vs enumeration after deleting the last key. */
        HKEY g = fresh(L"g6", 2);
        RegDeleteKeyW(g, L"kb");
        wchar_t nm[16];
        DWORD n = 16;
        LONG st1 = RegEnumKeyExW(g, 1, nm, &n, NULL, NULL, NULL, NULL);
        n = 16;
        LONG st0 = RegEnumKeyExW(g, 0, nm, &n, NULL, NULL, NULL, NULL);
        oracle_case("index_after_delete_last");
        o_status("index1", st1);
        o_status("index0", st0);
        o_wstr("index0_name", st0 ? L"" : nm);
        oracle_case_end();
        RegCloseKey(g);
    }

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
