/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: FindFirstChangeNotificationW / FindNextChangeNotification and
 * synchronous ReadDirectoryChangesW. Every wait has a timeout, and a
 * watchdog cancels a synchronous read that never completes.
 *
 * ReadDirectoryChangesW batches are made deterministic by arming the watch
 * with a first read, then making all changes, then reading once: the changes
 * are already queued when the second read is issued.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-findfirstchangenotificationw
 *       https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-findnextchangenotification
 *       https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-readdirectorychangesw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-file_notify_information
 */
#include "oracle.h"

static void make_file(const wchar_t *path)
{
    CloseHandle(CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL));
}

static const char *wait_name(DWORD w)
{
    switch (w) {
    case WAIT_OBJECT_0: return "signaled";
    case WAIT_TIMEOUT: return "timeout";
    case WAIT_FAILED: return "failed";
    default: return "other";
    }
}

static void fcn_case(const char *name, const wchar_t *dir, BOOL subtree, DWORD filter)
{
    HANDLE h;
    ORACLE_CALL(h, FindFirstChangeNotificationW(dir, subtree, filter));
    oracle_case(name);
    o_handle("ret", h);
    o_lasterr();
    oracle_case_end();
    if (h != INVALID_HANDLE_VALUE) FindCloseChangeNotification(h);
}

/* Watch "w", run change(), report whether the handle got signaled. */
static void signal_case(const char *name, BOOL subtree, DWORD filter, void (*change)(void))
{
    HANDLE h = FindFirstChangeNotificationW(L"w", subtree, filter);
    DWORD before = WaitForSingleObject(h, 0);
    change();
    DWORD after = WaitForSingleObject(h, 2000);
    oracle_case(name);
    o_str("before", wait_name(before));
    o_str("after", wait_name(after));
    BOOL r;
    ORACLE_CALL(r, FindNextChangeNotification(h));
    o_bool("next_ret", r);
    o_lasterr();
    o_str("after_next", wait_name(WaitForSingleObject(h, 0)));
    oracle_case_end();
    FindCloseChangeNotification(h);
}

