#!/bin/bash
# SPDX-License-Identifier: MIT OR Apache-2.0
# Smoke test for C: on ext4 casefold and the drive-letter service
# (services/fs/README.md). Boots the dev image (QEMU, --snapshot), waits for
# the Setu session, then over SSH and the QEMU monitor:
#   1. C: is the casefold volume: the backing directory has the casefold
#      flag (lsattr -d shows F) on an ext4 file system with the casefold
#      feature; C:\Test\MixedCase.txt made by cmd is found as
#      c:\test\mixedcase.TXT (by Windows programs and by Linux) and a
#      directory listing shows it as MixedCase.txt;
#   2. drive letters: plugging a FAT USB stick (QEMU drive_add/device_add)
#      gives it D: (GetLogicalDrives, GetDriveType, GetVolumeInformation,
#      `dir D:` in cmd), a second disk (ext4) gets E:, unplugging the stick
#      removes D: and leaves E:, plugging it again gives it D: again;
#   3. a screendump with a Command Prompt listing D: (--shot), and one with
#      Notepad and the launcher (--shell-shot, the README's session-shell.png);
#   4. then runs msi-smoke.sh (unless --no-msi).
#
# Usage: distro/tests/drives-smoke.sh [options]
#   --image FILE       dev image (default: build/distro/setu-dev.qcow2)
#   --vnc N            VNC display (default: 22)
#   --ssh-port N       host port for the guest's SSH (default: 2234)
#   --shot FILE        PNG of the drives check (default: build/distro/drives-smoke/drives.png)
#   --shell-shot FILE  PNG of Notepad + launcher (default: build/distro/drives-smoke/session-shell.png)
#   --msi FILE         passed to msi-smoke.sh
#   --no-msi           don't run msi-smoke.sh afterwards
#   --keep             leave the VM running at the end (it still discards writes)
#   -h, --help
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
OUT=$REPO/build/distro/drives-smoke
IMAGE=$REPO/build/distro/setu-dev.qcow2
VNC=22
SSH_PORT=2234
SHOT=
SHELL_SHOT=
MSI_ARGS=()
RUN_MSI=1
KEEP=0

usage() { sed -n '2,/^set -euo/{/^set -euo/d;s/^# \{0,1\}//;p}' "$0"; }
while [ $# -gt 0 ]; do
    case $1 in
        --image) IMAGE=$2; shift ;;
        --vnc) VNC=$2; shift ;;
        --ssh-port) SSH_PORT=$2; shift ;;
        --shot) SHOT=$2; shift ;;
        --shell-shot) SHELL_SHOT=$2; shift ;;
        --msi) MSI_ARGS=(--msi "$2"); shift ;;
        --no-msi) RUN_MSI=0 ;;
        --keep) KEEP=1 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "unknown option: $1 (see --help)" >&2; exit 2 ;;
    esac
    shift
done
SHOT=${SHOT:-$OUT/drives.png}
SHELL_SHOT=${SHELL_SHOT:-$OUT/session-shell.png}
RUN=$OUT/run
mkdir -p "$OUT" "$RUN"
. "$HERE/lib.sh"
PATH=$PATH:/usr/sbin:/sbin

# --- the disks to plug in, made without root ---------------------------------
rm -rf "$OUT/disks"
mkdir -p "$OUT/disks/data"
echo "on the stick" > "$OUT/disks/OnStick.txt"
mkfs.vfat -C -n SETUSTICK "$OUT/disks/stick.img" 16384 >/dev/null
mcopy -i "$OUT/disks/stick.img" "$OUT/disks/OnStick.txt" ::OnStick.txt
echo "on the data disk" > "$OUT/disks/data/OnData.txt"
mkfs.ext4 -q -F -L SETUDATA -d "$OUT/disks/data" "$OUT/disks/data.img" 16M

# --- boot -------------------------------------------------------------------
boot_vm "$IMAGE" "$VNC"
wait_session

CDIR=/var/lib/setu/volumes/c
# gcmd 'COMMAND': run a cmd.exe command line in the session
gcmd() { gwine cmd /c "$1" | tr -d '\r'; }
drives() { gwine /opt/setu/bin/setu-mountctl.exe list | tr -d '\r'; }
# wait_drives REGEX [present|absent]: until the drive list matches (or not)
wait_drives() {
    local i
    for i in $(seq 30); do
        if drives | grep -Eq "$1"; then [ "${2:-present}" = present ] && return 0
        else [ "${2:-present}" = absent ] && return 0
        fi
        sleep 1
    done
    return 1
}

# --- 1. C: on ext4 casefold ---------------------------------------------------
c_target=$(gssh readlink .setu/prefix/dosdevices/c:)
[ "$c_target" = "$CDIR" ] || fail "the prefix's c: is $c_target, not $CDIR"
gssh "awk '\$2 == \"$CDIR\" { print \$3 }' /proc/mounts" | grep -qx ext4 || fail "$CDIR is not an ext4 mount"
gssh "sudo -n tune2fs -l \$(awk '\$2 == \"$CDIR\" { print \$1 }' /proc/mounts)" | grep -q '^Filesystem features:.*casefold' \
    || fail "the C: file system has no casefold feature"
