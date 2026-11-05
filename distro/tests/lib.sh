# SPDX-License-Identifier: MIT OR Apache-2.0
# Shared helpers for the distro smoke tests (sourced, not run). The caller
# sets REPO, RUN (run dir), SSH_PORT and KEEP before calling boot_vm.

pass() { echo "PASS: $*"; }
fail() { echo "FAIL: $*" >&2; exit 1; }

SSH_OPTS=(-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR -o ConnectTimeout=5
          -o PreferredAuthentications=password -o PubkeyAuthentication=no)
gssh() { sshpass -p setu ssh "${SSH_OPTS[@]}" -p "$SSH_PORT" setu@127.0.0.1 "$@"; }
gscp() { sshpass -p setu scp "${SSH_OPTS[@]}" -P "$SSH_PORT" "$@"; }
# monitor CMD: a QEMU monitor command; its output (without the banner and
# the echoed command) goes to stdout
monitor() {
    printf '%s\n' "$1" | nc -U -q1 "$RUN/monitor.sock" | tr -d '\r' \
        | sed -e '/^QEMU .* monitor/d' -e '/^(qemu)/d' -e 's/\x1b\[[0-9;]*[A-Za-z]//g'
}

stop_vm() {
    [ "$KEEP" = 1 ] && { echo "VM left running (pid $(cat "$RUN/qemu.pid" 2>/dev/null))"; return; }
    [ -S "$RUN/monitor.sock" ] && monitor quit >/dev/null || true
    sleep 1
    [ -f "$RUN/qemu.pid" ] && kill "$(cat "$RUN/qemu.pid")" 2>/dev/null || true
    rm -f "$RUN/qemu.pid"
}

# boot_vm IMAGE VNC: boot a throwaway snapshot of IMAGE, wait for SSH and
# install the guest helper /tmp/setu-env
boot_vm() {
    trap stop_vm EXIT
    "$REPO/distro/run-qemu.sh" --image "$1" --vnc "$2" --ssh-port "$SSH_PORT" \
        --run-dir "$RUN" --snapshot --daemonize
    local i
    for i in $(seq 90); do gssh true 2>/dev/null && break; sleep 2; done
    gssh true || fail "no SSH on port $SSH_PORT after 3 minutes (serial log: $RUN/serial.log)"
    pass "SSH up"

    # Guest helper: the session's environment (setu-session) plus the X
    # display Xwayland opened, then run the arguments.
    gssh 'cat > /tmp/setu-env && chmod +x /tmp/setu-env' <<'EOF'
#!/bin/sh
export PATH=/opt/setu/personality/bin:$PATH WINEPREFIX=$HOME/.setu/prefix
export WINEDEBUG=-all WINEDLLOVERRIDES='mscoree,mshtml='
for s in /tmp/.X11-unix/X*; do [ -S "$s" ] && export DISPLAY=:${s##*/X} && break; done
exec "$@"
EOF
}

# gwine ARGS...: run wine in the guest session, each argument passed as is
gwine() { gssh "/tmp/setu-env wine $(printf '%q ' "$@")"; }
probe() { gwine /opt/setu/shell/setu-probe.exe "$@" | tr -d '\r'; }
# wait_window REGEX [SECONDS]: a visible top-level window line matches
wait_window() {
    local i
    for i in $(seq "${2:-30}"); do
        probe windows 2>/dev/null | grep -Eq "$1" && return 0
        sleep 1
    done
    return 1
}

# wait_session: the shell is running and its taskbar is up
wait_session() {
    local i
    for i in $(seq 60); do gssh pgrep -f setu-shell.exe >/dev/null 2>&1 && break; sleep 2; done
    gssh pgrep -f setu-shell.exe >/dev/null || fail "setu-shell.exe not running (journal: ssh in and run journalctl -u setu-session)"
    gssh test -f /opt/setu/shell/setu-probe.exe || fail "the image has no setu-probe.exe (rebuild it with --shell from a build with make -C shell)"
    wait_window '\|Setu taskbar\|' 60 || fail "no Setu taskbar window"
    pass "session up: $(probe windows | grep -c '^window=') visible top-level windows"
}

# screendump PNG: save the VM's display, scaled to fit 1024x1024
screendump() {
    local ppm=$RUN/screen.ppm i
    rm -f "$ppm"
    monitor "screendump $ppm" >/dev/null
    for i in $(seq 10); do [ -s "$ppm" ] && break; sleep 0.5; done
    [ -s "$ppm" ] || fail "screendump failed"
    mkdir -p "$(dirname "$1")"
    convert "$ppm" -resize '1024x1024>' +dither -colors 128 -strip "PNG8:$1"
    rm -f "$ppm"
}
