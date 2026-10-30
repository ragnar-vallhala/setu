# Windows 11 oracle VM

The oracle needs one real Windows 11 machine to record **golden** results. It
runs as a local QEMU/KVM VM that the harness reaches over SSH. The VM is
reverted to a live snapshot called `clean` before every run.

Everything here is scripted by `vmctl.sh`. State (disk, firmware variables,
TPM, SSH key) lives in `oracle/vm/state/`, which git ignores.

> You must supply your own Windows 11 ISO and hold a valid Windows license
> for the VM. Setu never ships or downloads Windows media.

## Requirements (host)

| | |
|---|---|
| CPU | x86-64 with virtualization enabled (KVM); you in the `kvm` group |
| RAM | 4 GiB for the guest (`SETU_VM_RAM`, default `4G`) on top of the host's needs |
| Disk | **~40 GiB free**: the guest uses ~25–30 GiB, and each live snapshot stores RAM (~4 GiB) |
| Packages (Debian/Ubuntu/Mint) | `sudo apt install qemu-system-x86 qemu-utils ovmf swtpm swtpm-tools genisoimage` |
| Packages (Arch) | `sudo pacman -S qemu-full edk2-ovmf swtpm cdrtools` |

The VM gets UEFI + Secure Boot (OVMF), a software TPM 2.0 (swtpm), an AHCI
disk and an e1000e NIC. Windows has inbox drivers for both, so no driver ISO
is needed. SSH is forwarded **only on the host's loopback**: `127.0.0.1:2222`.

## One-time setup

```sh
# 1. Create the VM and start the unattended install (a window or VNC :5901 shows progress).
oracle/vm/vmctl.sh create --iso ~/Downloads/Win11_24H2_English_x64.iso

# 2. Wait. Setup partitions the disk, installs, reboots a few times, and on
#    first logon runs setup-guest.ps1 (OpenSSH server, SSH key, settings).
#    Typical time: 15–40 minutes.

# 3. Provision: waits for SSH, reboots without installer media, lets the guest
#    settle, then saves the live snapshot 'clean'.
oracle/vm/vmctl.sh provision

# 4. Record golden files.
python3 oracle/run.py build windows
```

What `create` does:

1. Generates an SSH key pair (`state/id_ed25519`).
2. Creates a 64 GiB thin qcow2 disk and copies the Secure Boot variable
   template to `state/vars.qcow2`. Storing it as qcow2 lets `savevm` snapshot
   it too.
3. Builds a small **seed ISO** containing `autounattend.xml`,
   `setup-guest.ps1` and the public key. Windows Setup picks up
   `autounattend.xml` from any attached drive automatically.
4. Boots the installer and presses a key at the "Press any key to boot from
   CD" prompt for you.

The unattended install creates a local administrator **`setu`** (password
`setu-oracle`, used only for auto-logon; SSH is key-only) and selects
**Windows 11 Pro**. To use another edition, change the key in
`autounattend.xml`. It's a Microsoft-published KMS client setup key, which
selects an edition but doesn't activate Windows.

## Day-to-day

```sh
oracle/vm/vmctl.sh start            # boot (headless, VNC on 127.0.0.1:5901); --gui for a window
python3 oracle/run.py windows       # reverts to 'clean', copies tests, runs, writes tests/golden/
oracle/vm/vmctl.sh stop             # ACPI shutdown (the 'clean' snapshot is kept in the disk image)
```

Other commands: `status`, `ssh [cmd]`, `revert [name]`, `snapshot [name]`,
`snapshots`, `monitor "<HMP command>"`, `kill`.

The harness's revert sends `loadvm clean` to the QEMU monitor socket,
continues the VM if needed, and waits until SSH answers. A revert takes a few
seconds, which is much faster than a reboot. To revert before *every* test,
not just once per run, set `revert_each_test = true` in `oracle/config.toml`.

**Important:** a live snapshot can only be loaded into a QEMU started with the
same device configuration. Always start the VM with `vmctl.sh start`. If you
change `vmctl.sh` devices or `SETU_VM_RAM`, retake the snapshot
(`vmctl.sh snapshot clean`) right after a fresh boot.

## Troubleshooting

* **Setup asks for something.** Watch VNC `127.0.0.1:5901` (or use `--gui`).
  Editions and ISO builds vary, so you can click through by hand if needed.
  Once you're on the desktop, run `D:\setup-guest.ps1` from an elevated
  PowerShell (`-Seed D:\`, where D: is the SETU_SEED drive).
* **`provision` waits forever for SSH.** Check `C:\setu-setup.log` in the guest.
  OpenSSH installs from Windows Update, so the guest needs network access
  (user-mode NAT works). The script falls back to the Win32-OpenSSH MSI from
  GitHub.
* **Host key changed** after reinstalling: `rm oracle/vm/state/known_hosts`.
* **Start over:** `vmctl.sh kill && rm -rf oracle/vm/state`.

## Clean-room reminder

The VM is a black-box measurement device (docs/CLEAN_ROOM.md §3). Don't
install disassemblers or debuggers on it, and don't copy files out of
`C:\Windows`. The harness only copies test programs in and reads their stdout.
