#!/bin/bash
# Runs as root INSIDE the throwaway builder VM started by distro/build-image.sh.
# Builds the Debian root filesystem with mmdebstrap, adds the Setu
# personality and session config, and then, by KIND:
#   image  partitions the target disk (GPT: ESP + ext4 root + the C: volume,
#          ext4 with casefold; distro/layout.env), builds into it and
#          installs GRUB (EFI, removable path): the dev image.
#   iso    builds into a directory, adds the installer (distro/installer/:
#          live-boot, Calamares and its config, the first-boot wizard),
#          squashes it and writes a hybrid UEFI ISO (grub-mkrescue) to the
#          start of the target disk: the installer ISO.
#
# Inputs (set up by build-image.sh):
#   /etc/setu-build.env      KIND SUITE MIRROR WINE_MODE IMAGE_HOSTNAME HAVE_SHELL ROOT_SIZE
#   9p tag "distro"          the repo's distro/ directory (read-only)
#   9p tag "overlay"         more files for the image (services/fs, installed)
#   9p tag "personality"     build/personality/install (read-only, WINE_MODE=setu)
#   9p tag "shell"           directory with setu-shell.exe and, if built,
#                            setu-probe.exe (HAVE_SHELL=1)
#   virtio disk serial "setu-target"   the output disk (raw)
# Output goes to the second serial port; its last line is SETU_BUILD_OK on
# success (KIND=iso: preceded by SETU_ISO_SIZE=<bytes>).
set -euo pipefail
# Work in a private mount namespace: otherwise services that start while the
# target is mounted (with their own namespaces) keep it busy after umount.
[ -n "${SETU_UNSHARED:-}" ] || exec env SETU_UNSHARED=1 unshare -m --propagation private bash "$0" "$@"
. /etc/setu-build.env
KIND=${KIND:-image}

log() { echo "[setu-build] $*"; }
export DEBIAN_FRONTEND=noninteractive
DISTRO=/mnt/distro
DISK=/dev/disk/by-id/virtio-setu-target

mount_9p() { # tag dir
    mkdir -p "$2"
    mount -t 9p -o trans=virtio,version=9p2000.L,ro,msize=1048576 "$1" "$2"
}

log "installing builder tools"
apt-get update -qq
tools="mmdebstrap dosfstools e2fsprogs fdisk attr"
[ "$KIND" = iso ] && tools="$tools squashfs-tools xorriso mtools grub-efi-amd64-bin"
apt-get install -y -q --no-install-recommends $tools

mountpoint -q $DISTRO || mount_9p distro $DISTRO
. $DISTRO/layout.env
ROOT_SIZE=${ROOT_SIZE:-$SETU_ROOT_SIZE}
CDIR=$SETU_CVOL_DIR

if [ "$KIND" = image ]; then
    T=/mnt/target
    log "partitioning $DISK"
    udevadm settle
    wipefs -a "$DISK"
    sfdisk -q "$DISK" <<EOF
label: gpt
size=$SETU_ESP_SIZE, type=uefi, name="$SETU_ESP_NAME"
size=$ROOT_SIZE, type=$SETU_ROOT_TYPE, name="$SETU_ROOT_NAME"
type=$SETU_CVOL_TYPE, name="$SETU_CVOL_NAME"
EOF
    udevadm settle
    ESP=${DISK}-part1
    ROOT=${DISK}-part2
    CVOL=${DISK}-part3
    mkfs.vfat -F 32 -n "$SETU_ESP_LABEL" "$ESP" >/dev/null
    mkfs.ext4 -q -F -L "$SETU_ROOT_LABEL" "$ROOT"
    # C: is case-insensitive in the kernel: ext4 casefold (UTF-8 folding)
    # with +F on the volume's root directory (setu-cvol-init below;
    # docs/ARCHITECTURE.md 3.3).
    mkfs.ext4 -q -F -L "$SETU_CVOL_LABEL" -O casefold -E encoding=utf8 "$CVOL"
    mkdir -p $T
    mount "$ROOT" $T
else
    # The live system, which is also the system the installer copies.
    T=/var/tmp/setu-live
    rm -rf $T
    mkdir -p $T
fi

