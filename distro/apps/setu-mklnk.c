/*
 * SPDX-License-Identifier: MIT OR Apache-2.0
 *
 * setu-mklnk: write a Windows shortcut (.lnk) with IShellLinkW, for the
 * default apps' Start menu entries (distro/apps/stage.py).
 *
 * Usage: setu-mklnk LNK TARGET [-a ARGS] [-d DIR] [-i ICONFILE[,INDEX]] [-c COMMENT]
 *
 * Docs: IShellLinkW and IPersistFile::Save,
 *   https://learn.microsoft.com/windows/win32/api/shobjidl_core/nn-shobjidl_core-ishelllinkw
 *   https://learn.microsoft.com/windows/win32/shell/links
 */
#define COBJMACROS
#include <windows.h>
#include <shlobj.h>
#include <stdio.h>
#include <wchar.h>

int wmain(int argc, WCHAR **argv)
{
    const WCHAR *lnk, *target, *args = NULL, *dir = NULL, *icon = NULL, *comment = NULL;
    WCHAR iconfile[MAX_PATH], *comma;
    IShellLinkW *sl;
    IPersistFile *pf;
    HRESULT hr;
    int i, index = 0;

    if (argc < 3)
    {
        fwprintf(stderr, L"usage: setu-mklnk LNK TARGET [-a ARGS] [-d DIR] [-i ICON[,INDEX]] [-c COMMENT]\n");
        return 2;
    }
    lnk = argv[1];
    target = argv[2];
    for (i = 3; i + 1 < argc; i += 2)
    {
        if (!wcscmp(argv[i], L"-a")) args = argv[i + 1];
        else if (!wcscmp(argv[i], L"-d")) dir = argv[i + 1];
        else if (!wcscmp(argv[i], L"-i")) icon = argv[i + 1];
        else if (!wcscmp(argv[i], L"-c")) comment = argv[i + 1];
        else
        {
            fwprintf(stderr, L"setu-mklnk: unknown option %ls\n", argv[i]);
            return 2;
        }
    }

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    hr = CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkW, (void **)&sl);
    if (FAILED(hr))
    {
        fwprintf(stderr, L"setu-mklnk: CoCreateInstance failed: %08lx\n", hr);
        return 1;
    }
    IShellLinkW_SetPath(sl, target);
    if (args) IShellLinkW_SetArguments(sl, args);
    if (dir) IShellLinkW_SetWorkingDirectory(sl, dir);
    if (comment) IShellLinkW_SetDescription(sl, comment);
    if (icon)
    {
        lstrcpynW(iconfile, icon, MAX_PATH);
        if ((comma = wcsrchr(iconfile, L',')))
        {
            *comma = 0;
            index = _wtoi(comma + 1);
        }
        IShellLinkW_SetIconLocation(sl, iconfile, index);
    }
    hr = IShellLinkW_QueryInterface(sl, &IID_IPersistFile, (void **)&pf);
    if (SUCCEEDED(hr))
    {
        hr = IPersistFile_Save(pf, lnk, TRUE);
        IPersistFile_Release(pf);
    }
    IShellLinkW_Release(sl);
    CoUninitialize();
    if (FAILED(hr))
    {
        fwprintf(stderr, L"setu-mklnk: saving %ls failed: %08lx\n", lnk, hr);
        return 1;
    }
    return 0;
}
