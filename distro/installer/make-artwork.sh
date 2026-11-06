#!/bin/bash
# SPDX-License-Identifier: MIT OR Apache-2.0
# Regenerate the installer's Calamares artwork (Setu's own: text and plain
# shapes in the Setu colours, DejaVu Sans) with ImageMagick. The PNGs are
# committed; run this after changing the design.
#   distro/installer/make-artwork.sh
set -euo pipefail
B=$(cd "$(dirname "$0")" && pwd)/rootfs/usr/share/calamares/branding/setu
TEAL='#0f4c5c' DEEP='#0a2f3a' SAFFRON='#f2a541' LIGHT='#f4f1ea'
FONT=DejaVu-Sans FONTB=DejaVu-Sans-Bold

# the mark: a bridge (a saffron arch over a deck) on a teal rounded square
mark() { # SIZE OUT
    local s=$1 k
    k=$(awk "BEGIN{print $1/128}")
    convert -size "${s}x${s}" xc:none \
        -fill "$TEAL" -draw "roundrectangle 0,0 $((s-1)),$((s-1)) $((s/6)),$((s/6))" \
        -fill none -stroke "$SAFFRON" -strokewidth "$(awk "BEGIN{print 9*$k}")" \
        -draw "arc $(awk "BEGIN{print 22*$k}"),$(awk "BEGIN{print 40*$k}") $(awk "BEGIN{print 106*$k}"),$(awk "BEGIN{print 124*$k}") 180,360" \
        -stroke "$LIGHT" -strokewidth "$(awk "BEGIN{print 7*$k}")" \
        -draw "line $(awk "BEGIN{print 14*$k}"),$(awk "BEGIN{print 84*$k}") $(awk "BEGIN{print 114*$k}"),$(awk "BEGIN{print 84*$k}")" \
        -draw "line $(awk "BEGIN{print 40*$k}"),$(awk "BEGIN{print 54*$k}") $(awk "BEGIN{print 40*$k}"),$(awk "BEGIN{print 84*$k}")" \
        -draw "line $(awk "BEGIN{print 64*$k}"),$(awk "BEGIN{print 46*$k}") $(awk "BEGIN{print 64*$k}"),$(awk "BEGIN{print 84*$k}")" \
        -draw "line $(awk "BEGIN{print 88*$k}"),$(awk "BEGIN{print 54*$k}") $(awk "BEGIN{print 88*$k}"),$(awk "BEGIN{print 84*$k}")" \
        -strip "$2"
}
mark 128 "$B/setu-logo.png"

# welcome banner: the mark and the name
convert -size 600x180 xc:none \
    \( "$B/setu-logo.png" -resize 140x140 \) -geometry +10+20 -composite \
    -font "$FONTB" -pointsize 96 -fill "$TEAL" -annotate +180+128 'Setu' \
    -font "$FONT" -pointsize 20 -fill "$DEEP" -annotate +186+162 'Windows programs on a Linux desktop' \
    -strip "$B/setu-welcome.png"

# the slide shown while installing
convert -size 800x440 "xc:$TEAL" \
    \( "$B/setu-logo.png" -resize 120x120 \) -geometry +60+60 -composite \
    -font "$FONTB" -pointsize 56 -fill "$LIGHT" -annotate +210+130 'Installing Setu' \
    -font "$FONT" -pointsize 24 -fill "$LIGHT" \
    -annotate +62+250 'Setu runs Windows programs on a stock Linux kernel.' \
    -annotate +62+290 'Your C: drive is its own case-insensitive volume.' \
    -fill "$SAFFRON" -annotate +62+360 'After the restart, Setu asks for your language,' \
    -annotate +62+394 'keyboard, time zone, account and computer name.' \
    -strip "$B/setu-slide.png"
ls -l "$B"/*.png