pkgs=$(sed 's/#.*//' $DISTRO/packages.list | xargs | tr ' ' ',')
if [ "$WINE_MODE" = debian ]; then
    pkgs="$pkgs,$(sed 's/#.*//' $DISTRO/packages-wine-debian.list | xargs | tr ' ' ',')"
fi
if [ "$KIND" = iso ]; then
    pkgs="$pkgs,$(sed 's/#.*//' $DISTRO/installer/packages.list | xargs | tr ' ' ',')"
fi

log "mmdebstrap $SUITE from $MIRROR (wine: $WINE_MODE, kind: $KIND)"
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
[ "$KIND" = image ] && mount "$ESP" $T/boot/efi
for d in dev proc sys run; do mount --rbind /$d $T/$d; mount --make-rslave $T/$d; done

cp -a --no-preserve=ownership $DISTRO/rootfs/. $T/
mount_9p overlay /mnt/overlay
cp -a --no-preserve=ownership /mnt/overlay/. $T/
umount /mnt/overlay
echo "$IMAGE_HOSTNAME" > $T/etc/hostname
printf '127.0.0.1\tlocalhost\n127.0.1.1\t%s\n' "$IMAGE_HOSTNAME" > $T/etc/hosts
mkdir -p $T$CDIR

if [ "$KIND" = image ]; then
    cat > $T/etc/fstab <<EOF
UUID=$(blkid -s UUID -o value "$ROOT")  /          ext4  errors=remount-ro  0 1
UUID=$(blkid -s UUID -o value "$ESP")  /boot/efi  vfat  umask=0077         0 2
UUID=$(blkid -s UUID -o value "$CVOL")  $CDIR  ext4  defaults  0 2
EOF
    log "C: volume: ext4 casefold at $CDIR"
    mount "$CVOL" $T$CDIR
    out=$(PATH=$PATH:/sbin:/usr/sbin sh $DISTRO/rootfs/usr/lib/setu/setu-cvol-init $T$CDIR)
    log "  $out"
else
    log "installer: live-boot, Calamares (installer/rootfs)"
    cp -a --no-preserve=ownership $DISTRO/installer/rootfs/. $T/
    # Calamares' partition layout, from layout.env
    sed -e "s|@ESP_SIZE@|$SETU_ESP_SIZE|g" -e "s|@ESP_LABEL@|$SETU_ESP_LABEL|g" \
        -e "s|@ROOT_SIZE@|$ROOT_SIZE|g" -e "s|@ROOT_NAME@|$SETU_ROOT_NAME|g" \
        -e "s|@ROOT_TYPE@|$SETU_ROOT_TYPE|g" -e "s|@CVOL_NAME@|$SETU_CVOL_NAME|g" \
        -e "s|@CVOL_TYPE@|$SETU_CVOL_TYPE|g" -e "s|@CVOL_DIR@|$CDIR|g" \
        $DISTRO/installer/partition.conf.in > $T/etc/calamares/modules/partition.conf
    cp $DISTRO/layout.env $T/usr/lib/setu/layout.env
    # the live system has no fstab of its own (Calamares writes the target's)
    echo '# Setu live system: no static mounts; the installer writes this file.' > $T/etc/fstab
    # the dev image's passwordless sudo is not for installed systems
    rm -f $T/etc/sudoers.d/setu
fi

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
    # the test helper next to it, if it was built (distro/tests use it)
    if [ -f /mnt/shell/setu-probe.exe ]; then
        cp --no-preserve=ownership /mnt/shell/setu-probe.exe $T/opt/setu/shell/
    fi
    umount /mnt/shell
fi

if [ "$KIND" = image ]; then
    chroot $T /bin/bash -euo pipefail <<'EOF'
