# SPDX-License-Identifier: MIT OR Apache-2.0
"""Test discovery and cross-compilation."""
from __future__ import annotations

import fnmatch
import hashlib
import json
import re
import shutil
import subprocess
from dataclasses import dataclass
from pathlib import Path

from .config import BUILD_DIR, TESTS_DIR, Config

INCLUDE_DIR = TESTS_DIR / "include"
_LOCAL_INCLUDE = re.compile(r'^\s*#\s*include\s+"([^"]+\.h)"', re.M)
BIN_DIR = BUILD_DIR / "bin"


@dataclass(frozen=True)
class Test:
    id: str  # "<dll>/<api>", e.g. "kernel32/CreateFileW"
    source: Path

    @property
    def exe(self) -> Path:
        return BIN_DIR / f"{self.id}.exe"

    @property
    def flat(self) -> str:
        """File-system friendly id: kernel32__CreateFileW."""
        return self.id.replace("/", "__")

    def local_headers(self) -> list[Path]:
        """Headers next to the tests (outside include/) that this source includes
        with #include "..." , e.g. tests/win32/reg/regtool.h."""
        found = []
        for m in _LOCAL_INCLUDE.finditer(self.source.read_text(errors="replace")):
            p = (self.source.parent / m.group(1)).resolve()
            if p.is_file() and p.parent != INCLUDE_DIR.resolve() and p not in found:
                found.append(p)
        return found

    def source_sha256(self) -> str:
        """Hash of the test source plus the shared header (and any local header it
        includes): detects stale golden files."""
        h = hashlib.sha256()
        h.update(self.source.read_bytes())
        for hdr in sorted(INCLUDE_DIR.glob("*.h")):
            h.update(hdr.read_bytes())
        for hdr in self.local_headers():
            h.update(hdr.read_bytes())
        return h.hexdigest()


def discover(patterns: list[str] | None = None) -> list[Test]:
    tests = []
    for src in sorted(TESTS_DIR.glob("*/*.c")):
        if src.parent.name == "include":
            continue
        tid = f"{src.parent.name}/{src.stem}"
        if patterns and not any(fnmatch.fnmatchcase(tid, p) or tid == p for p in patterns):
            continue
        tests.append(Test(tid, src))
    if patterns and not tests:
        raise SystemExit(f"no tests match {patterns}")
    return tests


def build(cfg: Config, tests: list[Test], force: bool = False) -> list[Test]:
    """Compile tests that are out of date. Returns tests that failed to build."""
    cc = cfg["build"]["cc"]
    if not shutil.which(cc):
        raise SystemExit(
            f"cross compiler '{cc}' not found.\n"
            "  Debian/Ubuntu: sudo apt install gcc-mingw-w64-x86-64\n"
            "  No root:       tools/bootstrap-toolchain.sh (then add ~/.local/setu-toolchain/bin to PATH)"
        )
    headers = list(INCLUDE_DIR.glob("*.h"))
    failed = []
    built = 0
    for t in tests:
        t.exe.parent.mkdir(parents=True, exist_ok=True)
        newest = max([t.source.stat().st_mtime] + [h.stat().st_mtime for h in headers + t.local_headers()])
        if not force and t.exe.exists() and t.exe.stat().st_mtime >= newest:
            continue
        cmd = [cc, *cfg["build"]["cflags"], f"-I{INCLUDE_DIR}", "-o", str(t.exe), str(t.source),
               *cfg["build"]["ldflags"]]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0:
            print(f"  BUILD FAIL {t.id}\n{r.stderr}")
            failed.append(t)
            t.exe.unlink(missing_ok=True)
        else:
            built += 1
    print(f"build: {len(tests) - len(failed)}/{len(tests)} ok ({built} rebuilt) -> {BIN_DIR}")
    return failed


def error_names(cfg: Config) -> dict[int, str]:
    """Map Win32 error numbers to ERROR_* names, using the public winerror.h from
    the mingw-w64 headers (a permitted source, see docs/CLEAN_ROOM.md). Cached."""
    cache = BUILD_DIR / "winerror.json"
    if cache.exists():
        return {int(k): v for k, v in json.loads(cache.read_text()).items()}
    names: dict[int, str] = {}
    try:
        # Locate winerror.h through the compiler's own include path.
        r = subprocess.run([cfg["build"]["cc"], "-M", "-x", "c", "-"], input="#include <winerror.h>\n",
                           capture_output=True, text=True, timeout=60)
        hdr = next((w for w in r.stdout.replace("\\\n", " ").split() if w.endswith("winerror.h")), None)
        if not hdr:
            return {}
        pat = re.compile(r"^#define\s+((?:ERROR|WAIT)_\w+|NO_ERROR)\s+(?:__MSABI_LONG\()?(0x[0-9A-Fa-f]+|\d+)L?\)?\s*$")
        for line in Path(hdr).read_text(errors="replace").splitlines():
            m = pat.match(line.strip())
            if m and "SEVERITY" not in m.group(1):
                # First definition in file order is the canonical name (ERROR_SUCCESS, not NO_ERROR).
                names.setdefault(int(m.group(2), 0), m.group(1))
    except (OSError, subprocess.SubprocessError):
        return {}
    if names:
        BUILD_DIR.mkdir(exist_ok=True)
        cache.write_text(json.dumps(names, indent=0, sort_keys=True))
    return names
