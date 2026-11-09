#!/usr/bin/env bash
# SPDX-License-Identifier: MIT OR Apache-2.0
#
# Headless smoke test for the Setu file manager (shell/src/setu-files.c). It
# builds it, starts it on the Setu personality inside Xvfb (Wine virtual
# desktop "setu") with a throwaway prefix and home, and checks:
#   1. browsing: a folder opens with its items; the places sidebar has Home,
#      Desktop, Documents, Downloads, the drives and the Recycle Bin; clicks
#      on a place, Back, Forward and Up navigate; sorting; icons view;
#   2. file operations with real input: New folder + in-place name, F2
#      rename, Ctrl+C / Ctrl+V (a "- Copy" next to the original), copy into
#      another folder, drag and drop onto a folder (move);
#   3. the view refreshes when another process changes the folder;
#   4. Delete goes to the Recycle Bin (SHFileOperationW, FOF_ALLOWUNDO),
#      the bin lists it and its own menu restores it; Shift+Del deletes
#      for good;
#   5. Properties shows type/size/location and changes attributes;
#   6. Open with: choosing a program saves it per user in
#      HKCU\Software\Classes and opens the file with it; Enter opens folders;
#   6b. compressed folders: Enter on a .zip browses into it (address bar
#      shows C:\...\test.zip\docs), a file and a folder copy out of it, a
#      path typed inside a .tar.gz opens and copies out, Up leaves it, and
#      Extract All extracts it next to the archive;
#   7. the shell's launcher "Files" entry starts setu-files.exe.
# Screenshots go to build/shell/files-smoke/files-*.png; with --update-docs
# they are also copied to shell/docs/.
#
# Usage: tools/files-smoke.sh [--update-docs] [--keep]
#   --keep  leave Xvfb and the program running (DISPLAY is printed)
#
# Environment:
#   SETU_WINE      wine binary (default: build/personality/install/bin/wine,
#                  falling back to the main checkout's build)
#   SETU_PREFIX    Wine prefix (default: build/files-prefix; recreated)
#   SETU_RES       screen size (default: 1280x800)
#
# HOME and the XDG directories point into build/files-home, so the Recycle
# Bin (the XDG trash) and the profile folders never touch the real home.
# Like tools/shell-smoke.sh it runs with the host name "setu".
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
    -h|--help) sed -n '3,34p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
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

