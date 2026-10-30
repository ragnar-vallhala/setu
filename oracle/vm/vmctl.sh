#!/usr/bin/env bash
# SPDX-License-Identifier: MIT OR Apache-2.0
#
# vmctl.sh: manage the Windows 11 oracle VM (QEMU/KVM, UEFI + Secure Boot,
# software TPM 2.0, SSH on 127.0.0.1:2222). All state lives in oracle/vm/state/.
#
#   vmctl.sh create --iso Win11.iso   build disk + seed media, start unattended install
#   vmctl.sh start [--gui]            boot the installed VM (headless by default; VNC on 127.0.0.1:5901)
#   vmctl.sh wait-ssh [SECONDS]       wait until the guest's sshd answers
#   vmctl.sh provision [--settle N]   wait for SSH, let the guest settle, then take the 'clean' snapshot
#   vmctl.sh snapshot [NAME]          live snapshot (RAM + disk), default NAME=clean
#   vmctl.sh revert [NAME]            restore a live snapshot (the harness does this before each run)
#   vmctl.sh snapshots                list snapshots
#   vmctl.sh ssh [COMMAND...]         SSH into the guest (cmd.exe)
#   vmctl.sh monitor "HMP COMMAND"    raw QEMU monitor command
#   vmctl.sh status | stop | kill
#
# Environment overrides: SETU_VM_RAM (default 4G), SETU_VM_CPUS (4), SETU_VM_DISK (64G),
# SETU_VM_SSH_PORT (2222), SETU_VM_VNC (1 -> :5901), OVMF_CODE / OVMF_VARS (firmware paths).
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ORACLE="$(dirname "$HERE")"
STATE="$HERE/state"
DISK="$STATE/disk.qcow2"
VARS="$STATE/vars.qcow2"
TPMDIR="$STATE/tpm"
MON="$STATE/monitor.sock"
PIDF="$STATE/qemu.pid"
KEY="$STATE/id_ed25519"
KNOWN="$STATE/known_hosts"
SEED="$STATE/seed.iso"

RAM="${SETU_VM_RAM:-4G}"
CPUS="${SETU_VM_CPUS:-4}"
DISK_SIZE="${SETU_VM_DISK:-64G}"
SSH_PORT="${SETU_VM_SSH_PORT:-2222}"
VNC="${SETU_VM_VNC:-1}"
USER_NAME="setu"

die() { echo "vmctl: $*" >&2; exit 1; }
log() { echo "vmctl: $*"; }

find_fw() {  # $1 = CODE|VARS
  local c
  if [ "$1" = CODE ]; then
    [ -n "${OVMF_CODE:-}" ] && { echo "$OVMF_CODE"; return; }
    for c in /usr/share/OVMF/OVMF_CODE_4M.secboot.fd /usr/share/OVMF/OVMF_CODE_4M.ms.fd \
             /usr/share/edk2/x64/OVMF_CODE.secboot.4m.fd /usr/share/edk2/ovmf/OVMF_CODE.secboot.fd; do
      [ -e "$c" ] && { echo "$c"; return; }
    done
  else
    [ -n "${OVMF_VARS:-}" ] && { echo "$OVMF_VARS"; return; }
    # Variables template with the standard Secure Boot keys pre-enrolled.
    for c in /usr/share/OVMF/OVMF_VARS_4M.ms.fd /usr/share/edk2/x64/OVMF_VARS.4m.fd \
             /usr/share/edk2/ovmf/OVMF_VARS.secboot.fd; do
      [ -e "$c" ] && { echo "$c"; return; }
    done
  fi
  die "OVMF firmware ($1) not found. Install 'ovmf' (Debian/Ubuntu) or 'edk2-ovmf' (Arch/Fedora), or set OVMF_$1."
}

hmp() { PYTHONPATH="$ORACLE" python3 -m setu_oracle.hmp "$MON" "$*"; }

running() { [ -f "$PIDF" ] && kill -0 "$(cat "$PIDF")" 2>/dev/null; }

check_host() {
  command -v qemu-system-x86_64 >/dev/null || die "qemu-system-x86_64 not found (apt install qemu-system-x86)"
  command -v qemu-img >/dev/null || die "qemu-img not found (apt install qemu-utils)"
  command -v swtpm >/dev/null || die "swtpm not found (apt install swtpm swtpm-tools); Windows 11 needs TPM 2.0"
  [ -r /dev/kvm ] && [ -w /dev/kvm ] || die "/dev/kvm not accessible: enable virtualization and add yourself to the 'kvm' group"
}

