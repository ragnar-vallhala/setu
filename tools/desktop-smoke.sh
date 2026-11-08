#!/usr/bin/env bash
# SPDX-License-Identifier: MIT OR Apache-2.0
#
# Headless smoke test for the Setu desktop (shell/src/setu-desktop.c). It
# builds the shell, starts setu-shell.exe (which starts setu-desktop.exe) on
# the Setu personality inside Xvfb (Wine virtual desktop "setu") with a
# throwaway prefix and home, and checks:
#   1. every item of the Desktop and Public Desktop folders is shown: a text
#      file (known extension hidden), a folder, a shortcut (no ".lnk", its
#      target's icon), a Public Desktop file; hidden items and desktop.ini
#      are not; HideFileExt=0 shows extensions;
#   2. the layout: columns from the top left, top to bottom, clear of the
#      taskbar's work area; icons and labels are drawn (pixel checks on a
#      screenshot); the desktop stays below other windows;
#   3. live refresh: a file created, renamed and deleted by another process
#      appears / changes / disappears within about a second;
#   4. a click selects (accent highlight), arrows move the selection;
#   5. a double-click on a folder opens it in setu-files; Enter opens too;
#   6. the item menu (Open, Rename, Delete, Properties): Rename in place
#      keeps the hidden extension; Delete goes to the Recycle Bin;
#   7. the desktop menu: New > Folder (named in place), New > Text Document,
#      Open Desktop folder (setu-files);
#   8. dragging an icon moves it to another cell, remembered after a restart.
# Screenshots go to build/shell/desktop-smoke/desktop-*.png; with
# --update-docs they are also copied to shell/docs/.
#
# Usage: tools/desktop-smoke.sh [--update-docs] [--keep]
#   --keep  leave Xvfb and the programs running (DISPLAY is printed)
#
# Environment:
#   SETU_WINE      wine binary (default: build/personality/install/bin/wine,
#                  falling back to the main checkout's build)
#   SETU_PREFIX    Wine prefix (default: build/desktop-prefix; recreated)
#   SETU_RES       screen size (default: 1280x800)
#
# Like tools/files-smoke.sh it runs with the host name "setu" and a HOME in
# build/desktop-home, so the Recycle Bin never touches the real home.
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
    -h|--help) sed -n '3,37p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
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
echo "personality: $WINE"

