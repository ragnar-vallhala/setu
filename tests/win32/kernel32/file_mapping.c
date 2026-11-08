/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: file mappings: CreateFileMappingW / OpenFileMappingW /
 * MapViewOfFile(Ex) / UnmapViewOfFile / FlushViewOfFile: sizes, protection
 * and access checks, alignment, coherence with ReadFile/WriteFile, copy-on-
 * write views, named sections, and the file operations a mapping blocks.
 *
 * Addresses are never printed; only whether two views agree and what
 * VirtualQuery reports about a view.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-createfilemappingw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-openfilemappingw
 *       https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-mapviewoffile
 *       https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-mapviewoffileex
 *       https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-unmapviewoffile
 *       https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-flushviewoffile
 *       https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualquery
 *       https://learn.microsoft.com/en-us/windows/win32/memory/creating-a-file-mapping-object
 *       https://learn.microsoft.com/en-us/windows/win32/memory/file-mapping-security-and-access-rights
 *       https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-setendoffile
 */
#include "oracle.h"

#define NAME(s) L"SetuOracle_map_" s

#define BOOL_CASE(name, expr)     \
    do {                          \
        BOOL ok_;                 \
        ORACLE_CALL(ok_, (expr)); \
        oracle_case(name);        \
        o_bool("ret", ok_);       \
        o_lasterr();              \
        oracle_case_end();        \
    } while (0)

static HANDLE mk_file(const wchar_t *name, DWORD access, DWORD size, char fill)
{
    HANDLE h = CreateFileW(name, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, CREATE_ALWAYS, 0, NULL);
    char buf[4096];
    memset(buf, fill, sizeof buf);
    DWORD w;
    while (size) {
        DWORD n = size > sizeof buf ? sizeof buf : size;
        WriteFile(h, buf, n, &w, NULL);
        size -= n;
    }
    CloseHandle(h);
    return CreateFileW(name, access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
}

static HANDLE map_case(const char *name, HANDLE file, DWORD prot, DWORD hi, DWORD lo, const wchar_t *mname)
{
    HANDLE m;
    ORACLE_CALL(m, CreateFileMappingW(file, NULL, prot, hi, lo, mname));
    oracle_case(name);
    o_handle("ret", m);
    o_lasterr();
    oracle_case_end();
    return m;
}

static void *view_case(const char *name, HANDLE m, DWORD access, DWORD off, SIZE_T size)
{
    void *p;
    ORACLE_CALL(p, MapViewOfFile(m, access, 0, off, size));
    oracle_case(name);
    o_bool("ret", p != NULL);
    o_lasterr();
    oracle_case_end();
    return p;
}

static const char *prot_name(DWORD p)
{
    static char t[32];
    switch (p) {
    case PAGE_NOACCESS: return "NOACCESS";
    case PAGE_READONLY: return "READONLY";
    case PAGE_READWRITE: return "READWRITE";
    case PAGE_WRITECOPY: return "WRITECOPY";
    case PAGE_EXECUTE: return "EXECUTE";
    case PAGE_EXECUTE_READ: return "EXECUTE_READ";
    case PAGE_EXECUTE_READWRITE: return "EXECUTE_READWRITE";
    case PAGE_EXECUTE_WRITECOPY: return "EXECUTE_WRITECOPY";
    case 0: return "0";
    }
    snprintf(t, sizeof t, "0x%lx", (unsigned long)p);
    return t;
}

static void vq_case(const char *name, const void *p)
{
    MEMORY_BASIC_INFORMATION mbi;
    SIZE_T r = VirtualQuery(p, &mbi, sizeof mbi);
    oracle_case(name);
    o_bool("ret", r != 0);
    if (r) {
        o_bool("base_is_view", mbi.BaseAddress == p);
        o_bool("alloc_base_is_view", mbi.AllocationBase == p);
        o_str("alloc_protect", prot_name(mbi.AllocationProtect));
        o_str("protect", prot_name(mbi.Protect));
        o_u64("region_size", mbi.RegionSize);
        o_hex32("state", mbi.State);
        o_hex32("type", mbi.Type);
    }
    oracle_case_end();
}

static DWORD file_size(const wchar_t *name)
{
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExW(name, GetFileExInfoStandard, &fad)) return 0xFFFFFFFF;
    return fad.nFileSizeLow;
}

