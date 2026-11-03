# SPDX-License-Identifier: MIT OR Apache-2.0
"""Command line: python3 oracle/run.py STEP [STEP ...] [options]

Steps run in the order given:
  build     cross-compile tests/win32/**/*.c with mingw-w64
  windows   run on the Windows VM pool (each test twice) and write golden files
            to tests/golden/; with --verify, compare with them instead
  setu      run on the Setu side (stock Wine for now) -> build/results/setu/
  report    diff Setu results against golden files -> build/report/report.{md,json}
  all       = build windows setu report
  ci        = build setu report --check-expectations --repeat 2  (no VM needed)
"""
from __future__ import annotations

import argparse
import json
import sys
import threading
import time
from pathlib import Path

from . import config as C
from . import diff, pool, report, results
from .targets import Target, WindowsTarget, WineTarget
from .tests import Test, build, discover, error_names

RESULTS_SETU = C.BUILD_DIR / "results" / "setu"
REPORT_DIR = C.BUILD_DIR / "report"


def golden_path(t: Test) -> Path:
    return C.GOLDEN_DIR / f"{t.id}.json"


_print_lock = threading.Lock()


def say(*a) -> None:
    with _print_lock:
        print(*a, flush=True)


def run_repeated(target: Target, t: Test, repeat: int, label: str) -> tuple[dict, list[str]]:
    """Run one test `repeat` times. Returns the first result and how later runs differed (empty = stable)."""
    t0 = time.time()
    target.start_test(t)
    first = results.parse(t.id, target.run(t))
    diffs: list[str] = []
    for n in range(2, repeat + 1):
        again = results.parse(t.id, target.run(t))
        diffs = results.differences(results.comparable(first), results.comparable(again), "run 1", f"run {n}")
        if diffs:
            first["errors"].append(f"nondeterministic: run {n} differed from run 1")
            break
    status = "ok" if first["complete"] and not first["errors"] else "PROBLEM"
    say(f"  {label}: {t.id:44s} {len(first['cases']):3d} cases  {status}  ({time.time() - t0:.1f}s)")
    for e in first["errors"]:
        say(f"      ! {e}")
    for d in diffs:
        say(f"      ~ {d}")
    return first, diffs


def run_target(target: Target, tests: list[Test], repeat: int) -> tuple[dict[str, dict], dict[str, list[str]]]:
    """Run every test `repeat` times. Returns results and, per nondeterministic test, what differed."""
    target.prepare(tests)
    out, flaky = {}, {}
    try:
        for t in tests:
            if not t.exe.exists():
                print(f"  {target.name}: skip {t.id} (not built)")
                continue
            out[t.id], diffs = run_repeated(target, t, repeat, target.name)
            if diffs:
                flaky[t.id] = diffs
    finally:
        target.finish()
    return out, flaky


def run_windows_pool(cfg, tests: list[Test], args) -> tuple[dict[str, dict], dict[str, list[str]], dict, int]:
    """Run the tests on every free VM of the pool at once.

    One worker thread per VM. A worker takes its VM's lock(s) when they are
    free, then pulls tests from a shared queue. In revert-per-test mode it
    lets go of the VM between tests, so other agents' runs interleave with a
    long one instead of waiting for all of it.
    Returns results, flaky tests, the Windows description and the number of
    tests that could not be run.
    """
    vms = pool.discover(cfg, args.vm)
    todo: list[Test] = []
    for t in tests:
        if t.exe.exists():
            todo.append(t)
        else:
            say(f"  windows: skip {t.id} (not built)")
    queue = list(todo)
    qlock = threading.Lock()
    out: dict[str, dict] = {}
    flaky: dict[str, list[str]] = {}
    meta: dict = {}
    per_vm: dict[str, int] = {}
    revert_times: list[tuple[float, float]] = []
    deadline = time.time() + cfg["windows"]["lock_wait"]
    each = False if args.revert_once else None
    say(f"windows: pool {', '.join(f'{v.name} (:{v.settings['port']})' for v in vms)}; "
        f"{len(todo)} tests, {args.repeat} runs each, "
        f"revert {'off' if args.no_revert else 'once per VM' if args.revert_once or not cfg['windows']['revert_each_test'] else 'before every test'}")

    def take() -> Test | None:
        with qlock:
            return queue.pop(0) if queue else None

    def worker(vm: pool.VM) -> None:
        lease = pool.Lease(vm)
        waiting_said = False
        while True:
            with qlock:
                if not queue:
                    return
            if not lease.try_acquire():
                if time.time() > deadline:
                    say(f"windows[{vm}]: gave up waiting for the VM lock ({' + '.join(vm.locks)})")
                    return
                if not waiting_said:
                    say(f"windows[{vm}]: busy (another run holds {' or '.join(vm.locks)}); waiting ...")
                    waiting_said = True
                time.sleep(0.5)
                continue
            waiting_said = False
            target = WindowsTarget(cfg, revert=not args.no_revert, settings=vm.settings, vm_name=vm.name,
                                   revert_each_test=each)
            try:
                with qlock:
                    remaining = list(queue)
                if not remaining:
                    return
                target.prepare(remaining)
                if not meta:
                    meta.update(target.describe())
                while (t := take()) is not None:
                    try:
                        r, diffs = run_repeated(target, t, args.repeat, f"windows[{vm}]")
                    except BaseException:
                        with qlock:
                            queue.insert(0, t)  # let another VM have it
                        raise
                    with qlock:
                        out[t.id] = r
                        if diffs:
                            flaky[t.id] = diffs
                        per_vm[vm.name] = per_vm.get(vm.name, 0) + 1
                    if target.per_test:
                        break  # release the VM between tests (fairness), then take it again
            except BaseException as e:  # SystemExit from SSH/monitor problems included
                say(f"windows[{vm}]: VM failed, leaving it out of this run: {e}")
                return
            finally:
                target.finish()
                with qlock:
                    revert_times.extend(target.revert_times)
                lease.release()
            if target.per_test:
                time.sleep(0.6)  # give a waiting run (polling every 0.5s) a chance to get this VM

    t0 = time.time()
    threads = [threading.Thread(target=worker, args=(vm,), name=f"vm-{vm.name}") for vm in vms]
    for th in threads:
        th.start()
    for th in threads:
        th.join()
    elapsed = time.time() - t0
    if revert_times:
        lv = sum(a for a, _ in revert_times) / len(revert_times)
        ssh = sum(b for _, b in revert_times) / len(revert_times)
        say(f"windows: {len(revert_times)} reverts, average {lv + ssh:.1f}s "
            f"(loadvm {lv:.1f}s + until SSH answers {ssh:.1f}s)")
    say(f"windows: ran {len(out)} tests in {elapsed:.0f}s on "
        f"{', '.join(f'{k}: {v}' for k, v in sorted(per_vm.items())) or 'no VM'}")
    if queue:
        say(f"windows: NOT RUN ({len(queue)}): {', '.join(t.id for t in queue)}")
    return out, flaky, meta, len(queue)


