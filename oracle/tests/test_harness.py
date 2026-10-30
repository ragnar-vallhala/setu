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


if __name__ == "__main__":
    unittest.main()
