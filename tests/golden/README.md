# Golden files

`<dll>/<api>.json` holds the normalised output of `tests/win32/<dll>/<api>.c`
as run on a **real Windows 11 VM** by `python3 oracle/run.py windows`.
`_meta.json` records the Windows version the goldens came from.

* Golden files are **only ever written by the harness**. Never edit them by
  hand, and never write them from memory or documentation: they must be
  observed output (docs/CLEAN_ROOM.md).
* `source_sha256` ties each golden to the exact test source. If you change a
  test, re-record its golden file in the same PR.

**Status: interim Windows 10 goldens.** The current files were recorded on
Windows 10 22H2 (build 19045.2965), because no Windows 11 ISO was available
yet. The file and registry APIs covered so far are the same across 10 and 11.
Re-record everything on Windows 11 (`oracle/run.py windows --repeat 2`) as
soon as a Windows 11 VM exists, and review any diffs that appear.
