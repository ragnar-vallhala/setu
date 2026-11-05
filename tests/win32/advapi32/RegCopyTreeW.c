/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: RegCopyTreeW: what is copied (subkeys, values of every type,
 * the default value, key classes, volatility), merging into a destination
 * that already has keys and values, NULL subkey (copy the source key's own
 * contents), access needed on source and destination handles, and errors.
 * Trees are dumped as one string in enumeration order:
 *   name<class>{value:type=hex,...}[child,child]
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regcopytreew
 */
#include "oracle.h"

static HKEY root;

static void cat(char *out, size_t cap, const char *s) { strncat(out, s, cap - strlen(out) - 1); }

static void dump(HKEY k, const wchar_t *name, char *out, size_t cap)
{
    wchar_t cls[64];
    DWORD clen = 64, nsub = 0, nval = 0;
    RegQueryInfoKeyW(k, cls, &clen, NULL, &nsub, NULL, NULL, &nval, NULL, NULL, NULL, NULL);
    cat(out, cap, oracle_utf8(name));
    if (clen) { cat(out, cap, "<"); cat(out, cap, oracle_utf8(cls)); cat(out, cap, ">"); }
    cat(out, cap, "{");
    for (DWORD i = 0; i < nval; i++) {
        wchar_t vn[64];
        BYTE data[64];
        DWORD vl = 64, dl = sizeof data, type = 0;
        char t[200];
        if (RegEnumValueW(k, i, vn, &vl, NULL, &type, data, &dl)) break;
        snprintf(t, sizeof t, "%s%s:%lu=", i ? "," : "", vl ? oracle_utf8(vn) : "@", (unsigned long)type);
        cat(out, cap, t);
        for (DWORD j = 0; j < dl && j < 16; j++) {
            snprintf(t, sizeof t, "%02x", data[j]);
            cat(out, cap, t);
        }
    }
    cat(out, cap, "}");
    if (nsub) {
        cat(out, cap, "[");
        for (DWORD i = 0; i < nsub; i++) {
            wchar_t sn[64];
            DWORD sl = 64;
            HKEY c;
            if (RegEnumKeyExW(k, i, sn, &sl, NULL, NULL, NULL, NULL)) break;
            if (i) cat(out, cap, ",");
            if (!RegOpenKeyExW(k, sn, 0, KEY_READ, &c)) {
                dump(c, sn, out, cap);
                RegCloseKey(c);
            }
        }
        cat(out, cap, "]");
    }
}

static void tree_field(const char *field, const wchar_t *path)
{
    char out[2048] = "";
    HKEY k;
    LONG st = RegOpenKeyExW(root, path, 0, KEY_READ, &k);
    if (st) {
        snprintf(out, sizeof out, "missing(%ld)", (long)st);
    } else {
        dump(k, path, out, sizeof out);
        RegCloseKey(k);
    }
    o_str(field, out);
}

static HKEY mk(const wchar_t *path, const wchar_t *cls, DWORD options)
{
    HKEY k = NULL;
    RegCreateKeyExW(root, path, 0, (wchar_t *)cls, options, KEY_ALL_ACCESS, NULL, &k, NULL);
    return k;
}

static void setd(HKEY k, const wchar_t *n, DWORD v) { RegSetValueExW(k, n, 0, REG_DWORD, (const BYTE *)&v, sizeof v); }

/* Volatile iff a non-volatile child cannot be created. */
static void volatile_field(const char *field, const wchar_t *path)
{
    HKEY k, c;
    LONG st = RegOpenKeyExW(root, path, 0, KEY_ALL_ACCESS, &k);
    if (!st) {
        st = RegCreateKeyExW(k, L"nvprobe", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &c, NULL);
        if (!st) { RegCloseKey(c); RegDeleteKeyW(k, L"nvprobe"); }
        RegCloseKey(k);
    }
    o_status(field, st);
}

#define COPY_CASE(name, src, sub, dst, show)              \
    do {                                                   \
        LONG st_;                                          \
        ORACLE_CALL(st_, RegCopyTreeW((src), (sub), (dst))); \
        oracle_case(name);                                 \
        o_status("ret", st_);                              \
        o_lasterr();                                       \
        tree_field("dest", show);                          \
        oracle_case_end();                                 \
    } while (0)

