# SPDX-License-Identifier: MIT OR Apache-2.0
"""Unit tests for the harness itself (parsing, diffing, reporting).

All data here is synthetic and only exercises the harness logic. It is NOT
Windows behaviour. Run: python3 -m unittest discover -s oracle/tests
"""
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from setu_oracle import diff, report, results  # noqa: E402
from setu_oracle.results import RawRun  # noqa: E402

GOOD = (b'{"oracle":1,"test":"x/y"}\n'
        b'{"case":"a","ret":true,"last_error":0}\n'
        b'{"case":"b","ret":"INVALID_HANDLE_VALUE","last_error":2}\n'
        b'{"end":true,"cases":2}\n')


class ParseTests(unittest.TestCase):
    def test_complete(self):
        r = results.parse("x/y", RawRun(GOOD, b"", 0))
        self.assertTrue(r["complete"])
        self.assertEqual(r["errors"], [])
        self.assertEqual(list(r["cases"]), ["a", "b"])
        self.assertEqual(r["cases"]["b"], {"ret": "INVALID_HANDLE_VALUE", "last_error": 2})

    def test_crlf_and_truncated(self):
        raw = GOOD.replace(b"\n", b"\r\n").rsplit(b"{\"end", 1)[0]
        r = results.parse("x/y", RawRun(raw, b"", 3221225477))
        self.assertFalse(r["complete"])
        self.assertEqual(len(r["cases"]), 2)
        self.assertTrue(any("incomplete" in e for e in r["errors"]))

    def test_garbage_and_duplicates(self):
        raw = GOOD.replace(b'"case":"b"', b'"case":"a"') + b"fixme: noise\n"
        r = results.parse("x/y", RawRun(raw, b"", 0))
        self.assertTrue(any("duplicate" in e for e in r["errors"]))
        self.assertTrue(any("not JSON" in e for e in r["errors"]))

    def test_wrong_header(self):
        r = results.parse("other/test", RawRun(GOOD, b"", 0))
        self.assertTrue(any("header test id" in e for e in r["errors"]))


class DiffTests(unittest.TestCase):
    def setUp(self):
        self.golden = results.parse("x/y", RawRun(GOOD, b"", 0))

    def test_identical_passes(self):
        d = diff.compare("x/y", self.golden, results.parse("x/y", RawRun(GOOD, b"", 0)))
        self.assertEqual(d.status, diff.PASS)
        self.assertEqual(d.case_counts["pass"], 2)

    def test_field_mismatch(self):
        setu = results.parse("x/y", RawRun(GOOD.replace(b'"last_error":2', b'"last_error":3'), b"", 0))
        d = diff.compare("x/y", self.golden, setu)
        self.assertEqual(d.status, diff.FAIL)
        bad = [c for c in d.cases if c.status == "fail"]
        self.assertEqual(bad[0].fields, [("last_error", 2, 3)])

    def test_missing_extra_field_and_case(self):
        raw = (b'{"oracle":1,"test":"x/y"}\n{"case":"a","ret":true,"last_error":0,"more":1}\n'
               b'{"case":"c","ret":1}\n{"end":true,"cases":2}\n')
        d = diff.compare("x/y", self.golden, results.parse("x/y", RawRun(raw, b"", 0)))
        st = {c.name: c.status for c in d.cases}
        self.assertEqual(st, {"a": "fail", "b": "missing", "c": "extra"})
        self.assertEqual(d.cases[0].fields, [("more", "<absent>", 1)])

    def test_crash_is_error(self):
        setu = results.parse("x/y", RawRun(GOOD.split(b"\n")[0] + b"\n", b"", None, "timeout after 60s"))
        self.assertEqual(diff.compare("x/y", self.golden, setu).status, diff.ERROR)

    def test_no_golden(self):
        self.assertEqual(diff.compare("x/y", None, self.golden).status, diff.NO_GOLDEN)

    def test_exit_code_mismatch_fails(self):
        setu = results.parse("x/y", RawRun(GOOD, b"", 1))
        self.assertEqual(diff.compare("x/y", self.golden, setu).status, diff.FAIL)


class ReportTests(unittest.TestCase):
    def test_writes_md_and_json(self):
        g = results.parse("x/y", RawRun(GOOD, b"", 0))
        s = results.parse("x/y", RawRun(GOOD.replace(b'"last_error":2', b'"last_error":3'), b"", 0))
        d = diff.compare("x/y", g, s)
        with tempfile.TemporaryDirectory() as tmp:
            md, js = report.write(Path(tmp), [d], {}, {2: "ERROR_FILE_NOT_FOUND", 3: "ERROR_PATH_NOT_FOUND"},
                                  {"regressions": ["x/y:b"], "newly_passing": []})
            text = md.read_text()
            self.assertIn("ERROR_FILE_NOT_FOUND", text)
            self.assertIn("Regressions", text)
            data = json.loads(js.read_text())
            self.assertEqual(data["summary"]["cases"], {"pass": 1, "fail": 1, "missing": 0, "extra": 0})


class DifferencesTests(unittest.TestCase):
    def test_same_and_field_and_case(self):
        a = results.comparable(results.parse("x/y", RawRun(GOOD, b"", 0)))
        self.assertEqual(results.differences(a, a), [])
        b = results.comparable(results.parse("x/y", RawRun(
            GOOD.replace(b'"last_error":2', b'"last_error":3').replace(b'{"case":"a","ret":true,"last_error":0}\n', b"")
            .replace(b'"cases":2', b'"cases":1'), b"", 1)))
        d = results.differences(a, b)
        self.assertIn("exit_code: run 1 0 · run 2 1", d)
        self.assertIn("case a: only in run 1", d)
        self.assertIn("case b: last_error: run 1 2 · run 2 3", d)

    def test_limit(self):
        a = {"exit_code": 0, "complete": True, "cases": {f"c{i}": {"v": 1} for i in range(30)}}
        b = {"exit_code": 0, "complete": True, "cases": {f"c{i}": {"v": 2} for i in range(30)}}
        d = results.differences(a, b, limit=5)
        self.assertEqual(len(d), 6)
        self.assertIn("25 more", d[-1])


