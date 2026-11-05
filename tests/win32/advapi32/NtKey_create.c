/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: ntdll NtCreateKey / NtOpenKey / NtOpenKeyEx / NtDeleteKey
 * (looked up with GetProcAddress) relative to a test key: counted names that
 * the Win32 API cannot express (embedded NUL, trailing backslash, odd byte
 * lengths, Length > MaximumLength), names with a leading backslash, empty
 * names, OBJ_CASE_INSENSITIVE missing, dispositions and NTSTATUS codes.
 * Absolute paths are only used under \Registry\Machine\SOFTWARE.
 *
 * Docs: https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-zwcreatekey
 *       https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-zwopenkey
 *       https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-zwopenkeyex
 *       https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-zwdeletekey
 *       https://learn.microsoft.com/en-us/windows/win32/api/ntdef/nf-ntdef-initializeobjectattributes
 */
#include "oracle.h"
#include <winternl.h>

typedef LONG(NTAPI *NtCreateKey_fn)(HANDLE *, ACCESS_MASK, OBJECT_ATTRIBUTES *, ULONG, UNICODE_STRING *, ULONG, ULONG *);
typedef LONG(NTAPI *NtOpenKey_fn)(HANDLE *, ACCESS_MASK, OBJECT_ATTRIBUTES *);
typedef LONG(NTAPI *NtOpenKeyEx_fn)(HANDLE *, ACCESS_MASK, OBJECT_ATTRIBUTES *, ULONG);
typedef LONG(NTAPI *NtDeleteKey_fn)(HANDLE);
typedef LONG(NTAPI *NtClose_fn)(HANDLE);

static NtCreateKey_fn pNtCreateKey;
static NtOpenKey_fn pNtOpenKey;
static NtOpenKeyEx_fn pNtOpenKeyEx;
static NtDeleteKey_fn pNtDeleteKey;
static NtClose_fn pNtClose;
static HKEY root;

static void init_oa(OBJECT_ATTRIBUTES *oa, UNICODE_STRING *us, HANDLE dir, ULONG attr)
{
    memset(oa, 0, sizeof *oa);
    oa->Length = sizeof *oa;
    oa->RootDirectory = dir;
    oa->ObjectName = us;
    oa->Attributes = attr;
}

static void set_us(UNICODE_STRING *us, const wchar_t *buf, USHORT len_bytes, USHORT max_bytes)
{
    us->Buffer = (wchar_t *)buf;
    us->Length = len_bytes;
    us->MaximumLength = max_bytes;
}

static void children(const char *field)
{
    char list[512] = "";
    for (DWORD i = 0;; i++) {
        wchar_t nm[64];
        DWORD n = 64;
        if (RegEnumKeyExW(root, i, nm, &n, NULL, NULL, NULL, NULL)) break;
        if (i) strcat(list, "|");
        for (DWORD j = 0; j < n; j++) {
            char t[12];
            if (nm[j] >= 0x20 && nm[j] < 0x7f) { t[0] = (char)nm[j]; t[1] = 0; }
            else snprintf(t, sizeof t, "\\u%04x", nm[j]);
            strcat(list, t);
        }
    }
    o_str(field, list);
}

static void create_case(const char *name, HANDLE dir, const wchar_t *buf, USHORT len, USHORT max, ULONG attr,
                        ULONG options)
{
    UNICODE_STRING us;
    OBJECT_ATTRIBUTES oa;
    HANDLE h = NULL;
    ULONG disp = 0xAAAA;
    set_us(&us, buf, len, max);
    init_oa(&oa, &us, dir, attr);
    LONG st = pNtCreateKey(&h, KEY_ALL_ACCESS, &oa, 0, NULL, options, &disp);
    oracle_case(name);
    o_hex32("status", (DWORD)st);
    o_u32("disposition", disp);
    if (st >= 0) pNtClose(h);
    children("children");
    oracle_case_end();
}

