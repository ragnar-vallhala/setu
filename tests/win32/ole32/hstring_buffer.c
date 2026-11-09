/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: HSTRING buffers in combase: WindowsPreallocateStringBuffer
 * with length 0 and 1 (returned buffer, whether its memory is writable,
 * its terminating NUL), writing the documented terminator and promoting,
 * deleting an unpromoted buffer.
 *
 * combase is loaded with LoadLibraryW; functions come from GetProcAddress.
 * Writability is reported from VirtualQuery's page protection, so the test
 * never faults. HRESULTs are printed in hex; no pointer values.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winstring/nf-winstring-windowspreallocatestringbuffer
 *       https://learn.microsoft.com/en-us/windows/win32/api/winstring/nf-winstring-windowspromotestringbuffer
 *       https://learn.microsoft.com/en-us/windows/win32/api/winstring/nf-winstring-windowsdeletestringbuffer
 *       https://learn.microsoft.com/en-us/windows/win32/api/winstring/nf-winstring-windowsgetstringlen
 */
#include "oracle.h"

typedef void *HS_STRING;
typedef void *HS_BUFFER;
typedef HRESULT (WINAPI *prealloc_fn)(UINT32, WCHAR **, HS_BUFFER *);
typedef HRESULT (WINAPI *promote_fn)(HS_BUFFER, HS_STRING *);
typedef HRESULT (WINAPI *delbuf_fn)(HS_BUFFER);
typedef HRESULT (WINAPI *delstr_fn)(HS_STRING);
typedef UINT32 (WINAPI *len_fn)(HS_STRING);

static int writable(const void *p)
{
    MEMORY_BASIC_INFORMATION mbi;
    DWORD prot;
    if (!VirtualQuery(p, &mbi, sizeof(mbi))) return 0;
    prot = mbi.Protect & 0xff;
    return prot == PAGE_READWRITE || prot == PAGE_WRITECOPY
        || prot == PAGE_EXECUTE_READWRITE || prot == PAGE_EXECUTE_WRITECOPY;
}

int main(void)
{
    oracle_begin("ole32/hstring_buffer");
    HMODULE mod = LoadLibraryW(L"combase.dll");
    prealloc_fn pPrealloc = (prealloc_fn)(void (*)(void))GetProcAddress(mod, "WindowsPreallocateStringBuffer");
    promote_fn pPromote = (promote_fn)(void (*)(void))GetProcAddress(mod, "WindowsPromoteStringBuffer");
    delbuf_fn pDelBuf = (delbuf_fn)(void (*)(void))GetProcAddress(mod, "WindowsDeleteStringBuffer");
    delstr_fn pDelStr = (delstr_fn)(void (*)(void))GetProcAddress(mod, "WindowsDeleteString");
    len_fn pLen = (len_fn)(void (*)(void))GetProcAddress(mod, "WindowsGetStringLen");
    WCHAR *ptr;
    HS_BUFFER buf;
    HS_STRING str;
    HRESULT hr;

    oracle_case("exports_present");
    o_bool("present", mod && pPrealloc && pPromote && pDelBuf && pDelStr && pLen);
    oracle_case_end();
    if (!(mod && pPrealloc && pPromote && pDelBuf && pDelStr && pLen)) return oracle_end();

    /* length 0: the buffer already holds the terminator, which callers may write */
    ptr = NULL;
    buf = (HS_BUFFER)1;
    hr = pPrealloc(0, &ptr, &buf);
    oracle_case("zero_length");
    o_hex32("hr", (DWORD)hr);
    o_bool("ptr_set", ptr != NULL);
    o_bool("buffer_null", buf == NULL);
    o_bool("ptr_is_nul", ptr && ptr[0] == 0);
    o_bool("ptr_writable", ptr && writable(ptr));
    oracle_case_end();
    if (ptr && writable(ptr)) ptr[0] = 0;
    str = (HS_STRING)1;
    hr = pPromote(buf, &str);
    oracle_case("zero_length_promote");
    o_hex32("hr", (DWORD)hr);
    o_bool("string_null", str == NULL);
    oracle_case_end();
    pDelStr(str == (HS_STRING)1 ? NULL : str);

    /* two zero-length buffers */
    {
        WCHAR *p1 = NULL, *p2 = NULL;
        HS_BUFFER b1, b2;
        pPrealloc(0, &p1, &b1);
        pPrealloc(0, &p2, &b2);
        oracle_case("zero_length_twice");
        o_bool("same_pointer", p1 == p2);
        o_bool("both_writable", p1 && p2 && writable(p1) && writable(p2));
        oracle_case_end();
        pDelBuf(b1);
        pDelBuf(b2);
    }

    /* length 1: write the character and the terminator after it */
    ptr = NULL;
    buf = NULL;
    hr = pPrealloc(1, &ptr, &buf);
    oracle_case("one_char");
    o_hex32("hr", (DWORD)hr);
    o_bool("buffer_set", buf != NULL);
    o_bool("terminated", ptr && ptr[1] == 0);
    o_bool("ptr_writable", ptr && writable(ptr));
    oracle_case_end();
    if (ptr) { ptr[0] = L'a'; ptr[1] = 0; }
    str = NULL;
    hr = pPromote(buf, &str);
    oracle_case("one_char_promote");
    o_hex32("hr", (DWORD)hr);
    o_u32("len", pLen(str));
    oracle_case_end();
    pDelStr(str);

    /* deleting a zero-length buffer */
    pPrealloc(0, &ptr, &buf);
    hr = pDelBuf(buf);
    oracle_case("zero_length_delete");
    o_hex32("hr", (DWORD)hr);
    oracle_case_end();

    return oracle_end();
}