class PoolTests(unittest.TestCase):
    def cfg(self, user):
        from setu_oracle import config as C
        data = C._merge(C.DEFAULTS, user)
        return C.Config(data, None, user)

    def test_explicit_vms_and_locks(self):
        from setu_oracle import pool
        with tempfile.TemporaryDirectory() as tmp:
            user = {"windows": {"vms": [
                {"name": "a", "port": 2222, "monitor": f"{tmp}/a/monitor.sock"},
                {"name": "b", "port": 2223, "monitor": f"{tmp}/b/monitor.sock", "lock": f"{tmp}/b.lock",
                 "extra_locks": [f"{tmp}/legacy.lock"]},
            ]}}
            vms = pool.discover(self.cfg(user))
            self.assertEqual([v.name for v in vms], ["a", "b"])
            self.assertEqual(vms[1].settings["port"], 2223)
            self.assertEqual(vms[0].locks, [f"{tmp}/a/oracle.lock"])
            self.assertEqual(vms[1].locks, [f"{tmp}/b.lock", f"{tmp}/legacy.lock"])
            self.assertEqual([v.name for v in pool.discover(self.cfg(user), ["b"])], ["b"])
            with self.assertRaises(SystemExit):
                pool.discover(self.cfg(user), ["c"])

    def test_lease_is_exclusive_and_all_or_nothing(self):
        import fcntl
        import os
        from setu_oracle import pool
        with tempfile.TemporaryDirectory() as tmp:
            vm = pool.VM("a", {}, [f"{tmp}/own.lock", f"{tmp}/legacy.lock"])
            # Someone else (like `flock legacy.lock ...`) holds the legacy lock.
            fd = os.open(f"{tmp}/legacy.lock", os.O_RDONLY | os.O_CREAT)
            fcntl.flock(fd, fcntl.LOCK_EX)
            first = pool.Lease(vm)
            self.assertFalse(first.try_acquire())
            self.assertFalse(first.held)  # did not keep own.lock either
            os.close(fd)
            self.assertTrue(first.try_acquire())
            self.assertFalse(pool.Lease(vm).try_acquire())
            first.release()
            second = pool.Lease(vm)
            self.assertTrue(second.try_acquire())
            second.release()


class PoolSchedulerTests(unittest.TestCase):
    """run_windows_pool with fake VMs: spreads tests, skips busy VMs, flags flakes."""

    def test_spread_busy_and_flaky(self):
        import argparse
        import fcntl
        import os
        import threading
        from setu_oracle import cli, pool
        from setu_oracle.tests import Test

        with tempfile.TemporaryDirectory() as tmp:
            t = Path(tmp)
            tests = []
            for i in range(6):
                src = t / f"t{i}.c"
                src.write_text("")
                tests.append(Test(f"x/t{i}", src))
            ran: dict[str, list[str]] = {}
            calls: dict[str, int] = {}
            lk = threading.Lock()

            class FakeTarget:
                def __init__(self, cfg, revert, settings, vm_name, revert_each_test):
                    self.vm_name, self.per_test, self.revert_times = vm_name, True, [(0.0, 0.0)]

                def prepare(self, tests):
                    pass

                def describe(self):
                    return {"version": "fake"}

                def start_test(self, test):
                    pass

                def run(self, test):
                    with lk:
                        ran.setdefault(self.vm_name, []).append(test.id)
                        calls[test.id] = calls.get(test.id, 0) + 1
                        n = calls[test.id]
                    case = b'"v":%d' % n if test.id == "x/t3" else b'"v":0'
                    return RawRun(b'{"oracle":1,"test":"' + test.id.encode() + b'"}\n{"case":"a",' + case +
                                  b'}\n{"end":true,"cases":1}\n', b"", 0)

                def finish(self):
                    pass

            vms = [pool.VM(n, {"port": p}, [str(t / f"{n}.lock")]) for n, p in (("a", 1), ("b", 2), ("c", 3))]
            # VM c is busy the whole time (someone else's run).
            fd = os.open(str(t / "c.lock"), os.O_RDONLY | os.O_CREAT)
            fcntl.flock(fd, fcntl.LOCK_EX)
            cfg = PoolTests.cfg(None, {"windows": {"lock_wait": 3}})
            args = argparse.Namespace(vm=None, revert_once=False, no_revert=False, repeat=2)
            old_t, old_d, old_exists = cli.WindowsTarget, pool.discover, Test.exe
            try:
                cli.WindowsTarget = FakeTarget
                pool.discover = lambda cfg, only=None: vms
                Test.exe = property(lambda self: self.source)  # "built"
                out, flaky, meta, not_run = cli.run_windows_pool(cfg, tests, args)
            finally:
                cli.WindowsTarget, pool.discover, Test.exe = old_t, old_d, old_exists
                os.close(fd)
            self.assertEqual(sorted(out), [x.id for x in tests])
            self.assertEqual(not_run, 0)
            self.assertNotIn("c", ran)
            self.assertEqual(set(ran), {"a", "b"})
            self.assertEqual(list(flaky), ["x/t3"])
            self.assertIn("case a: v: run 1 1 · run 2 2", flaky["x/t3"])
            self.assertEqual(meta, {"version": "fake"})


if __name__ == "__main__":
    unittest.main()
