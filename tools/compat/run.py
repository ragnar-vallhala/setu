#!/usr/bin/env python3
# SPDX-License-Identifier: MIT OR Apache-2.0
"""Setu app compatibility runner.

For each app in tools/compat/apps.toml, in a fresh Setu prefix under its own
Xvfb (never the host display):

  download   fetch the installer into build/compat/cache, check its sha256
  prefix     create a fresh prefix (wineboot), one virtual desktop
  install    the vendor's documented silent install, with a timeout
  files      the files it should create exist
  registry   an Uninstall registry key with the expected DisplayName exists
  shortcut   a Start Menu .lnk exists
  launch     start the installed program; a visible top-level window that
             matches the manifest's pattern appears (compat-probe windows)
  screenshot a small Xvfb thumbnail of the desktop
  alive      the program is still running N seconds later
  close      WM_CLOSE to its windows; all its processes exit
  uninstall  (--uninstall) the silent uninstall removes the key and the files

Writes build/compat/results.json, build/compat/index.html (self-contained),
logs in build/compat/logs/<app>/ (WINEDEBUG=err+all, trimmed) and
thumbnails in build/compat/thumbs/. Prefixes are deleted afterwards.

Usage: tools/compat/run.py [-a NAME]... [--small | --large] [-j 1|2]
                           [--uninstall] [--alive N] [--keep]
                           [--html-only] [--update-docs] [--pin]
Environment: SETU_WINE (wine binary; default: this checkout's build, then
the main checkout's).
"""

import argparse
import base64
import concurrent.futures
import datetime
import hashlib
import html
import json
import os
import re
import shutil
import subprocess
import sys
import threading
import time
import tomllib
import zipfile

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HERE = os.path.join(REPO, "tools", "compat")
OUT = os.path.join(REPO, "build", "compat")
CACHE = os.path.join(OUT, "cache")
LOGS = os.path.join(OUT, "logs")
THUMBS = os.path.join(OUT, "thumbs")
PREFIXES = os.path.join(OUT, "prefix")
PROBE_SRC = os.path.join(HERE, "compat-probe.c")
PROBE = os.path.join(OUT, "bin", "compat-probe.exe")
RESULTS = os.path.join(OUT, "results.json")
DOC = os.path.join(REPO, "docs", "COMPAT.md")
RES = "1280x800"
HOSTNAME = "setu"
STAGES = ["download", "prefix", "install", "files", "registry", "shortcut",
          "launch", "screenshot", "alive", "close", "uninstall"]
# processes that belong to the prefix's session, not to the app under test
SYSTEM_EXES = {
    "explorer.exe", "services.exe", "winedevice.exe", "plugplay.exe", "svchost.exe",
    "rpcss.exe", "compat-probe.exe", "conhost.exe", "wineboot.exe", "tabtip.exe",
    "msiexec.exe", "start.exe", "rundll32.exe", "winemenubuilder.exe", "mscorsvw.exe",
    "cmd.exe", "reg.exe", "iexplore.exe", "spoolsv.exe", "dllhost.exe", "taskhostw.exe",
    "system", "[system process]",  # Toolhelp pseudo-processes
}
LOG_LINES = 400


# --------------------------------------------------------------------------
# environment

def reexec_in_namespace():
    """Run with host name "setu" (a private UTS namespace through user
    namespaces, as tools/shell-smoke.sh does), so the computer name Wine
    reports, the logs and the thumbnails never show the build machine's."""
    stage = os.environ.get("SETU_COMPAT_NS", "")
    if stage == "" and os.environ.get("SETU_COMPAT_NO_NS") != "1":
        if subprocess.run(["unshare", "-Ur", "--uts", "true"], capture_output=True).returncode == 0:
            env = dict(os.environ, SETU_COMPAT_NS="1", SETU_COMPAT_UID=str(os.getuid()),
                       SETU_COMPAT_GID=str(os.getgid()))
            os.execvpe("unshare", ["unshare", "-Ur", "--uts", sys.executable, *sys.argv], env)
        print("warning: no user + UTS namespace; the host name will show", file=sys.stderr)
    elif stage == "1":
        subprocess.run(["hostname", HOSTNAME], check=True)
        env = dict(os.environ, SETU_COMPAT_NS="2")
        os.execvpe("unshare", ["unshare", "-U", "--map-user=" + os.environ["SETU_COMPAT_UID"],
                               "--map-group=" + os.environ["SETU_COMPAT_GID"],
                               sys.executable, *sys.argv], env)


def find_wine():
    wine = os.environ.get("SETU_WINE") or os.path.join(REPO, "build/personality/install/bin/wine")
    if not os.access(wine, os.X_OK):
        try:
            common = subprocess.run(["git", "-C", REPO, "rev-parse", "--path-format=absolute",
                                     "--git-common-dir"], capture_output=True, text=True).stdout.strip()
            wine = os.path.join(os.path.dirname(common), "build/personality/install/bin/wine")
        except OSError:
            pass
    if not os.access(wine, os.X_OK):
        sys.exit("no personality build found; set SETU_WINE or run tools/build-personality.sh")
    return wine


def build_probe():
    if os.path.exists(PROBE) and os.path.getmtime(PROBE) >= os.path.getmtime(PROBE_SRC):
        return
    os.makedirs(os.path.dirname(PROBE), exist_ok=True)
    path = os.path.expanduser("~/.local/setu-toolchain/bin") + os.pathsep + os.environ.get("PATH", "")
    subprocess.run(["x86_64-w64-mingw32-gcc", "-O1", "-Wall", "-Wextra", "-Werror", "-municode",
                    "-o", PROBE, PROBE_SRC, "-luser32", "-ladvapi32"],
                   check=True, env=dict(os.environ, PATH=path))


