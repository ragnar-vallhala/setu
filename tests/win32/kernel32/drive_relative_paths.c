/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: drive-relative paths ("C:foo") and the per-drive current
 * directories.
 *
 * Win32 keeps one current directory per process; for other drives it reads
 * the hidden environment variables "=X:" (set by cmd.exe and inherited).
 * "X:foo" resolves against "=X:" when X is not the current drive, else
 * against the current directory; with no "=X:" variable it resolves against
 * "X:\". This test sets "=Q:" (Q: does not exist on either side, and
 * GetFullPathNameW does not touch the disk), checks GetFullPathNameW and the
 * file APIs, and checks whether SetCurrentDirectoryW updates "=X:".
 *
 * The current drive letter is printed as "<drive>" and the current
 * directory as "<cwd>".
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/fileio/naming-a-file
 *       https://learn.microsoft.com/en-us/dotnet/standard/io/file-path-formats
 *       https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getfullpathnamew
 *       https://devblogs.microsoft.com/oldnewthing/20100506-00/?p=14133
 */
#include "oracle.h"

static wchar_t cwd[MAX_PATH], parent[MAX_PATH], drv[3]; /* "C:" */

static const char *tokenise(const wchar_t *s)
{
    static wchar_t out[2 * MAX_PATH];
    size_t n = wcslen(cwd), np = wcslen(parent);
    if (!_wcsnicmp(s, cwd, n) && (s[n] == 0 || s[n] == L'\\')) swprintf(out, 2 * MAX_PATH, L"<cwd>%ls", s + n);
    else if (np && !_wcsnicmp(s, parent, np) && (s[np] == 0 || s[np] == L'\\'))
        swprintf(out, 2 * MAX_PATH, L"<parent>%ls", s + np);
    else if (towupper(s[0]) == towupper(drv[0]) && s[1] == L':')
        swprintf(out, 2 * MAX_PATH, L"<drive>%ls", s + 1);
    else wcscpy(out, s);
    return oracle_utf8(out);
}

static void full_case(const char *name, const wchar_t *in)
{
    wchar_t buf[2 * MAX_PATH];
    wchar_t *part = NULL;
    DWORD r;
    ORACLE_CALL(r, GetFullPathNameW(in, 2 * MAX_PATH, buf, &part));
    oracle_case(name);
    o_lasterr();
    if (r && r < 2 * MAX_PATH) {
        o_bool("ret_is_len", r == wcslen(buf));
        o_str("full", tokenise(buf));
        o_wstr("file_part", part);
    } else {
        o_u32("ret", r);
    }
    oracle_case_end();
}

static void env_case(const char *name, const wchar_t *var)
{
    wchar_t buf[MAX_PATH];
    DWORD r;
    ORACLE_CALL(r, GetEnvironmentVariableW(var, buf, MAX_PATH));
    oracle_case(name);
    o_bool("exists", r != 0);
    o_lasterr();
    if (r && r < MAX_PATH) o_str("value", tokenise(buf));
    oracle_case_end();
}

static void q(const wchar_t *value)
{
    SetEnvironmentVariableW(L"=Q:", value);
}

