# SPDX-License-Identifier: MIT OR Apache-2.0
"""Turn a test program's raw JSON Lines output into a normalised result.

Normalised result (this is also the golden-file format):

    {
      "test": "kernel32/CreateFileW",
      "schema": 1,
      "exit_code": 0,
      "complete": true,           # saw the {"end":true} line
      "cases": {"create_new_fresh": {"ret": "valid", "last_error": 0, ...}, ...},
      "errors": []                # harness-level problems (bad JSON, timeout, ...)
    }
"""
from __future__ import annotations

import json
from dataclasses import dataclass


@dataclass
class RawRun:
    stdout: bytes
    stderr: bytes
    exit_code: int | None  # None = timed out / could not run
    note: str = ""


def parse(test_id: str, run: RawRun) -> dict:
    res: dict = {"test": test_id, "schema": None, "exit_code": run.exit_code, "complete": False,
                 "cases": {}, "errors": []}
    if run.note:
        res["errors"].append(run.note)
    text = run.stdout.decode("utf-8", errors="replace").replace("\r\n", "\n")
    for n, line in enumerate(text.split("\n"), 1):
        if not line.strip():
            continue
        try:
            obj = json.loads(line)
        except json.JSONDecodeError:
            res["errors"].append(f"line {n}: not JSON: {line[:120]!r}")
            continue
        if not isinstance(obj, dict):
            res["errors"].append(f"line {n}: not an object")
        elif "oracle" in obj:
            res["schema"] = obj.get("oracle")
            if obj.get("test") != test_id:
                res["errors"].append(f"header test id {obj.get('test')!r} != {test_id!r}")
        elif obj.get("end") is True:
            res["complete"] = True
            if obj.get("cases") != len(res["cases"]):
                res["errors"].append(f"end line says {obj.get('cases')} cases, saw {len(res['cases'])}")
        elif "case" in obj:
            name = obj.pop("case")
            if name in res["cases"]:
                res["errors"].append(f"duplicate case name {name!r}")
            res["cases"][name] = obj
        else:
            res["errors"].append(f"line {n}: unrecognised object")
    if res["schema"] is None:
        res["errors"].append("no oracle header line")
    if not res["complete"]:
        res["errors"].append("output incomplete (no end line): crash, timeout or early exit")
    return res


def comparable(res: dict) -> dict:
    """The part of a result that must match between runs and targets."""
    return {"exit_code": res["exit_code"], "complete": res["complete"], "cases": res["cases"]}


def dump(obj: dict) -> str:
    return json.dumps(obj, indent=2, ensure_ascii=False) + "\n"
