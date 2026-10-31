# Setu

**Setu** (सेतु, "bridge") is an open-source desktop operating system that aims
to run Windows applications natively. It puts a Windows-compatible personality
(the Win32/NT API, registry, services, filesystem semantics, and an
Explorer-style shell) on top of a stock Linux kernel.

> **Status: pre-alpha, foundation stage.** Nothing here boots yet. This repo
> currently holds the architecture, the clean-room rules, and the **oracle
> harness**: the feedback loop that compares Setu's behaviour against a real
> Windows 11 VM, API call by API call.

> Setu is an independent project. It is **not affiliated with, endorsed by, or
> sponsored by Microsoft**. "Windows" is a trademark of Microsoft Corporation,
> used here only to describe compatibility.

## Why

Wine has shown that the Win32 API can be implemented on top of Unix. Setu takes
the next step and makes that personality the *whole operating system*. A user
should install Setu, double-click an `.exe` or `.msi`, and have it behave the
way it would on Windows: same paths, same registry, same services, same error
codes.

To get there we need to *know* how Windows behaves, without ever looking inside
it. That's the job of the oracle.

## How it works: the oracle loop

```
 tests/win32/<dll>/<api>.c ──mingw-w64──▶ <api>.exe
                                           │
              ┌────────────────────────────┴───────────────────────────┐
              ▼                                                        ▼
   Windows 11 VM (QEMU/KVM, SSH)                         Setu (personality/: Wine fork)
   reverted to a clean snapshot                          fresh prefix / sandbox
              │                                                        │
              ▼                                                        ▼
   deterministic JSON  ──▶ tests/golden/   ◀──── diff ────▶  build/results/setu/
                                           │
                                           ▼
                           build/report/report.{md,json}
```

Each test program calls one API with a set of inputs and prints only
deterministic facts: return values, `GetLastError()` codes, and the resulting
file and registry state. Windows output becomes the **golden** file. Every
behaviour change in Setu must move those diffs toward zero.

## Repository layout

| Path | What it is |
|---|---|
| `docs/` | [Architecture](docs/ARCHITECTURE.md), [clean-room rules](docs/CLEAN_ROOM.md), [roadmap](docs/ROADMAP.md), [contributing](docs/CONTRIBUTING.md), [oracle guide](docs/ORACLE.md) |
| `oracle/` | The harness (`run.py`) and Windows VM tooling (`oracle/vm/`) |
| `tests/win32/` | Oracle test programs, one file per API: `tests/win32/<dll>/<api>.c` |
| `tests/golden/` | Golden results recorded on real Windows (inputs/outputs only) |
| `tests/expectations.json` | Ratchet: tests that must keep passing on Setu |
| `personality/` | Wine fork (imported from Wine 11.0, no Setu changes yet): NT personality and Win32 layer, LGPL-2.1+ |
| `services/` | *(stub)* registry service, service control manager, filesystem semantics |
| `shell/` | *(stub)* Explorer-style desktop |
| `distro/` | *(stub)* bootable ISO, installer, first-boot setup |

## Quick start (Linux side only)

You need `x86_64-w64-mingw32-gcc`, Wine, and Python 3.11+.

```sh
# Debian/Ubuntu
sudo apt install gcc-mingw-w64-x86-64 wine64 python3
# ...or, without root, a private toolchain under ~/.local/setu-toolchain:
tools/bootstrap-toolchain.sh && export PATH="$HOME/.local/setu-toolchain/bin:$PATH"

# Build the Wine fork (first time ~15 min on 12 cores; see personality/README.md).
# Optional: without it, the harness falls back to stock Wine.
sudo apt install $(grep -v '^#' tools/personality-build-deps.txt)
tools/build-personality.sh

python3 oracle/run.py build     # cross-compile all tests
python3 oracle/run.py setu      # run them on the Setu target (personality build, else stock Wine)
python3 oracle/run.py report    # diff against tests/golden/, write build/report/
```

To record golden files you need a Windows 11 VM. See
[oracle/vm/README.md](oracle/vm/README.md).

## Licensing

* Setu's own code is dual licensed **MIT OR Apache-2.0**.
* Wine-derived code (under `personality/`) stays **LGPL-2.1-or-later**.

See [NOTICE](NOTICE) for the details.

## Clean room

Setu is built from public documentation, public headers, Wine's source, and
black-box observation only. Nobody working on Setu may disassemble or
decompile Windows binaries, or read leaked Microsoft source.
**Read [docs/CLEAN_ROOM.md](docs/CLEAN_ROOM.md) before contributing.**