start_tpm() {
  mkdir -p "$TPMDIR"
  if [ -S "$STATE/swtpm.sock" ] && pgrep -f "swtpm.*$STATE/swtpm.sock" >/dev/null; then return; fi
  rm -f "$STATE/swtpm.sock"
  swtpm socket --tpm2 --tpmstate dir="$TPMDIR" \
    --ctrl type=unixio,path="$STATE/swtpm.sock" \
    --log file="$STATE/swtpm.log" --daemon
  for _ in $(seq 50); do [ -S "$STATE/swtpm.sock" ] && return; sleep 0.1; done
  die "swtpm did not start (see $STATE/swtpm.log)"
}

# $1 = "install" (attach installer ISO + seed) or "run"; $2 = "gui" or "headless"
launch() {
  local mode="$1" ui="$2"
  running && die "VM already running (pid $(cat "$PIDF"))"
  [ -f "$DISK" ] || die "no disk at $DISK. Run: vmctl.sh create --iso <Win11.iso>"
  start_tpm
  rm -f "$MON"
  local args=(
    -name setu-oracle,process=setu-oracle
    -machine q35,smm=on,accel=kvm -cpu host -smp "$CPUS" -m "$RAM"
    -global ICH9-LPC.disable_s3=1
    -global driver=cfi.pflash01,property=secure,value=on
    # Firmware variables are stored as qcow2 so that `savevm` can snapshot them too.
    -drive if=pflash,format=raw,unit=0,readonly=on,file="$(find_fw CODE)"
    -drive if=pflash,format=qcow2,unit=1,file="$VARS"
    -chardev socket,id=chrtpm,path="$STATE/swtpm.sock"
    -tpmdev emulator,id=tpm0,chardev=chrtpm -device tpm-crb,tpmdev=tpm0
    # AHCI disk + e1000e NIC: both have inbox Windows drivers (no virtio ISO needed).
    -device ahci,id=ahci
    -drive if=none,id=disk0,format=qcow2,file="$DISK",cache=unsafe,discard=unmap
    -device ide-hd,drive=disk0,bus=ahci.0,bootindex=1
    # SSH only on the host's loopback interface.
    -netdev user,id=net0,hostfwd=tcp:127.0.0.1:"$SSH_PORT"-:22
    -device e1000e,netdev=net0
    -device qemu-xhci -device usb-tablet
    -rtc base=utc
    -monitor unix:"$MON",server,nowait
    -pidfile "$PIDF" -daemonize
  )
  if [ "$mode" = install ]; then
    [ -n "${ISO:-}" ] && [ -f "$ISO" ] || die "installer ISO not found: ${ISO:-<unset>}"
    args+=(
      -drive if=none,id=cd0,media=cdrom,readonly=on,file="$ISO"
      -device ide-cd,drive=cd0,bus=ahci.1,bootindex=0
      -drive if=none,id=cd1,media=cdrom,readonly=on,file="$SEED"
      -device ide-cd,drive=cd1,bus=ahci.2
    )
  fi
  if [ "$ui" = gui ] && [ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]; then
    args+=(-display gtk -vga std)
  else
    args+=(-display none -vga std -vnc 127.0.0.1:"$VNC")
    log "display: VNC on 127.0.0.1:$((5900 + VNC))"
  fi
  qemu-system-x86_64 "${args[@]}"
  for _ in $(seq 50); do [ -S "$MON" ] && break; sleep 0.1; done
  log "VM started (pid $(cat "$PIDF")), monitor $MON, ssh 127.0.0.1:$SSH_PORT"
}

ssh_base() {
  echo ssh -i "$KEY" -o IdentitiesOnly=yes -o BatchMode=yes -o StrictHostKeyChecking=accept-new \
    -o UserKnownHostsFile="$KNOWN" -o ConnectTimeout=5 -o LogLevel=ERROR -p "$SSH_PORT" "$USER_NAME@127.0.0.1"
}

wait_ssh() {
  local limit="${1:-1800}" t0=$SECONDS
  log "waiting for SSH on 127.0.0.1:$SSH_PORT (up to ${limit}s) ..."
  until $(ssh_base) "cmd /c echo ready" 2>/dev/null | grep -q ready; do
    running || die "VM is not running"
    (( SECONDS - t0 > limit )) && die "SSH not ready after ${limit}s (check the VM display: VNC 127.0.0.1:$((5900 + VNC)))"
    sleep 10
  done
  log "SSH is up after $((SECONDS - t0))s"
}

build_seed() {
  local tmp; tmp="$(mktemp -d)"
  local pub; pub="$(cat "$KEY.pub")"
  cp "$HERE/autounattend.xml" "$tmp/"
  cp "$HERE/setup-guest.ps1" "$tmp/"
  echo "$pub" > "$tmp/setu_oracle.pub"
  echo "Setu oracle seed media" > "$tmp/setu-seed.txt"
  local mk
  mk="$(command -v genisoimage || command -v mkisofs || command -v xorrisofs || true)"
  [ -n "$mk" ] || die "need genisoimage, mkisofs or xorrisofs to build the seed ISO (apt install genisoimage)"
  "$mk" -quiet -J -r -V SETU_SEED -o "$SEED" "$tmp"
  rm -rf "$tmp"
}

