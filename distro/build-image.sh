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
#   --suite NAME        Debian suite (default: trixie, the current stable)
#   --mirror URL        Debian mirror (default: http://deb.debian.org/debian)
#   --size SIZE         disk size (default: 12G; the C: volume gets what the
#                       root file system doesn't)
#   --root-size SIZE    Linux root partition (default: 6GiB)
#   --out FILE          output image (default: build/distro/setu-dev.qcow2)
#   --cpus N, --mem MB  builder VM resources (default: 4, 4096)
#   --keep-work         keep build/distro/work after the build
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
ROOT_SIZE=6GiB
OUT=$REPO/build/distro/setu-dev.qcow2
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

BUILD=$REPO/build/distro
DL=$BUILD/downloads
WORK=$BUILD/work
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
qemu-img create -q -f qcow2 -F qcow2 -b "$DL/$CLOUD_IMG" "$WORK/builder.qcow2" 12G
qemu-img create -q -f raw "$WORK/target.raw" "$SIZE"

# Setu's own system files that live outside distro/ (the drive-letter
# service, services/fs), installed into a staging tree the builder copies.
make -s -C "$REPO/services/fs" install DESTDIR="$WORK/overlay" \
    || die "cannot build services/fs (needs mingw-w64: tools/bootstrap-toolchain.sh)"

cat > "$WORK/meta-data" <<EOF
instance-id: setu-builder-$(date +%s)
local-hostname: setu-builder
EOF
cat > "$WORK/user-data" <<EOF
#cloud-config
write_files:
  - path: /etc/setu-build.env
    content: |
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
        -virtfs "local,path=$WORK/overlay,mount_tag=overlay,security_model=none,readonly=on")
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

# 4. Publish the image (qcow2 keeps it small) and clean up.
qemu-img convert -O qcow2 "$WORK/target.raw" "$OUT.new"
mv "$OUT.new" "$OUT"
[ "$KEEP_WORK" = 1 ] || rm -rf "$WORK"
log "done in $(( $(date +%s) - start ))s: $OUT ($(du -h "$OUT" | cut -f1))"
log "boot it with: distro/run-qemu.sh"
