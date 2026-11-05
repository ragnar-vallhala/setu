/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: RegCopyTreeA: copying a subtree with values, cp1252 names,
 * NULL and missing subkeys, and bad handles.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regcopytreea
 */
#include "oracle.h"

static HKEY root;

static void cat(char *out, size_t cap, const char *s) { strncat(out, s, cap - strlen(out) - 1); }

static void dump(HKEY k, const wchar_t *name, char *out, size_t cap)
{
    DWORD nsub = 0, nval = 0;
    RegQueryInfoKeyW(k, NULL, NULL, NULL, &nsub, NULL, NULL, &nval, NULL, NULL, NULL, NULL);
    cat(out, cap, oracle_utf8(name));
    cat(out, cap, "{");
    for (DWORD i = 0; i < nval; i++) {
        wchar_t vn[64];
        DWORD vl = 64;
        if (RegEnumValueW(k, i, vn, &vl, NULL, NULL, NULL, NULL)) break;
        if (i) cat(out, cap, ",");
        cat(out, cap, vl ? oracle_utf8(vn) : "@");
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

static void tree_field(const char *field, const char *path)
{
    char out[1024] = "";
    HKEY k;
    LONG st = RegOpenKeyExA(root, path, 0, KEY_READ, &k);
    if (st) snprintf(out, sizeof out, "missing(%ld)", (long)st);
    else {
        dump(k, L".", out, sizeof out);
        RegCloseKey(k);
    }
    o_str(field, out);
}

static HKEY mk(const char *path)
{
    HKEY k = NULL;
    DWORD v = 1;
    RegCreateKeyExA(root, path, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL);
    RegSetValueExA(k, "v", 0, REG_DWORD, (const BYTE *)&v, sizeof v);
    return k;
}

#define COPY_CASE(name, src, sub, dst, show)                \
    do {                                                     \
        LONG st_;                                            \
        ORACLE_CALL(st_, RegCopyTreeA((src), (sub), (dst))); \
        oracle_case(name);                                   \
        o_status("ret", st_);                                \
        o_lasterr();                                         \
        tree_field("dest", show);                            \
        oracle_case_end();                                   \
    } while (0)

int main(void)
{
    oracle_begin("advapi32/RegCopyTreeA");
    root = oracle_reg_root();

    HKEY s = mk("src");
    RegSetValueExA(s, "caf\xe9", 0, REG_SZ, (const BYTE *)"x", 2);
    RegCloseKey(mk("src\\caf\xe9\\inner"));
    RegCloseKey(mk("src\\b"));

    HKEY d = mk("d1");
    COPY_CASE("copy_null_subkey", s, NULL, d, "d1");
    RegCloseKey(d);
    d = mk("d2");
    COPY_CASE("copy_cp1252_subkey_upper", s, "CAF\xc9", d, "d2");
    COPY_CASE("copy_missing_subkey", s, "nope", d, "d2");
    COPY_CASE("copy_empty_subkey", s, "", d, "d2");
    COPY_CASE("copy_invalid_src", (HKEY)(ULONG_PTR)0x1234, NULL, d, "d2");
    COPY_CASE("copy_invalid_dest", s, NULL, (HKEY)(ULONG_PTR)0x1234, "d2");
    RegCloseKey(d);

    RegCloseKey(s);
    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
