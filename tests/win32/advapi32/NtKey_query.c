/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: ntdll NtQueryKey and NtEnumerateKey information classes on a
 * test key: KeyBasicInformation (name = last component only?),
 * KeyNodeInformation (class offset), KeyFullInformation (counts and max
 * lengths in bytes), KeyNameInformation (full path; the user SID part is
 * replaced by <SID>), KeyCachedInformation, KeyHandleTagsInformation, buffer
 * too small / overflow statuses and the returned lengths, and an invalid
 * class.
 *
 * Docs: https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-zwquerykey
 *       https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-zwenumeratekey
 *       https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/ne-wdm-_key_information_class
 *       https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/ns-wdm-_key_basic_information
 *       https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/ns-wdm-_key_full_information
 */
#include "oracle.h"

typedef LONG(NTAPI *NtQueryKey_fn)(HANDLE, int, void *, ULONG, ULONG *);
typedef LONG(NTAPI *NtEnumerateKey_fn)(HANDLE, ULONG, int, void *, ULONG, ULONG *);
static NtQueryKey_fn pNtQueryKey;
static NtEnumerateKey_fn pNtEnumerateKey;

enum { KeyBasic = 0, KeyNode = 1, KeyFull = 2, KeyName = 3, KeyCached = 4, KeyFlags = 5, KeyVirt = 6, KeyHandleTags = 7 };

typedef struct { LARGE_INTEGER LastWriteTime; ULONG TitleIndex; ULONG NameLength; WCHAR Name[1]; } BASIC;
typedef struct { LARGE_INTEGER LastWriteTime; ULONG TitleIndex; ULONG ClassOffset; ULONG ClassLength; ULONG NameLength; WCHAR Name[1]; } NODE;
typedef struct {
    LARGE_INTEGER LastWriteTime; ULONG TitleIndex; ULONG ClassOffset; ULONG ClassLength; ULONG SubKeys;
    ULONG MaxNameLen; ULONG MaxClassLen; ULONG Values; ULONG MaxValueNameLen; ULONG MaxValueDataLen; WCHAR Class[1];
} FULL;
typedef struct { ULONG NameLength; WCHAR Name[1]; } NAMEINFO;
typedef struct {
    LARGE_INTEGER LastWriteTime; ULONG TitleIndex; ULONG SubKeys; ULONG MaxNameLen; ULONG Values;
    ULONG MaxValueNameLen; ULONG MaxValueDataLen; ULONG NameLength;
} CACHED;

static wchar_t sid[200];

static void get_sid(void)
{
    HANDLE tok;
    BYTE buf[256];
    DWORD len = 0;
    OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok);
    GetTokenInformation(tok, TokenUser, buf, sizeof buf, &len);
    CloseHandle(tok);
    /* Format S-1-<auth>-<sub>... by hand (no extra imports). */
    PSID s = ((TOKEN_USER *)buf)->User.Sid;
    PSID_IDENTIFIER_AUTHORITY a = GetSidIdentifierAuthority(s);
    int n = swprintf(sid, 200, L"S-1-%u", a->Value[5]);
    for (DWORD i = 0; i < *GetSidSubAuthorityCount(s); i++)
        n += swprintf(sid + n, 200 - n, L"-%lu", (unsigned long)*GetSidSubAuthority(s, i));
}

static const char *tokenize(const wchar_t *w, ULONG chars)
{
    static wchar_t tmp[1024], out[1024];
    if (chars > 1000) chars = 1000;
    wmemcpy(tmp, w, chars);
    tmp[chars] = 0;
    wchar_t *p = wcsstr(tmp, sid);
    if (p && sid[0]) {
        size_t pre = (size_t)(p - tmp);
        wmemcpy(out, tmp, pre);
        out[pre] = 0;
        wcscat(out, L"<SID>");
        wcscat(out, p + wcslen(sid));
    } else {
        wcscpy(out, tmp);
    }
    return oracle_utf8(out);
}

