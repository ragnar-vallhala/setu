#!/usr/bin/env bash
# SPDX-License-Identifier: MIT OR Apache-2.0
#
# Build ICU (International Components for Unicode, https://icu.unicode.org)
# from the upstream source release as the system DLLs Windows ships with it
# (https://learn.microsoft.com/windows/win32/intl/international-components-for-unicode--icu-):
#
#   icu.dll     the combined "common" + "i18n" library (Windows 10 1903+)
#   icuuc.dll   the older "common" library; forwards to icu.dll
#   icuin.dll   the older "i18n" library; forwards to icu.dll
#
# for x86_64 (system32) and i386 (syswow64), and installs them into the
# personality's PE library folders (lib/wine/<arch>-windows), from where
# wineboot puts them in every prefix's system32 and syswow64.
#
# As on Windows, only the C API is exported, under the plain names (ICU's
# version-suffix renaming is off): the names Microsoft's ICU page lists,
# tools/icu/exports.txt. icuuc.dll and icuin.dll export the names that
# were there before Windows 10 1903; later ones are only in icu.dll (see
# tools/icu/README.md).
#
# ICU's data (locales, collation, break rules, converters...) is the
# release's prebuilt icudt<N>l.dat, linked once into the 64-bit icu.dll
# (section .icudata). The 32-bit icu.dll has no copy of its own: it maps
# the 64-bit DLL from the native system folder as a plain file and hands
# ICU that section (tools/icu/icudata.c).
#
# Usage: tools/build-icu.sh [--install DIR] [-j N] [--force]
#   --install DIR  personality install tree (default: build/personality/install)
#   -j N           parallel jobs (default 3)
#   --force        rebuild even if the stamp matches
# Needs: gcc/g++ for the host (ICU's data tools), x86_64-w64-mingw32-g++
# and i686-w64-mingw32-g++ (tools/bootstrap-toolchain.sh), curl, python3.
# Work tree: build/icu (the download is cached in build/downloads/icu).
set -euo pipefail

ICU_VERSION=72.1
ICU_TAR=icu4c-72_1-src.tgz
ICU_URL=https://github.com/unicode-org/icu/releases/download/release-72-1/$ICU_TAR
# Checked against the release's SHASUM512.txt (sha512 848c341b...518dcae8).
ICU_SHA256=a2d2d38217092a7ed56635e34467f92f976b370e20182ad325edea6681a71d68
ICU_SIZE=26303933
ICU_MAJOR=${ICU_VERSION%%.*}

REPO="$(cd "$(dirname "$0")/.." && pwd)"
HERE="$REPO/tools/icu"
INSTALL="$REPO/build/personality/install"
WORK="$REPO/build/icu"
DL="$REPO/build/downloads/icu"
JOBS=3
FORCE=0
while [ $# -gt 0 ]; do
  case "$1" in
    --install) INSTALL="$2"; shift ;;
    -j) JOBS="$2"; shift ;;
    -j*) JOBS="${1#-j}" ;;
    --force) FORCE=1 ;;
    -h|--help) sed -n '3,36p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
  shift
done
log() { echo "icu: $*"; }
die() { echo "icu: $*" >&2; exit 1; }

cxx_for() {  # the posix-threads g++ when there is one (static winpthread)
  if command -v "$1-w64-mingw32-g++-posix" >/dev/null; then echo "$1-w64-mingw32-g++-posix"
  elif command -v "$1-w64-mingw32-g++" >/dev/null; then echo "$1-w64-mingw32-g++"
  else die "missing $1-w64-mingw32-g++ (tools/bootstrap-toolchain.sh)"; fi
}
cc_for() {
  if command -v "$1-w64-mingw32-gcc-posix" >/dev/null; then echo "$1-w64-mingw32-gcc-posix"
  else echo "$1-w64-mingw32-gcc"; fi
}
for t in gcc g++ make curl python3 x86_64-w64-mingw32-gcc i686-w64-mingw32-gcc; do
  command -v "$t" >/dev/null || die "missing build tool: $t"
done

