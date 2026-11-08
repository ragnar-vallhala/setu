#!/usr/bin/env python3
# SPDX-License-Identifier: MIT OR Apache-2.0
"""Download the default apps (distro/apps/apps.toml) into the cache.

Each file is fetched from its pinned official URL into
build/distro/downloads/apps/<id>-<version><ext>, and kept only when its
sha256 matches the manifest. A cached file that matches is not fetched
again. Nothing here is ever committed.

Usage: distro/apps/fetch.py [--cache DIR] [--pin] [-a ID]...
  --pin   for apps whose sha256 is empty in the manifest, download and print
          the sha256 and size to paste into apps.toml (check them against
          the vendor's published checksum first, when there is one)
  -a ID   only these apps
Exit status: 0 all files present and verified, 1 otherwise.
"""

import argparse
import hashlib
import os
import sys
import tomllib
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
MANIFEST = os.path.join(HERE, "apps.toml")
CACHE = os.path.join(REPO, "build", "distro", "downloads", "apps")
UA = "Setu-image-build/1 (+https://github.com/ragnar-vallhala/setu)"


def load(path=MANIFEST):
    with open(path, "rb") as f:
        return tomllib.load(f)


def cache_name(app):
    ext = {"msi": ".msi", "zip": ".zip"}.get(app["kind"], ".exe")
    return "%s-%s%s" % (app["id"], app["version"], ext)


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def download(url, dest):
    tmp = dest + ".part"
    req = urllib.request.Request(url, headers={"User-Agent": UA})
    with urllib.request.urlopen(req, timeout=120) as r, open(tmp, "wb") as f:
        while True:
            b = r.read(1 << 20)
            if not b:
                break
            f.write(b)
    return tmp


def fetch(app, cache, pin=False):
    """Returns (path or None, message)."""
    os.makedirs(cache, exist_ok=True)
    dest = os.path.join(cache, cache_name(app))
    want = app.get("sha256", "")
    if want and os.path.exists(dest) and sha256(dest) == want:
        return dest, "cached"
    if not want and not pin:
        return None, "no sha256 pinned (run with --pin)"
    try:
        tmp = download(app["url"], dest)
    except OSError as e:
        return None, "download failed: %s" % e
    got = sha256(tmp)
    if not want:
        os.replace(tmp, dest)
        return dest, 'PIN sha256 = "%s"  size = %d' % (got, os.path.getsize(dest))
    if got != want:
        os.unlink(tmp)
        return None, "sha256 mismatch: got %s" % got
    os.replace(tmp, dest)
    return dest, "downloaded"


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--cache", default=CACHE)
    ap.add_argument("--pin", action="store_true")
    ap.add_argument("-a", dest="only", action="append", default=[])
    a = ap.parse_args()
    ok = True
    for app in load()["app"]:
        if a.only and app["id"] not in a.only:
            continue
        path, msg = fetch(app, a.cache, a.pin)
        print("%-17s %-12s %s" % (app["id"], app["version"], msg))
        ok = ok and path is not None and not msg.startswith("PIN")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