int main(void)
{
    oracle_begin("advapi32/NtKey_query");
    HMODULE nt = GetModuleHandleW(L"ntdll.dll");
    pNtQueryKey = (NtQueryKey_fn)(void (*)(void))GetProcAddress(nt, "NtQueryKey");
    pNtEnumerateKey = (NtEnumerateKey_fn)(void (*)(void))GetProcAddress(nt, "NtEnumerateKey");
    get_sid();
    HKEY root = oracle_reg_root(), k, c;
    RegCreateKeyExW(root, L"Node", 0, L"NodeClass", 0, KEY_ALL_ACCESS, NULL, &k, NULL);
    RegCreateKeyExW(k, L"ChildOne", 0, L"CC", 0, KEY_ALL_ACCESS, NULL, &c, NULL);
    RegCloseKey(c);
    RegCreateKeyExW(k, L"Kid", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &c, NULL);
    RegCloseKey(c);
    RegSetValueExW(k, L"valname", 0, REG_SZ, (const BYTE *)L"abcdef", 14);

    static BYTE buf[4096];
    ULONG len;
    LONG st;

    memset(buf, 0, sizeof buf);
    st = pNtQueryKey(k, KeyBasic, buf, sizeof buf, &len);
    oracle_case("basic");
    o_hex32("status", (DWORD)st);
    o_u32("len", len);
    if (st >= 0) {
        BASIC *b = (BASIC *)buf;
        o_u32("name_length", b->NameLength);
        o_str("name", tokenize(b->Name, b->NameLength / 2));
        o_u32("title_index", b->TitleIndex);
        o_bool("time_nonzero", b->LastWriteTime.QuadPart != 0);
    }
    oracle_case_end();

    memset(buf, 0, sizeof buf);
    st = pNtQueryKey(k, KeyNode, buf, sizeof buf, &len);
    oracle_case("node");
    o_hex32("status", (DWORD)st);
    o_u32("len", len);
    if (st >= 0) {
        NODE *n = (NODE *)buf;
        o_u32("class_offset", n->ClassOffset);
        o_u32("class_length", n->ClassLength);
        o_u32("name_length", n->NameLength);
        if (n->ClassOffset && n->ClassOffset < sizeof buf)
            o_str("class", tokenize((WCHAR *)(buf + n->ClassOffset), n->ClassLength / 2));
    }
    oracle_case_end();

    memset(buf, 0, sizeof buf);
    st = pNtQueryKey(k, KeyFull, buf, sizeof buf, &len);
    oracle_case("full");
    o_hex32("status", (DWORD)st);
    o_u32("len", len);
    if (st >= 0) {
        FULL *f = (FULL *)buf;
        o_u32("class_offset", f->ClassOffset);
        o_u32("class_length", f->ClassLength);
        o_u32("subkeys", f->SubKeys);
        o_u32("max_name_len", f->MaxNameLen);
        o_u32("max_class_len", f->MaxClassLen);
        o_u32("values", f->Values);
        o_u32("max_value_name_len", f->MaxValueNameLen);
        o_u32("max_value_data_len", f->MaxValueDataLen);
    }
    oracle_case_end();

    memset(buf, 0, sizeof buf);
    st = pNtQueryKey(k, KeyName, buf, sizeof buf, &len);
    oracle_case("name");
    o_hex32("status", (DWORD)st);
    if (st >= 0) {
        NAMEINFO *n = (NAMEINFO *)buf;
        o_str("path", tokenize(n->Name, n->NameLength / 2));
    }
    oracle_case_end();

    memset(buf, 0, sizeof buf);
    st = pNtQueryKey(k, KeyCached, buf, sizeof buf, &len);
    oracle_case("cached");
    o_hex32("status", (DWORD)st);
    o_u32("len", len);
    if (st >= 0) {
        CACHED *ci = (CACHED *)buf;
        o_u32("subkeys", ci->SubKeys);
        o_u32("max_name_len", ci->MaxNameLen);
        o_u32("values", ci->Values);
        o_u32("max_value_name_len", ci->MaxValueNameLen);
        o_u32("max_value_data_len", ci->MaxValueDataLen);
        o_u32("name_length", ci->NameLength);
    }
    oracle_case_end();

    {
        static const int classes[] = {KeyFlags, KeyVirt, KeyHandleTags, 8, 9, 10, 99};
        for (size_t i = 0; i < sizeof classes / sizeof *classes; i++) {
            char nm[32];
            memset(buf, 0, sizeof buf);
            len = 0xAAAA;
            st = pNtQueryKey(k, classes[i], buf, sizeof buf, &len);
            snprintf(nm, sizeof nm, "class_%d", classes[i]);
            oracle_case(nm);
            o_hex32("status", (DWORD)st);
            if (st >= 0) o_u32("len", len);
            oracle_case_end();
        }
    }
    {   /* Small buffers. */
        static const ULONG sizes[] = {0, 4, 16, 20, 24};
        for (size_t i = 0; i < sizeof sizes / sizeof *sizes; i++) {
            char nm[32];
            len = 0xAAAA;
            memset(buf, 0xcc, 64);
            st = pNtQueryKey(k, KeyBasic, buf, sizes[i], &len);
            snprintf(nm, sizeof nm, "basic_buffer_%lu", (unsigned long)sizes[i]);
            oracle_case(nm);
            o_hex32("status", (DWORD)st);
            o_u32("len", len);
            o_bytes("head_after_time", buf + 8, 16);
            o_bool("time_bytes_untouched", buf[0] == 0xcc && buf[7] == 0xcc);
            oracle_case_end();
        }
        len = 0xAAAA;
        st = pNtQueryKey(k, KeyName, buf, 6, &len);
        oracle_case("name_buffer_6");
        o_hex32("status", (DWORD)st);
        o_bool("len_plausible", len > 6 && len < 1024);
        oracle_case_end();
        len = 0xAAAA;
        st = pNtQueryKey(k, KeyFull, buf, 44, &len);
        oracle_case("full_buffer_44");
        o_hex32("status", (DWORD)st);
        o_u32("len", len);
        oracle_case_end();
    }
    {   /* NtEnumerateKey. */
        for (ULONG i = 0; i < 3; i++) {
            char nm[32];
            memset(buf, 0, sizeof buf);
            len = 0xAAAA;
            st = pNtEnumerateKey(k, i, KeyNode, buf, sizeof buf, &len);
            snprintf(nm, sizeof nm, "enum_node_%lu", (unsigned long)i);
            oracle_case(nm);
            o_hex32("status", (DWORD)st);
            if (st >= 0) {
                NODE *n = (NODE *)buf;
                o_u32("len", len);
                o_str("name", tokenize(n->Name, n->NameLength / 2));
                o_u32("class_offset", n->ClassOffset);
                o_u32("class_length", n->ClassLength);
            }
            oracle_case_end();
        }
        len = 0xAAAA;
        st = pNtEnumerateKey(k, 0, KeyBasic, buf, 8, &len);
        oracle_case("enum_basic_buffer_8");
        o_hex32("status", (DWORD)st);
        o_u32("len", len);
        oracle_case_end();
        st = pNtEnumerateKey(k, 0, KeyName, buf, sizeof buf, &len);
        oracle_case("enum_name_class");
        o_hex32("status", (DWORD)st);
        oracle_case_end();
        st = pNtEnumerateKey(k, 0, KeyFull, buf, sizeof buf, &len);
        oracle_case("enum_full_class");
        o_hex32("status", (DWORD)st);
        if (st >= 0) o_u32("len", len);
        oracle_case_end();
    }

    RegCloseKey(k);
    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
