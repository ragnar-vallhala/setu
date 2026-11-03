/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: last-error values of named kernel objects.
 *
 * Single-instance apps call CreateMutexW / CreateEventW and test
 * GetLastError() == ERROR_ALREADY_EXISTS; others open by name and look for
 * ERROR_FILE_NOT_FOUND. This test covers new versus existing names for each
 * object type, names in use by another type, names that are only different
 * in case, the Local\ and Global\ prefixes, bad names, and Open* calls.
 *
 * Names are fixed and process-private (the objects die with the process).
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-createmutexw
 *       https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-createeventw
 *       https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-openeventw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-createsemaphorew
 *       https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-createwaitabletimerw
 *       https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-createfilemappingw
 *       https://learn.microsoft.com/en-us/windows/win32/api/jobapi2/nf-jobapi2-createjobobjectw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-createnamedpipew
 *       https://learn.microsoft.com/en-us/windows/win32/termserv/kernel-object-namespaces
 */
#include "oracle.h"

#define P L"SetuOracleLE_"

static void hcase(const char *name, HANDLE h)
{
    oracle_case(name);
    o_handle("ret", h);
    o_lasterr();
    oracle_case_end();
}

static DWORD WINAPI try_mutex(void *m)
{
    DWORD r = WaitForSingleObject((HANDLE)m, 0);
    if (r == WAIT_OBJECT_0) ReleaseMutex((HANDLE)m);
    return r;
}

/* Is the mutex currently owned by some thread (as seen from another thread)? */
static int owned_elsewhere(HANDLE m)
{
    DWORD code = 0;
    HANDLE t = CreateThread(NULL, 0, try_mutex, m, 0, NULL);
    WaitForSingleObject(t, INFINITE);
    GetExitCodeThread(t, &code);
    CloseHandle(t);
    return code == WAIT_TIMEOUT;
}