useradd -m -s /bin/bash -G sudo,video,render,input,audio setu
echo setu:setu | chpasswd
passwd -l root
systemctl enable systemd-networkd systemd-resolved ssh setu-session.service setu-drives.service setu-ntacld.socket
# Until NT ACLs exist (services/fs), C: belongs to the one Windows user.
chown setu:setu /var/lib/setu/volumes/c
systemctl set-default graphical.target
grub-install --target=x86_64-efi --efi-directory=/boot/efi --removable --no-nvram
update-grub
apt-get clean
rm -f /etc/ssh/ssh_host_*_key*
EOF
    # Pre-create the setu user's Wine prefix so the first session starts fast
    # (the same command setu-session would run; no display is needed).
    log "creating the setu user's prefix"
    # Its C: is the system volume: dosdevices/c: links straight to it, so the
    # personality doesn't make a drive_c of its own (drive_c is a link too, for
    # tools that expect one).
    chroot $T runuser -u setu -- env HOME=/home/setu WINEPREFIX=/home/setu/.setu/prefix \
        WINEDEBUG=-all WINEDLLOVERRIDES='mshtml=' \
        PATH=/opt/setu/personality/bin:/usr/bin:/bin \
        sh -c 'mkdir -p $WINEPREFIX/dosdevices && ln -s /var/lib/setu/volumes/c $WINEPREFIX/dosdevices/c: \
               && ln -s / $WINEPREFIX/dosdevices/z: && ln -s /var/lib/setu/volumes/c $WINEPREFIX/drive_c \
               && wineboot -i && wineserver -w' \
        || log "warning: prefix creation failed; setu-session will retry on first boot"
else
    # No user yet: the first-boot wizard creates the admin account and then
    # enables setu-session.service for it (installer/README in distro/README.md).
    chroot $T /bin/bash -euo pipefail <<'EOF'
passwd -l root
systemctl enable systemd-networkd systemd-resolved ssh setu-drives.service setu-ntacld.socket \
    setu-live-installer.service setu-firstboot.service
systemctl set-default graphical.target
apt-get clean
rm -rf /var/lib/apt/lists/*
rm -f /etc/ssh/ssh_host_*_key*
# a new machine id on every boot of the live system and for each installed
# system (Calamares' machineid module)
: > /etc/machine-id
rm -f /var/lib/dbus/machine-id
EOF
fi

# Regenerate SSH host keys on first boot instead of baking them in. The
# keys must exist before Debian's own `sshd -t` check, so restate the list.
mkdir -p $T/etc/systemd/system/ssh.service.d
cat > $T/etc/systemd/system/ssh.service.d/hostkeys.conf <<'EOF'
[Service]
ExecStartPre=
ExecStartPre=/usr/bin/ssh-keygen -A
ExecStartPre=/usr/sbin/sshd -t
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
# Stop anything still running inside the chroot (e.g. gpg-agent started by
# apt), so the target can be unmounted cleanly.
for p in /proc/[0-9]*; do
    [ "$(readlink $p/root 2>/dev/null)" = $T ] && { log "killing $(cat $p/comm)"; kill -9 ${p#/proc/}; }
done
sleep 1
sync

if [ "$KIND" = image ]; then
    umount -R $T
    e2fsck -f -p "$ROOT" || [ $? -le 1 ]
    e2fsck -f -p "$CVOL" || [ $? -le 1 ]
    log "done"
    echo SETU_BUILD_OK
    exit 0
fi

for d in dev proc sys run; do umount -R $T/$d; done
ISO=/var/tmp/setu-iso
rm -rf $ISO
mkdir -p $ISO/live $ISO/boot/grub
kver=$(ls $T/lib/modules | sort -V | tail -1)
cp $T/boot/vmlinuz-$kver $ISO/live/vmlinuz
cp $T/boot/initrd.img-$kver $ISO/live/initrd.img
cp $DISTRO/installer/grub.cfg $ISO/boot/grub/grub.cfg
log "squashing the live system (kernel $kver)"
start=$(date +%s)
mksquashfs $T $ISO/live/filesystem.squashfs -comp xz -noappend -quiet -no-progress
log "  $(du -h $ISO/live/filesystem.squashfs | cut -f1) in $(( $(date +%s) - start ))s"
rm -rf $T
log "writing the ISO"
# grub-mkrescue: El Torito EFI boot image plus an appended EFI partition
# and a protective MBR, so the same file boots from a CD or a USB stick.
grub-mkrescue -o /var/tmp/setu-installer.iso $ISO -- -volid SETU_INSTALLER
size=$(stat -c %s /var/tmp/setu-installer.iso)
dd if=/var/tmp/setu-installer.iso of="$DISK" bs=4M conv=fsync status=none
log "done: ISO $(( size / 1048576 )) MiB"
echo "SETU_ISO_SIZE=$size"
echo SETU_BUILD_OK
