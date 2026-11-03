/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: GetFileType on disk files, directories, devices, pipes,
 * mailslots and non-file handles.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getfiletype
 */
#include "oracle.h"

static const char *type_name(DWORD t)
{
    switch (t) {
    case FILE_TYPE_UNKNOWN: return "UNKNOWN";
    case FILE_TYPE_DISK: return "DISK";
    case FILE_TYPE_CHAR: return "CHAR";
    case FILE_TYPE_PIPE: return "PIPE";
    case FILE_TYPE_REMOTE: return "REMOTE";
    default: return "other";
    }
}

static void type_case(const char *name, HANDLE h)
{
    DWORD t;
    ORACLE_CALL(t, GetFileType(h));
    oracle_case(name);
    o_str("type", type_name(t));
    o_u32("raw", t);
    o_lasterr();
    oracle_case_end();
}

static void open_case(const char *name, const wchar_t *path, DWORD access, DWORD share, DWORD disp, DWORD flags)
{
    HANDLE h;
    ORACLE_CALL(h, CreateFileW(path, access, share, NULL, disp, flags, NULL));
    if (h == INVALID_HANDLE_VALUE) {
        oracle_case(name);
        o_handle("open", h);
        o_lasterr();
        oracle_case_end();
        return;
    }
    type_case(name, h);
    CloseHandle(h);
}

int main(void)
{
    oracle_begin("kernel32/GetFileType");
    const DWORD SH = FILE_SHARE_READ | FILE_SHARE_WRITE;

    open_case("disk_file", L"f.txt", GENERIC_READ | GENERIC_WRITE, SH, CREATE_ALWAYS, 0);
    open_case("disk_file_no_access", L"f.txt", 0, SH, OPEN_EXISTING, 0);
    open_case("disk_file_overlapped", L"f.txt", GENERIC_READ, SH, OPEN_EXISTING, FILE_FLAG_OVERLAPPED);
    CreateDirectoryW(L"d", NULL);
    open_case("directory", L"d", GENERIC_READ, SH, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS);
    open_case("nul_device", L"NUL", GENERIC_READ | GENERIC_WRITE, SH, OPEN_EXISTING, 0);
    open_case("nul_device_dotdot", L"\\\\.\\NUL", GENERIC_READ, SH, OPEN_EXISTING, 0);
    open_case("nul_with_extension", L"nul.txt", GENERIC_READ, SH, OPEN_EXISTING, 0);
    open_case("nul_in_subdir", L"d\\nul", GENERIC_READ, SH, OPEN_EXISTING, 0);

    {
        HANDLE r, w;
        CreatePipe(&r, &w, NULL, 0);
        type_case("anon_pipe_read", r);
        type_case("anon_pipe_write", w);
        CloseHandle(r);
        CloseHandle(w);
    }
    {
        HANDLE s = CreateNamedPipeW(L"\\\\.\\pipe\\setu_oracle_gft", PIPE_ACCESS_DUPLEX,
                                    PIPE_TYPE_BYTE | PIPE_WAIT, 1, 512, 512, 0, NULL);
        type_case("named_pipe_server", s);
        HANDLE c = CreateFileW(L"\\\\.\\pipe\\setu_oracle_gft", GENERIC_READ | GENERIC_WRITE, 0, NULL,
                               OPEN_EXISTING, 0, NULL);
        type_case("named_pipe_client", c);
        CloseHandle(c);
        CloseHandle(s);
    }
    {
        HANDLE s = CreateNamedPipeW(L"\\\\.\\pipe\\setu_oracle_gft_msg", PIPE_ACCESS_DUPLEX,
                                    PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT, 1, 512, 512, 0, NULL);
        type_case("named_pipe_message_server", s);
        CloseHandle(s);
    }
    {
        HANDLE m = CreateMailslotW(L"\\\\.\\mailslot\\setu_oracle_gft", 0, 0, NULL);
        type_case("mailslot_server", m);
        HANDLE c = CreateFileW(L"\\\\.\\mailslot\\setu_oracle_gft", GENERIC_WRITE, FILE_SHARE_READ, NULL,
                               OPEN_EXISTING, 0, NULL);
        type_case("mailslot_client", c);
        CloseHandle(c);
        CloseHandle(m);
    }

    {
        HANDLE ev = CreateEventW(NULL, FALSE, FALSE, NULL);
        type_case("event", ev);
        CloseHandle(ev);
        HANDLE mx = CreateMutexW(NULL, FALSE, NULL);
        type_case("mutex", mx);
        CloseHandle(mx);
        HANDLE f = CreateFileW(L"f.txt", GENERIC_READ, SH, NULL, OPEN_EXISTING, 0, NULL);
        HANDLE map = CreateFileMappingW(f, NULL, PAGE_READONLY, 0, 0, NULL);
        type_case("file_mapping_empty_file", map);
        if (map) CloseHandle(map);
        CloseHandle(f);
        HANDLE sec = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 4096, NULL);
        type_case("pagefile_section", sec);
        CloseHandle(sec);
        HANDLE port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1);
        type_case("completion_port", port);
        CloseHandle(port);
        HKEY k;
        RegOpenKeyExW(HKEY_CURRENT_USER, L"Software", 0, KEY_READ, &k);
        type_case("registry_key", (HANDLE)k);
        RegCloseKey(k);
        HANDLE tok;
        OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok);
        type_case("token", tok);
        CloseHandle(tok);
    }

    type_case("current_process", GetCurrentProcess());
    type_case("current_thread", GetCurrentThread());
    type_case("invalid_handle_value", INVALID_HANDLE_VALUE);
    type_case("null_handle", NULL);
    type_case("bogus_handle", (HANDLE)(ULONG_PTR)0x00fff000);
    {
        HANDLE f = CreateFileW(L"f.txt", GENERIC_READ, SH, NULL, OPEN_EXISTING, 0, NULL);
        CloseHandle(f);
        type_case("closed_handle", f);
    }
    {
        HANDLE f = CreateFileW(L"f.txt", GENERIC_READ, SH, NULL, OPEN_EXISTING, 0, NULL), d;
        DuplicateHandle(GetCurrentProcess(), f, GetCurrentProcess(), &d, 0, FALSE, DUPLICATE_SAME_ACCESS);
        type_case("duplicated_file_handle", d);
        CloseHandle(d);
        CloseHandle(f);
    }

    return oracle_end();
}
