/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: reg.exe confirmation prompts answered on stdin. Each case runs
 * "cmd.exe /c (echo ...)| reg.exe ..." so the answers arrive through a pipe:
 * add overwriting a value, delete of a value / all values / a key, copy onto
 * existing values (Yes/No/All) and export onto an existing file. Each case
 * prints the exit code, stdout, stderr and the registry state afterwards.
 * Every case gives exactly one answer line: reg.exe may read the whole pipe
 * at once, and a prompt at end-of-file repeats until the process is killed.
 *
 * Docs: https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/reg-add
 *       https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/reg-delete
 *       https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/reg-copy
 *       https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/reg-export
 */
#include "regtool.h"

#define K L"HKCU\\Software\\SetuOracle\\reg_prompts"
#define SUB L"Software\\SetuOracle\\reg_prompts"

static HKEY root;

static void sz(HKEY k, const wchar_t *name, const wchar_t *v)
{
    RegSetValueExW(k, name, 0, REG_SZ, (const BYTE *)v, (DWORD)(wcslen(v) + 1) * 2);
}

static HKEY mk(const wchar_t *path)
{
    HKEY k = NULL;
    RegCreateKeyExW(root, path, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL);
    return k;
}

/* Fresh "w" with values a, b and subkey "s" holding value c; and "src" with
 * a, b (different data) and new. */
static void reset(void)
{
    HKEY k, s;
    RegDeleteTreeW(root, L"w");
    RegDeleteTreeW(root, L"src");
    RegDeleteTreeW(root, L"src1");
    k = mk(L"w");
    sz(k, L"a", L"old a");
    sz(k, L"b", L"old b");
    s = mk(L"w\\s");
    sz(s, L"c", L"old c");
    RegCloseKey(s);
    RegCloseKey(k);
    k = mk(L"src");
    sz(k, L"a", L"new a");
    sz(k, L"b", L"new b");
    sz(k, L"new", L"added");
    RegCloseKey(k);
    k = mk(L"src1");
    sz(k, L"a", L"new a");
    sz(k, L"new", L"added");
    RegCloseKey(k);
}

/* input is a cmd.exe command producing the answers, e.g. "echo y". */
static void prompt_case(const char *name, const wchar_t *input, const wchar_t *args)
{
    static wchar_t cmd[2048];
    reset();
    swprintf(cmd, 2048, L"cmd.exe /d /c \"(%ls)| reg.exe %ls\"", input, args);
    rt_run(cmd);
    oracle_case(name);
    o_u32("exit", rt_exit);
    o_str("out", rt_out);
    o_str("err", rt_err);
    o_regtree("tree", HKEY_CURRENT_USER, SUB L"\\w");
    oracle_case_end();
}

static void export_case(const char *name, const wchar_t *input)
{
    static wchar_t cmd[2048];
    reset();
    rt_write_ansi(L"exists.reg", "placeholder\r\n");
    swprintf(cmd, 2048, L"cmd.exe /d /c \"(%ls)| reg.exe export %ls\\w exists.reg\"", input, K);
    rt_run(cmd);
    oracle_case(name);
    o_u32("exit", rt_exit);
    o_str("out", rt_out);
    o_str("err", rt_err);
    o_text_file("file", L"exists.reg");
    oracle_case_end();
    DeleteFileW(L"exists.reg");
}

int main(void)
{
    oracle_begin("reg/prompts");
    root = oracle_reg_root();

    prompt_case("add_overwrite_yes", L"echo y", L"add " K L"\\w /v a /d fresh");
    prompt_case("add_overwrite_upper_yes", L"echo Y", L"add " K L"\\w /v a /d fresh");
    prompt_case("add_overwrite_word_yes", L"echo yes", L"add " K L"\\w /v a /d fresh");
    prompt_case("add_overwrite_no", L"echo n", L"add " K L"\\w /v a /d fresh");
    prompt_case("add_overwrite_default_yes", L"echo y", L"add " K L"\\w /ve /d fresh");
    prompt_case("add_new_value_no_prompt", L"echo n", L"add " K L"\\w /v z /d fresh");
    prompt_case("add_overwrite_f_ignores_input", L"echo n", L"add " K L"\\w /v a /d fresh /f");

    prompt_case("delete_value_yes", L"echo y", L"delete " K L"\\w /v a");
    prompt_case("delete_value_no", L"echo n", L"delete " K L"\\w /v a");
    prompt_case("delete_va_yes", L"echo y", L"delete " K L"\\w /va");
    prompt_case("delete_va_no", L"echo n", L"delete " K L"\\w /va");
    prompt_case("delete_key_yes", L"echo y", L"delete " K L"\\w");
    prompt_case("delete_key_no", L"echo n", L"delete " K L"\\w");
    prompt_case("delete_missing_value_no_prompt", L"echo y", L"delete " K L"\\w /v nosuch");

    prompt_case("copy_one_conflict_yes", L"echo y", L"copy " K L"\\src1 " K L"\\w");
    prompt_case("copy_one_conflict_no", L"echo n", L"copy " K L"\\src1 " K L"\\w");
    prompt_case("copy_one_conflict_all", L"echo a", L"copy " K L"\\src1 " K L"\\w");
    prompt_case("copy_overwrite_all", L"echo a", L"copy " K L"\\src " K L"\\w");

    export_case("export_overwrite_yes", L"echo y");
    export_case("export_overwrite_no", L"echo n");

    RegDeleteTreeW(root, L"w");
    RegDeleteTreeW(root, L"src");
    RegDeleteTreeW(root, L"src1");
    RegCloseKey(root);
    oracle_reg_cleanup();
    return oracle_end();
}
