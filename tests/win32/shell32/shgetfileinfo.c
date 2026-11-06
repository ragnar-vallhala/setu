/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: SHGetFileInfoW. Display names (SHGFI_DISPLAYNAME), type
 * names (SHGFI_TYPENAME), attributes (SHGFI_ATTRIBUTES, and
 * SHGFI_ATTR_SPECIFIED with a mask), the executable type
 * (SHGFI_EXETYPE), SHGFI_USEFILEATTRIBUTES for names that don't exist,
 * SHGFI_PIDL, and the icon flags. Icons are printed only as booleans
 * (handle returned, index >= 0, image list returned), never as pixels or
 * index values, which depend on the system image list's history.
 *
 * Inputs: a file, a directory, a file with an extension registered by the
 * test under HKCU\Software\Classes, an unknown extension, a file without
 * an extension, the test program itself (an .exe), hidden / read-only
 * files, the drive root, a missing file, and virtual folders through
 * PIDLs (Desktop, This PC, Recycle Bin).
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-shgetfileinfow
 *       https://learn.microsoft.com/en-us/windows/win32/api/shellapi/ns-shellapi-shfileinfow
 *       https://learn.microsoft.com/en-us/windows/win32/shell/sfgao
 */
#include "oracle.h"
#include <shellapi.h>
#include <shlobj.h>

static DWORD_PTR(WINAPI *pSHGetFileInfoW)(LPCWSTR, DWORD, SHFILEINFOW *, UINT, UINT);
static HRESULT(WINAPI *pSHGetSpecialFolderLocation)(HWND, int, PIDLIST_ABSOLUTE *);
static void(WINAPI *pSHChangeNotify)(LONG, UINT, LPCVOID, LPCVOID);
static void(WINAPI *pCoTaskMemFree)(void *);
static HRESULT(WINAPI *pCoInitializeEx)(void *, DWORD);

#define LOAD(m, f) (*(FARPROC *)&p##f = GetProcAddress(m, #f))
#define CLS L"Software\\Classes\\"

static void set_str(const wchar_t *key, const wchar_t *name, const wchar_t *val)
{
    HKEY h;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, key, 0, NULL, 0, KEY_WRITE, NULL, &h, NULL)) return;
    RegSetValueExW(h, name, 0, REG_SZ, (const BYTE *)val, (DWORD)(wcslen(val) + 1) * sizeof(wchar_t));
    RegCloseKey(h);
}

static void cleanup(void)
{
    RegDeleteTreeW(HKEY_CURRENT_USER, CLS L".setuoracle5");
    RegDeleteKeyW(HKEY_CURRENT_USER, CLS L".setuoracle5");
    RegDeleteTreeW(HKEY_CURRENT_USER, CLS L"SetuOracle.Doc5");
    RegDeleteKeyW(HKEY_CURRENT_USER, CLS L"SetuOracle.Doc5");
}

static void make_file(const wchar_t *name, DWORD attrs)
{
    HANDLE h = CreateFileW(name, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    DWORD n;
    WriteFile(h, "data", 4, &n, NULL);
    CloseHandle(h);
    if (attrs) SetFileAttributesW(name, attrs);
}

/* SFGAO bits that describe the item rather than the machine. */
#define ATTR_MASK (SFGAO_CANCOPY | SFGAO_CANMOVE | SFGAO_CANLINK | SFGAO_CANRENAME | SFGAO_CANDELETE | \
                   SFGAO_HASPROPSHEET | SFGAO_DROPTARGET | SFGAO_LINK | SFGAO_SHARE | SFGAO_READONLY | \
                   SFGAO_HIDDEN | SFGAO_FILESYSANCESTOR | SFGAO_FOLDER | SFGAO_FILESYSTEM | SFGAO_HASSUBFOLDER | \
                   SFGAO_STREAM | SFGAO_STORAGEANCESTOR | SFGAO_STORAGE | SFGAO_BROWSABLE)

