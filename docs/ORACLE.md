# The Oracle

The oracle is how Setu learns what "correct" means. It runs the same small
program on real Windows and on Setu, then compares what they print.

```
build ──▶ windows (golden) ──▶ setu ──▶ report
```

## Quick reference

```sh
python3 oracle/run.py build                       # compile tests/win32/**/*.c
python3 oracle/run.py windows                     # VM: revert, run, write tests/golden/
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
* The harness can verify determinism: `--repeat 2` runs every test twice and
  refuses to write a golden file for a test whose output changes.

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

## Targets

* **Windows** (`oracle/setu_oracle/targets.py: WindowsTarget`): OpenSSH into
  the VM (default shell `cmd.exe`), revert via the QEMU monitor (`loadvm
  clean`), copy every `.exe` in one `scp`, run each in
  `C:\setu-oracle\work\<test>`. See [oracle/vm/README.md](../oracle/vm/README.md).
* **Setu** (`WineTarget`): today stock Wine with a dedicated prefix
  (`oracle/.cache/wineprefix`), each test in a fresh `build/work/setu/<test>`.
  Once `personality/` builds, point `[setu] wine` at it.

## CI

`.github/workflows/oracle.yml` builds every test, runs the harness
self-tests, runs the Setu side twice (to catch nondeterminism) against the
committed golden files, checks the ratchet, and uploads the report. The
Windows VM side is local-only.
