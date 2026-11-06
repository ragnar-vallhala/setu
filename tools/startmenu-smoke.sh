#!/usr/bin/env bash
# SPDX-License-Identifier: MIT OR Apache-2.0
#
# Headless smoke test for the Setu start menu (shell/src/setu-shell.c). It
# runs the shell on the Setu personality inside Xvfb, in a throwaway prefix,
# with shortcuts pointing at shell/tests/setu-testapp.exe, and checks:
#   1. discovery: .lnk files in the per-user and common Programs folders and
#      their subfolders; a folder with several apps is a group (nested
#      folders are folded into it), a folder with one app is not; per-user
#      and common copies of a link are listed once; icons come from the
#      link's icon location, apps without one get our glyph; Uninstall
#      entries match their shortcuts, and the others are "Other apps"
#      (SystemComponent ones are hidden),
#   2. the Windows key opens the menu and typing searches; Enter starts the
#      best match,
#   3. Ctrl+Esc opens it on the pins; arrow keys move through the pins, up
#      to the "All apps" button, and Enter there shows the list, where they
#      move through the rows; Esc closes,
#   4. clicking "All apps" shows the list, and clicking a group expands it,
#   5. a shortcut added while the menu is open shows up, and goes away again
#      when deleted (no restart),
#   6. right-click > Pin pins an app (HKCU\Software\Setu\Shell "Pinned"),
#   7. right-click > Uninstall on a group runs its UninstallString,
#   8. text that matches no app runs as a command,
#   9. after a restart the pin is still there.
# Screenshots go to build/shell/startmenu/*.png; with --update-docs they are
# also copied to shell/docs/.
#
# Usage: tools/startmenu-smoke.sh [--update-docs] [--keep]
#
# Environment: SETU_WINE (as in tools/shell-smoke.sh), SETU_RES (1280x800).
set -euo pipefail

# A private host name, so nothing in the prefix or the screenshots shows
# this machine's name (see tools/shell-smoke.sh).
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

