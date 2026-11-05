/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: sc.exe output and exit codes for a service this test
 * creates: create (with type, start, error, group, display name), qc,
 * query, queryex (PID line masked), description / qdescription, config,
 * failure / qfailure, getdisplayname / getkeyname, start / stop of a
 * service whose binary is missing, delete, and the errors for a missing
 * service and a duplicate create. Output is printed line by line with
 * trailing spaces trimmed; nothing machine-specific appears in it because
 * the binary path is a fixed made-up path.
 *
 * Docs: https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/sc-create
 *       https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/sc-query
 *       https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/sc-config
 *       https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/sc-delete
 */
#include "oracle.h"

#define SVC L"SetuOracle_sc_1"

/* Runs `sc <args>` and prints its exit code and output. */
static void sc_case(const char *name, const wchar_t *args)
{
    wchar_t cmd[1024];
    static char out[65536];
    DWORD got = 0, code = 0xdead;
    swprintf(cmd, 1024, L"sc.exe %ls", args);
    SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
    HANDLE rd, wr;
    CreatePipe(&rd, &wr, &sa, 0);
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = wr;
    si.hStdError = wr;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    BOOL ok = CreateProcessW(NULL, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    CloseHandle(wr);
    if (ok) {
        DWORD n;
        while (got < sizeof out - 1 && ReadFile(rd, out + got, sizeof out - 1 - got, &n, NULL) && n) got += n;
        WaitForSingleObject(pi.hProcess, 30000);
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    CloseHandle(rd);
    out[got] = 0;
    oracle_case(name);
    o_bool("started", ok);
    o_u32("exit", code);
    /* Lines, trailing blanks trimmed, blank lines kept as "". */
    int line = 0;
    char *p = out;
    while (*p) {
        char *e = strchr(p, '\n');
        size_t len = e ? (size_t)(e - p) : strlen(p);
        char buf[1024];
        if (len > sizeof buf - 1) len = sizeof buf - 1;
        memcpy(buf, p, len);
        buf[len] = 0;
        while (len && (buf[len - 1] == '\r' || buf[len - 1] == ' ' || buf[len - 1] == '\t')) buf[--len] = 0;
        if (!strncmp(buf + strspn(buf, " "), "PID", 3)) strcpy(buf + strspn(buf, " ") + 3, " <masked>");
        char k[16];
        snprintf(k, sizeof k, "l%02d", line++);
        o_str(k, buf);
        if (!e) break;
        p = e + 1;
    }
    o_u32("lines", line);
    oracle_case_end();
}

int main(void)
{
    oracle_begin("sc/sc_service");
    /* Leftovers from an interrupted run. */
    SC_HANDLE m = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    SC_HANDLE s = m ? OpenServiceW(m, SVC, SERVICE_ALL_ACCESS) : NULL;
    if (s) DeleteService(s), CloseServiceHandle(s);

    sc_case("create", L"create " SVC L" binPath= \"C:\\setu-oracle\\missing\\none.exe -x\" type= own start= demand "
                      L"error= normal group= SetuOracleScGroup DisplayName= \"SetuOracle sc display\"");
    sc_case("create_duplicate", L"create " SVC L" binPath= C:\\setu-oracle\\x.exe");
    sc_case("qc", L"qc " SVC);
    sc_case("query", L"query " SVC);
    sc_case("queryex", L"queryex " SVC);
    sc_case("description", L"description " SVC L" \"SetuOracle sc description\"");
    sc_case("qdescription", L"qdescription " SVC);
    sc_case("config_start_auto", L"config " SVC L" start= auto error= ignore");
    sc_case("qc_after_config", L"qc " SVC);
    sc_case("failure", L"failure " SVC L" reset= 3600 actions= restart/5000/run/1000//0 command= \"C:\\setu-oracle\\x.exe\"");
    sc_case("qfailure", L"qfailure " SVC);
    sc_case("getdisplayname", L"getdisplayname " SVC);
    sc_case("getkeyname", L"getkeyname \"SetuOracle sc display\"");
    sc_case("start_missing_binary", L"start " SVC);
    sc_case("stop_not_started", L"stop " SVC);
    sc_case("delete", L"delete " SVC);
    sc_case("query_after_delete", L"query " SVC);
    sc_case("qc_missing", L"qc SetuOracle_sc_missing");
    sc_case("delete_missing", L"delete SetuOracle_sc_missing");
    sc_case("create_no_binpath", L"create SetuOracle_sc_2");

    s = m ? OpenServiceW(m, SVC, SERVICE_ALL_ACCESS) : NULL;
    if (s) DeleteService(s), CloseServiceHandle(s);
    s = m ? OpenServiceW(m, L"SetuOracle_sc_2", SERVICE_ALL_ACCESS) : NULL;
    if (s) DeleteService(s), CloseServiceHandle(s);
    if (m) CloseServiceHandle(m);
    return oracle_end();
}
