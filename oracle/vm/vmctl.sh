#!/usr/bin/env bash
# SPDX-License-Identifier: MIT OR Apache-2.0
#
# vmctl.sh: manage the Windows oracle VMs (QEMU/KVM, UEFI + Secure Boot,
# software TPM 2.0, SSH on 127.0.0.1:2222). All state of the first VM lives in
# oracle/vm/state/; VM N (N >= 2, a clone of the first) lives in
# oracle/vm/state-N/ and listens on SSH port 2221+N. Select it with --vm N.
#
#   vmctl.sh [--vm N] COMMAND ...    (default N=1, also $SETU_VM)
#
#   vmctl.sh create --iso Win11.iso [--gui]  build disk + seed media, start unattended install
#   vmctl.sh --vm N clone [--dir DIR] copy VM 1's 'clean' snapshot into VM N (VM 1 keeps running)
#   vmctl.sh start [--gui] [--loadvm NAME]  boot the VM, or resume it from a live snapshot
#                                     (headless by default; VNC on 127.0.0.1:5901, VM N: 5910+N)
#   vmctl.sh wait-ssh [SECONDS]       wait until the guest's sshd answers
#   vmctl.sh provision [--settle N]   wait for SSH, let the guest settle, then take the 'clean' snapshot
#   vmctl.sh snapshot [NAME]          live snapshot (RAM + disk), default NAME=clean
#   vmctl.sh revert [NAME]            restore a live snapshot (the harness does this before each run)
#   vmctl.sh snapshots                list snapshots
#   vmctl.sh ssh [COMMAND...]         SSH into the guest (cmd.exe)
#   vmctl.sh monitor "HMP COMMAND"    raw QEMU monitor command
#   vmctl.sh screenshot [FILE.png]    capture the VM display (handy for headless installs)
#   vmctl.sh status | stop | kill
#   vmctl.sh pool                     list the VMs and whether each is free (harness locks)
#
# Commands that change a running VM (revert, snapshot, stop, kill, monitor) do
# not take the harness's lock: run them as
#   python3 -m setu_oracle.pool locked --vm NAME -- oracle/vm/vmctl.sh ...
# (from oracle/) when other people may be using that VM.
#
# Environment overrides: SETU_VM_RAM (default 4G), SETU_VM_CPUS (4), SETU_VM_DISK (64G),
# SETU_VM_SSH_PORT (2221+N), SETU_VM_VNC (1, or 10+N for N >= 2: VNC :5900+that), OVMF_CODE / OVMF_VARS (firmware paths).
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ORACLE="$(dirname "$HERE")"

VM_N="${SETU_VM:-1}"
if [ "${1:-}" = --vm ]; then VM_N="${2:?--vm needs a number}"; shift 2; fi
[[ "$VM_N" =~ ^[1-9][0-9]?$ ]] || { echo "vmctl: --vm wants a number 1-99" >&2; exit 2; }

# The VMs exist once per machine. In a git worktree without its own state,
# use the main checkout's (the oracle harness does the same).
VMDIR="$HERE"
if [ ! -d "$HERE/state" ]; then
  common="$(git -C "$HERE" rev-parse --path-format=absolute --git-common-dir 2>/dev/null || true)"
  [ -n "$common" ] && [ -d "$(dirname "$common")/oracle/vm/state" ] && VMDIR="$(dirname "$common")/oracle/vm"
fi
if [ "$VM_N" = 1 ]; then
  STATE="$VMDIR/state"; NAME="setu-oracle"
else
  STATE="$VMDIR/state-$VM_N"; NAME="setu-oracle-$VM_N"