int main(void)
{
    oracle_begin("kernel32/file_mapping");

    HANDLE f, m, m2;
    unsigned char *v, *v2;
    DWORD got;
    BOOL ok;

    /* ---- creation errors and sizes ---- */
    f = mk_file(L"empty.bin", GENERIC_READ | GENERIC_WRITE, 0, 0);
    m = map_case("empty_file_size_zero", f, PAGE_READWRITE, 0, 0, NULL);
    if (m) CloseHandle(m);
    m = map_case("empty_file_readonly_size_100", f, PAGE_READONLY, 0, 100, NULL);
    if (m) CloseHandle(m);
    oracle_case("empty_file_after_readonly_map");
    o_u32("size", file_size(L"empty.bin"));
    oracle_case_end();
    m = map_case("empty_file_readwrite_size_100", f, PAGE_READWRITE, 0, 100, NULL);
    oracle_case("empty_file_extended_by_mapping");
    o_u32("size", file_size(L"empty.bin"));
    oracle_case_end();
    if (m) CloseHandle(m);
    CloseHandle(f);

    f = mk_file(L"ro.bin", GENERIC_READ, 100, 'r');
    m = map_case("readonly_handle_page_readwrite", f, PAGE_READWRITE, 0, 0, NULL);
    if (m) CloseHandle(m);
    m = map_case("readonly_handle_page_writecopy", f, PAGE_WRITECOPY, 0, 0, NULL);
    if (m) CloseHandle(m);
    m = map_case("readonly_handle_page_execute_read", f, PAGE_EXECUTE_READ, 0, 0, NULL);
    if (m) CloseHandle(m);
    m = map_case("readonly_handle_size_200", f, PAGE_READONLY, 0, 200, NULL);
    if (m) CloseHandle(m);
    m = map_case("readonly_handle_bad_protect", f, 0x12345, 0, 0, NULL);
    if (m) CloseHandle(m);
    m = map_case("readonly_handle_protect_zero", f, 0, 0, 0, NULL);
    if (m) CloseHandle(m);
    m = map_case("readonly_handle_page_noaccess", f, PAGE_NOACCESS, 0, 0, NULL);
    if (m) CloseHandle(m);
    m = map_case("readonly_handle_sec_image_not_pe", f, PAGE_READONLY | SEC_IMAGE, 0, 0, NULL);
    if (m) CloseHandle(m);
    m = map_case("readonly_handle_readonly", f, PAGE_READONLY, 0, 0, NULL);
    if (m) {
        v = view_case("view_write_on_readonly_mapping", m, FILE_MAP_WRITE, 0, 0);
        if (v) UnmapViewOfFile(v);
        v = view_case("view_copy_on_readonly_mapping", m, FILE_MAP_COPY, 0, 0);
        if (v) UnmapViewOfFile(v);
        v = view_case("view_execute_on_readonly_mapping", m, FILE_MAP_READ | FILE_MAP_EXECUTE, 0, 0);
        if (v) UnmapViewOfFile(v);
        v = view_case("view_read_on_readonly_mapping", m, FILE_MAP_READ, 0, 0);
        if (v) {
            oracle_case("view_read_contents");
            o_bytes("first", v, 4);
            o_bytes("tail_past_eof", v + 100, 4);
            oracle_case_end();
            vq_case("view_read_vq", v);
            UnmapViewOfFile(v);
        }
        v = view_case("view_size_larger_than_mapping", m, FILE_MAP_READ, 0, 8192);
        if (v) UnmapViewOfFile(v);
        v = view_case("view_offset_unaligned", m, FILE_MAP_READ, 4096, 0);
        if (v) UnmapViewOfFile(v);
        v = view_case("view_offset_beyond_mapping", m, FILE_MAP_READ, 65536, 0);
        if (v) UnmapViewOfFile(v);
        v = view_case("view_access_zero", m, 0, 0, 0);
        if (v) UnmapViewOfFile(v);
        CloseHandle(m);
    }
    CloseHandle(f);

    /* ---- coherence between a view and ReadFile/WriteFile ---- */
    f = mk_file(L"rw.bin", GENERIC_READ | GENERIC_WRITE, 70000, 'a');
    m = map_case("rw_mapping", f, PAGE_READWRITE, 0, 0, NULL);
    v = view_case("rw_view_all", m, FILE_MAP_WRITE, 0, 0);
    if (v) {
        vq_case("rw_view_vq", v);
        memcpy(v, "VIEW", 4);
        char buf[8] = {0};
        SetFilePointer(f, 0, NULL, FILE_BEGIN);
        ReadFile(f, buf, 4, &got, NULL);
        oracle_case("readfile_sees_view_write");
        o_bytes("data", buf, got);
        oracle_case_end();
        SetFilePointer(f, 8, NULL, FILE_BEGIN);
        WriteFile(f, "FILE", 4, &got, NULL);
        oracle_case("view_sees_writefile");
        o_bytes("data", v + 8, 4);
        oracle_case_end();
        BOOL_CASE("flush_view_whole", FlushViewOfFile(v, 0));
        BOOL_CASE("flush_view_partial", FlushViewOfFile(v + 10, 10));

        /* Second view at 64 KiB offset sees the same bytes. */
        v2 = view_case("rw_view_offset_64k", m, FILE_MAP_READ, 65536, 0);
        if (v2) {
            v[65536] = 'Z';
            oracle_case("second_view_coherent");
            o_bytes("data", v2, 2);
            oracle_case_end();
            vq_case("view_offset_64k_vq", v2);
            UnmapViewOfFile(v2);
        }

        /* Operations a mapped section blocks. */
        SetFilePointer(f, 10, NULL, FILE_BEGIN);
        BOOL_CASE("truncate_while_mapped", SetEndOfFile(f));
        SetFilePointer(f, 100000, NULL, FILE_BEGIN);
        BOOL_CASE("extend_while_mapped", SetEndOfFile(f));
        oracle_case("size_after_extend_while_mapped");
        o_u32("size", file_size(L"rw.bin"));
        oracle_case_end();

        BOOL_CASE("unmap", UnmapViewOfFile(v));
        BOOL_CASE("unmap_twice", UnmapViewOfFile(v));
    }
    BOOL_CASE("unmap_null", UnmapViewOfFile(NULL));
    {
        /* Private memory is not a view. (Never pass an image address: that
         * would unmap the executable itself.) */
        void *priv = VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        BOOL_CASE("unmap_private_memory", UnmapViewOfFile(priv));
        VirtualFree(priv, 0, MEM_RELEASE);
    }
    CloseHandle(f);
    /* File handle closed, mapping still open. */
    BOOL_CASE("delete_while_section_open", DeleteFileW(L"rw.bin"));
    f = CreateFileW(L"rw.bin", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                    TRUNCATE_EXISTING, 0, NULL);
    oracle_case("truncate_existing_while_section_open");
    o_handle("ret", f);
    o_lasterr();
    oracle_case_end();
    if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
    if (m) CloseHandle(m);
    BOOL_CASE("delete_after_section_closed", DeleteFileW(L"rw.bin"));

    /* ---- copy-on-write views ---- */
    f = mk_file(L"cow.bin", GENERIC_READ | GENERIC_WRITE, 4096, 'c');
    m = map_case("cow_mapping", f, PAGE_WRITECOPY, 0, 0, NULL);
    if (m) {
        v = view_case("cow_view", m, FILE_MAP_COPY, 0, 0);
        if (v) {
            vq_case("cow_vq_before_write", v);
            v[0] = 'X';
            vq_case("cow_vq_after_write", v);
            UnmapViewOfFile(v);
        }
        v = view_case("cow_mapping_view_write", m, FILE_MAP_WRITE, 0, 0);
        if (v) UnmapViewOfFile(v);
        v = view_case("cow_mapping_view_read", m, FILE_MAP_READ, 0, 0);
        if (v) {
            vq_case("cow_mapping_read_vq", v);
            UnmapViewOfFile(v);
        }
        CloseHandle(m);
    }
    CloseHandle(f);
    oracle_case("cow_file_unchanged");
    o_file_state("file", L"cow.bin");
    oracle_case_end();

    /* ---- pagefile-backed and named sections ---- */
    m = map_case("anon_size_zero", INVALID_HANDLE_VALUE, PAGE_READWRITE, 0, 0, NULL);
    if (m) CloseHandle(m);
    m = map_case("anon_writecopy", INVALID_HANDLE_VALUE, PAGE_WRITECOPY, 0, 4096, NULL);
    if (m) CloseHandle(m);
    m = map_case("anon_named_create", INVALID_HANDLE_VALUE, PAGE_READWRITE, 0, 10000, NAME("a"));
    m2 = map_case("anon_named_create_again", INVALID_HANDLE_VALUE, PAGE_READWRITE, 0, 20000, NAME("a"));
    if (m && m2) {
        v = MapViewOfFile(m, FILE_MAP_ALL_ACCESS, 0, 0, 0);
        v2 = MapViewOfFile(m2, FILE_MAP_READ, 0, 0, 0);
        if (v && v2) {
            oracle_case("anon_named_shared_view");
            o_bytes("fresh", v, 4);
            v[5] = 0x5A;
            o_bool("shared", v2[5] == 0x5A);
            o_bool("distinct_views", v != v2);
            oracle_case_end();
            vq_case("anon_view_vq", v);
        }
        if (v) UnmapViewOfFile(v);
        if (v2) UnmapViewOfFile(v2);
        v = view_case("anon_view_too_big", m, FILE_MAP_READ, 0, 12288);
        if (v) UnmapViewOfFile(v);
        v = view_case("anon_view_size_10000", m, FILE_MAP_READ, 0, 10000);
        if (v) UnmapViewOfFile(v);
    }
    {
        HANDLE o;
        ORACLE_CALL(o, OpenFileMappingW(FILE_MAP_READ, FALSE, NAME("a")));
        oracle_case("open_existing");
        o_handle("ret", o);
        o_lasterr();
        oracle_case_end();
        if (o) {
            v = view_case("open_read_view_write", o, FILE_MAP_WRITE, 0, 0);
            if (v) UnmapViewOfFile(v);
            CloseHandle(o);
        }
        ORACLE_CALL(o, OpenFileMappingW(FILE_MAP_READ, FALSE, NAME("missing")));
        oracle_case("open_missing");
        o_handle("ret", o);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(o, OpenFileMappingW(FILE_MAP_READ, FALSE, NULL));
        oracle_case("open_null_name");
        o_handle("ret", o);
        o_lasterr();
        oracle_case_end();
        if (o) CloseHandle(o);
        ORACLE_CALL(o, OpenFileMappingW(FILE_MAP_READ, FALSE, L"SETUORACLE_MAP_A"));
        oracle_case("open_name_other_case");
        o_handle("ret", o);
        o_lasterr();
        oracle_case_end();
        if (o) CloseHandle(o);
        HANDLE ev = CreateEventW(NULL, TRUE, FALSE, NAME("ev"));
        o = map_case("create_name_used_by_event", INVALID_HANDLE_VALUE, PAGE_READWRITE, 0, 4096, NAME("ev"));
        if (o) CloseHandle(o);
        ORACLE_CALL(o, OpenFileMappingW(FILE_MAP_READ, FALSE, NAME("ev")));
        oracle_case("open_name_used_by_event");
        o_handle("ret", o);
        o_lasterr();
        oracle_case_end();
        if (o) CloseHandle(o);
        CloseHandle(ev);
    }
    if (m) CloseHandle(m);
    if (m2) CloseHandle(m2);
    {
        HANDLE o;
        ORACLE_CALL(o, OpenFileMappingW(FILE_MAP_READ, FALSE, NAME("a")));
        oracle_case("open_after_all_closed");
        o_handle("ret", o);
        o_lasterr();
        oracle_case_end();
        if (o) CloseHandle(o);
    }

    /* ---- MapViewOfFileEx at a busy address ---- */
    m = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 65536, NULL);
    v = MapViewOfFile(m, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    if (v) {
        void *p;
        ORACLE_CALL(p, MapViewOfFileEx(m, FILE_MAP_READ, 0, 0, 0, v));
        oracle_case("mapex_busy_address");
        o_bool("ret", p != NULL);
        o_lasterr();
        oracle_case_end();
        if (p) UnmapViewOfFile(p);
        ORACLE_CALL(p, MapViewOfFileEx(m, FILE_MAP_READ, 0, 0, 0, v + 4096));
        oracle_case("mapex_unaligned_address");
        o_bool("ret", p != NULL);
        o_lasterr();
        oracle_case_end();
        if (p) UnmapViewOfFile(p);
        ORACLE_CALL(ok, UnmapViewOfFile(v + 4096));
        oracle_case("unmap_inside_view");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        if (!ok) UnmapViewOfFile(v);
        /* The view keeps the section alive after the handle is closed. */
        v = MapViewOfFile(m, FILE_MAP_ALL_ACCESS, 0, 0, 0);
        CloseHandle(m);
        m = NULL;
        v[1] = 9;
        oracle_case("view_survives_handle_close");
        o_u32("byte", v[1]);
        oracle_case_end();
        UnmapViewOfFile(v);
    }
    if (m) CloseHandle(m);

    /* ---- mapping a file with size larger than 4 GiB high part on read-only ---- */
    f = mk_file(L"small.bin", GENERIC_READ | GENERIC_WRITE, 10, 's');
    m = map_case("mapping_size_high_nonzero_readonly", f, PAGE_READONLY, 1, 0, NULL);
    if (m) CloseHandle(m);
    m = map_case("mapping_on_closed_handle_style_invalid", (HANDLE)(ULONG_PTR)0x1234, PAGE_READONLY, 0, 0, NULL);
    if (m) CloseHandle(m);
    CloseHandle(f);
    {
        HANDLE d = CreateFileW(L".", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                               FILE_FLAG_BACKUP_SEMANTICS, NULL);
        m = map_case("mapping_on_directory", d, PAGE_READONLY, 0, 0, NULL);
        if (m) CloseHandle(m);
        CloseHandle(d);
    }

    /* ---- which opens a section blocks (no view mapped, file handle closed) ---- */
    f = mk_file(L"sec.bin", GENERIC_READ | GENERIC_WRITE, 4096, 's');
    m = CreateFileMappingW(f, NULL, PAGE_READWRITE, 0, 0, NULL);
    CloseHandle(f);
    {
        static const struct { const char *name; DWORD access, disp; } opens[] = {
            {"section_open_rw", GENERIC_READ | GENERIC_WRITE, OPEN_EXISTING},
            {"section_truncate_existing", GENERIC_READ | GENERIC_WRITE, TRUNCATE_EXISTING},
            {"section_truncate_existing_write_only", GENERIC_WRITE, TRUNCATE_EXISTING},
            {"section_create_always", GENERIC_READ | GENERIC_WRITE, CREATE_ALWAYS},
            {"section_open_delete", DELETE, OPEN_EXISTING},
        };
        for (int i = 0; i < 5; i++) {
            HANDLE h;
            ORACLE_CALL(h, CreateFileW(L"sec.bin", opens[i].access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                       NULL, opens[i].disp, 0, NULL));
            oracle_case(opens[i].name);
            o_handle("ret", h);
            o_lasterr();
            oracle_case_end();
            if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        }
        HANDLE h = CreateFileW(L"sec.bin", DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                               OPEN_EXISTING, 0, NULL);
        FILE_DISPOSITION_INFO di = {TRUE};
        BOOL_CASE("section_set_disposition", SetFileInformationByHandle(h, FileDispositionInfo, &di, sizeof di));
        CloseHandle(h);
        BOOL_CASE("section_delete_file", DeleteFileW(L"sec.bin"));
        ORACLE_CALL(h, CreateFileW(L"sec.bin", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                   TRUNCATE_EXISTING, 0, NULL));
        oracle_case("section_truncate_after_failed_delete");
        o_handle("ret", h);
        o_lasterr();
        oracle_case_end();
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        h = CreateFileW(L"sec.bin", DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                        FILE_FLAG_DELETE_ON_CLOSE, NULL);
        oracle_case("section_doc_open");
        o_handle("ret", h);
        oracle_case_end();
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        oracle_case("section_after_doc_close");
        o_file_state("file", L"sec.bin");
        oracle_case_end();
    }
    if (m) CloseHandle(m);
    oracle_case("section_closed_file_state");
    o_bool("exists", GetFileAttributesW(L"sec.bin") != INVALID_FILE_ATTRIBUTES);
    oracle_case_end();

    /* ---- TRUNCATE_EXISTING over a section, after different uses of it ---- */
    for (int variant = 0; variant < 4; variant++) {
        static const char *const names[] = {"sec_trunc_after_view_written", "sec_trunc_with_view_mapped",
                                            "sec_trunc_after_file_extended", "sec_trunc_after_flush"};
        f = mk_file(L"sec2.bin", GENERIC_READ | GENERIC_WRITE, 8192, 't');
        m = CreateFileMappingW(f, NULL, PAGE_READWRITE, 0, 0, NULL);
        v = m ? MapViewOfFile(m, FILE_MAP_WRITE, 0, 0, 0) : NULL;
        if (v) v[0] = 'W';
        if (variant == 2) {
            SetFilePointer(f, 20000, NULL, FILE_BEGIN);
            SetEndOfFile(f);
        }
        if (v && variant == 3) FlushViewOfFile(v, 0);
        if (v && variant != 1) {
            UnmapViewOfFile(v);
            v = NULL;
        }
        CloseHandle(f);
        HANDLE h;
        ORACLE_CALL(h, CreateFileW(L"sec2.bin", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                   TRUNCATE_EXISTING, 0, NULL));
        oracle_case(names[variant]);
        o_handle("ret", h);
        o_lasterr();
        oracle_case_end();
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        if (v) UnmapViewOfFile(v);
        if (m) CloseHandle(m);
        DeleteFileW(L"sec2.bin");
    }

    /* the same, replaying the rw.bin sequence above step by step */
    for (int variant = 0; variant < 3; variant++) {
        static const char *const names[] = {"rw_replay_trunc_no_delete", "rw_replay_trunc_after_delete",
                                            "rw_replay_no_shrink_trunc_after_delete"};
        f = mk_file(L"sec3.bin", GENERIC_READ | GENERIC_WRITE, 70000, 'a');
        m = CreateFileMappingW(f, NULL, PAGE_READWRITE, 0, 0, NULL);
        v = m ? MapViewOfFile(m, FILE_MAP_WRITE, 0, 0, 0) : NULL;
        if (v) {
            memcpy(v, "VIEW", 4);
            SetFilePointer(f, 8, NULL, FILE_BEGIN);
            WriteFile(f, "FILE", 4, &got, NULL);
            FlushViewOfFile(v, 0);
            if (variant != 2) {
                SetFilePointer(f, 10, NULL, FILE_BEGIN);
                SetEndOfFile(f);
            }
            SetFilePointer(f, 100000, NULL, FILE_BEGIN);
            SetEndOfFile(f);
            UnmapViewOfFile(v);
        }
        CloseHandle(f);
        if (variant != 0) DeleteFileW(L"sec3.bin");
        HANDLE h;
        ORACLE_CALL(h, CreateFileW(L"sec3.bin", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                   TRUNCATE_EXISTING, 0, NULL));
        oracle_case(names[variant]);
        o_handle("ret", h);
        o_lasterr();
        oracle_case_end();
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        if (m) CloseHandle(m);
        DeleteFileW(L"sec3.bin");
    }

    /* ---- Probes: deleting a file that backs an image section, and the
     * FileDispositionInfoEx forms on a data section ---- */
    {
        wchar_t self[MAX_PATH];
        GetModuleFileNameW(NULL, self, MAX_PATH);
        for (int variant = 0; variant < 2; variant++) {
            static const char *const names[] = {"probe_image_section_delete", "probe_image_view_delete"};
            static const char *const states[] = {"probe_image_section_after_delete", "probe_image_view_after_delete"};
            const wchar_t *img = variant ? L"img2.exe" : L"img1.exe";
            CopyFileW(self, img, FALSE);
            HANDLE h = CreateFileW(img, GENERIC_READ | GENERIC_EXECUTE, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL,
                                   OPEN_EXISTING, 0, NULL);
            HANDLE im = CreateFileMappingW(h, NULL, PAGE_READONLY | SEC_IMAGE, 0, 0, NULL);
            CloseHandle(h);
            void *iv = (im && variant) ? MapViewOfFile(im, FILE_MAP_READ, 0, 0, 0) : NULL;
            oracle_case(names[variant]);
            o_bool("section", im != NULL);
            o_bool("view", iv != NULL);
            oracle_case_end();
            BOOL_CASE(states[variant], DeleteFileW(img));
            oracle_case(variant ? "probe_image_view_exists" : "probe_image_section_exists");
            o_bool("exists", GetFileAttributesW(img) != INVALID_FILE_ATTRIBUTES);
            oracle_case_end();
            if (iv) UnmapViewOfFile(iv);
            if (im) CloseHandle(im);
            DeleteFileW(img);
            oracle_case(variant ? "probe_image_view_closed_exists" : "probe_image_section_closed_exists");
            o_bool("exists", GetFileAttributesW(img) != INVALID_FILE_ATTRIBUTES);
            oracle_case_end();
        }
    }
    for (int variant = 0; variant < 2; variant++) {
        static const char *const names[] = {"probe_section_disp_ex_delete", "probe_section_disp_ex_posix"};
        static const DWORD flags[] = {0x1 /* DELETE */, 0x3 /* DELETE | POSIX_SEMANTICS */};
        f = mk_file(L"sec4.bin", GENERIC_READ | GENERIC_WRITE, 4096, 'd');
        m = CreateFileMappingW(f, NULL, PAGE_READWRITE, 0, 0, NULL);
        CloseHandle(f);
        HANDLE h = CreateFileW(L"sec4.bin", DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                               OPEN_EXISTING, 0, NULL);
        DWORD d = flags[variant];
        BOOL_CASE(names[variant], SetFileInformationByHandle(h, (FILE_INFO_BY_HANDLE_CLASS)21 /* FileDispositionInfoEx */,
                                                             &d, sizeof d));
        oracle_case(variant ? "probe_section_disp_ex_posix_state" : "probe_section_disp_ex_delete_state");
        o_bool("exists", GetFileAttributesW(L"sec4.bin") != INVALID_FILE_ATTRIBUTES);
        oracle_case_end();
        CloseHandle(h);
        oracle_case(variant ? "probe_section_disp_ex_posix_after_close" : "probe_section_disp_ex_delete_after_close");
        o_bool("exists", GetFileAttributesW(L"sec4.bin") != INVALID_FILE_ATTRIBUTES);
        o_dir_listing("listing", L".", 1);
        oracle_case_end();
        if (m) CloseHandle(m);
        DeleteFileW(L"sec4.bin");
    }

    return oracle_end();
}