def setu_commit():
    def git(*a):
        r = subprocess.run(["git", "-C", REPO, *a], capture_output=True, text=True)
        return r.stdout.strip()
    commit = git("rev-parse", "--short=11", "HEAD")
    dirty = git("status", "--porcelain", "--untracked-files=no")
    return commit + ("-dirty" if dirty else "")


def wine_build(wine):
    info = os.path.join(os.path.dirname(os.path.dirname(wine)), "SETU_BUILD")
    try:
        kv = dict(l.split("=", 1) for l in open(info).read().split() if "=" in l)
        return "%s (tree %s)" % (kv.get("version", "?"), kv.get("tree", "?")[:11])
    except OSError:
        return "unknown"


# --------------------------------------------------------------------------
# helpers

def winpath(prefix, p):
    """C:\\... (or Z:\\...) -> the Unix path in this prefix, matching case-insensitively."""
    if p[:2].upper() == "Z:":
        return p[2:].replace("\\", "/") or "/"
    assert p[:2].upper() == "C:", p
    cur = os.path.join(prefix, "drive_c")
    for part in [x for x in p[2:].split("\\") if x]:
        nxt = os.path.join(cur, part)
        if not os.path.exists(nxt) and os.path.isdir(cur):
            for e in os.listdir(cur):
                if e.lower() == part.lower():
                    nxt = os.path.join(cur, e)
                    break
        cur = nxt
    return cur


def zpath(unix):
    return "Z:" + unix.replace("/", "\\")


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def cache_name(app):
    return "%s-%s%s" % (app["name"], app["version"], {
        "msi": ".msi", "zip": ".zip"}.get(app["kind"], ".exe"))


def trim_log(path):
    """Collapse repeated lines and keep the head and tail of a long log."""
    try:
        lines = open(path, encoding="utf-8", errors="replace").read().splitlines()
    except OSError:
        return
    out, last, n = [], None, 0
    for l in lines + [None]:
        if l == last:
            n += 1
            continue
        if last is not None:
            out.append(last if n == 1 else "%s   [x%d]" % (last, n))
        last, n = l, 1
    if len(out) > LOG_LINES:
        cut = len(out) - LOG_LINES
        out = out[:LOG_LINES // 2] + ["... %d lines trimmed ..." % cut] + out[-LOG_LINES // 2:]
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(out) + "\n")


# What is printed in a fresh prefix whatever the app: not evidence of a failure.
BENIGN = re.compile(r"err:(winediag|systray|ole:start_rpcss|vulkan|wgl|d3d|dxgi|xrandr|x11drv:x11drv_init_display"
                    r"|menubuilder|wineusb|hid|setupapi:do_file_copyW|winedevice|plugplay|ntoskrnl"
                    r"|explorer|clipboard|wineboot:process_run_key.*winemenubuilder)")
EVIDENCE = [
    (re.compile(r"Call from \S+ to unimplemented function (\S+)"), "unimplemented function {0}"),
    (re.compile(r"err:module:import_dll Library (\S+) \(which is needed by (\S+)\) not found"), "{0} not found (needed by {1})"),
    (re.compile(r"err:module:import_dll Loading library (\S+) \(which is needed by (\S+)\) failed"), "loading {0} failed (needed by {1})"),
    (re.compile(r"err:module:LdrInitializeThunk Importing dlls for (\S+) failed"), "importing dlls for {0} failed"),
    (re.compile(r"(Unhandled exception: .*)"), "{0}"),
    (re.compile(r"(wine: Unhandled (?:page fault|exception).*)"), "{0}"),
    (re.compile(r"err:mscoree:\S+ (.*)"), ".NET (mscoree): {0}"),
]


def first_evidence(path):
    """The first line in a log that points at a failing API, or ""."""
    try:
        lines = open(path, encoding="utf-8", errors="replace").read().splitlines()
    except OSError:
        return ""
    for rx, fmt in EVIDENCE:
        for l in lines:
            m = rx.search(l)
            if m:
                return fmt.format(*m.groups())
    for l in lines:
        m = re.search(r"\b(err:\S+ .*)", l)
        if m and not BENIGN.search(l):
            return m.group(1)[:200]
    return ""


# --------------------------------------------------------------------------
# one app

class Stage(Exception):
    pass


