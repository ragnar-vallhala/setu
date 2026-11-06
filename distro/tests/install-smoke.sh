#!/bin/bash
# SPDX-License-Identifier: MIT OR Apache-2.0
# End-to-end check of the installer ISO (distro/build-iso.sh): in QEMU
# (4 GB, UEFI/OVMF) with a blank disk,
#   1. boot the ISO; drive Calamares from the keyboard (QEMU sendkey):
#      Welcome, Partitions ("erase disk", preselected), Summary, Install;
#      wait for the end (serial log) and press Done, which reboots;
#   2. boot the installed disk (then eject the ISO): the first-boot wizard
#      (Calamares in OEM mode) comes up; accept the language, time zone and
#      keyboard pages, type the account and computer name from the answers
#      file (tests/install-answers.conf), set "Log in automatically" and
#      "Enable remote login (SSH)" as it says, and press Set Up;
#   3. the Setu session starts for the new account: over SSH (as that
#      account) check the shell's taskbar (setu-probe), the wizard's
#      results (hostname, locale, time zone, keyboard, sudo, autologin,
#      sshd), C: (the setu-c partition, ext4 with the casefold flag; a file
#      made by cmd is found in another case), setu-ntacld (a DACL set from
#      Windows lands in security.NTACL), .NET (the bundled Wine Mono), and
#      that the wizard is disabled;
#   4. reboot and check the session comes back without the wizard.
# Then a second install on a new blank disk with both checkboxes unticked:
#   5. the wizard reports a password login and sshd disabled (serial log),
#      and the guest's SSH port does not answer; tty1 shows a login prompt
#      (screendump: a text console); log in there with the account's
#      password and check the Setu desktop comes up (screendump).
# Screendumps of each stage go to distro/docs/install-*.png (--shots). The
# scratch disk and UEFI variables are deleted at the end (unless --keep).
#
# Usage: distro/tests/install-smoke.sh [options]
#   --iso FILE       installer ISO (default: build/distro/setu-installer.iso)
#   --answers FILE   first-boot answers (default: distro/tests/install-answers.conf)
#   --shots DIR      where the install-*.png go (default: distro/docs)
#   --disk-size N    the blank disk (default: 16G)
#   --vnc N          VNC display (default: 24)
#   --ssh-port N     host port for the guest's SSH (default: 2236)
#   --scenario S     answers: as in the answers file (steps 1-4), unticked
#                    (step 5 only), or both (default)
#   --keep           leave the VM running and the disk in place
#   -h, --help
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
OUT=$REPO/build/distro/install-smoke
ISO=$REPO/build/distro/setu-installer.iso
ANSWERS=$HERE/install-answers.conf
SHOTS=$REPO/distro/docs
DISK_SIZE=16G
VNC=24
SSH_PORT=2236
KEEP=0
SCENARIO=both

usage() { sed -n '2,/^set -euo/{/^set -euo/d;s/^# \{0,1\}//;p}' "$0"; }
while [ $# -gt 0 ]; do
    case $1 in
        --iso) ISO=$2; shift ;;
        --answers) ANSWERS=$2; shift ;;
        --shots) SHOTS=$2; shift ;;
        --disk-size) DISK_SIZE=$2; shift ;;
        --vnc) VNC=$2; shift ;;
        --ssh-port) SSH_PORT=$2; shift ;;
        --scenario) SCENARIO=$2; shift ;;
        --keep) KEEP=1 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "unknown option: $1 (see --help)" >&2; exit 2 ;;
    esac
    shift
done
RUN=$OUT/run
DISK=$OUT/disk.qcow2
mkdir -p "$OUT" "$RUN" "$SHOTS"
. "$HERE/lib.sh"
[ -f "$ISO" ] || fail "no ISO at $ISO (distro/build-iso.sh --shell build/shell/setu-shell.exe)"
[ -f "$ANSWERS" ] || fail "no answers file $ANSWERS"
case $SCENARIO in answers|unticked|both) ;; *) fail "--scenario must be answers, unticked or both" ;; esac
. "$ANSWERS"
TOOLCHAIN=${TOOLCHAIN:-$HOME/.local/setu-toolchain/bin}
export PATH=$TOOLCHAIN:$PATH:/usr/sbin:/sbin
x86_64-w64-mingw32-gcc -municode -O2 -Wall -Wextra -Werror -mconsole -o "$OUT/ntacl-probe.exe" \
    "$HERE/ntacl-probe.c" -ladvapi32 || fail "cannot build ntacl-probe.exe (needs mingw-w64: tools/bootstrap-toolchain.sh)"

