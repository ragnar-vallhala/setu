#!/bin/bash
# Build the Setu dev disk image (Milestone 1): Debian stable + labwc/Xwayland
# + the Setu personality, autologin into setu-session. Output is a UEFI
# bootable qcow2 for distro/run-qemu.sh.
#
# Rootless: the host needs no root, no subuid helpers (newuidmap) and no
# fakechroot. The image is built by mmdebstrap running as root inside a
# throwaway KVM "builder" VM (the official Debian "generic" cloud image, driven by
# cloud-init). Inputs reach the VM over read-only 9p shares, the output is a raw
# disk attached to it. Every run starts from a fresh builder, so the build is
# idempotent; the finished image replaces the old one only on success.
#
# Usage: distro/build-image.sh [options]
#   --wine=setu|debian  setu (default): copy the Setu personality build into
#                       /opt/setu/personality. debian: use Debian's wine packages.
#   --personality DIR   Setu personality install dir
#                       (default: <main checkout>/build/personality/install)
#   --shell FILE        setu-shell.exe to install as /opt/setu/shell/setu-shell.exe
#                       (setu-files.exe, setu-theme.exe and setu-probe.exe
#                       next to it come along)
#   --suite NAME        Debian suite (default: trixie, the current stable)
#   --mirror URL        Debian mirror (default: http://deb.debian.org/debian)
#   --size SIZE         disk size (default: 12G; the C: volume gets what the
#                       root file system doesn't)
#   --root-size SIZE    Linux root partition (default: SETU_ROOT_SIZE in
#                       distro/layout.env, 6GiB)
#   --out FILE          output image (default: build/distro/setu-dev.qcow2,
#                       with --kind iso build/distro/setu-installer.iso)
#   --kind image|iso    image (default): the dev disk image. iso: the
#                       installer ISO instead (distro/build-iso.sh runs this)
#   --cpus N, --mem MB  builder VM resources (default: 4, 4096)
#   --keep-work         keep build/distro/work-KIND after the build
#   -h, --help          show this help
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/.." && pwd)
# The main checkout (worktrees share its build/personality output).
MAIN=$(cd "$(git -C "$REPO" rev-parse --git-common-dir 2>/dev/null)/.." 2>/dev/null && pwd || echo "$REPO")

WINE_MODE=setu
PERSONALITY=""
SHELL_EXE=""
SUITE=trixie
MIRROR=http://deb.debian.org/debian
SIZE=12G
ROOT_SIZE=
OUT=
KIND=image
CPUS=4
MEM=4096
KEEP_WORK=0

usage() { sed -n '2,/^set -euo/{/^set -euo/d;s/^# \{0,1\}//;p}' "$0"; }
while [ $# -gt 0 ]; do
    case $1 in
        --wine=*) WINE_MODE=${1#*=} ;;
        --wine) WINE_MODE=$2; shift ;;
        --personality) PERSONALITY=$2; shift ;;
        --shell) SHELL_EXE=$2; shift ;;
        --suite) SUITE=$2; shift ;;
        --mirror) MIRROR=$2; shift ;;
        --size) SIZE=$2; shift ;;
        --root-size) ROOT_SIZE=$2; shift ;;
        --out) OUT=$2; shift ;;
        --kind) KIND=$2; shift ;;
        --cpus) CPUS=$2; shift ;;
        --mem) MEM=$2; shift ;;
        --keep-work) KEEP_WORK=1 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "unknown option: $1 (see --help)" >&2; exit 2 ;;
    esac
    shift
done

log() { echo "[build-image] $*"; }
die() { echo "[build-image] error: $*" >&2; exit 1; }

case $WINE_MODE in setu|debian) ;; *) die "--wine must be setu or debian" ;; esac
case $KIND in
    image) OUT=${OUT:-$REPO/build/distro/setu-dev.qcow2}; BUILDER_SIZE=12G ;;
    # the ISO kind keeps the live tree, its squashfs and the ISO on the
    # builder's disk at the same time; the target disk only receives the ISO
    iso) OUT=${OUT:-$REPO/build/distro/setu-installer.iso}; BUILDER_SIZE=24G; SIZE=8G ;;
    *) die "--kind must be image or iso" ;;
esac
if [ "$WINE_MODE" = setu ]; then
    PERSONALITY=${PERSONALITY:-$MAIN/build/personality/install}
    [ -x "$PERSONALITY/bin/wine" ] || die "no personality build at $PERSONALITY (build it, pass --personality, or use --wine=debian)"
    PERSONALITY=$(cd "$PERSONALITY" && pwd)
fi
if [ -n "$SHELL_EXE" ]; then
    [ -f "$SHELL_EXE" ] || die "--shell: $SHELL_EXE not found"
    SHELL_EXE=$(cd "$(dirname "$SHELL_EXE")" && pwd)/$(basename "$SHELL_EXE")
fi
for t in qemu-system-x86_64 qemu-img curl sha512sum mkfs.vfat mcopy make; do
    command -v $t >/dev/null || die "missing host tool: $t"