int main(void)
{
    oracle_begin("advapi32/RegCopyTreeW");
    root = oracle_reg_root();

    HKEY s = mk(L"src", L"SrcClass", 0);
    setd(s, NULL, 1);
    setd(s, L"d", 2);
    RegSetValueExW(s, L"sz", 0, REG_SZ, (const BYTE *)L"hi", 6);
    RegSetValueExW(s, L"bin", 0, REG_BINARY, (const BYTE *)"\x01\x02\x03", 3);
    RegSetValueExW(s, L"none", 0, REG_NONE, NULL, 0);
    RegSetValueExW(s, L"odd", 0, 0x12345, (const BYTE *)"\xaa", 1);
    HKEY a = mk(L"src\\a", L"AClass", 0);
    setd(a, L"av", 3);
    HKEY b = mk(L"src\\a\\b", NULL, 0);
    HKEY v = mk(L"src\\vol", NULL, REG_OPTION_VOLATILE);
    setd(v, L"vv", 4);
    RegCloseKey(a);
    RegCloseKey(b);
    RegCloseKey(v);
    mk(L"src\\z", NULL, 0);

    oracle_case("source");
    tree_field("src", L"src");
    oracle_case_end();

    HKEY d1 = mk(L"d1", NULL, 0);
    COPY_CASE("copy_null_subkey_to_empty", s, NULL, d1, L"d1");
    oracle_case("copy_volatility");
    volatile_field("d1_vol_nonvolatile_child", L"d1\\vol");
    volatile_field("d1_a_nonvolatile_child", L"d1\\a");
    oracle_case_end();
    RegCloseKey(d1);

    HKEY d2 = mk(L"d2", L"DestClass", 0);
    COPY_CASE("copy_subkey", root, L"src\\a", d2, L"d2");
    COPY_CASE("copy_subkey_again_merge", root, L"src\\a", d2, L"d2");
    RegCloseKey(d2);

    HKEY d3 = mk(L"d3", NULL, 0);
    setd(d3, L"d", 99);
    setd(d3, L"keep", 5);
    HKEY d3a = mk(L"d3\\a", L"Existing", 0);
    setd(d3a, L"av", 100);
    setd(d3a, L"own", 6);
    RegCloseKey(d3a);
    HKEY d3x = mk(L"d3\\x", NULL, 0);
    RegCloseKey(d3x);
    COPY_CASE("copy_into_populated_merge", s, NULL, d3, L"d3");
    RegCloseKey(d3);

    HKEY d4 = mk(L"d4", NULL, 0);
    COPY_CASE("copy_empty_subkey_string", s, L"", d4, L"d4");
    COPY_CASE("copy_missing_subkey", s, L"nope", d4, L"d4");
    COPY_CASE("copy_other_case_subkey", root, L"SRC\\A\\B", d4, L"d4");
    RegCloseKey(d4);

    {   /* Access on the source and destination handles. */
        HKEY sq, dd;
        RegOpenKeyExW(root, L"src", 0, KEY_QUERY_VALUE, &sq);
        mk(L"d5", NULL, 0);
        RegOpenKeyExW(root, L"d5", 0, KEY_ALL_ACCESS, &dd);
        COPY_CASE("source_query_value_only", sq, NULL, dd, L"d5");
        RegCloseKey(sq);
        RegCloseKey(dd);
        RegOpenKeyExW(root, L"src", 0, KEY_ENUMERATE_SUB_KEYS, &sq);
        RegDeleteTreeW(root, L"d5");
        mk(L"d5", NULL, 0);
        RegOpenKeyExW(root, L"d5", 0, KEY_ALL_ACCESS, &dd);
        COPY_CASE("source_enumerate_only", sq, NULL, dd, L"d5");
        RegCloseKey(sq);
        RegCloseKey(dd);
        mk(L"d6", NULL, 0);
        RegOpenKeyExW(root, L"d6", 0, KEY_READ, &dd);
        COPY_CASE("dest_read_only", s, NULL, dd, L"d6");
        RegCloseKey(dd);
        mk(L"d7", NULL, 0);
        RegOpenKeyExW(root, L"d7", 0, KEY_SET_VALUE, &dd);
        COPY_CASE("dest_set_value_only", s, NULL, dd, L"d7");
        RegCloseKey(dd);
        mk(L"d8", NULL, 0);
        RegOpenKeyExW(root, L"d8", 0, KEY_CREATE_SUB_KEY, &dd);
        COPY_CASE("dest_create_sub_key_only", s, NULL, dd, L"d8");
        RegCloseKey(dd);
    }
    {
        HKEY dv = mk(L"dv", NULL, REG_OPTION_VOLATILE);
        COPY_CASE("copy_into_volatile_dest", s, NULL, dv, L"dv");
        RegCloseKey(dv);
    }
    {
        HKEY d9 = mk(L"d9", NULL, 0);
        COPY_CASE("invalid_source", (HKEY)(ULONG_PTR)0x1234, NULL, d9, L"d9");
        COPY_CASE("invalid_dest", s, NULL, (HKEY)(ULONG_PTR)0x1234, L"d9");
        COPY_CASE("null_dest", s, NULL, NULL, L"d9");
        COPY_CASE("null_dest_with_subkey", root, L"src\\z", NULL, L"d9");
        RegCloseKey(d9);
    }

    RegCloseKey(s);
    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
