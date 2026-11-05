/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: ntdll NtRenameKey (looked up with GetProcAddress) on test
 * keys: plain rename, case-only rename, a name that already exists, a name
 * with a backslash, an empty name, a too-long name, renaming a key with
 * subkeys and values (are they kept?), renaming through a read-only handle,
 * a deleted key, and a predefined-key handle. Each case lists the children
 * of the test key afterwards.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winternl/nf-winternl-ntrenamekey
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regrenamekey
 */
#include "oracle.h"
#include <winternl.h>

typedef LONG(NTAPI *NtRenameKey_fn)(HANDLE, UNICODE_STRING *);
static NtRenameKey_fn pNtRenameKey;
static HKEY root;

static void children(const char *field, HKEY k)
{
    char list[512] = "";
    for (DWORD i = 0;; i++) {
        wchar_t nm[300];
        DWORD n = 300;
        if (RegEnumKeyExW(k, i, nm, &n, NULL, NULL, NULL, NULL)) break;
        if (i) strcat(list, "|");
        if (strlen(list) + n > 400) { strcat(list, "..."); break; }
        strcat(list, oracle_utf8(nm));
    }
    o_str(field, list);
}

static HKEY mk(const wchar_t *path)
{
    HKEY k = NULL;
    RegCreateKeyExW(root, path, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL);
    return k;
}

static void rename_case(const char *name, HKEY k, const wchar_t *buf, USHORT len_bytes)
{
    UNICODE_STRING us;
    us.Buffer = (wchar_t *)buf;
    us.Length = len_bytes;
    us.MaximumLength = len_bytes;
    LONG st = pNtRenameKey(k, &us);
    oracle_case(name);
    o_hex32("status", (DWORD)st);
    children("children", root);
    oracle_case_end();
}

#define WL(s) (USHORT)(wcslen(s) * sizeof(wchar_t))

int main(void)
{
    oracle_begin("advapi32/NtKey_rename");
    pNtRenameKey = (NtRenameKey_fn)(void (*)(void))GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtRenameKey");
    root = oracle_reg_root();
    oracle_case("available");
    o_bool("ntrenamekey", pNtRenameKey != NULL);
    oracle_case_end();
    if (!pNtRenameKey) {
        RegCloseKey(root);
        oracle_reg_cleanup();
        return oracle_end();
    }

    HKEY a = mk(L"alpha");
    rename_case("plain", a, L"beta", WL(L"beta"));
    rename_case("case_only", a, L"BETA", WL(L"BETA"));
    rename_case("same_name", a, L"BETA", WL(L"BETA"));
    HKEY g = mk(L"gamma");
    rename_case("onto_existing", a, L"gamma", WL(L"gamma"));
    rename_case("onto_existing_other_case", a, L"GAMMA", WL(L"GAMMA"));
    rename_case("with_backslash", a, L"x\\y", WL(L"x\\y"));
    rename_case("empty", a, L"", 0);
    rename_case("odd_length", a, L"delta", 5);
    {
        wchar_t longname[300];
        wmemset(longname, L'L', 299);
        longname[299] = 0;
        rename_case("name_256", a, longname, 256 * sizeof(wchar_t));
        rename_case("name_257", a, longname, 257 * sizeof(wchar_t));
        RegCloseKey(a);
        a = NULL;
        RegOpenKeyExW(root, L"BETA", 0, KEY_ALL_ACCESS, &a);
        if (!a) {
            wmemset(longname, L'L', 256);
            longname[256] = 0;
            RegOpenKeyExW(root, longname, 0, KEY_ALL_ACCESS, &a);
        }
        if (a) rename_case("back_to_short", a, L"beta", WL(L"beta"));
    }
    {   /* Subkeys and values move with the key. */
        HKEY t = mk(L"tree\\sub1\\deep"), v;
        RegCloseKey(t);
        t = mk(L"tree");
        DWORD val = 7;
        RegSetValueExW(t, L"val", 0, REG_DWORD, (const BYTE *)&val, sizeof val);
        rename_case("with_children", t, L"forest", WL(L"forest"));
        oracle_case("children_kept");
        children("children_of_forest", t);
        LONG o = RegOpenKeyExW(root, L"forest\\sub1\\deep", 0, KEY_READ, &v);
        o_status("open_deep_by_new_path", o);
        if (!o) RegCloseKey(v);
        DWORD got = 0, sz = sizeof got;
        o_status("value", RegQueryValueExW(t, L"val", NULL, NULL, (BYTE *)&got, &sz));
        o_u32("value_data", got);
        oracle_case_end();
        RegCloseKey(t);
    }
    {
        HKEY ro;
        RegOpenKeyExW(root, L"gamma", 0, KEY_READ, &ro);
        rename_case("read_only_handle", ro, L"epsilon", WL(L"epsilon"));
        RegCloseKey(ro);
        RegOpenKeyExW(root, L"gamma", 0, KEY_WRITE, &ro);
        rename_case("write_handle", ro, L"epsilon", WL(L"epsilon"));
        RegCloseKey(ro);
    }
    {
        HKEY d = mk(L"doomed");
        RegDeleteKeyW(root, L"doomed");
        rename_case("deleted_key", d, L"zombie", WL(L"zombie"));
        RegCloseKey(d);
    }
    rename_case("predefined_hkcu", HKEY_CURRENT_USER, L"nope", WL(L"nope"));
    rename_case("null_handle", NULL, L"nope", WL(L"nope"));

    if (a) RegCloseKey(a);
    RegCloseKey(g);
    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
