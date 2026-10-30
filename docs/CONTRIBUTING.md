# Contributing to Setu

Thanks for helping build the bridge. Please read these first:

1. [CLEAN_ROOM.md](CLEAN_ROOM.md). **Mandatory.** It covers which sources you
   may and may not use.
2. [ARCHITECTURE.md](ARCHITECTURE.md). Where your change fits.
3. [ORACLE.md](ORACLE.md). How behaviour is measured.

## The workflow

Most work follows one loop:

1. **Write an oracle test** in `tests/win32/<dll>/<api>.c` that exposes a
   behaviour (see ORACLE.md for the format).
2. **Record the golden file** on a Windows 11 VM: `python3 oracle/run.py windows -t <dll>/<api>`.
   If you have no VM, open the PR with the test only. A maintainer with a VM
   records the golden file.
3. **Run it on Setu:** `python3 oracle/run.py setu report -t <dll>/<api>`.
4. **Fix the mismatch** in `personality/` or `services/`, citing your sources.
5. Once it passes, add the test to `tests/expectations.json` (`"pass"`), so CI
   keeps it green.

## Pull requests

* Keep PRs small and focused. One API or one behaviour is ideal.
* Write clear commit messages: an imperative summary line plus a body
  explaining *why*.
* PRs touching `personality/`, `services/`, `shell/` or `distro/` **must**
  include the *Clean-room sources* section from the PR template. CI enforces
  this.
* Never commit golden files edited by hand. They must come from
  `oracle/run.py windows`.
* Never commit Windows media, VM images, product keys, or files copied out
  of a Windows installation.

## Licensing of contributions

* Outside `personality/`: by contributing you agree your work is dual
  licensed under MIT OR Apache-2.0.
* Inside `personality/`: LGPL-2.1-or-later, like Wine.

Add an SPDX header to new source files:

```c
/* SPDX-License-Identifier: MIT OR Apache-2.0 */
```

## Code style

* C test programs: C11, `-Wall -Wextra -Werror` clean under mingw-w64. Use the
  helpers in `tests/win32/include/oracle.h`.
* Python (`oracle/`): standard library only, Python 3.11+, formatted
  black-style.
* Shell scripts: POSIX `sh` or `bash` with `set -euo pipefail`.

## Naming

Don't use "Windows", "Win", or other Microsoft trademarks in component or
package names. "Win32" is fine when it describes the API itself (as in
`tests/win32/`).
