#!/usr/bin/env python3
# SPDX-License-Identifier: MIT OR Apache-2.0
"""Install the default apps into a staging C: drive for the Setu image.

Runs on the build host with the Setu personality, before the builder VM
starts (distro/build-image.sh). In a throwaway prefix under its own Xvfb
(never the host display), each app in distro/apps/apps.toml is installed
with the vendor's documented silent install, as on Windows:

  OUT/c/...       the files the installs added to C: (Program Files,
                  Program Files (x86), ProgramData: the programs and the
                  Start menu shortcuts in the common Programs folder;
                  windows/Installer: the MSI packages Windows Installer
                  keeps for repair and uninstall, and shortcut icons);
                  the image puts them on the system volume behind C:
  OUT/apps.reg    what the installs added to HKEY_LOCAL_MACHINE\\Software
                  (file types, App Paths, Uninstall entries...), plus
                  Setu's choice of default app per file type
                  ([associations] in apps.toml), as a regedit file;
                  setu-apps-register imports it into every user's prefix
  OUT/report.json per app: installed or not, exit code, time, size
  OUT/stamp       hash of the inputs; a later run with the same inputs
                  does nothing (--force redoes it). apps.reg writes it to
                  HKLM\\Software\\Setu\\DefaultApps "Stamp"

Zip apps are unpacked to their install_dir. Apps whose installer makes no
shortcut, and Wine's accessories ([[builtin]]), get one from setu-mklnk.
An app with start_menu = false stays installed without Start menu
shortcuts (the ones its installer made are removed).

Usage: distro/apps/stage.py [--out DIR] [--cache DIR] [--wine WINE]
                            [-a ID]... [--force] [--keep-prefix]
Environment: SETU_WINE (default: the main checkout's personality build).
Exit status: 0 when every app installed, 1 otherwise (the output is still
written, without the apps that failed).
"""

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import fetch  # noqa: E402

OUT = os.path.join(REPO, "build", "distro", "apps-stage")
RES = "1280x800"
TOOLCHAIN = os.path.expanduser("~/.local/setu-toolchain/bin")
COMMON_PROGRAMS = r"C:\ProgramData\Microsoft\Windows\Start Menu\Programs"
ICON_DIR = r"C:\ProgramData\Setu\Icons"
# Files the installs add here are kept (relative to C:, lower case)
KEEP_DIRS = ("program files/", "program files (x86)/", "programdata/",
             # MSI: the cached packages (repair, uninstall) and shortcut icons
             "windows/installer/")
# Registry keys under HKLM that are not the apps' (Wine's own bookkeeping)
SKIP_KEYS = re.compile(r"^software\\(wine|microsoft\\cryptography|microsoft\\windows nt\\currentversion\\perflib"
                       r"|microsoft\\windows\\currentversion\\explorer\\shell icons)(\\|$)", re.I)


def log(msg):
    print("[stage] " + msg, flush=True)


# --------------------------------------------------------------------------
# environment

def reexec_in_namespace():
    """Host name "setu" in a private UTS namespace (as tools/compat/run.py
    does), so installers never record the build machine's name."""
    stage = os.environ.get("SETU_STAGE_NS", "")
    if stage == "":
        if subprocess.run(["unshare", "-Ur", "--uts", "true"], capture_output=True).returncode == 0:
            env = dict(os.environ, SETU_STAGE_NS="1", SETU_STAGE_UID=str(os.getuid()),
                       SETU_STAGE_GID=str(os.getgid()))
            os.execvpe("unshare", ["unshare", "-Ur", "--uts", sys.executable, *sys.argv], env)
        log("warning: no user + UTS namespace; the host name will show")
    elif stage == "1":
        subprocess.run(["hostname", "setu"], check=True)
        env = dict(os.environ, SETU_STAGE_NS="2")
        os.execvpe("unshare", ["unshare", "-U", "--map-user=" + os.environ["SETU_STAGE_UID"],
                               "--map-group=" + os.environ["SETU_STAGE_GID"],
                               sys.executable, *sys.argv], env)


