# Roadmap

Setu is built **oracle-first**. Every milestone is measured in oracle tests
that match real Windows, not in features that seem to work.

## Milestone 0: Foundation (current)

- [x] Architecture, clean-room rules, licensing split
- [x] Oracle harness: build → run on Windows VM → run on Setu → diff → report
- [x] ~10 seed tests (kernel32 file APIs, registry, error codes)
- [x] CI: build tests and run the Setu side against committed golden files
- [x] First golden files recorded from a real Windows VM (interim: Windows 10 22H2, build 19045)
- [ ] Re-record golden files on Windows 11
- [x] First expectations ratchet (`tests/expectations.json`): 167/188 cases pass on stock Wine 9.0

## Milestone 1: "Measured kernel32" + "first boot" (proposed)

**Exit criteria**

1. ✅ **100 kernel32 oracle tests passing** on Setu (golden files from Windows 11
   23H2 or later, recorded by the harness, ratcheted in CI). Coverage:
   files and directories, `FindFirstFile*`, paths (`GetFullPathNameW`,
   long/short names, `\\?\`), handles, `GetLastError` semantics, environment,
   processes (`CreateProcessW` exit codes and inheritance), sync primitives.
   Done 2026-11-03 against the interim Windows 10 22H2 oracle: 102 of 122
   kernel32 tests pass every case (7,769 cases ratcheted). Re-recording the
   goldens on Windows 11 is still open (see above).
2. ✅ **The Wine fork exists:** `personality/wine` imported from Wine 11.0,
   builds in CI (cached by source tree), and replaces stock Wine as the Setu
   target in the harness. Done 2026-10-31.
3. ✅ **Shell boots in a VM:** a Debian-based dev image (`distro/`) boots in QEMU
   to a Wayland session running the Setu shell stub (taskbar + launcher). From
   there it can launch `notepad.exe` and one third-party `.msi`. Done
   2026-11-03: 7-Zip 26.05's `.msi` installs and runs (`distro/tests/msi-smoke.sh`).
4. ✅ Oracle infrastructure: snapshot revert per test, run in parallel on 2+ VMs,
   flake detection (each golden recorded twice and compared). Done 2026-11-03
   (`oracle/run.py windows` VM pool, `--verify`).

## Milestone 2: System services

Oracle switched 2026-11-08 to Windows 11 26H2 (build 26300.9457), which
replaces the interim Windows 10 VMs. All goldens were re-recorded. Status
2026-11-09: 12,206 of 12,870 cases match Windows 11 (332 tests); Setu reports
itself as Windows 11 26H2. The status lines below are the earlier Windows 10
figures.

Status 2026-11-06, against the interim Windows 10 22H2 oracle (310 tests,
11,520 cases ratcheted, CI deterministic over two runs):

- ✅ Registry phase A complete for the advapi32 Reg* family (oracle coverage ~150
  tests); `reg.exe` and `regedit` behave the same.
  142 registry tests: 116 advapi32/ntdll key and value tests (2,311 of 2,391
  cases pass) and 26 `reg.exe`/`regedit` tests (all 476 cases pass).
  Phase B, still open: descriptors in saved hives, the `RegReplaceKey` file
  swap, `RegLoadAppKey`, real KTM transactions and HKCR merging.
- ✅ SCM: `CreateService` / `StartService` / `ControlService` oracle suite; a
  third-party service installs and runs.
  18 tests including `sc.exe` (547 of 569 cases pass). WinSW 2.12.0 (MIT,
  pinned by sha256, downloaded at test time) installs, starts, stops and
  uninstalls (`tests/thirdparty/winsw-service.sh`). Windows' required-buffer
  size estimates are a known difference.
- ✅ `C:` on an ext4-casefold volume in the distro image; drive-letter service.
  `setu-drives` maps volumes and removable disks to letters
  (`distro/tests/drives-smoke.sh`); `Z:` (the Linux root) stays visible for now.
- ✅ NT ACL storage in xattrs with an `AccessCheck` oracle suite.
  Descriptors live in `security.NTACL`, written by the narrow `setu-ntacld`
  helper (`user.setu.sd` only as a no-root fallback); 16 security tests, all
  769 cases pass (`distro/tests/ntacl-smoke.sh`). The wineserver still runs as
  the session user, so processes of that user can change their own files'
  descriptors through the helper; a system-account wineserver closes this.

## Milestone 3: Desktop

- Explorer-style file manager, start menu with installed-app discovery
  (`.lnk`, Start Menu folders), file associations.
- Installer ISO (Calamares), first-boot wizard.
- App compatibility dashboard: top-N apps installed and launched in CI.
- Look and feel: a modern, flat, light design of Setu's own
  (`docs/DESIGN.md`): control theme, window frames, Inter as the UI font,
  restyled taskbar, start menu and file manager, wallpaper and branding.
- Theme packs (`docs/THEMES.md`): every colour, font, size, icon and
  artwork comes from a swappable pack; `setu-theme` applies one; ships
  `setu-light` (default) and `setu-dark`.

- Linux apps alongside Windows apps: Setu runs both, and the desktop
  treats them the same.
  - Taskbar: lists and switches Linux (Wayland/X11) windows too, through
    labwc's foreign-toplevel protocol and a small bridge to setu-shell.
  - Start menu and search: Linux apps from freedesktop `.desktop` entries
    next to Windows `.lnk` shortcuts, with their icons.
  - Look: GTK 3/4 and Qt themes generated from the active theme pack
    (colours, light/dark, fonts, icons, cursors), so Linux apps follow
    `setu-theme apply`.
  - Installing: Flatpak with Flathub, alongside `.exe`/`.msi`.
  - Files: one view of the disk; `.desktop` files and Linux executables
    open from setu-files.

## Milestone 4: Daily driver alpha

- Updates, recovery, Secure Boot-signed images.
- Hardware enablement through the Linux stack (GPU, Wi-Fi, audio, printing).
- Kernel-seam documentation (`docs/hi/`) to prepare for a replaceable kernel
  layer.
