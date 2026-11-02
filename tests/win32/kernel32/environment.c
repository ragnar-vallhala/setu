/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: Get/SetEnvironmentVariableW, ExpandEnvironmentStringsW and
 * GetEnvironmentStringsW, using only variables the test sets itself.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/processenv/nf-processenv-getenvironmentvariablew
 *       https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-setenvironmentvariable
 *       https://learn.microsoft.com/en-us/windows/win32/api/processenv/nf-processenv-expandenvironmentstringsw
 *       https://learn.microsoft.com/en-us/windows/win32/api/processenv/nf-processenv-getenvironmentstringsw
 *       https://learn.microsoft.com/en-us/windows/win32/procthread/environment-variables
 */
#include "oracle.h"

static void get_case(const char *name, const wchar_t *var, DWORD size)
{
    wchar_t buf[64];
    DWORD r;
    wmemset(buf, L'#', 64);
    buf[63] = 0;
    ORACLE_CALL(r, GetEnvironmentVariableW(var, size ? buf : NULL, size));
    oracle_case(name);
    o_u32("ret", r);
    o_lasterr();
    if (size && r < size) o_wstr("value", buf);
    if (size && r >= size) {
        o_bool("buf_untouched", buf[0] == L'#');
        o_bytes("buf_head", buf, 8 * sizeof(wchar_t)); /* what a too-small call wrote */
    }
    oracle_case_end();
}

static void set_case(const char *name, const wchar_t *var, const wchar_t *val)
{
    BOOL ok;
    ORACLE_CALL(ok, SetEnvironmentVariableW(var, val));
    oracle_case(name);
    o_bool("ret", ok);
    o_lasterr();
    oracle_case_end();
}

static void expand_case(const char *name, const wchar_t *in, DWORD size)
{
    wchar_t buf[256];
    DWORD r;
    wmemset(buf, L'#', 256);
    buf[255] = 0;
    ORACLE_CALL(r, ExpandEnvironmentStringsW(in, size ? buf : NULL, size));
    oracle_case(name);
    o_u32("ret", r);
    o_lasterr();
    if (size && r && r <= size) o_wstr("out", buf);
    oracle_case_end();
}

int main(void)
{
    oracle_begin("kernel32/environment");

    set_case("set_new", L"SETU_ORACLE_A", L"hello");
    get_case("get_exact", L"SETU_ORACLE_A", 64);
    get_case("get_other_case", L"setu_oracle_a", 64);
    get_case("get_size_exact_len", L"SETU_ORACLE_A", 5);
    get_case("get_size_len_plus_one", L"SETU_ORACLE_A", 6);
    get_case("get_null_buffer", L"SETU_ORACLE_A", 0);
    get_case("get_missing", L"SETU_ORACLE_MISSING", 64);
    get_case("get_missing_null_buffer", L"SETU_ORACLE_MISSING", 0);

    set_case("set_empty_value", L"SETU_ORACLE_EMPTY", L"");
    get_case("get_empty_value", L"SETU_ORACLE_EMPTY", 64);
    get_case("get_empty_value_null_buffer", L"SETU_ORACLE_EMPTY", 0);

    set_case("set_overwrite", L"setu_oracle_a", L"world");
    get_case("get_after_case_insensitive_overwrite", L"SETU_ORACLE_A", 64);
    set_case("delete", L"SETU_ORACLE_A", NULL);
    get_case("get_deleted", L"SETU_ORACLE_A", 64);
    set_case("delete_missing", L"SETU_ORACLE_NEVER", NULL);

    set_case("set_name_with_equals", L"SETU=BAD", L"x");
    get_case("get_name_with_equals", L"SETU=BAD", 64);
    set_case("set_empty_name", L"", L"x");
    get_case("get_empty_name", L"", 64);
    set_case("set_leading_equals", L"=SETU_HIDDEN", L"h");
    get_case("get_leading_equals", L"=SETU_HIDDEN", 64);
    set_case("set_value_with_equals", L"SETU_ORACLE_EQ", L"a=b");
    get_case("get_value_with_equals", L"SETU_ORACLE_EQ", 64);
    set_case("set_name_with_space", L"SETU ORACLE", L"sp");
    get_case("get_name_with_space", L"SETU ORACLE", 64);
    get_case("get_name_with_trailing_space", L"SETU_ORACLE_EQ ", 64);

    SetEnvironmentVariableW(L"SETU_X", L"ex");
    SetEnvironmentVariableW(L"SETU_NEST", L"%SETU_X%");
    expand_case("expand_simple", L"a%SETU_X%b", 256);
    expand_case("expand_case_insensitive", L"%setu_x%", 256);
    expand_case("expand_undefined_kept", L"%SETU_UNDEFINED%", 256);
    expand_case("expand_not_recursive", L"%SETU_NEST%", 256);
    expand_case("expand_lone_percent", L"100% sure", 256);
    expand_case("expand_double_percent", L"%%SETU_X%%", 256);
    expand_case("expand_unterminated", L"%SETU_X", 256);
    expand_case("expand_empty_name", L"a%%b", 256);
    expand_case("expand_empty_input", L"", 256);
    expand_case("expand_null_buffer", L"a%SETU_X%b", 0);
    expand_case("expand_buffer_too_small", L"a%SETU_X%b", 3);
    expand_case("expand_buffer_exact_no_nul", L"a%SETU_X%b", 4);
    expand_case("expand_buffer_exact", L"a%SETU_X%b", 5);
    expand_case("expand_empty_variable", L"[%SETU_ORACLE_EMPTY%]", 256);
    expand_case("expand_hidden_variable", L"%=SETU_HIDDEN%", 256);

    {   /* The block must contain our variable once, in the exact case we set. */
        SetEnvironmentVariableW(L"Setu_Block", L"v");
        wchar_t *blk = GetEnvironmentStringsW();
        int found = 0, found_other_case = 0, sorted = 1;
        const wchar_t *prev = NULL;
        for (wchar_t *p = blk; *p; p += wcslen(p) + 1) {
            if (!wcscmp(p, L"Setu_Block=v")) found++;
            else if (!_wcsicmp(p, L"Setu_Block=v")) found_other_case++;
            if (p[0] != L'=' && prev && prev[0] != L'=' && _wcsicmp(prev, p) > 0) sorted = 0;
            prev = p;
        }
        oracle_case("block_contains_variable");
        o_u32("found", found);
        o_u32("found_other_case", found_other_case);
        oracle_case_end();
        oracle_case("block_case_insensitively_sorted");
        o_bool("sorted", sorted);
        oracle_case_end();
        BOOL ok;
        ORACLE_CALL(ok, FreeEnvironmentStringsW(blk));
        oracle_case("free_block");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
    }

    {   /* SetEnvironmentStringsW replaces the whole block. */
        static wchar_t block[] = L"SETU_ONLY=1\0Zeta=z\0";
        BOOL ok;
        ORACLE_CALL(ok, SetEnvironmentStringsW(block));
        oracle_case("set_environment_strings");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        get_case("after_replace_new", L"SETU_ONLY", 64);
        get_case("after_replace_old_gone", L"SETU_X", 64);
    }

    return oracle_end();
}
