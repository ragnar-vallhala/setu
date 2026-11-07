# SPDX-License-Identifier: MIT OR Apache-2.0
# Password login (first-boot wizard with "Log in automatically" unticked):
# after logging in on tty1, start the Setu session, as setu-session.service
# does for the autologin (same compositor, environment and session
# program). The login already made a logind session on seat0, which gives
# labwc the display and input devices. Quitting the session logs out, back
# to the login prompt.
if [ "$(tty 2>/dev/null)" = /dev/tty1 ] && [ -z "${WAYLAND_DISPLAY:-}" ] && [ "$(id -u)" -ne 0 ] \
    && [ -x /usr/local/bin/setu-session ]; then
    # the renderer (GPU or software) is chosen by setu-compositor
    export WLR_NO_HARDWARE_CURSORS=1 XDG_SESSION_TYPE=wayland
    # the keyboard layout chosen in the wizard
    if [ -r /etc/default/keyboard ]; then
        XKBMODEL= XKBLAYOUT= XKBVARIANT= XKBOPTIONS=
        . /etc/default/keyboard
        export XKB_DEFAULT_MODEL="$XKBMODEL" XKB_DEFAULT_LAYOUT="$XKBLAYOUT" \
            XKB_DEFAULT_VARIANT="$XKBVARIANT" XKB_DEFAULT_OPTIONS="$XKBOPTIONS"
    fi
    cd "$HOME" || true
    exec /usr/lib/setu/setu-compositor
fi
