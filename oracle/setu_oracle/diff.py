# SPDX-License-Identifier: MIT OR Apache-2.0
"""Compare a Setu result against a Windows golden result."""
from __future__ import annotations

from dataclasses import dataclass, field

# Statuses, from best to worst.
PASS = "pass"
FAIL = "fail"
NO_GOLDEN = "no-golden"
NO_RESULT = "no-result"
ERROR = "error"  # harness-level problem on the Setu side (bad output, timeout)


@dataclass
class CaseDiff:
    name: str
    status: str  # pass | fail | missing | extra
    fields: list[tuple[str, object, object]] = field(default_factory=list)  # (field, windows, setu)


@dataclass
class TestDiff:
    test: str
    status: str
    cases: list[CaseDiff] = field(default_factory=list)
    notes: list[str] = field(default_factory=list)

    @property
    def case_counts(self) -> dict:
        c = {"pass": 0, "fail": 0, "missing": 0, "extra": 0}
        for cd in self.cases:
            c[cd.status] += 1
        return c


_MISSING = "<absent>"


def compare(test: str, golden: dict | None, setu: dict | None) -> TestDiff:
    if golden is None:
        td = TestDiff(test, NO_GOLDEN)
        if setu:
            td.notes.append(f"Setu produced {len(setu['cases'])} cases; record a golden file on Windows.")
        return td
    if setu is None:
        return TestDiff(test, NO_RESULT, notes=["no Setu result; run `oracle/run.py setu`"])

    td = TestDiff(test, PASS)
    if golden.get("errors"):
        td.notes.append("golden file has harness errors: " + "; ".join(golden["errors"]))
    if setu.get("errors"):
        td.notes += [f"setu: {e}" for e in setu["errors"]]
    if golden["exit_code"] != setu["exit_code"]:
        td.notes.append(f"exit code: windows={golden['exit_code']} setu={setu['exit_code']}")
    if golden["complete"] != setu["complete"]:
        td.notes.append(f"complete: windows={golden['complete']} setu={setu['complete']}")

    g, s = golden["cases"], setu["cases"]
    for name, gfields in g.items():
        if name not in s:
            td.cases.append(CaseDiff(name, "missing"))
            continue
        sfields = s[name]
        diffs = []
        for k in list(gfields) + [k for k in sfields if k not in gfields]:
            gv, sv = gfields.get(k, _MISSING), sfields.get(k, _MISSING)
            if gv != sv:
                diffs.append((k, gv, sv))
        td.cases.append(CaseDiff(name, "fail" if diffs else "pass", diffs))
    for name in s:
        if name not in g:
            td.cases.append(CaseDiff(name, "extra"))

    if any(c.status != "pass" for c in td.cases) or golden["exit_code"] != setu["exit_code"] \
            or golden["complete"] != setu["complete"]:
        td.status = FAIL
    if setu.get("errors") and not setu["complete"]:
        td.status = ERROR
    return td