export WINEPREFIX="${SETU_PREFIX:-$REPO/build/files-prefix}"
export WINEDEBUG="${WINEDEBUG:--all}"
export WINEDLLOVERRIDES="mscoree,mshtml="
# Wine's mount manager adds this machine's disks (from udisks on the system
# bus) as drives, with their labels and mount paths; keep them out of the
# prefix and the screenshots
export DBUS_SYSTEM_BUS_ADDRESS=unix:path=/nonexistent
make -s -C "$REPO/shell" -j3   # before HOME moves: the Makefile finds the toolchain under it
export HOME="$REPO/build/files-home" USER=setu LOGNAME=setu
export XDG_DATA_HOME="$HOME/.local/share" XDG_CONFIG_HOME="$HOME/.config" XDG_CACHE_HOME="$HOME/.cache"
RES="${SETU_RES:-1280x800}"
OUT="$REPO/build/shell/files-smoke"
mkdir -p "$OUT"
rm -f "$OUT"/*.png "$OUT"/*.txt

FILES_EXE="$REPO/build/shell/setu-files.exe"
FPROBE="$REPO/build/shell/setu-files-probe.exe"
SHELL_EXE="$REPO/build/shell/setu-shell.exe"
PROBE_EXE="$REPO/build/shell/setu-probe.exe"

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

STATE_WIN='C:\files-state.txt'
STATE="$WINEPREFIX/drive_c/files-state.txt"
fail() { echo "FAIL: $*" >&2; [ -f "$STATE" ] && sed 's/^/  state: /' "$STATE" | grep -v '^  state: \(place\|button\)=' >&2; exit 1; }
pass() { echo "ok: $*"; }

shot() {
  # import reads the root window's pixels as shown; xwd -root drops a
  # window that lies wholly inside another when windows have their own
  # colormaps (Wine's do), which made such popups look black
  import -display "$DISPLAY" -window root "$OUT/$1.png"
}

# wait_state REGEX [SECONDS]: wait until the state file matches
wait_state() {
  local re="$1" t="${2:-15}" i
  for i in $(seq $((t * 10))); do
    tr -d '\r' 2>/dev/null < "$STATE" | grep -Eq -- "$re" && return 0
    sleep 0.1
  done
  return 1
}
st() { tr -d '\r' < "$STATE"; }
seq_now() { st | sed -n 's/^seq=//p'; }
# send ARG...: hand commands to the window and wait until they ran
send() {
  local before; before=$(seq_now)
  "$WINE" "$FPROBE" send "$@" >/dev/null || fail "send $*"
  local i
  for i in $(seq 150); do
    [ "$(seq_now)" != "$before" ] && grep -q '^end=1' "$STATE" && return 0
    sleep 0.1
  done
  fail "commands did not finish: $*"
}
# send_async ARG...: hand commands over without waiting (they may open a dialog)
send_async() { "$WINE" "$FPROBE" send "$@" >/dev/null || fail "send $*"; }
probe() { "$WINE" "$FPROBE" "$@" | tr -d '\r'; }
click() { "$WINE" "$PROBE_EXE" click "$1" "$2" >/dev/null; }
item_xy() { st | awk -F'|' -v n="$1" '$1 == "item=" n { split($NF, p, ","); print p[1], p[2]; exit }'; }
place_xy() { st | awk -F'|' -v n="$1" '$1 == "place=" n { split($4, p, ","); print p[1], p[2]; exit }'; }
button_xy() { st | awk -F'|' -v n="$1" '$1 == "button=" n { split($3, p, ","); print p[1], p[2]; exit }'; }
has_item() { st | grep -q "^item=$1|"; }
D="$WINEPREFIX/drive_c/files-smoke"

# --- a fresh prefix and home -------------------------------------------------
rm -rf "$WINEPREFIX" "$HOME"
mkdir -p "$HOME"
"$WINE" wineboot -i >/dev/null 2>&1
"$WINE" reg add 'HKCU\Software\Wine\Explorer' /v Desktop /d setu /f >/dev/null 2>&1
"$WINE" reg add 'HKCU\Software\Wine\Explorer\Desktops' /v setu /d "$RES" /f >/dev/null 2>&1
"$WINESERVER" -w
find "$WINEPREFIX/dosdevices" -maxdepth 1 -type l -name '[a-y]:*' ! -name 'c:' -delete

mkdir -p "$D/sub" "$D/archive"
printf 'hello from setu\r\n' > "$D/a.txt"
printf 'notes\r\n' > "$D/notes.setusmoke"
head -c 5000 /dev/zero > "$D/big.bin"
printf 'old\r\n' > "$D/zz-old.txt"
touch -d '2020-01-02 03:04:05' "$D/zz-old.txt"

# --- 1. browsing ----------------------------------------------------------------
"$WINE" "$FILES_EXE" --state-file "$STATE_WIN" 'C:\files-smoke' >"$OUT/files.log" 2>&1 &
wait_state '^end=1' 60 || fail "setu-files did not start"
wait_state '^location=fs\|C:\\files-smoke$' 10 || fail "did not open C:\\files-smoke"
for n in a.txt notes.setusmoke big.bin sub archive; do has_item "$n" || fail "item $n missing"; done
st | grep -q '^item=sub|dir|' || fail "sub is not listed as a folder"
st | grep -q '^item=big.bin|file|4.88 KB|' || st | grep -q '^item=big.bin|file|[0-9.]* KB|' || fail "big.bin size column"
# folders first, then files by name
order=$(st | sed -n 's/^item=\([^|]*\)|.*/\1/p' | tr '\n' ' ')
[ "$order" = "archive sub a.txt big.bin notes.setusmoke zz-old.txt " ] || fail "unexpected order: $order"
pass "folder lists: $order"
for p in Home Desktop Documents Downloads 'Recycle Bin'; do
  st | grep -q "^place=$p|" || fail "place $p missing from the sidebar"
done
st | grep -q '^place=.*(C:)|C:\\|' || fail "drive C: missing from the sidebar"
pass "sidebar: $(st | sed -n 's/^place=\([^|]*\)|.*/\1/p' | tr '\n' ',')"
sleep 0.5
shot files-01-details

# sort by size, descending on the second click
send --sort 1
send --sort 1
order=$(st | sed -n 's/^item=\([^|]*\)|file|.*/\1/p' | head -1)
[ "$order" = big.bin ] || fail "size sort (descending): first file is $order"
st | grep -q '^sort=Size,desc' || fail "sort state"
send --sort 0
pass "sorting by size"

