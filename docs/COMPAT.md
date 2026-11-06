# App compatibility

The roadmap's desktop milestone asks for "top-N apps installed and launched
in CI". This page explains how that is measured and gives the latest
results. The dashboard itself (a static page with thumbnails, timings and
logs) is generated in `build/compat/` and is not committed.

```sh
python3 tools/compat/run.py              # every app in tools/compat/apps.toml
python3 tools/compat/run.py -a putty     # one app (repeatable)
python3 tools/compat/run.py --small -j 2 # the small apps, two at a time
python3 tools/compat/run.py --uninstall  # also check the silent uninstall
python3 tools/compat/run.py --html-only  # rebuild index.html from results.json
python3 tools/compat/run.py --update-docs  # rewrite the results table below
```

Outputs: `build/compat/results.json`, `build/compat/index.html`
(self-contained), `build/compat/logs/<app>/*.log` and
`build/compat/thumbs/<app>.png`.

## The apps

Only third-party apps the owner approved are tested: WinSW, 7-Zip,
Notepad++, PuTTY, KeePass, WinSCP, LibreOffice, Firefox, GIMP and Inkscape.
Anything else needs the owner's approval first.

`tools/compat/apps.toml` lists, for each one, the current stable 64-bit
Windows release: version, official download URL, sha256 (checked against
the vendor's published checksum where there is one), licence, installer
kind, and the vendor's documented silent-install arguments with a link to
that documentation. It also gives the installed program to start, its
arguments (a small sample file `run.py` writes) and a pattern its main
window must match.

| App | Installer | Silent install (vendor docs) | Licence |
|---|---|---|---|
| WinSW 2.12.0 | single exe | none: copied, with its XML config ([README](https://github.com/winsw/winsw/blob/v2.12.0/README.md)) | MIT |
| 7-Zip 26.05 | MSI | `msiexec /i … /qn` ([FAQ](https://www.7-zip.org/faq.html)) | LGPL, BSD, unRAR restriction |
| Notepad++ 8.9.8.1 | NSIS | `/S` ([manual](https://npp-user-manual.org/docs/command-prompt/)) | GPL-3.0 |
| PuTTY 0.85 | MSI | `msiexec /i … /qn` ([msiexec](https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/msiexec)) | MIT |
| KeePass 2.62 (.NET) | Inno Setup | `/VERYSILENT` ([KeePass setup](https://keepass.info/help/v2/setup.html)) | GPL-2.0-or-later |
| KeePass 1.44 Classic | Inno Setup | `/VERYSILENT` ([KeePass 1 setup](https://keepass.info/help/v1/setup.html)) | GPL-2.0-or-later |
| WinSCP 6.7 | Inno Setup | `/VERYSILENT /ALLUSERS /NORESTART` ([WinSCP](https://winscp.net/eng/docs/installation#automating_installation)) | GPL-3.0-or-later |
| Firefox 159.0.1 | Mozilla NSIS | `/S` ([Firefox installer](https://firefox-source-docs.mozilla.org/browser/installer/windows/installer/FullConfig.html)) | MPL-2.0 |
| LibreOffice 26.8.2 | MSI | `msiexec /i … /qn` + properties ([TDF wiki](https://wiki.documentfoundation.org/Deployment_and_Migration#Silent_and_Unattended_Installations)) | MPL-2.0 |
| GIMP 3.2.8 | Inno Setup | `/VERYSILENT /ALLUSERS` ([Inno Setup](https://jrsoftware.org/ishelp/topic_setupcmdline.htm)) | GPL-3.0 |
| Inkscape 1.4.4 | MSI | `msiexec /i … /qn` ([msiexec](https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/msiexec)) | GPL |

KeePass 1.44 (the native Classic Edition, same vendor) is covered by the
KeePass approval; KeePass 2.62 needs a .NET runtime.

Runtimes: **wine-mono 10.4.1** (MIT, parts GPL/LGPL; owner approval
2026-11-06), the version and sha256 that
`personality/wine/dlls/appwiz.cpl/addons.c` expects, is pinned in the
manifest's `[runtime.wine-mono]`. Apps that list it in `runtimes` get it in
their prefix's Wine download cache, where `wineboot` finds and installs it;
the other apps run with `mscoree` disabled. Gecko (mshtml) is not approved
and stays disabled.

The installers are downloaded once into `build/compat/cache` and are never
committed. The last four are large, several hundred MB in all, so CI only
runs them on manual dispatch.

## Method

`tools/compat/run.py` runs each app on its own, in a new prefix under its
own Xvfb server (never the host display), with the Setu personality build
(`SETU_WINE`, by default the checkout's or the main checkout's
`build/personality/install`). Every stage is timed and recorded as pass,
fail, skip (an earlier stage failed) or n/a, with a short reason:

| Stage | Pass when |
|---|---|
| download | the cached installer's sha256 matches the pinned one |
| prefix | `wineboot --init` made a fresh prefix (one virtual desktop, 1280x800) |
| install | the silent install exits 0 (or 3010) within its timeout |
| files | the program and the other listed files exist |
| registry | an `…\CurrentVersion\Uninstall` key with the expected DisplayName exists (HKLM 64- and 32-bit views, HKCU) |
| shortcut | a Start Menu `.lnk` for it exists (all users or the current user) |
| launch | a visible top-level window of its processes matches the manifest pattern (`compat-probe windows`, over `EnumWindows`) |
| screenshot | a 400 px wide thumbnail of the Xvfb screen was taken |
| alive | it is still running, with that window, 10 s later (`--alive N`) |
| close | after `WM_CLOSE` to its windows, all its processes exit within 30 s |
| uninstall | (`--uninstall`) its quiet uninstall removes the key and the program |

Console programs (WinSW) pass `launch` on exit code 0 and expected output;
their window stages are n/a. An app **works** when every stage up to
`close` passes; **launches** when it installed and showed its window;
**installs only** when only the install passed.

`compat-probe.exe` (`tools/compat/compat-probe.c`, built with mingw-w64) runs
inside the prefix and lists windows (with their process), processes
(Toolhelp snapshot) and Uninstall keys, and posts `WM_CLOSE`. Processes of
the session itself (explorer, services, winedevice, msiexec, ...) are not
counted as the app's.

Determinism: each app gets a new prefix at a fixed path, host name `setu`
(a user + UTS namespace, as `tools/shell-smoke.sh` does), `TZ=UTC`,
`en_US.UTF-8`, a fixed screen size, the same sample files, and a new
Firefox profile. Mono and Gecko are disabled (`mscoree,mshtml=`), so no
download prompt can appear. Logs run with `WINEDEBUG=err+all`; they are
trimmed (repeated lines collapsed, head and tail of long logs kept) and the
first line that points at a failing API (an unimplemented function, a
missing DLL, an unhandled exception, the first unexpected `err:`) is put in
the failure reason. At most two apps run at once (`-j 2`); prefixes are
deleted afterwards (`--keep` keeps them).

CI: `.github/workflows/compat.yml` runs the small apps nightly and on
manual dispatch (`small`, `large`, `all`, or one app), and uploads the
dashboard as the `compat-dashboard` artifact. It is informational: it
never runs on push or pull requests.

## Latest results

<!-- results:begin -->
(not run yet)
<!-- results:end -->

## Known blockers

(filled from the latest run)
