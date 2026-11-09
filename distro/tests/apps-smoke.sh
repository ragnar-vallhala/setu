#!/bin/bash
# SPDX-License-Identifier: MIT OR Apache-2.0
# The default apps on the dev image (distro/apps, docs/DEFAULT_APPS.md):
# boot it in QEMU (--snapshot: the image is left untouched), wait for the
# session, then
#   1. the apps' registry entries were imported into the prefix
#      (HKLM\Software\Setu\DefaultApps Stamp) and Setu's default apps own the main
#      file types (.pdf .txt .png .jpg .zip .html .docx .mp4 .svg .kdbx);
#   2. the Start menu's common Programs folder has a shortcut for each app
#      and for Wine's accessories, and none for the apps with
#      start_menu = false;
#   3. for each app in apps.toml and each accessory: its exe exists on C:,
#      it starts (from the session, as the Start menu would) and shows a
#      visible top-level window matching the manifest's pattern within its
#      timeout (setu-probe windows); a QEMU screendump of each is saved in
#      build/distro/apps-smoke/<id>.png; then it is closed.
# Apps with a known_issue in apps.toml are reported but do not fail the
# test. Results: build/distro/apps-smoke/results.tsv.
#
# Usage: distro/tests/apps-smoke.sh [options]
#   --image FILE      dev image (default: build/distro/apps-check/setu-dev.qcow2)
#   --build           build that image first (distro/build-image.sh --mem 3072
#                     with this checkout's shell, build/shell)
#   --vnc N           VNC display (default: 26)
#   --ssh-port N      host port for the guest's SSH (default: 2246)
#   -a ID             only this app (repeatable)
#   --update-docs     copy small screenshots to docs/img/apps/<id>.png
#   --keep            leave the VM running at the end (it still discards writes)
#   -h, --help
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
CHECK=$REPO/build/distro/apps-check
OUT=$REPO/build/distro/apps-smoke
IMAGE=$CHECK/setu-dev.qcow2
VNC=26
SSH_PORT=2246
KEEP=0
DOCS=0
BUILD=0
ONLY=()

usage() { sed -n '2,/^set -euo/{/^set -euo/d;s/^# \{0,1\}//;p}' "$0"; }
while [ $# -gt 0 ]; do
    case $1 in
        --image) IMAGE=$2; shift ;;
        --build) BUILD=1 ;;
        --vnc) VNC=$2; shift ;;
        --ssh-port) SSH_PORT=$2; shift ;;
        -a) ONLY+=("$2"); shift ;;
        --update-docs) DOCS=1 ;;
        --keep) KEEP=1 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "unknown option: $1 (see --help)" >&2; exit 2 ;;
    esac
    shift
done
RUN=$OUT/run
rm -rf "$OUT"
mkdir -p "$OUT" "$RUN"
. "$HERE/lib.sh"
python3 -c 'import PIL' 2>/dev/null || fail "needs python3-pil on the host"

if [ "$BUILD" = 1 ]; then
    [ -f "$REPO/build/shell/setu-shell.exe" ] || fail "no build/shell/setu-shell.exe (make -C shell)"
    "$REPO/distro/build-image.sh" --mem 3072 --shell "$REPO/build/shell/setu-shell.exe" --out "$IMAGE" \
        || fail "image build failed"
fi
[ -f "$IMAGE" ] || fail "no image at $IMAGE (pass --build)"

