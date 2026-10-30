# Golden files

`<dll>/<api>.json` holds the normalised output of `tests/win32/<dll>/<api>.c`
as run on a **real Windows 11 VM** by `python3 oracle/run.py windows`.
`_meta.json` records the Windows version the goldens came from.

* Golden files are **only ever written by the harness**. Never edit them by
  hand, and never write them from memory or documentation: they must be
  observed output (docs/CLEAN_ROOM.md).
* `source_sha256` ties each golden to the exact test source. If you change a
  test, re-record its golden file in the same PR.

**Status:** empty. No golden files have been recorded yet (see
oracle/vm/README.md).
