/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: several threads creating and deleting the same keys at once.
 * Only counts are printed: how many threads saw REG_CREATED_NEW_KEY for one
 * shared name (exactly one?), how many creates succeeded, and the final
 * number of subkeys after threads create distinct names, and after threads
 * race to delete one key (at least one success, every other result
 * ERROR_FILE_NOT_FOUND or ERROR_KEY_DELETED).
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regcreatekeyexw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regdeletekeyw
 *       https://learn.microsoft.com/en-us/windows/win32/sync/using-synchronization
 */
#include "oracle.h"

#define THREADS 8
#define ROUNDS 25

static HKEY root;
static HANDLE go;
static volatile LONG created, opened, failed, deleted, not_found, other;

static DWORD WINAPI same_name(void *arg)
{
    int round = (int)(INT_PTR)arg;
    wchar_t nm[32];
    swprintf(nm, 32, L"same%d", round);
    WaitForSingleObject(go, INFINITE);
    HKEY k;
    DWORD disp = 0;
    LONG st = RegCreateKeyExW(root, nm, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, &disp);
    if (st) InterlockedIncrement(&failed);
    else {
        if (disp == REG_CREATED_NEW_KEY) InterlockedIncrement(&created);
        else if (disp == REG_OPENED_EXISTING_KEY) InterlockedIncrement(&opened);
        RegCloseKey(k);
    }
    return 0;
}

static DWORD WINAPI distinct_names(void *arg)
{
    int t = (int)(INT_PTR)arg;
    WaitForSingleObject(go, INFINITE);
    for (int i = 0; i < ROUNDS; i++) {
        wchar_t nm[32];
        HKEY k;
        swprintf(nm, 32, L"d\\t%d_%d", t, i);
        if (RegCreateKeyExW(root, nm, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL)) InterlockedIncrement(&failed);
        else RegCloseKey(k);
    }
    return 0;
}

static DWORD WINAPI delete_one(void *arg)
{
    (void)arg;
    WaitForSingleObject(go, INFINITE);
    LONG st = RegDeleteKeyW(root, L"victim");
    if (!st) InterlockedIncrement(&deleted);
    else if (st == ERROR_FILE_NOT_FOUND || st == ERROR_KEY_DELETED) InterlockedIncrement(&not_found);
    else InterlockedIncrement(&other);
    return 0;
}

static void run(LPTHREAD_START_ROUTINE fn, INT_PTR arg_base, int per_thread_arg)
{
    HANDLE th[THREADS];
    ResetEvent(go);
    for (int i = 0; i < THREADS; i++)
        th[i] = CreateThread(NULL, 0, fn, (void *)(per_thread_arg ? (INT_PTR)i : arg_base), 0, NULL);
    Sleep(20);
    SetEvent(go);
    WaitForMultipleObjects(THREADS, th, TRUE, INFINITE);
    for (int i = 0; i < THREADS; i++) CloseHandle(th[i]);
}

static DWORD subkeys(const wchar_t *path)
{
    HKEY k;
    DWORD n = 0;
    if (!RegOpenKeyExW(root, path, 0, KEY_READ, &k)) {
        RegQueryInfoKeyW(k, NULL, NULL, NULL, &n, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        RegCloseKey(k);
    }
    return n;
}

int main(void)
{
    oracle_begin("advapi32/RegCreateKeyEx_threads");
    root = oracle_reg_root();
    go = CreateEventW(NULL, TRUE, FALSE, NULL);

    {
        int exactly_one_every_round = 1;
        LONG total_failed = 0, total_opened = 0;
        for (int r = 0; r < ROUNDS; r++) {
            created = opened = failed = 0;
            run(same_name, r, 0);
            if (created != 1) exactly_one_every_round = 0;
            total_failed += failed;
            total_opened += opened;
        }
        oracle_case("same_name_race");
        o_bool("exactly_one_created_every_round", exactly_one_every_round);
        o_u32("opened_existing", (DWORD)total_opened);
        o_u32("failed", (DWORD)total_failed);
        o_u32("keys", subkeys(L""));
        oracle_case_end();
    }
    {
        failed = 0;
        run(distinct_names, 0, 1);
        oracle_case("distinct_names");
        o_u32("failed", (DWORD)failed);
        o_u32("subkeys_of_d", subkeys(L"d"));
        oracle_case_end();
    }
    {
        int winner_every_round = 1;
        LONG total_other = 0;
        for (int r = 0; r < ROUNDS; r++) {
            HKEY k;
            RegCreateKeyExW(root, L"victim", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL);
            RegCloseKey(k);
            deleted = not_found = other = 0;
            run(delete_one, 0, 0);
            if (deleted < 1 || deleted + not_found != THREADS) winner_every_round = 0;
            total_other += other;
        }
        oracle_case("delete_race");
        o_bool("winner_every_round", winner_every_round);
        o_u32("other_errors", (DWORD)total_other);
        HKEY v;
        LONG vs = RegOpenKeyExW(root, L"victim", 0, KEY_READ, &v);
        if (!vs) RegCloseKey(v);
        o_status("victim_open_after", vs);
        oracle_case_end();
    }

    CloseHandle(go);
    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
