# SPDX-License-Identifier: MIT OR Apache-2.0
"""The pool of Windows oracle VMs, and one lock per VM.

Which VMs exist:

* `[windows] vms = [...]` in oracle/config.toml lists them explicitly. Each
  entry overrides keys of `[windows]` (name, host, port, monitor, identity,
  known_hosts, snapshot, lock, extra_locks).
* A config file that sets `[windows] port`, `host` or `monitor` but no `vms`
  describes a single VM (the old, pre-pool form).
* Otherwise the VMs are discovered: `oracle/vm/state` (the first VM,
  "setu-oracle", SSH port 2222) plus every `oracle/vm/state-<N>` directory
  that `vmctl.sh --vm <N>` created. Each state directory may hold a
  `vm.json` with {"name", "ssh_port", "extra_locks"}. VMs whose QEMU is not
  running are left out.

Locking: a VM is used only while its lock file (`<state>/oracle.lock`, or
`lock` in the config) *and* all of its `extra_locks` are held with flock(2).
`extra_locks` keeps older ways of serialising a VM working: the first VM
lists the session-wide lock file that `flock <file> oracle/run.py windows`
users hold, so a harness run never shares a VM with such a run.
"""
from __future__ import annotations

import fcntl
import json
import os
import subprocess
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path

from . import config as C


@dataclass
class VM:
    name: str
    settings: dict  # the [windows] section with this VM's overrides applied
    locks: list[str] = field(default_factory=list)
    state: Path | None = None

    def __str__(self) -> str:
        return self.name


def _alive(state: Path) -> bool:
    try:
        pid = int((state / "qemu.pid").read_text().strip())
        os.kill(pid, 0)
    except (OSError, ValueError):
        return False
    return (state / "monitor.sock").exists()


def _from_state(w: dict, state: Path, primary: bool) -> VM | None:
    info: dict = {}
    if (state / "vm.json").exists():
        info = json.loads((state / "vm.json").read_text())
    port = info.get("ssh_port") or (w["port"] if primary else None)
    if port is None:
        return None  # not set up (yet): vmctl.sh writes vm.json
    s = dict(w)
    s["port"] = int(port)
    s["monitor"] = str(state / "monitor.sock")
    if (state / "id_ed25519").exists():
        s["identity"] = str(state / "id_ed25519")
    s["known_hosts"] = str(state / "known_hosts")
    name = info.get("name") or ("setu-oracle" if primary else f"setu-oracle-{state.name.split('-', 1)[-1]}")
    locks = [str(state / "oracle.lock")] + [C.repo_path(p) for p in info.get("extra_locks", [])]
    return VM(name, s, locks, state)


def discover(cfg: C.Config, only: list[str] | None = None) -> list[VM]:
    w = {k: v for k, v in cfg["windows"].items() if k not in ("vms",)}
    user_w = (cfg.user or {}).get("windows", {})
    vms: list[VM] = []
    if cfg["windows"]["vms"]:
        for i, entry in enumerate(cfg["windows"]["vms"]):
            s = dict(w)
            s.update(entry)
            for key in ("monitor", "identity", "known_hosts"):
                if s.get(key):
                    s[key] = C.repo_path(s[key])
            name = s.get("name") or f"vm{i + 1}"
            lock = s.get("lock") or str(Path(s["monitor"]).parent / "oracle.lock")
            locks = [C.repo_path(lock)] + [C.repo_path(p) for p in s.get("extra_locks", [])]
            vms.append(VM(name, s, locks))
    elif {"port", "host", "monitor"} & set(user_w):
        lock = w.get("lock") or str(Path(w["monitor"]).parent / "oracle.lock")
        locks = [C.repo_path(lock)] + [C.repo_path(p) for p in w.get("extra_locks", [])]
        vms.append(VM(w.get("name") or "setu-oracle", dict(w), locks))
    else:
        root = C.VM_STATE
        states = [root] + sorted((d for d in root.parent.glob("state-*") if d.is_dir()),
                                 key=lambda d: (len(d.name), d.name))
        found = [vm for i, d in enumerate(states) if (vm := _from_state(w, d, i == 0))]
        running = [vm for vm in found if vm.state and _alive(vm.state)]
        # Nothing running: keep the first VM so the run fails with a useful message.
        vms = running or found[:1]
    if only:
        names = {v.name for v in vms}
        missing = [n for n in only if n not in names]
        if missing:
            raise SystemExit(f"windows: no running VM named {', '.join(missing)} "
                             f"(pool: {', '.join(sorted(names)) or 'empty'})")
        vms = [v for v in vms if v.name in only]
    return vms


class Lease:
    """flock(2) on all of a VM's lock files, all or nothing, never blocking."""

    def __init__(self, vm: VM):
        self.vm = vm
        self.fds: list[int] = []

    def try_acquire(self) -> bool:
        assert not self.fds
        for path in self.vm.locks:
            Path(path).parent.mkdir(parents=True, exist_ok=True)
            fd = os.open(path, os.O_RDONLY | os.O_CREAT, 0o666)
            try:
                fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except BlockingIOError:
                os.close(fd)
                self.release()
                return False
            self.fds.append(fd)
        return True

    def release(self) -> None:
        for fd in self.fds:
            fcntl.flock(fd, fcntl.LOCK_UN)
            os.close(fd)
        self.fds = []

    @property
    def held(self) -> bool:
        return bool(self.fds)

    def acquire(self, timeout: float | None = None, poll: float = 0.5) -> bool:
        deadline = None if timeout is None else time.time() + timeout
        while not self.try_acquire():
            if deadline is not None and time.time() > deadline:
                return False
            time.sleep(poll)
        return True


def main(argv: list[str]) -> int:
    """python3 -m setu_oracle.pool list
       python3 -m setu_oracle.pool locked [--vm NAME] -- COMMAND ...

    `locked` waits until the VM (default: the first one) is free, holds its
    locks while COMMAND runs, and passes the VM's details in the environment:
    SETU_POOL_VM_NAME, SETU_POOL_VM_SSH_PORT, SETU_POOL_VM_MONITOR, SETU_POOL_VM_STATE
    (not SETU_VM_*: those configure vmctl.sh).
    """
    cfg = C.load(os.environ.get("SETU_ORACLE_CONFIG"))
    if argv[:1] == ["list"]:
        for vm in discover(cfg):
            free = Lease(vm)
            state = "free" if free.try_acquire() else "busy"
            free.release()
            print(f"{vm.name:16s} ssh 127.0.0.1:{vm.settings['port']:<5d} {state:4s}  "
                  f"monitor {vm.settings['monitor']}  locks {' + '.join(vm.locks)}")
        return 0
    if argv[:1] == ["locked"] and "--" in argv:
        opts, cmd = argv[1:argv.index("--")], argv[argv.index("--") + 1:]
        only = [opts[opts.index("--vm") + 1]] if "--vm" in opts else None
        vm = discover(cfg, only)[0]
        lease = Lease(vm)
        if not lease.try_acquire():
            print(f"pool: waiting for {vm.name} ...", file=sys.stderr)
            lease.acquire()
        try:
            env = dict(os.environ, SETU_POOL_VM_NAME=vm.name, SETU_POOL_VM_SSH_PORT=str(vm.settings["port"]),
                       SETU_POOL_VM_MONITOR=vm.settings["monitor"], SETU_POOL_VM_STATE=str(vm.state or ""))
            return subprocess.run(cmd, env=env).returncode
        finally:
            lease.release()
    print(main.__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
