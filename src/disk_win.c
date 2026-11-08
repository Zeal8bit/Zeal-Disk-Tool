/* SPDX-FileCopyrightText: 2025 Zeal 8-bit Computer <contact@zeal8bit.com>
 * SPDX-License-Identifier: Apache-2.0 */
#include "zealfs/internal.h"
#include <string.h>

static zfs_status win_error(zfs_error *e, const char *operation) {
    DWORD code = GetLastError();
    return zfs_error_set(e, code == ERROR_ACCESS_DENIED ? ZFS_PERMISSION : ZFS_IO,
                         "%s failed (Windows error %lu)", operation, (unsigned long)code);
}
zfs_status zfs_native_open(const char *path, bool writing, zfs_native *fd, uint64_t *size, zfs_error *e) {
    /* Device paths are ASCII identifiers, not host file paths. */
    *fd = CreateFileA(path, GENERIC_READ | (writing ? GENERIC_WRITE : 0), FILE_SHARE_READ | FILE_SHARE_WRITE,
                      NULL, OPEN_EXISTING, 0, NULL);
    if (*fd == INVALID_HANDLE_VALUE)
        return win_error(e, "Open device");
    GET_LENGTH_INFORMATION length;
    DISK_GEOMETRY geometry;
    DWORD n;
    bool ok =
        DeviceIoControl(*fd, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0, &length, sizeof(length), &n, NULL) &&
        DeviceIoControl(*fd, IOCTL_DISK_GET_DRIVE_GEOMETRY, NULL, 0, &geometry, sizeof(geometry), &n, NULL);
    *size = ok ? (uint64_t)length.Length.QuadPart : 0;
    if (!ok || geometry.BytesPerSector != 512 || *size < 512 || *size > ZFS_MAX_DISK_SIZE) {
        CloseHandle(*fd);
        *fd = INVALID_HANDLE_VALUE;
        return zfs_error_set(e, ZFS_INVALID,
                             "Device must have 512-byte sectors and a size from 512 bytes to 64 GiB");
    }
    return ZFS_OK;
}
static zfs_status io(zfs_native fd, uint64_t offset, void *data, size_t size, bool writing, zfs_error *e) {
    LARGE_INTEGER pos;
    pos.QuadPart = (LONGLONG)offset;
    if (!SetFilePointerEx(fd, pos, NULL, FILE_BEGIN))
        return win_error(e, "Device seek");
    while (size) {
        DWORD n = 0, chunk = size > 1048576 ? 1048576 : (DWORD)size;
        BOOL ok = writing ? WriteFile(fd, data, chunk, &n, NULL) : ReadFile(fd, data, chunk, &n, NULL);
        if (!ok || n != chunk)
            return win_error(e, writing ? "Device write" : "Device read");
        data = (unsigned char *)data + n;
        size -= n;
    }
    return ZFS_OK;
}
zfs_status zfs_native_read(zfs_native fd, uint64_t offset, void *data, size_t size, zfs_error *e) {
    return io(fd, offset, data, size, false, e);
}
zfs_status zfs_native_write(zfs_native fd, uint64_t offset, const void *data, size_t size, zfs_error *e) {
    return io(fd, offset, (void *)data, size, true, e);
}
zfs_status zfs_native_sync(zfs_native fd, zfs_error *e) {
    return FlushFileBuffers(fd) ? ZFS_OK : win_error(e, "Device flush");
}
void zfs_native_close(zfs_native fd) {
    if (fd != INVALID_HANDLE_VALUE)
        CloseHandle(fd);
}
zfs_status zfs_discover(zfs_info *out, size_t capacity, size_t *count, zfs_error *e) {
    if (!count || (!out && capacity))
        return zfs_error_set(e, ZFS_INVALID, "Invalid discovery buffer");
    *count = 0;
    for (int i = 0; i < 128 && *count < capacity; ++i) {
        char path[64];
        snprintf(path, sizeof(path), "\\\\.\\PhysicalDrive%d", i);
        zfs_disk *d = NULL;
        if (zfs_open(path, false, &d, NULL) == ZFS_OK) {
            out[(*count)++] = *zfs_get_info(d);
            zfs_close(d);
        }
    }
    return ZFS_OK;
}

