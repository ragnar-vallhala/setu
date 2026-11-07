#!/usr/bin/env python3
# SPDX-License-Identifier: MIT OR Apache-2.0
#
# Regenerate the Wine fork's built-in control theme,
# personality/wine/dlls/setu.msstyles (setu-light's tokens), byte for byte:
#
#   tools/setu-theme/make-setu-msstyles.py [--no-bmp]
#
# The generator is make-msstyles.py next to this script, which also builds a
# theme pack's <id>.msstyles from its theme.ini (docs/THEMES.md).
import os
import subprocess
import sys

here = os.path.dirname(os.path.abspath(__file__))
sys.exit(subprocess.call([sys.executable, os.path.join(here, "make-msstyles.py"), "--fork-dir"]
                         + sys.argv[1:]))