# real clicks: a place, Back, Forward, Up
read -r X Y < <(place_xy Documents)
click "$X" "$Y"
wait_state '^location=fs\|C:\\users\\[^\\]*\\Documents$' 10 || wait_state '^location=fs\|.*Documents$' 1 || fail "clicking Documents did not open it"
pass "place click: $(st | grep ^location=)"
read -r X Y < <(button_xy back)
click "$X" "$Y"
wait_state '^location=fs\|C:\\files-smoke$' 10 || fail "Back did not return"
read -r X Y < <(button_xy forward)
click "$X" "$Y"
wait_state '^location=fs\|.*Documents$' 10 || fail "Forward did not go forward"
read -r X Y < <(button_xy up)
click "$X" "$Y"
wait_state '^location=fs\|C:\\users\\[^\\]*$' 10 || fail "Up did not go to the parent"
wait_state '^selected=Documents$' 5 || fail "Up did not select the folder it came from"
pass "back / forward / up (up selects Documents)"
send --go 'C:\files-smoke'
wait_state '^location=fs\|C:\\files-smoke$' 5 || fail "--go"

# --- 2. file operations with real input ------------------------------------
"$WINE" "$PROBE_EXE" setfg 'files-smoke - Files' >/dev/null 2>&1 || true
send --focus
# New folder, named in place by typing
send --cmd newfolder
wait_state '^renaming=1' 5 || fail "New folder did not start an in-place rename"
has_item 'New folder' || fail "no New folder item"
probe type Projects >/dev/null
probe key Return >/dev/null
wait_state '^item=Projects\|dir\|' 10 || fail "typing a name for the new folder did not rename it"
[ -d "$D/Projects" ] || fail "Projects is not on disk"
pass "new folder named Projects in place"

# F2 rename
send --select a.txt --focus
probe key F2 >/dev/null
wait_state '^renaming=1' 5 || fail "F2 did not start renaming"
# the name without its extension is preselected
probe type 'renamed' >/dev/null
probe key Return >/dev/null
wait_state '^item=renamed.txt\|file' 10 || fail "F2 rename did not rename"
[ -f "$D/renamed.txt" ] && [ ! -e "$D/a.txt" ] || fail "rename not on disk"
pass "F2 rename: a.txt -> renamed.txt"

# Ctrl+C, Ctrl+V: a copy next to the original
send --select renamed.txt --focus
probe key ctrl+c >/dev/null
wait_state '^clipboard=files\|' 5 || fail "Ctrl+C did not put files on the clipboard"
probe key ctrl+v >/dev/null
wait_state '^item=renamed - Copy.txt\|file' 10 || fail "Ctrl+V did not make 'renamed - Copy.txt'"
cmp -s "$D/renamed.txt" "$D/renamed - Copy.txt" || fail "copy differs"
pass "copy + paste in place: renamed - Copy.txt"

# copy into another folder: copy here, paste there
send --select big.bin --cmd copy --go 'C:\files-smoke\Projects' --cmd paste
wait_state '^item=big.bin\|file' 10 || fail "paste into Projects"
[ -f "$D/Projects/big.bin" ] && [ -f "$D/big.bin" ] || fail "copy into Projects not on disk"
# cut + paste moves
send --go 'C:\files-smoke' --select zz-old.txt --cmd cut
wait_state '^clipboard=cut\|1' 5 || fail "cut state"
send --go 'C:\files-smoke\archive' --cmd paste
wait_state '^item=zz-old.txt\|file' 10 || fail "cut + paste into archive"
[ -f "$D/archive/zz-old.txt" ] && [ ! -e "$D/zz-old.txt" ] || fail "cut + paste did not move"
pass "copy into Projects, cut + paste into archive"
send --go 'C:\files-smoke'

