/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: what successful calls do to the thread's last-error value.
 *
 * Apps call GetLastError() after calls that succeeded: CreateFileW with
 * OPEN_ALWAYS / CREATE_ALWAYS to learn whether the file existed, GetFileSize
 * and SetFilePointer when the result equals the error marker, TlsGetValue
 * when the slot holds NULL, GetFileType for FILE_TYPE_UNKNOWN. Others rely on
 * a successful call leaving an earlier error alone. Each case sets a sentinel,
 * makes one successful call and prints what the last error became
 * ("unchanged" if the sentinel survived).
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/errhandlingapi/nf-errhandlingapi-getlasterror
 *       https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew
 *       https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getfilesize
 *       https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-setfilepointer
 *       https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-tlsgetvalue
 *       https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getfiletype
 *       https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-getmodulefilenamew
 *       https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-createfilemappingw
 */
#include "oracle.h"

/* Print the last error of a call whose result is only "ok or not". */
#define OK_CASE(name, expr)                    \
    do {                                       \
        ULONG_PTR r_;                          \
        ORACLE_CALL(r_, (ULONG_PTR)(expr));    \
        oracle_case(name);                     \
        o_bool("ok", r_ != 0);                 \
        o_lasterr();                           \
        oracle_case_end();                     \
    } while (0)

static void handle_case(const char *name, HANDLE h, DWORD err)
{
    oracle_case(name);
    o_handle("ret", h);
    oracle_last_error = err;
    o_lasterr();
    oracle_case_end();
}

static DWORD WINAPI thread_proc(void *arg)
{
    (void)arg;
    return 5;
}

