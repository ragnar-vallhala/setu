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
Firefox profile. Gecko is disabled (`mshtml=`), and so is Mono
(`mscoree=`) unless the app lists the pinned wine-mono, so no download
prompt can appear. First-run prompts that ignore `WM_CLOSE` (KeePass's
update-check question, which has no Cancel button) are answered by
clicking the button the manifest names (`dismiss`), and the result says
so. Logs run with `WINEDEBUG=err+all`; they are
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
Setu `1beaa7262a0-dirty`, personality wine-11.0 (tree 76b2bcd5525), 2026-11-06 09:13 UTC: **11/11 installed, 10/11 launched, 10/11 pass every stage.**

| App | Version | Verdict | download | prefix | install | files | registry | shortcut | launch | screenshot | alive | close | uninstall |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| winsw | 2.12.0 | Works | ✓ | ✓ | ✓ | ✓ | n/a | n/a | ✓ | n/a | n/a | n/a | n/a |
| 7-zip | 26.05 | Works | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| notepad++ | 8.9.8.1 | Works | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| putty | 0.85 | Works | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| keepass | 2.62 | Works | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| keepass-classic | 1.44 | Works | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| winscp | 6.7 | Installs only | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✗ | – | – | – | ✓ |
| firefox | 159.0.1 | Works | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| libreoffice | 26.8.2 | Works | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| gimp | 3.2.8 | Works | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| inkscape | 1.4.4 | Works | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |

Failures:

- **winscp** launch: compat-probe could not list windows within 60s (session hung) [log: err:secur32:SECUR32_initSchannelSP no schannel support, expect problems]
<!-- results:end -->

## Known blockers

First failing Windows API or behaviour for each app that does not work,
as reported to the shell-api agent (`BUG-FOR shell-api …` in room
setu-m3):

- **WinSCP 6.7** (32-bit Delphi, under wow64): installs, registers and
  uninstalls, but no window appears within 60 s, and in some runs the
  whole session stops answering (compat-probe cannot even list windows
  until the server is killed). With `+seh` the main
  thread spends the time in user callbacks (about 424,000 wow64 callback
  returns in 30 s, a window-message storm); the only `err:` lines are
  comctl32's listview `unknown msg 109d` (`LVM_ENABLEGROUPVIEW`, a stub)
  and `unknown msg 10bd`; a second start in the same prefix ended in an
  unhandled Delphi exception (`0x0EEDFADE`). KeePass 2 sends 109d too and
  works, so the listview stub may not be the cause. Reported 2026-11-06.
- **Firefox 159.0.1** (the table above predates Setu reporting Windows
  11): as Windows 11 build 26300 it takes its Windows App SDK path
  (CoreMessagingXP, Microsoft.UI.Windowing, Windows.UI.Composition and
  DirectComposition). Until 2026-11-10 that crashed and prefixes reported
  Windows 10 to firefox.exe only; with ALPC, the Compositor activation
  factory, the private DirectComposition device and visual methods it
  calls and GDI-compatible DirectComposition surfaces, it now starts as
  Windows 11, and wine.inf removes the old per-app override
  (`[SetuAppDefaults.Del]`); see docs/DEFAULT_APPS.md, "Known issues".
  Its first start after login no longer crashes and its sandboxed child
  processes start (no more `c0000022`); the sandbox's job-object limits and
  process mitigation policies are still stubs.

Not Setu bugs, but worth knowing:

- KeePass 2.62 is a .NET Framework app: it needs the wine-mono runtime
  (pinned and installed into its prefix; it works with it).
- KeePass 1.44 and 2.62 ask on first start whether to enable update
  checks, in a dialog without a Cancel button; `run.py` answers it.
- Downloads: SourceForge (KeePass, WinSCP) redirects to whichever mirror
  it picks, and some are unreachable from some networks; rerun or set a
  mirror if the download stage times out.
