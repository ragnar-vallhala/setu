/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: many handles to registry keys. 2000 handles to one key are
 * all distinct and all work; closing them in any order works; a key that is
 * deleted while 2000 handles are open fails with ERROR_KEY_DELETED through
 * every one of them; a subkey created through one handle is visible through
 * another; a handle stays usable while the same key is opened and closed
 * again many times. Only counts and booleans are printed.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regopenkeyexw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regclosekey
 *       https://learn.microsoft.com/en-us/windows/win32/sysinfo/registry-key-security-and-access-rights
 */
#include "oracle.h"

#define N 2000

static HKEY h[N];

int main(void)
{
    oracle_begin("advapi32/RegKeyHandles");
    HKEY root = oracle_reg_root(), k;
    RegCreateKeyExW(root, L"many", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL);
    RegCloseKey(k);

    {
        DWORD opened = 0, distinct = 1, working = 0;
        for (int i = 0; i < N; i++)
            if (!RegOpenKeyExW(root, L"many", 0, KEY_ALL_ACCESS, &h[i])) opened++;
        for (int i = 1; i < N && distinct; i++)
            if (h[i] == h[i - 1] || h[i] == h[0]) distinct = 0;
        for (int i = 0; i < N; i++) {
            DWORD n = 99;
            if (!RegQueryInfoKeyW(h[i], NULL, NULL, NULL, &n, NULL, NULL, NULL, NULL, NULL, NULL, NULL) && n == 0)
                working++;
        }
        oracle_case("open_many");
        o_u32("opened", opened);
        o_bool("distinct", distinct);
        o_u32("working", working);
        oracle_case_end();
    }
    {   /* A subkey created through one handle is seen through all the others. */
        HKEY c;
        RegCreateKeyExW(h[N / 2], L"child", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &c, NULL);
        RegCloseKey(c);
        DWORD seeing = 0;
        for (int i = 0; i < N; i++) {
            DWORD n = 0;
            if (!RegQueryInfoKeyW(h[i], NULL, NULL, NULL, &n, NULL, NULL, NULL, NULL, NULL, NULL, NULL) && n == 1)
                seeing++;
        }
        RegDeleteKeyW(h[0], L"child");
        oracle_case("shared_view");
        o_u32("handles_seeing_child", seeing);
        oracle_case_end();
    }
    {   /* Close every other handle, then the rest in reverse. */
        DWORD closed = 0;
        for (int i = 0; i < N; i += 2)
            if (!RegCloseKey(h[i])) closed++;
        DWORD still = 0;
        for (int i = 1; i < N; i += 2) {
            DWORD n = 99;
            if (!RegQueryInfoKeyW(h[i], NULL, NULL, NULL, &n, NULL, NULL, NULL, NULL, NULL, NULL, NULL)) still++;
        }
        for (int i = N - 1; i >= 1; i -= 2)
            if (!RegCloseKey(h[i])) closed++;
        oracle_case("close_interleaved");
        o_u32("closed", closed);
        o_u32("odd_handles_working_midway", still);
        oracle_case_end();
    }
    {   /* Delete the key while N handles are open. */
        for (int i = 0; i < N; i++) RegOpenKeyExW(root, L"many", 0, KEY_ALL_ACCESS, &h[i]);
        LONG del = RegDeleteKeyW(root, L"many");
        DWORD deleted_err = 0, other = 0;
        for (int i = 0; i < N; i++) {
            DWORD n;
            LONG st = RegQueryInfoKeyW(h[i], NULL, NULL, NULL, &n, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
            if (st == ERROR_KEY_DELETED) deleted_err++;
            else other++;
        }
        DWORD closed = 0;
        for (int i = 0; i < N; i++)
            if (!RegCloseKey(h[i])) closed++;
        oracle_case("delete_with_open_handles");
        o_status("delete", del);
        o_u32("key_deleted_errors", deleted_err);
        o_u32("other_results", other);
        o_u32("closed", closed);
        oracle_case_end();
    }
    {   /* Open/close churn does not disturb a long-lived handle. */
        HKEY keep;
        RegCreateKeyExW(root, L"churn", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &keep, NULL);
        DWORD ok = 0;
        for (int i = 0; i < 5000; i++) {
            HKEY t;
            if (!RegOpenKeyExW(root, L"churn", 0, KEY_READ, &t)) {
                RegCloseKey(t);
                ok++;
            }
        }
        DWORD val = 1;
        oracle_case("churn");
        o_u32("open_close_ok", ok);
        o_status("keep_set_value", RegSetValueExW(keep, L"v", 0, REG_DWORD, (const BYTE *)&val, sizeof val));
        oracle_case_end();
        RegCloseKey(keep);
    }

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
