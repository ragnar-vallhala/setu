#!/bin/bash
# SPDX-License-Identifier: MIT OR Apache-2.0
# End-to-end check of the dev image: boot it (QEMU, --snapshot, so the image
# is left untouched), wait for the Setu session, then over SSH:
#   1. start Notepad through the shell's run box and wait for its window,
#   2. install a third-party .msi with `msiexec /i FILE /qn`,
#   3. start the installed program through the run box and wait for its window,
#   4. take a QEMU screendump of the desktop (taskbar + both programs),
#   5. compile and run a tiny .NET program with the bundled Wine Mono
#      (lib.sh check_dotnet).
# Windows are found with setu-probe.exe (shell/tests/, installed next to the
# shell when build-image.sh was given --shell from a build that has it).
#
# The default MSI is 7-Zip 26.05 x64 (GNU LGPL, parts BSD-3-Clause, unRAR
# restriction; see 7-zip.org/license.txt), downloaded from 7-zip.org on first use.
#
# Usage: distro/tests/msi-smoke.sh [options]
#   --image FILE      dev image (default: build/distro/setu-dev.qcow2)
#   --msi FILE        the .msi to install (default: download 7-Zip, see above)
#   --exe 'C:\...'    program the .msi installs, to run afterwards
#                     (default: C:\Program Files\7-Zip\7zFM.exe)
#   --vnc N           VNC display (default: 21)
#   --ssh-port N      host port for the guest's SSH (default: 2233)
#   --shot FILE       save the screendump as PNG (default: build/distro/msi-smoke/session-msi.png)
#   --keep            leave the VM running at the end (it still discards writes)
#   -h, --help
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
OUT=$REPO/build/distro/msi-smoke
IMAGE=$REPO/build/distro/setu-dev.qcow2
MSI=
MSI_URL=https://www.7-zip.org/a/7z2605-x64.msi
EXE='C:\Program Files\7-Zip\7zFM.exe'
VNC=21
SSH_PORT=2233
SHOT=
KEEP=0

usage() { sed -n '2,/^set -euo/{/^set -euo/d;s/^# \{0,1\}//;p}' "$0"; }
while [ $# -gt 0 ]; do
    case $1 in
        --image) IMAGE=$2; shift ;;
        --msi) MSI=$2; shift ;;
        --exe) EXE=$2; shift ;;
        --vnc) VNC=$2; shift ;;
        --ssh-port) SSH_PORT=$2; shift ;;
        --shot) SHOT=$2; shift ;;
        --keep) KEEP=1 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "unknown option: $1 (see --help)" >&2; exit 2 ;;
    esac
    shift
done
SHOT=${SHOT:-$OUT/session-msi.png}
RUN=$OUT/run
mkdir -p "$OUT" "$RUN"
. "$HERE/lib.sh"

if [ -z "$MSI" ]; then
    MSI=$OUT/download/$(basename "$MSI_URL")
    if [ ! -f "$MSI" ]; then
        mkdir -p "$OUT/download"
        echo "downloading $MSI_URL"
        curl -sfL -m 300 -o "$MSI.part" "$MSI_URL" || fail "download of $MSI_URL failed"
        mv "$MSI.part" "$MSI"
    fi
fi
[ -f "$MSI" ] || fail "no MSI at $MSI"

# --- boot -------------------------------------------------------------------
boot_vm "$IMAGE" "$VNC"
wait_session

# --- 1. Notepad -------------------------------------------------------------
gwine /opt/setu/shell/setu-shell.exe --run notepad.exe || fail "setu-shell --run notepad.exe failed"
wait_window '\|Notepad\|[^|]*\|[^|]*\|visible' 30 || fail "no Notepad window"
pass "notepad: $(probe windows | grep '|Notepad|' | head -1)"

# --- 2. install the MSI -----------------------------------------------------
gscp "$MSI" setu@127.0.0.1:/tmp/smoke.msi
start=$SECONDS
rc=0; gwine msiexec /i 'Z:\tmp\smoke.msi' /qn > "$OUT/msiexec.log" 2>&1 || rc=$?
[ "$rc" = 0 ] || fail "msiexec /i /qn failed (exit $rc), see $OUT/msiexec.log"
EXE_UNIX=$(gwine winepath -u "$EXE" 2>/dev/null | tr -d '\r')
gssh test -f "$(printf %q "$EXE_UNIX")" || fail "$EXE ($EXE_UNIX) not there after the install"
pass "msiexec /i $(basename "$MSI") /qn: $EXE installed in $((SECONDS - start))s"

# --- 3. run the installed program -------------------------------------------
before=$(probe windows | grep -c '^window=')
gwine /opt/setu/shell/setu-shell.exe --run "\"$EXE\"" || fail "setu-shell --run $EXE failed"
# its window: a new visible top-level window that is on top
for i in $(seq 30); do
    [ "$(probe windows | grep -c '^window=')" -gt "$before" ] && break
    sleep 1
done
probe windows > "$OUT/windows.txt"
[ "$(grep -c '^window=' "$OUT/windows.txt")" -gt "$before" ] || fail "no new window after starting $EXE (see $OUT/windows.txt)"
pass "$(basename "${EXE//\\//}"): $(grep -v -e '|Setu taskbar|' -e '|Notepad|' "$OUT/windows.txt" | head -1)"

# --- 4. screendump ----------------------------------------------------------
sleep 2
screendump "$SHOT"
pass "screenshot: $SHOT"

# --- 5. .NET ------------------------------------------------------------------
check_dotnet
echo "all checks passed"
