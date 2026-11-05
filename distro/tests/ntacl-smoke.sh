#!/bin/bash
# SPDX-License-Identifier: MIT OR Apache-2.0
# Smoke test for NT security descriptors in security.NTACL
# (services/fs/README.md, "NT security descriptors"). Boots the dev image
# (QEMU, --snapshot) and, over SSH:
#   1. setu-ntacld.socket listens on /run/setu/ntacl.sock; the helper's only
#      capability is CAP_SYS_ADMIN, and the wineserver doesn't have it;
#   2. a DACL set from Windows (ntacl-probe.exe, SetFileSecurityW) on a file
#      on C: (the Setu system volume) lands in security.NTACL (not user.setu.sd), the file's owner is
#      Administrators (the token's default owner), and the DACL is enforced:
#      a write open is denied, a read open allowed;
#   3. the host user cannot remove security.NTACL (setfattr -x fails);
#   4. after `wineserver -k` a new wineserver reads the same descriptor back
#      and still enforces it;
#   5. a file outside the Setu volumes (Z:\tmp) gets the user.setu.sd fallback:
#      the helper refuses it (EXDEV) and nothing is written to security.NTACL.
#
# Usage: distro/tests/ntacl-smoke.sh [options]
#   --image FILE     dev image (default: build/distro/setu-dev.qcow2)
#   --vnc N          VNC display (default: 23)
#   --ssh-port N     host port for the guest's SSH (default: 2235)
#   --inject         copy this checkout's build into the running image first:
#                    the personality (build/personality/install), setu-ntacld
#                    and its units (make -C services/fs), getfattr/setfattr if
#                    the image lacks them; then restart the session. For an
#                    image built before setu-ntacld; writes are discarded.
#   --keep           leave the VM running at the end (it still discards writes)
#   -h, --help
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
OUT=$REPO/build/distro/ntacl-smoke
IMAGE=$REPO/build/distro/setu-dev.qcow2
VNC=23
SSH_PORT=2235
INJECT=0
KEEP=0

usage() { sed -n '2,/^set -euo/{/^set -euo/d;s/^# \{0,1\}//;p}' "$0"; }
while [ $# -gt 0 ]; do
    case $1 in
        --image) IMAGE=$2; shift ;;
        --vnc) VNC=$2; shift ;;
        --ssh-port) SSH_PORT=$2; shift ;;
        --inject) INJECT=1 ;;
        --keep) KEEP=1 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "unknown option: $1 (see --help)" >&2; exit 2 ;;
    esac
    shift
done
RUN=$OUT/run
mkdir -p "$OUT" "$RUN"
. "$HERE/lib.sh"
TOOLCHAIN=${TOOLCHAIN:-$HOME/.local/setu-toolchain/bin}
export PATH=$TOOLCHAIN:$PATH:/usr/sbin:/sbin

# --- the probe, built here ----------------------------------------------------
x86_64-w64-mingw32-gcc -municode -O2 -Wall -Wextra -Werror -mconsole -o "$OUT/ntacl-probe.exe" \
    "$HERE/ntacl-probe.c" -ladvapi32 || fail "cannot build ntacl-probe.exe (needs mingw-w64: tools/bootstrap-toolchain.sh)"
if [ "$INJECT" = 1 ]; then
    make -s -C "$REPO/services/fs" install DESTDIR="$OUT/overlay" || fail "cannot build services/fs"
    [ -x "$REPO/build/personality/install/bin/wine" ] || fail "no personality build in $REPO/build/personality/install"
fi

# --- boot -------------------------------------------------------------------
boot_vm "$IMAGE" "$VNC"
wait_session

