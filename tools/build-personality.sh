#!/usr/bin/env bash
# SPDX-License-Identifier: MIT OR Apache-2.0
#
# Build the Setu personality (the Wine fork in personality/wine) out of tree
# and install it into a private prefix inside the repository:
#
#   build/personality/obj       object files (incremental rebuilds reuse them)
#   build/personality/install   installed tree; the oracle harness uses
#                               build/personality/install/bin/wine
#
# Usage: tools/build-personality.sh [--force] [-j N] [--clean]
#   --force   rebuild even if the installed build matches the source tree
#   --clean   delete build/personality first (full rebuild)
#   -j N      parallel jobs (default: nproc)
#
# Needs: gcc, make, flex, bison, x86_64-w64-mingw32-gcc and
# i686-w64-mingw32-gcc (new WoW64 mode builds 32-bit PE code with mingw and
# needs no 32-bit Linux libraries), plus whatever host -dev libraries you
# want Wine to use (X11, freetype, fontconfig, Wayland, ...): the list is
# tools/personality-build-deps.txt. Without root,
# tools/bootstrap-toolchain.sh provides the compilers, flex and bison.
#
# After installing the fork it runs tools/build-icu.sh, which adds ICU
# (icu.dll, icuuc.dll, icuin.dll; tools/icu/README.md) to the installed
# tree, and does nothing when ICU is already there and up to date. That
# needs the mingw C++ compilers too.
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$REPO/personality/wine"
OUT="$REPO/build/personality"
OBJ="$OUT/obj"
INSTALL="$OUT/install"
STAMP="$INSTALL/SETU_BUILD"
JOBS="$(nproc)"
FORCE=0

while [ $# -gt 0 ]; do
  case "$1" in
    --force) FORCE=1 ;;
    --clean) rm -rf "$OUT" ;;
    -j) JOBS="$2"; shift ;;
    -j*) JOBS="${1#-j}" ;;
    -h|--help) sed -n '3,20p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
  shift
done

[ -f "$SRC/configure" ] || { echo "no Wine source in $SRC" >&2; exit 1; }
for t in gcc g++ make flex bison x86_64-w64-mingw32-gcc i686-w64-mingw32-gcc \
         x86_64-w64-mingw32-g++ i686-w64-mingw32-g++; do
  command -v "$t" >/dev/null || { echo "missing build tool: $t (see tools/bootstrap-toolchain.sh)" >&2; exit 1; }
done

# Identify the source: the git tree hash of personality/wine, plus "-dirty"
# if it has uncommitted changes. CI also uses this as its cache key.
id="$(git -C "$REPO" rev-parse HEAD:personality/wine 2>/dev/null || echo unknown)"
if [ -n "$(git -C "$REPO" status --porcelain -- personality/wine 2>/dev/null)" ]; then
  id="$id-dirty"
fi
if [ "$FORCE" = 0 ] && [ -f "$STAMP" ] && [ "$(sed -n 's/^tree=//p' "$STAMP")" = "$id" ] \
   && [[ "$id" != *-dirty ]]; then
  echo "personality: up to date ($id), nothing to do (use --force to rebuild)"
  "$REPO/tools/build-icu.sh" --install "$INSTALL" -j "$JOBS"
  exit 0
fi

mkdir -p "$OBJ"
cd "$OBJ"
if [ ! -f Makefile ] || [ "$SRC/configure" -nt Makefile ]; then
  echo "personality: configuring (out of tree, $OBJ)"
  # --disable-tests: Wine's own conformance tests are not needed for a
  # runtime build, and skipping them roughly halves the build time.
  # Use ccache when it is installed (CI caches its directory between runs).
  cc=()
  if command -v ccache >/dev/null && [ "${SETU_CCACHE:-1}" != 0 ]; then
    cc=(CC="ccache gcc" x86_64_CC="ccache x86_64-w64-mingw32-gcc" i386_CC="ccache i686-w64-mingw32-gcc")
  fi
  "$SRC/configure" "${cc[@]}" \
    --prefix="$INSTALL" \
    --enable-archs=x86_64,i386 \
    --disable-tests \
    > "$OUT/configure.log" 2>&1 \
    || { tail -30 "$OUT/configure.log"; echo "configure failed: $OUT/configure.log" >&2; exit 1; }
  # Show which optional features are off because a -dev library is missing.
  grep -E '^configure: (WARNING|.*not found)' "$OUT/configure.log" | sed 's/^/  /' | head -40 || true
fi

echo "personality: building with -j$JOBS (log: $OUT/build.log)"
start=$SECONDS
if ! make -j"$JOBS" > "$OUT/build.log" 2>&1; then
  grep -nE 'error|Error' "$OUT/build.log" | tail -20
  echo "build failed: $OUT/build.log" >&2
  exit 1
fi
make install > "$OUT/install.log" 2>&1 \
  || { tail -20 "$OUT/install.log"; echo "install failed" >&2; exit 1; }

version="$("$INSTALL/bin/wine" --version 2>/dev/null || echo unknown)"
cat > "$STAMP" <<EOF
tree=$id
version=$version
built=$(date -u +%Y-%m-%dT%H:%M:%SZ)
EOF
echo "personality: $version installed in $INSTALL ($((SECONDS - start))s, tree $id)"
"$REPO/tools/build-icu.sh" --install "$INSTALL" -j "$JOBS"
