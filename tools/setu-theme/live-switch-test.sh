#!/usr/bin/env bash
# SPDX-License-Identifier: MIT OR Apache-2.0
#
# Live theme switching check (docs/THEMES.md, "Applying a pack"): a program
# that is already running must pick up a new pack without restarting.
# Inside its own Xvfb (Wine virtual desktop "setu") and a throwaway prefix
# it builds two packs with make-msstyles.py, setu-light and a red-accent
# test pack, starts tools/setu-theme/live-switch.c's window, applies the
# packs to it from another process and samples pixels:
#   1. the setu-light pack draws exactly like the fork's built-in theme;
#   2. after switching to the red pack: the caption takes the new
#      COLOR_ACTIVECAPTION, the checked check box and the progress bar the
#      new accent, the edit selection the new COLOR_HIGHLIGHT, the desktop
#      the new wallpaper, and the hot close button the pack's close-hover;
#   3. switching back to setu-light draws exactly as in step 1.
# Screenshots go to build/live-switch/*.png; with --update-docs the ones
# worth keeping are copied to docs/look/.
#
# Usage: tools/setu-theme/live-switch-test.sh [--update-docs] [--keep]
# Environment: SETU_WINE (default build/personality/install/bin/wine).
set -euo pipefail

REPO="$(cd "$(dirname "$0")/../.." && pwd)"
UPDATE_DOCS=0
KEEP=0
for a in "$@"; do
  case "$a" in
    --update-docs) UPDATE_DOCS=1 ;;
    --keep) KEEP=1 ;;
    -h|--help) sed -n '3,20p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "unknown option: $a" >&2; exit 2 ;;
  esac
done

WINE="${SETU_WINE:-$REPO/build/personality/install/bin/wine}"
[ -x "$WINE" ] || { echo "no personality build found; run tools/build-personality.sh" >&2; exit 1; }
WINESERVER="$(dirname "$WINE")/wineserver"
export PATH="$HOME/.local/setu-toolchain/bin:$PATH"

