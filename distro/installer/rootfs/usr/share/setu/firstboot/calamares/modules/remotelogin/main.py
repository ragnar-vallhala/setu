# SPDX-License-Identifier: MIT OR Apache-2.0
# First-boot wizard job "remotelogin": the remote login page is a netinstall
# page (netinstall.conf) with one group, "Enable remote login (SSH)", whose
# only package is openssh-server. netinstall records the ticked groups'
# packages in GlobalStorage (packageOperations); when openssh-server is
# there, run setu-firstboot-ssh (enable and start sshd). Nothing is
# installed: the package is already on the system, so this replaces
# Calamares' packages job (which would also not run a group's pre-install
# script in Calamares 3.3).
import subprocess

import libcalamares


def pretty_name():
    return "Remote login"


def selected_packages():
    names = set()
    for op in libcalamares.globalstorage.value("packageOperations") or []:
        for key in ("install", "try_install"):
            for p in op.get(key, []):
                # a name, or a map with "package" (when there are scripts)
                names.add(p.get("package") if isinstance(p, dict) else p)
    return names


def run():
    if "openssh-server" not in selected_packages():
        libcalamares.utils.debug("remotelogin: not ticked, sshd stays disabled")
        return None
    try:
        subprocess.run(["/usr/lib/setu/setu-firstboot-ssh"], check=True)
    except (OSError, subprocess.CalledProcessError) as e:
        return ("Cannot enable remote login", str(e))
    return None
