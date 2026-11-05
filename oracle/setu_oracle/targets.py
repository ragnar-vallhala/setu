# SPDX-License-Identifier: MIT OR Apache-2.0
"""Execution targets: where a test .exe actually runs.

* WineTarget    - the Setu side: the personality built from personality/wine
                  (tools/build-personality.sh), or stock Wine as a fallback.
* WindowsTarget - a real Windows VM of the pool, reached over SSH, reverted to
                  a clean snapshot before each test. Produces golden files.
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

    def start_test(self, test: Test) -> None:
        """Called once before a test's (possibly repeated) runs."""

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
        # Never talk to the host's display server: tests must be headless and
        # must not open windows on the developer's desktop. Window creation
        # goes through Wine's null graphics driver instead (see prepare()).
        for var in ("DISPLAY", "WAYLAND_DISPLAY", "XAUTHORITY"):
            self.env.pop(var, None)
        # The harness's own settings (SETU_IMPL, ...) are not part of the
        # environment a test program sees on Windows; tests that list SETU*
        # variables would otherwise differ between a developer machine and CI.
        for var in [v for v in self.env if v.upper().startswith("SETU")]:
            del self.env[var]
        self.env.update({
            "WINEPREFIX": str(self.prefix),
            "WINEDEBUG": c["winedebug"],
            # No Mono/Gecko install prompts, no desktop menu entries.
            "WINEDLLOVERRIDES": "mscoree,mshtml=;winemenubuilder.exe=d",
        })
        # Same place as on the Windows VM (C:\setu-oracle\work\<test>): the
        # whole of C: is one host filesystem, so the cwd and its drive root
        # are one volume, as on Windows. Z: (the host's /) usually is not.
        self.workroot = self.prefix / "drive_c" / "setu-oracle" / "work"

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
        self._use_null_graphics_driver()
        if self.workroot.exists():
            shutil.rmtree(self.workroot)
        self.workroot.mkdir(parents=True)

    def _use_null_graphics_driver(self) -> None:
        """Select Wine's null graphics driver (HKCU\\Software\\Wine\\Drivers Graphics=null).

        With no display server, Wine would otherwise refuse to create any
        window that is not message-only. The null driver gives user32/gdi32
        tests a working, deterministic, invisible desktop, much like the
        Windows VM's non-interactive SSH session.
        """
        stamp = self.prefix / ".setu-oracle-null-graphics"
        if stamp.exists():
            return
        r = subprocess.run([*self.wine, "reg", "add", "HKCU\\Software\\Wine\\Drivers", "/v", "Graphics",
                            "/t", "REG_SZ", "/d", "null", "/f"], env=self.env, capture_output=True, timeout=300)
        self._wineserver("-w")
        if r.returncode != 0:
            raise SystemExit(f"setu: could not select the null graphics driver ({r.returncode}):\n"
                             f"{r.stderr.decode(errors='replace')[-2000:]}")
        stamp.write_text("ok\n")

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
    """Runs tests on one Windows VM of the pool over OpenSSH.

    The guest's default SSH shell is cmd.exe, and Win32-OpenSSH runs each command
    as `cmd.exe /c "<command>"`. So commands are plain cmd syntax: don't add
    another `cmd /c` prefix, or the nested cmd sees a stray trailing quote.

    With `revert_each_test` (the default), every test starts from the clean
    snapshot: start_test() reverts and copies that one program in. Its
    repeated runs then follow back to back, so the second run also shows
    whether the first one left state behind that changes its output (the
    Setu side repeats tests the same way, without a reset). Otherwise
    prepare() reverts once and copies every program, and all tests share
    the machine.
    """

    name = "windows"

    def __init__(self, cfg: Config, revert: bool = True, settings: dict | None = None,
                 vm_name: str = "setu-oracle", revert_each_test: bool | None = None):
        c = self.c = settings or cfg["windows"]
        self.vm_name = vm_name
        self.timeout = cfg["run"]["timeout"]
        self.revert_enabled = revert and c["revert"] != "none"
        each = c["revert_each_test"] if revert_each_test is None else revert_each_test
        self.per_test = self.revert_enabled and each
        self.dest = f"{c['user']}@{c['host']}"
        self.rdir = c["remote_dir"].rstrip("\\")
        self.staging = BUILD_DIR / f"stage-windows-{vm_name}"
        self.revert_times: list[tuple[float, float]] = []  # (loadvm s, until SSH answers s)
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

    def _close_master(self) -> None:
        subprocess.run(["ssh", *self.common, "-p", str(self.c["port"]), "-O", "exit", self.dest],
                       capture_output=True)

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
            time.sleep(0.5)
        raise SystemExit(f"windows: SSH not reachable at {self.dest}:{self.c['port']} ({last}).\n"
                         "  Is the VM running? Try: oracle/vm/vmctl.sh status")

    def revert(self) -> None:
        mode = self.c["revert"]
        # Close any multiplexed SSH connection first: it won't survive the revert.
        self._close_master()
        t0 = time.time()
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
        t1 = time.time()
        self.wait_ready()
        self._sync_clock()
        self.revert_times.append((t1 - t0, time.time() - t1))

    def _sync_clock(self) -> None:
        """Set the guest clock to the host's UTC time.

        A restored snapshot keeps the time it was saved at, and Windows later
        corrects it in one jump, which can land in the middle of a test (and
        makes 'how long ago' checks differ between two runs). Setting it now,
        before any test program starts, avoids the jump."""
        if self.c["revert"] == "none":
            return
        # PowerShell can take minutes to start on a loaded host. A clock that
        # couldn't be set is only a warning: dropping the VM for it would stop
        # the whole run.
        for timeout in (90, 240):
            now = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
            try:
                r = self.ssh("powershell -NoProfile -NonInteractive -Command "
                             f"\"Set-Date -Date ([DateTime]::Parse('{now}')) | Out-Null\"",
                             timeout=timeout)
            except subprocess.TimeoutExpired:
                print(f"windows: warning: setting the guest clock timed out after {timeout}s")
                continue
            if r.returncode == 0:
                return
            print(f"windows: warning: could not set the guest clock: "
                  f"{r.stderr.decode(errors='replace').strip()}")
            return

    def _upload(self, tests: list[Test]) -> None:
        """Fresh C:\\setu-oracle\\{bin,work} holding the given test programs."""
        rd = self.rdir
        self.ssh(f"if exist {rd} rmdir /s /q {rd}", timeout=120)
        self.ssh(f"mkdir {rd}\\bin && mkdir {rd}\\work", check=True)
        self.scp([self.staging / f"{t.flat}.exe" for t in tests], rd.replace("\\", "/") + "/bin/")

    # -- target API
    def prepare(self, tests: list[Test]) -> None:
        if self.staging.exists():
            shutil.rmtree(self.staging)
        self.staging.mkdir(parents=True)
        for t in tests:
            if t.exe.exists():
                shutil.copy2(t.exe, self.staging / f"{t.flat}.exe")
        if self.per_test:
            return  # run() reverts and uploads per test
        if self.revert_enabled:
            print(f"windows[{self.vm_name}]: reverting VM to snapshot '{self.c['snapshot']}' ...")
            self.revert()
        else:
            self.wait_ready()
        present = [t for t in tests if t.exe.exists()]
        print(f"windows[{self.vm_name}]: copying {len(present)} test programs to {self.rdir}\\bin")
        if present:
            self._upload(present)

    def start_test(self, test: Test) -> None:
        if self.per_test:
            self.revert()
            self._upload([test])

    def run(self, test: Test) -> RawRun:
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

    def describe(self) -> dict:
        r = self.ssh("ver", timeout=30)
        ver = r.stdout.decode(errors="replace").strip()
        return {"target": "windows", "version": ver}

    def finish(self) -> None:
        self._close_master()