export WINEPREFIX="${SETU_PREFIX:-$REPO/build/desktop-prefix}"
export WINEDEBUG="${WINEDEBUG:--all}"
# winemenubuilder would put an XDG Notepad.desktop next to Notepad.lnk
export WINEDLLOVERRIDES="mscoree,mshtml=;winemenubuilder.exe=d"
export DBUS_SYSTEM_BUS_ADDRESS=unix:path=/nonexistent
make -s -C "$REPO/shell" -j3   # before HOME moves: the Makefile finds the toolchain under it
export HOME="$REPO/build/desktop-home" USER=setu LOGNAME=setu
export XDG_DATA_HOME="$HOME/.local/share" XDG_CONFIG_HOME="$HOME/.config" XDG_CACHE_HOME="$HOME/.cache"
RES="${SETU_RES:-1280x800}"
OUT="$REPO/build/shell/desktop-smoke"
mkdir -p "$OUT"
rm -f "$OUT"/*.png "$OUT"/*.txt "$OUT"/*.log

SHELL_EXE="$REPO/build/shell/setu-shell.exe"
DESK_EXE="$REPO/build/shell/setu-desktop.exe"
PROBE_EXE="$REPO/build/shell/setu-probe.exe"
FPROBE="$REPO/build/shell/setu-files-probe.exe"

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

STATE_WIN='C:\desktop-state.txt'
STATE="$WINEPREFIX/drive_c/desktop-state.txt"
fail() { echo "FAIL: $*" >&2; [ -f "$STATE" ] && sed 's/^/  state: /' "$STATE" >&2; exit 1; }
pass() { echo "ok: $*"; }
shot() { import -display "$DISPLAY" -window root "$OUT/$1.png"; }
st() { tr -d '\r' 2>/dev/null < "$STATE"; }
# wait_state REGEX [SECONDS]
wait_state() {
  local re="$1" t="${2:-15}" i
  for i in $(seq $((t * 10))); do
    st | grep -Eq -- "$re" && return 0
    sleep 0.1
  done
  return 1
}
gone_state() {
  local re="$1" t="${2:-15}" i
  for i in $(seq $((t * 10))); do
    st | grep -Eq -- "$re" || return 0
    sleep 0.1
  done
  return 1
}
desk() { "$WINE" "$DESK_EXE" "$@" >/dev/null 2>&1; }
probe() { "$WINE" "$FPROBE" "$@" | tr -d '\r'; }
has_item() { st | grep -q "^item=$1|"; }
# field N of item LABEL (1 label, 2 file, 3 kind, 4 source, 5 icon x,y, 6 label x,y, 7 col,row, 8 icon, 9 sel)
item_field() { st | awk -F'|' -v n="$1" -v f="$2" '$1 == "item=" n { print $f; exit }'; }
icon_xy() { item_field "$1" 5 | tr ',' ' '; }
now_ms() { date +%s%3N; }
# taskbar_shown SHOT: the strip below the work area is the taskbar, not the desktop
taskbar_shown() {
  python3 - "$OUT/$1.png" "$WB" <<'PYEOF'
import sys
from PIL import Image
img = Image.open(sys.argv[1]).convert("RGB")
w, h = img.size
y = (int(sys.argv[2]) + h) // 2
bg = img.getpixel((w // 2, h // 3))
same = sum(sum(abs(a - b) for a, b in zip(img.getpixel((x, y)), bg)) < 30 for x in range(0, w, 8))
sys.exit(0 if same < (w // 8) // 4 else 1)
PYEOF
}

# --- a fresh prefix and home -------------------------------------------------
rm -rf "$WINEPREFIX" "$HOME"
mkdir -p "$HOME" "$XDG_DATA_HOME/Trash/files" "$XDG_DATA_HOME/Trash/info"
"$WINE" wineboot -i >/dev/null 2>&1
"$WINE" reg add 'HKCU\Software\Wine\Explorer' /v Desktop /d setu /f >/dev/null 2>&1
"$WINE" reg add 'HKCU\Software\Wine\Explorer\Desktops' /v setu /d "$RES" /f >/dev/null 2>&1
"$WINESERVER" -w
find "$WINEPREFIX/dosdevices" -maxdepth 1 -type l -name '[a-y]:*' ! -name 'c:' -delete

D="$WINEPREFIX/drive_c/users/setu/Desktop"
P="$WINEPREFIX/drive_c/users/Public/Desktop"
mkdir -p "$D" "$P"
[ -d "$D" ] || fail "no Desktop folder in the prefix"
mkdir -p "$D/Projects"
printf 'shopping list\r\n' > "$D/notes.txt"
printf 'secret\r\n' > "$D/secret.txt"
printf '[.ShellClassInfo]\r\n' > "$D/desktop.ini"
printf 'shared\r\n' > "$P/Team plan.txt"
"$WINE" cmd /c 'attrib +h C:\users\setu\Desktop\secret.txt' >/dev/null 2>&1 || true
"$WINE" "$PROBE_EXE" mklnk 'C:\users\setu\Desktop\Notepad.lnk' 'C:\windows\system32\notepad.exe' >/dev/null ||
  fail "could not make Notepad.lnk"
"$WINESERVER" -w

# --- 1. the items ---------------------------------------------------------------
"$WINE" "$SHELL_EXE" --state-file 'C:\shell-state.txt' >"$OUT/shell.log" 2>&1 &
probe find 'Setu desktop' 60 >/dev/null || fail "setu-shell did not start setu-desktop.exe"
desk --state-file "$STATE_WIN"
wait_state '^end=1' 30 || fail "no desktop state"
wait_state '^watching=2' 10 || fail "the desktop does not watch both folders"
for n in Projects notes Notepad 'Team plan'; do has_item "$n" || fail "item $n missing"; done
st | grep -q '^item=Projects|Projects|dir|user|' || fail "Projects is not a folder from Desktop"
st | grep -q '^item=notes|notes.txt|file|user|' || fail "notes.txt does not show as 'notes' (HideFileExt default)"
st | grep -q '^item=Notepad|Notepad.lnk|link|user|' || fail "the shortcut does not show as 'Notepad'"
st | grep -q '^item=Team plan|Team plan.txt|file|common|' || fail "the Public Desktop file is missing"
st | grep -q '^item=[^|]*|secret.txt|' && fail "a hidden file is shown"
st | grep -qi '^item=[^|]*|desktop.ini|' && fail "desktop.ini is shown"
st | grep '^item=' | grep -vq '|icon|' && fail "an item has no shell icon: $(st | grep '^item=' | grep -v '|icon|')"
pass "items: $(st | sed -n 's/^item=\([^|]*\)|\([^|]*\)|\([^|]*\)|\([^|]*\)|.*/\1 (\3, \4)/p' | tr '\n' ',' )"