done
[ -w /dev/kvm ] || die "/dev/kvm is not writable (the builder VM needs KVM)"
# distro/themes/render-packs.py runs on the host
python3 -c 'import numpy, PIL, cairo, gi; gi.require_version("Rsvg", "2.0"); from gi.repository import Rsvg' 2>/dev/null \
    || die "missing host Python modules for the theme packs: numpy, PIL, cairo, gi with Rsvg 2.0 (Debian: python3-numpy python3-pil python3-cairo python3-gi gir1.2-rsvg-2.0)"

BUILD=$REPO/build/distro
DL=$BUILD/downloads
WORK=$BUILD/work-$KIND
mkdir -p "$DL" "$(dirname "$OUT")"

# 1. Builder base image: Debian's official "generic" cloud image, verified
#    (not genericcloud: its kernel has no 9p).
CLOUD_URL=https://cloud.debian.org/images/cloud/$SUITE/latest
CLOUD_IMG=$(curl -sSfL "$CLOUD_URL/SHA512SUMS" -o "$DL/SHA512SUMS.new" \
    && grep -o 'debian-[0-9]*-generic-amd64\.qcow2$' "$DL/SHA512SUMS.new" | head -1) \
    || die "cannot fetch $CLOUD_URL/SHA512SUMS (network?)"
mv "$DL/SHA512SUMS.new" "$DL/SHA512SUMS"
if ! (cd "$DL" && grep " $CLOUD_IMG\$" SHA512SUMS | sha512sum -c --quiet 2>/dev/null); then
    log "downloading $CLOUD_IMG"
    curl -sSfL "$CLOUD_URL/$CLOUD_IMG" -o "$DL/$CLOUD_IMG.part"
    mv "$DL/$CLOUD_IMG.part" "$DL/$CLOUD_IMG"
    (cd "$DL" && grep " $CLOUD_IMG\$" SHA512SUMS | sha512sum -c --quiet) || die "checksum mismatch for $CLOUD_IMG"
fi

# 2. Fresh work dir: builder overlay, cloud-init seed, target disk.
rm -rf "$WORK"
mkdir -p "$WORK"
qemu-img create -q -f qcow2 -F qcow2 -b "$DL/$CLOUD_IMG" "$WORK/builder.qcow2" $BUILDER_SIZE
qemu-img create -q -f raw "$WORK/target.raw" "$SIZE"

# Setu's own system files that live outside distro/ (the drive-letter
# service, services/fs), installed into a staging tree the builder copies.
make -s -C "$REPO/services/fs" install DESTDIR="$WORK/overlay" \
    || die "cannot build services/fs (needs mingw-w64: tools/bootstrap-toolchain.sh)"

# Wine Mono, the .NET runtime Wine 11.0's mscoree loads (the version in
# personality/wine/dlls/appwiz.cpl/addons.c), unpacked into the
# personality's shared data dir: mscoree finds it there
# (<datadir>/wine/mono/wine-mono-VERSION) and new prefixes only register
# its small support MSI, without asking to download anything. The tarball
# has the same files as the MSI whose SHA-256 Wine pins (checked once, by
# hand); it is cached in build/distro/downloads and never committed.
# Licence: NOTICE.
MONO_VERSION=10.4.1
MONO_TAR=wine-mono-$MONO_VERSION-x86.tar.xz
MONO_SHA256=a16606ef0724202e6a6848ece6e0cbba64d11e2f11aefe744af1d93c6d9f99bb
if [ "$WINE_MODE" = setu ]; then
    if ! echo "$MONO_SHA256  $DL/$MONO_TAR" | sha256sum -c --quiet 2>/dev/null; then
        log "downloading $MONO_TAR"
        curl -sSfL "https://dl.winehq.org/wine/wine-mono/$MONO_VERSION/$MONO_TAR" -o "$DL/$MONO_TAR.part" \
            || die "cannot download $MONO_TAR (network?)"
        echo "$MONO_SHA256  $DL/$MONO_TAR.part" | sha256sum -c --quiet || die "checksum mismatch for $MONO_TAR"
        mv "$DL/$MONO_TAR.part" "$DL/$MONO_TAR"
    fi
    mkdir -p "$WORK/overlay/opt/setu/personality/share/wine/mono"
    tar -xJf "$DL/$MONO_TAR" -C "$WORK/overlay/opt/setu/personality/share/wine/mono"
fi

# The theme packs (distro/themes, docs/THEMES.md): every pack with its
# .theme, icons, wallpaper, control theme and boot splash, the icon sets,
# cursors and sounds (art/), labwc's default config and the installer's
# branding, rendered on the host into the overlay.
log "rendering the theme packs"
packs_start=$(date +%s)
pack_args=()
[ "$WINE_MODE" = setu ] && pack_args+=(--personality "$PERSONALITY" --wine-bin "$PERSONALITY/bin")
python3 "$HERE/themes/render-packs.py" --out "$WORK/overlay" "${pack_args[@]}" \
    || die "cannot render the theme packs"