static void info(const char *name, const wchar_t *path, DWORD fattrs, UINT flags)
{
    SHFILEINFOW fi;
    memset(&fi, 0, sizeof fi);
    wcscpy(fi.szDisplayName, L"#");
    wcscpy(fi.szTypeName, L"#");
    fi.iIcon = -77;
    if (flags & SHGFI_ATTR_SPECIFIED) fi.dwAttributes = SFGAO_FOLDER | SFGAO_FILESYSTEM | SFGAO_LINK;
    DWORD_PTR r;
    ORACLE_CALL(r, pSHGetFileInfoW(path, fattrs, &fi, sizeof fi, flags));
    oracle_case(name);
    if (flags & (SHGFI_SYSICONINDEX)) o_bool("ret_nonzero", r != 0);
    else if (flags & SHGFI_EXETYPE) o_hex32("ret", (DWORD)r);
    else o_u64("ret", r);
    if (flags & SHGFI_DISPLAYNAME) o_wstr("display", fi.szDisplayName);
    if (flags & SHGFI_TYPENAME) o_wstr("type", fi.szTypeName);
    if (flags & SHGFI_ATTRIBUTES) o_hex32("attrs", fi.dwAttributes & ATTR_MASK);
    if (flags & (SHGFI_ICON | SHGFI_SYSICONINDEX)) o_bool("icon_index_valid", fi.iIcon >= 0);
    if (flags & SHGFI_ICON) {
        o_bool("hicon", fi.hIcon != NULL);
        if (fi.hIcon) DestroyIcon(fi.hIcon);
    }
    oracle_case_end();
}

static void info_pidl(const char *name, int csidl, UINT flags)
{
    PIDLIST_ABSOLUTE pidl = NULL;
    pSHGetSpecialFolderLocation(NULL, csidl, &pidl);
    info(name, (LPCWSTR)pidl, 0, flags | SHGFI_PIDL);
    if (pidl) pCoTaskMemFree(pidl);
}

