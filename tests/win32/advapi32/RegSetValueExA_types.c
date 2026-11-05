/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: RegSetValueExA for each value type: which types are converted
 * from ANSI to Unicode (string types) and which are stored unchanged. Each
 * value is read back raw with NtQueryValueKey and with RegQueryValueExA.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regsetvalueexa
 *       https://learn.microsoft.com/en-us/windows/win32/sysinfo/registry-value-types
 *       https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-zwqueryvaluekey
 */
#include "oracle.h"

static HKEY root;

typedef struct { ULONG TitleIndex, Type, DataLength; UCHAR Data[1]; } kvpi;
typedef LONG (WINAPI *ntqvk_fn)(HANDLE, void *, int, void *, ULONG, ULONG *);

static void raw(const char *value)
{
    wchar_t w[64];
    MultiByteToWideChar(CP_ACP, 0, value, -1, w, 64);
    ntqvk_fn q = (ntqvk_fn)(void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryValueKey");
    struct { USHORT Length, MaximumLength; const wchar_t *Buffer; } us;
    us.Length = us.MaximumLength = (USHORT)(wcslen(w) * sizeof(wchar_t));
    us.Buffer = w;
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

static void set_case(const char *cname, const char *value, DWORD type, const void *data, DWORD size)
{
    LONG st;
    ORACLE_CALL(st, RegSetValueExA(root, value, 0, type, data, size));
    oracle_case(cname);
    o_status("ret", st);
    o_lasterr();
    raw(value);
    unsigned char b[256];
    memset(b, 0xCC, sizeof b);
    DWORD t = 0xAAAAAAAAu, sz = sizeof b;
    LONG q = RegQueryValueExA(root, value, NULL, &t, b, &sz);
    o_status("qa_ret", q);
    if (!q) { o_u32("qa_type", t); o_u32("qa_size", sz); o_bytes("qa_data", b, sz + 2); }
    oracle_case_end();
}

int main(void)
{
    oracle_begin("advapi32/RegSetValueExA_types");
    root = oracle_reg_root();

    static const char s[] = "text";                 /* 5 bytes with NUL */
    static const char m[] = "ab\0cd\0";              /* 7 bytes: "ab","cd" + final NUL */

    set_case("none", "T0", REG_NONE, s, sizeof s);
    set_case("sz", "T1", REG_SZ, s, sizeof s);
    set_case("expand_sz", "T2", REG_EXPAND_SZ, s, sizeof s);
    set_case("binary", "T3", REG_BINARY, s, sizeof s);
    set_case("dword", "T4", REG_DWORD, s, 4);
    set_case("dword_be", "T5", REG_DWORD_BIG_ENDIAN, s, 4);
    set_case("link", "T6", REG_LINK, s, sizeof s - 1);
    set_case("multi_sz", "T7", REG_MULTI_SZ, m, sizeof m);
    set_case("resource_list", "T8", REG_RESOURCE_LIST, s, sizeof s);
    set_case("full_resource_descriptor", "T9", REG_FULL_RESOURCE_DESCRIPTOR, s, sizeof s);
    set_case("resource_requirements_list", "T10", REG_RESOURCE_REQUIREMENTS_LIST, s, sizeof s);
    set_case("qword", "T11", REG_QWORD, "12345678", 8);
    set_case("undefined_12", "T12", 12, s, sizeof s);
    set_case("undefined_0xffffffff", "TF", 0xFFFFFFFFu, s, sizeof s);
    set_case("high_ansi_sz", "THigh", REG_SZ, "caf\xe9", 5);
    set_case("high_ansi_binary", "THighB", REG_BINARY, "caf\xe9", 5);

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
