/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: job objects: create/open by name, assigning processes,
 * IsProcessInJob, basic accounting and process-id lists, extended limits
 * (KILL_ON_JOB_CLOSE, ACTIVE_PROCESS, DIE_ON_UNHANDLED_EXCEPTION), the
 * information-class size checks, TerminateJobObject, and a completion port
 * receiving job messages.
 *
 * Children are this .exe ("sleep" waits on an event that is never set;
 * "exit N" returns N). PIDs and times are never printed.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/procthread/job-objects
 *       https://learn.microsoft.com/en-us/windows/win32/api/jobapi2/nf-jobapi2-createjobobjectw
 *       https://learn.microsoft.com/en-us/windows/win32/api/jobapi2/nf-jobapi2-openjobobjectw
 *       https://learn.microsoft.com/en-us/windows/win32/api/jobapi2/nf-jobapi2-assignprocesstojobobject
 *       https://learn.microsoft.com/en-us/windows/win32/api/jobapi/nf-jobapi-isprocessinjob
 *       https://learn.microsoft.com/en-us/windows/win32/api/jobapi2/nf-jobapi2-queryinformationjobobject
 *       https://learn.microsoft.com/en-us/windows/win32/api/jobapi2/nf-jobapi2-setinformationjobobject
 *       https://learn.microsoft.com/en-us/windows/win32/api/jobapi2/nf-jobapi2-terminatejobobject
 *       https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-jobobject_basic_limit_information
 *       https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-jobobject_associate_completion_port
 */
#include "oracle.h"

static char self[MAX_PATH];

static BOOL spawn(const char *args, DWORD flags, PROCESS_INFORMATION *pi)
{
    char cmd[MAX_PATH + 64];
    STARTUPINFOA si = {.cb = sizeof si};
    snprintf(cmd, sizeof cmd, "\"%s\" %s", self, args);
    return CreateProcessA(NULL, cmd, NULL, NULL, FALSE, flags, NULL, NULL, &si, pi);
}

#define BOOL_CASE(name, expr)     \
    do {                          \
        BOOL ok_;                 \
        ORACLE_CALL(ok_, (expr)); \
        oracle_case(name);        \
        o_bool("ret", ok_);       \
        o_lasterr();              \
        oracle_case_end();        \
    } while (0)

static void in_job_case(const char *name, HANDLE proc, HANDLE job)
{
    BOOL in = 0x55, ok;
    ORACLE_CALL(ok, IsProcessInJob(proc, job, &in));
    oracle_case(name);
    o_bool("ret", ok);
    o_lasterr();
    o_i64("in_job", in);
    oracle_case_end();
}

static void accounting_case(const char *name, HANDLE job)
{
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION a;
    BOOL ok;
    DWORD len = 0x5555;
    memset(&a, 0xcc, sizeof a);
    ORACLE_CALL(ok, QueryInformationJobObject(job, JobObjectBasicAccountingInformation, &a, sizeof a, &len));
    oracle_case(name);
    o_bool("ret", ok);
    o_lasterr();
    o_u32("len", len);
    o_u32("total", a.TotalProcesses);
    o_u32("active", a.ActiveProcesses);
    o_u32("terminated", a.TotalTerminatedProcesses);
    oracle_case_end();
}

