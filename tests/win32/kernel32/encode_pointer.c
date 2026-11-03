/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: EncodePointer / DecodePointer, EncodeSystemPointer /
 * DecodeSystemPointer and EncodeRemotePointer / DecodeRemotePointer.
 * Encoded values are secret per process, so only relationships are printed:
 * round trips, stability, NULL handling, the process and system encodings
 * being different, the remote functions on the current process matching the
 * local ones, a child process encoding differently, and last-error effects.
 *
 * Docs: https://learn.microsoft.com/en-us/previous-versions/bb432254(v=vs.85)
 *       https://learn.microsoft.com/en-us/previous-versions/bb432242(v=vs.85)
 *       https://learn.microsoft.com/en-us/previous-versions/bb432255(v=vs.85)
 *       https://learn.microsoft.com/en-us/previous-versions/bb432243(v=vs.85)
 *       https://learn.microsoft.com/en-us/windows/win32/api/utilapiset/nf-utilapiset-encodepointer
 *       https://learn.microsoft.com/en-us/windows/win32/api/utilapiset/nf-utilapiset-decodepointer
 *       https://learn.microsoft.com/en-us/windows/win32/api/utilapiset/nf-utilapiset-encoderemotepointer
 */
#include "oracle.h"

typedef PVOID (WINAPI *enc_fn)(PVOID);
typedef HRESULT (WINAPI *remote_fn)(HANDLE, PVOID, PVOID *);

int main(int argc, char **argv)
{
    void *probe = (void *)(ULONG_PTR)0x12345678abcULL;
    if (argc > 1 && !strcmp(argv[1], "child")) {
        /* Report whether the child's encoding of the probe equals the value passed in. */
        ULONG_PTR parent = (ULONG_PTR)_strtoui64(argv[2], NULL, 16);
        ExitProcess((ULONG_PTR)EncodePointer(probe) == parent ? 1 : 2);
    }

    oracle_begin("kernel32/encode_pointer");

    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    enc_fn pEncSys = (enc_fn)(void *)GetProcAddress(k32, "EncodeSystemPointer");
    enc_fn pDecSys = (enc_fn)(void *)GetProcAddress(k32, "DecodeSystemPointer");
    remote_fn pEncRemote = (remote_fn)(void *)GetProcAddress(k32, "EncodeRemotePointer");
    remote_fn pDecRemote = (remote_fn)(void *)GetProcAddress(k32, "DecodeRemotePointer");
    if (!pEncRemote) {
        HMODULE kb = GetModuleHandleW(L"kernelbase.dll");
        pEncRemote = (remote_fn)(void *)GetProcAddress(kb, "EncodeRemotePointer");
        pDecRemote = (remote_fn)(void *)GetProcAddress(kb, "DecodeRemotePointer");
    }

    void *e1, *e2, *d;
    ORACLE_CALL(e1, EncodePointer(probe));
    oracle_case("encode");
    o_lasterr();
    o_bool("changes_value", e1 != probe);
    oracle_case_end();

    e2 = EncodePointer(probe);
    ORACLE_CALL(d, DecodePointer(e1));
    oracle_case("decode_round_trip");
    o_lasterr();
    o_bool("round_trip", d == probe);
    o_bool("stable", e1 == e2);
    oracle_case_end();

    {
        void *en = EncodePointer(NULL);
        oracle_case("encode_null");
        o_bool("nonnull", en != NULL);
        o_bool("decodes_to_null", DecodePointer(en) == NULL);
        o_bool("decode_null_nonnull", DecodePointer(NULL) != NULL);
        oracle_case_end();
    }
    {
        void *a = EncodePointer((void *)1), *b = EncodePointer((void *)2);
        oracle_case("distinct_inputs");
        o_bool("different", a != b);
        o_bool("decode_encode_inverse", EncodePointer(DecodePointer(probe)) == probe);
        oracle_case_end();
    }
    {
        int all_ok = 1;
        for (ULONG_PTR v = 1; v; v <<= 1)
            if (DecodePointer(EncodePointer((void *)v)) != (void *)v) all_ok = 0;
        oracle_case("round_trip_every_bit");
        o_bool("all_ok", all_ok);
        oracle_case_end();
    }

    /* System pointers. */
    oracle_case("system_encode");
    o_bool("have_fns", pEncSys && pDecSys);
    if (pEncSys && pDecSys) {
        void *s1, *s2;
        SetLastError(ORACLE_SENTINEL);
        s1 = pEncSys(probe);
        oracle_last_error = GetLastError();
        s2 = pEncSys(probe);
        o_lasterr();
        o_bool("changes_value", s1 != probe);
        o_bool("stable", s1 == s2);
        o_bool("round_trip", pDecSys(s1) == probe);
        o_bool("differs_from_process_encoding", s1 != e1);
    }
    oracle_case_end();

    /* Remote versions on the current process. */
    oracle_case("remote_current_process");
    o_bool("have_fns", pEncRemote && pDecRemote);
    if (pEncRemote && pDecRemote) {
        void *r = NULL, *back = NULL;
        HRESULT h1, h2;
        SetLastError(ORACLE_SENTINEL);
        h1 = pEncRemote(GetCurrentProcess(), probe, &r);
        oracle_last_error = GetLastError();
        h2 = pDecRemote(GetCurrentProcess(), r, &back);
        o_hex32("encode_hr", (DWORD)h1);
        o_hex32("decode_hr", (DWORD)h2);
        o_lasterr();
        o_bool("matches_local", r == e1);
        o_bool("round_trip", back == probe);
    }
    oracle_case_end();
    oracle_case("remote_real_handle");
    if (pEncRemote) {
        HANDLE self = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, GetCurrentProcessId());
        void *r = NULL;
        HRESULT h = pEncRemote(self, probe, &r);
        o_hex32("hr", (DWORD)h);
        o_bool("matches_local", r == e1);
        CloseHandle(self);
        HANDLE weak = OpenProcess(SYNCHRONIZE, FALSE, GetCurrentProcessId());
        r = NULL;
        h = pEncRemote(weak, probe, &r);
        o_hex32("weak_handle_hr", (DWORD)h);
        CloseHandle(weak);
    }
    oracle_case_end();
    oracle_case("remote_bad_handle");
    if (pEncRemote) {
        void *r = (void *)1;
        HRESULT h = pEncRemote((HANDLE)(ULONG_PTR)0x1234, probe, &r);
        o_hex32("hr", (DWORD)h);
    }
    oracle_case_end();

    /* Another process uses another secret. */
    {
        wchar_t exe[MAX_PATH], cmd[MAX_PATH + 64];
        STARTUPINFOW si;
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
        PROCESS_INFORMATION pi;
        DWORD code = 0;
        GetModuleFileNameW(NULL, exe, MAX_PATH);
        swprintf(cmd, MAX_PATH + 64, L"\"%ls\" child %llx", exe, (unsigned long long)(ULONG_PTR)e1);
        BOOL ok = CreateProcessW(exe, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
        if (ok) {
            WaitForSingleObject(pi.hProcess, 10000);
            GetExitCodeProcess(pi.hProcess, &code);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
        oracle_case("child_process_differs");
        o_bool("created", ok);
        o_bool("child_encoding_differs", code == 2);
        oracle_case_end();
    }

    return oracle_end();
}
