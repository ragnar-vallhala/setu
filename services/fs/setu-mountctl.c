/* SPDX-License-Identifier: MIT OR Apache-2.0
 *
 * setu-mountctl.exe: tell the personality's mount manager (mountmgr.sys)
 * about a drive letter, and list the drives Windows programs see.
 *
 *   setu-mountctl define LETTER TYPE UNIX_MOUNT_POINT [UNIX_DEVICE]
 *       TYPE: fixed, removable, cdrom or remote
 *   setu-mountctl remove LETTER
 *   setu-mountctl list
 *
 * define/remove use the personality's private mount manager request
 * IOCTL_MOUNTMGR_DEFINE_UNIX_DRIVE (the one winecfg uses; layout from Wine's
 * include/ddk/mountmgr.h). mountmgr then creates or deletes the DOS device,
 * keeps $WINEPREFIX/dosdevices in step and reads the label, serial number and
 * file system from the device. setu-drives-sync runs this for each change in
 * the system drive table (/run/setu/dosdevices, see services/fs/README.md).
 *
 * list prints one line per drive in GetLogicalDrives():
 *   X: type=<GetDriveType> fs=<file system> label=<volume label>
 * Documented APIs: GetLogicalDrives, GetDriveTypeW, GetVolumeInformationW
 * (learn.microsoft.com/windows/win32/api/fileapi/).
 */
#include <windows.h>
#include <winioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MOUNTMGRCONTROLTYPE 0x0000006D
#define IOCTL_MOUNTMGR_DEFINE_UNIX_DRIVE \
    CTL_CODE(MOUNTMGRCONTROLTYPE, 32, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)

struct mountmgr_unix_drive
{
    ULONG     size;
    ULONG     type;
    ULONG     fs_type;
    DWORD     serial;
    ULONGLONG unix_dev;
    WCHAR     letter;
    USHORT    mount_point_offset;
    USHORT    device_offset;
    USHORT    label_offset;
};

static int usage(void)
{
    fprintf(stderr, "usage: setu-mountctl define LETTER fixed|removable|cdrom|remote MOUNT_POINT [DEVICE]\n"
                    "       setu-mountctl remove LETTER\n"
                    "       setu-mountctl list\n");
    return 2;
}

static HANDLE open_mountmgr(void)
{
    HANDLE h = INVALID_HANDLE_VALUE;
    int i;

    /* mountmgr.sys starts with the personality's boot; wait for it */
    for (i = 0; i < 100; i++)
    {
        h = CreateFileW(L"\\\\.\\MountPointManager", GENERIC_READ | GENERIC_WRITE,
                        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, 0);
        if (h != INVALID_HANDLE_VALUE) break;
        Sleep(100);
    }
    if (h == INVALID_HANDLE_VALUE)
        fprintf(stderr, "setu-mountctl: cannot open the mount manager (error %lu)\n", GetLastError());
    return h;
}

static int parse_letter(const char *s)
{
    char c = s[0] | 0x20;
    if (c < 'a' || c > 'z' || (s[1] && (s[1] != ':' || s[2]))) return 0;
    return c;
}

static int define(int letter, ULONG type, const char *mount_point, const char *device)
{
    size_t len = sizeof(struct mountmgr_unix_drive);
    struct mountmgr_unix_drive *req;
    HANDLE mgr;
    char *p;
    BOOL ok;

    if (mount_point) len += strlen(mount_point) + 1;
    if (device) len += strlen(device) + 1;
    if (len > 0xffff || !(req = calloc(1, len))) return 1;
    req->size = len;
    req->type = type;
    req->letter = letter;
    p = (char *)(req + 1);
    if (mount_point)
    {
        strcpy(p, mount_point);
        req->mount_point_offset = p - (char *)req;
        p += strlen(p) + 1;
    }
    if (device)
    {
        strcpy(p, device);
        req->device_offset = p - (char *)req;
    }

    if ((mgr = open_mountmgr()) == INVALID_HANDLE_VALUE) { free(req); return 1; }
    ok = DeviceIoControl(mgr, IOCTL_MOUNTMGR_DEFINE_UNIX_DRIVE, req, len, NULL, 0, NULL, NULL);
    if (!ok) fprintf(stderr, "setu-mountctl: %c: request failed (error %lu)\n", letter, GetLastError());
    CloseHandle(mgr);
    free(req);
    return ok ? 0 : 1;
}

static const char *type_name(UINT type)
{
    switch (type)
    {
    case DRIVE_NO_ROOT_DIR: return "no_root_dir";
    case DRIVE_REMOVABLE:   return "removable";
    case DRIVE_FIXED:       return "fixed";
    case DRIVE_REMOTE:      return "remote";
    case DRIVE_CDROM:       return "cdrom";
    case DRIVE_RAMDISK:     return "ramdisk";
    default:                return "unknown";
    }
}

static int list(void)
{
    DWORD drives = GetLogicalDrives();
    int i;

    for (i = 0; i < 26; i++)
    {
        WCHAR root[] = L"A:\\", label[MAX_PATH + 1], fs[MAX_PATH + 1];

        if (!(drives & (1u << i))) continue;
        root[0] += i;
        if (!GetVolumeInformationW(root, label, ARRAYSIZE(label), NULL, NULL, NULL, fs, ARRAYSIZE(fs)))
            label[0] = fs[0] = 0;
        printf("%c: type=%s fs=%ls label=%ls\n", 'A' + i, type_name(GetDriveTypeW(root)), fs, label);
    }
    return 0;
}

int main(int argc, char **argv)
{
    static const struct { const char *name; ULONG type; } types[] =
    {
        { "fixed", DRIVE_FIXED }, { "removable", DRIVE_REMOVABLE },
        { "cdrom", DRIVE_CDROM }, { "remote", DRIVE_REMOTE },
    };
    int letter;
    size_t i;

    if (argc == 2 && !strcmp(argv[1], "list")) return list();
    if (argc == 3 && !strcmp(argv[1], "remove"))
    {
        if (!(letter = parse_letter(argv[2]))) return usage();
        return define(letter, DRIVE_NO_ROOT_DIR, NULL, NULL);
    }
    if ((argc == 5 || argc == 6) && !strcmp(argv[1], "define"))
    {
        if (!(letter = parse_letter(argv[2]))) return usage();
        for (i = 0; i < ARRAYSIZE(types); i++)
            if (!strcmp(argv[3], types[i].name))
                return define(letter, types[i].type, argv[4], argc == 6 ? argv[5] : NULL);
    }
    return usage();
}
