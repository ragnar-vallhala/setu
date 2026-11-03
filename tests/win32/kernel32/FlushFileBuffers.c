/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: FlushFileBuffers on files opened with different access
 * rights and flags, directories, devices, pipes and non-file handles.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-flushfilebuffers
 */
#include "oracle.h"

static void flush_case(const char *name, HANDLE h)
{
    BOOL r;
    ORACLE_CALL(r, FlushFileBuffers(h));
    oracle_case(name);
    o_bool("ret", r);
    o_lasterr();
    oracle_case_end();
}

static void open_flush(const char *name, const wchar_t *path, DWORD access, DWORD disp, DWORD flags)
{
    HANDLE h;
    ORACLE_CALL(h, CreateFileW(path, access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, disp,
                               flags, NULL));
    if (h == INVALID_HANDLE_VALUE) {
        oracle_case(name);
        o_handle("open", h);
        o_lasterr();
        oracle_case_end();
        return;
    }
    flush_case(name, h);
    CloseHandle(h);
}

int main(void)
{
    oracle_begin("kernel32/FlushFileBuffers");
    DWORD n;

    {
        HANDLE h = CreateFileW(L"f.txt", GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        flush_case("fresh_empty_file", h);
        WriteFile(h, "hello", 5, &n, NULL);
        flush_case("after_write", h);
        flush_case("twice", h);
        CloseHandle(h);
    }
    open_flush("generic_read", L"f.txt", GENERIC_READ, OPEN_EXISTING, 0);
    open_flush("generic_write", L"f.txt", GENERIC_WRITE, OPEN_EXISTING, 0);
    open_flush("file_write_data", L"f.txt", FILE_WRITE_DATA, OPEN_EXISTING, 0);
    open_flush("file_append_data", L"f.txt", FILE_APPEND_DATA, OPEN_EXISTING, 0);
    open_flush("file_write_attributes", L"f.txt", FILE_WRITE_ATTRIBUTES, OPEN_EXISTING, 0);
    open_flush("read_attributes_only", L"f.txt", FILE_READ_ATTRIBUTES, OPEN_EXISTING, 0);
    open_flush("no_access", L"f.txt", 0, OPEN_EXISTING, 0);
    open_flush("write_through", L"f.txt", GENERIC_WRITE, OPEN_EXISTING, FILE_FLAG_WRITE_THROUGH);
    open_flush("no_buffering", L"f.txt", GENERIC_WRITE, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING);
    open_flush("overlapped", L"f.txt", GENERIC_WRITE, OPEN_EXISTING, FILE_FLAG_OVERLAPPED);
    open_flush("delete_on_close", L"g.txt", GENERIC_WRITE | DELETE, CREATE_ALWAYS, FILE_FLAG_DELETE_ON_CLOSE);

    CreateDirectoryW(L"d", NULL);
    open_flush("directory_read", L"d", GENERIC_READ, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS);
    open_flush("directory_write", L"d", GENERIC_WRITE, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS);
    open_flush("nul_read", L"NUL", GENERIC_READ, OPEN_EXISTING, 0);
    open_flush("nul_write", L"NUL", GENERIC_WRITE, OPEN_EXISTING, 0);

    {
        HANDLE r, w;
        CreatePipe(&r, &w, NULL, 0);
        flush_case("anon_pipe_write_empty", w);
        flush_case("anon_pipe_read_end", r);
        CloseHandle(r);
        flush_case("anon_pipe_write_reader_closed", w);
        CloseHandle(w);
    }
    {
        HANDLE s = CreateNamedPipeW(L"\\\\.\\pipe\\setu_oracle_ffb", PIPE_ACCESS_DUPLEX,
                                    PIPE_TYPE_BYTE | PIPE_WAIT, 1, 512, 512, 0, NULL);
        flush_case("named_pipe_server_unconnected", s);
        HANDLE c = CreateFileW(L"\\\\.\\pipe\\setu_oracle_ffb", GENERIC_READ | GENERIC_WRITE, 0, NULL,
                               OPEN_EXISTING, 0, NULL);
        flush_case("named_pipe_client_empty", c);
        flush_case("named_pipe_server_connected", s);
        CloseHandle(c);
        flush_case("named_pipe_server_client_gone", s);
        CloseHandle(s);
    }

    {
        HANDLE ev = CreateEventW(NULL, FALSE, FALSE, NULL);
        flush_case("event_handle", ev);
        CloseHandle(ev);
        HANDLE sec = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 4096, NULL);
        flush_case("section_handle", sec);
        CloseHandle(sec);
    }
    flush_case("invalid_handle_value", INVALID_HANDLE_VALUE);
    flush_case("null_handle", NULL);
    flush_case("current_process", GetCurrentProcess());
    {
        HANDLE h = CreateFileW(L"f.txt", GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        CloseHandle(h);
        flush_case("closed_handle", h);
    }

    {
        HANDLE h = CreateFileW(L"f.txt", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        HANDLE d = NULL;
        BOOL dup;
        ORACLE_CALL(dup, DuplicateHandle(GetCurrentProcess(), h, GetCurrentProcess(), &d, GENERIC_WRITE, FALSE, 0));
        oracle_case("duplicate_read_handle_for_write");
        o_bool("ret", dup);
        o_lasterr();
        oracle_case_end();
        if (dup) {
            flush_case("duplicated_with_write", d);
            CloseHandle(d);
        }
        CloseHandle(h);
    }

    oracle_case("file_content");
    o_file_state("f", L"f.txt");
    o_file_state("g", L"g.txt");
    oracle_case_end();

    return oracle_end();
}
