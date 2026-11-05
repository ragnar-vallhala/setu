/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: the 16-bit-compatible RegCreateKeyA/W and RegOpenKeyA/W:
 * NULL and empty subkeys (does the call return the same key or a new handle?),
 * existing keys, missing keys, invalid parents, and the access the returned
 * handles have.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regcreatekeya
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regcreatekeyw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regopenkeya
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regopenkeyw
 */
#include "oracle.h"

static HKEY root;

/* Reports the call's result, whether a key came back, whether it is the same
 * handle as the parent, and whether it can create a subkey and set a value. */
static void result_case(const char *name, LONG st, HKEY parent, HKEY k)
{
    oracle_case(name);
    o_status("ret", st);
    o_lasterr();
    o_bool("got_key", k != NULL && k != (HKEY)(ULONG_PTR)0xBADBAD);
    if (!st && k) {
        o_bool("same_as_parent", k == parent);
        DWORD v = 1;
        LONG sv = RegSetValueExW(k, L"probe", 0, REG_DWORD, (const BYTE *)&v, sizeof v);
        o_status("set_value", sv);
        if (!sv) RegDeleteValueW(k, L"probe");
        DWORD nsub = 99;
        o_status("query_info", RegQueryInfoKeyW(k, NULL, NULL, NULL, &nsub, NULL, NULL, NULL, NULL, NULL, NULL, NULL));
    }
    oracle_case_end();
    if (!st && k && k != parent) RegCloseKey(k);
}

#define CREATE_A(name, parent, sub)                               \
    do {                                                          \
        HKEY k_ = (HKEY)(ULONG_PTR)0xBADBAD;                      \
        LONG st_;                                                 \
        ORACLE_CALL(st_, RegCreateKeyA((parent), (sub), &k_));    \
        result_case(name, st_, (parent), k_);                     \
    } while (0)
#define CREATE_W(name, parent, sub)                               \
    do {                                                          \
        HKEY k_ = (HKEY)(ULONG_PTR)0xBADBAD;                      \
        LONG st_;                                                 \
        ORACLE_CALL(st_, RegCreateKeyW((parent), (sub), &k_));    \
        result_case(name, st_, (parent), k_);                     \
    } while (0)
#define OPEN_A(name, parent, sub)                                 \
    do {                                                          \
        HKEY k_ = (HKEY)(ULONG_PTR)0xBADBAD;                      \
        LONG st_;                                                 \
        ORACLE_CALL(st_, RegOpenKeyA((parent), (sub), &k_));      \
        result_case(name, st_, (parent), k_);                     \
    } while (0)
#define OPEN_W(name, parent, sub)                                 \
    do {                                                          \
        HKEY k_ = (HKEY)(ULONG_PTR)0xBADBAD;                      \
        LONG st_;                                                 \
        ORACLE_CALL(st_, RegOpenKeyW((parent), (sub), &k_));      \
        result_case(name, st_, (parent), k_);                     \
    } while (0)

int main(void)
{
    oracle_begin("advapi32/RegCreateKeyA");
    root = oracle_reg_root();

    CREATE_A("createa_new", root, "Alpha");
    CREATE_A("createa_existing", root, "alpha");
    CREATE_A("createa_multi", root, "A1\\B1\\C1");
    CREATE_A("createa_null_subkey", root, NULL);
    CREATE_A("createa_empty_subkey", root, "");
    CREATE_A("createa_leading_backslash", root, "\\Lead");
    CREATE_A("createa_invalid_parent", (HKEY)(ULONG_PTR)0x1234, "X");
    CREATE_W("createw_new", root, L"Beta");
    CREATE_W("createw_existing_upper", root, L"BETA");
    CREATE_W("createw_null_subkey", root, NULL);
    CREATE_W("createw_empty_subkey", root, L"");
    CREATE_W("createw_trailing_backslash", root, L"Gamma\\");
    CREATE_W("createw_invalid_parent", (HKEY)(ULONG_PTR)0x1234, L"X");

    OPEN_A("opena_existing", root, "Alpha");
    OPEN_A("opena_multi_other_case", root, "a1\\b1\\c1");
    OPEN_A("opena_missing", root, "Missing");
    OPEN_A("opena_null_subkey", root, NULL);
    OPEN_A("opena_empty_subkey", root, "");
    OPEN_A("opena_invalid_parent", (HKEY)(ULONG_PTR)0x1234, "Alpha");
    OPEN_W("openw_existing", root, L"Beta");
    OPEN_W("openw_missing", root, L"Missing");
    OPEN_W("openw_null_subkey", root, NULL);
    OPEN_W("openw_empty_subkey", root, L"");
    OPEN_W("openw_trailing_backslash", root, L"Beta\\");
    OPEN_W("openw_null_parent", NULL, L"Beta");

    {   /* NULL/empty subkey on a predefined key. */
        HKEY k = (HKEY)(ULONG_PTR)0xBADBAD;
        LONG st;
        ORACLE_CALL(st, RegOpenKeyW(HKEY_CURRENT_USER, NULL, &k));
        oracle_case("openw_hkcu_null_subkey");
        o_status("ret", st);
        o_lasterr();
        o_bool("same_as_parent", k == HKEY_CURRENT_USER);
        o_bool("is_predefined_range", ((ULONG_PTR)k & 0x80000000u) != 0 && (LONG_PTR)k < 0);
        oracle_case_end();
        if (!st && k != HKEY_CURRENT_USER) RegCloseKey(k);

        k = (HKEY)(ULONG_PTR)0xBADBAD;
        ORACLE_CALL(st, RegCreateKeyW(HKEY_CURRENT_USER, L"", &k));
        oracle_case("createw_hkcu_empty_subkey");
        o_status("ret", st);
        o_lasterr();
        o_bool("same_as_parent", k == HKEY_CURRENT_USER);
        oracle_case_end();
        if (!st && k != HKEY_CURRENT_USER) RegCloseKey(k);
    }

    {   /* A read-only parent: does RegCreateKey still create (the legacy API opens with full access)? */
        HKEY ro = NULL;
        RegOpenKeyExW(root, L"Alpha", 0, KEY_READ, &ro);
        CREATE_W("createw_under_readonly_parent", ro, L"Sub");
        OPEN_W("openw_under_readonly_parent", ro, L"Sub");
        RegCloseKey(ro);
    }

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