int main(void)
{
    oracle_begin("kernel32/drive_relative_paths");
    GetCurrentDirectoryW(MAX_PATH, cwd);
    drv[0] = cwd[0];
    drv[1] = L':';
    wcscpy(parent, cwd);
    if (wcsrchr(parent, L'\\')) *wcsrchr(parent, L'\\') = 0;
    wchar_t in[MAX_PATH], var[8];
    swprintf(var, 8, L"=%lc:", towupper(drv[0]));

    /* ---- another drive, with and without "=Q:" ---- */
    q(NULL);
    full_case("other_drive_no_var", L"Q:foo");
    full_case("other_drive_only_no_var", L"Q:");
    full_case("other_drive_no_var_lowercase_letter", L"q:foo");
    env_case("other_drive_no_var_not_created", L"=Q:");
    q(L"Q:\\qdir\\sub");
    full_case("other_drive_var", L"Q:foo");
    env_case("other_drive_var_missing_drive_kept", L"=Q:");
    full_case("other_drive_var_lowercase_letter", L"q:foo");
    full_case("other_drive_var_dotdot", L"Q:..\\x");
    full_case("other_drive_var_dotdot_past_root", L"Q:..\\..\\..\\x");
    full_case("other_drive_var_dot", L"Q:.");
    full_case("other_drive_only_var", L"Q:");
    full_case("other_drive_absolute_ignores_var", L"Q:\\abs");
    full_case("other_drive_forward_slash", L"Q:a/b");
    q(L"Q:\\qdir\\sub\\");
    full_case("other_drive_var_trailing_sep", L"Q:foo");
    q(L"q:\\lower");
    full_case("other_drive_var_lowercase_value", L"Q:foo");
    q(L"R:\\elsewhere");
    full_case("other_drive_var_names_other_drive", L"Q:foo");
    q(L"relative\\dir");
    full_case("other_drive_var_relative_value", L"Q:foo");
    q(L"");
    full_case("other_drive_var_empty", L"Q:foo");
    q(L"Q:\\qdir\\sub");
    SetEnvironmentVariableW(L"=q:", NULL);
    full_case("other_drive_var_deleted_lowercase_name", L"Q:foo");
    q(NULL);
    SetEnvironmentVariableW(L"=q:", L"Q:\\viaLower");
    full_case("other_drive_var_set_lowercase_name", L"Q:foo");
    SetEnvironmentVariableW(L"=q:", NULL);

    /* ---- the current drive uses the current directory, not "=C:" ---- */
    SetEnvironmentVariableW(var, NULL);
    swprintf(in, MAX_PATH, L"%ls%ls", drv, L"foo");
    full_case("current_drive_relative", in);
    {
        wchar_t v[MAX_PATH];
        swprintf(v, MAX_PATH, L"%lc:\\setu_no_such_dir", drv[0]);
        SetEnvironmentVariableW(var, v);
        full_case("current_drive_ignores_var", in);
        swprintf(in, MAX_PATH, L"%lc:foo", towlower(drv[0]));
        full_case("current_drive_lowercase_letter", in);
        SetEnvironmentVariableW(var, NULL);
    }
    swprintf(in, MAX_PATH, L"%ls..\\x", drv);
    full_case("current_drive_dotdot", in);

    /* ---- SetCurrentDirectoryW and the "=X:" variables ---- */
    CreateDirectoryW(L"sub", NULL);
    SetEnvironmentVariableW(var, NULL);
    SetCurrentDirectoryW(L"sub");
    env_case("setcd_does_not_set_var", var);
    SetCurrentDirectoryW(cwd);
    {
        BOOL ok;
        swprintf(in, MAX_PATH, L"%lssub", drv);
        ORACLE_CALL(ok, SetCurrentDirectoryW(in));
        oracle_case("setcd_drive_relative");
        o_bool("ret", ok);
        o_lasterr();
        wchar_t now[MAX_PATH];
        GetCurrentDirectoryW(MAX_PATH, now);
        o_str("now", tokenise(now));
        oracle_case_end();
        SetCurrentDirectoryW(cwd);
        ORACLE_CALL(ok, SetCurrentDirectoryW(drv));
        oracle_case("setcd_drive_only");
        o_bool("ret", ok);
        o_lasterr();
        GetCurrentDirectoryW(MAX_PATH, now);
        o_str("now", tokenise(now));
        oracle_case_end();
        SetCurrentDirectoryW(cwd);
        q(L"Q:\\qdir");
        ORACLE_CALL(ok, SetCurrentDirectoryW(L"Q:"));
        oracle_case("setcd_missing_drive_with_var");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        SetCurrentDirectoryW(cwd);
        q(NULL);
    }

    /* ---- file APIs with drive-relative names ---- */
    {
        HANDLE h;
        swprintf(in, MAX_PATH, L"%lsrel.txt", drv);
        ORACLE_CALL(h, CreateFileW(in, GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL));
        oracle_case("create_current_drive_relative");
        o_handle("ret", h);
        o_lasterr();
        oracle_case_end();
        CloseHandle(h);
        oracle_case("created_in_cwd");
        o_bool("exists", GetFileAttributesW(L"rel.txt") != INVALID_FILE_ATTRIBUTES);
        oracle_case_end();
        swprintf(in, MAX_PATH, L"%lssub\\..\\rel.txt", drv);
        DWORD a;
        ORACLE_CALL(a, GetFileAttributesW(in));
        oracle_case("attr_current_drive_relative_dotdot");
        o_hex32("ret", a);
        o_lasterr();
        oracle_case_end();
        q(L"Q:\\qdir");
        ORACLE_CALL(h, CreateFileW(L"Q:rel.txt", GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL));
        oracle_case("create_missing_drive_relative");
        o_handle("ret", h);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(a, GetFileAttributesW(L"Q:"));
        oracle_case("attr_missing_drive_only");
        o_hex32("ret", a);
        o_lasterr();
        oracle_case_end();
        q(NULL);
    }

    /* ---- deeper "=Q:" values ---- */
    q(L"Q:\\a1\\b2\\c3");
    full_case("other_drive_var_deep", L"Q:a\\b");
    full_case("other_drive_var_deep_dotdot", L"Q:..\\..\\x");
    q(L"Q:\\a1\\..\\b2");
    full_case("other_drive_var_unnormalised_value", L"Q:x");
    q(L"Q:/fw/slash");
    full_case("other_drive_var_forward_slash_value", L"Q:x");
    q(NULL);

    /* ---- Q: as an existing drive (a DefineDosDeviceW mapping of the cwd) ---- */
    {
        BOOL ok;
        ORACLE_CALL(ok, DefineDosDeviceW(0, L"Q:", cwd));
        oracle_case("define_q_drive");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        CreateDirectoryW(L"sub", NULL);
        CreateDirectoryW(L"sub\\deeper", NULL);
        q(NULL);
        full_case("mapped_drive_no_var", L"Q:foo");
        q(L"Q:\\sub");
        full_case("mapped_drive_var_existing_dir", L"Q:foo");
        full_case("mapped_drive_var_existing_dir_dotdot", L"Q:..\\x");
        full_case("mapped_drive_var_existing_dir_only", L"Q:");
        q(L"Q:\\sub\\deeper\\");
        full_case("mapped_drive_var_trailing_sep", L"Q:foo");
        full_case("mapped_drive_var_trailing_sep_dir_only", L"Q:");
        full_case("mapped_drive_var_trailing_sep_dotdot", L"Q:..\\x");
        q(L"Q:\\sub\\\\deeper");
        full_case("mapped_drive_var_double_sep_inside", L"Q:foo");
        full_case("mapped_drive_absolute_double_sep", L"Q:\\sub\\\\deeper\\x");
        q(L"q:\\sub");
        full_case("mapped_drive_var_lowercase_value", L"Q:foo");
        full_case("mapped_drive_lowercase_input_lowercase_value", L"q:foo");
        q(NULL);
        full_case("mapped_drive_no_var_lowercase_letter", L"q:foo");
        env_case("mapped_drive_no_var_not_created", L"=Q:");
        q(L"Q:\\nodir");
        full_case("mapped_drive_var_missing_dir", L"Q:foo");
        env_case("mapped_drive_var_missing_dir_kept", L"=Q:");
        q(L"R:\\sub");
        full_case("mapped_drive_var_other_letter", L"Q:foo");
        q(L"sub");
        full_case("mapped_drive_var_relative_value", L"Q:foo");
        env_case("mapped_drive_var_relative_value_kept", L"=Q:");
        q(L"Q:\\sub");
        {
            HANDLE h;
            ORACLE_CALL(h, CreateFileW(L"Q:viavar.txt", GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL));
            oracle_case("mapped_drive_create_relative");
            o_handle("ret", h);
            o_lasterr();
            oracle_case_end();
            if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
            oracle_case("mapped_drive_create_relative_where");
            o_bool("in_sub", GetFileAttributesW(L"sub\\viavar.txt") != INVALID_FILE_ATTRIBUTES);
            o_bool("in_root", GetFileAttributesW(L"viavar.txt") != INVALID_FILE_ATTRIBUTES);
            oracle_case_end();
            wchar_t now[MAX_PATH];
            ORACLE_CALL(ok, SetCurrentDirectoryW(L"Q:"));
            oracle_case("mapped_drive_setcd_drive_only");
            o_bool("ret", ok);
            o_lasterr();
            GetCurrentDirectoryW(MAX_PATH, now);
            o_str("now", tokenise(now));
            oracle_case_end();
            /* Now Q: is the current drive: "C:x" consults "=C:". */
            SetEnvironmentVariableW(var, NULL);
            swprintf(in, MAX_PATH, L"%lsfoo", drv);
            full_case("after_switch_old_drive_no_var", in);
            env_case("after_switch_c_var_after_full", var);
            SetCurrentDirectoryW(cwd);
            env_case("after_switch_back_q_var", L"=Q:");
            env_case("after_switch_back_c_var", var);

            /* Probes: which "=X:" variables SetCurrentDirectoryW writes. */
            SetEnvironmentVariableW(var, NULL);
            q(NULL);
            SetCurrentDirectoryW(L"Q:\\sub");
            env_case("probe_to_q_abs_c_var", var);
            env_case("probe_to_q_abs_q_var", L"=Q:");
            SetCurrentDirectoryW(L"Q:\\sub\\deeper");
            env_case("probe_within_q_c_var", var);
            env_case("probe_within_q_q_var", L"=Q:");
            swprintf(in, MAX_PATH, L"%ls\\sub", cwd);
            SetCurrentDirectoryW(in);
            env_case("probe_back_to_c_sub_c_var", var);
            env_case("probe_back_to_c_sub_q_var", L"=Q:");
            SetCurrentDirectoryW(cwd);
            env_case("probe_within_c_c_var", var);
            env_case("probe_within_c_q_var", L"=Q:");
            SetEnvironmentVariableW(var, NULL);
            q(NULL);
        }
        q(NULL);
        DefineDosDeviceW(DDD_REMOVE_DEFINITION, L"Q:", NULL);
    }
    return oracle_end();
}