int main(void)
{
    oracle_begin("kernel32/last_error_success");

    HANDLE h;
    DWORD r;
    BOOL ok;

    /* ---- CreateFileW dispositions, new and existing ---- */
    ORACLE_CALL(h, CreateFileW(L"f1.txt", GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL));
    handle_case("createfile_create_new", h, oracle_last_error);
    CloseHandle(h);
    ORACLE_CALL(h, CreateFileW(L"f1.txt", GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL));
    handle_case("createfile_open_existing", h, oracle_last_error);
    CloseHandle(h);
    ORACLE_CALL(h, CreateFileW(L"f1.txt", GENERIC_WRITE, 0, NULL, OPEN_ALWAYS, 0, NULL));
    handle_case("createfile_open_always_exists", h, oracle_last_error);
    CloseHandle(h);
    ORACLE_CALL(h, CreateFileW(L"f2.txt", GENERIC_WRITE, 0, NULL, OPEN_ALWAYS, 0, NULL));
    handle_case("createfile_open_always_new", h, oracle_last_error);
    CloseHandle(h);
    ORACLE_CALL(h, CreateFileW(L"f1.txt", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL));
    handle_case("createfile_create_always_exists", h, oracle_last_error);
    CloseHandle(h);
    ORACLE_CALL(h, CreateFileW(L"f3.txt", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL));
    handle_case("createfile_create_always_new", h, oracle_last_error);
    CloseHandle(h);
    ORACLE_CALL(h, CreateFileW(L"f1.txt", GENERIC_WRITE, 0, NULL, TRUNCATE_EXISTING, 0, NULL));
    handle_case("createfile_truncate_existing", h, oracle_last_error);
    CloseHandle(h);
    CreateDirectoryW(L"d", NULL);
    ORACLE_CALL(h, CreateFileW(L"d", 0, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_FLAG_BACKUP_SEMANTICS, NULL));
    handle_case("createfile_open_always_directory", h, oracle_last_error);
    CloseHandle(h);

    /* ---- file I/O ---- */
    h = CreateFileW(L"io.bin", GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    {
        DWORD n;
        char data[16] = "0123456789abcdef";
        OK_CASE("writefile", WriteFile(h, data, sizeof data, &n, NULL));
        OK_CASE("flushfilebuffers", FlushFileBuffers(h));
        ORACLE_CALL(r, GetFileSize(h, NULL));
        oracle_case("getfilesize");
        o_u32("ret", r);
        o_lasterr();
        oracle_case_end();
        LARGE_INTEGER li;
        OK_CASE("getfilesizeex", GetFileSizeEx(h, &li));
        ORACLE_CALL(r, SetFilePointer(h, 0, NULL, FILE_BEGIN));
        oracle_case("setfilepointer_begin");
        o_u32("ret", r);
        o_lasterr();
        oracle_case_end();
        OK_CASE("readfile", ReadFile(h, data, 4, &n, NULL));
        SetFilePointer(h, 0, NULL, FILE_END);
        ORACLE_CALL(ok, ReadFile(h, data, 4, &n, NULL));
        oracle_case("readfile_at_eof");
        o_bool("ok", ok);
        o_u32("read", n);
        o_lasterr();
        oracle_case_end();
        /* A position whose low part is 0xFFFFFFFF: the documented "check
         * GetLastError" case of SetFilePointer. */
        LONG high = 0;
        ORACLE_CALL(r, SetFilePointer(h, (LONG)0xFFFFFFFF, &high, FILE_BEGIN));
        oracle_case("setfilepointer_low_ffffffff");
        o_hex32("ret", r);
        o_i64("high", high);
        o_lasterr();
        oracle_case_end();
        SetFilePointer(h, 16, NULL, FILE_BEGIN);
        OK_CASE("setendoffile", SetEndOfFile(h));
        BY_HANDLE_FILE_INFORMATION bhfi;
        OK_CASE("getfileinformationbyhandle", GetFileInformationByHandle(h, &bhfi));
        ORACLE_CALL(r, GetFileType(h));
        oracle_case("getfiletype_disk");
        o_u32("ret", r);
        o_lasterr();
        oracle_case_end();
        OK_CASE("closehandle_file", CloseHandle(h));
    }
    {
        /* A file mapping section has no file type: FILE_TYPE_UNKNOWN. */
        HANDLE m = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 4096, NULL);
        ORACLE_CALL(r, GetFileType(m));
        oracle_case("getfiletype_unknown_section");
        o_u32("ret", r);
        o_lasterr();
        oracle_case_end();
        CloseHandle(m);
        HANDLE rd, wr;
        CreatePipe(&rd, &wr, NULL, 0);
        ORACLE_CALL(r, GetFileType(rd));
        oracle_case("getfiletype_pipe");
        o_u32("ret", r);
        o_lasterr();
        oracle_case_end();
        DWORD avail = 7;
        OK_CASE("peeknamedpipe_empty", PeekNamedPipe(rd, NULL, 0, NULL, &avail, NULL));
        CloseHandle(rd);
        CloseHandle(wr);
        HANDLE nul = CreateFileW(L"NUL", GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        ORACLE_CALL(r, GetFileType(nul));
        oracle_case("getfiletype_nul");
        o_u32("ret", r);
        o_lasterr();
        oracle_case_end();
        CloseHandle(nul);
    }

    /* ---- name-space operations ---- */
    OK_CASE("getfileattributes", GetFileAttributesW(L"f1.txt") != INVALID_FILE_ATTRIBUTES);
    {
        WIN32_FILE_ATTRIBUTE_DATA fad;
        OK_CASE("getfileattributesex", GetFileAttributesExW(L"f1.txt", GetFileExInfoStandard, &fad));
    }
    OK_CASE("setfileattributes", SetFileAttributesW(L"f1.txt", FILE_ATTRIBUTE_NORMAL));
    OK_CASE("createdirectory", CreateDirectoryW(L"d2", NULL));
    OK_CASE("removedirectory", RemoveDirectoryW(L"d2"));
    OK_CASE("copyfile", CopyFileW(L"f1.txt", L"copy.txt", FALSE));
    OK_CASE("copyfile_overwrite", CopyFileW(L"f1.txt", L"copy.txt", FALSE));
    OK_CASE("movefile", MoveFileW(L"copy.txt", L"moved.txt"));
    OK_CASE("movefileex_replace", MoveFileExW(L"f2.txt", L"moved.txt", MOVEFILE_REPLACE_EXISTING));
    OK_CASE("deletefile", DeleteFileW(L"moved.txt"));
    {
        WIN32_FIND_DATAW fd;
        HANDLE f;
        ORACLE_CALL(f, FindFirstFileW(L"*", &fd));
        handle_case("findfirstfile", f, oracle_last_error);
        OK_CASE("findnextfile", FindNextFileW(f, &fd));
        while (FindNextFileW(f, &fd)) {}
        OK_CASE("findclose", FindClose(f));
    }
    {
        wchar_t buf[MAX_PATH];
        OK_CASE("getcurrentdirectory", GetCurrentDirectoryW(MAX_PATH, buf));
        OK_CASE("setcurrentdirectory", SetCurrentDirectoryW(buf));
        OK_CASE("getfullpathname", GetFullPathNameW(L"x", MAX_PATH, buf, NULL));
        OK_CASE("getlongpathname", GetLongPathNameW(L"f1.txt", buf, MAX_PATH));
        OK_CASE("gettemppath", GetTempPathW(MAX_PATH, buf));
        OK_CASE("getsystemdirectory", GetSystemDirectoryW(buf, MAX_PATH));
        OK_CASE("getwindowsdirectory", GetWindowsDirectoryW(buf, MAX_PATH));
    }

    /* ---- modules ---- */
    {
        wchar_t buf[MAX_PATH];
        HMODULE k32;
        ORACLE_CALL(k32, GetModuleHandleW(L"kernel32.dll"));
        oracle_case("getmodulehandle");
        o_bool("ok", k32 != NULL);
        o_lasterr();
        oracle_case_end();
        OK_CASE("getmodulefilename", GetModuleFileNameW(k32, buf, MAX_PATH));
        /* Truncation still "succeeds" (returns nSize). */
        ORACLE_CALL(r, GetModuleFileNameW(k32, buf, 4));
        oracle_case("getmodulefilename_truncated");
        o_u32("ret", r);
        o_lasterr();
        oracle_case_end();
        OK_CASE("getprocaddress", GetProcAddress(k32, "GetTickCount"));
        HMODULE lib;
        ORACLE_CALL(lib, LoadLibraryW(L"version.dll"));
        oracle_case("loadlibrary");
        o_bool("ok", lib != NULL);
        o_lasterr();
        oracle_case_end();
        OK_CASE("freelibrary", FreeLibrary(lib));
    }

    /* ---- sync objects ---- */
    {
        HANDLE e;
        ORACLE_CALL(e, CreateEventW(NULL, TRUE, FALSE, NULL));
        handle_case("createevent_unnamed", e, oracle_last_error);
        OK_CASE("setevent", SetEvent(e));
        ORACLE_CALL(r, WaitForSingleObject(e, 0));
        oracle_case("wait_signaled");
        o_u32("ret", r);
        o_lasterr();
        oracle_case_end();
        OK_CASE("resetevent", ResetEvent(e));
        ORACLE_CALL(r, WaitForSingleObject(e, 0));
        oracle_case("wait_timeout");
        o_u32("ret", r);
        o_lasterr();
        oracle_case_end();
        HANDLE dup;
        OK_CASE("duplicatehandle", DuplicateHandle(GetCurrentProcess(), e, GetCurrentProcess(), &dup, 0, FALSE,
                                                   DUPLICATE_SAME_ACCESS));
        DWORD flags;
        OK_CASE("gethandleinformation", GetHandleInformation(dup, &flags));
        OK_CASE("closehandle_event", CloseHandle(dup));
        CloseHandle(e);

        HANDLE m;
        ORACLE_CALL(m, CreateMutexW(NULL, TRUE, NULL));
        handle_case("createmutex_unnamed", m, oracle_last_error);
        OK_CASE("releasemutex", ReleaseMutex(m));
        CloseHandle(m);

        HANDLE s;
        ORACLE_CALL(s, CreateSemaphoreW(NULL, 0, 2, NULL));
        handle_case("createsemaphore_unnamed", s, oracle_last_error);
        LONG prev;
        OK_CASE("releasesemaphore", ReleaseSemaphore(s, 1, &prev));
        CloseHandle(s);

        HANDLE map;
        ORACLE_CALL(map, CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 4096, NULL));
        handle_case("createfilemapping_unnamed", map, oracle_last_error);
        void *view;
        ORACLE_CALL(view, MapViewOfFile(map, FILE_MAP_WRITE, 0, 0, 0));
        oracle_case("mapviewoffile");
        o_bool("ok", view != NULL);
        o_lasterr();
        oracle_case_end();
        OK_CASE("unmapviewoffile", UnmapViewOfFile(view));
        CloseHandle(map);

        CRITICAL_SECTION cs;
        InitializeCriticalSection(&cs);
        SetLastError(ORACLE_SENTINEL);
        EnterCriticalSection(&cs);
        LeaveCriticalSection(&cs);
        oracle_last_error = GetLastError();
        oracle_case("critical_section_enter_leave");
        o_lasterr();
        oracle_case_end();
        DeleteCriticalSection(&cs);
    }

    /* ---- threads, TLS ---- */
    {
        HANDLE t;
        ORACLE_CALL(t, CreateThread(NULL, 0, thread_proc, NULL, 0, NULL));
        handle_case("createthread", t, oracle_last_error);
        WaitForSingleObject(t, INFINITE);
        DWORD code;
        OK_CASE("getexitcodethread", GetExitCodeThread(t, &code));
        CloseHandle(t);

        DWORD idx;
        ORACLE_CALL(idx, TlsAlloc());
        oracle_case("tlsalloc");
        o_bool("ok", idx != TLS_OUT_OF_INDEXES);
        o_lasterr();
        oracle_case_end();
        void *v;
        ORACLE_CALL(v, TlsGetValue(idx));
        oracle_case("tlsgetvalue_null");
        o_bool("null", v == NULL);
        o_lasterr();
        oracle_case_end();
        OK_CASE("tlssetvalue", TlsSetValue(idx, (void *)1));
        ORACLE_CALL(v, TlsGetValue(idx));
        oracle_case("tlsgetvalue_set");
        o_bool("one", v == (void *)1);
        o_lasterr();
        oracle_case_end();
        OK_CASE("tlsfree", TlsFree(idx));
    }

    /* ---- memory ---- */
    {
        void *p;
        ORACLE_CALL(p, HeapAlloc(GetProcessHeap(), 0, 64));
        oracle_case("heapalloc");
        o_bool("ok", p != NULL);
        o_lasterr();
        oracle_case_end();
        OK_CASE("heapfree", HeapFree(GetProcessHeap(), 0, p));
        ORACLE_CALL(p, VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        oracle_case("virtualalloc");
        o_bool("ok", p != NULL);
        o_lasterr();
        oracle_case_end();
        OK_CASE("virtualfree", VirtualFree(p, 0, MEM_RELEASE));
        HLOCAL l;
        ORACLE_CALL(l, LocalAlloc(LMEM_FIXED, 16));
        oracle_case("localalloc");
        o_bool("ok", l != NULL);
        o_lasterr();
        oracle_case_end();
        HLOCAL lr;
        ORACLE_CALL(lr, LocalFree(l));
        oracle_case("localfree");
        o_bool("ret_null", lr == NULL);
        o_lasterr();
        oracle_case_end();
    }

    /* ---- environment, strings, misc ---- */
    {
        wchar_t buf[64];
        OK_CASE("setenvironmentvariable", SetEnvironmentVariableW(L"SETU_LE", L"v"));
        OK_CASE("getenvironmentvariable", GetEnvironmentVariableW(L"SETU_LE", buf, 64));
        OK_CASE("expandenvironmentstrings", ExpandEnvironmentStringsW(L"%SETU_LE%", buf, 64));
        OK_CASE("setenvironmentvariable_delete", SetEnvironmentVariableW(L"SETU_LE", NULL));
        OK_CASE("multibytetowidechar", MultiByteToWideChar(CP_ACP, 0, "abc", -1, buf, 64));
        char a[16];
        OK_CASE("widechartomultibyte", WideCharToMultiByte(CP_ACP, 0, L"abc", -1, a, 16, NULL, NULL));
        OK_CASE("getstdhandle", GetStdHandle(STD_OUTPUT_HANDLE) != INVALID_HANDLE_VALUE);
        LARGE_INTEGER li;
        OK_CASE("queryperformancecounter", QueryPerformanceCounter(&li));
        OSVERSIONINFOW vi = { sizeof vi, 0, 0, 0, 0, {0} };
        OK_CASE("getversionex", GetVersionExW(&vi));
        SYSTEMTIME st;
        SetLastError(ORACLE_SENTINEL);
        GetSystemTime(&st);
        oracle_last_error = GetLastError();
        oracle_case("getsystemtime");
        o_lasterr();
        oracle_case_end();
        SetLastError(ORACLE_SENTINEL);
        Sleep(0);
        oracle_last_error = GetLastError();
        oracle_case("sleep_0");
        o_lasterr();
        oracle_case_end();
        OK_CASE("formatmessage", FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL,
                                                ERROR_FILE_NOT_FOUND, MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL),
                                                buf, 64, NULL));
        OK_CASE("openprocess_self", OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, GetCurrentProcessId()));
    }

    return oracle_end();
}
