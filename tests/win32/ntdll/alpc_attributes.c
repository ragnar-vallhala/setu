/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: ntdll's ALPC message attribute helpers, which the Windows App
 * SDK's CoreMessagingXP.dll (shipped with Firefox) imports:
 * AlpcInitializeMessageAttribute (required sizes, small buffers, the
 * Allocated/ValidAttributes words it writes), AlpcGetMessageAttribute (where
 * each attribute lives in the buffer, and NULL for one that is not
 * allocated), AlpcGetHeaderSize and AlpcMaxAllowedMessageLength.
 *
 * Neither MinGW-w64 nor Wine declare these functions. The prototypes and the
 * attribute bits follow the public, MIT-licensed phnt headers (ntlpcapi.h);
 * everything printed here is black-box behaviour.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/etw/alpc
 *       https://github.com/winsiderss/phnt/blob/master/ntlpcapi.h
 */
#include "oracle.h"

typedef struct {
    ULONG AllocatedAttributes;
    ULONG ValidAttributes;
} ALPC_MESSAGE_ATTRIBUTES_;

typedef LONG (WINAPI *init_fn)(ULONG, ALPC_MESSAGE_ATTRIBUTES_ *, SIZE_T, SIZE_T *);
typedef void *(WINAPI *get_fn)(ALPC_MESSAGE_ATTRIBUTES_ *, ULONG);
typedef ULONG (WINAPI *hdr_fn)(ULONG);
typedef ULONG (WINAPI *max_fn)(void);

static init_fn pInit;
static get_fn pGet;
static hdr_fn pHdr;
static max_fn pMax;

#define ATTR_SECURITY 0x80000000u
#define ATTR_VIEW     0x40000000u
#define ATTR_CONTEXT  0x20000000u
#define ATTR_HANDLE   0x10000000u
#define ATTR_TOKEN    0x08000000u
#define ATTR_DIRECT   0x04000000u
#define ATTR_WORK     0x02000000u

static const ULONG single[] = { ATTR_SECURITY, ATTR_VIEW, ATTR_CONTEXT, ATTR_HANDLE,
                                ATTR_TOKEN, ATTR_DIRECT, ATTR_WORK, 0x01000000u,
                                0x00000001u, 0x00010000u };

static union { ALPC_MESSAGE_ATTRIBUTES_ a; BYTE b[1024]; ULONG_PTR align; } buf;

static void o_offset(const char *k, void *p)
{
    if (!p) o_str(k, "NULL");
    else o_i64(k, (LONG_PTR)((BYTE *)p - buf.b));
}

/* required size for FLAGS and the buffer it initialises */
static void case_flags(const char *name, ULONG flags)
{
    SIZE_T req = 0x7777, req2 = 0x7777;
    LONG st;
    char k[64];
    unsigned i;

    oracle_case(name);
    o_hex32("flags", flags);
    st = pInit(flags, NULL, 0, &req);
    o_status("null_ret", st);
    o_u64("required", req);
    o_u32("header_size", pHdr(flags));

    if (req && req < sizeof(buf)) {
        if (req > sizeof(ALPC_MESSAGE_ATTRIBUTES_)) {
            memset(buf.b, 0xcc, sizeof(buf));
            st = pInit(flags, &buf.a, req - 1, &req2);
            o_status("short_ret", st);
            o_u64("short_required", req2);
            o_hex32("short_allocated", buf.a.AllocatedAttributes);
        }
        memset(buf.b, 0xcc, sizeof(buf));
        req2 = 0x7777;
        st = pInit(flags, &buf.a, req, &req2);
        o_status("ret", st);
        o_u64("ret_required", req2);
        o_hex32("allocated", buf.a.AllocatedAttributes);
        o_hex32("valid", buf.a.ValidAttributes);
        /* does it clear the attribute bodies? */
        o_bool("body_untouched", req <= sizeof(ALPC_MESSAGE_ATTRIBUTES_) ||
               buf.b[sizeof(ALPC_MESSAGE_ATTRIBUTES_)] == 0xcc);
        o_bool("tail_untouched", buf.b[req] == 0xcc);
        for (i = 0; i < sizeof(single) / sizeof(single[0]); i++) {
            wsprintfA(k, "get_%08lx", single[i]);
            o_offset(k, pGet(&buf.a, single[i]));
        }
        wsprintfA(k, "get_%08lx", flags);
        o_offset(k, pGet(&buf.a, flags));
    }
    oracle_case_end();
}

int main(void)
{
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    SIZE_T req;
    LONG st;
    unsigned i;
    char name[64];

    oracle_begin("ntdll/alpc_attributes");

    pInit = (init_fn)(void (*)(void))GetProcAddress(ntdll, "AlpcInitializeMessageAttribute");
    pGet = (get_fn)(void (*)(void))GetProcAddress(ntdll, "AlpcGetMessageAttribute");
    pHdr = (hdr_fn)(void (*)(void))GetProcAddress(ntdll, "AlpcGetHeaderSize");
    pMax = (max_fn)(void (*)(void))GetProcAddress(ntdll, "AlpcMaxAllowedMessageLength");
    oracle_case("exports");
    o_bool("init", pInit != NULL);
    o_bool("get", pGet != NULL);
    o_bool("header_size", pHdr != NULL);
    o_bool("max_length", pMax != NULL);
    oracle_case_end();
    if (!pInit || !pGet || !pHdr || !pMax) return oracle_end();

    oracle_case("max_allowed_length");
    o_u32("value", pMax());
    oracle_case_end();

    case_flags("none", 0);
    for (i = 0; i < sizeof(single) / sizeof(single[0]); i++) {
        wsprintfA(name, "single_%08lx", single[i]);
        case_flags(name, single[i]);
    }
    case_flags("context_handle", ATTR_CONTEXT | ATTR_HANDLE);
    case_flags("security_view_context_handle", ATTR_SECURITY | ATTR_VIEW | ATTR_CONTEXT | ATTR_HANDLE);
    case_flags("all_documented", 0xff000000u);
    case_flags("with_low_bits", ATTR_CONTEXT | 0x00ffffffu);

    /* the buffer is optional only when it is too small */
    oracle_case("null_buffer_with_size");
    req = 0x7777;
    st = pInit(ATTR_CONTEXT, NULL, 64, &req);
    o_status("ret", st);
    o_u64("required", req);
    oracle_case_end();

    /* AlpcGetMessageAttribute when the flag is allocated but not valid */
    oracle_case("get_ignores_valid");
    memset(buf.b, 0, sizeof(buf));
    pInit(ATTR_CONTEXT | ATTR_HANDLE, &buf.a, sizeof(buf), &req);
    buf.a.ValidAttributes = 0;
    o_offset("context", pGet(&buf.a, ATTR_CONTEXT));
    buf.a.AllocatedAttributes = ATTR_HANDLE;
    o_offset("context_not_allocated", pGet(&buf.a, ATTR_CONTEXT));
    o_offset("handle_alone", pGet(&buf.a, ATTR_HANDLE));
    buf.a.AllocatedAttributes = 0xff000000u;
    o_offset("handle_all_allocated", pGet(&buf.a, ATTR_HANDLE));
    o_offset("context_all_allocated", pGet(&buf.a, ATTR_CONTEXT));
    o_offset("two_flags", pGet(&buf.a, ATTR_CONTEXT | ATTR_HANDLE));
    oracle_case_end();

    return oracle_end();
}