# drag and drop onto a folder (a real mouse drag): moves it
read -r X1 Y1 < <(item_xy 'renamed - Copy.txt')
read -r X2 Y2 < <(item_xy sub)
[ "$X1" -gt 0 ] && [ "$X2" -gt 0 ] || fail "no screen positions for the drag"
X1=$((X1 + 40)) X2=$((X2 + 30))
# a first click selects it (and activates the window), then drag it
probe setfg 'files-smoke - Files' >/dev/null
probe click "$X1" "$Y1" >/dev/null
sleep 0.8
probe drag "$X1" "$Y1" "$X2" "$Y2" >/dev/null
for _ in $(seq 50); do [ -f "$D/sub/renamed - Copy.txt" ] && break; sleep 0.1; done
[ -f "$D/sub/renamed - Copy.txt" ] && [ ! -e "$D/renamed - Copy.txt" ] || fail "drag and drop onto sub did not move the file"
pass "drag and drop: moved 'renamed - Copy.txt' into sub"
sleep 0.5
shot files-02-operations

# --- 3. outside changes refresh the view -----------------------------------
printf 'x\r\n' > "$D/external.txt"
wait_state '^item=external.txt\|' 10 || fail "the view did not refresh after another process created a file"
pass "refresh on outside change"

# --- 4. Recycle Bin ------------------------------------------------------------
[ "$(probe recycle)" = 0 ] || fail "Recycle Bin not empty at start"
send --select renamed.txt --focus
probe key Delete >/dev/null
# Shift+Del asks first (answer Yes); the Recycle Bin doesn't (owner decision)
confirm() {
  if probe setfg 'Confirm' 5 >"$OUT/confirm.txt" 2>&1; then
    sed 's/^/  confirm dialog: /' "$OUT/confirm.txt"
    sleep 0.3
    probe key Return >/dev/null
  fi
}
if probe setfg 'Confirm' 2 >"$OUT/confirm.txt" 2>&1; then fail "Delete to the Recycle Bin asked for confirmation"; fi
for _ in $(seq 50); do [ ! -e "$D/renamed.txt" ] && break; sleep 0.1; done
[ ! -e "$D/renamed.txt" ] || fail "Delete did not remove renamed.txt"
wait_state '^recycle_count=1' 10 || fail "renamed.txt is not in the Recycle Bin"
pass "Delete: renamed.txt moved to the Recycle Bin ($(probe recycle) item)"
read -r X Y < <(place_xy 'Recycle Bin')
click "$X" "$Y"
wait_state '^location=recycle' 10 || fail "clicking Recycle Bin did not open it"
wait_state '^item=renamed.txt\|file\|C:\\files-smoke\|' 10 || fail "the Recycle Bin does not list renamed.txt with its location"
sleep 0.5
shot files-03-recycle-bin
pass "Recycle Bin lists: $(st | grep '^item=' | cut -d'|' -f1-3)"

# restore through the bin's own menu
send --select renamed.txt --bin-menu
restore=$(st | awk -F'|' '/^bin_menu=/ && tolower($2) ~ /restore/ { sub("bin_menu=", "", $1); print $1; exit }')
[ -n "$restore" ] || fail "no Restore in the Recycle Bin menu: $(st | grep ^bin_menu= | tr '\n' ' ')"
send --bin-invoke "$restore"
for _ in $(seq 50); do [ -f "$D/renamed.txt" ] && break; sleep 0.1; done
[ -f "$D/renamed.txt" ] || fail "Restore did not bring renamed.txt back"
wait_state '^recycle_count=0' 10 || fail "Recycle Bin not empty after restore"
pass "restored renamed.txt from the Recycle Bin"

# Shift+Del: gone for good
send --go 'C:\files-smoke' --select external.txt --focus
probe key shift+Delete >/dev/null
confirm
for _ in $(seq 50); do [ ! -e "$D/external.txt" ] && break; sleep 0.1; done
[ ! -e "$D/external.txt" ] || fail "Shift+Del did not delete external.txt"
sleep 0.5
[ "$(probe recycle)" = 0 ] || fail "Shift+Del put the file in the Recycle Bin"
pass "Shift+Del deleted external.txt permanently"

# --- 5. Properties --------------------------------------------------------------
send --select notes.setusmoke --cmd properties
wait_state '^properties=notes.setusmoke' 5 || fail "no Properties window"
wait_state '^prop_done=1' 10 || fail "size never finished"
st | grep -q '^prop=Location|C:\\files-smoke$' || fail "Properties location"
st | grep -q '^prop=Size|7 bytes (7 bytes)$' || fail "Properties size: $(st | grep '^prop=Size')"
st | grep -q '^prop=Modified|' || fail "Properties modified date"
sleep 0.5
shot files-04-properties
send --props-check readonly 1 --props-button ok
wait_state '^properties=$' 5 || fail "OK did not close Properties"
wait_state '^item=notes.setusmoke\|file\|[^|]*\|[^|]*\|[^|]*\|RA?\|' 10 || fail "read-only not applied: $(st | grep '^item=notes')"
pass "Properties: $(st | grep '^item=notes' | cut -d'|' -f1-6)"
send --select sub --cmd properties
wait_state '^prop_done=1' 10 || fail "folder size never finished"
st | grep -q '^prop=Contains|1 file, 0 folders$' || fail "folder contents: $(st | grep '^prop=Contains')"
rows=$(st | grep -c '^prop=')
send --props-button cancel
pass "folder Properties: $rows rows"

