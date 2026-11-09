#!/usr/bin/env python3
# SPDX-License-Identifier: MIT OR Apache-2.0
"""Write the .def files for icu.dll, icuuc.dll and icuin.dll.

Usage: mkdefs.py EXPORTS.TXT ICU_SRC ICU_BUILD_DIR OUT_DIR NM

  EXPORTS.TXT    the documented names (tools/icu/exports.txt)
  ICU_SRC        ICU's source folder (common/unicode, i18n/unicode: headers)
  ICU_BUILD_DIR  a cross build of ICU (lib/libsicuuc.a, lib/libsicuin.a)
  OUT_DIR        gets icu.def, icuuc.def, icuin.def and exports.tsv
  NM             nm for that build's object format

icu.dll exports every documented name this ICU defines. icuuc.dll ("common")
and icuin.dll ("i18n") forward to it the names of their half of ICU that
ICU's headers mark as introduced in ICU OLD_LIBS_MAX or earlier: Microsoft
added no new functions to the two older DLLs after Windows 10 1903, which
brought icu.dll. OLD_LIBS_MAX is the newest ICU whose functions Windows 11's
icuuc.dll / icuin.dll still have (oracle test icu/icu_basic, case
old_dll_exports).
"""
import os
import re
import subprocess
import sys

OLD_LIBS_MAX = 59


def names(path):
    out = []
    for line in open(path):
        line = line.strip()
        if line and not line.startswith("#") and line not in out:
            out.append(line)
    return out


def defined(nm, lib):
    r = subprocess.run([nm, "-g", "--defined-only", lib], capture_output=True, text=True, check=True)
    syms = set()
    for line in r.stdout.splitlines():
        f = line.split()
        if len(f) == 3 and f[1] in "TDRB":
            syms.add(f[2].lstrip("_") if f[2].startswith("_") and not f[2].startswith("_Z") else f[2])
    return syms


DOC = re.compile(r"/\*\*(.*?)\*/(.*?)(?=/\*\*|\Z)", re.S)
TAG = re.compile(r"@(stable|draft|deprecated|internal|obsolete)\b(?:\s+ICU\s+(\d+))?")


def since_versions(src):
    """{function name: (tag, ICU major or None)} from the doc comment in
    front of each declaration in ICU's public headers."""
    # ICU 2.0 .. 4.8 count as 2 .. 4 here, below 49 and later.
    # - "@stable ICU 2.0 ... @deprecated ICU 63": 2 (the first release);
    # - only "@deprecated ICU 69": older than that, counted as 2;
    # - no version ("@internal"): the newest version the header names
    #   above it (it belongs with the functions around it), else 2.
    out = {}
    for sub in ("common", "i18n"):
        d = os.path.join(src, sub, "unicode")
        for h in sorted(os.listdir(d)):
            if not h.endswith(".h"):
                continue
            text = open(os.path.join(d, h), encoding="utf-8", errors="replace").read()
            newest = 2
            for m in DOC.finditer(text):
                comment, decl = m.group(1), m.group(2)
                tags = TAG.findall(comment)
                intro = [int(v) for t, v in tags if v and t in ("stable", "draft", "internal")]
                decl = decl.split(";", 1)[0]
                fm = re.search(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*\(", decl)
                if fm and tags and re.search(r"\bU_(CAPI|STABLE|DRAFT|INTERNAL|DEPRECATED)\b", decl):
                    if intro:
                        ver = min(intro)
                    elif any(v for t, v in tags):
                        ver = 2
                    else:
                        ver = newest
                    out.setdefault(fm.group(1), (tags[0][0], ver))
                if intro:
                    newest = max(newest, max(intro))
    return out


def main():
    exports, src, build, outdir, nm = sys.argv[1:6]
    uc = defined(nm, os.path.join(build, "lib", "libsicuuc.a"))
    i18n = defined(nm, os.path.join(build, "lib", "libsicuin.a"))
    since = since_versions(src)
    os.makedirs(outdir, exist_ok=True)
    rows, missing = [], []
    for n in names(exports):
        lib = "common" if n in uc else "i18n" if n in i18n else None
        if not lib:
            missing.append(n)
            continue
        tag, ver = since.get(n, ("?", None))
        old = ver is not None and ver <= OLD_LIBS_MAX
        rows.append((n, lib, tag, ver, old))
    with open(os.path.join(outdir, "icu.def"), "w") as f:
        f.write("LIBRARY icu.dll\nEXPORTS\n")
        for n, *_ in rows:
            f.write("    %s\n" % n)
    for dll, lib in (("icuuc", "common"), ("icuin", "i18n")):
        with open(os.path.join(outdir, dll + ".def"), "w") as f:
            f.write("LIBRARY %s.dll\nEXPORTS\n" % dll)
            for n, l, tag, ver, old in rows:
                if l == lib and old:
                    f.write("    %s = icu.%s\n" % (n, n))
    with open(os.path.join(outdir, "exports.tsv"), "w") as f:
        f.write("name\tlibrary\ttag\tsince\told_dlls\n")
        for n, l, tag, ver, old in rows:
            f.write("%s\t%s\t%s\t%s\t%s\n" % (n, l, tag, ver if ver is not None else "-", "yes" if old else "no"))
    nold = {lib: sum(1 for r in rows if r[1] == lib and r[4]) for lib in ("common", "i18n")}
    print("icu: exports: icu.dll %d, icuuc.dll %d, icuin.dll %d (documented names: %d)"
          % (len(rows), nold["common"], nold["i18n"], len(rows) + len(missing)))
    unknown = [r[0] for r in rows if r[3] is None]
    if unknown:
        print("icu: no ICU version in the headers (icu.dll only): " + " ".join(unknown))
    if missing:
        print("icu: documented but not in ICU: " + " ".join(missing))


if __name__ == "__main__":
    main()
