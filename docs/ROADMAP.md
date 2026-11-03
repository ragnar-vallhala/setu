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

1. **100 kernel32 oracle tests passing** on Setu (golden files from Windows 11
   23H2 or later, recorded by the harness, ratcheted in CI). Coverage:
   files and directories, `FindFirstFile*`, paths (`GetFullPathNameW`,
   long/short names, `\\?\`), handles, `GetLastError` semantics, environment,
   processes (`CreateProcessW` exit codes and inheritance), sync primitives.
   Status 2026-11-03: 75 of 106 kernel32 tests pass every case (goldens are
   still from the interim Windows 10 22H2 VM).
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

- Registry phase A complete for the advapi32 Reg* family (oracle coverage ~150
  tests); `reg.exe` and `regedit` behave the same.
- SCM: `CreateService` / `StartService` / `ControlService` oracle suite; a
  third-party service installs and runs.
- `C:` on an ext4-casefold volume in the distro image; drive-letter service.
- NT ACL storage in xattrs with an `AccessCheck` oracle suite.

## Milestone 3: Desktop

- Explorer-style file manager, start menu with installed-app discovery
  (`.lnk`, Start Menu folders), file associations.
- Installer ISO (Calamares), first-boot wizard.
- App compatibility dashboard: top-N apps installed and launched in CI.

## Milestone 4: Daily driver alpha

- Updates, recovery, Secure Boot-signed images.
- Hardware enablement through the Linux stack (GPU, Wi-Fi, audio, printing).
- Kernel-seam documentation (`docs/hi/`) to prepare for a replaceable kernel
  layer.
