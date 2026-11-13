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
# build 26300 (worked around with a per-app Windows 10 version in
# wine.inf's SetuAppDefaults until ALPC lands, todo 271). Firefox's first
# start used to exit without a window too (todo 88) and passed only on the
# retry; the summary now names any app that needed the retry. A "Crash
# Reporter" window next to the app's window also counts as a failure. The
# window must still be there 30 s later and the app's private memory must
# stay under 1500 MB: Firefox opened and was then OOM-killed in the 3 GB
# image because each of its ten processes held a private 130 MB copy of
# xul.dll (todo 268), which Windows shares between processes.
# It is slow (about three minutes) and needs the staged apps, so it is
# not part of the other smoke tests.
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
#   SETU_APPS_STAY   seconds an app's window must stay open (default 30)
#   SETU_APPS_MAX_MB limit for the app's private memory (default 1500)
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
    -h|--help) sed -n '3,40p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
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
STAY="${SETU_APPS_STAY:-30}"       # seconds a window must stay open
MAX_MB="${SETU_APPS_MAX_MB:-1500}" # limit for an app's private memory
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
# app_mem_mb DIRNAME: MB of anonymous (private) memory in the processes whose
# command line names DIRNAME (the app's folder under Program Files)
app_mem_mb() {
  local kb=0 p v
  for p in $(pgrep -u "$(id -u)" -f "$1" 2>/dev/null); do
    v=$(awk '/^Anonymous:/{print $2}' "/proc/$p/smaps_rollup" 2>/dev/null) || v=
    kb=$(( kb + ${v:-0} ))
  done
  echo $(( kb / 1024 ))
}
probe_windows() { "$WINE" "$PROBE_EXE" windows 2>/dev/null | tr -d '\r'; }

failed=()
retried=()
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
  # a crash reporter next to the window means a child process crashed
  crash= gone= mem=
  if [ "$ok" = y ]; then
    sleep 8
    crash=$(probe_windows | matches 'Crash Reporter') || crash=
    # the window must stay: Firefox once opened and was then killed by the
    # kernel's OOM killer in a 3 GB guest (todo 268), which a window check
    # right after the start cannot see
    shown=$(date +%s)
    while [ -z "$crash" ] && (( $(date +%s) - shown < STAY - 8 )); do
      sleep 3
      probe_windows | matches "$window" > /dev/null || { gone=y; break; }
    done
    [ -n "$gone" ] || probe_windows | matches "$window" > /dev/null || gone=y
    # private memory of the app's processes (their command lines carry the
    # app's folder): the guest has 3 GB for everything
    mem=$(app_mem_mb "$sub")
    note="$note, still open after ${STAY}s, ${mem} MB private memory"
  fi
  if [ -n "$gone" ]; then
    echo "FAIL: $id: $note, but the window was gone before ${STAY}s" >&2
    failed+=("$id")
  elif [ -n "$crash" ]; then
    echo "FAIL: $id: $note, but also a crash reporter window '$crash'" >&2
    failed+=("$id")
  elif [ -n "$mem" ] && (( mem > MAX_MB )); then
    echo "FAIL: $id: $note, over the ${MAX_MB} MB limit" >&2
    failed+=("$id")
  elif [ "$ok" = y ]; then
    echo "PASS: $id: $note"
    [ -z "$again" ] || retried+=("$id")
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
# the retry stays (a first start can legitimately exit), but it is not free:
# an app that needs it is a first-start bug the retry hides
if [ ${#retried[@]} -gt 0 ]; then
  echo "NOTE: needed the retry (first start exited without a window): ${retried[*]}" >&2
fi
if [ ${#failed[@]} -gt 0 ]; then
  echo "FAIL: ${failed[*]}" >&2
  exit 1
fi
echo "apps-xvfb-smoke: all passed"
