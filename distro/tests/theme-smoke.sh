#!/bin/bash
# SPDX-License-Identifier: MIT OR Apache-2.0
# Theme packs on the dev image (docs/THEMES.md, distro/themes): boot it in
# QEMU (--snapshot: the image is left untouched), wait for the session, then
#   1. the default pack is active: HKCU\Software\Setu\Theme\Current is
#      setu-light or unset, the taskbar shows setu-light's [Colors] taskbar
#      (QEMU screendump), the packs, icon sets, wallpaper collection,
#      cursors and sounds are on C:, labwc's config is setu-light's;
#   2. `setu-theme apply setu-dark` changes the running desktop: Current,
#      the taskbar turns setu-dark's colour, the wallpaper changes;
#   3. log out and in (setu-session.service restarted): setu-dark is still
#      active: Current, the taskbar colour, ~/.config/labwc (border colour,
#      XCURSOR_THEME), the session's XCURSOR_THEME, and the boot splash
#      (/etc/plymouth/plymouthd.conf, rebuilt by setu-boot-theme).
# Without setu-theme.exe in the image (built without theme-shell's tool),
# step 2 is skipped and step 3 starts from Current written with `wine reg`.
#
# Usage: distro/tests/theme-smoke.sh [options]
#   --image FILE      dev image (default: build/distro/setu-dev.qcow2)
#   --vnc N           VNC display (default: 22)
#   --ssh-port N      host port for the guest's SSH (default: 2234)
#   --update-docs     copy the screenshots to distro/docs/theme-*.png
#   --keep            leave the VM running at the end (it still discards writes)
#   -h, --help
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
OUT=$REPO/build/distro/theme-smoke
IMAGE=$REPO/build/distro/setu-dev.qcow2
VNC=22
SSH_PORT=2234
KEEP=0
DOCS=0

usage() { sed -n '2,/^set -euo/{/^set -euo/d;s/^# \{0,1\}//;p}' "$0"; }
while [ $# -gt 0 ]; do
    case $1 in
        --image) IMAGE=$2; shift ;;
        --vnc) VNC=$2; shift ;;
        --ssh-port) SSH_PORT=$2; shift ;;
        --update-docs) DOCS=1 ;;
        --keep) KEEP=1 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "unknown option: $1 (see --help)" >&2; exit 2 ;;
    esac
    shift
done
RUN=$OUT/run
mkdir -p "$OUT" "$RUN"
. "$HERE/lib.sh"
[ -f "$IMAGE" ] || fail "no image at $IMAGE (distro/build-image.sh)"
python3 -c 'import PIL' 2>/dev/null || fail "needs python3-pil on the host"