class AppRun:
    def __init__(self, app, args, wine, runtimes):
        self.app, self.args, self.wine, self.runtimes = app, args, wine, runtimes
        self.wineserver = os.path.join(os.path.dirname(wine), "wineserver")
        self.name = app["name"]
        self.prefix = os.path.join(PREFIXES, re.sub(r"[^A-Za-z0-9_.-]", "_", self.name))
        self.logdir = os.path.join(LOGS, re.sub(r"[^A-Za-z0-9_.-]", "_", self.name))
        self.stages = {}
        self.xvfb = None
        self.display = None
        self.windows_seen = []
        self.launched = None

    # -- infrastructure
    def log(self, msg):
        print("[%s] %s" % (self.name, msg), flush=True)

    def env(self):
        e = {k: v for k, v in os.environ.items()
             if k not in ("DISPLAY", "WAYLAND_DISPLAY", "XAUTHORITY", "WINEDEBUG", "WINEPREFIX")}
        # HOME and XDG dirs inside the prefix: Wine links Documents, Desktop...
        # and the Recycle Bin to them, so nothing reaches the real home
        home = os.path.join(self.prefix, "home")
        e.update(HOME=home, XDG_DATA_HOME=home + "/.local/share", XDG_CONFIG_HOME=home + "/.config",
                 XDG_CACHE_HOME=home + "/.cache")
        # without wine-mono in the prefix, mscoree stays off: no download prompt
        off = "mshtml" if "wine-mono" in self.app.get("runtimes", []) else "mscoree,mshtml"
        e.update(WINEPREFIX=self.prefix, DISPLAY=self.display, WINEDEBUG="err+all",
                 WINEDLLOVERRIDES=off + "=;winemenubuilder.exe=d",
                 TZ="UTC", LANG="en_US.UTF-8", LC_ALL="en_US.UTF-8")
        return e

    def wine_run(self, argv, log, timeout, cwd=None):
        """Run a wine command, its output into the stage log; return exit code or None on timeout."""
        with open(os.path.join(self.logdir, log), "ab") as f:
            p = subprocess.Popen([self.wine, *argv], env=self.env(), stdout=f, stderr=subprocess.STDOUT,
                                 stdin=subprocess.DEVNULL, cwd=cwd or OUT)
            try:
                return p.wait(timeout)
            except subprocess.TimeoutExpired:
                p.kill()
                p.wait()
                return None

    def probe(self, *argv):
        r = subprocess.run([self.wine, PROBE, *argv], env=dict(self.env(), WINEDEBUG="-all"),
                           capture_output=True, timeout=60, stdin=subprocess.DEVNULL)
        return r.stdout.decode("utf-8", "replace").replace("\r", "")

    def procs(self):
        out = []
        for l in self.probe("procs").splitlines():
            if l.startswith("proc="):
                pid, ppid, exe = l[5:].split("|", 2)
                out.append((int(pid), int(ppid), exe))
        return out

    def app_pids(self):
        return {pid for pid, _, exe in self.procs() if exe.lower() not in SYSTEM_EXES}

    def windows(self):
        out = []
        for l in self.probe("windows").splitlines():
            if not l.startswith("window="):
                continue
            f = l[7:].split("|")
            if len(f) < 7:
                continue
            hwnd, pid, exe, cls, title, rect, state = f[:7]
            r = [int(x) for x in rect.split(",")]
            out.append(dict(hwnd=hwnd, pid=int(pid), exe=exe, cls=cls, title=title, rect=r,
                            visible=state.startswith("visible") and "minimized" not in state
                            and r[2] > r[0] and r[3] > r[1]))
        return out

    def app_windows(self):
        return [w for w in self.windows() if w["visible"] and w["exe"].lower() not in SYSTEM_EXES]

    def wait_idle(self, timeout):
        """Wait until no app process is left in the prefix."""
        end = time.time() + timeout
        while time.time() < end:
            if not self.app_pids():
                return True
            time.sleep(1)
        return False

    def start_xvfb(self):
        r, w = os.pipe()
        self.xvfb = subprocess.Popen(["Xvfb", "-displayfd", str(w), "-screen", "0", RES + "x24",
                                      "-nolisten", "tcp"],
                                     pass_fds=(w,), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        os.close(w)
        num = b""
        while not num.endswith(b"\n"):
            c = os.read(r, 1)
            if not c:
                raise RuntimeError("Xvfb did not start")
            num += c
        os.close(r)
        self.display = ":" + num.decode().strip()

    def stop(self):
        subprocess.run([self.wineserver, "-k"], env=self.env(), capture_output=True)
        subprocess.run([self.wineserver, "-w"], env=self.env(), capture_output=True, timeout=60)
        if self.xvfb:
            self.xvfb.terminate()
            try:
                self.xvfb.wait(10)
            except subprocess.TimeoutExpired:
                self.xvfb.kill()

    def stage(self, name, fn):
        if any(s["status"] == "fail" for s in self.stages.values()) and name not in ("uninstall",):
            self.stages[name] = dict(status="skip", seconds=0, reason="an earlier stage failed")
            return False
        t = time.time()
        try:
            res = fn()
            status, reason = ("pass", "") if res is None else res
        except Stage as e:
            status, reason = "fail", str(e)
        except Exception as e:  # a bug in the runner, not in Setu
            status, reason = "fail", "runner error: %s: %s" % (type(e).__name__, e)
        self.stages[name] = dict(status=status, seconds=round(time.time() - t, 1), reason=reason)
        self.log("%-10s %s %s" % (name, status, reason))
        return status == "pass"

    # -- stages
    @staticmethod
    def fetch(url, dest):
        if not os.path.exists(dest):
            part = dest + ".part"
            r = subprocess.run(["curl", "-sfL", "--retry", "2", "--connect-timeout", "30", "-m", "3600", "-o", part, url])
            if r.returncode:
                raise Stage("download of %s failed (curl exit %d)" % (url, r.returncode))
            os.replace(part, dest)

    def s_download(self):
        a = self.app
        os.makedirs(CACHE, exist_ok=True)
        for name in a.get("runtimes", []):
            rt = self.runtimes[name]
            self.fetch(rt["url"], os.path.join(CACHE, rt["file"]))
            if sha256(os.path.join(CACHE, rt["file"])) != rt["sha256"]:
                raise Stage("sha256 mismatch for runtime %s" % name)
        self.installer = os.path.join(CACHE, cache_name(a))
        self.fetch(a["url"], self.installer)
        got = sha256(self.installer)
        if not a.get("sha256"):
            if not self.args.pin:
                raise Stage("no sha256 pinned in apps.toml (got %s; run with --pin to accept it)" % got)
            self.pinned = got
            return ("pass", "pinned %s" % got)
        if got != a["sha256"]:
            raise Stage("sha256 mismatch: %s, expected %s" % (got, a["sha256"]))

    def s_prefix(self):
        shutil.rmtree(self.prefix, ignore_errors=True)
        os.makedirs(os.path.join(self.prefix, "home"))
        if self.app.get("runtimes"):
            # wineboot installs add-ons it finds in its download cache
            wcache = os.path.join(self.prefix, "home", ".cache", "wine")
            os.makedirs(wcache)
            for name in self.app["runtimes"]:
                f = self.runtimes[name]["file"]
                os.symlink(os.path.join(CACHE, f), os.path.join(wcache, f))
        rc = self.wine_run(["wineboot", "--init"], "prefix.log", 300)
        subprocess.run([self.wineserver, "-w"], env=self.env(), timeout=300)
        if not os.path.exists(os.path.join(self.prefix, "system.reg")):
            raise Stage("wineboot --init failed (exit %s)" % rc)
        if "wine-mono" in self.app.get("runtimes", []) and \
                not os.path.isdir(os.path.join(self.prefix, "drive_c", "windows", "mono")):
            raise Stage("wineboot did not install wine-mono%s" % self.evidence("prefix.log"))
        for argv in (["reg", "add", r"HKCU\Software\Wine\Explorer", "/v", "Desktop", "/d", "setu", "/f"],
                     ["reg", "add", r"HKCU\Software\Wine\Explorer\Desktops", "/v", "setu", "/d", RES, "/f"]):
            self.wine_run(argv, "prefix.log", 60)
        d = os.path.join(self.prefix, "drive_c", "setu-compat")
        os.makedirs(d, exist_ok=True)
        with open(os.path.join(d, "sample.txt"), "w", newline="\r\n") as f:
            f.write("Setu compatibility check.\nThis file was written by tools/compat/run.py.\n")
        with open(os.path.join(d, "sample.svg"), "w") as f:
            f.write('<svg xmlns="http://www.w3.org/2000/svg" width="320" height="200">'
                    '<rect width="320" height="200" fill="#2a6f97"/>'
                    '<circle cx="160" cy="100" r="60" fill="#f4d35e"/></svg>\n')
        with open(os.path.join(d, "sample.html"), "w") as f:
            f.write("<!doctype html><title>Setu compat</title><h1>Setu compatibility check</h1>\n")
        subprocess.run([self.wineserver, "-w"], env=self.env(), timeout=300)

    def s_install(self):
        res = self.install()
        for path, text in self.app.get("write", {}).items():
            dest = winpath(self.prefix, path)
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            with open(dest, "w", newline="\r\n") as f:
                f.write(text.lstrip("\n"))
        return res

    def install(self):
        a = self.app
        kind = a["kind"]
        timeout = a.get("install_timeout", 300)
        if kind in ("exe", "zip"):
            dest = winpath(self.prefix, a["install_dir"])
            os.makedirs(dest, exist_ok=True)
            if kind == "exe":
                shutil.copy(self.installer, os.path.join(dest, os.path.basename(a["url"])))
            else:
                with zipfile.ZipFile(self.installer) as z:
                    z.extractall(dest)
            return
        if kind == "msi":
            argv = ["msiexec", "/i", zpath(self.installer), *a["install_args"],
                    "/l*", r"C:\setu-compat\msiexec.log"]
        else:
            argv = [zpath(self.installer), *a["install_args"]]
        rc = self.wine_run(argv, "install.log", timeout)
        if rc is None:
            raise Stage("timed out after %ds%s" % (timeout, self.evidence("install.log")))
        # installers may leave a child running for a moment (NSIS, Inno)
        self.wait_idle(60)
        msilog = os.path.join(self.prefix, "drive_c", "setu-compat", "msiexec.log")
        if os.path.exists(msilog):
            raw = open(msilog, "rb").read()
            txt = raw.decode("utf-16") if raw[:2] == b"\xff\xfe" else raw.decode("utf-8", "replace")
            with open(os.path.join(self.logdir, "msiexec.log"), "w", encoding="utf-8") as f:
                f.write(txt.replace("\r", ""))
            trim_log(os.path.join(self.logdir, "msiexec.log"))
        if rc == 3010:
            return ("pass", "exit 3010 (reboot required)")
        if rc != 0:
            raise Stage("installer exit code %d%s" % (rc, self.evidence("install.log")))

    def s_files(self):
        missing = [p for p in self.app.get("files", []) + [self.app["launch"]["exe"]]
                   if not os.path.isfile(winpath(self.prefix, p))]
        if missing:
            raise Stage("missing: " + ", ".join(sorted(set(missing))))

    def uninstall_entries(self):
        rx = re.compile(self.app["uninstall_name"])
        out = []
        for l in self.probe("uninstall").splitlines():
            if l.startswith("uninstall="):
                f = l[10:].split("|")
                if len(f) >= 6 and rx.search(f[3]):
                    out.append(dict(root=f[0], view=f[1], key=f[2], name=f[3], cmd=f[4], quiet=f[5]))
        return out

    def s_registry(self):
        if "uninstall_name" not in self.app:
            return ("n/a", "no installer")
        e = self.uninstall_entries()
        if not e:
            raise Stage("no Uninstall key with a DisplayName matching /%s/" % self.app["uninstall_name"])
        self.uninstall_entry = e[0]
        return ("pass", "%s\\...\\Uninstall\\%s (%s-bit): %s" % (e[0]["root"], e[0]["key"], e[0]["view"], e[0]["name"]))

    def s_shortcut(self):
        if "lnk" not in self.app:
            return ("n/a", "no installer")
        rx = re.compile(self.app["lnk"], re.I)
        roots = [os.path.join(self.prefix, "drive_c", "ProgramData", "Microsoft", "Windows", "Start Menu", "Programs")]
        users = os.path.join(self.prefix, "drive_c", "users")
        for u in sorted(os.listdir(users)) if os.path.isdir(users) else []:
            roots.append(os.path.join(users, u, "AppData", "Roaming", "Microsoft", "Windows", "Start Menu", "Programs"))
        found = []
        for root in roots:
            for d, _, files in os.walk(root):
                for fn in files:
                    rel = os.path.relpath(os.path.join(d, fn), root)
                    if fn.lower().endswith(".lnk") and rx.search(rel):
                        found.append(rel)
        if not found:
            raise Stage("no Start Menu .lnk matching /%s/" % self.app["lnk"])
        return ("pass", "; ".join(sorted(found)[:3]))

    def launch_args(self):
        subst = {"%SAMPLE_TXT%": r"C:\setu-compat\sample.txt", "%SAMPLE_SVG%": r"C:\setu-compat\sample.svg",
                 "%SAMPLE_HTML%": r"C:\setu-compat\sample.html"}
        return [subst.get(x, x) for x in self.app["launch"].get("args", [])]

    def s_launch(self):
        L = self.app["launch"]
        exe = winpath(self.prefix, L["exe"])
        if L.get("console"):
            log = os.path.join(self.logdir, "launch.log")
            r = subprocess.run([self.wine, L["exe"], *self.launch_args()], env=self.env(), capture_output=True,
                               timeout=L.get("timeout", 60), cwd=os.path.dirname(exe), stdin=subprocess.DEVNULL)
            out = r.stdout.decode("utf-8", "replace").replace("\r", "")
            with open(log, "w", encoding="utf-8") as f:
                f.write(out + r.stderr.decode("utf-8", "replace"))
            if r.returncode != 0:
                raise Stage("exit code %d%s" % (r.returncode, self.evidence("launch.log")))
            if not re.search(L.get("output", ""), out, re.M):
                raise Stage("output %r doesn't match /%s/" % (out.strip()[:80], L.get("output", "")))
            return ("pass", "console: %s" % out.strip().splitlines()[0][:80] if out.strip() else "console")
        self.launch_log = open(os.path.join(self.logdir, "launch.log"), "ab")
        self.launched = subprocess.Popen([self.wine, L["exe"], *self.launch_args()], env=self.env(),
                                         stdout=self.launch_log, stderr=subprocess.STDOUT,
                                         stdin=subprocess.DEVNULL, cwd=os.path.dirname(exe))
        rx = re.compile(L["window"])
        dismiss = [(re.compile(k), v) for k, v in L.get("dismiss", {}).items()]
        dismissed, prompts = [], {}  # prompts: hwnd -> True if a button was clicked in it
        end = time.time() + L.get("timeout", 60)
        seen = []
        while time.time() < end:
            time.sleep(1)
            wins = self.app_windows()
            seen = ["%s|%s" % (w["cls"], w["title"]) for w in wins]
            # A first-run prompt can look like the main window (KeePass 1's
            # main window is a dialog with the same title), so a window that
            # matches a dismiss pattern only counts once no button was found
            # in it.
            for w in wins:
                key = "%s|%s" % (w["cls"], w["title"])
                for drx, button in dismiss:
                    if drx.search(key) and w["hwnd"] not in prompts and len(prompts) < 5:
                        prompts[w["hwnd"]] = self.probe("click", w["hwnd"], button).startswith("clicked")
                        if prompts[w["hwnd"]]:
                            dismissed.append("%s -> %s" % (w["title"], button))
            match = [w for w in wins if rx.search("%s|%s" % (w["cls"], w["title"])) and not prompts.get(w["hwnd"])]
            if match:
                self.main_window = match[0]
                self.windows_seen = seen
                return ("pass", "%s|%s %s%s" % (match[0]["cls"], match[0]["title"], "x".join(
                    str(v) for v in (match[0]["rect"][2] - match[0]["rect"][0], match[0]["rect"][3] - match[0]["rect"][1])),
                    "; answered first-run prompt: " + ", ".join(dismissed) if dismissed else ""))
            if self.launched.poll() is not None and not self.app_pids():
                raise Stage("exited with code %s before showing a window%s" % (
                    self.launched.returncode, self.evidence("launch.log")))
        self.windows_seen = seen
        if seen:
            raise Stage("no window matching /%s/ in %ds; visible: %s%s" % (
                L["window"], L.get("timeout", 60), "; ".join(seen[:4]), self.evidence("launch.log")))
        hidden = ["%s|%s" % (w["cls"], w["title"]) for w in self.windows()
                  if w["exe"].lower() not in SYSTEM_EXES]
        procs = sorted({exe for _, _, exe in self.procs() if exe.lower() not in SYSTEM_EXES})
        raise Stage("no visible window in %ds; processes: %s; hidden windows: %s%s" % (
            L.get("timeout", 60), ", ".join(procs) or "none", "; ".join(hidden[:4]) or "none",
            self.evidence("launch.log")))

    def s_screenshot(self):
        if self.app["launch"].get("console"):
            return ("n/a", "console program")
        time.sleep(3)  # let it finish painting
        os.makedirs(THUMBS, exist_ok=True)
        xwd = os.path.join(self.logdir, "screen.xwd")
        png = os.path.join(THUMBS, re.sub(r"[^A-Za-z0-9_.-]", "_", self.name) + ".png")
        with open(xwd, "wb") as f:
            subprocess.run(["xwd", "-root", "-silent", "-display", self.display], stdout=f, check=True)
        subprocess.run(["convert", xwd, "-resize", "400x", "-strip", "-colors", "128", png], check=True)
        os.unlink(xwd)
        self.thumb = png

    def s_alive(self):
        if self.app["launch"].get("console"):
            return ("n/a", "console program")
        n = self.args.alive
        time.sleep(n)
        if not self.app_pids():
            raise Stage("exited within %ds%s" % (n, self.evidence("launch.log")))
        wins = [w for w in self.app_windows() if re.search(self.app["launch"]["window"], "%s|%s" % (w["cls"], w["title"]))]
        if not wins:
            raise Stage("its window is gone after %ds" % n)
        return ("pass", "running after %ds" % n)

    def s_close(self):
        if self.app["launch"].get("console"):
            return ("n/a", "console program")
        sent = 0
        end = time.time() + 30
        while time.time() < end:
            wins = self.app_windows()
            if not wins and not self.app_pids():
                return ("pass", "exited after WM_CLOSE to %d window(s)" % sent)
            for w in wins[:1]:  # top of the z-order: a prompt comes first
                self.probe("close", w["hwnd"])
                sent += 1
            time.sleep(3)
        left = ["%s|%s" % (w["cls"], w["title"]) for w in self.app_windows()]
        procs = sorted({exe for _, _, exe in self.procs() if exe.lower() not in SYSTEM_EXES})
        raise Stage("still running 30s after WM_CLOSE; processes: %s; windows: %s%s" % (
            ", ".join(procs) or "none", "; ".join(left[:4]) or "none", self.evidence("launch.log")))

    def s_uninstall(self):
        if "uninstall_name" not in self.app:
            return ("n/a", "no installer")
        if not self.args.uninstall:
            return ("skip", "not requested (--uninstall)")
        if self.stages.get("registry", {}).get("status") != "pass":
            return ("skip", "no Uninstall key")
        self.stop_launched()
        e = self.uninstall_entry
        if e["cmd"].lower().startswith("msiexec") or re.fullmatch(r"\{[0-9A-Fa-f-]+\}", e["key"]):
            argv = ["msiexec", "/x", e["key"], "/qn"]
        else:
            argv = split_command(e["quiet"] or (e["cmd"] + " " + self.app.get("uninstall_args", "")))
        rc = self.wine_run(argv, "uninstall.log", 300)
        if rc is None:
            raise Stage("timed out after 300s")
        end = time.time() + 120
        while time.time() < end:
            if not self.uninstall_entries() and not os.path.exists(winpath(self.prefix, self.app["launch"]["exe"])):
                return ("pass", "exit %s; key and %s removed" % (rc, os.path.basename(self.app["launch"]["exe"].replace("\\", "/"))))
            time.sleep(2)
        left = []
        if self.uninstall_entries():
            left.append("Uninstall key")
        if os.path.exists(winpath(self.prefix, self.app["launch"]["exe"])):
            left.append(self.app["launch"]["exe"])
        raise Stage("exit %s; still there: %s%s" % (rc, ", ".join(left), self.evidence("uninstall.log")))

    def stop_launched(self):
        if self.launched and self.launched.poll() is None:
            self.launched.kill()
            self.launched.wait()

    def evidence(self, log):
        ev = first_evidence(os.path.join(self.logdir, log))
        return " [log: %s]" % ev if ev else ""

    def run(self):
        shutil.rmtree(self.logdir, ignore_errors=True)
        os.makedirs(self.logdir, exist_ok=True)
        start = time.time()
        try:
            self.start_xvfb()
            self.stage("download", self.s_download)
            self.stage("prefix", self.s_prefix)
            self.stage("install", self.s_install)
            self.stage("files", self.s_files)
            self.stage("registry", self.s_registry)
            self.stage("shortcut", self.s_shortcut)
            self.stage("launch", self.s_launch)
            if self.stages["launch"]["status"] == "fail" and self.windows_seen:
                # keep a picture of what it showed instead
                try:
                    self.s_screenshot()
                except Exception:
                    pass
            self.stage("screenshot", self.s_screenshot)
            self.stage("alive", self.s_alive)
            self.stage("close", self.s_close)
            self.stage("uninstall", self.s_uninstall)
        finally:
            self.stop_launched()
            if getattr(self, "launch_log", None):
                self.launch_log.close()
            self.stop()
            for fn in os.listdir(self.logdir):
                trim_log(os.path.join(self.logdir, fn))
            if not self.args.keep:
                shutil.rmtree(self.prefix, ignore_errors=True)
        a = self.app
        res = dict(name=self.name, version=a["version"], kind=a["kind"], licence=a["licence"],
                   url=a["url"], large=bool(a.get("large")), stages=self.stages,
                   seconds=round(time.time() - start, 1),
                   windows=self.windows_seen[:6],
                   thumb=os.path.relpath(self.thumb, OUT) if getattr(self, "thumb", None) else None)
        if getattr(self, "pinned", None):
            res["pinned_sha256"] = self.pinned
        res["verdict"] = verdict(res)
        return res


def split_command(cmd):
    """Split a Windows command line (an UninstallString) into argv."""
    cmd = cmd.strip()
    if cmd.startswith('"'):
        exe, _, rest = cmd[1:].partition('"')
    else:
        m = re.match(r"(.*?\.exe)\b(.*)", cmd, re.I)
        exe, rest = (m.group(1), m.group(2)) if m else cmd.split(" ", 1) + [""] * (" " not in cmd)
    args = re.findall(r'"[^"]*"|\S+', rest)
    return [exe] + [a.strip('"') for a in args]


def verdict(res):
    st = res["stages"]
    ok = lambda s: st.get(s, {}).get("status") in ("pass", "n/a")
    if all(ok(s) for s in ("install", "files", "registry", "shortcut", "launch", "alive", "close")):
        return "works"
    if ok("install") and ok("launch"):
        return "partial"
    if ok("install"):
        return "installs"
    return "fails"


# --------------------------------------------------------------------------
# reports

VERDICT_TEXT = {"works": "Works", "partial": "Launches", "installs": "Installs only", "fails": "Fails"}


def write_html(data):
    rows = []
    for r in data["apps"]:
        cells = []
        for s in STAGES:
            st = r["stages"].get(s, {"status": "skip", "seconds": 0, "reason": ""})
            tip = html.escape(st.get("reason") or st["status"], quote=True)
            t = "%.0fs" % st["seconds"] if st["seconds"] >= 1 else ""
            mark = {"pass": "✓", "fail": "✗", "skip": "–", "n/a": "n/a"}[st["status"]]
            cells.append('<td class="st %s" title="%s"><span>%s</span><small>%s</small></td>' % (
                st["status"].replace("/", ""), tip, mark, t))
        thumb = ""
        if r.get("thumb") and os.path.exists(os.path.join(OUT, r["thumb"])):
            b64 = base64.b64encode(open(os.path.join(OUT, r["thumb"]), "rb").read()).decode()
            thumb = '<img alt="%s under Setu" src="data:image/png;base64,%s">' % (html.escape(r["name"]), b64)
        fails = [(s, r["stages"][s]["reason"]) for s in STAGES
                 if r["stages"].get(s, {}).get("status") == "fail"]
        reason = "".join("<div><b>%s</b>: %s</div>" % (s, html.escape(why)) for s, why in fails)
        rows.append('<tr><th scope="row"><div class="app">%s</div><div class="ver">%s · %s</div>'
                    '<div class="v %s">%s</div></th>%s<td class="thumb">%s</td><td class="why">%s</td></tr>' % (
                        html.escape(r["name"]), html.escape(r["version"]), html.escape(r["kind"]),
                        r["verdict"], VERDICT_TEXT[r["verdict"]], "".join(cells), thumb, reason))
    counts = {v: sum(1 for r in data["apps"] if r["verdict"] == v) for v in VERDICT_TEXT}
    launched = sum(1 for r in data["apps"] if r["stages"].get("launch", {}).get("status") == "pass")
    installed = sum(1 for r in data["apps"] if r["stages"].get("install", {}).get("status") == "pass")
    n = len(data["apps"])
    page = """<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Setu app compatibility</title>
<style>
:root { --bg:#f7f6f2; --fg:#1d232a; --muted:#5d6670; --line:#d9d6cc; --card:#fff;
  --pass:#1f7a4d; --pass-bg:#e3f3ea; --fail:#b42318; --fail-bg:#fbe7e5; --skip:#8a8f96; --skip-bg:#efeee9; }
@media (prefers-color-scheme: dark) { :root { --bg:#14181c; --fg:#e7e9ea; --muted:#9aa3ab; --line:#2c333a;
  --card:#1b2026; --pass:#5cd08f; --pass-bg:#173326; --fail:#ff8a7a; --fail-bg:#3a1d1a; --skip:#7d858c; --skip-bg:#20252b; } }
* { box-sizing:border-box }
body { margin:0; background:var(--bg); color:var(--fg); font:14px/1.45 system-ui, sans-serif; }
header, main { max-width:1500px; margin:0 auto; padding:16px; }
h1 { font-size:22px; margin:8px 0 4px } .meta { color:var(--muted) }
.tiles { display:flex; gap:12px; flex-wrap:wrap; margin:14px 0 }
.tile { background:var(--card); border:1px solid var(--line); border-radius:8px; padding:10px 14px; min-width:130px }
.tile b { font-size:22px; display:block }
.wrap { overflow-x:auto; border:1px solid var(--line); border-radius:8px; background:var(--card) }
table { border-collapse:collapse; width:100%% }
th, td { border-bottom:1px solid var(--line); padding:6px 8px; vertical-align:top; text-align:left }
thead th { font-size:12px; color:var(--muted); font-weight:600; position:sticky; top:0; background:var(--card) }
.app { font-weight:600 } .ver { color:var(--muted); font-size:12px }
.v { display:inline-block; margin-top:4px; font-size:12px; padding:1px 6px; border-radius:4px }
.v.works { background:var(--pass-bg); color:var(--pass) } .v.fails { background:var(--fail-bg); color:var(--fail) }
.v.partial, .v.installs { background:var(--skip-bg); color:var(--fg) }
td.st { text-align:center; min-width:58px } td.st span { display:block; font-weight:700 }
td.st small { color:var(--muted); font-size:11px }
td.pass { background:var(--pass-bg); color:var(--pass) } td.fail { background:var(--fail-bg); color:var(--fail) }
td.skip, td.na { color:var(--skip) }
td.thumb img { width:200px; border:1px solid var(--line); border-radius:4px; display:block }
td.why { font-size:12px; max-width:420px; word-break:break-word }
footer { color:var(--muted); font-size:12px; max-width:1500px; margin:0 auto; padding:0 16px 24px }
</style></head><body>
<header><h1>Setu app compatibility</h1>
<div class="meta">Setu %(commit)s · personality %(wine)s · run %(date)s · Xvfb %(res)s, fresh prefix per app</div>
<div class="tiles">
<div class="tile"><b>%(installed)d/%(n)d</b>installed</div>
<div class="tile"><b>%(launched)d/%(n)d</b>launched a window</div>
<div class="tile"><b>%(works)d/%(n)d</b>all stages pass</div>
</div></header>
<main><div class="wrap"><table>
<thead><tr><th>App</th>%(heads)s<th>Screen</th><th>Failure reasons</th></tr></thead>
<tbody>%(rows)s</tbody></table></div></main>
<footer>Stages: hover a cell for its detail. Generated by tools/compat/run.py; method in docs/COMPAT.md.
Thumbnails are small Xvfb captures of each program running under Setu.</footer>
</body></html>
""" % dict(commit=html.escape(data["commit"]), wine=html.escape(data["wine"]), date=html.escape(data["date"]),
           res=RES, installed=installed, launched=launched, works=counts["works"], n=n,
           heads="".join("<th>%s</th>" % s for s in STAGES), rows="\n".join(rows))
    with open(os.path.join(OUT, "index.html"), "w", encoding="utf-8") as f:
        f.write(page)


def markdown_table(data):
    mark = {"pass": "✓", "fail": "✗", "skip": "–", "n/a": "n/a"}
    head = "| App | Version | Verdict | " + " | ".join(STAGES) + " |"
    out = [head, "|" + "---|" * (3 + len(STAGES))]
    for r in data["apps"]:
        out.append("| %s | %s | %s | %s |" % (r["name"], r["version"], VERDICT_TEXT[r["verdict"]], " | ".join(
            mark[r["stages"].get(s, {"status": "skip"})["status"]] for s in STAGES)))
    n = len(data["apps"])
    inst = sum(1 for r in data["apps"] if r["stages"].get("install", {}).get("status") == "pass")
    lau = sum(1 for r in data["apps"] if r["stages"].get("launch", {}).get("status") == "pass")
    ok = sum(1 for r in data["apps"] if r["verdict"] == "works")
    summary = ("Setu `%s`, personality %s, %s: **%d/%d installed, %d/%d launched, %d/%d pass every stage.**"
               % (data["commit"], data["wine"], data["date"], inst, n, lau, n, ok, n))
    fails = []
    for r in data["apps"]:
        for s in STAGES:
            st = r["stages"].get(s, {})
            if st.get("status") == "fail":
                fails.append("- **%s** %s: %s" % (r["name"], s, st["reason"].replace("|", "\\|")))
    return summary + "\n\n" + "\n".join(out) + "\n\nFailures:\n\n" + ("\n".join(fails) or "- none") + "\n"


def update_docs(data):
    doc = open(DOC, encoding="utf-8").read()
    begin, end = "<!-- results:begin -->", "<!-- results:end -->"
    i, j = doc.index(begin) + len(begin), doc.index(end)
    doc = doc[:i] + "\n" + markdown_table(data) + doc[j:]
    open(DOC, "w", encoding="utf-8").write(doc)


# --------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-a", "--app", action="append", help="run only this app (repeatable)")
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--small", action="store_true", help="only the apps not marked large")
    g.add_argument("--large", action="store_true", help="only the apps marked large")
    ap.add_argument("-j", type=int, default=1, choices=(1, 2), help="parallel apps (at most 2)")
    ap.add_argument("--uninstall", action="store_true", help="also run the silent uninstall")
    ap.add_argument("--alive", type=int, default=10, help="seconds the app must keep running (default 10)")
    ap.add_argument("--keep", action="store_true", help="keep the prefixes")
    ap.add_argument("--pin", action="store_true", help="accept and print the sha256 of unpinned downloads")
    ap.add_argument("--html-only", action="store_true", help="rebuild index.html from results.json")
    ap.add_argument("--update-docs", action="store_true", help="rewrite the results table in docs/COMPAT.md")
    ap.add_argument("--markdown", action="store_true", help="with --html-only: print the markdown table")
    ap.add_argument("--manifest", default=os.path.join(HERE, "apps.toml"), help="manifest (default: apps.toml)")
    ap.add_argument("--fresh", action="store_true", help="drop earlier results of apps not run now")
    args = ap.parse_args()

    toml = tomllib.load(open(args.manifest, "rb"))
    manifest, runtimes = toml["app"], toml.get("runtime", {})
    names = [a["name"] for a in manifest]
    if args.html_only:
        data = json.load(open(RESULTS))
        write_html(data)
        if args.update_docs:
            update_docs(data)
        print(markdown_table(data) if args.markdown else os.path.join(OUT, "index.html"))
        return 0
    reexec_in_namespace()
    apps = manifest
    if args.app:
        unknown = [n for n in args.app if n not in names]
        if unknown:
            sys.exit("unknown app(s): %s (have: %s)" % (", ".join(unknown), ", ".join(names)))
        apps = [a for a in manifest if a["name"] in args.app]
    elif args.small:
        apps = [a for a in manifest if not a.get("large")]
    elif args.large:
        apps = [a for a in manifest if a.get("large")]
    wine = find_wine()
    build_probe()
    os.makedirs(OUT, exist_ok=True)

    with concurrent.futures.ThreadPoolExecutor(max_workers=args.j) as pool:
        results = list(pool.map(lambda a: AppRun(a, args, wine, runtimes).run(), apps))

    old = {}
    if os.path.exists(RESULTS) and not args.fresh:
        old = {r["name"]: r for r in json.load(open(RESULTS)).get("apps", [])}
    for r in results:
        old[r["name"]] = r
    data = dict(schema=1, commit=setu_commit(), wine=wine_build(wine),
                date=datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%d %H:%M UTC"),
                apps=[old[n] for n in names if n in old])
    with open(RESULTS, "w") as f:
        json.dump(data, f, indent=1, ensure_ascii=False)
        f.write("\n")
    write_html(data)
    if args.update_docs:
        update_docs(data)
    print()
    print(markdown_table(data))
    for r in results:
        if r.get("pinned_sha256"):
            print("pin %s: sha256 = \"%s\"  size = %d" % (r["name"], r["pinned_sha256"],
                                                        os.path.getsize(os.path.join(CACHE, cache_name(
                                                            next(a for a in manifest if a["name"] == r["name"]))))))
    print("results:", RESULTS)
    print("page:   ", os.path.join(OUT, "index.html"))
    return 0 if all(r["verdict"] == "works" for r in results) else 1


if __name__ == "__main__":
    sys.exit(main())
