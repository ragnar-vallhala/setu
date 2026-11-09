#!/usr/bin/env bash
# SPDX-License-Identifier: MIT OR Apache-2.0
#
# Install a private mingw-w64 + Wine toolchain WITHOUT root, for Debian/Ubuntu
# (and derivatives such as Linux Mint). The .deb packages are downloaded with
# `apt-get download` and unpacked into $SETU_TOOLCHAIN (default
# ~/.local/setu-toolchain). Wrapper scripts are placed in $SETU_TOOLCHAIN/bin.
#
# If you have root, prefer:  sudo apt install gcc-mingw-w64-x86-64 wine64
#
# Usage: tools/bootstrap-toolchain.sh
#        export PATH="$HOME/.local/setu-toolchain/bin:$PATH"
set -euo pipefail

PREFIX="${SETU_TOOLCHAIN:-$HOME/.local/setu-toolchain}"
DEBS="$PREFIX/debs"
mkdir -p "$DEBS" "$PREFIX/bin"

command -v apt-get >/dev/null || { echo "apt-get not found: this script supports Debian/Ubuntu only" >&2; exit 1; }

PKGS=(
  # mingw-w64 cross compiler (posix threads variant)
  gcc-mingw-w64-x86-64-posix gcc-mingw-w64-x86-64-posix-runtime gcc-mingw-w64-base
  binutils-mingw-w64-x86-64 mingw-w64-x86-64-dev mingw-w64-common
  # Wine (64-bit) and its PE-side dependencies that are usually not installed
  wine64 libwine libz-mingw-w64
  # Needed to build personality/ (the Wine fork): 32-bit PE compiler for the
  # new WoW64 mode, plus the parser generators Wine's build uses
  gcc-mingw-w64-i686-posix gcc-mingw-w64-i686-posix-runtime
  binutils-mingw-w64-i686 mingw-w64-i686-dev
  # C++ compilers: ICU, which tools/build-icu.sh builds into the personality
  g++-mingw-w64-x86-64-posix g++-mingw-w64-i686-posix
  flex bison
)
echo "downloading: ${PKGS[*]}"
(cd "$DEBS" && apt-get download "${PKGS[@]}")
# Optional runtime libraries; ignore if unavailable on this release.
(cd "$DEBS" && apt-get download libcapi20-3t64 ocl-icd-libopencl1 2>/dev/null) || true

for d in "$DEBS"/*.deb; do dpkg -x "$d" "$PREFIX"; done

# libpcsclite's link library, for the personality's winscard.dll (the smart
# card API KeePassXC imports): kept in its own root, $PREFIX/pcsclite, so
# that tools/build-personality.sh can add just this library to the link
# path. At run time winscard.so loads the system's libpcsclite.so.1
# (distro/packages.list).
# Pinned to Ubuntu 24.04's version (what CI and the distro image use); other
# releases fall back to their own.
PCSC_VER=2.0.3-1build1
PCSC_DEBS="$PREFIX/debs-pcsclite"
mkdir -p "$PCSC_DEBS"
(cd "$PCSC_DEBS" && { apt-get download "libpcsclite-dev=$PCSC_VER" "libpcsclite1=$PCSC_VER" \
  || { echo "libpcsclite $PCSC_VER not available here; using this release's" >&2
       apt-get download libpcsclite-dev libpcsclite1; }; })
for d in "$PCSC_DEBS"/*.deb; do dpkg -x "$d" "$PREFIX/pcsclite"; done

U="$PREFIX/usr"
# Debian's wineserver wrapper hardcodes /usr/lib/wine; point it at our prefix.
sed -i "s#/usr/lib/wine/#$U/lib/wine/#g" "$U/lib/wine/wineserver"
# Debian builds Wine against a system zlib1.dll; put it where the PE loader looks.
# It ships as an absolute symlink into /usr, so replace the link itself.
ZDLL="$U/lib/x86_64-linux-gnu/wine/x86_64-windows/zlib1.dll"
rm -f "$ZDLL" && cp "$U/x86_64-w64-mingw32/lib/zlib1.dll" "$ZDLL"

# Wrappers on PATH.
ln -sf "$U/bin/x86_64-w64-mingw32-gcc-posix" "$PREFIX/bin/x86_64-w64-mingw32-gcc"
ln -sf "$U/bin/i686-w64-mingw32-gcc-posix" "$PREFIX/bin/i686-w64-mingw32-gcc"
ln -sf "$U/bin/x86_64-w64-mingw32-g++-posix" "$PREFIX/bin/x86_64-w64-mingw32-g++"
ln -sf "$U/bin/i686-w64-mingw32-g++-posix" "$PREFIX/bin/i686-w64-mingw32-g++"
for arch in x86_64 i686; do
  for tool in "$U"/bin/$arch-w64-mingw32-{ar,as,ld,nm,objcopy,strip,windres,dlltool,ranlib}; do
    [ -e "$tool" ] && ln -sf "$tool" "$PREFIX/bin/$(basename "$tool")"
  done
done
ln -sf "$U/bin/flex" "$PREFIX/bin/flex"
# Relocated bison must be told where its skeleton files are.
cat > "$PREFIX/bin/bison" <<EOF
#!/bin/sh
export BISON_PKGDATADIR="$U/share/bison"
exec "$U/bin/bison" "\$@"
EOF
chmod +x "$PREFIX/bin/bison"
cat > "$PREFIX/bin/wine" <<EOF
#!/bin/sh
export LD_LIBRARY_PATH="$U/lib/x86_64-linux-gnu\${LD_LIBRARY_PATH:+:\$LD_LIBRARY_PATH}"
export WINESERVER="$U/lib/wine/wineserver64"
# Relocated Debian Wine can't find its zlib1.dll from PE processes; give the
# prefix its own copy (needed by user32 / cmd.exe).
P="\${WINEPREFIX:-\$HOME/.wine}/drive_c/windows/system32"
if [ -d "\$P" ] && [ ! -e "\$P/zlib1.dll" ]; then cp "$U/x86_64-w64-mingw32/lib/zlib1.dll" "\$P/"; fi
exec "$U/lib/wine/wine64" "\$@"
EOF
cat > "$PREFIX/bin/wineserver" <<EOF
#!/bin/sh
export LD_LIBRARY_PATH="$U/lib/x86_64-linux-gnu\${LD_LIBRARY_PATH:+:\$LD_LIBRARY_PATH}"
exec "$U/lib/wine/wineserver64" "\$@"
EOF
chmod +x "$PREFIX/bin/wine" "$PREFIX/bin/wineserver"

echo
"$PREFIX/bin/x86_64-w64-mingw32-gcc" --version | head -1
"$PREFIX/bin/wine" --version
echo
echo "Toolchain ready. Add it to PATH:"
echo "  export PATH=\"$PREFIX/bin:\$PATH\""