# The apps and accessories from the manifest, one per line:
# id, exe, args (tab-separated), window regex, timeout, known issue,
# separated by US (0x1f): the regexes have | in them
apps_list() {
    python3 - "$REPO/distro/apps/apps.toml" "${ONLY[@]}" <<'EOF'
import sys, tomllib
d = tomllib.load(open(sys.argv[1], 'rb'))
only = sys.argv[2:]
for a in d['app'] + d.get('builtin', []):
    if only and a['id'] not in only:
        continue
    print('\x1f'.join([a['id'], a['exe'], '\t'.join(a.get('args', [])), a['window'],
                    str(a.get('window_timeout', 90)), a.get('known_issue', '')]))
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
# unix path of a C:\ path on the guest (C: is the casefold system volume)
cpath() { local p=${1#[Cc]:}; echo "/var/lib/setu/volumes/c${p//\\//}"; }

# --- sample files for the apps to open ------------------------------------------
S=$OUT/samples
mkdir -p "$S"
printf 'Setu default apps smoke test.\r\nHello from sample.txt\r\n' > "$S/sample.txt"
cat > "$S/sample.html" <<'EOF'
<!doctype html><html><head><meta charset="utf-8"><title>Setu sample</title></head>
<body style="font-family:sans-serif"><h1>Setu</h1><p>Default apps smoke test.</p></body></html>
EOF
cat > "$S/sample.svg" <<'EOF'
<svg xmlns="http://www.w3.org/2000/svg" width="200" height="120" viewBox="0 0 200 120">
<rect x="10" y="10" width="180" height="100" rx="12" fill="#1F6FB2"/>
<text x="100" y="70" font-size="28" text-anchor="middle" fill="#fff" font-family="sans-serif">Setu</text></svg>
EOF
python3 - "$S" <<'EOF'
import sys
from PIL import Image, ImageDraw
d = sys.argv[1]
im = Image.new('RGB', (320, 200), '#1F6FB2')
ImageDraw.Draw(im).ellipse((110, 50, 210, 150), fill='#F5A623')
im.save(d + '/sample.png')
# a one-page PDF (ISO 32000-1: header, objects, xref, trailer)
objs = [b'<< /Type /Catalog /Pages 2 0 R >>',
        b'<< /Type /Pages /Kids [3 0 R] /Count 1 >>',
        b'<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Contents 4 0 R '
        b'/Resources << /Font << /F1 5 0 R >> >> >>',
        None,
        b'<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>']
text = b'BT /F1 36 Tf 72 720 Td (Setu sample PDF) Tj ET'
objs[3] = b'<< /Length %d >>\nstream\n' % len(text) + text + b'\nendstream'
out = bytearray(b'%PDF-1.4\n')
offs = []
for i, o in enumerate(objs):
    offs.append(len(out))
    out += b'%d 0 obj\n' % (i + 1) + o + b'\nendobj\n'
x = len(out)
out += b'xref\n0 %d\n0000000000 65535 f \n' % (len(objs) + 1)
for o in offs:
    out += b'%010d 00000 n \n' % o
out += b'trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%d\n%%%%EOF\n' % (len(objs) + 1, x)
open(d + '/sample.pdf', 'wb').write(out)
EOF

# --- boot -------------------------------------------------------------------------
boot_vm "$IMAGE" "$VNC"
wait_session
# killapp [-q] TEXT: kill the processes whose command line contains TEXT
# (-q: only tell whether there are any)
gssh 'cat > /tmp/killapp && chmod +x /tmp/killapp' <<'EOF'
#!/bin/sh
q=0; [ "$1" = -q ] && { q=1; shift; }
for p in /proc/[0-9]*; do
    case ${p#/proc/} in $$|$PPID) continue ;; esac
    tr '\0' ' ' 2>/dev/null < $p/cmdline | grep -qiF -- "$1" || continue
    [ $q = 1 ] && exit 0
    kill -9 ${p#/proc/} 2>/dev/null
done
[ $q = 0 ]
EOF
CS=/var/lib/setu/volumes/c/setu-apps
gssh "mkdir -p $CS/ffprofile"
gscp "$S"/sample.* "$GUEST_USER@127.0.0.1:$CS/"
pass "sample files in C:\\setu-apps"

# --- 1. registry ------------------------------------------------------------------
gssh test -f /usr/share/setu/apps/apps.reg || fail "the image has no /usr/share/setu/apps/apps.reg (built with --no-apps?)"
want=$(gssh cat /usr/share/setu/apps/stamp 2>/dev/null | tr -d '\r' || true)
have=$(gwine reg query 'HKLM\Software\Setu\DefaultApps' /v Stamp 2>/dev/null | tr -d '\r' | sed -n 's/^ *Stamp *REG_SZ *//p' || true)
[ -n "$want" ] && [ "$have" = "$want" ] \
    || fail "setu-apps-register has not imported apps.reg into the prefix (HKLM\\Software\\Setu\\DefaultApps Stamp: '$have', want '$want')"
pass "apps.reg imported into the prefix"
# default ProgID of EXT, and its open command
assoc() {
    local progid
    progid=$(gwine reg query "HKCR\\$1" /ve 2>/dev/null | tr -d '\r' | sed -n 's/^ *(Default) *REG_SZ *//p')
    [ -n "$progid" ] || { echo "-"; return; }
    echo "$progid => $(gwine reg query "HKCR\\$progid\\shell\\open\\command" /ve 2>/dev/null | tr -d '\r' \
        | sed -n 's/^ *(Default) *REG_[A-Z_]*SZ *//p')"
}
bad=0
for pair in .pdf:SumatraPDF.exe .txt:notepad++.exe .png:ImageGlass.exe .zip:7zFM.exe .html:firefox.exe \
            .docx:swriter.exe .mp4:vlc.exe .svg:inkscape .kdbx:KeePassXC.exe; do
    ext=${pair%%:*} exe=${pair#*:}
    a=$(assoc "$ext")
    echo "  $ext -> $a"
    grep -qiF "$exe" <<< "$a" || { echo "FAIL: $ext does not open with $exe" >&2; bad=1; }
done
[ "$bad" = 0 ] || fail "file associations"
pass "file associations: Setu's default apps own .pdf .txt .png .zip .html .docx .mp4 .svg .kdbx"

# --- 2. Start menu ------------------------------------------------------------------
PROGS='/var/lib/setu/volumes/c/ProgramData/Microsoft/Windows/Start Menu/Programs'
gssh "cd '$PROGS' && find . -iname '*.lnk' | sort" | tr -d '\r' > "$OUT/start-menu.txt"
nlnk=$(wc -l < "$OUT/start-menu.txt")
[ "$nlnk" -ge 14 ] || fail "only $nlnk shortcuts in the common Programs folder (see $OUT/start-menu.txt)"
for l in WordPad Clock; do
    grep -qx "./$l.lnk" "$OUT/start-menu.txt" || fail "no $l.lnk in the common Programs folder"
done
# apps kept out of the Start menu: no shortcut names them
while read -r name; do
    [ -n "$name" ] || continue
    if grep -iF -- "$name" "$OUT/start-menu.txt"; then
        fail "$name has a Start menu shortcut, but apps.toml says start_menu = false"
    fi
done < <(python3 -c '
import sys, tomllib
for a in tomllib.load(open(sys.argv[1], "rb"))["app"]:
    if a.get("start_menu", True) is False:
        print(a["name"])' "$REPO/distro/apps/apps.toml")
pass "Start menu: $nlnk shortcuts in the common Programs folder"

# --- 3. each app ------------------------------------------------------------------
printf 'id\texe\twindow\tscreenshot\tnote\n' > "$OUT/results.tsv"
failed=() known=()
while IFS=$'\x1f' read -r -u 3 id exe args window timeout issue; do
    note=
    if gssh "test -f \"$(cpath "$exe")\""; then exe_ok=y; else exe_ok=n; fi
    win_ok=n shot=-
    if [ "$exe_ok" = y ]; then
        a=${args//%SAMPLE_TXT%/C:\\setu-apps\\sample.txt}
        a=${a//%SAMPLE_HTML%/C:\\setu-apps\\sample.html}
        a=${a//%SAMPLE_SVG%/C:\\setu-apps\\sample.svg}
        a=${a//%SAMPLE_PNG%/C:\\setu-apps\\sample.png}
        a=${a//%SAMPLE_PDF%/C:\\setu-apps\\sample.pdf}
        IFS=$'\t' read -r -a argv <<< "$a"
        # its processes: those whose command line names its folder
        root=$(python3 -c 'import re, sys; m = re.match(r"(?i)C:\\Program Files( \(x86\))?\\[^\\]+", sys.argv[1]); print(m.group(0) if m else sys.argv[1])' "$exe")
        launch="cd /tmp; nohup /tmp/setu-env wine $(printf '%q ' "$exe" "${argv[@]}") > /tmp/app-$id.log 2>&1 < /dev/null &"
        start=$(date +%s) again=
        gssh "$launch" || true
        while (( $(date +%s) - start < timeout )); do
            if hit=$(probe windows 2>/dev/null | matches "$window"); then
                win_ok=y
                note="window '$hit' after $(( $(date +%s) - start ))s$again"
                break
            fi
            # gone without a window: start it once more, as a user would
            # click again (Firefox's first start in a session crashes)
            if [ -z "$again" ] && (( $(date +%s) - start >= 10 )) \
                    && ! gssh "/tmp/killapp -q $(printf '%q' "$root")"; then
                again=" (the first start exited without a window)"
                gssh "$launch" || true
            fi
            sleep 2
        done
        sleep 4  # let it draw
        screendump "$OUT/$id.png"
        shot=$OUT/$id.png
        [ "$win_ok" = y ] || note="no matching window in ${timeout}s$again; windows: $(probe windows 2>/dev/null \
            | sed -n 's/^window=[^|]*|\([^|]*|[^|]*\)|.*/\1/p' | grep -v 'Setu taskbar' | head -3 | xargs -d '\n' | cut -c1-160 || true)"
        gssh "/tmp/killapp $(printf '%q' "$root")" || true
        sleep 3
    else
        note="exe missing"
    fi
    printf '%s\t%s\t%s\t%s\t%s\n' "$id" "$exe_ok" "$win_ok" "${shot#$REPO/}" "$note" >> "$OUT/results.tsv"
    if [ "$exe_ok" = y ] && [ "$win_ok" = y ]; then
        pass "$id: exe found, $note"
    elif [ -n "$issue" ]; then
        echo "KNOWN: $id: $note (known issue: $issue)"
        known+=("$id")
    else
        echo "FAIL: $id: exe $exe_ok, window $win_ok: $note" >&2
        failed+=("$id")
    fi
done 3< <(apps_list)

echo
column -t -s $'\t' "$OUT/results.tsv"
if [ "$DOCS" = 1 ]; then
    mkdir -p "$REPO/docs/img/apps"
    for f in "$OUT"/*.png; do
        python3 -c 'import sys; from PIL import Image; im = Image.open(sys.argv[1]).convert("RGB"); im.thumbnail((640, 640)); im.quantize(128).save(sys.argv[2], optimize=True)' \
            "$f" "$REPO/docs/img/apps/$(basename "$f")"
    done
    pass "screenshots: docs/img/apps/"
fi
[ ${#known[@]} = 0 ] || echo "known issues: ${known[*]}"
[ ${#failed[@]} = 0 ] || fail "${#failed[@]} app(s) failed: ${failed[*]}"
echo "all checks passed"
