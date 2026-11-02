# SPDX-License-Identifier: MIT OR Apache-2.0
"""Execution targets: where a test .exe actually runs.

* WineTarget    - the Setu side: the personality built from personality/wine
                  (tools/build-personality.sh), or stock Wine as a fallback.
* WindowsTarget - a real Windows 11 VM reached over SSH, reverted to a clean
                  snapshot before each run. Produces golden files.
"""
from __future__ import annotations

import os
import shlex
import shutil
import subprocess
import time
from pathlib import Path

from . import hmp
from .config import BUILD_DIR, ORACLE_DIR, Config
from .results import RawRun
from .tests import Test


class Target:
    name = "?"

    def prepare(self, tests: list[Test]) -> None: ...

    def run(self, test: Test) -> RawRun:
        raise NotImplementedError

    def describe(self) -> dict:
        return {}

    def finish(self) -> None: ...


# --------------------------------------------------------------------------- Setu / Wine


PERSONALITY = BUILD_DIR / "personality" / "install"


class WineTarget(Target):
    name = "setu"

    def __init__(self, cfg: Config, fresh_prefix: bool = False):
        c = cfg["setu"]
        self.timeout = cfg["run"]["timeout"]
        impl = c["implementation"]
        if impl not in ("auto", "personality", "stock"):
            raise SystemExit(f"[setu] implementation must be auto, personality or stock (got {impl!r})")
        built = (PERSONALITY / "bin" / "wine").exists()
        if c["wine"]:
            self.impl = "custom"
            self.wine = shlex.split(c["wine"])
            self.wineserver = self._resolve(c["wineserver"], ["wineserver"], None)
        elif impl == "personality" or (impl == "auto" and built):
            if not built:
                raise SystemExit("personality build not found in build/personality/install: "
                                 "run tools/build-personality.sh")
            self.impl = "personality"
            self.wine = [str(PERSONALITY / "bin" / "wine")]
            self.wineserver = [str(PERSONALITY / "bin" / "wineserver")]
        else:
            self.impl = "stock-wine"
            self.wine = self._resolve("", ["wine", "wine64"], "wine")
            self.wineserver = self._resolve(c["wineserver"], ["wineserver"], None)
        self.prefix = Path(c["prefix"]) if c["prefix"] else ORACLE_DIR / ".cache" / f"wineprefix-{self.impl}"
        self.fresh_prefix = fresh_prefix
        self.env = dict(os.environ)
        # Wine copies the Unix environment into the Windows one. Some host
        # tools set this variable, which drops the current directory from
        # CreateProcess's search path; the Windows VM does not have it.
        self.env.pop("NoDefaultCurrentDirectoryInExePath", None)
        self.env.update({
            "WINEPREFIX": str(self.prefix),
            "WINEDEBUG": c["winedebug"],
            # No Mono/Gecko install prompts, no desktop menu entries.
            "WINEDLLOVERRIDES": "mscoree,mshtml=;winemenubuilder.exe=d",
        })
        self.workroot = BUILD_DIR / "work" / "setu"

    @staticmethod
    def _resolve(configured: str, candidates: list[str], required: str | None) -> list[str] | None:
        if configured:
            return shlex.split(configured)
        for c in candidates:
            if shutil.which(c):
                return [c]
        if required:
            raise SystemExit(
                f"'{required}' not found. Install Wine (sudo apt install wine64), run "
                "tools/bootstrap-toolchain.sh, or set [setu] wine in oracle/config.toml / $SETU_WINE."
            )
        return None

    def _wineserver(self, *args: str) -> None:
        if self.wineserver:
            subprocess.run([*self.wineserver, *args], env=self.env, capture_output=True, timeout=120)

    def prepare(self, tests: list[Test]) -> None:
        if self.fresh_prefix and self.prefix.exists():
            self._wineserver("-k")
            shutil.rmtree(self.prefix)
        stamp = self.prefix / ".setu-oracle-initialized"
        if not stamp.exists():
            print(f"setu: initialising {self.impl} prefix {self.prefix} (one-time, ~10-60s)")
            self.prefix.parent.mkdir(parents=True, exist_ok=True)
            r = subprocess.run([*self.wine, "wineboot", "--init"], env=self.env, capture_output=True,
                               timeout=600)
            self._wineserver("-w")
            # 64-bit-only Wine builds exit non-zero because the 32-bit
            # (syswow64) half is missing. That's fine as long as the prefix exists.
            if r.returncode != 0 and (self.prefix / "system.reg").exists():
                print(f"setu: note: wineboot exited {r.returncode} (usually: no 32-bit Wine); prefix is usable")
            elif r.returncode != 0:
                raise SystemExit(f"wineboot failed ({r.returncode}):\n{r.stderr.decode(errors='replace')[-2000:]}")
            stamp.write_text("ok\n")
        if self.workroot.exists():
            shutil.rmtree(self.workroot)
        self.workroot.mkdir(parents=True)

    def run(self, test: Test) -> RawRun:
        work = self.workroot / test.flat
        if work.exists():
            shutil.rmtree(work)
        work.mkdir(parents=True)
        try:
            r = subprocess.run([*self.wine, str(test.exe.resolve())], cwd=work, env=self.env,
                               capture_output=True, timeout=self.timeout)
            return RawRun(r.stdout, r.stderr, r.returncode)
        except subprocess.TimeoutExpired as e:
            self._wineserver("-k")
            return RawRun(e.stdout or b"", e.stderr or b"", None, f"timeout after {self.timeout}s")

    def describe(self) -> dict:
        try:
            v = subprocess.run([*self.wine, "--version"], capture_output=True, text=True, timeout=30).stdout.strip()
        except (OSError, subprocess.SubprocessError):
            v = "unknown"
        if self.impl == "personality":
            stamp = PERSONALITY / "SETU_BUILD"
            tree = ""
            if stamp.exists():
                for line in stamp.read_text().splitlines():
                    if line.startswith("tree="):
                        tree = line[5:]
            if tree:
                v = f"{v} (personality tree {tree[:12]}{'-dirty' if tree.endswith('-dirty') else ''})"
        return {"target": "setu", "implementation": self.impl, "version": v}

    def finish(self) -> None:
        self._wineserver("-w")


