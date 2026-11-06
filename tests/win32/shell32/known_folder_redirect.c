/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: redirecting a per-user known folder (Videos) to a directory
 * that doesn't exist yet with SHSetKnownFolderPath / SHSetFolderPathW, then
 * reading it back with SHGetKnownFolderPath (no flags, KF_FLAG_DONT_VERIFY,
 * KF_FLAG_CREATE, KF_FLAG_DEFAULT_PATH), SHGetFolderPathW (CSIDL_FLAG_CREATE,
 * CSIDL_FLAG_DONT_VERIFY, SHGFP_TYPE_DEFAULT) and SHGetFolderPathAndSubDirW;
 * whether each call creates the directory; and what lands in the
 * User Shell Folders registry key (value names, type, unexpanded form).
 *
 * The test saves every value of HKCU\...\Explorer\User Shell Folders and
 * Shell Folders first and restores them at the end, so a second run sees the
 * same starting state. Paths print with the working directory as "<cwd>"
 * and the user's profile as "<profile>".
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/nf-shlobj_core-shsetknownfolderpath
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/nf-shlobj_core-shgetknownfolderpath
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/ne-shlobj_core-known_folder_flag
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/nf-shlobj_core-shgetfolderpathw
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/nf-shlobj_core-shgetfolderpathandsubdirw
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/nf-shlobj_core-shsetfolderpathw
 *       https://learn.microsoft.com/en-us/windows/win32/shell/csidl
 */
#define INITGUID
#include "oracle.h"
#include <shlobj.h>
#include <knownfolders.h>

static HRESULT(WINAPI *pSHGetKnownFolderPath)(const GUID *, DWORD, HANDLE, PWSTR *);
static HRESULT(WINAPI *pSHSetKnownFolderPath)(const GUID *, DWORD, HANDLE, PCWSTR);
static HRESULT(WINAPI *pSHGetFolderPathW)(HWND, int, HANDLE, DWORD, LPWSTR);
static HRESULT(WINAPI *pSHSetFolderPathW)(int, HANDLE, DWORD, LPCWSTR);
static HRESULT(WINAPI *pSHGetFolderPathAndSubDirW)(HWND, int, HANDLE, DWORD, LPCWSTR, LPWSTR);
static void(WINAPI *pCoTaskMemFree)(void *);

#define LOAD(m, f) (*(FARPROC *)&p##f = GetProcAddress(m, #f))

static wchar_t cwd[MAX_PATH], profile[MAX_PATH];

static int has_prefix_ci(const wchar_t *s, const wchar_t *p)
{
    size_t n = wcslen(p);
    return n && !_wcsnicmp(s, p, n) && (s[n] == 0 || s[n] == L'\\');
}

static void o_path(const char *k, const wchar_t *s)
{
    wchar_t out[2 * MAX_PATH];
    if (!s) { o_str(k, NULL); return; }
    if (has_prefix_ci(s, cwd)) swprintf(out, 2 * MAX_PATH, L"<cwd>%ls", s + wcslen(cwd));
    else if (has_prefix_ci(s, profile)) swprintf(out, 2 * MAX_PATH, L"<profile>%ls", s + wcslen(profile));
    else swprintf(out, 2 * MAX_PATH, L"%ls", s);
    o_wstr(k, out);
}

static int dir_exists(const wchar_t *p)
{
    DWORD a = GetFileAttributesW(p);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

/* ---- save / restore the two Explorer folder keys ---- */
#define EXPLORER L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\"
struct saved_value { wchar_t name[256]; DWORD type, size; BYTE data[2048]; };
struct saved_key { const wchar_t *path; int n; struct saved_value v[128]; };
static struct saved_key saved[2] = {{EXPLORER L"User Shell Folders", 0, {{{0}, 0, 0, {0}}}},
                                    {EXPLORER L"Shell Folders", 0, {{{0}, 0, 0, {0}}}}};

static void save_key(struct saved_key *k)
{
    HKEY h;
    k->n = 0;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, k->path, 0, KEY_READ, &h)) return;
    for (DWORD i = 0; k->n < 128; i++) {
        struct saved_value *v = &k->v[k->n];
        DWORD nl = 256;
        v->size = sizeof v->data;
        if (RegEnumValueW(h, i, v->name, &nl, NULL, &v->type, v->data, &v->size)) break;
        k->n++;
    }
    RegCloseKey(h);
}

static void restore_key(struct saved_key *k)
{
    HKEY h;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, k->path, 0, KEY_READ | KEY_WRITE, &h)) return;
    /* Delete values that weren't there before, then rewrite the saved ones. */
    for (;;) {
        wchar_t name[256];
        int deleted = 0;
        for (DWORD i = 0;; i++) {
            DWORD nl = 256;
            if (RegEnumValueW(h, i, name, &nl, NULL, NULL, NULL, NULL)) break;
            int found = 0;
            for (int j = 0; j < k->n; j++)
                if (!_wcsicmp(k->v[j].name, name)) found = 1;
            if (!found) { RegDeleteValueW(h, name); deleted = 1; break; }
        }
        if (!deleted) break;
    }
    for (int j = 0; j < k->n; j++)
        RegSetValueExW(h, k->v[j].name, 0, k->v[j].type, k->v[j].data, k->v[j].size);
    RegCloseKey(h);
}

