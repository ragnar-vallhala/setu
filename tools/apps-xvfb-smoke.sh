#!/usr/bin/env bash
# SPDX-License-Identifier: MIT OR Apache-2.0
#
# Regression guard for default apps on the personality build, without the
# disk image: a fresh prefix inside Xvfb, Firefox and ShareX copied in from
# the image build's staging C: drive (build/distro/apps-stage, written by
# distro/apps/stage.py), each started and checked for its top-level window
# with setu-probe windows, using the window regexes of distro/apps/apps.toml
# (as distro/tests/apps-smoke.sh does in the image).
#
# Both broke once without any other test noticing (todo 269): ShareX
# exited at once, believing another instance ran (special user APCs), and
# Firefox crashes in the Windows App SDK path it takes on Windows 11
# build 26300. It is slow (about three minutes) and needs the staged apps,
# so it is not part of the other smoke tests.
#
# Usage: tools/apps-xvfb-smoke.sh [--keep] [APP-ID...]   (default: firefox sharex)
#   --keep  leave the prefix's processes and Xvfb running
#
# Environment:
#   SETU_WINE        wine binary (default: build/personality/install/bin/wine,
#                    falling back to the main checkout's build)
#   SETU_APPS_STAGE  staging dir (default: build/distro/apps-stage, falling
#                    back to the main checkout's)
#   SETU_PREFIX      Wine prefix (default: build/apps-xvfb-prefix; recreated)
#
# Exit: 0 all windows seen, 1 a failure, 77 the staged apps are missing.
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
MAIN="$(git -C "$REPO" rev-parse --path-format=absolute --git-common-dir 2>/dev/null || true)"
MAIN="${MAIN%/.git}"
KEEP=0
APPS=()
for a in "$@"; do
  case "$a" in
    --keep) KEEP=1 ;;
    -h|--help) sed -n '3,28p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    -*) echo "unknown option: $a" >&2; exit 2 ;;
    *) APPS+=("$a") ;;
  esac
