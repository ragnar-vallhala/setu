#!/bin/bash
# SPDX-License-Identifier: MIT OR Apache-2.0
# Third-party service check: a Windows service manager that isn't ours
# installs, runs and uninstalls a service on Setu.
#
# WinSW 2.12.0 (MIT licence, https://github.com/winsw/winsw, release
# v2.12.0 of 2023-01-28) wraps any program as a Windows service: its
# `install` calls CreateService, `start` StartService, and the service it
# runs is WinSW itself (StartServiceCtrlDispatcher, SetServiceStatus, ...),
# which starts and supervises the wrapped program and logs its output.
# WinSW-x64.exe is the self-contained .NET build, so no .NET Framework is
# needed in the prefix.
#
# Steps, in a fresh Setu prefix (build/thirdparty/winsw/prefix):
#   1. download WinSW-x64.exe once (checked against the pinned sha256),
#   2. build heartbeat.exe (a tiny program that writes a counter to a file
#      and stdout every 100 ms) with the mingw-w64 cross compiler,
#   3. `WinSW install`  -> the service exists (sc qc),
#   4. `WinSW start`    -> the service is RUNNING (sc query), the heartbeat
#                          file keeps growing, WinSW's .out.log has its output,
#   5. `WinSW status` reports Started,
#   6. `WinSW stop`     -> STOPPED, the heartbeat stops (child process gone),
#   7. `WinSW uninstall`-> the service no longer exists.
# The binary is never committed and runs only in that prefix.
#
# Usage: tests/thirdparty/winsw-service.sh [--keep] [--wine PATH]
#   --keep        keep the prefix afterwards (default: deleted on success)
#   --wine PATH   wine to use (default: build/personality/install/bin/wine)
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
OUT=$REPO/build/thirdparty/winsw
WINSW_URL=https://github.com/winsw/winsw/releases/download/v2.12.0/WinSW-x64.exe
# sha256 of the v2.12.0 WinSW-x64.exe release asset (18243033 bytes), taken
# when it was first downloaded on 2026-11-05; GitHub publishes no digest
# for this release.
WINSW_SHA256=05b82d46ad331cc16bdc00de5c6332c1ef818df8ceefcd49c726553209b3a0da
WINE=$REPO/build/personality/install/bin/wine
KEEP=0
ID=SetuWinSWTest

usage() { sed -n '2,/^set -euo/{/^set -euo/d;s/^# \{0,1\}//;p}' "$0"; }
while [ $# -gt 0 ]; do
    case $1 in
        --keep) KEEP=1 ;;
        --wine) WINE=$2; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "unknown option: $1 (see --help)" >&2; exit 2 ;;
    esac
    shift
done
[ -x "$WINE" ] || { echo "no wine at $WINE (run tools/build-personality.sh)" >&2; exit 2; }
WINESERVER=$(dirname "$WINE")/wineserver
export PATH=$HOME/.local/setu-toolchain/bin:$PATH

trap 'echo "FAIL: line $LINENO exited with $?" >&2' ERR
pass() { echo "PASS: $*"; }
fail() { echo "FAIL: $*" >&2; exit 1; }

# --- 1. download ---------------------------------------------------------------
DL=$OUT/download
EXE=$DL/WinSW-x64.exe
mkdir -p "$DL"
if [ ! -f "$EXE" ]; then
    echo "downloading $WINSW_URL"
    curl -sfL -m 300 -o "$EXE.part" "$WINSW_URL" || fail "download of $WINSW_URL failed"
    mv "$EXE.part" "$EXE"
fi
echo "$WINSW_SHA256  $EXE" | sha256sum -c --quiet - || fail "sha256 mismatch for $EXE"
pass "WinSW-x64.exe 2.12.0 (sha256 ok)"

# --- prefix --------------------------------------------------------------------
export WINEPREFIX=$OUT/prefix WINEDEBUG=-all WINEDLLOVERRIDES='mscoree,mshtml=;winemenubuilder.exe=d'
unset DISPLAY WAYLAND_DISPLAY XAUTHORITY
cleanup() {
    "$WINESERVER" -k 2>/dev/null || true
    if [ "$KEEP" = 0 ] && [ "${OK:-0}" = 1 ]; then rm -rf "$WINEPREFIX"; fi
}
trap cleanup EXIT
"$WINESERVER" -k 2>/dev/null || true
rm -rf "$WINEPREFIX"
"$WINE" wineboot --init >/dev/null 2>&1 || [ -f "$WINEPREFIX/system.reg" ] || fail "wineboot failed"
"$WINESERVER" -w
W=$WINEPREFIX/drive_c/setu-winsw
mkdir -p "$W"

