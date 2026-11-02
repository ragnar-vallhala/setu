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

## What will *not* go here

Setu's own MIT/Apache components (registry service, shell, distro tooling).
They live in `services/`, `shell/`, `distro/` and never copy code from here.