int main(void)
{
    oracle_begin("kernel32/last_error_named_objects");
    HANDLE h, h2, keep[64];
    int nkeep = 0;

    /* ---- events ---- */
    ORACLE_CALL(h, CreateEventW(NULL, TRUE, FALSE, P L"ev"));
    hcase("event_new", h);
    keep[nkeep++] = h;
    ORACLE_CALL(h2, CreateEventW(NULL, FALSE, TRUE, P L"ev"));
    hcase("event_existing", h2);
    oracle_case("event_existing_keeps_state");
    o_u32("wait", WaitForSingleObject(h2, 0)); /* still manual-reset, unsignaled */
    oracle_case_end();
    CloseHandle(h2);
    ORACLE_CALL(h, CreateEventW(NULL, TRUE, FALSE, P L"EV"));
    hcase("event_other_case", h);
    keep[nkeep++] = h;
    ORACLE_CALL(h, CreateEventExW(NULL, P L"ev", 0, EVENT_ALL_ACCESS));
    hcase("event_ex_existing", h);
    CloseHandle(h);
    ORACLE_CALL(h, OpenEventW(EVENT_ALL_ACCESS, FALSE, P L"ev"));
    hcase("open_event_existing", h);
    CloseHandle(h);
    ORACLE_CALL(h, OpenEventW(EVENT_ALL_ACCESS, FALSE, P L"missing"));
    hcase("open_event_missing", h);
    ORACLE_CALL(h, OpenEventW(EVENT_ALL_ACCESS, FALSE, NULL));
    hcase("open_event_null_name", h);
    ORACLE_CALL(h, OpenEventW(EVENT_ALL_ACCESS, FALSE, L""));
    hcase("open_event_empty_name", h);
    ORACLE_CALL(h, CreateEventW(NULL, TRUE, FALSE, L""));
    hcase("event_empty_name", h);
    if (h) CloseHandle(h);
    ORACLE_CALL(h, CreateEventW(NULL, TRUE, FALSE, P L"a\\b"));
    hcase("event_name_with_backslash", h);
    if (h) CloseHandle(h);
    ORACLE_CALL(h, CreateEventW(NULL, TRUE, FALSE, L"Local\\" P L"ev"));
    hcase("event_local_prefix_same_object", h);
    if (h) CloseHandle(h);
    ORACLE_CALL(h, CreateEventW(NULL, TRUE, FALSE, L"local\\" P L"ev"));
    hcase("event_local_prefix_lowercase", h);
    if (h) CloseHandle(h);
    ORACLE_CALL(h, CreateEventW(NULL, TRUE, FALSE, L"Global\\" P L"gev"));
    hcase("event_global_prefix_new", h);
    if (h) keep[nkeep++] = h;
    ORACLE_CALL(h, OpenEventW(EVENT_ALL_ACCESS, FALSE, L"Global\\" P L"gev"));
    hcase("open_event_global_prefix", h);
    if (h) CloseHandle(h);
    ORACLE_CALL(h, CreateEventW(NULL, TRUE, FALSE, L"Bogus\\" P L"ev"));
    hcase("event_unknown_prefix", h);
    if (h) CloseHandle(h);
    {
        wchar_t lng[400];
        wcscpy(lng, P);
        while (wcslen(lng) < 300) wcscat(lng, L"x");
        ORACLE_CALL(h, CreateEventW(NULL, TRUE, FALSE, lng));
        hcase("event_name_300_chars", h);
        if (h) CloseHandle(h);
    }

    /* ---- mutexes ---- */
    ORACLE_CALL(h, CreateMutexW(NULL, FALSE, P L"mx"));
    hcase("mutex_new", h);
    keep[nkeep++] = h;
    ORACLE_CALL(h2, CreateMutexW(NULL, TRUE, P L"mx"));
    hcase("mutex_existing_initial_owner", h2);
    oracle_case("mutex_existing_initial_owner_owned");
    o_bool("owned", owned_elsewhere(h2));
    oracle_case_end();
    CloseHandle(h2);
    ORACLE_CALL(h, CreateMutexExW(NULL, P L"mx", 0, MUTEX_ALL_ACCESS));
    hcase("mutex_ex_existing", h);
    CloseHandle(h);
    ORACLE_CALL(h, OpenMutexW(SYNCHRONIZE, FALSE, P L"mx"));
    hcase("open_mutex_existing", h);
    CloseHandle(h);
    ORACLE_CALL(h, OpenMutexW(SYNCHRONIZE, FALSE, P L"nomx"));
    hcase("open_mutex_missing", h);

    /* ---- names taken by another object type ---- */
    ORACLE_CALL(h, CreateMutexW(NULL, FALSE, P L"ev"));
    hcase("mutex_over_event_name", h);
    ORACLE_CALL(h, CreateSemaphoreW(NULL, 0, 1, P L"ev"));
    hcase("semaphore_over_event_name", h);
    ORACLE_CALL(h, CreateEventW(NULL, TRUE, FALSE, P L"mx"));
    hcase("event_over_mutex_name", h);
    ORACLE_CALL(h, CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 4096, P L"ev"));
    hcase("mapping_over_event_name", h);
    ORACLE_CALL(h, CreateWaitableTimerW(NULL, TRUE, P L"ev"));
    hcase("timer_over_event_name", h);
    ORACLE_CALL(h, CreateJobObjectW(NULL, P L"ev"));
    hcase("job_over_event_name", h);
    ORACLE_CALL(h, OpenMutexW(SYNCHRONIZE, FALSE, P L"ev"));
    hcase("open_mutex_on_event_name", h);
    ORACLE_CALL(h, OpenEventW(SYNCHRONIZE, FALSE, P L"mx"));
    hcase("open_event_on_mutex_name", h);
    ORACLE_CALL(h, OpenFileMappingW(FILE_MAP_READ, FALSE, P L"ev"));
    hcase("open_mapping_on_event_name", h);

    /* ---- semaphores ---- */
    ORACLE_CALL(h, CreateSemaphoreW(NULL, 1, 2, P L"sem"));
    hcase("semaphore_new", h);
    keep[nkeep++] = h;
    ORACLE_CALL(h2, CreateSemaphoreW(NULL, 0, 5, P L"sem"));
    hcase("semaphore_existing", h2);
    {
        LONG prev = -1;
        BOOL ok = ReleaseSemaphore(h2, 1, &prev);
        oracle_case("semaphore_existing_keeps_max");
        o_bool("release_ok", ok);
        o_i64("prev", prev);
        BOOL ok2 = ReleaseSemaphore(h2, 1, &prev);
        o_bool("release_past_max_ok", ok2);
        o_u32("release_past_max_err", ok2 ? 0 : GetLastError());
        oracle_case_end();
    }
    CloseHandle(h2);
    ORACLE_CALL(h, CreateSemaphoreW(NULL, 3, 2, P L"badsem"));
    hcase("semaphore_initial_above_max", h);
    ORACLE_CALL(h, OpenSemaphoreW(SEMAPHORE_ALL_ACCESS, FALSE, P L"nosem"));
    hcase("open_semaphore_missing", h);

    /* ---- waitable timers ---- */
    ORACLE_CALL(h, CreateWaitableTimerW(NULL, TRUE, P L"tm"));
    hcase("timer_new", h);
    keep[nkeep++] = h;
    ORACLE_CALL(h, CreateWaitableTimerW(NULL, FALSE, P L"tm"));
    hcase("timer_existing", h);
    CloseHandle(h);
    ORACLE_CALL(h, OpenWaitableTimerW(TIMER_ALL_ACCESS, FALSE, P L"notm"));
    hcase("open_timer_missing", h);

    /* ---- file mappings ---- */
    ORACLE_CALL(h, CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 4096, P L"map"));
    hcase("mapping_new", h);
    keep[nkeep++] = h;
    ORACLE_CALL(h, CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 65536, P L"map"));
    hcase("mapping_existing_bigger_size", h);
    if (h) {
        MEMORY_BASIC_INFORMATION mbi;
        void *v = MapViewOfFile(h, FILE_MAP_READ, 0, 0, 0);
        VirtualQuery(v, &mbi, sizeof mbi);
        oracle_case("mapping_existing_keeps_size");
        o_u64("view_size", mbi.RegionSize);
        oracle_case_end();
        UnmapViewOfFile(v);
        CloseHandle(h);
    }
    ORACLE_CALL(h, CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READONLY, 0, 4096, P L"map"));
    hcase("mapping_existing_other_protect", h);
    if (h) CloseHandle(h);
    ORACLE_CALL(h, OpenFileMappingW(FILE_MAP_READ, FALSE, P L"nomap"));
    hcase("open_mapping_missing", h);

    /* ---- job objects ---- */
    ORACLE_CALL(h, CreateJobObjectW(NULL, P L"job"));
    hcase("job_new", h);
    keep[nkeep++] = h;
    ORACLE_CALL(h, CreateJobObjectW(NULL, P L"job"));
    hcase("job_existing", h);
    if (h) CloseHandle(h);
    ORACLE_CALL(h, OpenJobObjectW(JOB_OBJECT_QUERY, FALSE, P L"nojob"));
    hcase("open_job_missing", h);

    /* ---- named pipes ---- */
    ORACLE_CALL(h, CreateNamedPipeW(L"\\\\.\\pipe\\" P L"pipe", PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
                                    PIPE_TYPE_BYTE, 2, 512, 512, 0, NULL));
    hcase("pipe_first_instance_new", h);
    keep[nkeep++] = h;
    ORACLE_CALL(h, CreateNamedPipeW(L"\\\\.\\pipe\\" P L"pipe", PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE, 2, 512, 512, 0,
                                    NULL));
    hcase("pipe_second_instance", h);
    if (h != INVALID_HANDLE_VALUE) keep[nkeep++] = h;
    ORACLE_CALL(h, CreateNamedPipeW(L"\\\\.\\pipe\\" P L"pipe", PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE, 2, 512, 512, 0,
                                    NULL));
    hcase("pipe_instances_exhausted", h);
    ORACLE_CALL(h, CreateNamedPipeW(L"\\\\.\\pipe\\" P L"pipe", PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
                                    PIPE_TYPE_BYTE, PIPE_UNLIMITED_INSTANCES, 512, 512, 0, NULL));
    hcase("pipe_first_instance_existing", h);
    ORACLE_CALL(h, CreateFileW(L"\\\\.\\pipe\\" P L"nopipe", GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL));
    hcase("open_pipe_missing", h);
    {
        BOOL ok;
        ORACLE_CALL(ok, WaitNamedPipeW(L"\\\\.\\pipe\\" P L"nopipe", 0));
        oracle_case("wait_pipe_missing");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
    }

    /* ---- the name is freed when the last handle closes ---- */
    ORACLE_CALL(h, CreateEventW(NULL, TRUE, FALSE, P L"tmp"));
    hcase("event_tmp_new", h);
    CloseHandle(h);
    ORACLE_CALL(h, OpenEventW(SYNCHRONIZE, FALSE, P L"tmp"));
    hcase("open_event_after_last_close", h);
    ORACLE_CALL(h, CreateEventW(NULL, TRUE, FALSE, P L"tmp"));
    hcase("event_tmp_recreated", h);
    CloseHandle(h);

    while (nkeep) CloseHandle(keep[--nkeep]);
    return oracle_end();
}