def find_wine(arg):
    cands = [arg, os.environ.get("SETU_WINE"), os.path.join(REPO, "build/personality/install/bin/wine")]
    try:
        common = subprocess.run(["git", "-C", REPO, "rev-parse", "--path-format=absolute", "--git-common-dir"],
                                capture_output=True, text=True).stdout.strip()
        if common:
            cands.append(os.path.join(os.path.dirname(common), "build/personality/install/bin/wine"))
    except OSError:
        pass
    for w in cands:
        if w and os.access(w, os.X_OK):
            return os.path.abspath(w)
    sys.exit("stage: no personality build found; set SETU_WINE or pass --wine")


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


class Prefix:
    def __init__(self, root, wine):
        self.root = root
        self.prefix = os.path.join(root, "prefix")
        self.cdir = os.path.join(root, "c")
        self.home = os.path.join(root, "home")
        self.wine = wine
        self.wineserver = os.path.join(os.path.dirname(wine), "wineserver")
        self.xvfb = None
        self.display = None
        self.logdir = os.path.join(root, "logs")

    def env(self, debug="-all"):
        e = {k: v for k, v in os.environ.items()
             if k not in ("DISPLAY", "WAYLAND_DISPLAY", "XAUTHORITY", "WINEDEBUG", "WINEPREFIX")}
        h = self.home
        e.update(HOME=h, XDG_DATA_HOME=h + "/.local/share", XDG_CONFIG_HOME=h + "/.config",
                 XDG_CACHE_HOME=h + "/.cache", USER="setu", LOGNAME="setu",
                 WINEPREFIX=self.prefix, DISPLAY=self.display, WINEDEBUG=debug,
                 # no Wine Mono or Gecko in the staging prefix: no download prompt
                 WINEDLLOVERRIDES="mscoree,mshtml=;winemenubuilder.exe=d",
                 TZ="UTC", LANG="en_US.UTF-8", LC_ALL="en_US.UTF-8")
        return e

    def start_xvfb(self):
        r, w = os.pipe()
        self.xvfb = subprocess.Popen(["Xvfb", "-displayfd", str(w), "-screen", "0", RES + "x24",
                                      "-nolisten", "tcp"], pass_fds=(w,),
                                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        os.close(w)
        num = b""
        while not num.endswith(b"\n"):
            b = os.read(r, 16)
            if not b:
                break
            num += b
        os.close(r)
        if not num.strip():
            sys.exit("stage: Xvfb did not start")
        self.display = ":" + num.decode().strip()

    def stop(self):
        subprocess.run([self.wineserver, "-k"], env=self.env(), capture_output=True)
        if self.xvfb:
            self.xvfb.terminate()
            self.xvfb.wait()

    def run(self, argv, logname, timeout, cwd=None):
        """wine ARGV; returns the exit code, or None after a timeout."""
        os.makedirs(self.logdir, exist_ok=True)
        with open(os.path.join(self.logdir, logname), "ab") as f:
            p = subprocess.Popen([self.wine, *argv], env=self.env("err+all"), stdout=f,
                                 stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, cwd=cwd or self.root)
            try:
                return p.wait(timeout)
            except subprocess.TimeoutExpired:
                p.kill()
                p.wait()
                return None

    def settle(self, timeout=120):
        """Wait for every process in the prefix to end (an installer may
        leave a helper running), then stop the wineserver, which writes the
        registry to disk."""
        try:
            subprocess.run([self.wineserver, "-w"], env=self.env(), timeout=timeout)
        except subprocess.TimeoutExpired:
            pass
        subprocess.run([self.wineserver, "-k"], env=self.env(), capture_output=True)
        subprocess.run([self.wineserver, "-w"], env=self.env(), capture_output=True)

    def create(self):
        os.makedirs(os.path.join(self.prefix, "dosdevices"), exist_ok=True)
        os.makedirs(self.cdir, exist_ok=True)
        os.makedirs(self.home, exist_ok=True)
        os.symlink(self.cdir, os.path.join(self.prefix, "dosdevices", "c:"))
        os.symlink("/", os.path.join(self.prefix, "dosdevices", "z:"))
        os.symlink(self.cdir, os.path.join(self.prefix, "drive_c"))
        rc = self.run(["wineboot", "-i"], "wineboot.log", 600)
        self.settle()
        if rc != 0 or not os.path.exists(os.path.join(self.prefix, "system.reg")):
            sys.exit("stage: wineboot -i failed (log: %s)" % os.path.join(self.logdir, "wineboot.log"))

    def unix(self, winpath):
        """C:\\... -> the Unix path under the staging C:, matching case-insensitively."""
        assert winpath[:2].upper() == "C:", winpath
        cur = self.cdir
        for part in [x for x in winpath[2:].split("\\") if x]:
            nxt = os.path.join(cur, part)
            if not os.path.lexists(nxt) and os.path.isdir(cur):
                for e in os.listdir(cur):
                    if e.lower() == part.lower():
                        nxt = os.path.join(cur, e)
                        break
            cur = nxt
        return cur


# --------------------------------------------------------------------------
# Wine's registry files ("WINE REGISTRY Version 2", server/registry.c)

def unescape(s):
    """A string as server/unicode.c dump_strW writes it."""
    out, i = [], 0
    simple = {"a": "\a", "b": "\b", "t": "\t", "n": "\n", "v": "\v", "f": "\f", "r": "\r", "e": "\x1b"}
    while i < len(s):
        c = s[i]
        if c != "\\":
            out.append(c)
            i += 1
            continue
        i += 1
        c = s[i]
        if c == "x":
            m = re.match(r"[0-9a-fA-F]{1,4}", s[i + 1:])
            out.append(chr(int(m.group(0), 16)))
            i += 1 + len(m.group(0))
        elif c in "01234567":
            m = re.match(r"[0-7]{1,3}", s[i:])
            out.append(chr(int(m.group(0), 8)))
            i += len(m.group(0))
        elif c in simple:
            out.append(simple[c])
            i += 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


def split_quoted(s):
    """'"abc\\"d"rest' -> ('abc\\"d', 'rest')"""
    assert s[0] == '"', s
    i = 1
    while s[i] != '"':
        i += 2 if s[i] == "\\" else 1
    return s[1:i], s[i + 1:]


def parse_value(data):
    """Wine's value text -> (type, bytes)."""
    if data.startswith('"'):
        return 1, (unescape(split_quoted(data)[0]) + "\0").encode("utf-16-le")
    m = re.match(r"str\(([0-9a-f]+)\):", data)
    if m:
        return int(m.group(1), 16), (unescape(split_quoted(data[m.end():])[0]) + "\0").encode("utf-16-le")
    if data.startswith("dword:"):
        return 4, int(data[6:], 16).to_bytes(4, "little")
    m = re.match(r"hex(?:\(([0-9a-f]+)\))?:", data)
    if m:
        typ = int(m.group(1), 16) if m.group(1) else 3
        hx = data[m.end():].replace(",", "").replace("\\", "").replace(" ", "")
        return typ, bytes.fromhex(hx)
    raise ValueError("unknown value: " + data[:40])


def read_reg(path):
    """{key (as in the file, unescaped): {value name ('' = default): (type, bytes)}};
    symbolic-link keys are left out."""
    keys, cur, pending = {}, None, ""
    with open(path, encoding="utf-8", errors="surrogateescape") as f:
        lines = f.read().split("\n")
    for line in lines:
        if pending:
            line = pending + line.lstrip()
            pending = ""
        if line.endswith("\\") and cur is not None and ("=hex" in line or pending):
            pending = line[:-1]
            continue
        if line.startswith("["):
            end = line.rindex("]")
            name = unescape(line[1:end])
            cur = keys.setdefault(name, {})
        elif cur is None or not line or line.startswith(";"):
            continue
        elif line.startswith("#link"):
            keys.pop(name, None)
            cur = {}
        elif line.startswith("#"):
            continue
        elif line.startswith("@="):
            cur[""] = parse_value(line[2:])
        elif line.startswith('"'):
            n, rest = split_quoted(line)
            cur[unescape(n)] = parse_value(rest[1:])
    return keys


def reg_delta(before, after):
    """Keys and values that are new or changed in AFTER (under Software)."""
    out = {}
    for k, vals in after.items():
        if not k.lower().startswith("software\\") or SKIP_KEYS.match(k):
            continue
        old = before.get(k)
        if old is None:
            out[k] = dict(vals)
            continue
        ch = {n: v for n, v in vals.items() if old.get(n) != v}
        if ch:
            out[k] = ch
    return out


def regedit_text(keys):
    """{key: {name: (type, bytes)}} -> regedit 5 text (HKEY_LOCAL_MACHINE)."""
    def esc(s):
        return s.replace("\\", "\\\\").replace('"', '\\"')

    lines = []
    for k in sorted(keys, key=str.lower):
        lines.append("")
        lines.append("[HKEY_LOCAL_MACHINE\\%s]" % k)
        for n in sorted(keys[k], key=str.lower):
            typ, data = keys[k][n]
            name = "@" if n == "" else '"%s"' % esc(n)
            text = None
            if typ == 1 and len(data) % 2 == 0:
                s = data.decode("utf-16-le", "surrogatepass")
                if s.endswith("\0") and "\0" not in s[:-1] and not re.search(r"[\x00-\x1f]", s[:-1]):
                    text = '"%s"' % esc(s[:-1])
            elif typ == 4 and len(data) == 4:
                text = "dword:%08x" % int.from_bytes(data, "little")
            if text is None:
                hx = ",".join("%02x" % b for b in data)
                text = ("hex:" if typ == 3 else "hex(%x):" % typ) + hx
            lines.append("%s=%s" % (name, text))
    return "\n".join(lines)


# --------------------------------------------------------------------------
# files

def snapshot(cdir):
    out = {}
    for dp, dn, fn in os.walk(cdir):
        for f in fn:
            p = os.path.join(dp, f)
            try:
                st = os.lstat(p)
            except OSError:
                continue
            out[os.path.relpath(p, cdir)] = (st.st_size, st.st_mtime_ns)
    return out


def dir_size(path):
    total = 0
    for dp, dn, fn in os.walk(path):
        for f in fn:
            try:
                total += os.lstat(os.path.join(dp, f)).st_size
            except OSError:
                pass
    return total


def unzip_to(archive, dest):
    """Unpack ARCHIVE into DEST; a single top-level folder is stripped."""
    with zipfile.ZipFile(archive) as z:
        names = [n for n in z.namelist() if n.strip("/")]
        tops = {n.split("/", 1)[0] for n in names}
        strip = len(tops) == 1 and all("/" in n for n in names)
        for info in z.infolist():
            name = info.filename.split("/", 1)[1] if strip else info.filename
            if not name or name.startswith("/") or ".." in name.split("/"):
                continue
            target = os.path.join(dest, name)
            if info.is_dir():
                os.makedirs(target, exist_ok=True)
                continue
            os.makedirs(os.path.dirname(target), exist_ok=True)
            with z.open(info) as src, open(target, "wb") as dst:
                shutil.copyfileobj(src, dst)


def start_menu_links(files):
    """Shortcuts among FILES (paths relative to C:), relative to the
    Programs folder they are in (the common one or the build user's)."""
    out = []
    for rel in files:
        low = rel.lower()
        i = low.find("/start menu/programs/")
        if i >= 0 and low.endswith((".lnk", ".url")) and \
                low.startswith(("programdata/", "users/")):
            out.append(rel[i + len("/start menu/programs/"):])
    return sorted(out)


def remove_links(progs, rels):
    """Delete the shortcuts RELS under PROGS, and folders left empty."""
    gone = []
    for rel in rels:
        p = os.path.join(progs, rel)
        if not os.path.lexists(p):
            continue
        os.remove(p)
        gone.append(rel)
        d = os.path.dirname(p)
        while os.path.abspath(d) != os.path.abspath(progs) and not os.listdir(d):
            os.rmdir(d)
            d = os.path.dirname(d)
    return gone


def build_mklnk(outdir):
    exe = os.path.join(outdir, "setu-mklnk.exe")
    env = dict(os.environ, PATH=TOOLCHAIN + os.pathsep + os.environ.get("PATH", ""))
    subprocess.run(["x86_64-w64-mingw32-gcc", "-O1", "-Wall", "-Wextra", "-Werror", "-municode",
                    "-o", exe, os.path.join(HERE, "setu-mklnk.c"), "-lole32", "-luuid", "-lshell32"],
                   check=True, env=env)
    return exe


# --------------------------------------------------------------------------

def install(px, app, path):
    kind = app["kind"]
    zp = "Z:" + path.replace("/", "\\")
    timeout = app.get("install_timeout", 600)
    if kind == "msi":
        return px.run(["msiexec", "/i", zp, *app["install_args"]], app["id"] + ".log", timeout)
    if kind in ("nsis", "inno", "mozilla", "sumatra"):
        return px.run([zp, *app["install_args"]], app["id"] + ".log", timeout)
    if kind == "zip":
        unzip_to(path, px.unix(app["install_dir"]))
        return 0
    raise ValueError("unknown kind " + kind)


def sz(s):
    return (1, (s + "\0").encode("utf-16-le"))


def sz_text(v):
    return v[1].decode("utf-16-le", "replace").rstrip("\0") if v[0] in (1, 2) else ""


def progids_for(delta, ext):
    """ProgIDs an install registered for EXT (a file extension or a URL
    protocol): the extension's default and OpenWithProgids, and the app's
    Capabilities (FileAssociations, URLAssociations: Default Programs,
    https://learn.microsoft.com/windows/win32/shell/default-programs)."""
    out = []
    # key names keep the case of whichever program created them first
    low = {key.lower(): vals for key, vals in delta.items()}
    k = low.get(("Software\\Classes\\" + ext).lower(), {})
    if "" in k and sz_text(k[""]):
        out.append(sz_text(k[""]))
    for n in low.get(("Software\\Classes\\%s\\OpenWithProgids" % ext).lower(), {}):
        if n:
            out.append(n)
    for key, vals in delta.items():
        if key.lower().endswith(("\\capabilities\\fileassociations", "\\capabilities\\urlassociations")):
            for n, v in vals.items():
                if n.lower() == ext.lower() and sz_text(v):
                    out.append(sz_text(v))
    return out


def associations(conf, deltas, registry, apps, open_with=None):
    """Setu's default app per file type: Software\\Classes\\<ext> points at
    the ProgID that app's own installer registered; for apps whose
    installer registers none, a Setu.<name> ProgID is written. A URL
    protocol (no leading dot) gets the ProgID's open command. Types in
    OPEN_WITH ([open_with]) only get the app's ProgID in OpenWithProgids:
    their default stays the system's."""
    lower = {k.lower(): k for k in registry}
    keys, notes = {}, []
    items = [(e, a, False) for e, a in conf.items()]
    items += [(e, a, True) for e, a in (open_with or {}).items()]
    for ext, appid, only_open_with in sorted(items):
        app = apps.get(appid)
        if not app or appid not in deltas:
            notes.append("%s: %s not installed" % (ext, appid))
            continue
        progid = cmd = None
        for p in progids_for(deltas[appid], ext):
            c = registry.get(lower.get(("software\\classes\\%s\\shell\\open\\command" % p).lower(), ""), {}).get("")
            if c and sz_text(c):
                progid, cmd = p, sz_text(c)
                break
        if not progid:
            progid = "Setu.%s" % re.sub(r"[^A-Za-z0-9]", "", app["name"])
            cmd = '"%s" "%%1"' % app["exe"]
            keys["Software\\Classes\\" + progid] = {"": sz(app["name"])}
            keys["Software\\Classes\\%s\\DefaultIcon" % progid] = {"": sz('"%s",0' % app["exe"])}
            keys["Software\\Classes\\%s\\shell\\open\\command" % progid] = {"": sz(cmd)}
            notes.append("%s: %s (Setu ProgID %s)" % (ext, appid, progid))
        else:
            notes.append("%s: %s (%s)" % (ext, appid, progid))
        if only_open_with:
            notes[-1] += ", Open with only"
            keys.setdefault("Software\\Classes\\%s\\OpenWithProgids" % ext, {})[progid] = (0, b"")
        elif ext.startswith("."):
            keys.setdefault("Software\\Classes\\" + ext, {})[""] = sz(progid)
            keys.setdefault("Software\\Classes\\%s\\OpenWithProgids" % ext, {})[progid] = (0, b"")
        else:
            keys["Software\\Classes\\" + ext] = {"": sz("URL:%s" % ext.upper()), "URL Protocol": sz("")}
            keys["Software\\Classes\\%s\\shell\\open\\command" % ext] = {"": sz(cmd)}
            keys["Software\\Classes\\%s\\DefaultIcon" % ext] = {"": sz('"%s",0' % app["exe"])}
    return keys, notes


def licences_text(apps):
    """/usr/share/setu/apps/LICENSES.txt: each app's licence and where its
    source code is (docs/DEFAULT_APPS.md, "Licences and source")."""
    out = ["The default apps in this Setu image (docs/DEFAULT_APPS.md)",
           "",
           "Each app is the vendor's official Windows release, installed unmodified",
           "with its own installer. Its licence text is in its install folder and at",
           "the licence URL below. The source code of exactly this release is at the",
           "source URL (docs/DEFAULT_APPS.md, \"Licences and source\").", ""]
    for a in apps:
        out += ["%s %s" % (a["name"], a["version"]),
                "  licence: %s" % a["licence"],
                "  licence text: %s" % a["licence_url"],
                "  source: %s" % a["source_url"],
                "  downloaded from: %s" % a["url"],
                "  sha256: %s" % a["sha256"], ""]
    return "\n".join(out)


def inputs_hash(manifest_path, wine, ids):
    h = hashlib.sha256()
    for f in ("apps.toml", "stage.py", "setu-mklnk.c", "icons.py"):
        p = os.path.join(HERE, f)
        if os.path.exists(p):
            h.update(open(p, "rb").read())
    build = os.path.join(os.path.dirname(os.path.dirname(wine)), "SETU_BUILD")
    if os.path.exists(build):
        h.update(open(build, "rb").read())
    h.update(",".join(ids).encode())
    return h.hexdigest()[:16]


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--out", default=OUT)
    ap.add_argument("--cache", default=fetch.CACHE)
    ap.add_argument("--wine")
    ap.add_argument("-a", dest="only", action="append", default=[])
    ap.add_argument("--force", action="store_true")
    ap.add_argument("--keep-prefix", action="store_true")
    a = ap.parse_args()

    conf = fetch.load()
    apps = [x for x in conf["app"] if not a.only or x["id"] in a.only]
    builtins = conf.get("builtin", []) if not a.only else []
    wine = find_wine(a.wine)
    out = os.path.abspath(a.out)
    stamp = inputs_hash(fetch.MANIFEST, wine, [x["id"] for x in apps])
    if not a.force and os.path.exists(os.path.join(out, "stamp")) \
            and open(os.path.join(out, "stamp")).read().strip() == stamp:
        log("up to date (%s)" % out)
        return 0 if json.load(open(os.path.join(out, "report.json")))["ok"] else 1

    reexec_in_namespace()
    # downloads first: a missing file fails before anything is installed
    paths = {}
    for app in apps:
        p, msg = fetch.fetch(app, a.cache)
        log("%-17s %s" % (app["id"], msg))
        if not p:
            sys.exit("stage: cannot get %s" % app["id"])
        paths[app["id"]] = p

    work = out + ".work"
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    px = Prefix(work, wine)
    px.logdir = out + "-logs"
    shutil.rmtree(px.logdir, ignore_errors=True)
    px.start_xvfb()
    report = {"stamp": stamp, "wine": wine, "apps": {}, "builtins": {}, "ok": True}
    deltas, links = {}, {}
    try:
        log("creating the staging prefix (Xvfb %s)" % px.display)
        px.create()
        base_files = snapshot(px.cdir)
        reg_path = os.path.join(px.prefix, "system.reg")
        base_reg = prev_reg = read_reg(reg_path)

        for app in apps:
            t0 = time.time()
            before = snapshot(px.cdir)
            log("installing %s %s (%s)" % (app["id"], app["version"], app["kind"]))
            rc = install(px, app, paths[app["id"]])
            px.settle()
            cur_reg = read_reg(reg_path)
            exe_ok = os.path.isfile(px.unix(app["exe"]))
            ok = rc in (0, 3010) and exe_ok
            new = {k: v for k, v in snapshot(px.cdir).items() if before.get(k) != v}
            links[app["id"]] = start_menu_links(new)
            r = dict(version=app["version"], exit=rc, installed=ok, exe=exe_ok,
                     seconds=round(time.time() - t0), files=len(new),
                     bytes=sum(v[0] for v in new.values()))
            report["apps"][app["id"]] = r
            if ok:
                deltas[app["id"]] = reg_delta(prev_reg, cur_reg)
            else:
                report["ok"] = False
            log("  %s: exit %s, exe %s, %d files, %d MiB, %ds" % (
                "ok" if ok else "FAILED", rc, "found" if exe_ok else "missing",
                r["files"], r["bytes"] >> 20, r["seconds"]))
            prev_reg = cur_reg

        # Shortcuts an MSI made in the installing user's Programs folder go to
        # the common one, where ALLUSERS=1 puts them on Windows (Wine's msi
        # resolves ProgramMenuFolder per user: dlls/msi/package.c FIXME).
        # Desktop shortcuts are left out: the desktop stays empty. So are
        # Startup shortcuts (ShareX makes one): no default app starts by
        # itself at login.
        progs = px.unix(COMMON_PROGRAMS)
        for rel in sorted(snapshot(progs) if os.path.isdir(progs) else ()):
            if rel.lower().startswith("startup/"):
                os.remove(os.path.join(progs, rel))
                log("shortcut %s: removed (no app starts at login)" % rel)
        os.makedirs(progs, exist_ok=True)
        user_progs = px.unix(r"C:\users\setu\AppData\Roaming\Microsoft\Windows\Start Menu\Programs")
        for rel in sorted(snapshot(user_progs) if os.path.isdir(user_progs) else ()):
            if not rel.lower().endswith((".lnk", ".url")):
                continue
            dst = os.path.join(progs, rel)
            if rel.lower().startswith("startup/"):
                log("shortcut %s: left out (no app starts at login)" % rel)
            elif not os.path.exists(dst):
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                os.rename(os.path.join(user_progs, rel), dst)
                log("shortcut %s: moved to the common Programs folder" % rel)

        # Apps kept out of the Start menu (start_menu = false in apps.toml)
        for app in apps:
            if app.get("start_menu", True):
                continue
            gone = remove_links(progs, links.get(app["id"], []))
            gone += remove_links(user_progs, links.get(app["id"], []))
            report["apps"][app["id"]]["start_menu"] = False
            for rel in sorted(set(gone)):
                log("shortcut %s: removed (%s: start_menu = false)" % (rel, app["id"]))

        # Start menu shortcuts for zip apps and Wine's accessories
        mklnk = build_mklnk(work)
        icons_out = px.unix(ICON_DIR)
        os.makedirs(icons_out, exist_ok=True)
        if builtins:
            subprocess.run([sys.executable, os.path.join(HERE, "icons.py"), icons_out,
                            *sorted({b["icon"] for b in builtins})], check=True)
        lnks = [(x, x["lnk"], None) for x in apps
                if x.get("lnk") and x.get("start_menu", True) and report["apps"][x["id"]]["installed"]]
        lnks += [(b, b["lnk"], "%s\\%s.ico" % (ICON_DIR, b["icon"])) for b in builtins]
        for item, name, icon in lnks:
            argv = ["Z:" + mklnk.replace("/", "\\"), "%s\\%s.lnk" % (COMMON_PROGRAMS, name), item["exe"],
                    "-d", item["exe"].rsplit("\\", 1)[0]]
            if icon:
                argv += ["-i", icon + ",0"]
            rc = px.run(argv, "mklnk.log", 60)
            done = rc == 0 and os.path.exists(os.path.join(progs, name + ".lnk"))
            if "kind" not in item:
                report["builtins"][item["id"]] = dict(lnk=name, ok=done)
            log("shortcut %s.lnk: %s" % (name, "ok" if done else "FAILED (exit %s)" % rc))
            report["ok"] = report["ok"] and done
        px.settle()
        final_reg = read_reg(reg_path)

        # what goes into the image
        shutil.rmtree(out, ignore_errors=True)
        os.makedirs(out)
        outc = os.path.join(out, "c")
        skipped = {}
        for rel, v in snapshot(px.cdir).items():
            if base_files.get(rel) == v:
                continue
            src = os.path.join(px.cdir, rel)
            low = rel.lower()
            if not low.startswith(KEEP_DIRS) or os.path.islink(src):
                top = "/".join(rel.split("/")[:3])
                skipped[top] = skipped.get(top, 0) + 1
                continue
            dst = os.path.join(outc, rel)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            os.rename(src, dst)
        report["skipped"] = skipped
        for top, n in sorted(skipped.items()):
            log("not kept: %s (%d files)" % (top, n))

        delta = reg_delta(base_reg, final_reg)
        # [open_with] types keep the prefix's own default (Windows' handler):
        # drop whatever default an installer gave them
        for ext in conf.get("open_with", {}):
            for key, vals in delta.items():
                if key.lower() == ("software\\classes\\" + ext).lower() and "" in vals:
                    del vals[""]
                    log("association %s: installer default dropped ([open_with])" % ext)
        assoc, notes = associations(conf.get("associations", {}), deltas, final_reg,
                                    {x["id"]: x for x in apps}, conf.get("open_with", {}))
        for n in notes:
            log("association " + n)
        # setu-apps-register looks for this to tell whether a prefix has
        # this version of the entries
        assoc["Software\\Setu\\DefaultApps"] = {"Stamp": sz(stamp)}
        report["associations"] = notes
        # values naming the build user's profile would be wrong for anyone else
        userish = sorted(k for k, vals in delta.items()
                         for t, d in vals.values() if t in (1, 2, 7) and b"\\\x00u\x00s\x00e\x00r\x00s\x00\\\x00" in d.lower())
        report["user_paths"] = userish
        text = "Windows Registry Editor Version 5.00\n"
        text += "\n; The default apps' registry entries (distro/apps/stage.py)" + regedit_text(delta)
        text += "\n\n; Setu's default app per file type (apps.toml [associations])" + regedit_text(assoc) + "\n"
        with open(os.path.join(out, "apps.reg"), "wb") as f:
            f.write(b"\xff\xfe" + text.replace("\n", "\r\n").encode("utf-16-le"))
        report["registry_keys"] = len(delta)
        report["size"] = dir_size(outc)
        log("staged %d MiB of files, %d registry keys" % (report["size"] >> 20, len(delta)))
    finally:
        px.stop()
        if not a.keep_prefix:
            shutil.rmtree(work, ignore_errors=True)
        else:
            log("prefix kept in " + work)
    if not os.path.isdir(out):
        return 1
    with open(os.path.join(out, "report.json"), "w") as f:
        json.dump(report, f, indent=1, sort_keys=True)
    with open(os.path.join(out, "LICENSES.txt"), "w") as f:
        f.write(licences_text(apps))
    with open(os.path.join(out, "stamp"), "w") as f:
        f.write(stamp + "\n")
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