# --------------------------------------------------------------------------- Windows VM


class WindowsTarget(Target):
    """Runs tests on a Windows VM over OpenSSH.

    The guest's default SSH shell is cmd.exe, and Win32-OpenSSH runs each command
    as `cmd.exe /c "<command>"`. So commands are plain cmd syntax: don't add
    another `cmd /c` prefix, or the nested cmd sees a stray trailing quote.
    """

    name = "windows"

    def __init__(self, cfg: Config, revert: bool = True):
        c = self.c = cfg["windows"]
        self.timeout = cfg["run"]["timeout"]
        self.revert_enabled = revert and c["revert"] != "none"
        self.dest = f"{c['user']}@{c['host']}"
        self.rdir = c["remote_dir"].rstrip("\\")
        ctl = BUILD_DIR / "ssh-ctl"
        # A Unix socket path is limited to 108 bytes and %C adds 40: deep
        # checkouts (e.g. git worktrees) use a short private directory instead.
        if len(str(ctl)) > 60:
            ctl = Path("/tmp") / f"setu-ssh-{os.getuid()}"
        ctl.mkdir(mode=0o700, parents=True, exist_ok=True)
        self.common = [
            "-o", "BatchMode=yes",
            "-o", "StrictHostKeyChecking=accept-new",
            "-o", f"UserKnownHostsFile={c['known_hosts']}",
            "-o", "ConnectTimeout=10",
            "-o", "ServerAliveInterval=5",
            "-o", "ControlMaster=auto",
            "-o", f"ControlPath={ctl}/%C",
            "-o", "ControlPersist=120",
            "-o", "LogLevel=ERROR",
        ]
        if c["identity"]:
            self.common += ["-i", c["identity"], "-o", "IdentitiesOnly=yes"]

    # -- plumbing
    def ssh(self, command: str, timeout: float | None = None, check: bool = False) -> subprocess.CompletedProcess:
        cmd = ["ssh", *self.common, "-p", str(self.c["port"]), self.dest, command]
        r = subprocess.run(cmd, capture_output=True, timeout=timeout or self.timeout)
        if check and r.returncode != 0:
            raise RuntimeError(f"ssh {command!r} failed ({r.returncode}): {r.stderr.decode(errors='replace')}")
        return r

    def scp(self, files: list[Path], remote: str) -> None:
        cmd = ["scp", *self.common, "-P", str(self.c["port"]), "-q", *map(str, files), f"{self.dest}:{remote}"]
        r = subprocess.run(cmd, capture_output=True, timeout=600)
        if r.returncode != 0:
            raise RuntimeError(f"scp failed: {r.stderr.decode(errors='replace')}")

    def wait_ready(self) -> None:
        deadline = time.time() + self.c["ssh_ready_timeout"]
        last = ""
        while time.time() < deadline:
            try:
                r = self.ssh("echo ready", timeout=15)
                if r.returncode == 0 and b"ready" in r.stdout:
                    return
                last = r.stderr.decode(errors="replace").strip()
            except subprocess.TimeoutExpired:
                last = "timeout"
            time.sleep(3)
        raise SystemExit(f"windows: SSH not reachable at {self.dest}:{self.c['port']} ({last}).\n"
                         "  Is the VM running? Try: oracle/vm/vmctl.sh status")

    def revert(self) -> None:
        mode = self.c["revert"]
        # Close any multiplexed SSH connection first: it won't survive the revert.
        subprocess.run(["ssh", *self.common, "-p", str(self.c["port"]), "-O", "exit", self.dest],
                       capture_output=True)
        if mode == "loadvm":
            if not os.path.exists(self.c["monitor"]):
                raise SystemExit(f"windows: QEMU monitor socket {self.c['monitor']} not found. "
                                 "Start the VM with oracle/vm/vmctl.sh start")
            out = hmp.command(self.c["monitor"], f"loadvm {self.c['snapshot']}")
            if hmp.failed(out):
                raise SystemExit(f"windows: loadvm {self.c['snapshot']} failed: {out}")
            # A restore can leave the VM paused ("restore-vm"); make sure it runs.
            if "running" not in hmp.command(self.c["monitor"], "info status"):
                hmp.command(self.c["monitor"], "cont")
        elif mode == "command":
            subprocess.run(self.c["revert_command"], shell=True, check=True)
        self.wait_ready()

    # -- target API
    def prepare(self, tests: list[Test]) -> None:
        if self.revert_enabled:
            print(f"windows: reverting VM to snapshot '{self.c['snapshot']}' ...")
            self.revert()
        else:
            self.wait_ready()
        rd = self.rdir
        self.ssh(f"if exist {rd} rmdir /s /q {rd}", timeout=120)
        self.ssh(f"mkdir {rd}\\bin && mkdir {rd}\\work", check=True)
        staging = BUILD_DIR / "stage-windows"
        if staging.exists():
            shutil.rmtree(staging)
        staging.mkdir(parents=True)
        files = []
        for t in tests:
            dst = staging / f"{t.flat}.exe"
            shutil.copy2(t.exe, dst)
            files.append(dst)
        print(f"windows: copying {len(files)} test programs to {rd}\\bin")
        self.scp(files, rd.replace("\\", "/") + "/bin/")

    def run(self, test: Test) -> RawRun:
        if self.revert_enabled and self.c["revert_each_test"]:
            self.revert()
            self.prepare_single(test)
        rd, f = self.rdir, test.flat
        w = f"{rd}\\work\\{f}"
        # Fresh, empty working directory for every run (repeats included).
        command = f"(if exist {w} rmdir /s /q {w}) & mkdir {w} && cd /d {w} && {rd}\\bin\\{f}.exe"
        try:
            r = self.ssh(command)
            return RawRun(r.stdout, r.stderr, r.returncode)
        except subprocess.TimeoutExpired as e:
            self.ssh(f"taskkill /f /im {f}.exe", timeout=30)
            return RawRun(e.stdout or b"", e.stderr or b"", None, f"timeout after {self.timeout}s")

    def prepare_single(self, test: Test) -> None:
        rd = self.rdir
        self.ssh(f"if not exist {rd}\\bin mkdir {rd}\\bin", timeout=60)
        self.ssh(f"if not exist {rd}\\work mkdir {rd}\\work", timeout=60)
        dst = BUILD_DIR / "stage-windows" / f"{test.flat}.exe"
        self.scp([dst], rd.replace("\\", "/") + "/bin/")

    def describe(self) -> dict:
        r = self.ssh("ver", timeout=30)
        ver = r.stdout.decode(errors="replace").strip()
        return {"target": "windows", "version": ver}

    def finish(self) -> None:
        subprocess.run(["ssh", *self.common, "-p", str(self.c["port"]), "-O", "exit", self.dest],
                       capture_output=True)