# the [Colors] value KEY of a source pack (distro/themes/ID/theme.ini, its
# Base= chain)
pack_color() { # ID KEY
    python3 -c 'import sys; sys.path.insert(0, sys.argv[1]); import themelib; print(themelib.Pack(sys.argv[2]).color(sys.argv[3]))' \
        "$REPO/distro/themes" "$1" "$2"
}
# shot NAME: a full-size screendump to $OUT/NAME.ppm and a PNG next to it
shot() {
    rm -f "$OUT/$1.ppm"
    monitor "screendump $OUT/$1.ppm" >/dev/null
    local i
    for i in $(seq 20); do [ -s "$OUT/$1.ppm" ] && break; sleep 0.5; done
    sleep 0.5
    [ -s "$OUT/$1.ppm" ] || fail "screendump failed"
    python3 -c 'import sys; from PIL import Image; Image.open(sys.argv[1]).save(sys.argv[2], optimize=True)' "$OUT/$1.ppm" "$OUT/$1.png"
}
# taskbar NAME: the colour of the taskbar's empty left end (the median of a
# strip 8-120 px from the left edge, 12 px above the bottom), as RRGGBB
taskbar() {
    python3 - "$OUT/$1.ppm" <<'EOF'
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert('RGB')
w, h = im.size
px = sorted(im.getpixel((x, h - 12)) for x in range(8, 120))
print('%02X%02X%02X' % px[len(px) // 2])
EOF
}
# desktop NAME: the mean colour of the desktop's middle (above the taskbar)
desktop() {
    python3 - "$OUT/$1.ppm" <<'EOF'
import sys
from PIL import Image, ImageStat
im = Image.open(sys.argv[1]).convert('RGB')
w, h = im.size
print('%02X%02X%02X' % tuple(round(v) for v in ImageStat.Stat(im.crop((w // 4, h // 8, w * 3 // 4, h * 5 // 8))).mean))
EOF
}
# near A B [TOL]: two RRGGBB colours differ by at most TOL per channel
near() {
    local a=${1#\#} b=${2#\#} t=${3:-24} i
    for i in 0 2 4; do
        local d=$(( 16#${a:i:2} - 16#${b:i:2} ))
        [ ${d#-} -le "$t" ] || return 1
    done
}
current() {
    gwine reg query 'HKCU\Software\Setu\Theme' /v Current 2>/dev/null | tr -d '\r' \
        | sed -n 's/^ *Current *REG_SZ *//p'
}
# wait_taskbar NAME COLOUR SECONDS: screendumps until the taskbar is COLOUR
wait_taskbar() {
    local i
    for (( i = 0; i < $3; i += 2 )); do
        shot "$1"
        near "$(taskbar "$1")" "$2" && return 0
        sleep 2
    done
    return 1
}

LIGHT_BAR=$(pack_color setu-light taskbar)
DARK_BAR=$(pack_color setu-dark taskbar)
DARK_BORDER=$(pack_color setu-dark border)

# --- boot -------------------------------------------------------------------
boot_vm "$IMAGE" "$VNC"
wait_session

# --- 1. setu-light ------------------------------------------------------------
cur=$(current)
case $cur in ''|setu-light) ;; *) fail "Current is '$cur', expected setu-light or unset" ;; esac
pass "registry: Current='${cur:-<unset>}' (setu-light)"
wait_taskbar setu-light "$LIGHT_BAR" 30 || fail "taskbar is #$(taskbar setu-light), expected setu-light's $LIGHT_BAR (see $OUT/setu-light.png)"
pass "taskbar pixel #$(taskbar setu-light) ~ setu-light $LIGHT_BAR"
light_desk=$(desktop setu-light)

C='$HOME/.setu/prefix/dosdevices/c:/windows'
n=$(gssh "ls -d $C/Resources/Themes/setu-*/ | wc -l")
[ "$n" -ge 2 ] || fail "only $n packs in C:\\Windows\\Resources\\Themes"
for f in "Resources/Themes/setu-light/setu-light.theme" "Resources/Themes/setu-dark/setu-dark.theme" \
         "Resources/Themes/setu-light/wallpaper/wallpaper.bmp" "Resources/Setu/Icons/setu-colour/index.ini" \
         "Cursors/Setu/setu-dark/arrow.cur" "Media/Setu/setu-chimes/sounds.ini"; do
    gssh "test -f $C/$f" || fail "C:\\Windows\\${f//\//\\} is missing"
done
n=$(gssh "ls $C/Web/Wallpaper/Setu/*.jpg | wc -l")
[ "$n" = 48 ] || fail "$n wallpapers in C:\\Windows\\Web\\Wallpaper\\Setu, expected 48"
pass "C: has $(gssh "ls -d $C/Resources/Themes/setu-*/ | wc -l") packs, 5 icon sets, cursors, sounds, $n collection wallpapers"
gssh grep -q XCURSOR_THEME=setu-light .config/labwc/environment || fail "~/.config/labwc/environment is not setu-light's"
pass "labwc: setu-light (~/.config/labwc)"

# --- 2. setu-theme apply setu-dark ---------------------------------------------
if gssh test -f /opt/setu/shell/setu-theme.exe; then
    rc=0; gwine /opt/setu/shell/setu-theme.exe apply setu-dark > "$OUT/apply.log" 2>&1 || rc=$?
    [ "$rc" = 0 ] || fail "setu-theme apply setu-dark: exit $rc ($(tr -d '\r' < "$OUT/apply.log" | tail -3 | xargs))"
    [ "$(current)" = setu-dark ] || fail "Current is '$(current)' after apply"
    wait_taskbar setu-dark-live "$DARK_BAR" 30 \
        || fail "taskbar is #$(taskbar setu-dark-live) after apply, expected setu-dark's $DARK_BAR (see $OUT/setu-dark-live.png)"
    pass "live: taskbar #$(taskbar setu-dark-live) ~ setu-dark $DARK_BAR"
    dark_desk=$(desktop setu-dark-live)
    near "$dark_desk" "$light_desk" 8 && fail "the desktop did not change (mean #$light_desk -> #$dark_desk)"
    pass "live: desktop mean #$light_desk -> #$dark_desk"
else
    echo "SKIP: live apply (no /opt/setu/shell/setu-theme.exe in the image); setting Current with wine reg"
    gwine reg add 'HKCU\Software\Setu\Theme' /v Current /d setu-dark /f >/dev/null
fi

# --- 3. log out and in -----------------------------------------------------------
gssh sudo -n systemctl restart setu-session.service
sleep 5
install_guest_env
wait_session
[ "$(current)" = setu-dark ] || fail "Current is '$(current)' after logging in again"
pass "registry after login: Current=setu-dark"
wait_taskbar setu-dark "$DARK_BAR" 30 || fail "taskbar is #$(taskbar setu-dark) after login, expected $DARK_BAR (see $OUT/setu-dark.png)"
pass "taskbar after login #$(taskbar setu-dark) ~ setu-dark $DARK_BAR"
gssh grep -q XCURSOR_THEME=setu-dark .config/labwc/environment || fail "~/.config/labwc/environment is not setu-dark's"
gssh "grep -qi 'border.color: $DARK_BORDER' .config/labwc/themerc-override" || fail "~/.config/labwc/themerc-override has no setu-dark border colour $DARK_BORDER"
env=$(gssh 'sudo -n cat /proc/$(pgrep -x setu-shell.exe | head -1)/environ | tr "\0" "\n"' | grep '^XCURSOR_THEME=' || true)
[ "$env" = XCURSOR_THEME=setu-dark ] || fail "the session has '$env', expected XCURSOR_THEME=setu-dark"
pass "labwc and cursor after login: setu-dark ($env)"
for i in $(seq 60); do
    gssh grep -q '^Theme=setu-dark' /etc/plymouth/plymouthd.conf && ! gssh pgrep -f update-initramfs >/dev/null && break
    sleep 3
done
gssh grep -q '^Theme=setu-dark' /etc/plymouth/plymouthd.conf || fail "boot splash: $(gssh grep ^Theme= /etc/plymouth/plymouthd.conf)"
gssh sudo -n lsinitramfs /boot/initrd.img-\$\(uname -r\) | grep -q 'plymouth/themes/setu-dark/setu.script' \
    || fail "the initramfs has no setu-dark boot splash"
pass "boot splash: setu-dark (plymouthd.conf and the initramfs)"

if [ "$DOCS" = 1 ]; then
    mkdir -p "$REPO/distro/docs"
    for n in setu-light setu-dark; do
        python3 -c 'import sys; from PIL import Image; im = Image.open(sys.argv[1]); im.thumbnail((1280, 1280)); im.save(sys.argv[2], optimize=True)' \
            "$OUT/$n.png" "$REPO/distro/docs/theme-$n.png"
    done
    pass "screenshots: distro/docs/theme-setu-light.png, theme-setu-dark.png"
fi
rm -f "$OUT"/*.ppm
echo "all checks passed"
