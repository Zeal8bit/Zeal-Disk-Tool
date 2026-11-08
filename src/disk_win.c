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