/* Print what the Videos folder looks like in User Shell Folders now. */
static void o_usf(const char *prefix)
{
    static const wchar_t *names[] = {L"My Video", L"{18989B1D-99B5-455B-841C-AB7C74E4DDFC}"};
    static const char *keys[] = {"legacy_name", "guid_name"};
    HKEY h;
    char k[64];
    if (RegOpenKeyExW(HKEY_CURRENT_USER, EXPLORER L"User Shell Folders", 0, KEY_READ, &h)) {
        snprintf(k, sizeof k, "%s.open", prefix);
        o_bool(k, 0);
        return;
    }
    for (int i = 0; i < 2; i++) {
        wchar_t buf[MAX_PATH * 2];
        DWORD type = 0, size = sizeof buf - sizeof(wchar_t);
        LONG r = RegQueryValueExW(h, names[i], NULL, &type, (BYTE *)buf, &size);
        snprintf(k, sizeof k, "%s.%s.present", prefix, keys[i]);
        o_bool(k, r == 0);
        if (r == 0) {
            buf[size / sizeof(wchar_t)] = 0;
            snprintf(k, sizeof k, "%s.%s.type", prefix, keys[i]);
            o_u32(k, type);
            snprintf(k, sizeof k, "%s.%s.value", prefix, keys[i]);
            o_path(k, buf);
        }
    }
    RegCloseKey(h);
}

static void kf_get(const char *name, DWORD flags, const wchar_t *check)
{
    PWSTR p = NULL;
    HRESULT hr = pSHGetKnownFolderPath(&FOLDERID_Videos, flags, NULL, &p);
    oracle_case(name);
    o_hex32("hr", (DWORD)hr);
    o_path("path", SUCCEEDED(hr) ? p : NULL);
    if (check) o_bool("dir_exists_after", dir_exists(check));
    oracle_case_end();
    if (p) pCoTaskMemFree(p);
}

static void fp_get(const char *name, int csidl, DWORD type, const wchar_t *check)
{
    wchar_t buf[MAX_PATH];
    buf[0] = 0;
    HRESULT hr = pSHGetFolderPathW(NULL, csidl, NULL, type, buf);
    oracle_case(name);
    o_hex32("hr", (DWORD)hr);
    o_path("path", SUCCEEDED(hr) ? buf : NULL);
    if (check) o_bool("dir_exists_after", dir_exists(check));
    oracle_case_end();
}

static void kf_set(const char *name, DWORD flags, const wchar_t *path, const wchar_t *check)
{
    HRESULT hr = pSHSetKnownFolderPath(&FOLDERID_Videos, flags, NULL, path);
    oracle_case(name);
    o_hex32("hr", (DWORD)hr);
    if (check) o_bool("dir_exists_after", dir_exists(check));
    o_usf("usf");
    oracle_case_end();
}

