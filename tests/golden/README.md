# Golden files

`<dll>/<api>.json` holds the normalised output of `tests/win32/<dll>/<api>.c`
as run on a **real Windows 11 VM** by `python3 oracle/run.py windows`.
`_meta.json` records the Windows version the goldens came from.

* Golden files are **only ever written by the harness**. Never edit them by
  hand, and never write them from memory or documentation: they must be
  observed output (docs/CLEAN_ROOM.md).
* `source_sha256` ties each golden to the exact test source. If you change a
  test, re-record its golden file in the same PR.

**Status: Windows 11 goldens.** Recorded on Windows 11 26H2 Pro (build
26300.9457) on 2026-11-08, replacing the interim Windows 10 22H2 files. Two
tests were nondeterministic on Windows 11 and still carry their Windows 10
goldens until the tests are fixed: advapi32/SvcStart
(process_exit_without_stopped reads the service state before the SCM has
noticed the exit) and user32/messages (timer_probe_peeked depends on whether
the timer has fired yet).