static void ch_create(void) { make_file(L"w\\new1.txt"); }
static void ch_create_nested(void) { make_file(L"w\\sub\\nested.txt"); }
static void ch_attr(void) { SetFileAttributesW(L"w\\new1.txt", FILE_ATTRIBUTE_HIDDEN); }
static void ch_size(void)
{
    HANDLE f = CreateFileW(L"w\\new1.txt", GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    DWORD n;
    WriteFile(f, "grow", 4, &n, NULL);
    CloseHandle(f);
}
static void ch_mkdir(void) { CreateDirectoryW(L"w\\newdir", NULL); }
static void ch_rename(void) { MoveFileW(L"w\\new1.txt", L"w\\renamed.txt"); }
static void ch_outside(void) { make_file(L"outside.txt"); }
static void ch_none(void) {}

/* ---- ReadDirectoryChangesW ---- */

static HANDLE g_dir;
static HANDLE g_main_thread;

static DWORD WINAPI watchdog(void *arg)
{
    if (WaitForSingleObject((HANDLE)arg, 5000) == WAIT_TIMEOUT) CancelSynchronousIo(g_main_thread);
    return 0;
}

static void print_records(const BYTE *buf, DWORD bytes)
{
    char out[2048] = "";
    DWORD off = 0, n = 0;
    if (bytes) {
        for (;;) {
            const FILE_NOTIFY_INFORMATION *fni = (const FILE_NOTIFY_INFORMATION *)(buf + off);
            wchar_t name[MAX_PATH];
            char item[MAX_PATH * 2];
            DWORD len = fni->FileNameLength / sizeof(WCHAR);
            if (len >= MAX_PATH) len = MAX_PATH - 1;
            memcpy(name, fni->FileName, len * sizeof(WCHAR));
            name[len] = 0;
            snprintf(item, sizeof item, "%s%lu:%s", n ? "," : "", (unsigned long)fni->Action, oracle_utf8(name));
            strncat(out, item, sizeof out - strlen(out) - 1);
            n++;
            if (!fni->NextEntryOffset) break;
            off += fni->NextEntryOffset;
        }
    }
    o_str("records", out);
    o_u32("count", n);
}

/* Synchronous read guarded by a watchdog. */
static BOOL rdc(BYTE *buf, DWORD size, BOOL subtree, DWORD filter, DWORD *bytes)
{
    HANDLE done = CreateEventW(NULL, TRUE, FALSE, NULL);
    HANDLE wd = CreateThread(NULL, 0, watchdog, done, 0, NULL);
    BOOL r;
    *bytes = 0xdead;
    ORACLE_CALL(r, ReadDirectoryChangesW(g_dir, buf, size, subtree, filter, bytes, NULL, NULL));
    SetEvent(done);
    WaitForSingleObject(wd, INFINITE);
    CloseHandle(wd);
    CloseHandle(done);
    return r;
}

struct arm { DWORD filter; BOOL subtree; BOOL ret; DWORD bytes; DWORD err; BYTE buf[4096]; };

static DWORD WINAPI arm_thread(void *p)
{
    struct arm *a = p;
    SetLastError(0);
    a->ret = ReadDirectoryChangesW(g_dir, a->buf, sizeof a->buf, a->subtree, a->filter, &a->bytes, NULL, NULL);
    a->err = GetLastError();
    return 0;
}

/* Arms a watch on "r" by blocking in a first read on another thread, then
 * releases it with one change to "r\\arm<k>". */
static void arm_watch(const char *name, BOOL subtree, DWORD filter, int k)
{
    static struct arm a;
    wchar_t p[32];
    memset(&a, 0, sizeof a);
    a.filter = filter;
    a.subtree = subtree;
    HANDLE t = CreateThread(NULL, 0, arm_thread, &a, 0, NULL);
    Sleep(300); /* let the first read reach the file system */
    swprintf(p, 32, L"r\\arm%d", k);
    CreateDirectoryW(p, NULL);
    DWORD w = WaitForSingleObject(t, 5000);
    if (w == WAIT_TIMEOUT) {
        CancelIoEx(g_dir, NULL);
        WaitForSingleObject(t, 5000);
    }
    CloseHandle(t);
    oracle_case(name);
    o_str("wait", wait_name(w));
    o_bool("ret", a.ret);
    print_records(a.buf, a.ret ? a.bytes : 0);
    oracle_case_end();
}

int main(void)
{
    oracle_begin("kernel32/change_notifications");
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &g_main_thread, 0, FALSE,
                    DUPLICATE_SAME_ACCESS);
    CreateDirectoryW(L"w", NULL);
    CreateDirectoryW(L"w\\sub", NULL);
    make_file(L"f.txt");

    /* Parameter checks. */
    fcn_case("fcn_valid", L"w", FALSE, FILE_NOTIFY_CHANGE_FILE_NAME);
    fcn_case("fcn_missing_dir", L"nope", FALSE, FILE_NOTIFY_CHANGE_FILE_NAME);
    fcn_case("fcn_missing_parent", L"nope\\deeper", FALSE, FILE_NOTIFY_CHANGE_FILE_NAME);
    fcn_case("fcn_on_file", L"f.txt", FALSE, FILE_NOTIFY_CHANGE_FILE_NAME);
    fcn_case("fcn_zero_filter", L"w", FALSE, 0);
    fcn_case("fcn_unknown_filter_bit", L"w", FALSE, 0x1000);
    fcn_case("fcn_all_filters", L"w", TRUE, 0x17f);
    fcn_case("fcn_empty_path", L"", FALSE, FILE_NOTIFY_CHANGE_FILE_NAME);
    fcn_case("fcn_trailing_backslash", L"w\\", FALSE, FILE_NOTIFY_CHANGE_FILE_NAME);

    /* Signaling. */
    signal_case("signal_create_file_name", FALSE, FILE_NOTIFY_CHANGE_FILE_NAME, ch_create);
    signal_case("signal_nested_no_subtree", FALSE, FILE_NOTIFY_CHANGE_FILE_NAME, ch_create_nested);
    DeleteFileW(L"w\\sub\\nested.txt");
    signal_case("signal_nested_subtree", TRUE, FILE_NOTIFY_CHANGE_FILE_NAME, ch_create_nested);
    signal_case("signal_attr_with_name_filter", FALSE, FILE_NOTIFY_CHANGE_FILE_NAME, ch_attr);
    signal_case("signal_attr_with_attr_filter", FALSE, FILE_NOTIFY_CHANGE_ATTRIBUTES, ch_attr);
    signal_case("signal_size_with_size_filter", FALSE, FILE_NOTIFY_CHANGE_SIZE, ch_size);
    signal_case("signal_mkdir_with_file_filter", FALSE, FILE_NOTIFY_CHANGE_FILE_NAME, ch_mkdir);
    RemoveDirectoryW(L"w\\newdir");
    signal_case("signal_mkdir_with_dir_filter", FALSE, FILE_NOTIFY_CHANGE_DIR_NAME, ch_mkdir);
    signal_case("signal_rename", FALSE, FILE_NOTIFY_CHANGE_FILE_NAME, ch_rename);
    signal_case("signal_outside_change", TRUE, FILE_NOTIFY_CHANGE_FILE_NAME, ch_outside);
    signal_case("signal_nothing", TRUE, 0x17f, ch_none);

    {
        HANDLE h = FindFirstChangeNotificationW(L"w", FALSE, FILE_NOTIFY_CHANGE_FILE_NAME);
        BOOL r;
        ORACLE_CALL(r, FindCloseChangeNotification(h));
        oracle_case("close_valid");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, FindCloseChangeNotification(INVALID_HANDLE_VALUE));
        oracle_case("close_invalid");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(r, FindNextChangeNotification(INVALID_HANDLE_VALUE));
        oracle_case("next_invalid");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
    }

    /* ReadDirectoryChangesW. */
    CreateDirectoryW(L"r", NULL);
    g_dir = CreateFileW(L"r", FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    const DWORD NAMES = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME;
    {
        BYTE buf[4096];
        DWORD bytes;
        BOOL r;
        ORACLE_CALL(r, ReadDirectoryChangesW(g_dir, buf, sizeof buf, FALSE, 0, &bytes, NULL, NULL));
        oracle_case("rdc_zero_filter");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        HANDLE f = CreateFileW(L"f.txt", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        ORACLE_CALL(r, ReadDirectoryChangesW(f, buf, sizeof buf, FALSE, NAMES, &bytes, NULL, NULL));
        oracle_case("rdc_on_file_handle");
        o_bool("ret", r);
        o_lasterr();
        oracle_case_end();
        CloseHandle(f);
    }

    arm_watch("rdc_arm_names", FALSE, NAMES, 0);
    {
        BYTE buf[4096];
        DWORD bytes;
        BOOL r;
        make_file(L"r\\one.txt");
        make_file(L"r\\two.txt");
        MoveFileW(L"r\\one.txt", L"r\\uno.txt");
        DeleteFileW(L"r\\two.txt");
        CreateDirectoryW(L"r\\dir", NULL);
        RemoveDirectoryW(L"r\\arm0");
        r = rdc(buf, sizeof buf, FALSE, NAMES, &bytes);
        oracle_case("rdc_batch_names");
        o_bool("ret", r);
        o_lasterr();
        print_records(buf, r ? bytes : 0);
        oracle_case_end();

        /* Nested change while the watch is not recursive: nothing queued
         * for it; the next change ends the read. */
        make_file(L"r\\dir\\nested.txt");
        make_file(L"r\\three.txt");
        r = rdc(buf, sizeof buf, FALSE, NAMES, &bytes);
        oracle_case("rdc_nested_not_reported");
        o_bool("ret", r);
        print_records(buf, r ? bytes : 0);
        oracle_case_end();

        /* Overflow: a buffer too small for the queued records. */
        make_file(L"r\\overflow_a_long_name_1.txt");
        make_file(L"r\\overflow_a_long_name_2.txt");
        make_file(L"r\\overflow_a_long_name_3.txt");
        ORACLE_CALL(r, ReadDirectoryChangesW(g_dir, buf, 16, FALSE, NAMES, &bytes, NULL, NULL));
        oracle_case("rdc_overflow_small_buffer");
        o_bool("ret", r);
        o_lasterr();
        o_u32("bytes", bytes);
        oracle_case_end();
    }
    CloseHandle(g_dir);

    /* Recursive watch on a fresh handle. */
    g_dir = CreateFileW(L"r", FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    arm_watch("rdc_arm_subtree", TRUE, NAMES, 1);
    {
        BYTE buf[4096];
        DWORD bytes;
        BOOL r;
        make_file(L"r\\dir\\deep.txt");
        DeleteFileW(L"r\\dir\\deep.txt");
        MoveFileW(L"r\\uno.txt", L"r\\dir\\moved_in.txt");
        r = rdc(buf, sizeof buf, TRUE, NAMES, &bytes);
        oracle_case("rdc_batch_subtree");
        o_bool("ret", r);
        o_lasterr();
        print_records(buf, r ? bytes : 0);
        oracle_case_end();
    }
    CloseHandle(g_dir);

    /* Attribute filter only. */
    g_dir = CreateFileW(L"r", FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    {
        static struct arm a;
        memset(&a, 0, sizeof a);
        a.filter = FILE_NOTIFY_CHANGE_ATTRIBUTES;
        HANDLE t = CreateThread(NULL, 0, arm_thread, &a, 0, NULL);
        Sleep(300);
        make_file(L"r\\ignored_by_attr_filter.txt");
        SetFileAttributesW(L"r\\three.txt", FILE_ATTRIBUTE_HIDDEN);
        DWORD w = WaitForSingleObject(t, 5000);
        if (w == WAIT_TIMEOUT) {
            CancelIoEx(g_dir, NULL);
            WaitForSingleObject(t, 5000);
        }
        CloseHandle(t);
        oracle_case("rdc_attribute_filter");
        o_str("wait", wait_name(w));
        o_bool("ret", a.ret);
        print_records(a.buf, a.ret ? a.bytes : 0);
        oracle_case_end();
    }
    CloseHandle(g_dir);

    return oracle_end();
}
