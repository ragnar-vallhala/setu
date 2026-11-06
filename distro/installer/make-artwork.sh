#!/bin/bash
# SPDX-License-Identifier: MIT OR Apache-2.0
# Regenerate the installer's Calamares artwork and the boot splash images
# (Setu's own, drawn from code in the docs/DESIGN.md colours with Inter:
# distro/artwork/branding.py). The PNGs are committed; run this after
# changing the design. Needs python3-numpy, python3-pil and fonts-inter.
#   distro/installer/make-artwork.sh
set -euo pipefail
exec python3 "$(cd "$(dirname "$0")/.." && pwd)/artwork/branding.py"
