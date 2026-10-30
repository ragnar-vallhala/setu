# SPDX-License-Identifier: MIT OR Apache-2.0
"""Minimal client for the QEMU human monitor (HMP) on a unix socket.

Usage from the shell:  python3 -m setu_oracle.hmp <socket> "<command>"
"""
from __future__ import annotations

import re
import socket
import sys

_PROMPT = b"(qemu) "
_ANSI = re.compile(r"\x1b\[[0-9;]*[A-Za-z]|\x1b[()][A-Za-z0-9]")


def _read_until_prompt(s: socket.socket) -> bytes:
    buf = b""
    while not buf.endswith(_PROMPT):
        chunk = s.recv(65536)
        if not chunk:
            break
        buf += chunk
    return buf


def command(path: str, cmd: str, timeout: float = 600) -> str:
    """Run one HMP command and return its output (echo and prompt stripped)."""
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as s:
        s.settimeout(timeout)
        s.connect(path)
        _read_until_prompt(s)  # banner
        s.sendall(cmd.encode() + b"\n")
        out = _read_until_prompt(s).decode(errors="replace")
    out = _ANSI.sub("", out)
    if out.endswith(_PROMPT.decode()):
        out = out[: -len(_PROMPT)]
    lines = out.replace("\r", "").split("\n")
    if lines and lines[0].strip().endswith(cmd.strip()):  # the echoed command line
        lines = lines[1:]
    return "\n".join(lines).strip()


def failed(output: str) -> bool:
    return bool(re.search(r"\berror\b|could not|not found|doesn't exist|does not exist", output, re.I))


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    out = command(argv[1], argv[2])
    if out:
        print(out)
    return 1 if failed(out) else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
