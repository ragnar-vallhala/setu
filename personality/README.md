# personality/: NT personality and Win32 layer (Wine fork)

> **License: LGPL-2.1-or-later.** Everything in this directory is derived from
> [Wine](https://www.winehq.org/) and stays under Wine's license. See
> `COPYING.LGPL`, `wine/COPYING.LIB` and the top-level `NOTICE`.

**Status:** Wine **11.0** imported into `wine/` as a squashed git subtree
(upstream commit `db11d0fe6a16`; Wine's own history stays upstream).
The oracle harness runs this build as the Setu target. Setu's changes are the
commits touching `wine/` after the import; each one names the oracle cases
and documentation that justify it:

```sh
git log --grep='^Setu-Oracle:' --format='%h %s' -- personality/wine
```

## On-disk state Setu adds

| Where | What |
|---|---|
| xattr `user.WINENOARCHIVE` | Set on a file whose ARCHIVE attribute was cleared, next to Samba's `user.DOSATTRIB`. Holds the file's mtime at that moment; ARCHIVE reads as set again once the mtime changes. On kernels without multigrain timestamps (before Linux 6.13), a write in the same clock tick as the clear can go unnoticed. |

## Mandatory byte-range locks

Windows byte-range locks (`LockFile`, `LockFileEx`) are mandatory: `ReadFile`
fails with `ERROR_LOCK_VIOLATION` when another handle holds an exclusive lock on
any byte of the range, and `WriteFile` fails when any lock covers a byte of the
range, except the writing handle's own exclusive lock (the handle's own shared
lock blocks it too). Upstream Wine tracks locks in the wineserver but does
plain `pread`/`pwrite` in ntdll, so it never enforces them.

How Setu enforces them without slowing down unlocked files:

* The wineserver already mirrors each lock as a POSIX `fcntl` lock on the Unix
  file (shared locks as read locks, exclusive ones as write locks). The kernel's
  lock list on the inode is the "this range has locks" flag; nothing new is
  stored.
* Before a read or write on a regular file (`NtReadFile`, `NtWriteFile`,
  `NtReadFileScatter`, `NtWriteFileGather`, explicit offset or file pointer),
  ntdll asks the kernel with `fcntl(F_GETLK)` whether any lock overlaps the
  range. For a file without locks the kernel answers at once, so there is no
  wineserver round trip, only one cheap syscall.
* Only when some lock overlaps does ntdll send `check_file_lock` to the
  wineserver, which applies the Windows rules against its own lock list
  (owner = the file object, as for lock conflicts) and returns
  `STATUS_FILE_LOCK_CONFLICT` if the I/O is not allowed. No bytes are
  transferred then.

Limits:

* Memory-mapped views (`MapViewOfFile`) and I/O done by Unix code (other Unix
  processes, Unix libraries in the same process) bypass the check.
* If the filesystem has no POSIX locks (the server then stops mirroring them,
  for example on some network filesystems), or a lock lies beyond the largest
  Unix offset, the fast path sees no lock and the I/O goes through.
* POSIX locks held by other Unix processes (another wineserver, native apps)
  are visible to `F_GETLK` too; they only cost a server round trip, the server
  then allows the I/O.
* The check and the I/O are not atomic: a lock taken by another thread between
  them does not stop that one I/O.
* Other operations Windows may also block on locked ranges (truncation with
  `SetEndOfFile`, for example) are not checked.

## Layout

| Path | What |
|---|---|
| `wine/` | The fork: a squashed git subtree of upstream Wine (gitlab.winehq.org/wine/wine) |
| `COPYING.LGPL` | License text |
| `README.md` | This file |

Setu's build script lives outside this directory, in
`tools/build-personality.sh` (MIT/Apache, like the rest of the tooling).

## Building

```sh
sudo apt install $(grep -v '^#' tools/personality-build-deps.txt)
#   no root? tools/bootstrap-toolchain.sh installs the compilers, flex and bison
tools/build-personality.sh            # out of tree: build/personality/{obj,install}
python3 oracle/run.py setu report     # the harness picks up the build automatically
```

* New WoW64 mode (`--enable-archs=x86_64,i386`): 32-bit PE code is built with
  mingw, so no 32-bit Linux libraries are needed.
* Wine's own conformance tests are not built (`--disable-tests`). Setu's
  measure of correctness is the oracle (`docs/ORACLE.md`).
* The script stamps `build/personality/install/SETU_BUILD` with the git tree
  hash of `personality/wine` and skips the build when nothing changed.
  Incremental rebuilds after an edit take seconds to minutes; the first build
  takes a while (tens of minutes on 12 cores).
* CI caches the installed build by that tree hash, so it only rebuilds when
  `personality/wine` changes (`.github/workflows/oracle.yml`).

## Changing the fork

1. **Find the mismatch with the oracle first.** Every behaviour change starts
   from a failing oracle case (`build/report/report.md`).
2. **Read only allowed sources:** learn.microsoft.com, public headers (mingw-w64),
   Wine's own source, and oracle inputs/outputs. Never Windows binaries,
   symbols or leaked source (`docs/CLEAN_ROOM.md`).
3. **One self-contained commit per change,** with trailers:

   ```
   kernel32: Return ERROR_INVALID_NAME from GetFullPathNameW("").

   Setu-Oracle: kernel32/invalid_params:fullpath_empty
   Setu-Docs: https://learn.microsoft.com/...
   ```
4. **Ratchet:** `python3 oracle/run.py setu report --update-expectations`, and
   commit the updated `tests/expectations.json` together with the fix.
5. **Upstream general fixes to Wine** where they apply to Wine as a whole.
   Keep Setu-only behaviour (system session, registry phase B hooks, casefold
   fast path) in clearly named modules.
6. **Respect the Host Interface seam** (docs/ARCHITECTURE.md §3.6). Linux-specific
   code stays on the unix side of ntdll and in wineserver.

## Updating to a new Wine release

```sh
git subtree pull --prefix=personality/wine --squash \
    https://gitlab.winehq.org/wine/wine.git wine-X.Y
tools/build-personality.sh
python3 oracle/run.py setu report      # check for regressions before pushing
```

Git warns `[attr]generated ... not allowed: personality/wine/.gitattributes`
on diffs inside `wine/`. That's expected: git only allows attribute macros at
the top level, so the top-level `.gitattributes` defines that macro instead.

## Not supported: DOS and 16-bit Windows programs

Setu runs 32-bit and 64-bit Windows programs only, like 64-bit Windows.
Starting a DOS (MZ-only) or Win16 (NE) program fails with
`ERROR_EXE_MACHINE_TYPE_MISMATCH` (216), and Wine's `winevdm` is not used.
Users who need such programs can run DOSBox or similar themselves.

## What will *not* go here

Setu's own MIT/Apache components (registry service, shell, distro tooling).
They live in `services/`, `shell/`, `distro/` and never copy code from here.
