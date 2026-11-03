/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: the CreateFileW sharing matrix. For each (access, share) pair
 * of a first handle, try every (access, share) pair for a second open of the
 * same file and record success or the error code. One case per first-handle
 * pair; one field per second-open pair.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/fileio/creating-and-opening-files
 *       https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew
 */
#include "oracle.h"

struct acc { const char *name; DWORD mask; };
static const struct acc accesses[] = {
    {"none", 0},
    {"attr", FILE_READ_ATTRIBUTES},
    {"r", GENERIC_READ},
    {"w", GENERIC_WRITE},
    {"rw", GENERIC_READ | GENERIC_WRITE},
    {"del", DELETE},
};
#define NACC (sizeof accesses / sizeof accesses[0])

static void share_name(char *buf, DWORD s)
{
    buf[0] = (s & FILE_SHARE_READ) ? 'R' : '-';
    buf[1] = (s & FILE_SHARE_WRITE) ? 'W' : '-';
    buf[2] = (s & FILE_SHARE_DELETE) ? 'D' : '-';
    buf[3] = 0;
}

int main(void)
{
    oracle_begin("kernel32/share_mode_matrix");

    HANDLE f = CreateFileW(L"f.txt", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    DWORD n;
    WriteFile(f, "data", 4, &n, NULL);
    CloseHandle(f);

    for (unsigned a1 = 0; a1 < NACC; a1++) {
        for (DWORD s1 = 0; s1 < 8; s1++) {
            char cname[64], sn[4];
            share_name(sn, s1);
            snprintf(cname, sizeof cname, "first_%s_%s", accesses[a1].name, sn);
            HANDLE h1;
            ORACLE_CALL(h1, CreateFileW(L"f.txt", accesses[a1].mask, s1, NULL, OPEN_EXISTING, 0, NULL));
            oracle_case(cname);
            o_handle("first", h1);
            o_lasterr();
            for (unsigned a2 = 0; a2 < NACC; a2++) {
                for (DWORD s2 = 0; s2 < 8; s2++) {
                    char key[32], val[16];
                    HANDLE h2;
                    share_name(sn, s2);
                    snprintf(key, sizeof key, "%s_%s", accesses[a2].name, sn);
                    ORACLE_CALL(h2, CreateFileW(L"f.txt", accesses[a2].mask, s2, NULL, OPEN_EXISTING, 0, NULL));
                    if (h2 != INVALID_HANDLE_VALUE) {
                        strcpy(val, "ok");
                        CloseHandle(h2);
                    } else {
                        snprintf(val, sizeof val, "e%lu", (unsigned long)oracle_last_error);
                    }
                    o_str(key, val);
                }
            }
            oracle_case_end();
            if (h1 != INVALID_HANDLE_VALUE) CloseHandle(h1);
        }
    }

    /* Dispositions that write the file against a read-only-shared handle. */
    {
        static const struct { const char *name; DWORD disp; DWORD access; } d[] = {
            {"truncate_existing", TRUNCATE_EXISTING, GENERIC_WRITE},
            {"create_always", CREATE_ALWAYS, GENERIC_WRITE},
            {"open_always", OPEN_ALWAYS, GENERIC_READ},
            {"create_always_read", CREATE_ALWAYS, GENERIC_READ},
        };
        for (DWORD s1 = 0; s1 < 8; s1++) {
            char cname[64], sn[4];
            share_name(sn, s1);
            snprintf(cname, sizeof cname, "disposition_vs_r_%s", sn);
            HANDLE h1 = CreateFileW(L"f.txt", GENERIC_READ, s1, NULL, OPEN_EXISTING, 0, NULL);
            oracle_case(cname);
            for (unsigned i = 0; i < sizeof d / sizeof d[0]; i++) {
                char val[16];
                HANDLE h2;
                ORACLE_CALL(h2, CreateFileW(L"f.txt", d[i].access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                            NULL, d[i].disp, 0, NULL));
                if (h2 != INVALID_HANDLE_VALUE) {
                    strcpy(val, "ok");
                    CloseHandle(h2);
                } else {
                    snprintf(val, sizeof val, "e%lu", (unsigned long)oracle_last_error);
                }
                o_str(d[i].name, val);
            }
            oracle_case_end();
            CloseHandle(h1);
        }
    }

    return oracle_end();
}