static void exit_code_case(const char *name, HANDLE h)
{
    DWORD code = 0xaaaaaaaa;
    WaitForSingleObject(h, INFINITE);
    GetExitCodeProcess(h, &code);
    oracle_case(name);
    o_u32("code", code);
    oracle_case_end();
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "sleep")) {
        HANDLE never = CreateEventW(NULL, TRUE, FALSE, NULL);
        WaitForSingleObject(never, INFINITE);
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "exit")) return atoi(argv[2]);
    oracle_begin("kernel32/job_objects");
    GetModuleFileNameA(NULL, self, MAX_PATH);
    BOOL ok;
    HANDLE job, job2;

    /* ---- create / open ---- */
    ORACLE_CALL(job, CreateJobObjectW(NULL, L"SetuOracleJob"));
    oracle_case("create_named");
    o_handle("ret", job);
    o_lasterr();
    oracle_case_end();
    ORACLE_CALL(job2, CreateJobObjectW(NULL, L"SetuOracleJob"));
    oracle_case("create_named_again");
    o_handle("ret", job2);
    o_lasterr();
    oracle_case_end();
    CloseHandle(job2);
    ORACLE_CALL(job2, OpenJobObjectW(JOB_OBJECT_QUERY, FALSE, L"SetuOracleJob"));
    oracle_case("open_named");
    o_handle("ret", job2);
    o_lasterr();
    oracle_case_end();
    ORACLE_CALL(job2, OpenJobObjectW(JOB_OBJECT_QUERY, FALSE, L"SetuOracleJobMissing"));
    oracle_case("open_missing");
    o_handle("ret", job2);
    o_lasterr();
    oracle_case_end();
    {
        HANDLE ev;
        ORACLE_CALL(ev, CreateEventW(NULL, FALSE, FALSE, L"SetuOracleJob"));
        oracle_case("event_with_job_name");
        o_handle("ret", ev);
        o_lasterr();
        oracle_case_end();
        if (ev) CloseHandle(ev);
    }
    CloseHandle(job);

    job = CreateJobObjectW(NULL, NULL);
    accounting_case("accounting_empty", job);
    in_job_case("self_in_new_job", GetCurrentProcess(), job);

    /* ---- information-class sizes ---- */
    {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION x;
        DWORD len = 0x5555;
        memset(&x, 0xcc, sizeof x);
        ORACLE_CALL(ok, QueryInformationJobObject(job, JobObjectExtendedLimitInformation, &x, sizeof x, &len));
        oracle_case("query_extended_limits_default");
        o_bool("ret", ok);
        o_lasterr();
        o_u32("len", len);
        o_hex32("flags", x.BasicLimitInformation.LimitFlags);
        o_u32("active_limit", x.BasicLimitInformation.ActiveProcessLimit);
        o_u64("process_mem", x.ProcessMemoryLimit);
        oracle_case_end();
        ORACLE_CALL(ok, QueryInformationJobObject(job, JobObjectExtendedLimitInformation, &x, sizeof x - 1, &len));
        oracle_case("query_extended_limits_short");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(ok, QueryInformationJobObject(job, JobObjectBasicAccountingInformation, &x, 4, NULL));
        oracle_case("query_accounting_short");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(ok, QueryInformationJobObject(job, (JOBOBJECTINFOCLASS)200, &x, sizeof x, NULL));
        oracle_case("query_bad_class");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();

        memset(&x, 0, sizeof x);
        x.BasicLimitInformation.LimitFlags = 0x80000000;
        BOOL_CASE("set_unknown_limit_flag",
                  SetInformationJobObject(job, JobObjectExtendedLimitInformation, &x, sizeof x));
        x.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        BOOL_CASE("set_kill_on_close_basic_class",
                  SetInformationJobObject(job, JobObjectBasicLimitInformation, &x, sizeof x.BasicLimitInformation));
        BOOL_CASE("set_extended_short",
                  SetInformationJobObject(job, JobObjectExtendedLimitInformation, &x, sizeof x - 4));
        BOOL_CASE("set_kill_on_close",
                  SetInformationJobObject(job, JobObjectExtendedLimitInformation, &x, sizeof x));
        memset(&x, 0xcc, sizeof x);
        QueryInformationJobObject(job, JobObjectExtendedLimitInformation, &x, sizeof x, NULL);
        oracle_case("query_after_kill_on_close");
        o_hex32("flags", x.BasicLimitInformation.LimitFlags);
        oracle_case_end();
    }

    /* ---- assign a child ---- */
    PROCESS_INFORMATION a, b;
    spawn("sleep", CREATE_SUSPENDED, &a);
    in_job_case("child_not_in_job", a.hProcess, job);
    BOOL_CASE("assign_child", AssignProcessToJobObject(job, a.hProcess));
    in_job_case("child_in_job", a.hProcess, job);
    in_job_case("child_in_any_job_after_assign", a.hProcess, NULL);
    BOOL_CASE("assign_child_twice", AssignProcessToJobObject(job, a.hProcess));
    accounting_case("accounting_one", job);
    {
        struct {
            JOBOBJECT_BASIC_PROCESS_ID_LIST l;
            ULONG_PTR more[8];
        } list;
        DWORD len = 0;
        memset(&list, 0, sizeof list);
        ORACLE_CALL(ok, QueryInformationJobObject(job, JobObjectBasicProcessIdList, &list, sizeof list, &len));
        oracle_case("process_id_list");
        o_bool("ret", ok);
        o_lasterr();
        o_u32("assigned", list.l.NumberOfAssignedProcesses);
        o_u32("in_list", list.l.NumberOfProcessIdsInList);
        o_bool("pid_matches", list.l.ProcessIdList[0] == a.dwProcessId);
        o_u32("len", len);
        oracle_case_end();
        JOBOBJECT_BASIC_PROCESS_ID_LIST small;
        memset(&small, 0, sizeof small);
        ORACLE_CALL(ok, QueryInformationJobObject(job, JobObjectBasicProcessIdList, &small,
                                                  offsetof(JOBOBJECT_BASIC_PROCESS_ID_LIST, ProcessIdList), &len));
        oracle_case("process_id_list_header_only");
        o_bool("ret", ok);
        o_lasterr();
        o_u32("assigned", small.NumberOfAssignedProcesses);
        o_u32("in_list", small.NumberOfProcessIdsInList);
        oracle_case_end();
    }

    /* A second job: nested jobs are allowed since Windows 8. */
    job2 = CreateJobObjectW(NULL, NULL);
    BOOL_CASE("assign_child_second_job", AssignProcessToJobObject(job2, a.hProcess));
    in_job_case("child_in_second_job", a.hProcess, job2);

    /* ---- terminate the job ---- */
    BOOL_CASE("terminate_job", TerminateJobObject(job, 0x77));
    exit_code_case("child_exit_after_terminate_job", a.hProcess);
    accounting_case("accounting_after_terminate", job);
    BOOL_CASE("assign_exited_process", AssignProcessToJobObject(job, a.hProcess));
    CloseHandle(a.hProcess);
    CloseHandle(a.hThread);
    CloseHandle(job2);

    /* ---- normal exit is counted ---- */
    spawn("exit 5", CREATE_SUSPENDED, &b);
    AssignProcessToJobObject(job, b.hProcess);
    ResumeThread(b.hThread);
    exit_code_case("child_exit_normal_in_job", b.hProcess);
    accounting_case("accounting_after_normal_exit", job);
    CloseHandle(b.hProcess);
    CloseHandle(b.hThread);

    /* ---- kill on job close ---- */
    spawn("sleep", 0, &a);
    AssignProcessToJobObject(job, a.hProcess);
    CloseHandle(job);
    exit_code_case("kill_on_close_exit_code", a.hProcess);
    CloseHandle(a.hProcess);
    CloseHandle(a.hThread);

    /* ---- active process limit ---- */
    {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION x;
        job = CreateJobObjectW(NULL, NULL);
        memset(&x, 0, sizeof x);
        x.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_ACTIVE_PROCESS | JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        x.BasicLimitInformation.ActiveProcessLimit = 1;
        BOOL_CASE("set_active_limit_1", SetInformationJobObject(job, JobObjectExtendedLimitInformation, &x, sizeof x));
        spawn("sleep", CREATE_SUSPENDED, &a);
        spawn("sleep", CREATE_SUSPENDED, &b);
        BOOL_CASE("active_limit_first", AssignProcessToJobObject(job, a.hProcess));
        BOOL_CASE("active_limit_second", AssignProcessToJobObject(job, b.hProcess));
        accounting_case("accounting_active_limit", job);
        DWORD w = WaitForSingleObject(b.hProcess, 5000);
        oracle_case("active_limit_second_state");
        o_bool("exited", w == WAIT_OBJECT_0);
        oracle_case_end();
        TerminateProcess(b.hProcess, 1);
        CloseHandle(job);
        exit_code_case("active_limit_first_killed_on_close", a.hProcess);
        CloseHandle(a.hProcess);
        CloseHandle(a.hThread);
        CloseHandle(b.hProcess);
        CloseHandle(b.hThread);
    }

    /* ---- completion port messages ---- */
    {
        HANDLE port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1);
        job = CreateJobObjectW(NULL, NULL);
        JOBOBJECT_ASSOCIATE_COMPLETION_PORT acp = {(void *)0x1234, port};
        BOOL_CASE("associate_port", SetInformationJobObject(job, JobObjectAssociateCompletionPortInformation, &acp, sizeof acp));
        spawn("exit 3", CREATE_SUSPENDED, &a);
        AssignProcessToJobObject(job, a.hProcess);
        ResumeThread(a.hThread);
        WaitForSingleObject(a.hProcess, INFINITE);
        char msgs[128] = "";
        for (int i = 0; i < 6; i++) {
            DWORD msg;
            ULONG_PTR key;
            OVERLAPPED *ov;
            if (!GetQueuedCompletionStatus(port, &msg, &key, &ov, 2000)) break;
            char t[32];
            snprintf(t, sizeof t, "%s%lu%s", *msgs ? "," : "", msg,
                     (ULONG_PTR)ov == a.dwProcessId ? "(pid)" : key == 0x1234 ? "" : "(badkey)");
            strcat(msgs, t);
            if (msg == JOB_OBJECT_MSG_ACTIVE_PROCESS_ZERO) break;
        }
        oracle_case("port_messages");
        o_str("messages", msgs);
        oracle_case_end();
        BOOL_CASE("associate_port_again", SetInformationJobObject(job, JobObjectAssociateCompletionPortInformation, &acp, sizeof acp));
        CloseHandle(a.hProcess);
        CloseHandle(a.hThread);
        CloseHandle(job);
        CloseHandle(port);
    }

    /* ---- the unhandled-exception limit flag is accepted ---- */
    {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION x;
        job = CreateJobObjectW(NULL, NULL);
        memset(&x, 0, sizeof x);
        x.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION |
                                             JOB_OBJECT_LIMIT_BREAKAWAY_OK | JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK;
        BOOL_CASE("set_die_on_unhandled_breakaway",
                  SetInformationJobObject(job, JobObjectExtendedLimitInformation, &x, sizeof x));
        memset(&x, 0xcc, sizeof x);
        QueryInformationJobObject(job, JobObjectExtendedLimitInformation, &x, sizeof x, NULL);
        oracle_case("query_die_on_unhandled_breakaway");
        o_hex32("flags", x.BasicLimitInformation.LimitFlags);
        oracle_case_end();
        CloseHandle(job);
    }

    BOOL_CASE("terminate_null_job", TerminateJobObject(NULL, 1));
    BOOL_CASE("assign_null_process", AssignProcessToJobObject(CreateJobObjectW(NULL, NULL), NULL));

    return oracle_end();
}