# --- 2. the wrapped program ----------------------------------------------------
cat > "$OUT/heartbeat.c" <<'EOF'
/* Writes "beat N" to C:\setu-winsw\beats.txt (rewritten) and stdout every 100 ms. */
#include <windows.h>
#include <stdio.h>
int main(void)
{
    for (unsigned n = 1;; n++) {
        FILE *f = fopen("C:\\setu-winsw\\beats.txt", "w");
        if (f) fprintf(f, "%u\n", n), fclose(f);
        printf("beat %u\n", n);
        fflush(stdout);
        Sleep(100);
    }
}
EOF
x86_64-w64-mingw32-gcc -O1 -o "$W/heartbeat.exe" "$OUT/heartbeat.c" || fail "cannot build heartbeat.exe"
# One services.exe for the whole check, as on a running Setu system: without a
# user process wine shuts it down between commands, and a new one autostarts
# the AUTO_START service, as at boot.
printf '#include <windows.h>\nint main(void) { Sleep(INFINITE); return 0; }\n' > "$OUT/keepalive.c"
x86_64-w64-mingw32-gcc -O1 -o "$W/keepalive.exe" "$OUT/keepalive.c" || fail "cannot build keepalive.exe"
"$WINE" "C:\\setu-winsw\\keepalive.exe" < /dev/null > /dev/null 2>&1 &
# and let it bring services.exe up before anything else starts
for i in $(seq 100); do pgrep -f 'setu-winsw.keepalive\.exe' > /dev/null && break; sleep 0.1; done
for i in $(seq 100); do "$WINE" sc query type= service > /dev/null 2>&1 && break; sleep 0.1; done
cp "$EXE" "$W/$ID.exe"
cat > "$W/$ID.xml" <<EOF
<service>
  <id>$ID</id>
  <name>Setu WinSW test</name>
  <description>Runs heartbeat.exe as a service (Setu third-party service check).</description>
  <executable>C:\\setu-winsw\\heartbeat.exe</executable>
  <workingdirectory>C:\\setu-winsw</workingdirectory>
  <logpath>C:\\setu-winsw\\logs</logpath>
  <log mode="reset"/>
  <stoptimeout>5 sec</stoptimeout>
</service>
EOF

# Output goes through a file, not a pipe: a process the command starts (such as
# services.exe) inherits its stdout and would keep a pipe open.
run() { local rc=0; "$@" > "$OUT/run.tmp" 2>&1 || rc=$?; tr -d '\r' < "$OUT/run.tmp"; return $rc; }
winsw() { (cd "$W" && run "$WINE" "C:\\setu-winsw\\$ID.exe" "$@"); }
sc() { run "$WINE" sc "$@"; }
state() { sc query "$ID" | sed -n 's/^ *STATE *: *[0-9]* *\([A-Z_]*\).*/\1/p'; }
beat() { cat "$W/beats.txt" 2>/dev/null | tr -d '\r\n' || true; }
wait_state() {
    local i
    for i in $(seq 100); do [ "$(state)" = "$1" ] && return 0; sleep 0.2; done
    return 1
}

# --- 3. install ----------------------------------------------------------------
winsw install > "$OUT/install.log" || fail "WinSW install failed: $(cat "$OUT/install.log")"
sc qc "$ID" > "$OUT/qc.txt"
grep -q "SERVICE_NAME: *$ID" "$OUT/qc.txt" || fail "sc qc doesn't show $ID after install (see $OUT/qc.txt)"
grep -qi "BINARY_PATH_NAME.*$ID.exe" "$OUT/qc.txt" || fail "unexpected binary path (see $OUT/qc.txt)"
[ "$(state)" = STOPPED ] || fail "state after install: $(state)"
pass "install: service $ID created ($(grep -i START_TYPE "$OUT/qc.txt" | tr -s ' '))"

# --- 4. start ------------------------------------------------------------------
winsw start > "$OUT/start.log" || fail "WinSW start failed: $(cat "$OUT/start.log")"
wait_state RUNNING || fail "not RUNNING after start (state: $(state)); see $OUT/start.log, $W/logs"
b1=; for i in $(seq 100); do b1=$(beat); [ -n "$b1" ] && break; sleep 0.1; done
[ -n "$b1" ] || fail "heartbeat.exe never wrote its file"
sleep 1
b2=$(beat)
[ "${b2:-0}" -gt "$b1" ] || fail "heartbeat not advancing ($b1 -> $b2)"
pass "start: RUNNING, heartbeat $b1 -> $b2"
for i in $(seq 50); do grep -qs "beat" "$W/logs/$ID.out.log" && break; sleep 0.1; done
grep -qs "beat" "$W/logs/$ID.out.log" || fail "WinSW didn't capture the program's output ($W/logs)"
pass "WinSW captured stdout: $(grep -c beat "$W/logs/$ID.out.log") lines in $ID.out.log"

# --- 5. status -----------------------------------------------------------------
st=$(winsw status)
echo "$st" | grep -q "Started" || fail "WinSW status: $st"
pass "status: $st"

# --- 6. stop -------------------------------------------------------------------
winsw stop > "$OUT/stop.log" || fail "WinSW stop failed: $(cat "$OUT/stop.log")"
wait_state STOPPED || fail "not STOPPED after stop (state: $(state))"
s1=$(beat); sleep 1; s2=$(beat)
[ "$s1" = "$s2" ] || fail "heartbeat still running after stop ($s1 -> $s2)"
pass "stop: STOPPED, heartbeat stopped at $s2"

# --- 7. uninstall --------------------------------------------------------------
winsw uninstall > "$OUT/uninstall.log" || fail "WinSW uninstall failed: $(cat "$OUT/uninstall.log")"
# sc fails with 1060 (ERROR_SERVICE_DOES_NOT_EXIST) once it is gone
gone() { local out; out=$(sc query "$ID" || true); [[ $out == *1060* ]]; }
for i in $(seq 50); do gone && break; sleep 0.2; done
gone || fail "service still exists after uninstall: $(sc query "$ID" || true)"
pass "uninstall: service gone"
OK=1
echo "all checks passed"
