# Setu Architecture

Status: **draft v0.1** (foundation stage). This document describes the target
architecture and the decisions taken so far. Items marked **(proposal)** are
open for discussion. Items marked **(decided)** need a written rationale to
change.

## 1. Goal and non-goals

**Goal.** A desktop OS where Windows applications are first-class. A user
installs Setu, runs an `.exe` or `.msi`, and the app sees a Windows-shaped
world: `C:\` paths, a registry, services, Windows error codes, and an
Explorer-style shell.

**Non-goals (for now).**

* Running Windows kernel-mode drivers. Hardware belongs to Linux drivers.
* Binary compatibility for Linux apps *inside* the Windows personality.
  Linux apps keep working on the host side, but that isn't the product.
* Anti-cheat or DRM that relies on Windows kernel internals.

## 2. "Path 2" in one picture

```
┌───────────────────────────────────────────────────────────────────────┐
│  Windows applications (.exe / .msi)          Setu Shell (Win32 app)   │  user
├───────────────────────────────────────────────────────────────────────┤
│  Win32 layer  — kernel32/kernelbase, user32, gdi32, advapi32, ...     │
│                 (Wine-derived, LGPL)            personality/          │
├───────────────────────────────────────────────────────────────────────┤
│  NT personality — ntdll (PE side)  │  System services                 │
│                                    │   registry · SCM · fs semantics  │
│                                    │   services/                      │
├──────────────────── Host Interface (HI) seam ─────────────────────────┤
│  ntdll unix side + wineserver (object manager, handles, sync,         │
│  registry store)  — talks to Linux via syscalls                       │
├───────────────────────────────────────────────────────────────────────┤
│  Linux LTS kernel (stock)  · ext4 casefold · Wayland · Mesa/Vulkan    │  kernel
└───────────────────────────────────────────────────────────────────────┘
```

## 3. Layers

### 3.1 Kernel: stock Linux LTS (decided)

* We track an upstream **LTS** kernel and do **not** fork it at this stage.
  Distro kernels (Debian's, for example) are fine.
* Kernel features we rely on, all upstream:
  * **ext4 casefold** (`-O casefold`, per-directory `+F`, since 5.2). This gives
    case-insensitive lookup in the kernel for the volume that backs `C:`.
  * `ntsync` (upstream since 6.14) for NT synchronisation primitives
    (mutexes, semaphores, events with NT semantics). Fall back to
    wineserver-based sync on older kernels.
  * `pidfd`, `io_uring` (optional), `fanotify` (file-change notifications),
    `seccomp` (sandboxing).
  * Extended attributes (`user.*`, `security.*`) for NT metadata.
* Anything that needs a kernel patch goes through an RFC in `docs/rfcs/` first,
  and must be upstreamable.

### 3.2 NT personality and Win32 layer: Wine fork (decided)

* `personality/` is a **fork of Wine** (LGPL-2.1+). It starts from the latest
  Wine stable release and tracks upstream regularly. Upstream is the base, not
  a competitor: we send fixes upstream whenever they're general.
* We use Wine's **new WoW64 mode**, so 32-bit Windows apps run without 32-bit
  Linux libraries.
* Setu-specific changes are kept as clearly separated commits or patch series
  in `personality/`. That keeps rebasing on Wine tractable.
* **Behaviour target is Windows, as measured by the oracle.** When Wine and
  Windows disagree, the oracle golden file wins (see §6).
* Graphics: Direct3D through DXVK / vkd3d-proton (proposal). GDI and user32 go
  through Wine's Wayland driver (`winewayland.drv`). X11 is supported only as
  a fallback.

### 3.3 System services (`services/`)

Setu's own code (MIT/Apache) running as Windows-side processes on top of the
personality, or as host-side daemons where that fits better.

**Registry service (proposal).**
* Phase A: keep wineserver as the registry authority (`system.reg` /
  `user.reg`) and fix semantic gaps the oracle finds (ordering, case folding,
  error codes, value-type edge cases).
* Phase B: a transactional store (SQLite or LMDB, decision pending) behind
  the same wineserver protocol. Adds crash safety, hive-style load and unload
  per user, and `RegSaveKey` / `RegLoadKey` with a documented on-disk format
  of our own. We don't parse Microsoft's hive format unless it's publicly
  documented.

**Service Control Manager (proposal).**
* Windows services (`CreateService`, `StartService`, SCM database in
  `HKLM\SYSTEM\CurrentControlSet\Services`) run inside the personality through
  Wine's `services.exe`, extended to match the oracle.
* The whole "Windows system session" (wineserver + `services.exe` + system
  processes) is a single **systemd unit** (`setu-system.service`). Linux
  supervises the personality, and the personality supervises Windows services.
  We don't map each Windows service to a systemd unit: their semantics (SCM
  control codes, recovery actions, dependency groups) don't map cleanly.

**Filesystem semantics (proposal).**
* **Case-insensitivity:** the `C:` volume is an ext4 filesystem with casefold
  enabled at its root, so lookups are case-insensitive in the kernel while
  names keep their case. Wine's userspace case-folding remains the fallback for
  other mounts (USB sticks, network shares).
* **Drive letters:** a per-system `dosdevices` table (`C:` → system volume,
  `D:`… → removable or other volumes), managed by a mount service that listens
  to udev. It exposes `GetLogicalDrives` / `GetVolumeInformation` semantics.
* **DOS attributes and NT metadata:** hidden, system, archive, read-only and
  the 8.3 short name are stored in xattrs (`user.DOSATTRIB`, compatible with
  Wine and Samba).
* **ACLs:** the full NT security descriptor is stored in an xattr, in the
  spirit of Samba's `security.NTACL` (public design). The personality
  enforces it on `CreateFile` / `AccessCheck`. Coarse POSIX permissions are
  derived from it, so the Linux side gives defence in depth. Windows users and
  groups (SIDs) map to Linux uids and gids through a small identity service.
  * *Decision (Milestone 2, owner):* the attribute is **`security.NTACL`**,
    holding one format byte (`1`) and the descriptor (owner, group, DACL,
    SACL) in the self-relative layout of [MS-DTYP] 2.4.6. The wineserver
    (`server/filesec.c`) reads it and checks access on open; a file without it
    keeps Wine's mode-derived descriptor and is not access-checked, so the
    host's existing files behave as before. A new file inherits from its
    parent's stored descriptor; the user's own files are owned by the token's
    default owner (Administrators for Setu's elevated token), as on Windows.
  * *Who writes it:* reading a `security.*` attribute needs no privilege, so
    the wineserver reads it directly. Writing one needs `CAP_SYS_ADMIN`, and
    the wineserver stays unprivileged: it passes the file's open descriptor
    over a unix socket to **`setu-ntacld`** (`services/fs/`), a
    socket-activated systemd service whose only capability is
    `CAP_SYS_ADMIN`, running as a dynamic user under a strict sandbox. The
    helper takes no paths and opens no files; it writes or removes only
    `security.NTACL`, only on a regular file or directory owned by the
    caller's uid (`SO_PEERCRED`), only on a file system mounted under the
    Setu volumes (`/var/lib/setu/volumes/`, `/run/setu/volumes/`), and only a
    well-formed value. Protocol: `services/fs/setu-ntacl.h`.
  * *Fallback:* where there is no helper (developer machines, GitHub CI, the
    oracle) or the helper answers `EXDEV`/`ENOTSUP` (not a Setu volume, no
    security attributes), the same value goes to **`user.setu.sd`**, which an
    ordinary user may write, so the oracle suite runs everywhere. Reads try
    `security.NTACL` first. A file that already has a `security.NTACL` the
    server can't rewrite fails with access denied rather than getting a
    `user.setu.sd` the first one would hide; after a helper write a stale
    `user.setu.sd` is removed.
  * *What it protects:* with `security.NTACL`, Linux tools and other users
    with write access to a file can't change or strip its descriptor (only the
    helper or root can). It is still not a boundary against the file's own
    Unix owner, who can ask the helper directly or change the POSIX bits; the
    POSIX bits stay the Linux boundary.
  * *Samba:* Samba's `vfs_acl_xattr` uses the same attribute name with its
    own NDR-encoded value. Setu does not write that format and ignores values
    it can't parse, so a Setu volume should not also be exported with
    `vfs_acl_xattr`.
  * The owner's own POSIX `rw` (and `x` for directories) bits are always kept,
    so the host user never loses access to their files; deriving tighter POSIX
    bits from the descriptor is still to do.
* **Alternate data streams, reparse points, junctions:** later. Reparse points
  map to symlinks where semantics allow.

### 3.4 Shell (`shell/`)

* Our own Explorer-style desktop: taskbar, start menu, file manager, system
  tray, and a control panel for Setu settings.
* **(proposal)** The shell is itself a **Win32 application** running on the
  personality. That dogfoods the API, makes it the natural owner of `.lnk`,
  file associations and shell extensions, and lets it launch `.exe` / `.msi`
  through `ShellExecuteEx` exactly as Windows apps expect.
* It runs on a minimal Wayland compositor (wlroots-based, proposal) in
  "desktop mode", where every top-level Win32 window is a Wayland toplevel.

### 3.5 Distro (`distro/`)

* **(proposal) Debian stable base.** It matches the "stock LTS kernel" rule,
  has reproducible image tooling (`mmdebstrap`, `live-build`), and gives us a
  long support window. Arch is a reasonable choice for developer images; the
  shipped distro should be Debian.
* Bootable hybrid ISO (UEFI + Secure Boot via shim), with the **Calamares**
  installer. It sets up the ext4-casefold system volume and the first Windows
  user (SID + Linux uid).
* First-boot setup: locale, keyboard, network, time zone, user account. It
  then starts `setu-system.service` and logs into the shell.

### 3.6 Long-term: replaceable kernel layer

The **Host Interface (HI) seam** is the boundary between "Windows-shaped" code
and the host OS:

* the **unix side of ntdll** (the `__wine_unix_call` / unixlib boundary), and
* the **wineserver protocol** (object manager, handles, sync, registry).

Everything above HI is PE code that only sees NT system calls. If we keep HI
narrow and documented (`docs/hi/`, to be written), an NT-compatible kernel
could later replace "ntdll unix side + wineserver + Linux" without touching the
Win32 layer, services, or shell. The rule that makes this possible: **Setu code
above HI never calls Linux directly.**

## 4. Process model (proposal)

| Linux process | Role |
|---|---|
| `setu-system.service` → `wineserver` (system) | NT object manager, registry, handle tables |
| `services.exe`, `winlogon`-equivalent, `setu-registryd` (phase B) | system session |
| per-user session: shell `.exe`, user apps | one Linux uid per Windows user |
| compositor | display, input, seat |

## 5. Licensing boundary (decided)

* `personality/`: LGPL-2.1-or-later (Wine-derived). Never copy code out of it.
* Everything else: MIT OR Apache-2.0.
* Our components interact with `personality/` across process boundaries,
  documented IPC, or dynamic linking only. See [NOTICE](../NOTICE).

## 6. How behaviour is decided: the oracle

Setu doesn't guess how Windows behaves. It measures it.

1. A test program in `tests/win32/<dll>/<api>.c` exercises an API and prints
   deterministic JSON.
2. `oracle/run.py` runs it on a clean Windows 11 VM (the **golden** result) and
   on Setu.
3. Diffs become work items. A fix lands with the doc links and oracle tests
   that justify it ([CLEAN_ROOM.md](CLEAN_ROOM.md)).
4. `tests/expectations.json` ratchets: once a test passes on Setu, CI keeps it
   passing.

See [ORACLE.md](ORACLE.md) for the test format and harness details.

## 7. Open questions

* Registry phase B backing store: SQLite vs LMDB vs custom log-structured.
* Compositor: build our own on wlroots, or adopt an existing one?
* How much of `explorer.exe` behaviour do apps depend on (shell namespace,
  `IShellFolder`)? This decides whether the shell must be COM-complete early.
* Identity mapping for domain/Microsoft accounts (out of scope for 1.0?).
