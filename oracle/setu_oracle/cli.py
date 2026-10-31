# SPDX-License-Identifier: MIT OR Apache-2.0
"""Command line: python3 oracle/run.py STEP [STEP ...] [options]

Steps run in the order given:
  build     cross-compile tests/win32/**/*.c with mingw-w64
  windows   run on the Windows VM and write golden files to tests/golden/
  setu      run on the Setu side (stock Wine for now) -> build/results/setu/
  report    diff Setu results against golden files -> build/report/report.{md,json}
  all       = build windows setu report
  ci        = build setu report --check-expectations --repeat 2  (no VM needed)
"""
from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path

from . import config as C
from . import diff, report, results
from .targets import Target, WindowsTarget, WineTarget
from .tests import Test, build, discover, error_names

RESULTS_SETU = C.BUILD_DIR / "results" / "setu"
REPORT_DIR = C.BUILD_DIR / "report"


def golden_path(t: Test) -> Path:
    return C.GOLDEN_DIR / f"{t.id}.json"


def run_target(target: Target, tests: list[Test], repeat: int) -> tuple[dict[str, dict], list[str]]:
    """Run every test `repeat` times. Returns results and the ids of nondeterministic tests."""
    target.prepare(tests)
    out, flaky = {}, []
    try:
        for t in tests:
            if not t.exe.exists():
                print(f"  {target.name}: skip {t.id} (not built)")
                continue
            t0 = time.time()
            first = results.parse(t.id, target.run(t))
            for _ in range(repeat - 1):
                again = results.parse(t.id, target.run(t))
                if results.comparable(again) != results.comparable(first):
                    flaky.append(t.id)
                    first["errors"].append("nondeterministic: output differed between repeated runs")
                    break
            status = "ok" if first["complete"] and not first["errors"] else "PROBLEM"
            print(f"  {target.name}: {t.id:44s} {len(first['cases']):3d} cases  {status}  ({time.time() - t0:.1f}s)")
            for e in first["errors"]:
                print(f"      ! {e}")
            out[t.id] = first
    finally:
        target.finish()
    return out, flaky


def step_windows(cfg, tests, args) -> int:
    target = WindowsTarget(cfg, revert=not args.no_revert)
    res, flaky = run_target(target, tests, args.repeat)
    meta = target.describe()
    written = 0
    for tid, r in res.items():
        t = next(t for t in tests if t.id == tid)
        if tid in flaky:
            print(f"  windows: NOT writing golden for {tid}: nondeterministic on Windows (fix the test)")
            continue
        r = dict(r)
        r["source_sha256"] = t.source_sha256()
        p = golden_path(t)
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(results.dump(r))
        written += 1
    meta_path = C.GOLDEN_DIR / "_meta.json"
    meta_path.write_text(results.dump({"recorded_on": meta, "recorded_with": "oracle/run.py windows"}))
    print(f"windows: wrote {written} golden files to {C.GOLDEN_DIR} ({meta.get('version', '?')})")
    return 1 if flaky else 0


def step_setu(cfg, tests, args) -> int:
    if args.setu_impl:
        cfg["setu"]["implementation"] = args.setu_impl
    target = WineTarget(cfg, fresh_prefix=args.fresh_prefix)
    print(f"setu: using {target.impl}: {' '.join(target.wine)}")
    res, flaky = run_target(target, tests, args.repeat)
    RESULTS_SETU.mkdir(parents=True, exist_ok=True)
    for tid, r in res.items():
        p = RESULTS_SETU / f"{tid}.json"
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(results.dump(r))
    (RESULTS_SETU / "_meta.json").write_text(results.dump(target.describe()))
    print(f"setu: wrote {len(res)} results to {RESULTS_SETU}")
    if flaky:
        print(f"setu: nondeterministic tests: {', '.join(flaky)}")
    return 1 if flaky else 0


def _load(p: Path) -> dict | None:
    return json.loads(p.read_text()) if p.exists() else None


def load_expectations() -> set[str]:
    data = _load(C.EXPECTATIONS) or {}
    return set(data.get("passing_cases", []))


