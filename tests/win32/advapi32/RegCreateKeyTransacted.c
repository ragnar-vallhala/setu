/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: RegCreateKeyTransactedW / RegOpenKeyTransactedW /
 * RegDeleteKeyTransactedW with bad transaction handles, and with a real KTM
 * transaction (ktmw32 CreateTransaction): is the key visible outside the
 * transaction before commit, after commit, and after a rollback?
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regcreatekeytransactedw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regopenkeytransactedw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regdeletekeytransactedw
 *       https://learn.microsoft.com/en-us/windows/win32/api/ktmw32/nf-ktmw32-createtransaction
 */
#include "oracle.h"

typedef HANDLE(WINAPI *CreateTransaction_fn)(LPSECURITY_ATTRIBUTES, LPGUID, DWORD, DWORD, DWORD, DWORD, LPWSTR);
typedef BOOL(WINAPI *TxEnd_fn)(HANDLE);

static HKEY root;

static LONG exists(const wchar_t *sub)
{
    HKEY k;
    LONG st = RegOpenKeyExW(root, sub, 0, KEY_READ, &k);
    if (!st) RegCloseKey(k);
    return st;
}

static void create_tx_case(const char *name, const wchar_t *sub, HANDLE tx)
{
    HKEY k = NULL;
    DWORD disp = 0xAAAAAAAAu;
    LONG st;
    ORACLE_CALL(st, RegCreateKeyTransactedW(root, sub, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, &disp, tx, NULL));
    oracle_case(name);
    o_status("ret", st);
    o_lasterr();
    o_u32("disposition", disp);
    oracle_case_end();
    if (!st) RegCloseKey(k);
}

int main(void)
{
    oracle_begin("advapi32/RegCreateKeyTransacted");
    root = oracle_reg_root();

    create_tx_case("create_null_transaction", L"n", NULL);
    create_tx_case("create_invalid_transaction", L"i", INVALID_HANDLE_VALUE);
    create_tx_case("create_made_up_transaction", L"m", (HANDLE)(ULONG_PTR)0x1234);
    {
        HANDLE ev = CreateEventW(NULL, TRUE, FALSE, NULL);
        create_tx_case("create_event_as_transaction", L"e", ev);
        CloseHandle(ev);
    }
    {
        HKEY k = NULL;
        LONG st;
        ORACLE_CALL(st, RegOpenKeyTransactedW(root, L"n", 0, KEY_READ, &k, NULL, NULL));
        oracle_case("open_null_transaction");
        o_status("ret", st);
        o_lasterr();
        oracle_case_end();
        if (!st) RegCloseKey(k);
        ORACLE_CALL(st, RegDeleteKeyTransactedW(root, L"n", 0, 0, NULL, NULL));
        oracle_case("delete_null_transaction");
        o_status("ret", st);
        o_lasterr();
        o_status("exists_after", exists(L"n"));
        oracle_case_end();
    }

    HMODULE ktm = LoadLibraryW(L"ktmw32.dll");
    CreateTransaction_fn pCreate = ktm ? (CreateTransaction_fn)(void (*)(void))GetProcAddress(ktm, "CreateTransaction") : NULL;
    TxEnd_fn pCommit = ktm ? (TxEnd_fn)(void (*)(void))GetProcAddress(ktm, "CommitTransaction") : NULL;
    TxEnd_fn pRollback = ktm ? (TxEnd_fn)(void (*)(void))GetProcAddress(ktm, "RollbackTransaction") : NULL;
    oracle_case("ktm_available");
    o_bool("available", pCreate && pCommit && pRollback);
    oracle_case_end();

    if (pCreate && pCommit && pRollback) {
        HANDLE tx = pCreate(NULL, NULL, 0, 0, 0, 0, NULL);
        oracle_case("create_transaction");
        o_handle("tx", tx);
        oracle_case_end();
        if (tx != INVALID_HANDLE_VALUE) {
            create_tx_case("create_in_tx", L"tx1", tx);
            oracle_case("visible_before_commit");
            o_status("open_outside", exists(L"tx1"));
            oracle_case_end();
            {
                HKEY k = NULL;
                LONG st = RegOpenKeyTransactedW(root, L"tx1", 0, KEY_READ, &k, tx, NULL);
                oracle_case("open_inside_before_commit");
                o_status("ret", st);
                oracle_case_end();
                if (!st) RegCloseKey(k);
            }
            BOOL c = pCommit(tx);
            oracle_case("commit");
            o_bool("ok", c);
            o_status("open_outside_after", exists(L"tx1"));
            oracle_case_end();
            create_tx_case("create_in_committed_tx", L"tx2", tx);
            CloseHandle(tx);
        }

        tx = pCreate(NULL, NULL, 0, 0, 0, 0, NULL);
        if (tx != INVALID_HANDLE_VALUE) {
            create_tx_case("create_in_tx_to_roll_back", L"rb", tx);
            BOOL r = pRollback(tx);
            oracle_case("rollback");
            o_bool("ok", r);
            o_status("open_outside_after", exists(L"rb"));
            oracle_case_end();
            CloseHandle(tx);
        }

        tx = pCreate(NULL, NULL, 0, 0, 0, 0, NULL);
        if (tx != INVALID_HANDLE_VALUE) {
            LONG st;
            ORACLE_CALL(st, RegDeleteKeyTransactedW(root, L"tx1", 0, 0, tx, NULL));
            oracle_case("delete_in_tx");
            o_status("ret", st);
            o_lasterr();
            o_status("open_outside_before_commit", exists(L"tx1"));
            BOOL c = pCommit(tx);
            o_bool("commit_ok", c);
            o_status("open_outside_after_commit", exists(L"tx1"));
            oracle_case_end();
            CloseHandle(tx);
        }
    }

    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