# a throwaway prefix, made fresh every run
export WINEPREFIX="$REPO/build/startmenu-prefix"
export WINEDEBUG="${WINEDEBUG:--all}"
# the start menu shows the user name: a neutral one, not this machine's
export USER=setu LOGNAME=setu
export WINEDLLOVERRIDES="mscoree,mshtml="
RES="${SETU_RES:-1280x800}"
OUT="$REPO/build/shell/startmenu"
mkdir -p "$OUT"
rm -f "$OUT"/*.png "$OUT"/*.txt "$OUT"/*.log

make -s -C "$REPO/shell"
SHELL_EXE="$REPO/build/shell/setu-shell.exe"
PROBE_EXE="$REPO/build/shell/setu-probe.exe"
APP_EXE="$REPO/build/shell/setu-testapp.exe"

for n in $(seq 90 140); do
  [ -e "/tmp/.X11-unix/X$n" ] || [ -e "/tmp/.X$n-lock" ] || { DISP=$n; break; }
done
Xvfb ":$DISP" -screen 0 "${RES}x24" -nolisten tcp >/dev/null 2>&1 &
XVFB_PID=$!
export DISPLAY=":$DISP"
for _ in $(seq 50); do [ -e "/tmp/.X11-unix/X$DISP" ] && break; sleep 0.1; done

cleanup() {
  if [ "$KEEP" = 1 ]; then
    echo "kept running on DISPLAY=$DISPLAY (Xvfb pid $XVFB_PID), prefix $WINEPREFIX"
    return
  fi
  "$WINESERVER" -k >/dev/null 2>&1 || true
  kill "$XVFB_PID" >/dev/null 2>&1 || true
  [ "${PASSED:-0}" = 1 ] && rm -rf "$WINEPREFIX"
  return 0
}
trap cleanup EXIT

STATE_WIN='C:\setu-shell-state.txt'
STATE="$WINEPREFIX/drive_c/setu-shell-state.txt"
fail() { echo "FAIL: $*" >&2; [ -f "$STATE" ] && sed 's/^/  state: /' "$STATE" >&2; exit 1; }
pass() { echo "ok: $*"; }
shot() {
  # import reads the root window's pixels as shown; xwd -root drops a
  # window that lies wholly inside another when windows have their own
  # colormaps (Wine's do), which made such popups look black
  import -display "$DISPLAY" -window root "$OUT/$1.png"
}
probe() { "$WINE" "$PROBE_EXE" "$@" >>"$OUT/probe.log" 2>&1; }
# wait_state REGEX [SECONDS]: wait until the shell's state file matches
wait_state() {
  local re="$1" t="${2:-20}" i
  for i in $(seq $((t * 10))); do
    { tr -d '\r' < "$STATE"; } 2>/dev/null | grep -Eq -- "$re" && return 0
    sleep 0.1
  done
  return 1
}
# wait_nostate REGEX [SECONDS]: wait until it no longer matches
wait_nostate() {
  local re="$1" t="${2:-20}" i
  for i in $(seq $((t * 10))); do
    { tr -d '\r' < "$STATE"; } 2>/dev/null | grep -Eq -- "$re" || return 0
    sleep 0.1
  done
  return 1
}
state() { tr -d '\r' < "$STATE" | grep -E -- "$1" | head -1; }
# row_xy LABEL: screen centre of the menu row with that label, as "X Y"
row_xy() { state "^row=[0-9]+\|(app|group)\|$1\|" | awk -F'|' '{ split($5, c, ","); print c[1], c[2] }'; }
# wait_window CLASS [SECONDS]: wait for a visible top-level window of CLASS
wait_window() {
  local i
  for i in $(seq $(( ${2:-10} * 4 ))); do
    "$WINE" "$PROBE_EXE" windows | tr -d '\r' | awk -F'|' -v c="$1" '$2 == c { f = 1 } END { exit !f }' && return 0
    sleep 0.25
  done
  return 1
}
# close_window: close the test app's windows
close_window() { "$WINE" cmd /c "taskkill /f /im setu-testapp.exe" >/dev/null 2>&1 || true; }

# --- prefix ------------------------------------------------------------------
rm -rf "$WINEPREFIX"
"$WINE" wineboot -i >/dev/null 2>&1
"$WINE" reg add 'HKCU\Software\Wine\Explorer' /v Desktop /d setu /f >/dev/null 2>&1
"$WINE" reg add 'HKCU\Software\Wine\Explorer\Desktops' /v setu /d "$RES" /f >/dev/null 2>&1
"$WINESERVER" -w

# the test app, "installed" in three places
for d in alpha beta gamma delta; do
  mkdir -p "$WINEPREFIX/drive_c/setu-smoke/$d"
  cp "$APP_EXE" "$WINEPREFIX/drive_c/setu-smoke/$d/setu-testapp.exe"
done
APP='C:\setu-smoke'
USER_PROGRAMS="$("$WINE" cmd /c 'echo %APPDATA%' | tr -d '\r')\\Microsoft\\Windows\\Start Menu\\Programs"
COMMON_PROGRAMS='C:\ProgramData\Microsoft\Windows\Start Menu\Programs'
mklnk() { probe mklnk "$@" || fail "mklnk $1"; }

# Alpha Tool: in both folders (listed once, from the per-user folder), no icon
mklnk "$USER_PROGRAMS\\Alpha Tool.lnk" "$APP\\alpha\\setu-testapp.exe" '--title Alpha-Tool'
mklnk "$COMMON_PROGRAMS\\Alpha Tool.lnk" "$APP\\alpha\\setu-testapp.exe" '--title Alpha-Tool-Common'
# Beta Editor: icon location set to notepad's icon
mklnk "$COMMON_PROGRAMS\\Beta Editor.lnk" "$APP\\beta\\setu-testapp.exe" '--title Beta-Editor' 'C:\windows\system32\notepad.exe,0'
# Gamma Suite: a folder with a nested folder; and its Uninstall entry
mklnk "$COMMON_PROGRAMS\\Gamma Suite\\Gamma Writer.lnk" "$APP\\gamma\\setu-testapp.exe" '--title Gamma-Writer'
mklnk "$COMMON_PROGRAMS\\Gamma Suite\\Uninstall Gamma.lnk" "$APP\\gamma\\setu-testapp.exe" '--title Gamma-Uninstaller'
mklnk "$COMMON_PROGRAMS\\Gamma Suite\\Extras\\Gamma Help.lnk" "$APP\\gamma\\setu-testapp.exe" '--title Gamma-Help'
# a folder holding a single app is not a group
mklnk "$USER_PROGRAMS\\Solo Folder\\Solo App.lnk" "$APP\\alpha\\setu-testapp.exe" '--title Solo-App'
U='HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall'
"$WINE" reg add "$U\\GammaSuite" /v DisplayName /d 'Gamma Suite 2.0' /f >/dev/null
"$WINE" reg add "$U\\GammaSuite" /v UninstallString /d "$APP\\gamma\\setu-testapp.exe --title Gamma-Uninstaller" /f >/dev/null
"$WINE" reg add "$U\\GammaSuite" /v InstallLocation /d "$APP\\gamma" /f >/dev/null
# Delta Service: installed, no shortcut -> "Other apps"
"$WINE" reg add "$U\\Delta" /v DisplayName /d 'Delta Service' /f >/dev/null
"$WINE" reg add "$U\\Delta" /v DisplayIcon /d "$APP\\delta\\setu-testapp.exe,0" /f >/dev/null
"$WINE" reg add "$U\\Delta" /v UninstallString /d "$APP\\delta\\setu-testapp.exe --title Delta-Uninstaller" /f >/dev/null
# a system component: never listed
"$WINE" reg add "$U\\Hidden" /v DisplayName /d 'Hidden Runtime' /f >/dev/null
"$WINE" reg add "$U\\Hidden" /v SystemComponent /t REG_DWORD /d 1 /f >/dev/null
"$WINESERVER" -w

start_shell() {
  rm -f "$STATE"
  "$WINE" "$SHELL_EXE" --state-file "$STATE_WIN" >>"$OUT/shell.log" 2>&1 &
  wait_state '^launcher=closed' 60 || fail "shell did not start"
  wait_state '^watches=[2-9]' 10 || fail "the shell is not watching the Programs folders"
}

# --- 1. discovery ------------------------------------------------------------
start_shell
pass "shell up: $(state '^watches=') $(state '^hotkey=') $(state '^winkey_hook=') $(state '^shnotify=')"
wait_state '^app=shortcut\|Alpha Tool\|C:\\setu-smoke\\alpha\\setu-testapp.exe\|\|glyph\|-\|-\|user$' ||
  fail "Alpha Tool (per-user, no icon) not listed as expected"
[ "$(tr -d '\r' < "$STATE" | grep -c '^app=shortcut|Alpha Tool|')" = 1 ] || fail "Alpha Tool listed twice"
wait_state '^app=shortcut\|Beta Editor\|.*\|\|icon\|-\|-\|common$' || fail "Beta Editor (icon location) not listed with an icon"
wait_state '^group=Gamma Suite\|collapsed\|3$' || fail "no Gamma Suite group of 3 (nested folder folded in)"
wait_state '^app=shortcut\|Gamma Help\|.*\|Gamma Suite\|.*\|uninstall\|common$' || fail "Gamma Help not in the group with its uninstaller"
wait_state '^app=shortcut\|Solo App\|.*\|Solo Folder\|' || fail "Solo App missing"
grep -q '^group=Solo Folder' "$STATE" && fail "a folder with one app became a group"
wait_state '^app=other\|Delta Service\|C:\\setu-smoke\\delta\\setu-testapp.exe\|\|' || fail "Delta Service not under Other apps"
grep -q 'Hidden Runtime' "$STATE" && fail "a SystemComponent entry is listed"
grep -q '^app=other|Gamma Suite' "$STATE" && fail "Gamma Suite's Uninstall entry was not matched to its shortcuts"
pass "discovery: $(grep -c '^app=' "$STATE") apps, $(grep -c '^group=' "$STATE") group(s)"

# --- 2. Windows key, type to search, Enter starts the best match -------------
probe key lwin
wait_state '^launcher=open' 10 || fail "the Windows key did not open the menu"
wait_state '^view=pinned$' 1 || fail "the menu did not open on the pins"
pass "Windows key opens the menu"
sleep 0.5
shot startmenu-01-pinned
probe type beta
wait_state '^search=beta$' 10 || fail "typing did not reach the search box"
wait_state '^row=[0-9]+\|app\|Beta Editor\|0\|.*\|selected$' 5 || fail "Beta Editor is not the selected best match"
pass "search: $(state '^row=.*\|selected$')"
sleep 0.3
shot startmenu-02-search
probe key enter
wait_state '^task=.*\|Beta-Editor\|' 20 || fail "Enter did not start Beta Editor"
wait_state '^launcher=closed' 5 || fail "the menu did not close after starting an app"
pass "Enter starts the best match: $(state '^task=.*Beta-Editor')"
close_window

# --- 3. Ctrl+Esc, arrow keys, Esc ----------------------------------------------
probe key ctrl+esc
wait_state '^launcher=open' 10 || fail "Ctrl+Esc did not open the menu"
probe key down
wait_state '^pin=0\|Files\|.*\|selected$' 5 || fail "Down did not select the first pin"
probe key right
wait_state '^pin=1\|Notepad\|.*\|selected$' 5 || fail "Right did not move to the second pin"
probe key up
wait_state '^header_button=All apps\|[0-9,]+\|selected$' 5 || fail "Up from the pins did not select the All apps button"
probe key enter
wait_state '^view=all$' 5 || fail "Enter on All apps did not show the list"
wait_state '^row=[0-9]+\|app\|Alpha Tool\|0\|.*\|visible\|selected$' 5 || fail "the list did not open on the first app"
wait_state '^launcher=open' 1 || fail "the menu closed on Enter"
sleep 0.3
shot startmenu-01-all-apps
probe key down
wait_state '^row=[0-9]+\|app\|Beta Editor\|0\|.*\|selected$' 5 || fail "Down did not move to the next app"
pass "arrow keys: $(state '^row=.*\|selected$')"
probe key esc
wait_state '^launcher=closed' 5 || fail "Esc did not close the menu"
pass "Ctrl+Esc opens, Esc closes"

# --- 4. clicking a group expands it ----------------------------------------------
"$WINE" "$SHELL_EXE" --open-launcher
wait_state '^launcher=open' 10 || fail "--open-launcher"
wait_state '^view=pinned$' 5 || fail "the menu did not reopen on the pins"
read -r HX HY < <(state '^header_button=' | awk -F'|' '{ split($2, c, ","); print c[1], c[2] }')
[ -n "$HY" ] || fail "no All apps button"
probe click "$HX" "$HY"
wait_state '^view=all$' 5 || fail "clicking All apps did not show the list"
wait_state '^header_button=Back\|' 1 || fail "the list has no Back button"
pass "All apps button shows the list"
read -r GX GY < <(row_xy 'Gamma Suite')
[ -n "$GY" ] || fail "no Gamma Suite row"
probe click "$GX" "$GY"
wait_state '^group=Gamma Suite\|expanded\|3$' 5 || fail "clicking the group did not expand it"
wait_state '^row=[0-9]+\|app\|Gamma Writer\|1\|[0-9,]+\|visible' 5 || fail "group members not shown (and scrolled into view) under the group"
pass "group expands: $(state '^row=.*Gamma Writer')"
sleep 0.3
shot startmenu-03-group

# --- 5. live refresh ------------------------------------------------------------
mklnk "$USER_PROGRAMS\\Epsilon Viewer.lnk" "$APP\\alpha\\setu-testapp.exe" '--title Epsilon'
wait_state '^app=shortcut\|Epsilon Viewer\|' 10 || fail "a new shortcut did not show up while the menu was open"
wait_state '^row=[0-9]+\|app\|Epsilon Viewer\|' 5 || fail "the new shortcut has no row"
wait_state '^launcher=open' 1 || fail "the menu closed during the refresh"
pass "new shortcut appears: $(state '^reloads=')"
rm -f "$WINEPREFIX/drive_c/users/"*/AppData/Roaming/Microsoft/Windows/Start\ Menu/Programs/Epsilon\ Viewer.lnk
wait_nostate '^app=shortcut\|Epsilon Viewer\|' 10 || fail "a deleted shortcut is still listed"
pass "deleted shortcut disappears"
wait_state '^group=Gamma Suite\|expanded' 1 || fail "the refresh collapsed the group"

