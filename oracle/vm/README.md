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
`snapshots`, `monitor "<HMP command>"`, `kill`, `pool` (all VMs and whether
each is busy).

The harness's revert sends `loadvm clean` to the QEMU monitor socket,
continues the VM if needed, and waits until SSH answers. It does this before
*every* test (about 10–15 s each, much faster than a reboot); `--revert-once`
reverts only once per run. See docs/ORACLE.md, "Recording on Windows".

Other people may be running tests on a VM. Anything that changes a running
VM by hand (`revert`, `snapshot`, `monitor`, `stop`, `kill`) should hold the
VM's harness lock:

```sh
cd oracle && python3 -m setu_oracle.pool locked --vm setu-oracle -- vm/vmctl.sh revert
```

## More VMs

The harness runs tests on every running VM at once (docs/ORACLE.md, "The VM
pool"). VM *N* (N ≥ 2) is a clone of VM 1's `clean` snapshot, with its state
in `oracle/vm/state-N/`, SSH on `127.0.0.1:2221+N` and VNC on
`127.0.0.1:5910+N`. Every `vmctl.sh` command takes `--vm N`.

```sh
# Copy VM 1's 'clean' snapshot. VM 1 keeps running; --dir puts the files on
# another filesystem (state-N becomes a symlink to it).
oracle/vm/vmctl.sh --vm 2 clone --dir ~/.local/share/setu-oracle/state-2

# Resume the copy from the snapshot (no boot), then check it before trusting it.
oracle/vm/vmctl.sh --vm 2 start --loadvm clean
python3 oracle/run.py build windows --verify --vm setu-oracle-2
```

How `clone` stays safe for a VM 1 that others are using: it sends VM 1 no
monitor command, never pauses or reverts it, and only *reads* its image
files, while holding VM 1's harness lock (so no test run or `savevm` changes
them meanwhile).

* The disk is copied with `qemu-img convert -U -l snapshot.name=clean`,
  which reads only the clusters of the `clean` snapshot. qcow2 never
  rewrites a cluster that a snapshot still references, so they are a fixed
  image even while VM 1 runs. The copy is zstd-compressed and gets its own
  internal `clean` snapshot.
* `vars.qcow2` (firmware variables, and the snapshot's RAM image) is copied
  whole, checked with `qemu-img check`, and its active layer reset to
  `clean` offline. The TPM state, SSH key and `known_hosts` are copied too.

The result is the very same machine state as VM 1 after a revert: same
RAM, disk, firmware variables and TPM. Starting it with `--loadvm clean`
means Windows never boots on the copy; it resumes where VM 1's snapshot
was taken.

Resources per extra VM: 4 GiB of host RAM (`SETU_VM_RAM`), and on disk
about 7.6 GiB (5.1 GiB compressed disk copy of a 10.8 GiB snapshot, plus
the 2.5 GiB RAM image), growing slowly as tests write to it. Copying took
about 5 minutes, during which VM 1's lock is held (its users wait).

**Windows licensing.** Each VM is a separate installation of Windows that
needs its own license. The Windows license terms allow one instance of the
software on one device, "whether that device is physical or virtual", and
require a separate license for each additional virtual device
([Windows 10 retail license terms](https://www.microsoft.com/content/dam/microsoft/usetm/documents/windows/10/retail-packaged/UseTerms_Retail_Windows_10_English.pdf),
"Use in a virtualized environment"). Cloning doesn't change that. The
oracle VMs aren't activated: the unattended install uses a public KMS
client setup key, which selects the edition but doesn't activate, and
`slmgr.vbs /dli` in the guest reports "License Status: Notification".
In that state Windows keeps reminding you to activate. That hasn't
affected any test so far, and the goldens are identical across clones. Licensing the VMs properly is the
owner's responsibility. Don't try to work around activation.

Clones share the machine identity of VM 1 (computer name, machine SID).
That only matters on a shared network or a domain. Each oracle VM has its
own private user-mode network (only SSH is forwarded, on the host's
loopback), so the clones never see each other. Microsoft's supported way to
deploy one image to many machines is Sysprep
(https://learn.microsoft.com/en-us/windows-hardware/manufacture/desktop/sysprep--generalize--a-windows-installation);
it's not used here, because the clones must stay bit-for-bit identical to
VM 1 for the golden files to match.

To remove VM *N*: `vmctl.sh --vm N kill`, then delete `oracle/vm/state-N`
(and its `--dir` target).

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
