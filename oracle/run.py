#!/usr/bin/env python3
# SPDX-License-Identifier: MIT OR Apache-2.0
"""Setu oracle harness entry point. See docs/ORACLE.md, or run with --help."""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from setu_oracle.cli import main  # noqa: E402

if __name__ == "__main__":
    sys.exit(main())