cleanup() {
    stop_vm
    if [ "$KEEP" != 1 ]; then
        rm -f "$DISK" "$RUN/OVMF_VARS.fd" "$RUN/screen.ppm"
    fi
}
trap cleanup EXIT
shot() { screendump "$SHOTS/install-$1.png"; echo "  screenshot: install-$1.png"; }
start=$SECONDS
# page NAME: give the page time to draw, then a screendump
page() { sleep "${2:-4}"; shot "$1"; }

# install_disk: steps 1 and 2, with AUTOLOGIN and SSH as set
install_disk() {
# --- 0. a blank disk, fresh UEFI variables -----------------------------------
KEEP=0 stop_vm
rm -f "$DISK" "$RUN/OVMF_VARS.fd" "$RUN/serial.log"
qemu-img create -q -f qcow2 "$DISK" "$DISK_SIZE"
"$REPO/distro/run-qemu.sh" --image "$DISK" --cdrom "$ISO" --vnc "$VNC" --ssh-port "$SSH_PORT" \
    --run-dir "$RUN" --mem 4096 --daemonize

# --- 1. the installer ----------------------------------------------------------
wait_serial 'setu-installer: starting Calamares' 300 || fail "the live system did not start the installer (serial log: $RUN/serial.log)"
pass "live system up, Calamares starting ($((SECONDS - start))s)"
# Next stays disabled until every module's requirements are checked (the
# welcome page: disk, RAM, root; the partition page: the disk scan)
wait_serial 'All requirements have been checked' 300 \
    || { shot failed; fail "Calamares did not finish its requirement checks (serial log: $RUN/serial.log)"; }
page welcome 2
sendkey alt-n
page partition 6
sendkey alt-n
page summary 4
sendkey alt-i
page progress 30
wait_serial 'setu-install: installation finished' 1800 \
    || { shot failed; fail "the installation did not finish (serial log: $RUN/serial.log)"; }
pass "installed in $((SECONDS - start))s"
page finished 6
from=$(serial_lines)
# Done ("Restart now" is checked). The disk is first in the boot order, so
# the installed system boots with the ISO still in; it is ejected once that
# system is up (ejecting it earlier can pull the live system's squashfs
# away while it is still shutting down).
sendkey alt-d

# --- 2. the first-boot wizard ---------------------------------------------------
wait_serial 'setu-firstboot: starting the first-boot wizard' 300 "$from" \
    || { shot failed; fail "the installed system did not start the first-boot wizard (serial log: $RUN/serial.log)"; }
monitor 'eject -f cd0' >/dev/null || true
pass "installed system booted into the first-boot wizard (ISO ejected)"
wait_serial 'All requirements have been checked' 120 "$from" || true
page firstboot-welcome 2
sendkey alt-n
page firstboot-locale 5
sendkey alt-n
page firstboot-keyboard 5
sendkey alt-n
sleep 4
# users page: the focus starts in "What is your name?"; then the login
# name, the computer name (both suggested from the name: replace them), the
# password twice
type_text "$FULLNAME"
sendkey tab ctrl-a
type_text "$LOGIN"
sendkey tab ctrl-a
type_text "$HOSTNAME"
sendkey tab
type_text "$PASSWORD"
sendkey tab
type_text "$PASSWORD"
# then the "Log in automatically" checkbox (ticked by default)
[ "$AUTOLOGIN" = yes ] || sendkey tab spc
page firstboot-users 2
sendkey alt-n
# the remote login page: one checkbox in a list (unticked by default),
# which has the focus
sleep 4
[ "$SSH" = yes ] && sendkey spc
page firstboot-remote 2
sendkey alt-n
page firstboot-summary 4
sendkey alt-s
wait_serial 'setu-firstboot: done' 300 "$from" \
    || { shot failed; fail "the first-boot wizard did not finish (serial log: $RUN/serial.log)"; }
pass "first-boot wizard finished: $(tail -n +"$from" "$RUN/serial.log" | tr -d '\r' | grep -ao 'setu-firstboot: done.*' | head -1)"
}

