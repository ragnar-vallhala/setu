# services/fs/: the C: volume and drive letters

License: MIT OR Apache-2.0. Design: docs/ARCHITECTURE.md §3.3
("Filesystem semantics"). Installed into the dev image by
`distro/build-image.sh` (`make -C services/fs install DESTDIR=...`).

## C: is an ext4 casefold volume

The image has a third GPT partition, `setu-c` (label `SETU-C`), made with
`mkfs.ext4 -O casefold -E encoding=utf8` and mounted at
**`/var/lib/setu/volumes/c`**. Its root directory has the casefold flag
(`chattr +F`, shown as `F` by `lsattr -d`), which every directory created
under it inherits: the kernel looks names up without regard to case and
keeps the case they were created with. `lost+found` carries
`user.DOSATTRIB=0x16` (hidden, system), so Windows programs treat it like
"System Volume Information".

The personality needs no change for this. When a name isn't found as given,
Wine's ntdll checks the directory's `EXT4_CASEFOLD_FL` (`get_dir_case_sensitivity`)
and skips its own case-insensitive directory scan; and a name in the wrong
case is found by the first `stat()` anyway. Wine's scan remains the fallback
on every other file system (USB sticks, network shares, Z:).

Each prefix links to the volume: `dosdevices/c:` → `/var/lib/setu/volumes/c`
(the image build does it for the `setu` user before `wineboot -i`;
`setu-session` does it for a new prefix). Until NT ACLs exist, the volume
belongs to that one user.

## Drive letters: `/run/setu/dosdevices`

The per-system drive table is the directory **`/run/setu/dosdevices`**, in
the format of Wine's `dosdevices`: `x:` is a symlink to the directory behind
drive X:, and `x::` a symlink to its block device.

* Fixed letters come from `/etc/setu/drives.conf` (`drives.conf` here):
  `C:` is the system volume; `Z:` is reserved (each prefix's view of the Unix
  root). `setu-drives.service` writes them at boot.
* Every other block device with a file system gets the first free letter
  from `D:` when it appears and loses it when it goes away. udev
  (`71-setu-drives.rules`) tags the device for systemd with
  `SYSTEMD_WANTS=setu-volume@<kernel name>.service`; that unit runs
  `setu-drives add` (mount under `/run/setu/volumes/<kernel name>`, take a
  letter), and since it `BindsTo` the device, systemd stops it when the
  device disappears (`setu-drives remove`: drop the letter, unmount). The EFI
  system partition and Setu's own partitions (`setu-*` partition names) get
  no letter. FAT, exFAT and NTFS are mounted owned by the owner of C:.
* The rule also tags the device `uaccess`, so the seat's user can read it:
  the personality's mount manager reads the volume label, serial number
  and file system type from the device.

`setu-drives list` prints the table.

### In the session

A running personality learns about changes from **`setu-drives-sync`**,
which `setu-session` starts in the background. Once a second it compares
D:–Y: in the table with the prefix's `dosdevices` (which the personality's
mount manager rewrites on every change) and sends each difference to the
mount manager with **`setu-mountctl.exe`** (`define`/`remove`, the
personality's `IOCTL_MOUNTMGR_DEFINE_UNIX_DRIVE`, as winecfg uses). The
mount manager then creates or deletes the drive, so `GetLogicalDrives`,
`GetDriveType`, `GetVolumeInformation` and `dir D:` see it. Drive types:
`removable` when the device (or its disk) reports removable media,
`cdrom` for `sr*`, else `fixed`.

`setu-mountctl.exe list` prints what Windows programs see:

```
C: type=fixed fs=NTFS label=
D: type=removable fs=FAT label=SETUSTICK
Z: type=fixed fs=NTFS label=
```

## Test

`distro/tests/drives-smoke.sh` boots the image and checks both parts,
hot-plugging USB disks through the QEMU monitor (see `distro/README.md`).

## Not yet

* The table lives in `/run`, so letters of removable volumes aren't
  remembered across plug-ins or boots (Windows keeps them in
  `HKLM\SYSTEM\MountedDevices`).
* One Windows user: C: and FAT mounts belong to the owner of C:.
* `setu-drives-sync` belongs in `setu-system.service` once that exists
  (§3.3, Service Control Manager); it polls instead of using inotify.
* Volume serial and label of C: come from the personality's defaults, not
  from the ext4 superblock (the user can't read the partition device).
