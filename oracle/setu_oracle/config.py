# SPDX-License-Identifier: MIT OR Apache-2.0
"""Configuration: built-in defaults, overridden by oracle/config.toml, then env vars."""
from __future__ import annotations

import copy
import os
import tomllib
from dataclasses import dataclass
import subprocess
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
ORACLE_DIR = REPO / "oracle"
TESTS_DIR = REPO / "tests" / "win32"
GOLDEN_DIR = REPO / "tests" / "golden"
EXPECTATIONS = REPO / "tests" / "expectations.json"
BUILD_DIR = REPO / "build"


def _vm_state() -> Path:
    """oracle/vm/state, or the main checkout's when running from a git worktree.

    The VM (disk, SSH key, monitor socket) exists once per machine, so a
    worktree without its own state uses the main checkout's.
    """
    own = ORACLE_DIR / "vm" / "state"
    if own.exists():
        return own
    try:
        common = subprocess.run(["git", "-C", str(REPO), "rev-parse", "--path-format=absolute",
                                 "--git-common-dir"], capture_output=True, text=True, check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return own
    main = Path(common).parent / "oracle" / "vm" / "state"
    return main if main.exists() else own


VM_STATE = _vm_state()

DEFAULTS: dict = {
    "build": {
        "cc": "x86_64-w64-mingw32-gcc",
        "cflags": ["-std=c11", "-O1", "-Wall", "-Wextra", "-Werror"],
        "ldflags": ["-static", "-ladvapi32"],
    },
    "run": {
        "timeout": 60,
    },
    "setu": {
        # Which build runs PEs on the Setu side:
        #   "auto"        - the personality/ build if it exists, else stock Wine
        #   "personality" - build/personality/install (tools/build-personality.sh)
        #   "stock"       - stock Wine from PATH
        # An explicit `wine` command (or $SETU_WINE) overrides all of these.
        "implementation": "auto",
        "wine": "",
        "wineserver": "",
        # "" = oracle/.cache/wineprefix-<implementation>, so builds never share
        # (and upgrade or downgrade) each other's prefix.
        "prefix": "",
        "winedebug": "-all",
    },
    "windows": {
        "host": "127.0.0.1",
        "port": 2222,
        "user": "setu",
        "identity": str(VM_STATE / "id_ed25519"),
        "known_hosts": str(VM_STATE / "known_hosts"),
        "remote_dir": "C:\\setu-oracle",
        # How to get back to a clean machine before a run:
        #   "loadvm"  - QEMU live snapshot via the HMP monitor socket (default)
        #   "command" - run `revert_command` (a shell command) on the host
        #   "none"    - don't revert (useful while debugging a test)
        "revert": "loadvm",
        "snapshot": "clean",
        "monitor": str(VM_STATE / "monitor.sock"),
        "revert_command": "",
        # Revert before every test run (each repeat included), so a test that
        # leaves files, registry keys or processes behind can't affect the
        # next one. `--revert-once` turns it off for one invocation.
        "revert_each_test": True,
        "ssh_ready_timeout": 180,
        # The VM pool (see setu_oracle/pool.py). Empty = discover the VMs
        # from oracle/vm/state and oracle/vm/state-*/ (vmctl.sh --vm N).
        # Each entry is a table that overrides keys of [windows] for one VM:
        #   name, host, port, monitor, identity, known_hosts, snapshot,
        #   lock (lock file path), extra_locks (more lock files to hold).
        "vms": [],
        # Give up if no VM of the pool is free after this many seconds.
        "lock_wait": 7200,
    },
}


def _merge(base: dict, over: dict) -> dict:
    out = copy.deepcopy(base)
    for k, v in over.items():
        if isinstance(v, dict) and isinstance(out.get(k), dict):
            out[k] = _merge(out[k], v)
        else:
            out[k] = v
    return out


@dataclass
class Config:
    data: dict
    path: Path | None
    user: dict | None = None  # the config file's own content, before defaults

    def __getitem__(self, section: str) -> dict:
        return self.data[section]


def load(path: str | None = None) -> Config:
    p = Path(path) if path else ORACLE_DIR / "config.toml"
    data = DEFAULTS
    used = None
    user: dict = {}
    if p.exists():
        with open(p, "rb") as f:
            user = tomllib.load(f)
        data = _merge(DEFAULTS, user)
        used = p
    elif path:
        raise SystemExit(f"config file not found: {p}")
    data = copy.deepcopy(data)
    # A few env overrides that are handy in CI and one-off runs.
    env = os.environ
    if env.get("SETU_CC"):
        data["build"]["cc"] = env["SETU_CC"]
    if env.get("SETU_WINE"):
        data["setu"]["wine"] = env["SETU_WINE"]
    if env.get("SETU_WINESERVER"):
        data["setu"]["wineserver"] = env["SETU_WINESERVER"]
    if env.get("SETU_IMPL"):
        data["setu"]["implementation"] = env["SETU_IMPL"]
    if env.get("SETU_WINEPREFIX"):
        data["setu"]["prefix"] = env["SETU_WINEPREFIX"]
    # Relative paths in the config are relative to the repository root.
    for section, key in (("setu", "prefix"), ("windows", "identity"), ("windows", "known_hosts"),
                         ("windows", "monitor")):
        v = data[section][key]
        if v and not os.path.isabs(os.path.expanduser(v)):
            data[section][key] = str(REPO / v)
        elif v:
            data[section][key] = os.path.expanduser(v)
    return Config(data, used, user)


def repo_path(v: str) -> str:
    """A config path: ~ expanded, relative paths taken from the repository root."""
    v = os.path.expanduser(v)
    return v if os.path.isabs(v) else str(REPO / v)