if [ "$SCENARIO" != unticked ]; then
[ "$AUTOLOGIN" = yes ] && [ "$SSH" = yes ] || fail "steps 3-4 expect AUTOLOGIN=yes and SSH=yes in $ANSWERS"
install_disk
# --- 3. the session, as the new account -----------------------------------------
GUEST_USER=$LOGIN GUEST_PASS=$PASSWORD
for i in $(seq 90); do gssh true 2>/dev/null && break; sleep 2; done
gssh true || fail "no SSH as $LOGIN"
install_guest_env
# the first session creates the account's prefix (wineboot) before the shell
for i in $(seq 180); do gssh pgrep -f setu-shell.exe >/dev/null 2>&1 && break; sleep 2; done
wait_session
page session 5

# what the wizard set
[ "$(gssh hostname)" = "$HOSTNAME" ] || fail "hostname is '$(gssh hostname)', not $HOSTNAME"
[ "$(gssh cat /etc/hostname)" = "$HOSTNAME" ] || fail "/etc/hostname is not $HOSTNAME"
gssh grep -qx "LANG=$LOCALE" /etc/default/locale || fail "/etc/default/locale: $(gssh cat /etc/default/locale | xargs)"
tz=$(gssh readlink /etc/localtime)
[ "${tz#*zoneinfo/}" = "$TIMEZONE" ] || fail "/etc/localtime -> $tz, not $TIMEZONE"
kbd=$(gssh cat /etc/default/keyboard)
grep -Eq "^XKBLAYOUT=\"?$KEYMAP\"?\$" <<< "$kbd" || fail "/etc/default/keyboard: $(xargs <<< "$kbd")"
groups=$(gssh id -nG)
for g in sudo video render input audio; do
    echo " $groups " | grep -q " $g " || fail "$LOGIN is not in $g ($groups)"
done
gssh "echo $(printf %q "$PASSWORD") | sudo -S -p '' true" || fail "sudo with the account's password failed"
[ "$(gssh stat -c %U /var/lib/setu/volumes/c)" = "$LOGIN" ] || fail "C: does not belong to $LOGIN"
gssh test -f /var/lib/setu/firstboot.done || fail "no /var/lib/setu/firstboot.done"
[ "$(gssh systemctl is-enabled setu-firstboot.service)" = disabled ] || fail "setu-firstboot.service is still enabled"
[ "$(gssh systemctl is-enabled setu-session.service)" = enabled ] || fail "setu-session.service is not enabled (autologin)"
gssh id -nG | tr ' ' '\n' | grep -qx autologin || fail "$LOGIN is not in the autologin group"
[ "$(gssh systemctl is-enabled ssh.service)" = enabled ] || fail "ssh.service is not enabled"
gssh test ! -e /etc/calamares || fail "/etc/calamares is still there"
gssh "dpkg-query -W -f '\${Status}' live-boot 2>/dev/null | grep -q installed" && fail "live-boot is still installed"
pass "wizard: $LOGIN ($groups) on $HOSTNAME, $LOCALE, $TIMEZONE, keyboard $KEYMAP; sudo works; autologin and sshd on; wizard disabled"

# C: on its own casefold partition
src=$(gssh findmnt -n -o SOURCE /var/lib/setu/volumes/c)
[ "$(gssh lsblk -no PARTLABEL "$src")" = setu-c ] || fail "C: is on $src, not the setu-c partition"
[ "$(gssh lsblk -no PARTLABEL "$(gssh findmnt -n -o SOURCE /)")" = setu-root ] || fail "/ is not on setu-root"
gssh lsattr -d /var/lib/setu/volumes/c | cut -d' ' -f1 | grep -q F || fail "C: has no casefold flag: $(gssh lsattr -d /var/lib/setu/volumes/c)"
# (+F can only be set on a file system with the casefold feature)
[ "$(gssh findmnt -n -o FSTYPE /var/lib/setu/volumes/c)" = ext4 ] || fail "C: is not ext4"
gwine cmd /c 'mkdir C:\Test & echo hello> C:\Test\MixedCase.txt' >/dev/null 2>&1 || true
gssh test -f /var/lib/setu/volumes/c/test/mixedcase.TXT || fail "C:\\Test\\MixedCase.txt is not found as test/mixedcase.TXT"
gssh ls /var/lib/setu/volumes/c/Test | grep -qx MixedCase.txt || fail "the listing does not keep the case MixedCase.txt"
pass "C: is $src (setu-c, ext4, casefold flag); C:\\Test\\MixedCase.txt found as test/mixedcase.TXT"

