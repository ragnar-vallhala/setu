#!/usr/bin/env bash
# SPDX-License-Identifier: MIT OR Apache-2.0
#
# Headless smoke test for Setu theme packs (docs/THEMES.md) and
# setu-theme.exe (shell/src/setu-theme-tool.c). It builds the shell, starts
# setu-shell and setu-files on the Setu personality inside Xvfb, and checks:
#   1. setu-theme current/list on a prefix with no pack: the built-in
#      setu-light,
#   2. setu-theme check: a throwaway pack "smoke-red" (red accent, a
#      full-colour folder icon and start button icon) passes; a pack with
#      grey text on white and a missing icon fails (exit 2),
#   3. setu-theme apply smoke-red: the registry keys of the 7 steps, and
#      both programs reload live (theme= in their state files) and repaint:
#      accent pixels turn red in the taskbar and the Files window, the pack's
#      folder icon shows in the file list and its setu-app icon on the start
#      button (pixels counted in screenshots), and the icon set the pack
#      names ([Icons] set=) gives the names the pack has no icon for,
#   4. setu-theme apply setu-light puts everything back.
# Screenshots go to build/shell/theme-smoke/*.png; with --update-docs they
# are also copied to shell/docs/.
#
# Usage: tools/theme-smoke.sh [--update-docs] [--keep]
#
# Environment:
#   SETU_WINE      wine binary (default: build/personality/install/bin/wine,
#                  falling back to the main checkout's build)
#   SETU_PREFIX    Wine prefix (default: build/theme-prefix; recreated)
#   SETU_RES       screen size (default: 1280x800)
set -euo pipefail

SMOKE_HOSTNAME=setu
case "${SETU_SMOKE_NS:-}" in
  "")
    if unshare -Ur --uts true 2>/dev/null; then
      exec env SETU_SMOKE_NS=1 SETU_SMOKE_UID="$(id -u)" SETU_SMOKE_GID="$(id -g)" \
        unshare -Ur --uts "$0" "$@"
    fi
    echo "FAIL: cannot create a user + UTS namespace (unshare -Ur --uts)" >&2
    exit 1 ;;
  1)
    hostname "$SMOKE_HOSTNAME"
    exec env SETU_SMOKE_NS=2 unshare -U --map-user="$SETU_SMOKE_UID" --map-group="$SETU_SMOKE_GID" "$0" "$@" ;;
esac
[ "$(hostname)" = "$SMOKE_HOSTNAME" ] || { echo "FAIL: host name is not $SMOKE_HOSTNAME" >&2; exit 1; }

REPO="$(cd "$(dirname "$0")/.." && pwd)"
UPDATE_DOCS=0
KEEP=0
for a in "$@"; do
  case "$a" in
    --update-docs) UPDATE_DOCS=1 ;;
    --keep) KEEP=1 ;;
    -h|--help) sed -n '3,28p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "unknown option: $a" >&2; exit 2 ;;
  esac
done

WINE="${SETU_WINE:-$REPO/build/personality/install/bin/wine}"
if [ ! -x "$WINE" ]; then
  main="$(git -C "$REPO" rev-parse --path-format=absolute --git-common-dir 2>/dev/null || true)"
  main="${main%/.git}"
  WINE="$main/build/personality/install/bin/wine"
fi
[ -x "$WINE" ] || { echo "no personality build found; run tools/build-personality.sh" >&2; exit 1; }
WINESERVER="$(dirname "$WINE")/wineserver"

