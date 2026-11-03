/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: GetFileTime / SetFileTime. Prints only times this test set
 * itself, or relationships (before/after) between times, never the clock.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getfiletime
 *       https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-setfiletime
 *       https://learn.microsoft.com/en-us/windows/win32/sysinfo/file-times
 */
#include "oracle.h"

static FILETIME ft(unsigned long long v)
{
    FILETIME f;
    f.dwLowDateTime = (DWORD)v;
    f.dwHighDateTime = (DWORD)(v >> 32);
    return f;
}
static unsigned long long u(FILETIME f) { return ((unsigned long long)f.dwHighDateTime << 32) | f.dwLowDateTime; }

/* 2001-02-03 04:05:06.7891234 UTC and two later values. */
#define T1 126254331067891234ULL
#define T2 127000000000000001ULL
#define T3 128000000000000000ULL

/* Print a file time that the test set itself as its value (T1, T2, T3, 0 or
 * the high-bit value), and any other time as "other": a time the system
 * filled in from the clock differs between runs. */
static void o_time(const char *name, unsigned long long v)
{
    if (v == T1 || v == T2 || v == T3 || v == 0 || v == 0x8000000000000000ULL)
        o_u64(name, v);
    else
        o_str(name, "other");
}

static void make_file(const wchar_t *path, const char *content)
{
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    DWORD w;
    WriteFile(h, content, (DWORD)strlen(content), &w, NULL);
    CloseHandle(h);
}

static void times_of(const wchar_t *path, unsigned long long *c, unsigned long long *a, unsigned long long *w)
{
    WIN32_FILE_ATTRIBUTE_DATA fad;
    *c = *a = *w = 0;
    if (GetFileAttributesExW(path, GetFileExInfoStandard, &fad)) {
        *c = u(fad.ftCreationTime);
        *a = u(fad.ftLastAccessTime);
        *w = u(fad.ftLastWriteTime);
    }
}

static void set_case(const char *name, HANDLE h, const FILETIME *c, const FILETIME *a, const FILETIME *w)
{
    BOOL r;
    ORACLE_CALL(r, SetFileTime(h, c, a, w));
    oracle_case(name);
    o_bool("ret", r);
    o_lasterr();
    oracle_case_end();
}