# setu-ntacld
gssh systemctl is-active --quiet setu-ntacld.socket || fail "setu-ntacld.socket is not active"
gscp "$OUT/ntacl-probe.exe" "$LOGIN@127.0.0.1:/var/lib/setu/volumes/c/ntacl-probe.exe"
nprobe() { gwine 'C:\ntacl-probe.exe' "$@" | tr -d '\r'; }
[ "$(nprobe create 'C:\Test\acl')" = ok ] || fail "ntacl-probe create failed"
[ "$(nprobe set 'C:\Test\acl' 'D:P(A;;FR;;;WD)')" = ok ] || fail "ntacl-probe set failed"
names=$(gssh "getfattr -d -m - --absolute-names /var/lib/setu/volumes/c/Test/acl 2>/dev/null | sed -n 's/=.*//p' | xargs")
echo "$names" | grep -qw security.NTACL || fail "no security.NTACL on C:\\Test\\acl ($names)"
[ "$(nprobe open 'C:\Test\acl' write)" = "error 5" ] || fail "the DACL is not enforced"
pass "setu-ntacld: a DACL set from Windows is in security.NTACL ($names) and enforced"
check_dotnet

# --- 4. the wizard never runs again ---------------------------------------------
from=$(serial_lines)
gssh "echo $(printf %q "$PASSWORD") | sudo -S -p '' systemctl reboot" || true
sleep 10
for i in $(seq 90); do gssh true 2>/dev/null && break; sleep 2; done
install_guest_env
wait_session
tail -n +"$from" "$RUN/serial.log" | grep -aq 'setu-firstboot: starting' && fail "the first-boot wizard ran again"
pass "after a reboot the session starts directly (no wizard)"

fi

# --- 5. both checkboxes unticked: password login, no sshd -------------------------
if [ "$SCENARIO" != answers ]; then
AUTOLOGIN=no SSH=no
# the pages' screendumps go next to the run, not over the first install's
docs=$SHOTS
[ "$SCENARIO" = both ] && SHOTS=$OUT/unticked && mkdir -p "$SHOTS"
install_disk
SHOTS=$docs
done_line=$(tail -n +"$from" "$RUN/serial.log" | tr -d '\r' | grep -ao 'setu-firstboot: done.*' | head -1)
grep -q 'login: password, ssh: disabled' <<< "$done_line" || fail "the wizard did not set up a password login without sshd: $done_line"
grep -aq 'remote login (SSH) enabled' <(tail -n +"$from" "$RUN/serial.log") && fail "sshd was enabled"
sleep 10
gssh true 2>/dev/null && fail "the guest answers SSH"
pass "unticked: the wizard set up a password login and left sshd disabled (no SSH)"
# Without SSH the screen is the only view: a screendump's mean brightness,
# whole and of the bottom 5% (where the taskbar is). The text console is
# black (mean ~0.00); the Setu desktop is a mid blue (~0.37) with a dark
# taskbar (~0.13); labwc before the personality's desktop is black too.
means() { local ppm=$RUN/mean.ppm; rm -f "$ppm"; monitor "screendump $ppm" >/dev/null; sleep 1
          echo "$(convert "$ppm" -format '%[fx:mean]' info:) $(convert "$ppm" -gravity south \
              -crop 100%x5%+0+0 +repage -format '%[fx:mean]' info:)"; rm -f "$ppm"; }
console() { awk -v a="$1" 'BEGIN { exit !(a < 0.1) }'; }
desktop() { awk -v a="$1" -v b="$2" 'BEGIN { exit !(a > 0.25 && b < 0.2) }'; }
# tty1: the login prompt on the text console
for i in $(seq 30); do m=$(means); console $m && break; sleep 2; done
console $m || { shot failed; fail "tty1 is not a text console (screen means $m)"; }
shot login
type_text "$LOGIN"; sendkey ret
sleep 3
type_text "$PASSWORD"; sendkey ret
# the first session creates the account's prefix (wineboot) before the shell
for i in $(seq 90); do m=$(means); desktop $m && break; sleep 3; done
desktop $m || { shot failed; fail "no Setu desktop and taskbar after logging in on tty1 (screen means $m)"; }
sleep 5
shot password-session
pass "unticked: logged in on tty1 with the password, the Setu desktop and taskbar came up (screen means $m)"
fi

echo "install-smoke: PASS in $((SECONDS - start))s"