# --- 6. right-click > Pin ----------------------------------------------------------
read -r AX AY < <(row_xy 'Alpha Tool')
[ -n "$AY" ] || fail "no Alpha Tool row"
probe rclick "$AX" "$AY"
wait_window '#32768' 10 || fail "right-click did not open the context menu"
sleep 1.5
shot startmenu-04-context
probe key p
wait_state '^pin=[0-9]+\|Alpha Tool\|' 5 || fail "Pin did not add Alpha Tool to the pins"
wait_state '^app=shortcut\|Alpha Tool\|.*\|pinned\|' 5 || fail "Alpha Tool not marked pinned"
"$WINE" reg query 'HKCU\Software\Setu\Shell' /v Pinned 2>/dev/null | tr -d '\r' | grep -q 'Alpha Tool' ||
  fail "the pin was not saved in HKCU\\Software\\Setu\\Shell"
pass "pinned: $(state '^pin=[0-9]+\|Alpha Tool')"

# --- 7. right-click > Uninstall on the group ------------------------------------------
read -r GX GY < <(row_xy 'Gamma Suite')
probe rclick "$GX" "$GY"
wait_window '#32768' 10 || fail "right-click on the group did not open the context menu"
probe key u
wait_state '^task=.*\|Gamma-Uninstaller\|' 20 || fail "Uninstall did not run the UninstallString"
pass "uninstall runs: $(state '^task=.*Gamma-Uninstaller')"
close_window