log "  $(ls "$WORK/overlay/usr/share/setu/themes" | wc -l) packs, $(du -sh "$WORK/overlay/usr/share/setu" | cut -f1), in $(( $(date +%s) - packs_start ))s"

# The wallpaper collection is drawn in the builder (build-rootfs.sh) into a
# cache keyed by wallgen.py, so a build with the same generator reuses it.
WALLGEN=$REPO/art/wallpapers/wallgen.py
WPCACHE=$BUILD/wallpaper-cache/$(sha256sum "$WALLGEN" | cut -c1-16)
for d in "$BUILD"/wallpaper-cache/*; do
    [ -d "$d" ] && [ "$d" != "$WPCACHE" ] && rm -rf "$d"  # an older wallgen.py's
done
mkdir -p "$WPCACHE"
cp "$WALLGEN" "$WPCACHE/wallgen.py"

cat > "$WORK/meta-data" <<EOF
instance-id: setu-builder-$(date +%s)
local-hostname: setu-builder
EOF
cat > "$WORK/user-data" <<EOF
#cloud-config
write_files:
  - path: /etc/setu-build.env
    content: |
      KIND=$KIND
      SUITE=$SUITE
      MIRROR=$MIRROR
      WINE_MODE=$WINE_MODE
      IMAGE_HOSTNAME=setu
      ROOT_SIZE=$ROOT_SIZE
      HAVE_SHELL=$([ -n "$SHELL_EXE" ] && echo 1 || echo 0)
runcmd:
  - [sh, -c, "mkdir -p /mnt/distro && mount -t 9p -o trans=virtio,version=9p2000.L,ro distro /mnt/distro && bash /mnt/distro/builder/build-rootfs.sh > /dev/ttyS1 2>&1 || echo SETU_BUILD_FAILED > /dev/ttyS1; sync; poweroff -f"]
EOF
mkfs.vfat -n CIDATA -C "$WORK/seed.img" 1024 >/dev/null
mcopy -i "$WORK/seed.img" "$WORK/user-data" "$WORK/meta-data" ::

# 3. Run the builder VM until it powers itself off.
SHARES=(-virtfs "local,path=$HERE,mount_tag=distro,security_model=none,readonly=on"
        -virtfs "local,path=$WORK/overlay,mount_tag=overlay,security_model=none,readonly=on"
        -virtfs "local,path=$WPCACHE,mount_tag=wpcache,security_model=none")
[ "$WINE_MODE" = setu ] && SHARES+=(-virtfs "local,path=$PERSONALITY,mount_tag=personality,security_model=none,readonly=on")
[ -n "$SHELL_EXE" ] && SHARES+=(-virtfs "local,path=$(dirname "$SHELL_EXE"),mount_tag=shell,security_model=none,readonly=on")

# Build output goes to the VM's second serial port (ttyS1): the first one
# carries the VM console and a getty, which hangs up the port on start.
log "building in VM (log: $WORK/build.log)"
start=$(date +%s)
qemu-system-x86_64 -enable-kvm -cpu host -smp "$CPUS" -m "$MEM" \
    -display none -no-reboot \
    -serial "file:$WORK/builder-console.log" -serial "file:$WORK/build.log" -monitor none \
    -drive "file=$WORK/builder.qcow2,if=none,id=builder,format=qcow2" \
    -device virtio-blk-pci,drive=builder,bootindex=0 \
    -drive "file=$WORK/seed.img,if=virtio,format=raw,readonly=on" \
    -drive "file=$WORK/target.raw,if=none,id=target,format=raw" \
    -device virtio-blk-pci,drive=target,serial=setu-target \
    -nic user,model=virtio-net-pci \
    "${SHARES[@]}"
cp "$WORK/build.log" "$BUILD/last-build.log"
grep -a '^\[setu-build\]' "$WORK/build.log" | sed 's/\r$//' || true
grep -aq '^SETU_BUILD_OK' "$WORK/build.log" \
    || die "build failed; see $BUILD/last-build.log"

# 4. Publish the image (qcow2 keeps it small; the ISO is the start of the
#    raw disk) and clean up.
if [ "$KIND" = iso ]; then
    iso_size=$(grep -a '^SETU_ISO_SIZE=' "$WORK/build.log" | tr -d '\r' | cut -d= -f2)
    [ -n "$iso_size" ] || die "the builder reported no ISO size"
    truncate -s "$iso_size" "$WORK/target.raw"
    mv "$WORK/target.raw" "$OUT"
else
    qemu-img convert -O qcow2 "$WORK/target.raw" "$OUT.new"
    mv "$OUT.new" "$OUT"
fi
[ "$KEEP_WORK" = 1 ] || rm -rf "$WORK"
log "done in $(( $(date +%s) - start ))s: $OUT ($(du -h --apparent-size "$OUT" | cut -f1))"
if [ "$KIND" = iso ]; then
    log "try it with: distro/tests/install-smoke.sh"
else
    log "boot it with: distro/run-qemu.sh"
fi
