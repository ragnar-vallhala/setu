# The Oracle

The oracle is how Setu learns what "correct" means. It runs the same small
program on real Windows and on Setu, then compares what they print.

```
build ──▶ windows (golden) ──▶ setu ──▶ report
```

## Quick reference

```sh
python3 oracle/run.py build                       # compile tests/win32/**/*.c
python3 oracle/run.py windows -t kernel32/X       # VM pool: run twice from 'clean', write tests/golden/
python3 oracle/run.py windows --verify            # re-run on Windows, diff against tests/golden/, write nothing
python3 oracle/run.py setu report                 # run on Setu (Wine), diff, report
python3 oracle/run.py all                         # all of the above
python3 oracle/run.py ci                          # build + setu (x2) + report --check-expectations
python3 oracle/run.py setu report -t 'advapi32/*' # a subset (glob, repeatable)
python3 -m unittest discover -s oracle/tests      # harness self-tests
```

Outputs:

| Path | Content | Committed? |
|---|---|---|
| `build/bin/<dll>/<api>.exe` | compiled tests | no |
| `tests/golden/<dll>/<api>.json` | normalised Windows results | **yes** |
| `tests/golden/_meta.json` | Windows version the goldens came from | **yes** |
| `build/results/setu/<dll>/<api>.json` | normalised Setu results | no |
| `build/report/report.md`, `report.json` | pass/fail report with per-field diffs | no (CI artifact) |
| `tests/expectations.json` | ratchet: cases that must keep passing | **yes** |

## Writing a test

One file per API (or per behaviour area): `tests/win32/<dll>/<api>.c`.
The test id is `<dll>/<api>`. Start from an existing test; the shape is:

```c
/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/* Docs: https://learn.microsoft.com/...   <- required: what the test is based on */
#include "oracle.h"

int main(void)
{
    oracle_begin("kernel32/CreateFileW");   /* must equal the test id */

    HANDLE h;
    ORACLE_CALL(h, CreateFileW(L"a.txt", GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL));
    oracle_case("create_new_fresh");        /* unique, stable, snake_case */
    o_handle("ret", h);                     /* "valid" | "INVALID_HANDLE_VALUE" | "NULL" */
    o_lasterr();                            /* number, or "unchanged" */
    o_file_state("file", L"a.txt");         /* exists / attrs / size / first bytes */
    oracle_case_end();
    CloseHandle(h);

    return oracle_end();
}
```

### Output format (schema 1)

JSON Lines on stdout. The header comes first, then one object per case, then
an end marker:

```
{"oracle":1,"test":"kernel32/CreateFileW"}
{"case":"create_new_fresh","ret":"valid","last_error":0,"file.exists":true,...}
{"end":true,"cases":1}
```

Each line is flushed as soon as it's written, so if a test crashes, the cases
before the crash are kept. The harness marks the run `complete: false` and
records the exit code.

### Determinism rules

* **Never print** pointers, handle values, PIDs/TIDs, timestamps, absolute
  paths, user or machine names, or uninitialised memory.
* `ORACLE_CALL(var, expr)` sets the last error to a sentinel (`0xDEADBEEF`),
  makes the call, and captures `GetLastError()` immediately. `last_error:
  "unchanged"` means the API didn't touch it. That's observable behaviour too.
* Work only in the **current directory** (the harness gives each run a fresh,
  empty one) and under **`HKCU\Software\SetuOracle\<dll>_<api>`**
  (`oracle_reg_root()` wipes it, `oracle_reg_cleanup()` removes it).
* Record *order* only when order is the behaviour under test (for example,
  `FindFirstFileW` and `RegEnumKeyExW`). For state snapshots, use the sorted
  helpers (`o_dir_listing(..., 1)`).
