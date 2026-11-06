#!/bin/bash
# Build the Setu installer ISO: build/distro/setu-installer.iso, a hybrid
# UEFI image (CD or USB stick) that boots a Debian live system running the
# Calamares installer. The installed system starts the first-boot wizard,
# then the Setu session (distro/README.md, "Installer ISO").
#
# This is build-image.sh --kind iso: the same rootless builder VM and the
# same root file system as the dev image (packages.list, rootfs/,
# services/fs, the personality, the shell), plus distro/installer/.
#
# Usage: distro/build-iso.sh [build-image.sh options]
#   e.g. distro/build-iso.sh --shell build/shell/setu-shell.exe
#   (--out FILE, --personality DIR, --wine=debian, --suite, --mirror,
#   --root-size, --cpus, --mem and --keep-work work as for build-image.sh)
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
case ${1:-} in -h|--help) sed -n '2,/^set -euo/{/^set -euo/d;s/^# \{0,1\}//;p}' "$0"; exit 0 ;; esac
exec "$HERE/build-image.sh" --kind iso "$@"