def step_report(cfg, tests, args) -> int:
    diffs = []
    passing_now: set[str] = set()
    for t in tests:
        g = _load(golden_path(t))
        s = _load(RESULTS_SETU / f"{t.id}.json")
        d = diff.compare(t.id, g, s)
        if g and g.get("source_sha256") and g["source_sha256"] != t.source_sha256():
            d.notes.append("golden file is STALE: the test source changed since it was recorded; "
                           "re-run `oracle/run.py windows`")
        diffs.append(d)
        passing_now |= {f"{t.id}:{c.name}" for c in d.cases if c.status == "pass"}

    expected = {e for e in load_expectations() if e.split(":")[0] in {t.id for t in tests}}
    ratchet = {
        "expected_passing": len(expected),
        "regressions": sorted(expected - passing_now),
        "newly_passing": sorted(passing_now - expected),
    }
    meta = {"golden": (_load(C.GOLDEN_DIR / "_meta.json") or {}).get("recorded_on"),
            "setu": _load(RESULTS_SETU / "_meta.json")}
    md, js = report.write(REPORT_DIR, diffs, meta, error_names(cfg), ratchet)
    s = report.summarize(diffs)
    c = s["cases"]
    print(f"report: {s['tests']} tests {s['by_status']}; cases pass={c['pass']} fail={c['fail']} "
          f"missing={c['missing']} extra={c['extra']}")
    print(f"report: {md}\nreport: {js}")

    if args.update_expectations:
        keep = load_expectations() - expected  # entries for tests outside this selection
        C.EXPECTATIONS.write_text(json.dumps({"passing_cases": sorted(keep | passing_now)}, indent=2) + "\n")
        print(f"report: {C.EXPECTATIONS} now lists {len(keep | passing_now)} passing cases")
    if ratchet["newly_passing"] and not args.update_expectations:
        print(f"report: {len(ratchet['newly_passing'])} cases pass but are not in tests/expectations.json "
              "(run `report --update-expectations` to ratchet them in)")
    if args.check_expectations and ratchet["regressions"]:
        print(f"report: REGRESSIONS ({len(ratchet['regressions'])}):")
        for r in ratchet["regressions"]:
            print(f"  - {r}")
        return 1
    return 0


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(prog="oracle/run.py", description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("steps", nargs="+", choices=["build", "windows", "setu", "report", "all", "ci"])
    ap.add_argument("-t", "--test", action="append", dest="tests", metavar="PATTERN",
                    help="test id or glob, e.g. kernel32/CreateFileW or 'advapi32/*' (repeatable)")
    ap.add_argument("-c", "--config", help="config file (default: oracle/config.toml if present)")
    ap.add_argument("--repeat", type=int, default=1, help="run each test N times and flag nondeterminism")
    ap.add_argument("--force-build", action="store_true", help="rebuild even if up to date")
    ap.add_argument("--no-revert", action="store_true", help="windows: don't revert the VM snapshot first")
    ap.add_argument("--fresh-prefix", action="store_true", help="setu: recreate the Wine prefix first")
    ap.add_argument("--setu-impl", choices=["auto", "personality", "stock"],
                    help="setu: which build to run (default: [setu] implementation, i.e. auto)")
    ap.add_argument("--update-expectations", action="store_true",
                    help="report: write currently passing cases to tests/expectations.json")
    ap.add_argument("--check-expectations", action="store_true",
                    help="report: exit 1 if a case listed in tests/expectations.json no longer passes")
    args = ap.parse_args(argv)

    steps: list[str] = []
    for s in args.steps:
        if s == "all":
            steps += ["build", "windows", "setu", "report"]
        elif s == "ci":
            steps += ["build", "setu", "report"]
            args.check_expectations = True
            args.repeat = max(args.repeat, 2)
        else:
            steps.append(s)

    cfg = C.load(args.config)
    tests = discover(args.tests)
    rc = 0
    for s in steps:
        print(f"== {s} ({len(tests)} tests)")
        if s == "build":
            if build(cfg, tests, force=args.force_build):
                rc = 1
        elif s == "windows":
            rc |= step_windows(cfg, tests, args)
        elif s == "setu":
            rc |= step_setu(cfg, tests, args)
        elif s == "report":
            rc |= step_report(cfg, tests, args)
    return rc


if __name__ == "__main__":
    sys.exit(main())