* Avoid inputs that the docs say are undefined behaviour (most NULL out-pointers).
* The harness checks determinism: `windows` runs every test twice and
  refuses to write a golden file for a test whose output changes (see
  [Recording on Windows](#recording-on-windows)).

### Helpers (`tests/win32/include/oracle.h`)

| Helper | Emits |
|---|---|
| `o_bool`, `o_u32`, `o_u64`, `o_i64`, `o_hex32`, `o_str`, `o_wstr` | scalar fields |
| `o_handle(k, h)` | handle class, never the value |
| `o_lasterr()` | `last_error` captured by `ORACLE_CALL` |
| `o_status(k, lstatus)` | registry-style return codes |
| `o_bytes(k, p, n)` | hex string (≤256 bytes) |
| `o_file_state(prefix, path)` | `prefix.exists/.attrs/.size/.data` |
| `o_dir_listing(k, dir, sorted)` | comma-separated names |

## How results are compared

`tests/golden/<id>.json` is compared with `build/results/setu/<id>.json`:

* **per case:** each field must be equal (JSON equality). The report lists
  every differing field as `Windows X · Setu Y`. Error-code fields are
  decorated with their `ERROR_*` names, taken from the public mingw-w64
  `winerror.h`.
* **per test:** `pass` if all cases pass and the exit code and completeness
  match. Otherwise `fail`, or `error` if the Setu run crashed or timed out.
  The other statuses are `no-golden` (not yet recorded on Windows) and
  `no-result`.
* Every golden file stores `source_sha256` (test source + header). If the
  test changed after its golden was recorded, the report flags it **STALE**.

## The ratchet

`tests/expectations.json` lists `"<test>:<case>"` entries that pass on Setu.
`report --check-expectations` (used by CI) fails if any of them stop passing.
After a fix, run:

```sh
python3 oracle/run.py setu report --update-expectations
```

and commit the updated file together with the fix.

## Recording on Windows

```sh
python3 oracle/run.py windows -t kernel32/MyTest       # record (or re-record) golden files
python3 oracle/run.py windows --verify                 # re-check every committed golden file
python3 oracle/run.py windows --verify --vm setu-oracle-2   # ... on one particular VM
oracle/vm/vmctl.sh pool                                # which VMs exist, which are busy
```

Don't wrap these in `flock`: the harness takes the VM locks itself (see
[The VM pool](#the-vm-pool)).

### Each golden is recorded twice (flake detection)

`windows` runs every test twice (`--repeat N` for more; fewer than 2 is
refused) and compares the runs with the same rules as the report. If any
case differs, the golden file is **not** written, the run exits 1, and the
differences are listed per case:

```
  windows[setu-oracle]: kernel32/Foo     12 cases  PROBLEM  (14.0s)
      ! nondeterministic: run 2 differed from run 1
      ~ case tick_delta: value: run 1 15 · run 2 16
windows: REFUSED 1 nondeterministic: kernel32/Foo
```

The two runs follow each other on the same machine with no revert in
between (each in a fresh working directory). So run 2 also differs when
run 1 left something behind that changes the output, for example a file
outside the working directory or a registry key that isn't cleaned up. The
Setu side runs tests the same way in CI (`ci` = `--repeat 2`, no reset), so
such a test would be flaky there too. Fix the test, not the harness.

`windows --verify` re-runs tests (twice each, by default) and compares the
result with the committed golden file instead of writing one. It writes
nothing and exits 1 if any test differs from its golden file, or is flaky.
Use it to:

* re-check old golden files for flakiness, e.g. after a VM change or before
  trusting a new VM (`--vm NAME`);
* see what a re-recording would change before running `windows`.

It also tells you when a golden file is STALE (the test changed since).

### Snapshot revert per test

Before **each test**, the harness reverts the VM to its `clean` live
snapshot (`loadvm clean` on the QEMU monitor, which restores RAM, disk,
firmware variables and TPM), waits until SSH answers, and copies in only
that test's program. Whatever a test leaves behind (files anywhere, registry
keys, services, running processes, changed settings) is gone before the
next test starts, so tests can't affect each other and their order doesn't
matter.

Cost (measured on the Windows 10 VM, 4 GiB RAM, with other VMs and builds
on the host): a revert takes 10–16 s, about 15 s on average over the
47-test suite: 13 s for `loadvm` (reading the 2.5 GiB RAM image back) plus
under 2 s until SSH answers. So a test
costs about that much more than its own run time. `--revert-once` reverts only
once per VM at the start (the old behaviour: much faster for a big run,
but tests share the machine); `--no-revert` doesn't revert at all (for
debugging). `revert_each_test = false` in `oracle/config.toml` makes
`--revert-once` the default.

### The VM pool

There can be several identical Windows VMs. `windows` (and `--verify`)
spreads the selected tests over all running VMs at once: one worker per
VM takes tests from a shared queue.

* **Discovery.** The VMs are `oracle/vm/state` (`setu-oracle`, SSH port
  2222) and every `oracle/vm/state-<N>` directory (`setu-oracle-<N>`, port
  2221+N) whose QEMU is running. Each state directory has a `vm.json`
  (`name`, `ssh_port`, optional `extra_locks`). To list VMs explicitly
  instead, put `[[windows.vms]]` tables in `oracle/config.toml` (see
  `oracle/config.example.toml`). `--vm NAME` restricts a run to some VMs.
* **One lock per VM.** A worker uses a VM only while it holds that VM's
  lock file (`<state>/oracle.lock`) and its `extra_locks`, all with
  `flock(2)`. A busy VM is skipped, and the worker waits for it, while the
  other VMs work. In per-test revert mode, a worker releases its VM after
  every test and takes it again, so a short run by someone else gets a turn
  during a long one.
* **Old style still works.** `flock <file> oracle/run.py windows`, where
  `<file>` is listed in the first VM's `extra_locks`, holds the first VM:
  older harness versions only know that VM, and a pool-aware harness sees
  it as busy and uses the other VMs. A pool-aware harness started *inside*
  such a `flock` therefore never uses the first VM: just drop the `flock`.
* **Golden files don't depend on the VM.** Every VM is a clone of the same
  `clean` snapshot (`vmctl.sh --vm N clone`, see
  [oracle/vm/README.md](../oracle/vm/README.md#more-vms)). Before a new VM
  is trusted, `windows --verify --vm <new>` must report every golden as
  `same`.

Speed-up, measured with `windows --verify` over the 47-test suite (two
runs per test, revert before each test): 814 s on one VM, 380 s on two
(23 + 24 tests), about 2.1×. Both VMs agreed with every committed golden
file. Each extra VM needs 4 GiB of host RAM and about 8 GiB of disk.

To run something else on a VM while holding its lock (for example a manual
`vmctl.sh revert`):

```sh
cd oracle && python3 -m setu_oracle.pool locked --vm setu-oracle-2 -- vm/vmctl.sh --vm 2 revert
```

## Targets

* **Windows** (`oracle/setu_oracle/targets.py: WindowsTarget`, one per VM of
  the pool in `pool.py`): OpenSSH into the VM (default shell `cmd.exe`),
  revert via the QEMU monitor (`loadvm clean`) before each test, copy the
  test's `.exe`, run it in `C:\setu-oracle\work\<test>`. See
  [oracle/vm/README.md](../oracle/vm/README.md).
* **Setu** (`WineTarget`): the Wine fork in `personality/wine`, built by
  `tools/build-personality.sh` into `build/personality/install`. Without
  that build, the harness falls back to stock Wine from PATH
  (`--setu-impl personality|stock` forces one). Each implementation gets its
  own prefix (`oracle/.cache/wineprefix-<impl>`), and each test runs in a fresh
  `build/work/setu/<test>`.
  The Setu side is headless: the harness drops `DISPLAY`, `WAYLAND_DISPLAY`
  and `XAUTHORITY` from the environment and selects Wine's null graphics
  driver in the prefix (`HKCU\Software\Wine\Drivers` `Graphics=null`,
  stamped by `.setu-oracle-null-graphics`), so user32/gdi32 tests can create
  hidden windows without touching the host's display.

## CI

`.github/workflows/oracle.yml` builds every test, runs the harness
self-tests, runs the Setu side twice (to catch nondeterminism) against the
committed golden files, checks the ratchet, and uploads the report. The
Windows VM side is local-only.
