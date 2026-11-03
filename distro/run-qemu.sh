#!/bin/bash
# Boot the Setu dev image (from distro/build-image.sh) in QEMU/KVM with UEFI.
#
# Display: VNC on 127.0.0.1:5911 (display :11). Serial console: a log file.
# QEMU monitor: a unix socket, e.g. for screenshots:
#   echo 'screendump /tmp/s.ppm' | nc -U -q1 build/distro/run/monitor.sock
# SSH: ssh -p 2230 setu@127.0.0.1 (password: setu).
#
# Usage: distro/run-qemu.sh [options]
#   --image FILE     disk image (default: build/distro/setu-dev.qcow2)
#   --vnc N          VNC display number (default: 11, i.e. port 5911)
#   --ssh-port N     host port forwarded to guest SSH (default: 2230; 2222+ are the Windows oracle VMs)
#   --serial FILE    serial log (default: RUN_DIR/serial.log)
#   --run-dir DIR    monitor socket, pid file, UEFI vars and serial log
#                    (default: build/distro/run); use another one to run a
#                    second VM next to the first
#   --snapshot       discard all disk writes on exit
#   --daemonize      return once QEMU is running (pid in RUN_DIR/qemu.pid)
#   --cpus N, --mem MB   (default: 4, 4096)
#   -h, --help
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/.." && pwd)
RUN=$REPO/build/distro/run
IMAGE=$REPO/build/distro/setu-dev.qcow2
VNC=11
SSH_PORT=2230
SERIAL=
DAEMONIZE=0
CPUS=4
MEM=4096
EXTRA=()

usage() { sed -n '2,/^set -euo/{/^set -euo/d;s/^# \{0,1\}//;p}' "$0"; }
while [ $# -gt 0 ]; do
    case $1 in
        --image) IMAGE=$2; shift ;;
        --vnc) VNC=$2; shift ;;
        --ssh-port) SSH_PORT=$2; shift ;;
        --serial) SERIAL=$2; shift ;;
        --run-dir) RUN=$(realpath -m "$2"); shift ;;
        --snapshot) EXTRA+=(-snapshot) ;;
        --daemonize) DAEMONIZE=1 ;;
        --cpus) CPUS=$2; shift ;;
        --mem) MEM=$2; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "unknown option: $1 (see --help)" >&2; exit 2 ;;
    esac
    shift
done

[ -n "$SERIAL" ] || SERIAL=$RUN/serial.log
[ "$DAEMONIZE" = 0 ] || EXTRA+=(-daemonize -pidfile "$RUN/qemu.pid")
[ -f "$IMAGE" ] || { echo "no image at $IMAGE; run distro/build-image.sh" >&2; exit 1; }
[ "$VNC" != 1 ] || { echo "VNC :1 (5901) belongs to the oracle Windows VM" >&2; exit 1; }
mkdir -p "$RUN" "$(dirname "$SERIAL")"

# Per-image writable copy of the UEFI variable store.
OVMF_CODE=/usr/share/OVMF/OVMF_CODE_4M.fd
[ -f "$RUN/OVMF_VARS.fd" ] || cp /usr/share/OVMF/OVMF_VARS_4M.fd "$RUN/OVMF_VARS.fd"

echo "VNC: 127.0.0.1:$((5900 + VNC))  serial: $SERIAL  monitor: $RUN/monitor.sock  ssh: -p $SSH_PORT setu@127.0.0.1"
exec qemu-system-x86_64 -enable-kvm -cpu host -smp "$CPUS" -m "$MEM" \
    -machine q35 \
    -drive "if=pflash,format=raw,readonly=on,file=$OVMF_CODE" \
    -drive "if=pflash,format=raw,file=$RUN/OVMF_VARS.fd" \
    -drive "file=$IMAGE,if=virtio,format=qcow2" \
    -device virtio-vga \
    -device qemu-xhci -device usb-tablet -device usb-kbd \
    -nic "user,model=virtio-net-pci,hostfwd=tcp:127.0.0.1:$SSH_PORT-:22" \
    -display "vnc=127.0.0.1:$VNC" \
    -serial "file:$SERIAL" \
    -monitor "unix:$RUN/monitor.sock,server,nowait" \
    "${EXTRA[@]}"
