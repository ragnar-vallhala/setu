/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: RegSetValueExA string data (REG_SZ, REG_EXPAND_SZ,
 * REG_MULTI_SZ) with missing, extra and embedded NUL terminators, sizes of
 * 0 and 1, and NULL data: the stored Unicode bytes (read raw with
 * NtQueryValueKey) and what RegQueryValueExW returns.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regsetvalueexa
 *       https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-zwqueryvaluekey
 */
#include "oracle.h"

static HKEY root;

typedef struct { ULONG TitleIndex, Type, DataLength; UCHAR Data[1]; } kvpi;
typedef LONG (WINAPI *ntqvk_fn)(HANDLE, void *, int, void *, ULONG, ULONG *);

static void raw(const wchar_t *value)
{
    ntqvk_fn q = (ntqvk_fn)(void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryValueKey");
    struct { USHORT Length, MaximumLength; const wchar_t *Buffer; } us;
    us.Length = us.MaximumLength = (USHORT)(wcslen(value) * sizeof(wchar_t));
    us.Buffer = value;
    union { kvpi info; unsigned char b[600]; } buf;
    ULONG len = 0;
    LONG st = q(root, &us, 2, &buf, sizeof buf, &len);
    o_hex32("raw_status", (DWORD)st);
    if (!st) {
        o_u32("raw_type", buf.info.Type);
        o_u32("raw_size", buf.info.DataLength);
        o_bytes("raw_data", buf.info.Data, buf.info.DataLength);
    }
}

static void set_case(const char *cname, DWORD type, const void *data, DWORD size)
{
    LONG st;
    ORACLE_CALL(st, RegSetValueExA(root, "V", 0, type, data, size));
    oracle_case(cname);
    o_status("ret", st);
    o_lasterr();
    raw(L"V");
    unsigned char b[256];
    memset(b, 0xCC, sizeof b);
    DWORD sz = sizeof b;
    LONG q = RegQueryValueExA(root, "V", NULL, NULL, b, &sz);
    o_status("qa_ret", q);
    if (!q) { o_u32("qa_size", sz); o_bytes("qa_data", b, sz + 2); }
    RegDeleteValueA(root, "V");
    oracle_case_end();
}

int main(void)
{
    oracle_begin("advapi32/RegSetValueExA_strings");
    root = oracle_reg_root();

    static const char hello[] = "hello";       /* 6 bytes */
    static const char extra[] = "hello\0";      /* 7 bytes */
    static const char emb[] = "ab\0cd";         /* 6 bytes */
    static const DWORD types[] = { REG_SZ, REG_EXPAND_SZ, REG_MULTI_SZ };
    static const char *tn[] = { "sz", "expand", "multi" };

    for (int i = 0; i < 3; i++) {
        char n[64];
#define C(suffix, data, size) snprintf(n, sizeof n, "%s_%s", tn[i], suffix); set_case(n, types[i], data, size)
        C("with_nul", hello, sizeof hello);
        C("without_nul", hello, sizeof hello - 1);
        C("extra_nul", extra, sizeof extra);
        C("embedded_nul", emb, sizeof emb);
        C("size_1", hello, 1);
        C("size_1_nul", "", 1);
        C("size_0", hello, 0);
        C("size_0_null", NULL, 0);
        C("double_nul_only", "\0", 2);
#undef C
    }

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