fi
SRC_STATE="$VMDIR/state"   # what `clone` copies from
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
json_port() { [ -f "$STATE/vm.json" ] && python3 -c 'import json,sys; print(json.load(open(sys.argv[1])).get("ssh_port",""))' "$STATE/vm.json" || true; }
SSH_PORT="${SETU_VM_SSH_PORT:-$(json_port)}"
SSH_PORT="${SSH_PORT:-$((2221 + VM_N))}"
VNC="${SETU_VM_VNC:-$(( VM_N == 1 ? 1 : 10 + VM_N ))}"
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
launch() {  # $3 = live snapshot to resume from (optional)
  local mode="$1" ui="$2" loadvm="${3:-}"
  running && die "VM already running (pid $(cat "$PIDF"))"
  [ -f "$DISK" ] || die "no disk at $DISK. Run: vmctl.sh create --iso <Win11.iso>"
  start_tpm
  rm -f "$MON"
  local args=(
    -name "$NAME,process=$NAME"
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
  # Resume from a live snapshot instead of booting (how a clone first starts).
  [ -n "$loadvm" ] && args+=(-loadvm "$loadvm")
  qemu-system-x86_64 "${args[@]}"
  for _ in $(seq 50); do [ -S "$MON" ] && break; sleep 0.1; done
  write_vm_json
  log "VM started (pid $(cat "$PIDF")), monitor $MON, ssh 127.0.0.1:$SSH_PORT"
}

# oracle/run.py finds the VMs through their state directories; vm.json tells
# it the name and SSH port. An existing file is kept (it may list extra_locks).
write_vm_json() {
  [ -f "$STATE/vm.json" ] && return
  printf '{\n  "name": "%s",\n  "ssh_port": %s\n}\n' "$NAME" "$SSH_PORT" > "$STATE/vm.json"
}

ssh_base() {
  echo ssh -i "$KEY" -o IdentitiesOnly=yes -o BatchMode=yes -o StrictHostKeyChecking=accept-new \
    -o UserKnownHostsFile="$KNOWN" -o ConnectTimeout=5 -o LogLevel=ERROR -p "$SSH_PORT" "$USER_NAME@127.0.0.1"
}

wait_ssh() {
  local limit="${1:-1800}" t0=$SECONDS
  log "waiting for SSH on 127.0.0.1:$SSH_PORT (up to ${limit}s) ..."
  until $(ssh_base) "echo ready" 2>/dev/null | grep -q ready; do
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
  local iso="" ui=headless
  while [ $# -gt 0 ]; do
    case "$1" in
      --iso) iso="$2"; shift 2 ;;
      --gui) ui=gui; shift ;;
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
  ISO="$(realpath "$iso")" launch install "$ui"
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
  $(ssh_base) "type C:\\setu-oracle-ready.txt" \
    || die "guest setup marker C:\\setu-oracle-ready.txt missing: check C:\\setu-setup.log in the guest"
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
  $(ssh_base) "ver"
  log "done. The oracle can now run: python3 oracle/run.py build windows"
}

cmd_snapshot() {
  local name="${1:-clean}"
  running || die "VM is not running"
  log "saving live snapshot '$name' (pauses the VM briefly) ..."
  hmp "savevm $name"
  log "snapshot '$name' saved"
}

# Make VM N (N >= 2) from VM 1's 'clean' live snapshot without stopping,
# pausing or reverting VM 1, and without sending it any monitor command:
#  * the disk: `qemu-img convert -U -l snapshot.name=clean` reads only the
#    snapshot's clusters. qcow2 never rewrites a cluster that a snapshot
#    still references (writes to it are copied elsewhere), so these are a
#    fixed image even while VM 1 runs. Stored zstd-compressed, as a new
#    image with its own internal 'clean' snapshot.
#  * firmware variables + RAM state: vars.qcow2 holds the snapshot's VM state
#    (the RAM image saved by `savevm`). It is copied whole, checked with
#    `qemu-img check`, and its active layer is reset to 'clean' offline.
#  * TPM state, SSH key and known_hosts are copied.
# Everything happens while VM 1's harness locks are held, so no oracle run
# (and no `savevm`) can change VM 1's image in the meantime.
cmd_clone() {
  local dir=""
  [ "${1:-}" = --dir ] && { dir="$2"; shift 2; }
  [ "$VM_N" != 1 ] || die "clone makes VM N >= 2 from VM 1, e.g. vmctl.sh --vm 2 clone"
  { [ -e "$STATE" ] || [ -L "$STATE" ]; } && die "$STATE already exists. To start over: vmctl.sh --vm $VM_N kill, then remove $STATE (and its --dir target)"
  local f
  for f in disk.qcow2 vars.qcow2 id_ed25519 id_ed25519.pub; do [ -f "$SRC_STATE/$f" ] || die "missing $SRC_STATE/$f"; done
  qemu-img snapshot -U -l "$SRC_STATE/disk.qcow2" | awk '{print $2}' | grep -qx clean \
    || die "VM 1 has no 'clean' snapshot"
  local need
  need=$(qemu-img measure -U -l snapshot.name=clean -O qcow2 "$SRC_STATE/disk.qcow2" | awk '/^required size/{print $3}')
  need=$(( (need + $(stat -c %s "$SRC_STATE/vars.qcow2")) / 1024 / 1024 / 1024 + 1 ))
  if [ -n "$dir" ]; then
    mkdir -p "$dir"
    [ -z "$(ls -A "$dir")" ] || die "$dir is not empty"
    ln -s "$(realpath "$dir")" "$STATE"
  else
    mkdir -p "$STATE"
  fi
  local avail; avail=$(df -Pk "$STATE/" | awk 'NR==2{print int($4/1024/1024)}')
  if (( avail < need + 2 )); then
    rm -f "$STATE" 2>/dev/null || rmdir "$STATE"
    die "need ~${need} GiB (at most; the disk copy is compressed) + 2 GiB spare under $STATE, have ${avail} GiB. Use --dir on a bigger filesystem."
  fi
  log "copying VM 1's 'clean' snapshot into $STATE (VM 1 keeps running; waiting for its harness lock) ..."
  PYTHONPATH="$ORACLE" python3 -m setu_oracle.pool locked --vm setu-oracle -- "$0" --vm "$VM_N" _clone-copy
  log "done. Next: vmctl.sh --vm $VM_N start --loadvm clean"
  log "then check it: python3 oracle/run.py windows --verify --vm $NAME"
}

