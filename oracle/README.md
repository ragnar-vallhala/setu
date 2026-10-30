# oracle/: the Setu oracle harness

Runs each test program in `tests/win32/` on a real Windows 11 VM (golden) and
on Setu (today: stock Wine), diffs the results, and writes a pass/fail report.

* `run.py` is the entry point. Run `python3 oracle/run.py --help`.
* `setu_oracle/` holds the harness code (Python 3.11+, standard library only).
* `vm/` has the Windows VM tooling (QEMU/KVM, unattended install, snapshots).
  See [vm/README.md](vm/README.md).
* `tests/` holds the harness self-tests: `python3 -m unittest discover -s oracle/tests`.
* `config.example.toml`: copy it to `config.toml` to override defaults.

Full guide: [docs/ORACLE.md](../docs/ORACLE.md).