# --- 8. no match: the text runs as a command ------------------------------------------
"$WINE" "$SHELL_EXE" --search 'regedit.exe'
wait_state '^row=0\|hint\|' 5 || fail "no 'run as a command' hint for text that matches nothing"
probe key enter
wait_state '^task=.*Registry Editor' 30 || fail "Enter did not run the text as a command"
pass "fallback command: $(state '^status=')"
"$WINE" cmd /c "taskkill /f /im regedit.exe" >/dev/null 2>&1 || true

# --- 9. the pin survives a restart --------------------------------------------------------
"$WINE" "$SHELL_EXE" --quit || true
for _ in $(seq 50); do "$WINE" "$PROBE_EXE" windows all | tr -d '\r' | grep -q '|SetuShellTaskbar|' || break; sleep 0.2; done
start_shell
wait_state '^app=shortcut\|Alpha Tool\|.*\|pinned\|' 10 || fail "the pin did not survive a restart"
"$WINE" "$SHELL_EXE" --open-launcher
wait_state '^pin=[0-9]+\|Alpha Tool\|' 5 || fail "no Alpha Tool pin after the restart"
pass "pin kept after restart: $(tr -d '\r' < "$STATE" | grep '^pin=' | cut -d'|' -f2 | paste -sd, -)"
sleep 0.5
shot startmenu-05-pinned

PASSED=1
echo "screenshots: $OUT"
if [ "$UPDATE_DOCS" = 1 ]; then
  for f in "$OUT"/*.png; do
    convert "$f" -strip -define png:compression-level=9 "$REPO/shell/docs/$(basename "$f")"
  done
  echo "copied to shell/docs/"
fi
echo "PASS"