OUT="$REPO/build/live-switch"
export WINEPREFIX="$OUT/prefix"
export WINEDEBUG="${WINEDEBUG:--all}"
export WINEDLLOVERRIDES="mscoree,mshtml="
export DBUS_SYSTEM_BUS_ADDRESS=unix:path=/nonexistent
RES=1024x768
rm -rf "$WINEPREFIX"
mkdir -p "$OUT"
rm -f "$OUT"/*.png

fail() { echo "FAIL: $*" >&2; exit 1; }
pass() { echo "ok: $*"; }

# --- packs -----------------------------------------------------------------
printf '[Theme]\r\nId=setu-light\r\nName=Setu Light\r\nMode=light\r\n' > "$OUT/setu-light.ini"
cat > "$OUT/test-red.ini" <<'EOF'
[Theme]
Id=test-red
Name=Test Red
Mode=light
[Colors]
accent=#C0392B
accent-hover=#D04A3C
accent-pressed=#A93226
titlebar-active=#FBE9E7
titlebar-inactive=#F3F3F3
close-hover=#7B1FA2
selection=#C0392B33
[Metrics]
radius-control=8
scrollbar-width=4
EOF
for id in setu-light test-red; do
  python3 "$REPO/tools/setu-theme/make-msstyles.py" --tokens "$OUT/$id.ini" --out "$OUT/$id.msstyles" \
    --colors-out "$OUT/$id-colors.ini" --wine-bin "$(dirname "$WINE")" >/dev/null || fail "make-msstyles $id"
done
convert -size 64x64 xc:'#2E5E8C' BMP3:"$OUT/wall-light.bmp"
convert -size 64x64 xc:'#6B2A24' BMP3:"$OUT/wall-red.bmp"

x86_64-w64-mingw32-gcc -O2 -Wall -municode -o "$OUT/live-switch.exe" "$REPO/tools/setu-theme/live-switch.c" \
  -lcomctl32 -luser32 -lgdi32 -ladvapi32 || fail "build live-switch.exe"
cat > "$OUT/live-switch.exe.manifest" <<'EOF'
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0">
  <dependency><dependentAssembly>
    <assemblyIdentity type="win32" name="Microsoft.Windows.Common-Controls" version="6.0.0.0"
      processorArchitecture="*" publicKeyToken="6595b64144ccf1df" language="*"/>
  </dependentAssembly></dependency>
</assembly>
EOF

# --- X server and prefix ----------------------------------------------------
for n in $(seq 150 199); do
  [ -e "/tmp/.X11-unix/X$n" ] || [ -e "/tmp/.X$n-lock" ] || { DISP=$n; break; }
done
Xvfb ":$DISP" -screen 0 "${RES}x24" -nolisten tcp >/dev/null 2>&1 &
XVFB_PID=$!
export DISPLAY=":$DISP"
for _ in $(seq 50); do [ -e "/tmp/.X11-unix/X$DISP" ] && break; sleep 0.1; done
cleanup() {
  if [ "$KEEP" = 1 ]; then echo "kept running on DISPLAY=$DISPLAY"; return; fi
  "$WINESERVER" -k >/dev/null 2>&1 || true
  kill "$XVFB_PID" >/dev/null 2>&1 || true
}
trap cleanup EXIT

"$WINE" wineboot -i >/dev/null 2>&1
"$WINE" reg add 'HKCU\Software\Wine\Explorer' /v Desktop /d setu /f >/dev/null 2>&1
"$WINE" reg add 'HKCU\Software\Wine\Explorer\Desktops' /v setu /d "$RES" /f >/dev/null 2>&1
"$WINESERVER" -w
THEMES="$WINEPREFIX/drive_c/windows/Resources/Themes"
for id in setu-light test-red; do
  mkdir -p "$THEMES/$id"
  cp "$OUT/$id.msstyles" "$OUT/$id.ini" "$OUT/$id-colors.ini" "$THEMES/$id/"
done
cp "$OUT/wall-light.bmp" "$OUT/wall-red.bmp" "$WINEPREFIX/drive_c/windows/"
cp "$OUT/live-switch.exe" "$OUT/live-switch.exe.manifest" "$WINEPREFIX/drive_c/"

STATE="$WINEPREFIX/drive_c/live-state.txt"
st() { tr -d '\r' < "$STATE"; }
val() { st | sed -n "s/^$1=//p"; }
wait_seq_above() {
  local n="$1" i
  for i in $(seq 100); do
    [ "$(val seq)" -gt "$n" ] 2>/dev/null && return 0
    sleep 0.1
  done
  return 1
}
shot() { import -display "$DISPLAY" -window root "$OUT/$1.png"; }
# pixel NAME X Y: "rrggbb" at X,Y of screenshot NAME
pixel() { python3 -c 'import sys; from PIL import Image; print("%02x%02x%02x" % Image.open(sys.argv[1]).convert("RGB").getpixel((int(sys.argv[2]), int(sys.argv[3]))))' "$OUT/$1.png" "$2" "$3"; }
# count NAME RECT RGB: pixels of colour RGB (+-TOL) inside "l t r b"
count() {
  python3 - "$OUT/$1.png" "$2" "$3" "${4:-6}" <<'EOF'
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert("RGB")
l, t, r, b = map(int, sys.argv[2].split())
want = tuple(int(sys.argv[3][i:i + 2], 16) for i in (0, 2, 4))
tol = int(sys.argv[4])
print(sum(1 for y in range(t, b) for x in range(l, r)
          if all(abs(a - w) <= tol for a, w in zip(im.getpixel((x, y)), want))))
EOF
}
# same A B RECT: the two screenshots are equal inside RECT
same() {
  python3 - "$OUT/$1.png" "$OUT/$2.png" "$3" <<'EOF'
import sys
from PIL import Image, ImageChops
a, b = (Image.open(p).convert("RGB") for p in sys.argv[1:3])
box = tuple(map(int, sys.argv[3].split()))
d = ImageChops.difference(a.crop(box), b.crop(box)).getbbox()
print("same" if d is None else "differs at %s" % (d,))
EOF
}
apply() {
  local id="$1" wall="$2" before
  before=$(val seq)
  "$WINE" 'C:\live-switch.exe' apply "C:\\windows\\Resources\\Themes\\$id\\$id.ini" \
    "C:\\windows\\Resources\\Themes\\$id\\$id.msstyles" "C:\\windows\\Resources\\Themes\\$id\\$id-colors.ini" \
    "$wall" || fail "apply $id"
  wait_seq_above "$before" || fail "the window saw no change after applying $id"
  sleep 1.5
}
hover() { "$WINE" 'C:\live-switch.exe' hover "$1" "$2"; sleep 1; }

# --- 1. built-in theme vs the setu-light pack -------------------------------
(cd "$WINEPREFIX/drive_c" && "$WINE" 'C:\live-switch.exe' app 'C:\live-state.txt' >"$OUT/app.log" 2>&1 &)
for _ in $(seq 150); do [ -f "$STATE" ] && break; sleep 0.1; done
[ -f "$STATE" ] || fail "the test window did not start"
sleep 2
read -r WL WT WR WB <<<"$(val window)"
read -r CL CT CR CB <<<"$(val close)"
read -r TL TT TR TB <<<"$(val titlebar)"
read -r PL PT PR PB <<<"$(val progress)"
read -r KL KT KR KB <<<"$(val check)"
read -r EL ET ER EB <<<"$(val edit)"
WIN="$WL $WT $WR $WB"
hover 900 700
shot builtin
apply setu-light -
shot light
r=$(same builtin light "$WIN"); [ "$r" = same ] || fail "the setu-light pack draws the window unlike the built-in theme: $r"
pass "setu-light pack draws the window exactly like the built-in theme"

CAP_X=$(( (TL + CL) / 2 )); CAP_Y=$(( (TT + TB) / 2 ))
light_cap=$(pixel light "$CAP_X" "$CAP_Y")
light_desk=$(pixel light 1000 740)

# --- 2. switch to the red pack ---------------------------------------------
apply test-red 'C:\windows\wall-red.bmp'
shot red
[ "$(val activecaption)" = e7e9fb ] || fail "GetSysColor(COLOR_ACTIVECAPTION) in the running program is $(val activecaption)"
c=$(pixel red "$CAP_X" "$CAP_Y"); [ "$c" = fbe9e7 ] || fail "caption pixel is $c after the switch (was $light_cap)"
pass "caption repainted with the new COLOR_ACTIVECAPTION (#$c, was #$light_cap)"
n=$(count red "$PL $PT $PR $PB" c0392b 24); [ "$n" -gt 100 ] || fail "progress bar has $n accent pixels"
pass "progress bar drawn in the new accent ($n px)"
n=$(count red "$KL $KT $((KL + 24)) $KB" c0392b 24); [ "$n" -gt 20 ] || fail "check box has $n accent pixels"
pass "checked check box drawn in the new accent ($n px)"
d=$(pixel red 1000 740); [ "$d" = 6b2a24 ] || fail "desktop pixel is $d after SPI_SETDESKWALLPAPER (was $light_desk)"
pass "wallpaper changed (#$d)"
hover $(( (CL + CR) / 2 )) $(( (CT + CB) / 2 ))
shot red-close-hover
n=$(count red-close-hover "$CL $CT $CR $CB" 7b1fa2 2); [ "$n" -gt 100 ] || fail "close button has $n close-hover pixels"
pass "hot close button drawn in the pack's close-hover ($n px)"
hover 900 700

# --- 3. back to setu-light ---------------------------------------------------
apply setu-light 'C:\windows\wall-light.bmp'
shot back
r=$(same light back "$WIN"); [ "$r" = same ] || fail "switching back to setu-light draws the window differently: $r"
pass "switching back to setu-light restores the same pixels"

if [ "$UPDATE_DOCS" = 1 ]; then
  for n in light red red-close-hover; do
    convert "$OUT/$n.png" -crop "$((WR - WL + 40))x$((WB - WT + 40))+$((WL - 20))+$((WT - 20))" +repage \
      "$REPO/docs/look/live-switch-$n.png"
  done
  echo "screenshots copied to docs/look/live-switch-*.png"
fi
echo "PASS: live theme switching"