if [ "$INJECT" = 1 ]; then
    gssh 'sudo -n systemctl stop setu-session.service; /tmp/setu-env wineserver -k 2>/dev/null; sleep 1; pkill -u setu -x wineserver || true'
    tar -C "$OUT/overlay" -cf - . | gssh 'sudo -n tar -C / --no-same-owner -xf -'
    tar -C "$REPO/build/personality/install" -cf - . | gssh 'sudo -n tar -C /opt/setu/personality --no-same-owner -xf -'
    gssh command -v getfattr >/dev/null || {
        gscp /usr/bin/getfattr /usr/bin/setfattr setu@127.0.0.1:/tmp/
        gssh 'sudo -n install -m 0755 /tmp/getfattr /tmp/setfattr /usr/local/bin/'
    }
    gssh 'sudo -n systemctl daemon-reload && sudo -n systemctl enable --now setu-ntacld.socket && sudo -n systemctl start setu-session.service'
    wait_session
    pass "injected this checkout's personality and setu-ntacld"
fi

# The Setu volume to test on: C: (/var/lib/setu/volumes/c). An image from
# before the C: volume (only with --inject) gets a tmpfs mounted at
# /var/lib/setu/volumes/t as T: instead.
L=c
VDIR=/var/lib/setu/volumes/c
if ! gssh mountpoint -q $VDIR; then
    [ "$INJECT" = 1 ] || fail "$VDIR is not mounted (an image from before the C: volume? try --inject)"
    L=t
    VDIR=/var/lib/setu/volumes/t
    gssh "sudo -n mkdir -p $VDIR && sudo -n mount -t tmpfs -o size=64M,mode=0755 setu-t $VDIR &&
          sudo -n chown setu:setu $VDIR && ln -sfn $VDIR .setu/prefix/dosdevices/t:"
    pass "no C: volume in this image: testing on T: ($VDIR, a tmpfs)"
fi
U=${L^^}
gscp "$OUT/ntacl-probe.exe" setu@127.0.0.1:/tmp/ntacl-probe.exe
gssh "mkdir -p $VDIR/ntacl-smoke && cp /tmp/ntacl-probe.exe $VDIR/ntacl-smoke/"
# probe ARGS...: run ntacl-probe.exe in the session, its output without CRs
probe() { gwine "$U:\\ntacl-smoke\\ntacl-probe.exe" "$@" | tr -d '\r'; }
# xattrs FILE: the names of FILE's extended attributes, on one line
xattrs() { gssh "getfattr -d -m - --absolute-names '$1' 2>/dev/null | sed -n 's/=.*//p' | tr '\n' ' '"; }
cap_eff() { gssh "awk '/^CapEff/ { print \$2 }' /proc/$1/status"; }

# --- 1. the helper -------------------------------------------------------------
gssh systemctl is-active --quiet setu-ntacld.socket || fail "setu-ntacld.socket is not active"
gssh test -S /run/setu/ntacl.sock || fail "no /run/setu/ntacl.sock"
bset=$(gssh systemctl show -p CapabilityBoundingSet --value setu-ntacld.service)
[ "$bset" = cap_sys_admin ] || fail "setu-ntacld's capability bounding set is '$bset', not cap_sys_admin"
pass "setu-ntacld.socket listening; the service's bounding set is $bset"

# --- 2. a DACL from Windows lands in security.NTACL ------------------------------
F="$U:\\ntacl-smoke\\f"
LF=$VDIR/ntacl-smoke/f
[ "$(probe create "$F")" = ok ] || fail "cannot create $F"
[ "$(probe set "$F" 'D:P(A;;FA;;;BA)(A;;FR;;;WD)')" = ok ] || fail "cannot set the first DACL"
[ "$(probe open "$F" write)" = ok ] || fail "write open denied under D:P(A;;FA;;;BA)(A;;FR;;;WD)"
[ "$(probe set "$F" 'D:P(A;;FR;;;WD)')" = ok ] || fail "cannot set the read-only DACL"
names=$(xattrs "$LF")
echo "  $LF: $names"
echo "$names" | grep -qw security.NTACL || fail "no security.NTACL on $LF"
echo "$names" | grep -qw user.setu.sd && fail "$LF still has user.setu.sd"
first=$(gssh "getfattr -n security.NTACL -e hex --absolute-names '$LF'" | sed -n 's/^security.NTACL=0x//p' | cut -c1-4)
[ "$first" = 0101 ] || fail "security.NTACL starts with '$first', not format 1 + revision 1"
sd=$(probe get "$F")
echo "  $F: $sd"
case $sd in O:BAG:*D:P\(A\;\;FR\;\;\;WD\)) ;; *) fail "SD is '$sd'" ;; esac
[ "$(probe open "$F" write)" = "error 5" ] || fail "write open not denied under D:P(A;;FR;;;WD)"
[ "$(probe open "$F" read)" = ok ] || fail "read open denied under D:P(A;;FR;;;WD)"
pass "DACL in security.NTACL, owner BA, write denied and read allowed"