#include <winioctl.h>
#include <stddef.h>
#include <stdlib.h>
bool zfs_native_same(zfs_native a, zfs_native b) {
    STORAGE_DEVICE_NUMBER x, y;
    DWORD n;
    return DeviceIoControl(a, IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL, 0, &x, sizeof(x), &n, NULL) &&
           DeviceIoControl(b, IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL, 0, &y, sizeof(y), &n, NULL) &&
           x.DeviceType == y.DeviceType && x.DeviceNumber == y.DeviceNumber;
}
static zfs_status unmounted(HANDLE disk, zfs_error *e) {
    STORAGE_DEVICE_NUMBER device;
    DWORD n;
    if (!DeviceIoControl(disk, IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL, 0, &device, sizeof(device), &n, NULL))
        return win_error(e, "Identify device");
    if (device.PartitionNumber != 0 && device.PartitionNumber != MAXDWORD)
        return zfs_error_set(e, ZFS_INVALID, "Select a whole PhysicalDrive device, not a volume/partition");
    wchar_t volume[MAX_PATH];
    HANDLE search = FindFirstVolumeW(volume, MAX_PATH);
    if (search == INVALID_HANDLE_VALUE)
        return win_error(e, "Enumerate mounted volumes");
    zfs_status result = ZFS_OK;
    do {
        size_t len = wcslen(volume);
        if (len && volume[len - 1] == L'\\')
            volume[len - 1] = 0;
        HANDLE h = CreateFileW(volume, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (h == INVALID_HANDLE_VALUE) {
            result = win_error(e, "Inspect volume; cannot establish that device is unmounted");
            break;
        }
        union {
            VOLUME_DISK_EXTENTS alignment;
            unsigned char bytes[65536];
        } buffer;
        BOOL ok = DeviceIoControl(h, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, NULL, 0, buffer.bytes,
                                  sizeof(buffer.bytes), &n, NULL);
        DWORD code = ok ? ERROR_SUCCESS : GetLastError();
        CloseHandle(h);
        if (!ok) {
            if (code == ERROR_INVALID_FUNCTION || code == ERROR_NOT_SUPPORTED)
                continue;
            result =
                zfs_error_set(e, ZFS_BUSY, "Cannot inspect volume extents; close volume users and retry");
            break;
        }
        VOLUME_DISK_EXTENTS *extents = (VOLUME_DISK_EXTENTS *)buffer.bytes;
        if (n < offsetof(VOLUME_DISK_EXTENTS, Extents) ||
            extents->NumberOfDiskExtents >
                (n - offsetof(VOLUME_DISK_EXTENTS, Extents)) / sizeof(DISK_EXTENT)) {
            result = zfs_error_set(e, ZFS_BUSY, "Invalid volume extent information");
            break;
        }
        bool matches = false;
        for (DWORD i = 0; i < extents->NumberOfDiskExtents; ++i)
            if (extents->Extents[i].DiskNumber == device.DeviceNumber)
                matches = true;
        if (!matches)
            continue;
        if (!len) {
            result = zfs_error_set(e, ZFS_BUSY, "Invalid volume name");
            break;
        }
        volume[len - 1] = L'\\';
        wchar_t paths[32768];
        DWORD required;
        if (!GetVolumePathNamesForVolumeNameW(volume, paths, 32768, &required)) {
            result = win_error(e, "Inspect volume mount points");
            break;
        }
        if (*paths) {
            result = zfs_error_set(e, ZFS_BUSY,
                                   "Device has a mounted volume; unmount it before merging or restoring");
            break;
        }
    } while (FindNextVolumeW(search, volume, MAX_PATH));
    if (!result && GetLastError() != ERROR_NO_MORE_FILES)
        result = win_error(e, "Enumerate volumes");
    FindVolumeClose(search);
    return result;
}
zfs_status zfs_native_guard(zfs_native *fd, const char *path, bool image, bool writing, zfs_error *e) {
    if (image) {
        OVERLAPPED overlap = {0};
        return LockFileEx(*fd, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, MAXDWORD, MAXDWORD,
                          &overlap)
                   ? ZFS_OK
                   : zfs_error_set(e, ZFS_BUSY, "Image is locked by another operation");
    }
    zfs_status s = unmounted(*fd, e);
    if (s)
        return s;
    STORAGE_DEVICE_NUMBER before, after;
    DWORD n;
    if (!DeviceIoControl(*fd, IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL, 0, &before, sizeof(before), &n, NULL))
        return win_error(e, "Identify device");
    CloseHandle(*fd);
    *fd = CreateFileA(path, GENERIC_READ | (writing ? GENERIC_WRITE : 0), 0, NULL, OPEN_EXISTING, 0, NULL);
    if (*fd == INVALID_HANDLE_VALUE)
        return zfs_error_set(
            e, ZFS_BUSY,
            "Cannot exclusively open device; unmount it, close other applications, and check permissions");
    if (!DeviceIoControl(*fd, IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL, 0, &after, sizeof(after), &n, NULL) ||
        before.DeviceNumber != after.DeviceNumber || before.DeviceType != after.DeviceType)
        return zfs_error_set(e, ZFS_BUSY, "Device identity changed");
    return ZFS_OK;
}
