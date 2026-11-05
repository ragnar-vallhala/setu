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

## NT security descriptors: `security.NTACL` and `setu-ntacld`

A file's NT security descriptor is the xattr **`security.NTACL`**: one
format byte (`1`) and the self-relative descriptor ([MS-DTYP] 2.4.6). The
wineserver reads it itself (reading `security.*` needs no privilege) and
enforces it on open (`personality/wine/server/filesec.c`). Writing it needs
`CAP_SYS_ADMIN`, which only **`setu-ntacld`** has:

* `setu-ntacld.socket` listens on `/run/setu/ntacl.sock` (`SOCK_SEQPACKET`,
  any local user may connect) and starts `setu-ntacld.service` on demand; the
  helper exits after a minute without clients.
* A request is a header and the new value, with the open file passed as an
  `SCM_RIGHTS` descriptor; the reply is an errno value
  (`setu-ntacl.h`). The helper takes no paths and opens no files.
* It writes or removes only `security.NTACL`, only on a regular file or
  directory owned by the caller's uid, only on a file system mounted under
  `/var/lib/setu/volumes/` or `/run/setu/volumes/` (otherwise `EXDEV`), and
  only a well-formed format-1 value (otherwise `EINVAL`).
* The unit runs it as a dynamic user with `CAP_SYS_ADMIN` as its only
  capability (bounding and ambient set) and a strict sandbox: no network, no
  writable file system, a system call filter.

Where the helper is missing (developer machines, CI) or answers `EXDEV` or
`ENOTSUP`, the wineserver writes the same value to `user.setu.sd` instead.
`SETU_NTACL_SOCKET` in the wineserver's environment points it at another
socket (for tests).

## Test

`distro/tests/drives-smoke.sh` boots the image and checks both parts,
hot-plugging USB disks through the QEMU monitor (see `distro/README.md`).
`distro/tests/ntacl-smoke.sh` checks security descriptors: a DACL set from
Windows lands in `security.NTACL`, is enforced, can't be removed by the
user, survives a wineserver restart, and a file outside the Setu volumes gets
the `user.setu.sd` fallback.

## Not yet

* The table lives in `/run`, so letters of removable volumes aren't
  remembered across plug-ins or boots (Windows keeps them in
  `HKLM\SYSTEM\MountedDevices`).
* One Windows user: C: and FAT mounts belong to the owner of C:, and
  setu-ntacld only writes descriptors of files the caller owns.
* `setu-drives-sync` belongs in `setu-system.service` once that exists
  (§3.3, Service Control Manager); it polls instead of using inotify.
* Volume serial and label of C: come from the personality's defaults, not
  from the ext4 superblock (the user can't read the partition device).
