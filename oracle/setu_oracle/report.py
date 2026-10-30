# SPDX-License-Identifier: MIT OR Apache-2.0
"""Write the pass/fail report as Markdown and JSON."""
from __future__ import annotations

import json
from pathlib import Path

from .diff import TestDiff

# Fields whose integer values are Win32 error codes, decorated with names in the report.
_ERROR_FIELDS = ("last_error", "end_error", "ret", "final_ret", "readback_ret", "set_ret")


def _fmt(field: str, v, names: dict[int, str]) -> str:
    if isinstance(v, int) and not isinstance(v, bool) and field.split(".")[-1] in _ERROR_FIELDS \
            and v in names and v < 0x10000:
        return f"`{v}` ({names[v]})"
    return f"`{json.dumps(v, ensure_ascii=False)}`" if not isinstance(v, str) or v != "<absent>" else "_absent_"


def summarize(diffs: list[TestDiff]) -> dict:
    by_status: dict[str, int] = {}
    cases = {"pass": 0, "fail": 0, "missing": 0, "extra": 0}
    for d in diffs:
        by_status[d.status] = by_status.get(d.status, 0) + 1
        for k, v in d.case_counts.items():
            cases[k] += v
    return {"tests": len(diffs), "by_status": by_status, "cases": cases}


def write(out_dir: Path, diffs: list[TestDiff], meta: dict, names: dict[int, str],
          ratchet: dict | None = None) -> tuple[Path, Path]:
    out_dir.mkdir(parents=True, exist_ok=True)
    summary = summarize(diffs)

    data = {
        "summary": summary,
        "meta": meta,
        "ratchet": ratchet or {},
        "tests": [
            {
                "test": d.test,
                "status": d.status,
                "notes": d.notes,
                "case_counts": d.case_counts,
                "cases": [
                    {"case": c.name, "status": c.status,
                     "diffs": [{"field": f, "windows": w, "setu": s} for f, w, s in c.fields]}
                    for c in d.cases
                ],
            }
            for d in diffs
        ],
    }
    jpath = out_dir / "report.json"
    jpath.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n")

    L = ["# Setu oracle report", ""]
    g = meta.get("golden") or {}
    s = meta.get("setu") or {}
    L.append(f"- **Windows (golden):** {g.get('version', 'n/a')}")
    L.append(f"- **Setu:** {s.get('implementation', 'n/a')} {s.get('version', '')}".rstrip())
    c = summary["cases"]
    total_cases = sum(c.values())
    st = summary["by_status"]
    L.append(f"- **Tests:** {summary['tests']} — " + ", ".join(f"{k}: {v}" for k, v in sorted(st.items())))
    if total_cases:
        L.append(f"- **Cases:** {c['pass']}/{total_cases} pass ({100 * c['pass'] / total_cases:.1f}%), "
                 f"{c['fail']} fail, {c['missing']} missing on Setu, {c['extra']} extra on Setu")
    if ratchet:
        L.append(f"- **Ratchet:** {len(ratchet.get('regressions', []))} regressions, "
                 f"{len(ratchet.get('newly_passing', []))} newly passing")
    L += ["", "| Test | Status | Cases pass/total |", "|---|---|---|"]
    icon = {"pass": "✅ pass", "fail": "❌ fail", "error": "💥 error", "no-golden": "⚪ no golden",
            "no-result": "⚪ no result"}
    for d in diffs:
        cc = d.case_counts
        tot = sum(cc.values())
        L.append(f"| `{d.test}` | {icon.get(d.status, d.status)} | {cc['pass']}/{tot} |" if tot
                 else f"| `{d.test}` | {icon.get(d.status, d.status)} | – |")

    if ratchet and ratchet.get("regressions"):
        L += ["", "## Regressions (expected to pass, now failing)", ""]
        L += [f"- `{r}`" for r in ratchet["regressions"]]

    for d in diffs:
        bad = [cd for cd in d.cases if cd.status != "pass"]
        if not bad and not d.notes:
            continue
        L += ["", f"## `{d.test}` — {d.status}", ""]
        L += [f"> {n}" for n in d.notes]
        if d.notes:
            L.append("")
        for cd in bad:
            if cd.status in ("missing", "extra"):
                L.append(f"- **{cd.name}**: case {cd.status} on Setu")
                continue
            L.append(f"- **{cd.name}**")
            for f, w, s in cd.fields:
                L.append(f"  - `{f}`: Windows {_fmt(f, w, names)} · Setu {_fmt(f, s, names)}")
    mpath = out_dir / "report.md"
    mpath.write_text("\n".join(L) + "\n")
    return mpath, jpath