def step_windows(cfg, tests, args) -> int:
    if args.verify:
        return step_verify(cfg, tests, args)
    if args.repeat < 2:
        raise SystemExit("windows: golden files are recorded at least twice (--repeat 2 or more) so that a "
                         "nondeterministic test is caught; use --verify to just check a test")
    res, flaky, meta, not_run = run_windows_pool(cfg, tests, args)
    written = 0
    for tid, r in res.items():
        t = next(t for t in tests if t.id == tid)
        if tid in flaky:
            say(f"  windows: NOT writing golden for {tid}: nondeterministic on Windows (fix the test):")
            for d in flaky[tid]:
                say(f"      ~ {d}")
            continue
        r = dict(r)
        r["source_sha256"] = t.source_sha256()
        p = golden_path(t)
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(results.dump(r))
        written += 1
    if written:
        meta_path = C.GOLDEN_DIR / "_meta.json"
        meta_path.write_text(results.dump({"recorded_on": meta, "recorded_with": "oracle/run.py windows"}))
    say(f"windows: wrote {written} golden files to {C.GOLDEN_DIR} ({meta.get('version', '?')})")
    if flaky:
        say(f"windows: REFUSED {len(flaky)} nondeterministic: {', '.join(sorted(flaky))}")
    return 1 if flaky or not_run else 0


def step_verify(cfg, tests, args) -> int:
    """Re-run tests on Windows and compare with the committed golden files. Writes nothing."""
    res, flaky, meta, not_run = run_windows_pool(cfg, tests, args)
    same, differ, missing = [], [], []
    for tid, r in sorted(res.items()):
        t = next(t for t in tests if t.id == tid)
        g = _load(golden_path(t))
        if g is None:
            missing.append(tid)
            say(f"  verify: {tid:44s} no golden file")
            continue
        stale = g.get("source_sha256") and g["source_sha256"] != t.source_sha256()
        diffs = results.differences(results.comparable(g), results.comparable(r), "golden", "now")
        if tid in flaky:
            differ.append(tid)
            say(f"  verify: {tid:44s} FLAKY (repeated runs differ)")
            for d in flaky[tid]:
                say(f"      ~ {d}")
        elif diffs:
            differ.append(tid)
            say(f"  verify: {tid:44s} DIFFERS from golden{' (golden is STALE: test changed)' if stale else ''}")
            for d in diffs:
                say(f"      ~ {d}")
        else:
            same.append(tid)
            say(f"  verify: {tid:44s} same{' (but golden is STALE: re-record)' if stale else ''}")
    say(f"verify: {len(same)} same, {len(differ)} differ/flaky, {len(missing)} without golden, "
        f"{not_run} not run ({meta.get('version', '?')})")
    if differ:
        say(f"verify: DIFFER: {', '.join(differ)}")
    return 1 if differ or not_run else 0


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
    ap.add_argument("--repeat", type=int, default=None,
                    help="run each test N times and flag nondeterminism (default: 2 for windows, else 1)")
    ap.add_argument("--force-build", action="store_true", help="rebuild even if up to date")
    ap.add_argument("--no-revert", action="store_true", help="windows: don't revert the VM snapshot at all")
    ap.add_argument("--revert-once", action="store_true",
                    help="windows: revert once per VM, not before every test run (faster, less isolated)")
    ap.add_argument("--verify", action="store_true",
                    help="windows: re-run and compare with the committed golden files; write nothing")
    ap.add_argument("--vm", action="append", metavar="NAME",
                    help="windows: use only this VM of the pool (repeatable; default: every running VM)")
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
            args.repeat = max(args.repeat or 1, 2)
        else:
            steps.append(s)
    explicit_repeat = args.repeat

    cfg = C.load(args.config)
    tests = discover(args.tests)
    rc = 0
    for s in steps:
        print(f"== {s} ({len(tests)} tests)")
        if s == "build":
            if build(cfg, tests, force=args.force_build):
                rc = 1
        elif s == "windows":
            args.repeat = explicit_repeat or 2
            rc |= step_windows(cfg, tests, args)
        elif s == "setu":
            args.repeat = explicit_repeat or 1
            rc |= step_setu(cfg, tests, args)
        elif s == "report":
            rc |= step_report(cfg, tests, args)
    return rc


if __name__ == "__main__":
    sys.exit(main())
