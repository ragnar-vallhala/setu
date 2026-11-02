#!/bin/bash
# Runs as root INSIDE the throwaway builder VM started by distro/build-image.sh.
# Partitions the target disk (GPT: ESP + ext4 root), builds the Debian root
# filesystem into it with mmdebstrap, adds the Setu personality and session
# config, and installs GRUB (EFI, removable path).
#
# Inputs (set up by build-image.sh):
#   /etc/setu-build.env      SUITE MIRROR WINE_MODE IMAGE_HOSTNAME HAVE_SHELL
#   9p tag "distro"          the repo's distro/ directory (read-only)
#   9p tag "personality"     build/personality/install (read-only, WINE_MODE=setu)
#   9p tag "shell"           directory with setu-shell.exe (HAVE_SHELL=1)
#   virtio disk serial "setu-target"   the output disk (raw)
# Output goes to the second serial port; its last line is SETU_BUILD_OK on success.
set -euo pipefail
. /etc/setu-build.env

log() { echo "[setu-build] $*"; }
export DEBIAN_FRONTEND=noninteractive
DISTRO=/mnt/distro
T=/mnt/target
DISK=/dev/disk/by-id/virtio-setu-target

mount_9p() { # tag dir
    mkdir -p "$2"
    mount -t 9p -o trans=virtio,version=9p2000.L,ro,msize=1048576 "$1" "$2"
}

log "installing builder tools"
apt-get update -qq
apt-get install -y -q --no-install-recommends mmdebstrap dosfstools e2fsprogs fdisk

mountpoint -q $DISTRO || mount_9p distro $DISTRO

log "partitioning $DISK"
udevadm settle
wipefs -a "$DISK"
# 4f68bce3-... is the "Linux root (x86-64)" type from the Discoverable
# Partitions Specification.
sfdisk -q "$DISK" <<'EOF'
label: gpt
size=256MiB, type=uefi, name="ESP"
type=4f68bce3-e8cd-4db1-96e7-fbcaf984b709, name="setu-root"
EOF
udevadm settle
ESP=${DISK}-part1
ROOT=${DISK}-part2
mkfs.vfat -F 32 -n SETU-ESP "$ESP" >/dev/null
mkfs.ext4 -q -F -L setu-root "$ROOT"
mkdir -p $T
mount "$ROOT" $T

pkgs=$(sed 's/#.*//' $DISTRO/packages.list | xargs | tr ' ' ',')
if [ "$WINE_MODE" = debian ]; then
    pkgs="$pkgs,$(sed 's/#.*//' $DISTRO/packages-wine-debian.list | xargs | tr ' ' ',')"
fi

log "mmdebstrap $SUITE from $MIRROR (wine: $WINE_MODE)"
mmdebstrap --variant=minbase --include="$pkgs" \
    --aptopt='Acquire::Retries "3"' \
    --dpkgopt='path-exclude=/usr/share/doc/*' \
    --dpkgopt='path-include=/usr/share/doc/*/copyright' \
    --dpkgopt='path-exclude=/usr/share/man/*' \
    "$SUITE" $T \
    "deb $MIRROR $SUITE main" \
    "deb $MIRROR $SUITE-updates main" \
    "deb http://security.debian.org/debian-security $SUITE-security main"

log "configuring"
mkdir -p $T/boot/efi
mount "$ESP" $T/boot/efi
for d in dev proc sys run; do mount --rbind /$d $T/$d; mount --make-rslave $T/$d; done

cp -a --no-preserve=ownership $DISTRO/rootfs/. $T/
echo "$IMAGE_HOSTNAME" > $T/etc/hostname
printf '127.0.0.1\tlocalhost\n127.0.1.1\t%s\n' "$IMAGE_HOSTNAME" > $T/etc/hosts
cat > $T/etc/fstab <<EOF
UUID=$(blkid -s UUID -o value "$ROOT")  /          ext4  errors=remount-ro  0 1
UUID=$(blkid -s UUID -o value "$ESP")  /boot/efi  vfat  umask=0077         0 2
EOF

if [ "$WINE_MODE" = setu ]; then
    log "copying the Setu personality to /opt/setu/personality"
    mount_9p personality /mnt/personality
    mkdir -p $T/opt/setu/personality
    cp -a --no-preserve=ownership /mnt/personality/. $T/opt/setu/personality/
    umount /mnt/personality
fi
if [ "${HAVE_SHELL:-0}" = 1 ]; then
    log "copying setu-shell.exe to /opt/setu/shell"
    mount_9p shell /mnt/shell
    mkdir -p $T/opt/setu/shell
    cp --no-preserve=ownership /mnt/shell/setu-shell.exe $T/opt/setu/shell/
    umount /mnt/shell
fi

chroot $T /bin/bash -euo pipefail <<'EOF'
useradd -m -s /bin/bash -G sudo,video,render,input,audio setu
echo setu:setu | chpasswd
passwd -l root
systemctl enable systemd-networkd systemd-resolved ssh setu-session.service
systemctl set-default graphical.target
grub-install --target=x86_64-efi --efi-directory=/boot/efi --removable --no-nvram
update-grub
apt-get clean
rm -f /etc/ssh/ssh_host_*_key*
EOF
# Pre-create the setu user's Wine prefix so the first session starts fast
# (the same command setu-session would run; no display is needed).
log "creating the setu user's prefix"
chroot $T runuser -u setu -- env HOME=/home/setu WINEPREFIX=/home/setu/.setu/prefix \
    WINEDEBUG=-all WINEDLLOVERRIDES='mscoree,mshtml=' \
    PATH=/opt/setu/personality/bin:/usr/bin:/bin sh -c 'wineboot -i && wineserver -w' \
    || log "warning: prefix creation failed; setu-session will retry on first boot"

# Regenerate SSH host keys on first boot instead of baking them in.
mkdir -p $T/etc/systemd/system/ssh.service.d
cat > $T/etc/systemd/system/ssh.service.d/hostkeys.conf <<'EOF'
[Service]
ExecStartPre=/usr/bin/ssh-keygen -A
EOF
ln -sf /run/systemd/resolve/stub-resolv.conf $T/etc/resolv.conf

if [ "$WINE_MODE" = setu ]; then
    log "unix libraries of the personality with unresolved dependencies:"
    # ntdll.so and win32u.so are loaded by Wine itself, not by soname.
    for so in $T/opt/setu/personality/lib/wine/x86_64-unix/*.so $T/opt/setu/personality/bin/wine; do
        missing=$(chroot $T ldd "${so#$T}" 2>/dev/null \
            | awk '/not found/ && $1 != "ntdll.so" && $1 != "win32u.so" {print $1}' | xargs)
        [ -n "$missing" ] && log "  $(basename "$so"): $missing"
    done
    true
fi

log "rootfs size: $(du -sh --exclude=$T/proc --exclude=$T/sys --exclude=$T/dev --exclude=$T/run $T 2>/dev/null | cut -f1)"
sync
umount -R $T || umount -R -l $T
udevadm settle
if grep -q "^$(readlink -f "$ROOT") " /proc/mounts; then
    log "warning: root still mounted, skipping fsck"; grep /mnt/target /proc/mounts || true
else
    e2fsck -f -p "$ROOT" || [ $? -le 1 ]
fi
log "done"
echo SETU_BUILD_OK