export WINEPREFIX="${SETU_PREFIX:-$REPO/build/theme-prefix}"
export WINEDEBUG="${WINEDEBUG:--all}"
export WINEDLLOVERRIDES="mscoree,mshtml="
# keep this machine's disks (udisks on the system bus) out of the prefix
export DBUS_SYSTEM_BUS_ADDRESS=unix:path=/nonexistent
make -s -C "$REPO/shell" -j3   # before HOME moves: the Makefile finds the toolchain under it
export HOME="$REPO/build/theme-home" USER=setu LOGNAME=setu
export XDG_DATA_HOME="$HOME/.local/share" XDG_CONFIG_HOME="$HOME/.config" XDG_CACHE_HOME="$HOME/.cache"
RES="${SETU_RES:-1280x800}"
OUT="$REPO/build/shell/theme-smoke"
mkdir -p "$OUT"
rm -f "$OUT"/*.png "$OUT"/*.txt

SHELL_EXE="$REPO/build/shell/setu-shell.exe"
FILES_EXE="$REPO/build/shell/setu-files.exe"
THEME_EXE="$REPO/build/shell/setu-theme.exe"

# --- X server -------------------------------------------------------------
for n in $(seq 90 140); do
  [ -e "/tmp/.X11-unix/X$n" ] || [ -e "/tmp/.X$n-lock" ] || { DISP=$n; break; }
done
Xvfb ":$DISP" -screen 0 "${RES}x24" -nolisten tcp >/dev/null 2>&1 &
XVFB_PID=$!
export DISPLAY=":$DISP"
for _ in $(seq 50); do [ -e "/tmp/.X11-unix/X$DISP" ] && break; sleep 0.1; done

cleanup() {
  if [ "$KEEP" = 1 ]; then
    echo "kept running on DISPLAY=$DISPLAY (Xvfb pid $XVFB_PID); stop with: WINEPREFIX=$WINEPREFIX $WINESERVER -k"
    return
  fi
  "$WINESERVER" -k >/dev/null 2>&1 || true
  kill "$XVFB_PID" >/dev/null 2>&1 || true
}
trap cleanup EXIT

SSTATE_WIN='C:\shell-state.txt'
SSTATE="$WINEPREFIX/drive_c/shell-state.txt"
FSTATE_WIN='C:\files-state.txt'
FSTATE="$WINEPREFIX/drive_c/files-state.txt"
fail() {
  echo "FAIL: $*" >&2
  for f in "$SSTATE" "$FSTATE"; do
    [ -f "$f" ] && tr -d '\r' < "$f" | grep -E '^(theme|accent|taskbar_rect|window|start_button)' | sed "s|^|  $(basename "$f"): |" >&2
  done
  exit 1
}
pass() { echo "ok: $*"; }
shot() { import -display "$DISPLAY" -window root "$OUT/$1.png"; }
# wait_in FILE REGEX [SECONDS]: wait until the state file matches
wait_in() {
  local i
  for i in $(seq $(( ${3:-15} * 10 ))); do
    tr -d '\r' 2>/dev/null < "$1" | grep -Eq -- "$2" && return 0
    sleep 0.1
  done
  return 1
}
val() { tr -d '\r' < "$1" | sed -n "s/^$2=//p" | head -1; }
theme() { "$WINE" "$THEME_EXE" "$@" 2>&1 | tr -d '\r'; }
reg_query() { "$WINE" reg query "$1" /v "$2" 2>/dev/null | tr -d '\r' | awk -v v="$2" '$1 == v { $1 = ""; $2 = ""; sub(/^ +/, ""); print }'; }
# count PNG "L,T,R,B" COLOUR [FUZZ%]: pixels of that colour in the rectangle
count() {
  local l t r b
  IFS=, read -r l t r b <<< "$2"
  convert "$1" -crop "$((r - l))x$((b - t))+$l+$t" +repage -fuzz "${4:-4}%" \
    -fill black +opaque "$3" -fill white -opaque "$3" -colorspace gray -format "%[fx:round(mean*w*h)]" info:
}

# --- a fresh prefix -----------------------------------------------------------
rm -rf "$WINEPREFIX" "$HOME"
mkdir -p "$HOME"
"$WINE" wineboot -i >/dev/null 2>&1
"$WINE" reg add 'HKCU\Software\Wine\Explorer' /v Desktop /d setu /f >/dev/null 2>&1
"$WINE" reg add 'HKCU\Software\Wine\Explorer\Desktops' /v setu /d "$RES" /f >/dev/null 2>&1
"$WINESERVER" -w
find "$WINEPREFIX/dosdevices" -maxdepth 1 -type l -name '[a-y]:*' ! -name 'c:' -delete
mkdir -p "$WINEPREFIX/drive_c/theme-smoke/folder one" "$WINEPREFIX/drive_c/theme-smoke/folder two"
printf 'hello\r\n' > "$WINEPREFIX/drive_c/theme-smoke/a.txt"

# --- the throwaway packs, in %LOCALAPPDATA%\Setu\Themes -----------------------
THEMES="$WINEPREFIX/drive_c/users/setu/AppData/Local/Setu/Themes"
RED='#C42B1C'
MAGENTA='#FF00FF'   # the folder icon's colours: none of Setu's
CYAN='#00B4D8'      # the start button icon's
P="$THEMES/smoke-red"
mkdir -p "$P/icons"
cat > "$P/theme.ini" <<EOF
; a throwaway pack for tools/theme-smoke.sh
[Theme]
Id=smoke-red
Name=Smoke Red
Mode=light
Base=setu-light

[Colors]
accent=$RED
accent-hover=#D13A2A
accent-pressed=#A82417
accent-text=#FFFFFF

[Icons]
set=smoke-set
EOF
# an icon set (%LOCALAPPDATA%\Setu\Icons\<set>) that the pack names
ISET="$WINEPREFIX/drive_c/users/setu/AppData/Local/Setu/Icons/smoke-set"
mkdir -p "$ISET"
printf '[Icons]\r\ncomputer=computer.ico\r\n' > "$ISET/index.ini"
convert -size 32x32 xc:none -fill '#00A000' -draw 'rectangle 2,2 29,29' -define icon:auto-resize=32,16 "$ISET/computer.ico"
cat > "$P/icons/index.ini" <<'EOF'
[Icons]
folder=folder.ico
setu-app=setu-app.ico
EOF
# full-colour icons (two colours, so they are drawn as they are, not tinted)
convert -size 48x48 xc:none -fill "$MAGENTA" -draw 'rectangle 2,10 45,41' -fill '#00A000' -draw 'rectangle 2,4 20,10' \
  -define icon:auto-resize=48,32,24,16 "$P/icons/folder.ico"
convert -size 48x48 xc:none -fill "$CYAN" -draw 'roundrectangle 2,2 45,45 8,8' -fill '#FFFFFF' -draw 'rectangle 14,14 33,33' \
  -define icon:auto-resize=48,32,24,16 "$P/icons/setu-app.ico"
B="$THEMES/smoke-bad"
mkdir -p "$B/icons"
cat > "$B/theme.ini" <<'EOF'
[Theme]
Id=smoke-bad
Name=Smoke Bad
Mode=lightish

[Colors]
text=#EEEEEE
surface=#FFFFFF
accent=nope

[Metrics]
row-height=-3
EOF
printf '[Icons]\nfolder=missing.ico\n' > "$B/icons/index.ini"

# --- 1. no pack chosen: the built-in setu-light --------------------------------
CUR=$(theme current)
[ "$CUR" = "setu-light" ] || fail "setu-theme current on a fresh prefix: '$CUR'"
theme list > "$OUT/list.txt"
grep -Pq '^\* setu-light\t.*\tbuilt-in\t' "$OUT/list.txt" || { cat "$OUT/list.txt" >&2; fail "setu-theme list does not mark the built-in setu-light"; }
grep -Pq '^  smoke-red\tSmoke Red\tlight\tuser\t' "$OUT/list.txt" || { cat "$OUT/list.txt" >&2; fail "setu-theme list misses smoke-red"; }
pass "current=setu-light; list: $(grep -c . "$OUT/list.txt") lines"

# --- 2. check --------------------------------------------------------------------
rc=0; theme check 'C:\users\setu\AppData\Local\Setu\Themes\smoke-red' > "$OUT/check-red.txt" || rc=$?
[ "$rc" = 0 ] || { cat "$OUT/check-red.txt" >&2; fail "setu-theme check smoke-red exited $rc"; }
pass "check smoke-red: $(tail -1 "$OUT/check-red.txt")"
rc=0; theme check 'C:\users\setu\AppData\Local\Setu\Themes\smoke-bad' > "$OUT/check-bad.txt" || rc=$?
[ "$rc" = 2 ] || { cat "$OUT/check-bad.txt" >&2; fail "setu-theme check smoke-bad exited $rc, not 2"; }
for want in contrast accent row-height Mode missing.ico; do
  grep -q "$want" "$OUT/check-bad.txt" || { cat "$OUT/check-bad.txt" >&2; fail "check smoke-bad does not report $want"; }
done
pass "check smoke-bad: exit 2, $(grep -c '^error' "$OUT/check-bad.txt") errors"

# --- the programs ----------------------------------------------------------------
"$WINE" "$SHELL_EXE" --state-file "$SSTATE_WIN" >"$OUT/shell.log" 2>&1 &
wait_in "$SSTATE" '^launcher=closed' 60 || fail "the shell did not start"
"$WINE" "$FILES_EXE" --state-file "$FSTATE_WIN" 'C:\theme-smoke' >"$OUT/files.log" 2>&1 &
wait_in "$FSTATE" '^end=1' 60 || fail "setu-files did not start"
wait_in "$SSTATE" '^task=.*theme-smoke.*\|active' 15 || fail "no active taskbar button for Files"
sleep 1.5

TB=$(val "$SSTATE" taskbar_rect)
# the Files window below its caption (the frame's title text has colour
# fringes from subpixel smoothing, close to any blue)
IFS=, read -r FL FT FR FB <<< "$(val "$FSTATE" window)"
FW="$FL,$((FT + 34)),$FR,$FB"
LIGHT_ACCENT=$(val "$SSTATE" accent)
IFS=, read -r SX SY <<< "$(val "$SSTATE" start_button)"
SB="$((SX - 24)),$((SY - 22)),$((SX + 24)),$((SY + 22))"
measure() {  # measure NAME: screenshot, then the pixel counts
  shot "$1"
  local f="$OUT/$1.png"
  N_TB_RED=$(count "$f" "$TB" "$RED"); N_FW_RED=$(count "$f" "$FW" "$RED")
  N_TB_LIGHT=$(count "$f" "$TB" "$LIGHT_ACCENT"); N_FW_LIGHT=$(count "$f" "$FW" "$LIGHT_ACCENT")
  N_FOLDER=$(count "$f" "$FW" "$MAGENTA" 2); N_START=$(count "$f" "$SB" "$CYAN" 2)
  echo "$1: taskbar red=$N_TB_RED $LIGHT_ACCENT=$N_TB_LIGHT; files red=$N_FW_RED $LIGHT_ACCENT=$N_FW_LIGHT; folder icon=$N_FOLDER; start icon=$N_START" | tee -a "$OUT/pixels.txt"
}
measure theme-01-light
[ "$N_FW_LIGHT" -gt 20 ] || fail "no setu-light accent ($LIGHT_ACCENT) in the Files window before applying"
[ "$N_TB_RED" -lt 5 ] && [ "$N_FW_RED" -lt 5 ] && [ "$N_FOLDER" -lt 5 ] && [ "$N_START" -lt 5 ] ||
  fail "smoke-red colours on screen before applying it"
pass "before: setu-light (accent $LIGHT_ACCENT)"

# --- 3. apply smoke-red: both programs reload and repaint ----------------------
SR=$(val "$SSTATE" theme_reloads); FR=$(val "$FSTATE" theme_reloads)
theme apply smoke-red > "$OUT/apply-red.txt" || { cat "$OUT/apply-red.txt" >&2; fail "setu-theme apply smoke-red failed"; }
grep -q '^applied smoke-red' "$OUT/apply-red.txt" || { cat "$OUT/apply-red.txt" >&2; fail "no 'applied' line"; }
wait_in "$SSTATE" '^theme=smoke-red' 15 || fail "the shell did not reload smoke-red"
wait_in "$FSTATE" '^theme=smoke-red' 15 || fail "setu-files did not reload smoke-red"
wait_in "$SSTATE" "^accent=$RED" 5 || fail "the shell's accent is not $RED"
[ "$(val "$SSTATE" theme_reloads)" -gt "$SR" ] && [ "$(val "$FSTATE" theme_reloads)" -gt "$FR" ] || fail "theme_reloads did not grow"
pass "reloaded live: shell $(val "$SSTATE" theme_source) pack, $(val "$SSTATE" theme_reloads) reload(s); files $(val "$FSTATE" theme_reloads)"
[ "$(theme current)" = smoke-red ] || fail "setu-theme current is not smoke-red"
[ "$(reg_query 'HKCU\Software\Setu\Theme' Current)" = smoke-red ] || fail "HKCU\\Software\\Setu\\Theme\\Current"
[ "$(reg_query 'HKCU\Software\Setu\Theme\Colors' accent)" = "$RED" ] || fail "HKCU\\Software\\Setu\\Theme\\Colors\\accent"
[ "$(reg_query 'HKCU\Software\Setu\Theme\Metrics' caption-height)" = 0x1e ] || fail "HKCU\\Software\\Setu\\Theme\\Metrics\\caption-height"
[ "$(reg_query 'HKCU\Control Panel\Colors' Hilight)" = "196 43 28" ] || fail "Control Panel\\Colors\\Hilight is '$(reg_query 'HKCU\Control Panel\Colors' Hilight)'"
reg_query 'HKLM\Software\Microsoft\Windows\CurrentVersion\Explorer\Shell Icons' 3 | grep -qi 'smoke-red\\icons\\folder.ico' ||
  fail "Shell Icons 3 is not the pack's folder.ico"
reg_query 'HKLM\Software\Microsoft\Windows\CurrentVersion\Explorer\Shell Icons' 15 | grep -qi 'Icons\\smoke-set\\computer.ico' ||
  fail "Shell Icons 15 is not the icon set's computer.ico"
pass "registry: Current, Setu\\Theme\\Colors+Metrics, Control Panel\\Colors, Shell Icons (pack and icon set)"
sleep 1.5
measure theme-02-smoke-red
[ "$N_TB_RED" -gt 20 ] || fail "no red accent in the taskbar (the active button's pill)"
[ "$N_FW_RED" -gt 20 ] || fail "no red accent in the Files window"
[ "$N_FW_LIGHT" -lt 5 ] || fail "setu-light's accent still in the Files window"
[ "$N_FOLDER" -gt 100 ] || fail "the pack's folder icon is not in the file list"
[ "$N_START" -gt 100 ] || fail "the pack's setu-app icon is not on the start button"
pass "repainted: red accent, pack folder icon and start button icon on screen"

# --- 4. back to setu-light ------------------------------------------------------
theme apply setu-light > "$OUT/apply-light.txt" || { cat "$OUT/apply-light.txt" >&2; fail "setu-theme apply setu-light failed"; }
wait_in "$SSTATE" '^theme=setu-light' 15 || fail "the shell did not go back to setu-light"
wait_in "$FSTATE" '^theme=setu-light' 15 || fail "setu-files did not go back to setu-light"
wait_in "$SSTATE" "^accent=$LIGHT_ACCENT" 5 || fail "the shell's accent is not $LIGHT_ACCENT again"
reg_query 'HKLM\Software\Microsoft\Windows\CurrentVersion\Explorer\Shell Icons' 3 | grep -q . && fail "Shell Icons 3 left behind"
sleep 1.5
measure theme-03-light-again
[ "$N_TB_RED" -lt 5 ] && [ "$N_FW_RED" -lt 5 ] || fail "red left on screen after going back to setu-light"
[ "$N_FOLDER" -lt 5 ] && [ "$N_START" -lt 5 ] || fail "pack icons left on screen after going back to setu-light"
[ "$N_FW_LIGHT" -gt 20 ] || fail "setu-light's accent did not come back in the Files window"
pass "back to setu-light"

echo "screenshots: $OUT"
if [ "$UPDATE_DOCS" = 1 ]; then
  mkdir -p "$REPO/shell/docs"
  for f in theme-01-light theme-02-smoke-red; do
    convert "$OUT/$f.png" -strip -define png:compression-level=9 "$REPO/shell/docs/$f.png"
  done
  echo "copied to shell/docs/"
fi
echo "PASS"
