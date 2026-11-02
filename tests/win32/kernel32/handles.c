/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: generic handle operations: CloseHandle, DuplicateHandle,
 * Get/SetHandleInformation, GetFileType, CompareObjectHandles and
 * pseudo-handles.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/handleapi/nf-handleapi-closehandle
 *       https://learn.microsoft.com/en-us/windows/win32/api/handleapi/nf-handleapi-duplicatehandle
 *       https://learn.microsoft.com/en-us/windows/win32/api/handleapi/nf-handleapi-gethandleinformation
 *       https://learn.microsoft.com/en-us/windows/win32/api/handleapi/nf-handleapi-sethandleinformation
 *       https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getfiletype
 *       https://learn.microsoft.com/en-us/windows/win32/api/handleapi/nf-handleapi-compareobjecthandles
 */
#include "oracle.h"

#define BOOL_CASE(name, expr)     \
    do {                          \
        BOOL ok_;                 \
        ORACLE_CALL(ok_, (expr)); \
        oracle_case(name);        \
        o_bool("ret", ok_);       \
        o_lasterr();              \
        oracle_case_end();        \
    } while (0)

static void info_case(const char *name, HANDLE h)
{
    DWORD flags = 0xffff;
    BOOL ok;
    ORACLE_CALL(ok, GetHandleInformation(h, &flags));
    oracle_case(name);
    o_bool("ret", ok);
    o_lasterr();
    if (ok) o_hex32("flags", flags);
    oracle_case_end();
}

static void type_case(const char *name, HANDLE h)
{
    DWORD t;
    ORACLE_CALL(t, GetFileType(h));
    oracle_case(name);
    o_u32("type", t);
    o_lasterr();
    oracle_case_end();
}

typedef BOOL(WINAPI *compare_fn)(HANDLE, HANDLE);

int main(void)
{
    oracle_begin("kernel32/handles");

    HANDLE proc = GetCurrentProcess();
    HANDLE f = CreateFileW(L"f", GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    HANDLE ev = CreateEventW(NULL, TRUE, FALSE, NULL);

    oracle_case("pseudo_handles");
    o_bool("process_is_minus_1", proc == (HANDLE)(LONG_PTR)-1);
    o_bool("thread_is_minus_2", GetCurrentThread() == (HANDLE)(LONG_PTR)-2);
    oracle_case_end();

    info_case("info_file", f);
    info_case("info_event", ev);
    info_case("info_null", NULL);
    info_case("info_bogus", (HANDLE)(ULONG_PTR)0x7ff0);
    info_case("info_pseudo_process", proc);

    BOOL_CASE("set_inherit", SetHandleInformation(f, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT));
    info_case("info_after_inherit", f);
    BOOL_CASE("set_protect", SetHandleInformation(f, HANDLE_FLAG_PROTECT_FROM_CLOSE,
                                                  HANDLE_FLAG_PROTECT_FROM_CLOSE));
    info_case("info_after_protect", f);
    BOOL_CASE("set_unknown_flag", SetHandleInformation(f, 0x100, 0x100));
    info_case("info_after_unknown_flag", f);
    BOOL_CASE("set_mask_zero", SetHandleInformation(f, 0, HANDLE_FLAG_INHERIT));
    BOOL_CASE("set_clear_inherit", SetHandleInformation(f, HANDLE_FLAG_INHERIT, 0));
    info_case("info_after_clear_inherit", f);

    {   /* DuplicateHandle */
        HANDLE d = NULL;
        BOOL ok;
        ORACLE_CALL(ok, DuplicateHandle(proc, f, proc, &d, 0, FALSE, DUPLICATE_SAME_ACCESS));
        oracle_case("dup_same_access");
        o_bool("ret", ok);
        o_lasterr();
        o_bool("different_value", d != f);
        oracle_case_end();
        info_case("info_dup_does_not_copy_flags", d);
        {
            compare_fn cmp = (compare_fn)(void *)GetProcAddress(GetModuleHandleW(L"kernelbase.dll"),
                                                                "CompareObjectHandles");
            oracle_case("compare_dup");
            o_bool("available", cmp != NULL);
            if (cmp) {
                o_bool("same_object", cmp(f, d));
                o_bool("different_object", cmp(f, ev));
            }
            oracle_case_end();
        }
        BOOL_CASE("close_dup", CloseHandle(d));
        BOOL_CASE("close_dup_twice", CloseHandle(d));

        d = NULL;
        ORACLE_CALL(ok, DuplicateHandle(proc, ev, proc, &d, SYNCHRONIZE, TRUE, 0));
        oracle_case("dup_reduced_access_inherit");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        info_case("info_dup_inherit", d);
        BOOL_CASE("set_event_via_sync_only_dup", SetEvent(d));
        CloseHandle(d);

        ORACLE_CALL(ok, DuplicateHandle(proc, (HANDLE)(ULONG_PTR)0x7ff0, proc, &d, 0, FALSE,
                                        DUPLICATE_SAME_ACCESS));
        oracle_case("dup_bogus_source");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();

        d = NULL;
        ORACLE_CALL(ok, DuplicateHandle(proc, proc, proc, &d, 0, FALSE, DUPLICATE_SAME_ACCESS));
        oracle_case("dup_pseudo_process");
        o_bool("ret", ok);
        o_lasterr();
        o_bool("real_handle", d != NULL && d != proc);
        oracle_case_end();
        type_case("type_process_handle", d);
        CloseHandle(d);

        HANDLE tmp;
        DuplicateHandle(proc, ev, proc, &tmp, 0, FALSE, DUPLICATE_SAME_ACCESS);
        d = NULL;
        ORACLE_CALL(ok, DuplicateHandle(proc, tmp, proc, &d, 0, FALSE,
                                        DUPLICATE_SAME_ACCESS | DUPLICATE_CLOSE_SOURCE));
        oracle_case("dup_close_source");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        BOOL_CASE("close_closed_source", CloseHandle(tmp));
        CloseHandle(d);
    }

    type_case("type_disk_file", f);
    type_case("type_event", ev);
    type_case("type_null", NULL);
    type_case("type_invalid", INVALID_HANDLE_VALUE);
    {
        HANDLE r, w;
        CreatePipe(&r, &w, NULL, 0);
        type_case("type_pipe", r);
        CloseHandle(r);
        CloseHandle(w);
    }
    {
        HANDLE nul = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                 OPEN_EXISTING, 0, NULL);
        oracle_case("open_nul");
        o_handle("ret", nul);
        oracle_case_end();
        type_case("type_nul", nul);
        CloseHandle(nul);
    }

    BOOL_CASE("close_protected", CloseHandle(f));
    info_case("info_after_close_protected", f);
    SetHandleInformation(f, HANDLE_FLAG_PROTECT_FROM_CLOSE, 0);
    BOOL_CASE("close_unprotected", CloseHandle(f));
    BOOL_CASE("close_again", CloseHandle(f));
    BOOL_CASE("close_null", CloseHandle(NULL));
    BOOL_CASE("close_bogus", CloseHandle((HANDLE)(ULONG_PTR)0x7ff0));
    BOOL_CASE("close_pseudo_process", CloseHandle(proc));
    BOOL_CASE("close_pseudo_thread", CloseHandle(GetCurrentThread()));
    BOOL_CASE("close_event", CloseHandle(ev));

    return oracle_end();
}
