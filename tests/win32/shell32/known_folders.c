/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: SHGetKnownFolderPath for the per-user, common and fixed
 * folders a desktop and installers depend on (Desktop, Documents, Start
 * Menu, Programs, Startup, AppData, Program Files, Public ...), with the
 * current user, the Default user (hToken = -1) and KF_FLAG_DEFAULT_PATH,
 * plus IKnownFolderManager::GetFolder / IKnownFolder::GetCategory /
 * GetFolderDefinition (canonical names) and FindFolderFromPath.
 *
 * Machine-specific path parts are tokenised: the current user's profile
 * directory prints as "<profile>", the directory holding the profiles as
 * "<profiles>" and the Windows directory as "<windir>". The rest of each
 * path is printed as is (case included).
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/nf-shlobj_core-shgetknownfolderpath
 *       https://learn.microsoft.com/en-us/windows/win32/shell/knownfolderid
 *       https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/ne-shlobj_core-known_folder_flag
 *       https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nn-shobjidl_core-iknownfoldermanager
 *       https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nn-shobjidl_core-iknownfolder
 *       https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/ns-shobjidl_core-knownfolder_definition
 *       https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/ne-shobjidl_core-kf_category
 */
#define INITGUID
#include "oracle.h"
#include <shlobj.h>
#include <knownfolders.h>
#include <shobjidl.h>

static HRESULT(WINAPI *pSHGetKnownFolderPath)(const GUID *, DWORD, HANDLE, PWSTR *);
static void(WINAPI *pCoTaskMemFree)(void *);
static HRESULT(WINAPI *pCoInitializeEx)(void *, DWORD);
static HRESULT(WINAPI *pCoCreateInstance)(REFCLSID, IUnknown *, DWORD, REFIID, void **);

#define LOAD(m, f) (*(FARPROC *)&p##f = GetProcAddress(m, #f))

static wchar_t profile[MAX_PATH], profiles[MAX_PATH], windir[MAX_PATH];

static int has_prefix_ci(const wchar_t *s, const wchar_t *p)
{
    size_t n = wcslen(p);
    return n && !_wcsnicmp(s, p, n) && (s[n] == 0 || s[n] == L'\\');
}

/* Tokenise the machine-specific prefix of a path. */
static void o_path(const char *k, const wchar_t *s)
{
    wchar_t out[2 * MAX_PATH];
    if (!s) { o_str(k, NULL); return; }
    if (has_prefix_ci(s, profile)) swprintf(out, 2 * MAX_PATH, L"<profile>%ls", s + wcslen(profile));
    else if (has_prefix_ci(s, profiles)) swprintf(out, 2 * MAX_PATH, L"<profiles>%ls", s + wcslen(profiles));
    else if (has_prefix_ci(s, windir)) swprintf(out, 2 * MAX_PATH, L"<windir>%ls", s + wcslen(windir));
    else swprintf(out, 2 * MAX_PATH, L"%ls", s);
    o_wstr(k, out);
}

static void kf_case(const char *name, const GUID *id, DWORD flags, HANDLE token)
{
    PWSTR p = NULL;
    HRESULT hr = pSHGetKnownFolderPath(id, flags, token, &p);
    oracle_case(name);
    o_hex32("hr", (DWORD)hr);
    if (SUCCEEDED(hr)) o_path("path", p);
    else o_bool("out_null", p == NULL);
    oracle_case_end();
    if (p) pCoTaskMemFree(p);
}

struct kf { const char *name; const GUID *id; };
static const GUID zero_guid;