"$WINE" reg add 'HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced' /v HideFileExt /t REG_DWORD /d 0 /f >/dev/null 2>&1
desk --refresh
wait_state '^item=notes.txt\|notes.txt\|' 10 || fail "HideFileExt=0 does not show the extension"
"$WINE" reg add 'HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced' /v HideFileExt /t REG_DWORD /d 1 /f >/dev/null 2>&1
desk --refresh
wait_state '^item=notes\|notes.txt\|' 10 || fail "HideFileExt=1 does not hide it again"
pass "HideFileExt honoured"

# --- 2. layout and drawing ---------------------------------------------------
read -r WL WT WR WB < <(st | sed -n 's/^workarea=//p' | tr ',' ' ')
[ "$WB" -lt "${RES#*x}" ] || fail "the work area does not leave room for the taskbar: $WL,$WT,$WR,$WB"
CELL_H=$(st | sed -n 's/^cell=[0-9]*,//p')
prev_y=0 col_x=""
while IFS='|' read -r label _ _ _ ixy lxy cr _; do
  ix=${ixy%,*} iy=${ixy#*,} ly=${lxy#*,}
  [ -z "$col_x" ] && col_x=$ix
  [ "${cr%,*}" = 0 ] || fail "$label is not in the first column ($cr)"
  [ "$ix" = "$col_x" ] || fail "$label is not under the others (x $ix, not $col_x)"
  [ "$iy" -gt "$prev_y" ] || fail "$label is not below the previous item"
  [ $((ly + CELL_H / 2)) -lt "$WB" ] || fail "$label's label reaches the taskbar ($ly vs $WB)"
  prev_y=$iy
done < <(st | sed -n 's/^item=//p')
st | grep -q '^bottom=1' || fail "the desktop window is not at the bottom of the Z order"
sleep 1
shot desktop-01-icons
st | awk -F'|' '/^item=/ { sub("item=", "", $1); print $1 "|" $5 "|" $6 }' > "$OUT/items.txt"
python3 - "$OUT/desktop-01-icons.png" "$OUT/items.txt" > "$OUT/pixels.txt" <<'EOF' || fail "pixel checks: $(cat "$OUT/pixels.txt")"
import sys
from PIL import Image
img = Image.open(sys.argv[1]).convert("RGB")
w, h = img.size
bg = img.getpixel((w // 2, h // 3))           # empty desktop
def far(a, b): return sum(abs(x - y) for x, y in zip(a, b)) > 60
bad = []
for line in open(sys.argv[2]):
    label, ixy, lxy = line.rstrip("\n").split("|")
    ix, iy = map(int, ixy.split(",")); lx, ly = map(int, lxy.split(","))
    icon = [img.getpixel((x, y)) for x in range(ix - 12, ix + 13) for y in range(iy - 12, iy + 13)]
    n_icon = sum(far(p, bg) for p in icon)
    text = [img.getpixel((x, y)) for x in range(lx - 30, lx + 31) for y in range(ly - 6, ly + 7)]
    white = sum(min(p) > 200 for p in text)
    dark = sum(max(p) < 80 for p in text)
    print(f"{label}: icon {n_icon}/625 off-background, label {white} light + {dark} dark px")
    if n_icon < 150: bad.append(f"{label}: no icon drawn")
    if white < 15: bad.append(f"{label}: no label text")
    if dark < 5: bad.append(f"{label}: no label shadow")
print("background", bg)
if bad:
    print("; ".join(bad)); sys.exit(1)
EOF
sed 's/^/  /' "$OUT/pixels.txt"
taskbar_shown desktop-01-icons || fail "the taskbar is not visible over the desktop"
pass "layout: one column from the top left inside the work area $WL,$WT,$WR,$WB; icons and labels drawn"

# --- 3. live refresh -----------------------------------------------------------
t0=$(now_ms)
printf 'saved from an app\r\n' > "$D/test.txt"
wait_state '^item=test\|test.txt\|file\|user\|' 10 || fail "a new file did not appear"
t1=$(now_ms)
mv "$D/test.txt" "$D/renamed outside.txt"
wait_state '^item=renamed outside\|' 10 || fail "a file renamed outside did not change"
gone_state '^item=test\|' 5 || fail "the old name is still shown"
t2=$(now_ms)
mkdir "$P/Shared folder"
wait_state '^item=Shared folder\|Shared folder\|dir\|common\|' 10 || fail "a new Public Desktop folder did not appear"
rm "$D/renamed outside.txt"
gone_state '^item=renamed outside\|' 10 || fail "a deleted file is still shown"
t3=$(now_ms)
[ $((t1 - t0)) -lt 2000 ] || fail "the new file took $((t1 - t0)) ms to appear"
pass "live refresh: new file in $((t1 - t0)) ms, rename in $((t2 - t1)) ms; Public Desktop folder + delete in $((t3 - t2)) ms"

# --- 4. selection --------------------------------------------------------------
read -r X Y < <(icon_xy notes)
probe click "$X" "$Y" >/dev/null
wait_state '^selected=notes$' 5 || fail "a click did not select notes"
wait_state '^focused=1' 5 || fail "the desktop did not get the focus"
sleep 0.5
shot desktop-02-selected
ACCENT=$(st | sed -n 's/^accent=//p')
python3 - "$OUT/desktop-02-selected.png" "$X" "$Y" "$ACCENT" "$OUT/desktop-01-icons.png" <<'EOF' > "$OUT/select.txt" || fail "no accent highlight: $(cat "$OUT/select.txt")"
import sys
from PIL import Image
a = Image.open(sys.argv[1]).convert("RGB"); b = Image.open(sys.argv[5]).convert("RGB")
x, y = int(sys.argv[2]), int(sys.argv[3])
acc = tuple(int(sys.argv[4][i:i + 2], 16) for i in (1, 3, 5))
# the highlight sits around the icon: compare a strip left of it with the unselected shot
pts = [(xx, yy) for xx in range(x - 38, x - 28) for yy in range(y - 10, y + 10)]
def toward(p, q):   # p moved toward the accent compared with q
    return sum(abs(c - t) for c, t in zip(p, acc)) + 15 < sum(abs(c - t) for c, t in zip(q, acc))
n = sum(toward(a.getpixel(p), b.getpixel(p)) for p in pts)
print(f"{n}/{len(pts)} px tinted toward {sys.argv[4]}")
sys.exit(0 if n > len(pts) // 2 else 1)
EOF
taskbar_shown desktop-02-selected || fail "a click on the desktop put it over the taskbar"
pass "click selects notes: $(cat "$OUT/select.txt")"
probe key Down >/dev/null
wait_state '^selected=Team plan$' 5 || fail "Down did not select the next item: $(st | grep ^selected=)"
probe key Up >/dev/null
wait_state '^selected=notes$' 5 || fail "Up did not go back"
pass "arrow keys move the selection"

# --- 5. opening ----------------------------------------------------------------
read -r X Y < <(icon_xy Projects)
probe dblclick "$X" "$Y" >/dev/null
probe find 'Projects - Files' 30 >/dev/null || fail "double-clicking Projects did not open it in setu-files: $(st | grep -E '^(last_open|status)=')"
st | grep -q '^last_open=files|C:\\users\\setu\\Desktop\\Projects$' || fail "last_open: $(st | grep ^last_open=)"
sleep 1
"$WINE" "$PROBE_EXE" windows > "$OUT/windows.txt"
desk --refresh
st | grep -q '^bottom=1' || fail "the desktop is not below setu-files"
[ "$(grep -v '|hidden' "$OUT/windows.txt" | tail -n 1 | cut -d'|' -f2)" = SetuDesktop ] ||
  fail "the desktop window is not the bottom visible window: $(cat "$OUT/windows.txt")"
shot desktop-03-open-folder
taskbar_shown desktop-03-open-folder || fail "the taskbar is hidden with setu-files open"
probe close 'Projects - Files' >/dev/null
pass "double-click opens Projects in setu-files; the desktop stays at the bottom"
# Enter on the shortcut starts its target
read -r X Y < <(icon_xy Notepad)
probe click "$X" "$Y" >/dev/null
wait_state '^selected=Notepad$' 5 || fail "could not select Notepad"
probe key Return >/dev/null
probe find 'Notepad' 30 >/dev/null || fail "Enter on the Notepad shortcut did not start Notepad"
probe close ' - Notepad' >/dev/null || true
pass "Enter opens the shortcut's target"

# --- 6. the item menu ---------------------------------------------------------
read -r X Y < <(icon_xy notes)
probe rclick "$X" "$Y" >/dev/null
wait_state '^menu=&Open\|' 5 || fail "right-click on notes shows no menu"
st | grep -q '^menu=&Open|Rena&me|&Delete|P&roperties$' || fail "item menu: $(st | grep ^menu=)"
sleep 0.5
shot desktop-04-item-menu
probe key m >/dev/null
wait_state '^renaming=1' 5 || fail "Rename did not start renaming in place"
probe type 'groceries' >/dev/null
probe key Return >/dev/null
wait_state '^item=groceries\|groceries.txt\|' 10 || fail "rename in place: $(st | grep '^item=')"
[ -f "$D/groceries.txt" ] && [ ! -e "$D/notes.txt" ] || fail "the rename is not on disk"
pass "Rename in place: notes -> groceries (groceries.txt, extension kept)"
read -r X Y < <(icon_xy groceries)
probe click "$X" "$Y" >/dev/null
wait_state '^selected=groceries$' 5 || fail "could not select groceries"
probe key F2 >/dev/null
wait_state '^renaming=1' 5 || fail "F2 did not start renaming"
probe key Escape >/dev/null
wait_state '^renaming=0' 5 || fail "Escape did not cancel the rename"
[ -f "$D/groceries.txt" ] || fail "Escape renamed the file"
pass "F2 renames, Escape cancels"
BIN0=$(probe recycle || true)
case "$BIN0" in ''|*[!0-9]*) fail "cannot count the Recycle Bin: $BIN0" ;; esac
read -r X Y < <(icon_xy groceries)
probe rclick "$X" "$Y" >/dev/null
wait_state '^menu=.*Delete' 5 || fail "no item menu for Delete"
probe key d >/dev/null
if probe setfg 'Confirm' 5 >"$OUT/confirm.txt" 2>&1; then sleep 0.3; probe key Return >/dev/null; fi
for _ in $(seq 50); do [ ! -e "$D/groceries.txt" ] && break; sleep 0.1; done
[ ! -e "$D/groceries.txt" ] || fail "Delete did not remove groceries.txt"
gone_state '^item=groceries\|' 5 || fail "groceries is still shown"
BIN1=$(probe recycle || true)
[ "$BIN1" = $((BIN0 + 1)) ] || fail "groceries.txt is not in the Recycle Bin ($BIN0 -> $BIN1)"
pass "Delete moves groceries.txt to the Recycle Bin"

# --- 7. the desktop menu ------------------------------------------------------
EX=$((WR - 300)) EY=$((WB - 200))
probe rclick "$EX" "$EY" >/dev/null
wait_state '^menu=.*Ne&?w' 5 || fail "right-click on empty space shows no desktop menu"
st | grep -q '^menu=&Arrange by name|R&efresh|&Paste|Ne&w|&Open Desktop folder$' ||
  fail "desktop menu: $(st | grep ^menu=)"
sleep 0.5
shot desktop-05-desktop-menu
probe key w >/dev/null
sleep 0.3
probe key f >/dev/null
wait_state '^renaming=1' 5 || fail "New > Folder did not start renaming"
[ -d "$D/New folder" ] || fail "New > Folder made no folder"
st | grep -q '^item=New folder|New folder|dir|user|' || fail "the new folder is not shown"
probe type 'Made here' >/dev/null
probe key Return >/dev/null
wait_state '^item=Made here\|Made here\|dir\|' 10 || fail "naming the new folder: $(st | grep '^item=')"
[ -d "$D/Made here" ] || fail "Made here is not on disk"
# it went where the menu was opened
read -r MX MY < <(icon_xy 'Made here')
[ "$MX" -gt $((WR - 500)) ] || fail "the new folder is not near where the menu opened ($MX,$MY)"
pass "New > Folder named in place, at the click ($MX,$MY)"
probe rclick "$EX" $((EY - 150)) >/dev/null
wait_state '^menu=.*Ne&?w' 5 || fail "no desktop menu"
probe key w >/dev/null
sleep 0.3
probe key t >/dev/null
wait_state '^renaming=1' 5 || fail "New > Text Document did not start renaming"
probe key Return >/dev/null
wait_state '^item=New Text Document\|New Text Document.txt\|file\|' 10 || fail "no New Text Document"
[ -f "$D/New Text Document.txt" ] || fail "New Text Document.txt not on disk"
pass "New > Text Document"
probe rclick "$EX" $((EY + 60)) >/dev/null
wait_state '^menu=.*Open Desktop folder' 5 || fail "no desktop menu"
probe key o >/dev/null
probe find 'Desktop - Files' 30 >/dev/null || fail "Open Desktop folder did not open setu-files: $(st | grep -E '^(last_open|status)=')"
probe close 'Desktop - Files' >/dev/null
pass "Open Desktop folder opens setu-files"

# --- 8. dragging, remembered --------------------------------------------------
read -r X1 Y1 < <(icon_xy 'Team plan')
X2=$((X1 + 400)) Y2=$((Y1 + 100))
probe drag "$X1" "$Y1" "$X2" "$Y2" >/dev/null
sleep 0.5
cr=$(item_field 'Team plan' 7)
[ "${cr%,*}" -ge 3 ] || fail "dragging Team plan did not move it (cell $cr)"
"$WINE" reg query 'HKCU\Software\Setu\Desktop\Positions' > "$OUT/positions.txt" 2>&1 || true
grep -q 'c:Team plan.txt' "$OUT/positions.txt" || fail "the position is not saved: $(cat "$OUT/positions.txt")"
sleep 0.5
shot desktop-06-moved
desk --quit
gone_state '^end=1' 1 || true
probe find 'Setu desktop' 1 >/dev/null 2>&1 && sleep 1
rm -f "$STATE"
"$WINE" "$DESK_EXE" --state-file "$STATE_WIN" >"$OUT/desk2.log" 2>&1 &
wait_state '^end=1' 30 || fail "the desktop did not start again"
wait_state "^item=Team plan\|[^|]*\|[^|]*\|[^|]*\|[^|]*\|[^|]*\|$cr\|" 10 || fail "Team plan's cell was not remembered: $(st | grep '^item=Team plan')"
pass "drag moves Team plan to cell $cr, remembered after a restart"

echo "screenshots: $OUT"
if [ "$UPDATE_DOCS" = 1 ]; then
  mkdir -p "$REPO/shell/docs"
  for f in "$OUT"/desktop-0[1-5]*.png; do
    convert "$f" -strip -define png:compression-level=9 "$REPO/shell/docs/$(basename "$f")"
  done
  echo "copied to shell/docs/"
fi
echo "PASS"
