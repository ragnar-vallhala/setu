# distro/: bootable Setu image

**Status: stub.** License: MIT OR Apache-2.0 (the image itself aggregates
many packages under their own licenses).

Plan (docs/ARCHITECTURE.md §3.5):

* **Base:** Debian stable (proposal), stock LTS kernel, built with `mmdebstrap`
  for a reproducible root filesystem.
* **Image:** hybrid UEFI ISO, Secure Boot via shim.
* **Installer:** Calamares. It creates the ext4 **casefold** system volume that
  backs `C:` and the first user account.
* **First boot:** locale/keyboard/network/user wizard, then
  `setu-system.service` and the shell session.

Milestone 1 target: a QEMU-bootable dev image that starts the shell stub.