static void open_case(const char *name, HANDLE dir, const wchar_t *buf, USHORT len, USHORT max, ULONG attr)
{
    UNICODE_STRING us;
    OBJECT_ATTRIBUTES oa;
    HANDLE h = NULL;
    set_us(&us, buf, len, max);
    init_oa(&oa, &us, dir, attr);
    LONG st = pNtOpenKey(&h, KEY_READ, &oa);
    oracle_case(name);
    o_hex32("status", (DWORD)st);
    if (st >= 0) pNtClose(h);
    oracle_case_end();
}

#define WL(s) (USHORT)(sizeof(s) - sizeof(wchar_t))

int main(void)
{
    oracle_begin("advapi32/NtKey_create");
    HMODULE nt = GetModuleHandleW(L"ntdll.dll");
    pNtCreateKey = (NtCreateKey_fn)(void (*)(void))GetProcAddress(nt, "NtCreateKey");
    pNtOpenKey = (NtOpenKey_fn)(void (*)(void))GetProcAddress(nt, "NtOpenKey");
    pNtOpenKeyEx = (NtOpenKeyEx_fn)(void (*)(void))GetProcAddress(nt, "NtOpenKeyEx");
    pNtDeleteKey = (NtDeleteKey_fn)(void (*)(void))GetProcAddress(nt, "NtDeleteKey");
    pNtClose = (NtClose_fn)(void (*)(void))GetProcAddress(nt, "NtClose");
    root = oracle_reg_root();
    const ULONG CI = OBJ_CASE_INSENSITIVE;

    static const wchar_t plain[] = L"plain";
    create_case("create_plain", root, plain, WL(plain), sizeof plain, CI, 0);
    create_case("create_plain_again", root, plain, WL(plain), sizeof plain, CI, 0);
    create_case("create_plain_upper_no_ci", root, L"PLAIN", 10, 12, 0, 0);
    static const wchar_t emb[] = L"ab\0cd";
    create_case("create_embedded_nul", root, emb, 10, 12, CI, 0);
    static const wchar_t tb[] = L"trail\\";
    create_case("create_trailing_backslash", root, tb, WL(tb), sizeof tb, CI, 0);
    static const wchar_t lb[] = L"\\lead";
    create_case("create_leading_backslash_relative", root, lb, WL(lb), sizeof lb, CI, 0);
    static const wchar_t ml[] = L"m1\\m2";
    create_case("create_multi_level_missing_parent", root, ml, WL(ml), sizeof ml, CI, 0);
    static const wchar_t dbl[] = L"m1\\\\m2";
    create_case("create_double_backslash", root, dbl, WL(dbl), sizeof dbl, CI, 0);
    create_case("create_odd_length", root, L"oddname", 7, 16, CI, 0);
    create_case("create_length_gt_max", root, L"lenmax", 12, 4, CI, 0);
    create_case("create_empty_name", root, L"", 0, 2, CI, 0);
    create_case("create_partial_length", root, L"partial_xyz", 14, 24, CI, 0);
    create_case("create_volatile", root, L"vol", 6, 8, CI, REG_OPTION_VOLATILE);
    create_case("create_bad_options", root, L"badopt", 12, 14, CI, 0x100);
    create_case("create_null_root_relative_name", NULL, L"norootname", 20, 22, CI, 0);
    create_case("create_absolute_with_root", root, L"\\Registry\\Machine", 34, 36, CI, 0);

    open_case("open_plain", root, L"plain", 10, 12, CI);
    open_case("open_upper_no_ci", root, L"PLAIN", 10, 12, 0);
    open_case("open_embedded_nul", root, emb, 10, 12, CI);
    open_case("open_ab_only", root, L"ab", 4, 6, CI);
    open_case("open_trailing_backslash", root, L"plain\\", 12, 14, CI);
    open_case("open_empty_name", root, L"", 0, 2, CI);
    open_case("open_missing", root, L"missing", 14, 16, CI);
    open_case("open_missing_parent", root, L"missing\\x", 18, 20, CI);
    open_case("open_absolute_hklm_software", NULL, L"\\Registry\\Machine\\SOFTWARE", 52, 54, CI);
    open_case("open_absolute_lowercase", NULL, L"\\registry\\machine\\software", 52, 54, CI);
    open_case("open_absolute_lowercase_no_ci", NULL, L"\\registry\\machine\\software", 52, 54, 0);
    open_case("open_registry_root", NULL, L"\\Registry", 18, 20, CI);
    open_case("open_absolute_trailing_backslash", NULL, L"\\Registry\\Machine\\", 36, 38, CI);
    open_case("open_relative_without_root", NULL, L"Registry\\Machine", 32, 34, CI);
    open_case("open_object_dir_not_key", NULL, L"\\BaseNamedObjects", 34, 36, CI);
    {
        UNICODE_STRING us;
        OBJECT_ATTRIBUTES oa;
        HANDLE h = NULL;
        set_us(&us, L"plain", 10, 12);
        init_oa(&oa, &us, root, CI);
        LONG st = pNtOpenKeyEx ? pNtOpenKeyEx(&h, KEY_READ, &oa, 0) : -1;
        oracle_case("openex_plain");
        o_hex32("status", (DWORD)st);
        oracle_case_end();
        if (st >= 0) pNtClose(h);
        st = pNtOpenKeyEx ? pNtOpenKeyEx(&h, KEY_READ, &oa, REG_OPTION_BACKUP_RESTORE) : -1;
        oracle_case("openex_backup_restore_no_priv");
        o_hex32("status", (DWORD)st);
        oracle_case_end();
        if (st >= 0) pNtClose(h);
        st = pNtOpenKeyEx ? pNtOpenKeyEx(&h, KEY_READ, &oa, 0x100) : -1;
        oracle_case("openex_bad_options");
        o_hex32("status", (DWORD)st);
        oracle_case_end();
        if (st >= 0) pNtClose(h);
    }
    {   /* NtDeleteKey on a key with children, a leaf, twice, and a read-only handle. */
        HANDLE h = NULL;
        UNICODE_STRING us;
        OBJECT_ATTRIBUTES oa;
        HKEY t;
        RegCreateKeyExW(root, L"del\\child", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &t, NULL);
        RegCloseKey(t);
        set_us(&us, L"del", 6, 8);
        init_oa(&oa, &us, root, CI);
        pNtOpenKey(&h, KEY_ALL_ACCESS, &oa);
        LONG s1 = pNtDeleteKey(h);
        pNtClose(h);
        set_us(&us, L"del\\child", 18, 20);
        pNtOpenKey(&h, KEY_ALL_ACCESS, &oa);
        LONG s2 = pNtDeleteKey(h);
        LONG s3 = pNtDeleteKey(h);
        pNtClose(h);
        set_us(&us, L"del", 6, 8);
        pNtOpenKey(&h, KEY_READ, &oa);
        LONG s4 = pNtDeleteKey(h);
        pNtClose(h);
        pNtOpenKey(&h, DELETE, &oa);
        LONG s5 = pNtDeleteKey(h);
        pNtClose(h);
        oracle_case("nt_delete");
        o_hex32("with_children", (DWORD)s1);
        o_hex32("leaf", (DWORD)s2);
        o_hex32("leaf_twice", (DWORD)s3);
        o_hex32("read_only_handle", (DWORD)s4);
        o_hex32("delete_access_only", (DWORD)s5);
        oracle_case_end();
    }

    {   /* Win32 cannot delete a name with an embedded NUL: remove it here. */
        UNICODE_STRING us;
        OBJECT_ATTRIBUTES oa;
        HANDLE h = NULL;
        set_us(&us, emb, 10, 12);
        init_oa(&oa, &us, root, CI);
        if (pNtOpenKey(&h, DELETE, &oa) >= 0) {
            pNtDeleteKey(h);
            pNtClose(h);
        }
    }
    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