int main(void)
{
    oracle_begin("shell32/known_folders");
    HMODULE sh = LoadLibraryW(L"shell32.dll");
    HMODULE ole = LoadLibraryW(L"ole32.dll");
    LOAD(sh, SHGetKnownFolderPath);
    LOAD(ole, CoTaskMemFree);
    LOAD(ole, CoInitializeEx);
    LOAD(ole, CoCreateInstance);
    oracle_case("setup");
    o_bool("loaded", pSHGetKnownFolderPath && pCoTaskMemFree && pCoInitializeEx && pCoCreateInstance);
    oracle_case_end();
    if (!pSHGetKnownFolderPath || !pCoCreateInstance) return oracle_end();

    GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH);
    wcscpy(profiles, profile);
    wchar_t *slash = wcsrchr(profiles, L'\\');
    if (slash) *slash = 0;
    GetWindowsDirectoryW(windir, MAX_PATH);

    static const struct kf folders[] = {
        {"desktop", &FOLDERID_Desktop},
        {"documents", &FOLDERID_Documents},
        {"downloads", &FOLDERID_Downloads},
        {"music", &FOLDERID_Music},
        {"pictures", &FOLDERID_Pictures},
        {"videos", &FOLDERID_Videos},
        {"favorites", &FOLDERID_Favorites},
        {"links", &FOLDERID_Links},
        {"saved_games", &FOLDERID_SavedGames},
        {"contacts", &FOLDERID_Contacts},
        {"searches", &FOLDERID_SavedSearches},
        {"objects3d", &FOLDERID_Objects3D},
        {"profile", &FOLDERID_Profile},
        {"user_profiles", &FOLDERID_UserProfiles},
        {"roaming_appdata", &FOLDERID_RoamingAppData},
        {"local_appdata", &FOLDERID_LocalAppData},
        {"local_appdata_low", &FOLDERID_LocalAppDataLow},
        {"user_program_files", &FOLDERID_UserProgramFiles},
        {"user_program_files_common", &FOLDERID_UserProgramFilesCommon},
        {"start_menu", &FOLDERID_StartMenu},
        {"programs", &FOLDERID_Programs},
        {"startup", &FOLDERID_Startup},
        {"admin_tools", &FOLDERID_AdminTools},
        {"send_to", &FOLDERID_SendTo},
        {"recent", &FOLDERID_Recent},
        {"templates", &FOLDERID_Templates},
        {"quick_launch", &FOLDERID_QuickLaunch},
        {"history", &FOLDERID_History},
        {"internet_cache", &FOLDERID_InternetCache},
        {"cookies", &FOLDERID_Cookies},
        {"net_hood", &FOLDERID_NetHood},
        {"print_hood", &FOLDERID_PrintHood},
        {"cd_burning", &FOLDERID_CDBurning},
        {"common_start_menu", &FOLDERID_CommonStartMenu},
        {"common_programs", &FOLDERID_CommonPrograms},
        {"common_startup", &FOLDERID_CommonStartup},
        {"common_admin_tools", &FOLDERID_CommonAdminTools},
        {"common_templates", &FOLDERID_CommonTemplates},
        {"program_data", &FOLDERID_ProgramData},
        {"public", &FOLDERID_Public},
        {"public_desktop", &FOLDERID_PublicDesktop},
        {"public_documents", &FOLDERID_PublicDocuments},
        {"public_downloads", &FOLDERID_PublicDownloads},
        {"public_music", &FOLDERID_PublicMusic},
        {"public_pictures", &FOLDERID_PublicPictures},
        {"public_videos", &FOLDERID_PublicVideos},
        {"program_files", &FOLDERID_ProgramFiles},
        {"program_files_x64", &FOLDERID_ProgramFilesX64},
        {"program_files_x86", &FOLDERID_ProgramFilesX86},
        {"program_files_common", &FOLDERID_ProgramFilesCommon},
        {"program_files_common_x64", &FOLDERID_ProgramFilesCommonX64},
        {"program_files_common_x86", &FOLDERID_ProgramFilesCommonX86},
        {"windows", &FOLDERID_Windows},
        {"system", &FOLDERID_System},
        {"system_x86", &FOLDERID_SystemX86},
        {"fonts", &FOLDERID_Fonts},
        {"resource_dir", &FOLDERID_ResourceDir},
    };
    char nm[96];
    for (size_t i = 0; i < sizeof folders / sizeof folders[0]; i++) {
        snprintf(nm, sizeof nm, "path_%s", folders[i].name);
        kf_case(nm, folders[i].id, 0, NULL);
    }
    /* KF_FLAG_DEFAULT_PATH: the path the folder would have without redirection. */
    kf_case("default_path_documents", &FOLDERID_Documents, KF_FLAG_DEFAULT_PATH, NULL);
    kf_case("default_path_roaming_appdata", &FOLDERID_RoamingAppData, KF_FLAG_DEFAULT_PATH, NULL);
    kf_case("default_path_programs", &FOLDERID_Programs, KF_FLAG_DEFAULT_PATH, NULL);
    kf_case("default_path_common_programs", &FOLDERID_CommonPrograms, KF_FLAG_DEFAULT_PATH, NULL);
    kf_case("default_path_program_files", &FOLDERID_ProgramFiles, KF_FLAG_DEFAULT_PATH, NULL);
    /* hToken = -1: the Default user. */
    HANDLE def = (HANDLE)(LONG_PTR)-1;
    kf_case("default_user_documents", &FOLDERID_Documents, 0, def);
    kf_case("default_user_desktop", &FOLDERID_Desktop, 0, def);
    kf_case("default_user_roaming_appdata", &FOLDERID_RoamingAppData, 0, def);
    kf_case("default_user_local_appdata", &FOLDERID_LocalAppData, 0, def);
    kf_case("default_user_programs", &FOLDERID_Programs, 0, def);
    kf_case("default_user_profile", &FOLDERID_Profile, 0, def);
    kf_case("default_user_program_files", &FOLDERID_ProgramFiles, 0, def);
    kf_case("default_user_common_programs", &FOLDERID_CommonPrograms, 0, def);
    /* Flags that don't change the result for an existing folder. */
    kf_case("flag_dont_verify_documents", &FOLDERID_Documents, KF_FLAG_DONT_VERIFY, NULL);
    kf_case("flag_create_documents", &FOLDERID_Documents, KF_FLAG_CREATE, NULL);
    kf_case("flag_no_alias_documents", &FOLDERID_Documents, KF_FLAG_NO_ALIAS, NULL);
    kf_case("flag_not_parent_relative_programs", &FOLDERID_Programs, KF_FLAG_NOT_PARENT_RELATIVE, NULL);
    kf_case("flag_dont_unexpand_appdata", &FOLDERID_RoamingAppData, KF_FLAG_DONT_UNEXPAND, NULL);
    kf_case("flag_alias_only_documents", &FOLDERID_Documents, KF_FLAG_ALIAS_ONLY, NULL);
    /* Virtual folders have no path. */
    kf_case("virtual_computer", &FOLDERID_ComputerFolder, 0, NULL);
    kf_case("virtual_recycle_bin", &FOLDERID_RecycleBinFolder, 0, NULL);
    kf_case("virtual_control_panel", &FOLDERID_ControlPanelFolder, 0, NULL);
    kf_case("virtual_apps_folder", &FOLDERID_AppsFolder, 0, NULL);
    kf_case("virtual_libraries_documents", &FOLDERID_DocumentsLibrary, 0, NULL);

    /* ---- IKnownFolderManager ---- */
    HRESULT hr = pCoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    IKnownFolderManager *mgr = NULL;
    hr = pCoCreateInstance(&CLSID_KnownFolderManager, NULL, CLSCTX_INPROC_SERVER,
                           &IID_IKnownFolderManager, (void **)&mgr);
    oracle_case("manager_create");
    o_hex32("hr", (DWORD)hr);
    oracle_case_end();
    if (FAILED(hr) || !mgr) return oracle_end();

    static const struct kf defs[] = {
        {"documents", &FOLDERID_Documents},
        {"desktop", &FOLDERID_Desktop},
        {"programs", &FOLDERID_Programs},
        {"common_programs", &FOLDERID_CommonPrograms},
        {"start_menu", &FOLDERID_StartMenu},
        {"startup", &FOLDERID_Startup},
        {"roaming_appdata", &FOLDERID_RoamingAppData},
        {"local_appdata", &FOLDERID_LocalAppData},
        {"program_files", &FOLDERID_ProgramFiles},
        {"windows", &FOLDERID_Windows},
        {"computer", &FOLDERID_ComputerFolder},
        {"recycle_bin", &FOLDERID_RecycleBinFolder},
        {"public_documents", &FOLDERID_PublicDocuments},
        {"downloads", &FOLDERID_Downloads},
    };
    for (size_t i = 0; i < sizeof defs / sizeof defs[0]; i++) {
        IKnownFolder *kf = NULL;
        hr = mgr->lpVtbl->GetFolder(mgr, defs[i].id, &kf);
        snprintf(nm, sizeof nm, "def_%s", defs[i].name);
        oracle_case(nm);
        o_hex32("get_hr", (DWORD)hr);
        if (SUCCEEDED(hr) && kf) {
            KF_CATEGORY cat = 0;
            HRESULT h2 = kf->lpVtbl->GetCategory(kf, &cat);
            o_hex32("category_hr", (DWORD)h2);
            o_u32("category", (DWORD)cat);
            KNOWNFOLDER_DEFINITION d;
            memset(&d, 0, sizeof d);
            h2 = kf->lpVtbl->GetFolderDefinition(kf, &d);
            o_hex32("definition_hr", (DWORD)h2);
            if (SUCCEEDED(h2)) {
                o_u32("def_category", (DWORD)d.category);
                o_wstr("def_name", d.pszName);
                o_wstr("def_relative_path", d.pszRelativePath);
                o_bool("def_has_parent", !IsEqualGUID(&d.fidParent, &zero_guid));
                o_hex32("def_flags", d.kfdFlags);
                pCoTaskMemFree(d.pszName); pCoTaskMemFree(d.pszDescription);
                pCoTaskMemFree(d.pszRelativePath); pCoTaskMemFree(d.pszParsingName);
                pCoTaskMemFree(d.pszTooltip); pCoTaskMemFree(d.pszLocalizedName);
                pCoTaskMemFree(d.pszIcon); pCoTaskMemFree(d.pszSecurity);
            }
            PWSTR p = NULL;
            h2 = kf->lpVtbl->GetPath(kf, 0, &p);
            o_hex32("path_hr", (DWORD)h2);
            if (SUCCEEDED(h2)) o_path("path", p);
            if (p) pCoTaskMemFree(p);
            kf->lpVtbl->Release(kf);
        }
        oracle_case_end();
    }
    {
        static const GUID bogus = {0x5e7a0003, 0x1111, 0x2222, {0x33, 0x33, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44}};
        IKnownFolder *kf = NULL;
        hr = mgr->lpVtbl->GetFolder(mgr, &bogus, &kf);
        oracle_case("get_bogus");
        o_hex32("hr", (DWORD)hr);
        o_bool("out_null", kf == NULL);
        oracle_case_end();
        if (kf) kf->lpVtbl->Release(kf);
    }
    {
        static const struct { const char *name; const wchar_t *canon; } byname[] = {
            {"byname_personal", L"Personal"},
            {"byname_desktop", L"Desktop"},
            {"byname_programs", L"Programs"},
            {"byname_appdata", L"AppData"},
            {"byname_common_programs", L"Common Programs"},
            {"byname_bogus", L"No Such Folder Name"},
        };
        for (size_t i = 0; i < sizeof byname / sizeof byname[0]; i++) {
            IKnownFolder *kf = NULL;
            hr = mgr->lpVtbl->GetFolderByName(mgr, byname[i].canon, &kf);
            oracle_case(byname[i].name);
            o_hex32("hr", (DWORD)hr);
            if (SUCCEEDED(hr) && kf) {
                KNOWNFOLDERID id;
                kf->lpVtbl->GetId(kf, &id);
                o_bool("is_documents", IsEqualGUID(&id, &FOLDERID_Documents));
                o_bool("is_desktop", IsEqualGUID(&id, &FOLDERID_Desktop));
                o_bool("is_programs", IsEqualGUID(&id, &FOLDERID_Programs));
                o_bool("is_roaming_appdata", IsEqualGUID(&id, &FOLDERID_RoamingAppData));
                o_bool("is_common_programs", IsEqualGUID(&id, &FOLDERID_CommonPrograms));
                kf->lpVtbl->Release(kf);
            }
            oracle_case_end();
        }
    }
    {
        /* FindFolderFromPath: exact match and a path below a known folder. */
        PWSTR docs = NULL;
        pSHGetKnownFolderPath(&FOLDERID_Documents, 0, NULL, &docs);
        wchar_t sub[MAX_PATH + 16];
        swprintf(sub, MAX_PATH + 16, L"%ls\\sub\\dir", docs ? docs : L"");
        static const struct { const char *name; FFFP_MODE mode; int child; } ff[] = {
            {"find_exact_documents", FFFP_EXACTMATCH, 0},
            {"find_nearest_documents", FFFP_NEARESTPARENTMATCH, 0},
            {"find_exact_child", FFFP_EXACTMATCH, 1},
            {"find_nearest_child", FFFP_NEARESTPARENTMATCH, 1},
        };
        for (size_t i = 0; i < 4; i++) {
            IKnownFolder *kf = NULL;
            hr = mgr->lpVtbl->FindFolderFromPath(mgr, ff[i].child ? sub : docs, ff[i].mode, &kf);
            oracle_case(ff[i].name);
            o_hex32("hr", (DWORD)hr);
            if (SUCCEEDED(hr) && kf) {
                KNOWNFOLDERID id;
                kf->lpVtbl->GetId(kf, &id);
                o_bool("is_documents", IsEqualGUID(&id, &FOLDERID_Documents));
                kf->lpVtbl->Release(kf);
            }
            oracle_case_end();
        }
        if (docs) pCoTaskMemFree(docs);
    }
    mgr->lpVtbl->Release(mgr);
    return oracle_end();
}