wpid=$(gssh pgrep -u setu -x wineserver | head -1)
[ -n "$wpid" ] || fail "no wineserver"
wcap=$(cap_eff "$wpid")
(( (16#$wcap & 16#200000) == 0 )) || fail "the wineserver has CAP_SYS_ADMIN (CapEff $wcap)"
hpid=$(gssh systemctl show -p MainPID --value setu-ntacld.service)
if [ -n "$hpid" ] && [ "$hpid" != 0 ]; then
    [ "$(cap_eff "$hpid")" = 0000000000200000 ] || fail "setu-ntacld's effective capabilities are $(cap_eff "$hpid"), not CAP_SYS_ADMIN alone"
    pass "wineserver (pid $wpid, CapEff $wcap) lacks CAP_SYS_ADMIN; setu-ntacld (pid $hpid) has CAP_SYS_ADMIN only"
else
    pass "wineserver (pid $wpid, CapEff $wcap) lacks CAP_SYS_ADMIN"
fi

# --- 3. the host user can't strip it --------------------------------------------
gssh "setfattr -x security.NTACL '$LF'" 2>/dev/null && fail "the setu user removed security.NTACL"
pass "setfattr -x security.NTACL as the user is refused"

# --- 4. a wineserver restart ---------------------------------------------------
# stop the session first, or it starts its programs (and a wineserver) again
gssh 'sudo -n systemctl stop setu-session.service; /tmp/setu-env wineserver -k
      for i in $(seq 60); do pgrep -u setu -x wineserver >/dev/null || exit 0; sleep 0.5; done; exit 1' \
    || fail "the wineserver did not exit"
[ "$(gssh pgrep -u setu -x wineserver)" = "" ] || fail "a wineserver is still running"
[ "$(probe get "$F")" = "$sd" ] || fail "after wineserver -k the SD is '$(probe get "$F")'"
[ "$(probe open "$F" write)" = "error 5" ] || fail "write open not denied after wineserver -k"
pass "after the wineserver exited (pid $wpid) a new one reads $sd, still enforced"

# --- 5. outside the Setu volumes: the fallback ---------------------------------
gssh 'rm -rf /tmp/ntacl-smoke && mkdir /tmp/ntacl-smoke'
[ "$(probe create 'Z:\tmp\ntacl-smoke\g')" = ok ] || fail "cannot create Z:\\tmp\\ntacl-smoke\\g"
[ "$(probe set 'Z:\tmp\ntacl-smoke\g' 'D:P(A;;FR;;;WD)')" = ok ] || fail "cannot set a DACL on Z:\\tmp\\ntacl-smoke\\g"
names=$(xattrs /tmp/ntacl-smoke/g)
echo "  /tmp/ntacl-smoke/g: $names"
echo "$names" | grep -qw user.setu.sd || fail "no user.setu.sd fallback on /tmp/ntacl-smoke/g"
echo "$names" | grep -qw security.NTACL && fail "security.NTACL written outside the Setu volumes"
[ "$(probe open 'Z:\tmp\ntacl-smoke\g' write)" = "error 5" ] || fail "the fallback DACL is not enforced"
pass "Z:\\tmp file: user.setu.sd fallback, enforced"

stop_vm
trap - EXIT
echo "ntacl checks passed"