cmd_clone_copy() {  # internal: runs while VM 1's harness locks are held
  local t0=$SECONDS rc=0
  log "disk: converting snapshot 'clean' (zstd-compressed) ..."
  qemu-img convert -U -p -f qcow2 -l snapshot.name=clean -O qcow2 -c -o compression_type=zstd \
    "$SRC_STATE/disk.qcow2" "$STATE/disk.qcow2"
  qemu-img snapshot -c clean "$STATE/disk.qcow2"
  log "firmware variables + RAM state ..."
  cp --sparse=always "$SRC_STATE/vars.qcow2" "$STATE/vars.qcow2"
  qemu-img check -r leaks "$STATE/vars.qcow2" || rc=$?
  (( rc == 0 || rc == 3 )) || die "the copied vars.qcow2 fails qemu-img check ($rc); not using it"
  qemu-img snapshot -a clean "$STATE/vars.qcow2"
  qemu-img check "$STATE/vars.qcow2" >/dev/null || die "vars.qcow2 inconsistent after reverting it to 'clean'"
  qemu-img check "$STATE/disk.qcow2" >/dev/null || die "disk.qcow2 copy fails qemu-img check"
  cp -a "$SRC_STATE/tpm" "$STATE/tpm"
  cp -a "$SRC_STATE/id_ed25519" "$SRC_STATE/id_ed25519.pub" "$STATE/"
  [ -f "$SRC_STATE/known_hosts" ] && cp -a "$SRC_STATE/known_hosts" "$STATE/"
  write_vm_json
  log "copied in $((SECONDS - t0))s: $(du -sh --apparent-size "$STATE/disk.qcow2" | cut -f1) disk (apparent), $(du -sh "$STATE/vars.qcow2" | cut -f1) vars"
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
  start)
    shift; ui=headless; snap=""
    while [ $# -gt 0 ]; do
      case "$1" in
        --gui) ui=gui; shift ;;
        --loadvm) snap="${2:?--loadvm needs a snapshot name}"; shift 2 ;;
        *) die "unknown option $1" ;;
      esac
    done
    check_host; launch run "$ui" "$snap" ;;
  clone) shift; cmd_clone "$@" ;;
  _clone-copy) cmd_clone_copy ;;
  pool) PYTHONPATH="$ORACLE" python3 -m setu_oracle.pool list ;;
  wait-ssh) shift; wait_ssh "${1:-1800}" ;;
  provision) shift; cmd_provision "$@" ;;
  snapshot) shift; cmd_snapshot "${1:-clean}" ;;
  revert) shift; cmd_revert "${1:-clean}" ;;
  snapshots) running && hmp "info snapshots" || qemu-img snapshot -l "$DISK" ;;
  ssh) shift; exec $(ssh_base) "$@" ;;
  monitor) shift; hmp "$*" ;;
  screenshot)
    shift; out="$(realpath -m "${1:-$STATE/screen.png}")"
    hmp "screendump $out -f png" && log "saved $out" ;;
  status)
    if running; then log "running (pid $(cat "$PIDF"))"; hmp "info status"; else log "not running"; fi ;;
  stop) running && hmp "system_powerdown" && log "ACPI shutdown requested" || log "not running" ;;
  kill)
    if running; then hmp "quit" >/dev/null 2>&1 || kill "$(cat "$PIDF")"; fi
    pkill -f "swtpm.*$STATE/swtpm.sock" 2>/dev/null || true
    rm -f "$PIDF" "$MON"; log "stopped" ;;
  *) sed -n '3,32p' "$0" | sed 's/^# \{0,1\}//'; exit 2 ;;
esac