# --- 6. Open with and opening -------------------------------------------------
# wine.inf maps .ini (not .txt) to a class that opens in Notepad
printf '[setu]\r\n' > "$D/settings.ini"
send --cmd refresh
wait_state '^item=settings.ini\|file' 10 || fail "settings.ini not listed"
send --select settings.ini --handlers
st | grep -q '^handler=.*|notepad.exe|' || fail ".ini handlers do not include notepad: $(st | grep ^handler= | tr '\n' ' ')"
pass ".ini handlers: $(st | grep ^handler= | cut -d'|' -f2-4 | tr '\n' ' ')"
send --select notes.setusmoke --handlers
st | grep -q '^handler=' && fail ".setusmoke has handlers before choosing one"
send_async --choose-program 'C:\windows\system32\notepad.exe' always
probe find 'notes.setusmoke' 20 >/dev/null || fail "choosing Notepad did not open notes.setusmoke in it"
"$WINE" reg query 'HKCU\Software\Classes\.setusmoke' > "$OUT/reg.txt" 2>&1 || true
grep -q 'Setu.Choice.notepad.setusmoke' "$OUT/reg.txt" || fail "the choice is not saved in HKCU\\Software\\Classes\\.setusmoke"
"$WINE" reg query 'HKCU\Software\Classes\.setusmoke\OpenWithList\notepad.exe' >/dev/null 2>&1 || fail "no OpenWithList entry"
send --select notes.setusmoke --handlers
st | grep -Eq '^handler=0\|[^|]*\|notepad.exe\|user-choice\|default' || fail "the saved choice is not the default: $(st | grep ^handler= | tr '\n' ' ')"
pass "Open with: Notepad saved as the default for .setusmoke"
sleep 0.5
shot files-05-open-with
probe close 'notes.setusmoke' >/dev/null
# Enter on a folder opens it in place; Backspace goes back
send --select sub --focus
probe key Return >/dev/null
wait_state '^location=fs\|C:\\files-smoke\\sub$' 10 || fail "Enter on a folder did not open it"
probe key Back >/dev/null
wait_state '^location=fs\|C:\\files-smoke$' 10 || fail "Backspace did not go back"
pass "Enter opens folders, Backspace goes back"

# --- 6b. compressed folders --------------------------------------------------
# archives browse like folders (zipfldr), copy out, and Extract All
python3 -c '
import io, sys, tarfile, zipfile
d = sys.argv[1]
with zipfile.ZipFile(d + "/test.zip", "w", zipfile.ZIP_DEFLATED) as z:
    z.writestr("top.txt", "top level\r\n")
    z.writestr("docs/readme.txt", "inside the zip\r\n" * 50)
    z.writestr("docs/deep/x.txt", "deep\r\n")
with tarfile.open(d + "/bundle.tar.gz", "w:gz") as t:
    data = b"from a tarball\r\n"
    info = tarfile.TarInfo("pkg/hello.txt"); info.size = len(data)
    t.addfile(info, io.BytesIO(data))