int main(void)
{
    oracle_begin("shell32/shgetfileinfo");
    HMODULE sh = LoadLibraryW(L"shell32.dll");
    HMODULE ole = LoadLibraryW(L"ole32.dll");
    LOAD(sh, SHGetFileInfoW);
    LOAD(sh, SHGetSpecialFolderLocation);
    LOAD(sh, SHChangeNotify);
    LOAD(ole, CoTaskMemFree);
    LOAD(ole, CoInitializeEx);
    oracle_case("setup");
    o_bool("loaded", pSHGetFileInfoW && pSHGetSpecialFolderLocation && pSHChangeNotify && pCoTaskMemFree && pCoInitializeEx);
    oracle_case_end();
    if (!pSHGetFileInfoW || !pSHGetSpecialFolderLocation || !pSHChangeNotify || !pCoTaskMemFree || !pCoInitializeEx)
        return oracle_end();
    pCoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    cleanup();
    set_str(CLS L".setuoracle5", NULL, L"SetuOracle.Doc5");
    set_str(CLS L"SetuOracle.Doc5", NULL, L"Setu Oracle Five");
    pSHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);

    wchar_t self[MAX_PATH];
    GetModuleFileNameW(NULL, self, MAX_PATH);
    CopyFileW(self, L"prog.exe", FALSE);
    make_file(L"plain.txt", 0);
    make_file(L"doc.setuoracle5", 0);
    make_file(L"thing.unknownext", 0);
    make_file(L"noext", 0);
    make_file(L"hidden.txt", FILE_ATTRIBUTE_HIDDEN);
    make_file(L"readonly.txt", FILE_ATTRIBUTE_READONLY);
    make_file(L"two.dots.tar.gz", 0);
    CreateDirectoryW(L"folder", NULL);
    CreateDirectoryW(L"folder\\child", NULL);
    CreateDirectoryW(L"emptyfolder", NULL);

    static const struct { const char *name; const wchar_t *path; } files[] = {
        {"plain_txt", L"plain.txt"},
        {"registered_ext", L"doc.setuoracle5"},
        {"unknown_ext", L"thing.unknownext"},
        {"no_ext", L"noext"},
        {"exe", L"prog.exe"},
        {"hidden", L"hidden.txt"},
        {"readonly", L"readonly.txt"},
        {"multi_dot", L"two.dots.tar.gz"},
        {"folder", L"folder"},
        {"empty_folder", L"emptyfolder"},
    };
    char nm[96];
    for (size_t i = 0; i < sizeof files / sizeof files[0]; i++) {
        snprintf(nm, sizeof nm, "%s_names", files[i].name);
        info(nm, files[i].path, 0, SHGFI_DISPLAYNAME | SHGFI_TYPENAME);
        snprintf(nm, sizeof nm, "%s_attrs", files[i].name);
        info(nm, files[i].path, 0, SHGFI_ATTRIBUTES);
        snprintf(nm, sizeof nm, "%s_icon", files[i].name);
        info(nm, files[i].path, 0, SHGFI_ICON | SHGFI_SMALLICON);
    }
    /* The drive root's display name contains the volume label: no names here. */
    info("drive_root_type", L"C:\\", 0, SHGFI_TYPENAME);
    info("drive_root_attrs", L"C:\\", 0, SHGFI_ATTRIBUTES);
    info("drive_root_icon", L"C:\\", 0, SHGFI_ICON | SHGFI_SMALLICON);

    /* Missing files. */
    info("missing_names", L"no_such_file.txt", 0, SHGFI_DISPLAYNAME | SHGFI_TYPENAME);
    info("missing_attrs", L"no_such_file.txt", 0, SHGFI_ATTRIBUTES);

    /* SHGFI_USEFILEATTRIBUTES: the file needn't exist. */
    static const struct { const char *name; const wchar_t *path; DWORD fa; } ufa[] = {
        {"ufa_txt", L"nothing.txt", FILE_ATTRIBUTE_NORMAL},
        {"ufa_registered", L"nothing.setuoracle5", FILE_ATTRIBUTE_NORMAL},
        {"ufa_unknown", L"nothing.qqqzz", FILE_ATTRIBUTE_NORMAL},
        {"ufa_no_ext", L"nothing", FILE_ATTRIBUTE_NORMAL},
        {"ufa_exe", L"nothing.exe", FILE_ATTRIBUTE_NORMAL},
        {"ufa_dir", L"nothing_dir", FILE_ATTRIBUTE_DIRECTORY},
        {"ufa_ext_only", L".setuoracle5", FILE_ATTRIBUTE_NORMAL},
        {"ufa_star_ext", L"*.setuoracle5", FILE_ATTRIBUTE_NORMAL},
        {"ufa_full_path", L"C:\\setu-no-such-dir\\x.txt", FILE_ATTRIBUTE_NORMAL},
        {"ufa_existing_dir_as_file", L"folder", FILE_ATTRIBUTE_NORMAL},
    };
    for (size_t i = 0; i < sizeof ufa / sizeof ufa[0]; i++) {
        snprintf(nm, sizeof nm, "%s_names", ufa[i].name);
        info(nm, ufa[i].path, ufa[i].fa, SHGFI_USEFILEATTRIBUTES | SHGFI_DISPLAYNAME | SHGFI_TYPENAME);
        snprintf(nm, sizeof nm, "%s_attrs", ufa[i].name);
        info(nm, ufa[i].path, ufa[i].fa, SHGFI_USEFILEATTRIBUTES | SHGFI_ATTRIBUTES);
        snprintf(nm, sizeof nm, "%s_sysicon", ufa[i].name);
        info(nm, ufa[i].path, ufa[i].fa, SHGFI_USEFILEATTRIBUTES | SHGFI_SYSICONINDEX | SHGFI_SMALLICON);
    }

    /* SHGFI_ATTR_SPECIFIED, SHGFI_EXETYPE. */
    info("attr_specified_folder", L"folder", 0, SHGFI_ATTRIBUTES | SHGFI_ATTR_SPECIFIED);
    info("attr_specified_file", L"plain.txt", 0, SHGFI_ATTRIBUTES | SHGFI_ATTR_SPECIFIED);
    info("exetype_exe", L"prog.exe", 0, SHGFI_EXETYPE);
    info("exetype_txt", L"plain.txt", 0, SHGFI_EXETYPE);
    info("exetype_missing", L"no_such.exe", 0, SHGFI_EXETYPE);
    info("sysicon_file", L"plain.txt", 0, SHGFI_SYSICONINDEX | SHGFI_SMALLICON);
    info("no_flags", L"plain.txt", 0, 0);

    /* Virtual folders by PIDL. */
    info_pidl("pidl_desktop_names", CSIDL_DESKTOP, SHGFI_DISPLAYNAME | SHGFI_TYPENAME);
    info_pidl("pidl_desktop_attrs", CSIDL_DESKTOP, SHGFI_ATTRIBUTES);
    info_pidl("pidl_drives_names", CSIDL_DRIVES, SHGFI_DISPLAYNAME | SHGFI_TYPENAME);
    info_pidl("pidl_drives_attrs", CSIDL_DRIVES, SHGFI_ATTRIBUTES);
    info_pidl("pidl_bitbucket_names", CSIDL_BITBUCKET, SHGFI_DISPLAYNAME | SHGFI_TYPENAME);
    info_pidl("pidl_bitbucket_attrs", CSIDL_BITBUCKET, SHGFI_ATTRIBUTES);
    info_pidl("pidl_personal_names", CSIDL_PERSONAL, SHGFI_DISPLAYNAME | SHGFI_TYPENAME);
    info_pidl("pidl_controls_names", CSIDL_CONTROLS, SHGFI_DISPLAYNAME);
    info_pidl("pidl_desktop_icon", CSIDL_DESKTOP, SHGFI_SYSICONINDEX | SHGFI_SMALLICON);

    cleanup();
    pSHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
    return oracle_end();
}