# Everything that decides the output; the installed DLLs are rebuilt when it changes.
stamp_id() {
  { echo "$ICU_SHA256"; cat "$0"; for f in "$HERE"/*; do [ -f "$f" ] && cat "$f"; done
    "$(cxx_for x86_64)" --version | head -1
    "$(cxx_for i686)" --version | head -1; } | sha256sum | cut -c1-16
}
ID=$(stamp_id)
installed() {
  for a in x86_64 i386; do
    for d in icu icuuc icuin; do [ -f "$INSTALL/lib/wine/$a-windows/$d.dll" ] || return 1; done
  done
  [ "$(cat "$INSTALL/lib/wine/x86_64-windows/icu.stamp" 2>/dev/null)" = "$ID" ]
}
if [ "$FORCE" = 0 ] && installed; then
  log "up to date ($ID), nothing to do"
  exit 0
fi

# --- download and unpack ------------------------------------------------------
mkdir -p "$DL"
if ! echo "$ICU_SHA256  $DL/$ICU_TAR" | sha256sum -c --quiet 2>/dev/null; then
  log "downloading $ICU_TAR"
  curl -sSfL "$ICU_URL" -o "$DL/$ICU_TAR.part" || die "cannot download $ICU_URL"
  [ "$(stat -c %s "$DL/$ICU_TAR.part")" = "$ICU_SIZE" ] || die "size mismatch for $ICU_TAR"
  echo "$ICU_SHA256  $DL/$ICU_TAR.part" | sha256sum -c --quiet || die "checksum mismatch for $ICU_TAR"
  mv "$DL/$ICU_TAR.part" "$DL/$ICU_TAR"
fi
# The unpacked source and the ICU builds are kept between runs while the
# source (tarball + patches) and the toolchain stay the same.
SRC_ID=$({ echo "$ICU_SHA256"; cat "$HERE"/*.patch 2>/dev/null; "$(cxx_for x86_64)" --version | head -1
           "$(cxx_for i686)" --version | head -1; } | sha256sum | cut -c1-16)
SRC="$WORK/src/icu/source"
if [ "$(cat "$WORK/src.id" 2>/dev/null)" != "$SRC_ID" ]; then
  rm -rf "$WORK"
  mkdir -p "$WORK/src"
  tar -xzf "$DL/$ICU_TAR" -C "$WORK/src"
  for p in "$HERE"/*.patch; do
    [ -e "$p" ] || continue
    log "patch $(basename "$p")"
    patch -d "$WORK/src/icu" -p1 -s < "$p"
  done
  echo "$SRC_ID" > "$WORK/src.id"
fi
rm -rf "$WORK/defs" "$WORK/out"
DATA="$SRC/data/in/icudt${ICU_MAJOR}l.dat"
[ -f "$DATA" ] || die "no prebuilt data in the release ($DATA)"

COMMON_OPTS=(--disable-renaming --disable-extras --disable-tests --disable-samples
             --disable-icuio --disable-layoutex)

# --- host build: ICU's tools, which the cross build needs ----------------------
if [ ! -f "$WORK/host/.done" ]; then
  log "host build (tools) with -j$JOBS"
  rm -rf "$WORK/host"
  mkdir -p "$WORK/host"
  (cd "$WORK/host" && "$SRC/configure" "${COMMON_OPTS[@]}" > configure.log 2>&1) \
    || { tail -20 "$WORK/host/configure.log"; die "host configure failed"; }
  make -C "$WORK/host" -j"$JOBS" > "$WORK/host/make.log" 2>&1 \
    || { tail -20 "$WORK/host/make.log"; die "host build failed ($WORK/host/make.log)"; }
  touch "$WORK/host/.done"
fi

# --- the PE libraries ----------------------------------------------------------
# Static libraries (no dllexport anywhere); the DLLs are linked below with a
# .def file, so they export exactly the documented C names.
for arch in x86_64 i686; do
  b="$WORK/$arch"
  [ -f "$b/.done" ] && continue
  rm -rf "$b"
  mkdir -p "$b"
  log "$arch: configure and build common + i18n with -j$JOBS"
  (cd "$b" && CC="$(cc_for $arch)" CXX="$(cxx_for $arch)" \
     CFLAGS="-O2" CXXFLAGS="-O2" \
     "$SRC/configure" --host=$arch-w64-mingw32 --with-cross-build="$WORK/host" \
       "${COMMON_OPTS[@]}" --disable-tools --enable-static --disable-shared \
       --with-data-packaging=static > configure.log 2>&1) \
    || { tail -20 "$b/configure.log"; die "$arch configure failed"; }
  mkdir -p "$b/lib" "$b/bin"
  for d in stubdata common i18n; do
    make -C "$b/$d" -j"$JOBS" > "$b/make-$d.log" 2>&1 \
      || { tail -20 "$b/make-$d.log"; die "$arch $d build failed ($b/make-$d.log)"; }
  done
  touch "$b/.done"
done

# --- export lists ----------------------------------------------------------------
# tools/icu/exports.txt: the documented names, each with the ICU version it
# first appeared in (from ICU's own header annotations; tools/icu/README.md).
# icuuc/icuin get the names up to OLD_LIBS_MAX; icu.dll gets all of them.
# Names this ICU does not define are left out (and listed in the log).
python3 -I "$HERE/mkdefs.py" "$HERE/exports.txt" "$SRC" "$WORK/x86_64" "$WORK/defs" \
  "$(command -v x86_64-w64-mingw32-nm || echo nm)" || die "export lists failed"

# --- link -----------------------------------------------------------------------
for arch in x86_64 i686; do
  b="$WORK/$arch"
  o="$WORK/out/$arch"
  mkdir -p "$o"
  cxx="$(cxx_for $arch)"
  cc="$(cc_for $arch)"
  objs=("$o/icudata.o")
  "$cc" -O2 -Wall -Wextra -Werror -c "$HERE/icudata.c" -o "$o/icudata.o" \
    -I"$SRC/common" -I"$b/common" -DU_STATIC_IMPLEMENTATION -DU_DISABLE_RENAMING=1 $([ $arch = x86_64 ] && echo -DICU_DATA_EMBEDDED)
  if [ $arch = x86_64 ]; then
    # The data, once: a 16-byte aligned section that the 32-bit icu.dll
    # finds by name, with the symbol ICU's static data packaging expects.
    printf '\t.section .icudata,"dr"\n\t.balign 16\n\t.globl icudt%s_dat\nicudt%s_dat:\n\t.incbin "%s"\n' \
      "$ICU_MAJOR" "$ICU_MAJOR" "$DATA" > "$o/data.S"
    "$cc" -c "$o/data.S" -o "$o/data.o"
    objs+=("$o/data.o")
    lib_dt=()
  else
    lib_dt=("$b/stubdata/libsicudt.a")
  fi
  # .def + static archives: the linker pulls in what the exports need.
  "$cxx" -shared -s -o "$o/icu.dll" "$WORK/defs/icu.def" "${objs[@]}" \
    "$b/lib/libsicuin.a" "$b/lib/libsicuuc.a" "${lib_dt[@]}" \
    -static -static-libgcc -static-libstdc++ -Wl,--no-insert-timestamp -Wl,--enable-stdcall-fixup \
    > "$o/link.log" 2>&1 || { tail -30 "$o/link.log"; die "$arch icu.dll link failed"; }
  entry=DllMain; [ $arch = i686 ] && entry=_DllMain@12
  for l in icuuc icuin; do
    "$cc" -shared -s -nostdlib -o "$o/$l.dll" "$HERE/forward.c" "$WORK/defs/$l.def" \
      -Wl,-e,$entry -Wl,--no-insert-timestamp > "$o/$l-link.log" 2>&1 \
      || { cat "$o/$l-link.log"; die "$arch $l.dll link failed"; }
  done
done

# --- install ------------------------------------------------------------------------
[ -d "$INSTALL/lib/wine/x86_64-windows" ] || die "no personality install in $INSTALL (tools/build-personality.sh)"
WINEBUILD="$INSTALL/bin/winebuild"
[ -x "$WINEBUILD" ] || die "no winebuild in $INSTALL/bin"
for arch in x86_64 i686; do
  w=$arch; [ $arch = i686 ] && w=i386
  dest="$INSTALL/lib/wine/$w-windows"
  for d in icu icuuc icuin; do
    # The "Wine builtin DLL" mark is what makes wineboot copy a DLL from
    # here into system32 / syswow64 (dlls/setupapi/fakedll.c).
    "$WINEBUILD" --builtin "$WORK/out/$arch/$d.dll"
    install -m 644 "$WORK/out/$arch/$d.dll" "$dest/$d.dll"
  done
done
mkdir -p "$INSTALL/share/wine/icu"
install -m 644 "$WORK/src/icu/LICENSE" "$INSTALL/share/wine/icu/LICENSE"
echo "$ID" > "$INSTALL/lib/wine/x86_64-windows/icu.stamp"
log "installed ICU $ICU_VERSION into $INSTALL/lib/wine ($ID):"
(cd "$INSTALL/lib/wine" && ls -l x86_64-windows/icu*.dll i386-windows/icu*.dll | awk '{printf "  %-28s %9d bytes\n", $NF, $5}')