attrs=$(gssh lsattr -d "$CDIR")
echo "  lsattr -d: $attrs"
echo "${attrs%% *}" | grep -q F || fail "no casefold flag (F) on $CDIR"
pass "C: is $CDIR: ext4 with casefold, +F on the volume root"

gcmd 'mkdir C:\Test && echo hello> C:\Test\MixedCase.txt' >/dev/null
[ "$(gcmd 'type c:\test\mixedcase.TXT')" = hello ] || fail "c:\\test\\mixedcase.TXT not found"
listing=$(gcmd 'dir /b c:\TEST')
[ "$listing" = MixedCase.txt ] || fail "dir /b c:\\TEST shows '$listing', not MixedCase.txt"
gssh ls "$CDIR/Test" | grep -qx MixedCase.txt || fail "Linux sees no $CDIR/Test/MixedCase.txt"
[ "$(gssh cat "$CDIR/TEST/mixedcase.TXT" | tr -d '\r')" = hello ] || fail "Linux lookup of TEST/mixedcase.TXT failed"
gssh lsattr -d "$CDIR/Test" | grep -q '^[^ ]*F' || fail "C:\\Test did not inherit +F"
pass "C:\\Test\\MixedCase.txt found as c:\\test\\mixedcase.TXT; listed as $listing; C:\\Test is casefolded too"

# --- 2. drive letters ---------------------------------------------------------
before=$(drives)
echo "$before" | sed 's/^/  /'
echo "$before" | grep -q '^C: type=fixed' || fail "no fixed C:"
echo "$before" | grep -q '^D:' && fail "D: exists before anything is plugged in"
gssh /usr/local/sbin/setu-drives list | grep -q "^C: $CDIR" || fail "C: is not in the system table"
pass "table: $(gssh /usr/local/sbin/setu-drives list | tr '\n' ';')"

monitor "drive_add 0 if=none,id=stick,format=raw,file=$OUT/disks/stick.img" | grep -q OK || fail "drive_add stick"
monitor "device_add usb-storage,id=stickdev,drive=stick,removable=on"
wait_drives '^D: ' || { drives; gssh journalctl -b -u 'setu-volume@*' --no-pager | tail -20; fail "no D: after plugging the stick"; }
d=$(drives | grep '^D:')
echo "$d" | grep -q '^D: type=removable fs=FAT[0-9]* label=SETUSTICK$' || fail "D: is '$d'"
gcmd 'dir D:\' > "$OUT/dir-d.txt"
grep -q 'OnStick.txt' "$OUT/dir-d.txt" || fail "dir D: does not list OnStick.txt (see $OUT/dir-d.txt)"
pass "stick plugged in: $d; dir D: lists OnStick.txt"

monitor "drive_add 0 if=none,id=data,format=raw,file=$OUT/disks/data.img" | grep -q OK || fail "drive_add data"
monitor "device_add usb-storage,id=datadev,drive=data"
wait_drives '^E: ' || fail "no E: after plugging the data disk"
e=$(drives | grep '^E:')
[ "$(gcmd 'type E:\ondata.TXT')" = "on the data disk" ] || fail "E:\\ondata.TXT not readable"
pass "data disk plugged in: $e"

# a Command Prompt showing the drives, for the screenshot
gwine /opt/setu/shell/setu-shell.exe --run 'cmd /k dir /b C:\Test & dir D:\ & dir E:\' \
    || fail "setu-shell --run cmd failed"
wait_window '\|.*cmd\.exe' 20 || true
sleep 2
screendump "$SHOT"
pass "screenshot: $SHOT"

monitor "device_del stickdev"
wait_drives '^D: ' absent || fail "D: still there after unplugging the stick"
gssh test -e /run/setu/dosdevices/d: && fail "d: still in the system table"
drives | grep -q '^E: ' || fail "E: went away with the stick"
pass "stick unplugged: D: gone, E: stays"

monitor "drive_add 0 if=none,id=stick2,format=raw,file=$OUT/disks/stick.img" | grep -q OK || fail "drive_add stick2"
monitor "device_add usb-storage,id=stickdev2,drive=stick2,removable=on"
wait_drives '^D: ' || fail "no D: after plugging the stick in again"
pass "stick plugged in again: $(drives | grep '^D:')"
monitor "device_del stickdev2"
monitor "device_del datadev"
wait_drives '^[DE]: ' absent || fail "D: or E: still there after unplugging both"
after=$(drives)
[ "$after" = "$before" ] || fail "drive list differs from the start: $after"
pass "both unplugged: back to $(echo "$after" | cut -d' ' -f1 | tr '\n' ' ')"

# --- 3. Notepad and the launcher ----------------------------------------------
gwine /opt/setu/shell/setu-shell.exe --run notepad.exe || fail "setu-shell --run notepad.exe failed"
wait_window '\|Notepad\|[^|]*\|[^|]*\|visible' 30 || fail "no Notepad window"
gwine /opt/setu/shell/setu-shell.exe --open-launcher || fail "setu-shell --open-launcher failed"
sleep 2
screendump "$SHELL_SHOT"
pass "screenshot: $SHELL_SHOT"

stop_vm
trap - EXIT
echo "drives checks passed"

# --- 4. msi-smoke ---------------------------------------------------------------
if [ "$RUN_MSI" = 1 ]; then
    "$HERE/msi-smoke.sh" --image "$IMAGE" "${MSI_ARGS[@]}"
fi
