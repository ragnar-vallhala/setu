#!/usr/bin/env bash
# SPDX-License-Identifier: MIT OR Apache-2.0
#
# Headless smoke test for the Setu shell (shell/). It builds the shell, starts
# it on the Setu personality inside Xvfb (Wine virtual desktop "setu"), and
# checks that:
#   1. the taskbar is docked to the bottom of the screen,
#   2. the launcher opens and lists the built-ins and a Start Menu .lnk,
#   3. launching Notepad from the launcher adds a taskbar button,
#   4. the run box starts a program (regedit.exe),
#   5. taskbar buttons activate, minimize and restore windows,
#   5b. the desktop is repainted where a window was hidden or minimized
#      (pixels sampled where the launcher and Notepad were),
#   6. two-process checks (shell/tests/setu-xproc.c): another process sees
#      the work area the taskbar reserved, maximizes inside it and hides
#      minimized windows (ARW_HIDE); RegisterShellHookWindow notifications;
#      icons of another process's window are usable.
#   7. the taskbar buttons are centred, and TaskbarAlignment=1 in
#      HKCU\Software\Setu\Shell puts them on the left.
# Screenshots go to build/shell/smoke/*.png; with --update-docs they are
# also copied to shell/docs/.
#
# Usage: tools/shell-smoke.sh [--update-docs] [--keep]
#   --keep  leave Xvfb and the shell running for poking around (DISPLAY is
#           printed); stop with: WINEPREFIX=... wineserver -k
#
# Environment:
#   SETU_WINE      wine binary (default: build/personality/install/bin/wine,
#                  falling back to the main checkout's build)
#   SETU_PREFIX    Wine prefix (default: build/shell-prefix)
#   SETU_RES       screen size (default: 1280x800)
#
# The test runs with the host name "setu" (a private UTS namespace, entered
# through unprivileged user namespaces), so the computer name the personality
# reports (wineboot reads it from gethostname()) and the screenshots never
# show the build machine's name.
set -euo pipefail

SMOKE_HOSTNAME=setu
case "${SETU_SMOKE_NS:-}" in
  "")
    if unshare -Ur --uts true 2>/dev/null; then
      # 1: a user namespace where we are root, to set the host name; 2: a
      # nested one that maps us back to our own uid and gid.
      exec env SETU_SMOKE_NS=1 SETU_SMOKE_UID="$(id -u)" SETU_SMOKE_GID="$(id -g)" \
        unshare -Ur --uts "$0" "$@"
    fi
    echo "FAIL: cannot create a user + UTS namespace (unshare -Ur --uts), so the" \
      "computer name would be this host's ($(hostname)); see /proc/sys/kernel/unprivileged_userns_clone" >&2
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
    -h|--help) sed -n '3,22p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "unknown option: $a" >&2; exit 2 ;;
  esac
done

WINE="${SETU_WINE:-$REPO/build/personality/install/bin/wine}"
if [ ! -x "$WINE" ]; then
  # in a git worktree, fall back to the main checkout's personality build
  main="$(git -C "$REPO" rev-parse --path-format=absolute --git-common-dir 2>/dev/null || true)"
  main="${main%/.git}"
  WINE="$main/build/personality/install/bin/wine"
fi
[ -x "$WINE" ] || { echo "no personality build found; run tools/build-personality.sh" >&2; exit 1; }
WINESERVER="$(dirname "$WINE")/wineserver"