int main(void)
{
    oracle_begin("shell32/known_folder_redirect");
    HMODULE sh = LoadLibraryW(L"shell32.dll");
    HMODULE ole = LoadLibraryW(L"ole32.dll");
    LOAD(sh, SHGetKnownFolderPath);
    LOAD(sh, SHSetKnownFolderPath);
    LOAD(sh, SHGetFolderPathW);
    LOAD(sh, SHSetFolderPathW);
    LOAD(sh, SHGetFolderPathAndSubDirW);
    LOAD(ole, CoTaskMemFree);
    oracle_case("setup");
    o_bool("loaded", pSHGetKnownFolderPath && pSHGetFolderPathW && pSHGetFolderPathAndSubDirW && pCoTaskMemFree);
    o_bool("has_SHSetKnownFolderPath", pSHSetKnownFolderPath != NULL);
    o_bool("has_SHSetFolderPathW", pSHSetFolderPathW != NULL);
    oracle_case_end();
    if (!pSHSetKnownFolderPath || !pSHGetKnownFolderPath || !pSHGetFolderPathW || !pSHGetFolderPathAndSubDirW)
        return oracle_end();

    GetCurrentDirectoryW(MAX_PATH, cwd);
    GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH);
    save_key(&saved[0]);
    save_key(&saved[1]);

    wchar_t r1[MAX_PATH], r2[MAX_PATH], r2sub[MAX_PATH], r3[MAX_PATH];
    swprintf(r1, MAX_PATH, L"%ls\\redir\\vid", cwd);
    swprintf(r2, MAX_PATH, L"%ls\\redir2", cwd);
    swprintf(r2sub, MAX_PATH, L"%ls\\redir2\\a\\b", cwd);
    swprintf(r3, MAX_PATH, L"%ls\\redir3", cwd);

    oracle_case("initial");
    o_usf("usf");
    oracle_case_end();

    kf_set("set_missing_dir", 0, r1, r1);
    kf_get("get_missing_no_flags", 0, r1);
    kf_get("get_missing_dont_verify", KF_FLAG_DONT_VERIFY, r1);
    kf_get("get_missing_default_path", KF_FLAG_DEFAULT_PATH, r1);
    fp_get("folderpath_missing", CSIDL_MYVIDEO, SHGFP_TYPE_CURRENT, r1);
    fp_get("folderpath_missing_dont_verify", CSIDL_MYVIDEO | CSIDL_FLAG_DONT_VERIFY, SHGFP_TYPE_CURRENT, r1);
    fp_get("folderpath_missing_type_default", CSIDL_MYVIDEO, SHGFP_TYPE_DEFAULT, r1);
    kf_get("get_missing_create", KF_FLAG_CREATE, r1);
    kf_get("get_after_create", 0, r1);

    /* SHGetFolderPathW with CSIDL_FLAG_CREATE on a fresh redirect. */
    kf_set("set_redir2", 0, r2, r2);
    fp_get("folderpath_create", CSIDL_MYVIDEO | CSIDL_FLAG_CREATE, SHGFP_TYPE_CURRENT, r2);
    {
        wchar_t buf[MAX_PATH];
        buf[0] = 0;
        HRESULT hr = pSHGetFolderPathAndSubDirW(NULL, CSIDL_MYVIDEO, NULL, SHGFP_TYPE_CURRENT, L"a\\b", buf);
        oracle_case("subdir_no_create");
        o_hex32("hr", (DWORD)hr);
        o_path("path", SUCCEEDED(hr) ? buf : NULL);
        o_bool("dir_exists_after", dir_exists(r2sub));
        oracle_case_end();
        buf[0] = 0;
        hr = pSHGetFolderPathAndSubDirW(NULL, CSIDL_MYVIDEO | CSIDL_FLAG_CREATE, NULL, SHGFP_TYPE_CURRENT, L"a\\b", buf);
        oracle_case("subdir_create");
        o_hex32("hr", (DWORD)hr);
        o_path("path", SUCCEEDED(hr) ? buf : NULL);
        o_bool("dir_exists_after", dir_exists(r2sub));
        oracle_case_end();
        buf[0] = 0;
        hr = pSHGetFolderPathAndSubDirW(NULL, CSIDL_MYVIDEO, NULL, SHGFP_TYPE_CURRENT, L"a\\b", buf);
        oracle_case("subdir_existing");
        o_hex32("hr", (DWORD)hr);
        o_path("path", SUCCEEDED(hr) ? buf : NULL);
        oracle_case_end();
        buf[0] = 0;
        hr = pSHGetFolderPathAndSubDirW(NULL, CSIDL_MYVIDEO, NULL, SHGFP_TYPE_CURRENT, NULL, buf);
        oracle_case("subdir_null");
        o_hex32("hr", (DWORD)hr);
        o_path("path", SUCCEEDED(hr) ? buf : NULL);
        oracle_case_end();
    }

    /* KF_FLAG_CREATE on set: the folder is created while redirecting. */
    kf_set("set_with_create", KF_FLAG_CREATE, r3, r3);
    kf_get("get_redir3", 0, NULL);

    /* KF_FLAG_DONT_UNEXPAND keeps %USERPROFILE% in the registry value. */
    {
        wchar_t p[MAX_PATH];
        swprintf(p, MAX_PATH, L"%ls\\Videos", profile);
        kf_set("set_profile_videos", 0, p, NULL);
        kf_set("set_profile_videos_dont_unexpand", KF_FLAG_DONT_UNEXPAND, p, NULL);
        kf_set("set_env_string", 0, L"%USERPROFILE%\\Videos", NULL);
        kf_get("get_after_env_string", 0, NULL);
    }

    /* Errors. */
    kf_set("set_relative_path", 0, L"relative\\dir", NULL);
    kf_set("set_empty_path", 0, L"", NULL);
    {
        HRESULT hr = pSHSetKnownFolderPath(&FOLDERID_Windows, 0, NULL, r1);
        oracle_case("set_fixed_folder");
        o_hex32("hr", (DWORD)hr);
        oracle_case_end();
        hr = pSHSetKnownFolderPath(&FOLDERID_ComputerFolder, 0, NULL, r1);
        oracle_case("set_virtual_folder");
        o_hex32("hr", (DWORD)hr);
        oracle_case_end();
    }

    /* SHSetFolderPathW: the CSIDL version. */
    if (pSHSetFolderPathW) {
        HRESULT hr = pSHSetFolderPathW(CSIDL_MYVIDEO, NULL, 0, r2);
        oracle_case("setfolderpath_csidl");
        o_hex32("hr", (DWORD)hr);
        o_usf("usf");
        oracle_case_end();
        fp_get("folderpath_after_setfolderpath", CSIDL_MYVIDEO, SHGFP_TYPE_CURRENT, NULL);
        hr = pSHSetFolderPathW(CSIDL_WINDOWS, NULL, 0, r2);
        oracle_case("setfolderpath_fixed");
        o_hex32("hr", (DWORD)hr);
        oracle_case_end();
    }

    restore_key(&saved[0]);
    restore_key(&saved[1]);
    oracle_case("restored");
    o_usf("usf");
    oracle_case_end();
    kf_get("get_restored", 0, NULL);
    return oracle_end();
}