' "$D"
send --cmd refresh
wait_state '^item=test.zip\|file\|[^|]*\|Compressed \(zipped\) Folder\|' 10 || fail "test.zip is not a Compressed (zipped) Folder: $(st | grep '^item=test.zip')"
send --select test.zip --focus
probe key Return >/dev/null
wait_state '^location=archive\|C:\\files-smoke\\test.zip$' 10 || fail "Enter on test.zip did not browse into it"
st | grep -q '^address=C:\\files-smoke\\test.zip$' || fail "address bar: $(st | grep ^address=)"
st | grep -q '^item=docs|dir|' && st | grep -q '^item=top.txt|file|' || fail "test.zip lists: $(st | grep ^item= | cut -d'|' -f1-2 | tr '\n' ' ')"
send --select docs --cmd open
wait_state '^location=archive\|C:\\files-smoke\\test.zip\\docs$' 10 || fail "docs in test.zip did not open"
st | grep -q '^item=deep|dir|' && st | grep -q '^item=readme.txt|file|' || fail "docs lists: $(st | grep ^item= | cut -d'|' -f1-2 | tr '\n' ' ')"
sleep 0.5
shot files-08-compressed-folder
pass "browsing test.zip: $(st | grep ^address=)"
send --select readme.txt --cmd copy
wait_state '^clipboard=items\|' 5 || fail "copy in a compressed folder did not put items on the clipboard"
send --go 'C:\files-smoke\Projects' --cmd paste
wait_state '^item=readme.txt\|file' 10 || fail "paste from test.zip"
[ "$(tr -d '\r' < "$D/Projects/readme.txt" | sort -u)" = "inside the zip" ] || fail "readme.txt copied out wrong"
send --go 'C:\files-smoke\test.zip' --select docs --cmd copy --go 'C:\files-smoke\Projects' --cmd paste
wait_state '^item=docs\|dir' 10 || fail "paste of a folder from test.zip"
[ -f "$D/Projects/docs/deep/x.txt" ] || fail "the folder copied out of test.zip is missing docs/deep/x.txt"
pass "copy out of test.zip: a file and a folder (recursive)"
send --go 'C:\files-smoke\bundle.tar.gz\pkg'
wait_state '^location=archive\|C:\\files-smoke\\bundle.tar.gz\\pkg$' 10 || fail "typing a path inside bundle.tar.gz did not open it"
send --select hello.txt --cmd copy --go 'C:\files-smoke\sub' --cmd paste
wait_state '^item=hello.txt\|file' 10 || fail "paste from bundle.tar.gz"
cmp -s <(printf 'from a tarball\r\n') "$D/sub/hello.txt" || fail "hello.txt copied out wrong"
pass "browsing and copying out of bundle.tar.gz"
send --go 'C:\files-smoke\test.zip' --cmd up
wait_state '^location=fs\|C:\\files-smoke$' 10 || fail "Up from test.zip"
wait_state '^selected=test.zip$' 5 || fail "Up from test.zip did not select it"
send --select test.zip --cmd extract-all
for _ in $(seq 50); do [ -f "$D/test/docs/deep/x.txt" ] && break; sleep 0.1; done
[ -f "$D/test/top.txt" ] && [ -f "$D/test/docs/deep/x.txt" ] || fail "Extract All did not extract test.zip into test: $(st | grep -E '^(last_op|status)=')"
pass "Extract All: test.zip -> test\\"

# icons view, and the drives
send --cmd icons
wait_state '^view=icons' 5 || fail "icons view"
sleep 0.5
shot files-06-icons
send --cmd details --go Drives
wait_state '^location=drives' 5 || fail "Drives"
st | grep -q '^item=.*(C:)|dir|' || fail "Drives does not list C:"
sleep 0.3
shot files-07-drives
pass "icons view and the Drives list"
send_async --quit
sleep 1

# --- 7. the shell's launcher starts setu-files ------------------------------
SSTATE_WIN='C:\shell-state.txt'
SSTATE="$WINEPREFIX/drive_c/shell-state.txt"
"$WINE" "$SHELL_EXE" --state-file "$SSTATE_WIN" >"$OUT/shell.log" 2>&1 &
for _ in $(seq 300); do grep -q '^launcher=' "$SSTATE" 2>/dev/null && break; sleep 0.1; done
grep -q '^app=builtin|Files|.*setu-files.exe' "$SSTATE" || fail "the launcher's Files entry is not setu-files.exe: $(grep '^app=builtin|Files' "$SSTATE")"
"$WINE" "$SHELL_EXE" --launch Files || fail "--launch Files"
probe find ' - Files' 20 >/dev/null || fail "launching Files did not open setu-files"
pass "launcher Files entry starts setu-files.exe"

echo "screenshots: $OUT"
if [ "$UPDATE_DOCS" = 1 ]; then
  mkdir -p "$REPO/shell/docs"
  for f in "$OUT"/files-*.png; do
    convert "$f" -strip -define png:compression-level=9 "$REPO/shell/docs/$(basename "$f")"
  done
  echo "copied to shell/docs/"
fi
echo "PASS"