cmd_create() {
  local iso=""
  while [ $# -gt 0 ]; do
    case "$1" in
      --iso) iso="$2"; shift 2 ;;
      *) die "unknown option $1" ;;
    esac
  done
  [ -n "$iso" ] || die "usage: vmctl.sh create --iso /path/to/Win11.iso"
  check_host
  [ -f "$DISK" ] && die "$DISK already exists. To start over: vmctl.sh kill && rm -rf $STATE"
  mkdir -p "$STATE"
  local avail; avail=$(df -Pk "$STATE" | awk 'NR==2{print int($4/1024/1024)}')
  (( avail < 40 )) && log "WARNING: only ${avail} GiB free under $STATE; Windows 11 needs ~30 GiB + RAM-sized snapshots"
  [ -f "$KEY" ] || ssh-keygen -q -t ed25519 -N "" -C "setu-oracle" -f "$KEY"
  qemu-img create -q -f qcow2 "$DISK" "$DISK_SIZE"
  qemu-img convert -q -f raw -O qcow2 "$(find_fw VARS)" "$VARS"
  build_seed
  ISO="$(realpath "$iso")" launch install gui
  # The Windows boot media asks "Press any key to boot from CD or DVD": press it for a while.
  log "pressing a key to boot from the installer ..."
  for _ in $(seq 25); do hmp "sendkey spc" >/dev/null 2>&1 || true; sleep 1; done
  log "unattended install running. It reboots a few times and takes 15-40 minutes."
  log "next: vmctl.sh provision     (waits for SSH, then takes the 'clean' snapshot)"
}

cmd_provision() {
  local settle=300
  [ "${1:-}" = --settle ] && settle="$2"
  running || die "VM is not running (vmctl.sh start)"
  wait_ssh 3600
  $(ssh_base) "cmd /c if exist C:\\setu-oracle-ready.txt (echo provisioned) else (echo NOT-PROVISIONED)"
  # A live snapshot can only be loaded into a QEMU with the same devices, so
  # power off and come back up in the normal "run" configuration (no ISOs).
  log "shutting down to restart without installer media ..."
  $(ssh_base) "shutdown /s /t 0" || true
  for _ in $(seq 180); do running || break; sleep 2; done
  running && die "guest did not power off within 6 minutes"
  rm -f "$PIDF"
  launch run headless
  wait_ssh 900
  log "letting the guest settle for ${settle}s (background first-boot tasks) ..."
  sleep "$settle"
  cmd_snapshot clean
  $(ssh_base) "cmd /c ver"
  log "done. The oracle can now run: python3 oracle/run.py build windows"
}

cmd_snapshot() {
  local name="${1:-clean}"
  running || die "VM is not running"
  log "saving live snapshot '$name' (pauses the VM briefly) ..."
  hmp "savevm $name"
  log "snapshot '$name' saved"
}

cmd_revert() {
  local name="${1:-clean}"
  running || die "VM is not running"
  hmp "loadvm $name"
  hmp "info status" | grep -q running || hmp "cont"
  log "reverted to '$name'"
}

case "${1:-}" in
  create) shift; cmd_create "$@" ;;
  start) shift; check_host; launch run "$([ "${1:-}" = --gui ] && echo gui || echo headless)" ;;
  wait-ssh) shift; wait_ssh "${1:-1800}" ;;
  provision) shift; cmd_provision "$@" ;;
  snapshot) shift; cmd_snapshot "${1:-clean}" ;;
  revert) shift; cmd_revert "${1:-clean}" ;;
  snapshots) running && hmp "info snapshots" || qemu-img snapshot -l "$DISK" ;;
  ssh) shift; exec $(ssh_base) "$@" ;;
  monitor) shift; hmp "$*" ;;
  status)
    if running; then log "running (pid $(cat "$PIDF"))"; hmp "info status"; else log "not running"; fi ;;
  stop) running && hmp "system_powerdown" && log "ACPI shutdown requested" || log "not running" ;;
  kill)
    if running; then hmp "quit" >/dev/null 2>&1 || kill "$(cat "$PIDF")"; fi
    pkill -f "swtpm.*$STATE/swtpm.sock" 2>/dev/null || true
    rm -f "$PIDF" "$MON"; log "stopped" ;;
  *) sed -n '3,20p' "$0" | sed 's/^# \{0,1\}//'; exit 2 ;;
esac