export WINEPREFIX="${SETU_PREFIX:-$REPO/build/shell-prefix}"
export WINEDEBUG="${WINEDEBUG:--all}"
# the start menu shows the user name: a neutral one, not this machine's
export USER=setu LOGNAME=setu
export WINEDLLOVERRIDES="mscoree,mshtml="
RES="${SETU_RES:-1280x800}"
OUT="$REPO/build/shell/smoke"
mkdir -p "$OUT"
rm -f "$OUT"/*.png "$OUT"/*.txt

make -s -C "$REPO/shell"
SHELL_EXE="$REPO/build/shell/setu-shell.exe"
PROBE_EXE="$REPO/build/shell/setu-probe.exe"
XPROC_EXE="$REPO/build/shell/setu-xproc.exe"

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
    echo "kept running on DISPLAY=$DISPLAY (Xvfb pid $XVFB_PID)"
    return
  fi
  "$WINESERVER" -k >/dev/null 2>&1 || true
  kill "$XVFB_PID" >/dev/null 2>&1 || true
}
trap cleanup EXIT

fail() { echo "FAIL: $*" >&2; [ -f "$STATE" ] && sed 's/^/  state: /' "$STATE" >&2; exit 1; }
pass() { echo "ok: $*"; }

shot() {
  # import reads the root window's pixels as shown; xwd -root drops a
  # window that lies wholly inside another when windows have their own
  # colormaps (Wine's do), which made such popups look black
  import -display "$DISPLAY" -window root "$OUT/$1.png"
}

# pixel X Y: the screen pixel at X,Y as "R,G,B"
pixel() {
  import -display "$DISPLAY" -window root "$OUT/pixel.png"
  convert "$OUT/pixel.png" -format "%[fx:int(255*p{$1,$2}.r)],%[fx:int(255*p{$1,$2}.g)],%[fx:int(255*p{$1,$2}.b)]" info:
  rm -f "$OUT/pixel.png"
}
# wait_pixel X Y RGB [SECONDS]: wait until the pixel at X,Y is RGB
wait_pixel() {
  local i
  for i in $(seq $(( ${4:-5} * 4 ))); do
    [ "$(pixel "$1" "$2")" = "$3" ] && return 0
    sleep 0.25
  done
  return 1
}

# win_rect TITLE [all]: "L T R B" of the top-level window with that title
win_rect() {
  "$WINE" "$PROBE_EXE" windows ${2:-} | tr -d '\r' | awk -F'|' -v t="$1" '$3 == t { gsub(",", " ", $4); print $4; exit }'
}

# wait_state REGEX [SECONDS]: wait until the shell's state file matches
wait_state() {
  local re="$1" t="${2:-20}" i
  for i in $(seq $((t * 10))); do
    grep -Eq -- "$re" "$STATE" 2>/dev/null && return 0
    sleep 0.1
  done
  return 1
}

# --- prefix: one virtual desktop called "setu" for every process ----------
if [ ! -f "$WINEPREFIX/system.reg" ]; then
  "$WINE" wineboot -i >/dev/null 2>&1
fi
"$WINE" reg add 'HKCU\Software\Wine\Explorer' /v Desktop /d setu /f >/dev/null 2>&1
"$WINE" reg add 'HKCU\Software\Wine\Explorer\Desktops' /v setu /d "$RES" /f >/dev/null 2>&1
"$WINESERVER" -w

# a Start Menu shortcut for the launcher to find
LNK='C:\ProgramData\Microsoft\Windows\Start Menu\Programs\Setu Smoke\Smoke Notepad.lnk'
"$WINE" "$PROBE_EXE" mklnk "$LNK" 'C:\windows\system32\notepad.exe' >/dev/null

STATE_WIN='C:\setu-shell-state.txt'
STATE="$WINEPREFIX/drive_c/setu-shell-state.txt"
rm -f "$STATE"

# --- 1. shell boots, taskbar docked ----------------------------------------
"$WINE" "$SHELL_EXE" --state-file "$STATE_WIN" >"$OUT/shell.log" 2>&1 &
wait_state '^launcher=closed' 60 || fail "shell did not start"
H="${RES#*x}"
grep -Eq "^taskbar_rect=0,[0-9]+,${RES%x*},$H" "$STATE" || fail "taskbar not docked at the bottom"
pass "taskbar docked: $(grep ^taskbar_rect "$STATE")"
grep ^appbar= "$STATE"; grep ^shellhook= "$STATE"; grep ^workarea "$STATE"
"$WINE" "$PROBE_EXE" workarea > "$OUT/workarea.txt" 2>&1 || true
"$WINE" "$PROBE_EXE" appbar-pos >> "$OUT/workarea.txt" 2>&1 || true
sed 's/^/  other process: /' "$OUT/workarea.txt"
sleep 1
shot 01-taskbar

# click X Y: a real mouse click (SendInput) at screen pixel X,Y
click() { "$WINE" "$PROBE_EXE" click "$1" "$2" >/dev/null; }
# task_xy TITLE-REGEX: centre of that window's taskbar button, as "X Y"
task_xy() { grep -E "^task=[^|]*\|[^|]*$1" "$STATE" | head -1 | tr -d '\r' | sed 's/.*|\([0-9]*\),\([0-9]*\)$/\1 \2/'; }
task_line() { grep -E "^task=[^|]*\|[^|]*$1" "$STATE" | head -1 | tr -d '\r'; }

# --- 2. clicking the start button opens the launcher ------------------------
read -r SX SY < <(grep ^start_button= "$STATE" | tr -d '\r' | sed 's/start_button=\([0-9]*\),\([0-9]*\)/\1 \2/')
click "$SX" "$SY"
wait_state '^launcher=open' 10 || fail "clicking the start button did not open the launcher"
grep -q '^app=builtin|Notepad|' "$STATE" || fail "Notepad missing from the launcher"
grep -q '^app=shortcut|Smoke Notepad|.*notepad.exe' "$STATE" || fail "Start Menu .lnk missing from the launcher"
pass "launcher open with $(grep -c '^app=' "$STATE") entries"
sleep 1
shot 02-launcher
# The desktop background, from a corner no window in this test covers.
DESK_RGB=$(pixel $(( ${RES%x*} - 20 )) 20)
read -r LL LT LR LB < <(win_rect 'Setu launcher' all)
[ -n "$LB" ] || fail "launcher window not found by setu-probe windows"

# --- 3. launch Notepad from the launcher: a taskbar button appears ----------
"$WINE" "$SHELL_EXE" --launch Notepad || fail "--launch Notepad failed"
wait_state '^task=.*Notepad' 30 || fail "no taskbar button for Notepad"
wait_state '^launcher=closed' 5 || fail "launcher did not close after launching"
pass "Notepad button: $(task_line Notepad)"
sleep 1
shot 03-notepad
# Where the launcher was and Notepad isn't, the desktop must show again.
read -r NL NT NR NB < <(win_rect 'Untitled - Notepad')
PX=$(( (LL + LR) / 2 )); PY=$(( (NB + LB) / 2 ))
[ "$PY" -gt "$NB" ] && [ "$PY" -lt "$LB" ] || fail "no launcher area outside Notepad to sample (launcher $LL,$LT,$LR,$LB, Notepad $NL,$NT,$NR,$NB)"
wait_pixel "$PX" "$PY" "$DESK_RGB" 5 || fail "desktop not repainted where the launcher was: pixel $PX,$PY is $(pixel "$PX" "$PY"), desktop is $DESK_RGB"
pass "desktop repainted under the closed launcher ($PX,$PY = $DESK_RGB)"

# --- 4. the run box starts programs ------------------------------------------
"$WINE" "$SHELL_EXE" --run 'regedit.exe' || fail "--run regedit.exe failed"
wait_state '^task=.*Registry Editor' 30 || fail "no taskbar button for regedit"
wait_state '^task=.*Registry Editor\|active' 10 || fail "regedit did not become active"
pass "run box: $(task_line 'Registry Editor')"
sleep 1
shot 04-two-apps
# A point Notepad covers and Registry Editor doesn't: the desktop once
# Notepad is minimized (checked in step 5).
read -r RL RT RR RB < <(win_rect 'Registry Editor')
if   [ $(( RL - NL )) -ge 2 ]; then QX=$(( (NL + RL) / 2 )); QY=$(( (NT + NB) / 2 ))
elif [ $(( NR - RR )) -ge 2 ]; then QX=$(( (RR + NR) / 2 )); QY=$(( (NT + NB) / 2 ))
elif [ $(( RT - NT )) -ge 2 ]; then QX=$(( (NL + NR) / 2 )); QY=$(( (NT + RT) / 2 ))
elif [ $(( NB - RB )) -ge 2 ]; then QX=$(( (NL + NR) / 2 )); QY=$(( (RB + NB) / 2 ))
else fail "Registry Editor ($RL,$RT,$RR,$RB) covers all of Notepad ($NL,$NT,$NR,$NB)"
fi

# --- 5. taskbar buttons: click activates, minimizes, restores ---------------
# Real clicks: Windows only lets the process that got the last input event
# move the foreground window, and the personality models that rule too.
click $(task_xy Notepad)
wait_state '^task=.*Notepad\|active\|' 10 || fail "clicking the Notepad button did not activate it"
pass "click activates: $(task_line Notepad)"
click $(task_xy Notepad)
wait_state '^task=.*Notepad\|inactive,minimized' 10 || fail "clicking the active Notepad button did not minimize it"
pass "click minimizes: $(task_line Notepad)"
sleep 1
shot 05-minimized
# Where only Notepad was, the desktop must show again, not black or stale.
wait_pixel "$QX" "$QY" "$DESK_RGB" 5 || fail "desktop not repainted where Notepad was minimized: pixel $QX,$QY is $(pixel "$QX" "$QY"), desktop is $DESK_RGB"
pass "desktop repainted under the minimized Notepad ($QX,$QY = $DESK_RGB)"
click $(task_xy Notepad)
wait_state '^task=.*Notepad\|active\|' 10 || fail "clicking the minimized Notepad button did not restore it"
pass "click restores: $(task_line Notepad)"

# --- 6. two-process checks ----------------------------------------------------
for check in workarea shellhook icons; do
  if "$WINE" "$XPROC_EXE" "$check" > "$OUT/xproc-$check.txt" 2>&1; then
    pass "setu-xproc $check: $(grep -c '^ok:' "$OUT/xproc-$check.txt") checks"
  else
    sed 's/^/  /' "$OUT/xproc-$check.txt" >&2
    fail "setu-xproc $check"
  fi
done
grep -q '^shellhook=1' "$STATE" || fail "the shell's RegisterShellHookWindow failed"
pass "shell: $(grep ^appbar= "$STATE" | tr -d '\r') $(grep ^shellhook= "$STATE" | tr -d '\r') $(grep ^workarea_set= "$STATE" | tr -d '\r')"

# --- 7. the buttons are centred; TaskbarAlignment=1 puts them on the left -----
start_x() { grep ^start_button= "$STATE" | tr -d '\r' | sed 's/start_button=\([0-9]*\),.*/\1/'; }
grep -q '^taskbar_alignment=center' "$STATE" || fail "the taskbar is not centred by default"
CX=$(start_x)
[ "$CX" -gt $(( ${RES%x*} / 4 )) ] || fail "centred taskbar, but the start button is at x=$CX"
"$WINE" reg add 'HKCU\Software\Setu\Shell' /v TaskbarAlignment /t REG_DWORD /d 1 /f >/dev/null 2>&1
"$WINE" "$SHELL_EXE" --reload
wait_state '^taskbar_alignment=left' 10 || fail "TaskbarAlignment=1 did not left-align the taskbar"
LX=$(start_x)
[ "$LX" -lt 100 ] && [ "$LX" -lt "$CX" ] || fail "left-aligned taskbar, but the start button is at x=$LX"
"$WINE" reg delete 'HKCU\Software\Setu\Shell' /v TaskbarAlignment /f >/dev/null 2>&1
"$WINE" "$SHELL_EXE" --reload
wait_state '^taskbar_alignment=center' 10 || fail "removing TaskbarAlignment did not centre the taskbar again"
pass "taskbar alignment: start button at x=$CX centred, x=$LX left"

PASSED=1
echo "screenshots: $OUT"
if [ "$UPDATE_DOCS" = 1 ]; then
  mkdir -p "$REPO/shell/docs"
  for f in "$OUT"/*.png; do
    convert "$f" -strip -define png:compression-level=9 "$REPO/shell/docs/$(basename "$f")"
  done
  echo "copied to shell/docs/"
fi
echo "PASS"