int main(void)
{
    oracle_begin("kernel32/FileTime");
    BOOL r;
    FILETIME c, a, w, t1 = ft(T1), t2 = ft(T2), t3 = ft(T3), zero = ft(0);
    unsigned long long fc, fa, fw;

    make_file(L"f.txt", "data");
    HANDLE h = CreateFileW(L"f.txt", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);

    ORACLE_CALL(r, GetFileTime(h, &c, &a, &w));
    oracle_case("get_fresh");
    o_bool("ret", r);
    o_lasterr();
    o_bool("creation_nonzero", u(c) != 0);
    o_bool("write_ge_creation", u(w) >= u(c));
    o_bool("after_2020", u(w) > 132223104000000000ULL);
    oracle_case_end();

    ORACLE_CALL(r, GetFileTime(h, NULL, NULL, NULL));
    oracle_case("get_all_null");
    o_bool("ret", r);
    o_lasterr();
    oracle_case_end();

    set_case("set_write_only", h, NULL, NULL, &t1);
    GetFileTime(h, &c, &a, &w);
    oracle_case("after_set_write_only");
    o_time("write", u(w));
    o_bool("creation_changed", u(c) == T1);
    oracle_case_end();

    set_case("set_all", h, &t1, &t2, &t3);
    GetFileTime(h, &c, &a, &w);
    oracle_case("after_set_all");
    o_time("creation", u(c));
    o_time("access", u(a));
    o_time("write", u(w));
    oracle_case_end();

    times_of(L"f.txt", &fc, &fa, &fw);
    oracle_case("after_set_all_by_path");
    o_time("creation", fc);
    o_time("access", fa);
    o_time("write", fw);
    oracle_case_end();

    {
        WIN32_FIND_DATAW fd;
        HANDLE f = FindFirstFileW(L"f.txt", &fd);
        oracle_case("after_set_all_by_find");
        o_time("creation", u(fd.ftCreationTime));
        o_time("write", u(fd.ftLastWriteTime));
        oracle_case_end();
        FindClose(f);
    }

    set_case("set_all_null", h, NULL, NULL, NULL);
    set_case("set_zero_write", h, NULL, NULL, &zero);
    GetFileTime(h, &c, &a, &w);
    oracle_case("after_set_zero_write");
    o_time("write", u(w));
    oracle_case_end();

    {
        FILETIME big = ft(0x8000000000000000ULL);
        set_case("set_write_high_bit", h, NULL, NULL, &big);
        GetFileTime(h, &c, &a, &w);
        oracle_case("after_set_write_high_bit");
        o_time("write", u(w));
        oracle_case_end();
    }

    /* -1: stop updating on later I/O through this handle. */
    {
        FILETIME m1 = ft(0xFFFFFFFFFFFFFFFFULL);
        set_case("set_write_minus_one", h, NULL, NULL, &m1);
        GetFileTime(h, &c, &a, &w);
        oracle_case("after_set_minus_one");
        o_time("write", u(w));
        oracle_case_end();
        DWORD n;
        SetFilePointer(h, 0, NULL, FILE_END);
        WriteFile(h, "more", 4, &n, NULL);
        FlushFileBuffers(h);
        GetFileTime(h, &c, &a, &w);
        oracle_case("write_after_minus_one");
        o_time("write", u(w));
        oracle_case_end();
    }
    CloseHandle(h);
    times_of(L"f.txt", &fc, &fa, &fw);
    oracle_case("after_close_minus_one");
    o_time("write", fw);
    oracle_case_end();

    /* A plain write moves the time forward at the latest when the handle closes. */
    h = CreateFileW(L"f.txt", GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    SetFileTime(h, NULL, NULL, &t1);
    {
        DWORD n;
        WriteFile(h, "x", 1, &n, NULL);
    }
    CloseHandle(h);
    times_of(L"f.txt", &fc, &fa, &fw);
    oracle_case("write_updates_time_after_close");
    o_bool("write_after_t1", fw > T1);
    o_time("creation", fc);
    oracle_case_end();

    /* Setting the time is the last thing before close: it sticks. */
    h = CreateFileW(L"f.txt", GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    {
        DWORD n;
        WriteFile(h, "y", 1, &n, NULL);
    }
    SetFileTime(h, NULL, NULL, &t2);
    CloseHandle(h);
    times_of(L"f.txt", &fc, &fa, &fw);
    oracle_case("set_after_write_sticks");
    o_time("write", fw);
    oracle_case_end();

    /* Access rights. */
    h = CreateFileW(L"f.txt", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    set_case("set_on_read_only_handle", h, NULL, NULL, &t3);
    ORACLE_CALL(r, GetFileTime(h, &c, &a, &w));
    oracle_case("get_on_read_handle");
    o_bool("ret", r);
    o_lasterr();
    o_time("write", u(w));
    oracle_case_end();
    CloseHandle(h);

    h = CreateFileW(L"f.txt", FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    set_case("set_on_write_attributes_handle", h, NULL, NULL, &t3);
    ORACLE_CALL(r, GetFileTime(h, &c, &a, &w));
    oracle_case("get_on_write_attributes_handle");
    o_bool("ret", r);
    o_lasterr();
    oracle_case_end();
    CloseHandle(h);

    h = CreateFileW(L"f.txt", FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    ORACLE_CALL(r, GetFileTime(h, &c, &a, &w));
    oracle_case("get_on_read_attributes_handle");
    o_bool("ret", r);
    o_lasterr();
    o_time("write", u(w));
    oracle_case_end();
    CloseHandle(h);

    h = CreateFileW(L"f.txt", 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    ORACLE_CALL(r, GetFileTime(h, &c, &a, &w));
    oracle_case("get_on_no_access_handle");
    o_bool("ret", r);
    o_lasterr();
    oracle_case_end();
    CloseHandle(h);

    /* Directories. */
    CreateDirectoryW(L"d", NULL);
    h = CreateFileW(L"d", FILE_WRITE_ATTRIBUTES | FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                    OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    set_case("set_directory", h, &t1, &t2, &t3);
    GetFileTime(h, &c, &a, &w);
    oracle_case("after_set_directory");
    o_time("creation", u(c));
    o_time("access", u(a));
    o_time("write", u(w));
    oracle_case_end();
    CloseHandle(h);

    make_file(L"d\\child", "c");
    times_of(L"d", &fc, &fa, &fw);
    oracle_case("dir_write_time_after_child_create");
    o_bool("write_changed", fw != T3);
    o_time("creation", fc);
    oracle_case_end();

    /* Bad handles. */
    ORACLE_CALL(r, GetFileTime(INVALID_HANDLE_VALUE, &c, &a, &w));
    oracle_case("get_invalid_handle");
    o_bool("ret", r);
    o_lasterr();
    oracle_case_end();
    set_case("set_invalid_handle", INVALID_HANDLE_VALUE, NULL, NULL, &t1);
    {
        HANDLE ev = CreateEventW(NULL, FALSE, FALSE, NULL);
        ORACLE_CALL(r, GetFileTime(ev, &c, &a, &w));
        oracle_case("get_event_handle");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        CloseHandle(ev);
    }

    /* CompareFileTime. */
    {
        LONG cr;
        ORACLE_CALL(cr, CompareFileTime(&t1, &t2));
        oracle_case("compare_less");
        o_i64("ret", cr);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(cr, CompareFileTime(&t3, &t2));
        oracle_case("compare_greater");
        o_i64("ret", cr);
        oracle_case_end();
        ORACLE_CALL(cr, CompareFileTime(&t2, &t2));
        oracle_case("compare_equal");
        o_i64("ret", cr);
        oracle_case_end();
        FILETIME hi = ft(0x8000000000000000ULL);
        ORACLE_CALL(cr, CompareFileTime(&hi, &t1));
        oracle_case("compare_high_bit");
        o_i64("ret", cr);
        oracle_case_end();
    }

    return oracle_end();
}