done
[ ${#APPS[@]} -gt 0 ] || APPS=(firefox sharex)

WINE="${SETU_WINE:-$REPO/build/personality/install/bin/wine}"
[ -x "$WINE" ] || WINE="$MAIN/build/personality/install/bin/wine"
[ -x "$WINE" ] || { echo "no personality build found; run tools/build-personality.sh" >&2; exit 1; }
WINESERVER="$(dirname "$WINE")/wineserver"
STAGE="${SETU_APPS_STAGE:-$REPO/build/distro/apps-stage}"
[ -d "$STAGE/c/Program Files" ] || STAGE="$MAIN/build/distro/apps-stage"
if [ ! -d "$STAGE/c/Program Files" ]; then
  echo "SKIP: no staged apps (build/distro/apps-stage); run distro/apps/fetch.py and distro/apps/stage.py" >&2
  exit 77
fi

make -s -C "$REPO/shell"
PROBE_EXE="$REPO/build/shell/setu-probe.exe"
OUT="$REPO/build/apps-xvfb-smoke"
mkdir -p "$OUT"
rm -f "$OUT"/*.log

export WINEPREFIX="${SETU_PREFIX:-$REPO/build/apps-xvfb-prefix}"
export HOME="$REPO/build/apps-xvfb-home"
export XDG_DATA_HOME="$HOME/.local/share" XDG_CONFIG_HOME="$HOME/.config" XDG_CACHE_HOME="$HOME/.cache"
export USER=setu LOGNAME=setu TZ=UTC
export WINEDEBUG="${WINEDEBUG:--all}"
export WINEDLLOVERRIDES="mshtml=;winemenubuilder.exe=d"
unset WAYLAND_DISPLAY XAUTHORITY
mkdir -p "$HOME"

# --- X server -------------------------------------------------------------
for n in $(seq 200 250); do
  [ -e "/tmp/.X11-unix/X$n" ] || [ -e "/tmp/.X$n-lock" ] || { DISP=$n; break; }
done
Xvfb ":$DISP" -screen 0 1280x800x24 -nolisten tcp >/dev/null 2>&1 &
XVFB_PID=$!
export DISPLAY=":$DISP"
for _ in $(seq 50); do [ -e "/tmp/.X11-unix/X$DISP" ] && break; sleep 0.1; done

cleanup() {
  if [ "$KEEP" = 1 ]; then
    echo "kept running on DISPLAY=$DISPLAY (Xvfb pid $XVFB_PID, WINEPREFIX=$WINEPREFIX)"
    return
  fi
  "$WINESERVER" -k >/dev/null 2>&1 || true
  kill "$XVFB_PID" >/dev/null 2>&1 || true
}
trap cleanup EXIT

# --- prefix -----------------------------------------------------------------
T0=$(date +%s)
rm -rf "$WINEPREFIX"
# without Wine Mono's and Gecko's installers (the apps bring their own runtimes)
WINEDLLOVERRIDES="mscoree,mshtml=;winemenubuilder.exe=d" timeout 300 "$WINE" wineboot -i > "$OUT/wineboot.log" 2>&1 \
  || { echo "FAIL: wineboot" >&2; exit 1; }
"$WINESERVER" -w
mkdir -p "$WINEPREFIX/drive_c/setu-apps/ffprofile"
printf '<!doctype html><html><head><meta charset="utf-8"><title>Setu sample</title></head><body><h1>Setu</h1></body></html>\n' \
  > "$WINEPREFIX/drive_c/setu-apps/sample.html"
echo "prefix ready in $(( $(date +%s) - T0 ))s"

# id, exe, args (tab-separated), window regex, timeout; US-separated
apps_list() {
  python3 - "$REPO/distro/apps/apps.toml" "${APPS[@]}" <<'EOF'
import sys, tomllib
d = tomllib.load(open(sys.argv[1], 'rb'))
want = sys.argv[2:]
apps = {a['id']: a for a in d['app']}
for i in want:
    a = apps[i]
    print('\x1f'.join([a['id'], a['exe'], '\t'.join(a.get('args', [])), a['window'],
                       str(a.get('window_timeout', 90))]))
EOF
}
# matches REGEX: stdin (setu-probe windows) has a "class|title" matching it
matches() {
  python3 -c '
import re, sys
rx = re.compile(sys.argv[1])
for l in sys.stdin.read().replace("\r", "").splitlines():
    if l.startswith("window="):
        f = l[7:].split("|")
        if len(f) >= 3 and rx.search(f[1] + "|" + f[2]):
            print(f[1] + "|" + f[2]); sys.exit(0)
sys.exit(1)' "$1"
}
probe_windows() { "$WINE" "$PROBE_EXE" windows 2>/dev/null | tr -d '\r'; }

failed=()
while IFS=$'\x1f' read -r -u 3 id exe args window timeout; do
  # copy the app's folder from the staging C: drive
  dir=${exe#[Cc]:\\}; dir=${dir%%\\*}            # "Program Files"
  sub=${exe#[Cc]:\\"$dir"\\}; sub=${sub%%\\*}     # "Mozilla Firefox"
  [ -d "$STAGE/c/$dir/$sub" ] || { echo "FAIL: $id: $STAGE/c/$dir/$sub missing" >&2; failed+=("$id"); continue; }
  cp -a "$STAGE/c/$dir/$sub" "$WINEPREFIX/drive_c/$dir/"
  a=${args//%SAMPLE_HTML%/C:\\setu-apps\\sample.html}
  argv=()
  [ -z "$a" ] || IFS=$'\t' read -r -a argv <<< "$a"
  start=$(date +%s) again= note=
  "$WINE" "$exe" "${argv[@]}" > "$OUT/$id.log" 2>&1 < /dev/null &
  pid=$!
  ok=n
  while (( $(date +%s) - start < timeout )); do
    if hit=$(probe_windows | matches "$window"); then
      ok=y; note="window '$hit' after $(( $(date +%s) - start ))s$again"; break
    fi
    # gone without a window: start it once more, as apps-smoke.sh does
    if ! kill -0 "$pid" 2>/dev/null && [ -z "$again" ] && (( $(date +%s) - start >= 10 )); then
      again=" (the first start exited without a window)"
      "$WINE" "$exe" "${argv[@]}" >> "$OUT/$id.log" 2>&1 < /dev/null &
      pid=$!
    fi
    sleep 2
  done
  if [ "$ok" = y ]; then
    echo "PASS: $id: $note"
  else
    echo "FAIL: $id: no window matching /$window/ in ${timeout}s$again; windows:" \
      "$(probe_windows | sed -n 's/^window=[^|]*|\([^|]*|[^|]*\)|.*/\1/p' | head -3 | xargs -d '\n' | cut -c1-160 || true)" >&2
    failed+=("$id")
  fi
  "$WINESERVER" -k >/dev/null 2>&1 || true
  wait "$pid" 2>/dev/null || true
  "$WINESERVER" -w
done 3< <(apps_list)

echo "total $(( $(date +%s) - T0 ))s; logs in $OUT"
if [ ${#failed[@]} -gt 0 ]; then
  echo "FAIL: ${failed[*]}" >&2
  exit 1
fi
echo "apps-xvfb-smoke: all passed"
