/**
 * SPDX-FileCopyrightText: 2025 Zeal 8-bit Computer <contact@zeal8bit.com>
 * SPDX-FileCopyrightText: 2026 David Higgins <zoul0813@me.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "internal.h"
#include <string.h>
#include <winioctl.h>
#include <stddef.h>
#include <stdlib.h>

/* Bound each synchronous device I/O request; this is not the sector size. */
#define DEVICE_IO_CHUNK_SIZE (1024 * 1024)
/* Scratch storage for the variable-length Windows volume extent query. */
#define VOLUME_EXTENTS_BUFFER_SIZE (64 * 1024)

static zfs_status win_error(zfs_error* error, const char* operation)
{
    DWORD code = GetLastError();
    return zfs_error_set(error,
                         code == ERROR_ACCESS_DENIED ? ZFS_PERMISSION : ZFS_IO,
                         "%s failed (Windows error %lu)",
                         operation,
                         (unsigned long) code);
}

zfs_status zfs_native_open(const char* path, bool writing, zfs_native* fd, uint64_t* size, zfs_error* error)
{
    /* Device paths are ASCII identifiers, not host file paths. */
    *fd = CreateFileA(path,
                      GENERIC_READ | (writing ? GENERIC_WRITE : 0),
                      FILE_SHARE_READ | FILE_SHARE_WRITE,
                      NULL,
                      OPEN_EXISTING,
                      0,
                      NULL);
    if (*fd == INVALID_HANDLE_VALUE) {
        return win_error(error, "Open device");
    }
    /* Get the size of the disk, make sure it is not bigger than expected */
    GET_LENGTH_INFORMATION length;
    DISK_GEOMETRY geometry;
    DWORD bytes_returned;
    bool ok =
        DeviceIoControl(*fd, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0, &length, sizeof(length), &bytes_returned, NULL) !=
            0 &&
        DeviceIoControl(*fd,
                        IOCTL_DISK_GET_DRIVE_GEOMETRY,
                        NULL,
                        0,
                        &geometry,
                        sizeof(geometry),
                        &bytes_returned,
                        NULL) != 0;
    *size = ok ? (uint64_t) length.Length.QuadPart : 0;
    if (ok == 0 || geometry.BytesPerSector != DISK_SECTOR_SIZE || *size < DISK_SECTOR_SIZE ||
        *size > ZFS_MAX_DISK_SIZE) {
        CloseHandle(*fd);
        *fd = INVALID_HANDLE_VALUE;
        return zfs_error_set(error,
                             ZFS_INVALID,
                             "Device must have 512-byte sectors and a size from 512 bytes to 64 GiB");
    }
    return ZFS_OK;
}

static zfs_status io(zfs_native fd, uint64_t offset, void* data, size_t size, bool writing, zfs_error* error)
{
    LARGE_INTEGER pos;
    pos.QuadPart = (LONGLONG) offset;
    if (SetFilePointerEx(fd, pos, NULL, FILE_BEGIN) == 0) {
        return win_error(error, "Device seek");
    }
    while (size != 0) {
        DWORD bytes_transferred = 0;
        DWORD chunk = size > DEVICE_IO_CHUNK_SIZE ? DEVICE_IO_CHUNK_SIZE : (DWORD) size;
        BOOL ok = writing ? WriteFile(fd, data, chunk, &bytes_transferred, NULL)
                          : ReadFile(fd, data, chunk, &bytes_transferred, NULL);
        if (ok == 0 || bytes_transferred != chunk) {
            return win_error(error, writing ? "Device write" : "Device read");
        }
        data = (unsigned char*) data + bytes_transferred;
        size -= bytes_transferred;
    }
    return ZFS_OK;
}

zfs_status zfs_native_read(zfs_native fd, uint64_t offset, void* data, size_t size, zfs_error* error)
{
    return io(fd, offset, data, size, false, error);
}

zfs_status zfs_native_write(zfs_native fd, uint64_t offset, const void* data, size_t size, zfs_error* error)
{
    return io(fd, offset, (void*) data, size, true, error);
}

zfs_status zfs_native_sync(zfs_native fd, zfs_error* error)
{
    return FlushFileBuffers(fd) != 0 ? ZFS_OK : win_error(error, "Device flush");
}

void zfs_native_close(zfs_native fd)
{
    if (fd != INVALID_HANDLE_VALUE) {
        CloseHandle(fd);
    }
}

zfs_status zfs_discover(zfs_info* out, size_t capacity, size_t* count, zfs_error* error)
{
    if (count == NULL || (out == NULL && capacity != 0)) {
        return zfs_error_set(error, ZFS_INVALID, "Invalid discovery buffer");
    }
    *count = 0;
    for (int i = 0; i < 128 && *count < capacity; ++i) {
        char path[64];
        snprintf(path, sizeof(path), "\\\\.\\PhysicalDrive%d", i);
        zfs_disk* disk = NULL;
        if (zfs_open(path, false, &disk, NULL) == ZFS_OK) {
            out[(*count)++] = *zfs_get_info(disk);
            zfs_close(disk);
        }
    }
    return ZFS_OK;
}

bool zfs_native_same(zfs_native first_device, zfs_native second_device)
{
    STORAGE_DEVICE_NUMBER first_info;
    STORAGE_DEVICE_NUMBER second_info;
    DWORD bytes_returned;
    return DeviceIoControl(first_device,
                           IOCTL_STORAGE_GET_DEVICE_NUMBER,
                           NULL,
                           0,
                           &first_info,
                           sizeof(first_info),
                           &bytes_returned,
                           NULL) != 0 &&
           DeviceIoControl(second_device,
                           IOCTL_STORAGE_GET_DEVICE_NUMBER,
                           NULL,
                           0,
                           &second_info,
                           sizeof(second_info),
                           &bytes_returned,
                           NULL) != 0 &&
           first_info.DeviceType == second_info.DeviceType && first_info.DeviceNumber == second_info.DeviceNumber;
}

static zfs_status unmounted(HANDLE disk, zfs_error* error)
{
    STORAGE_DEVICE_NUMBER device;
    DWORD bytes_returned;
    if (DeviceIoControl(disk,
                        IOCTL_STORAGE_GET_DEVICE_NUMBER,
                        NULL,
                        0,
                        &device,
                        sizeof(device),
                        &bytes_returned,
                        NULL) == 0) {
        return win_error(error, "Identify device");
    }
    if (device.PartitionNumber != 0 && device.PartitionNumber != MAXDWORD) {
        return zfs_error_set(error, ZFS_INVALID, "Select a whole PhysicalDrive device, not a volume/partition");
    }
    wchar_t volume[MAX_PATH];
    HANDLE search = FindFirstVolumeW(volume, MAX_PATH);
    if (search == INVALID_HANDLE_VALUE) {
        return win_error(error, "Enumerate mounted volumes");
    }
    zfs_status result = ZFS_OK;
    do {
        size_t length = wcslen(volume);
        if (length != 0 && volume[length - 1] == L'\\') {
            volume[length - 1] = 0;
        }
        HANDLE volume_handle = CreateFileW(volume, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (volume_handle == INVALID_HANDLE_VALUE) {
            result = win_error(error, "Inspect volume; cannot establish that device is unmounted");
            break;
        }

        union {
            VOLUME_DISK_EXTENTS alignment;
            unsigned char bytes[VOLUME_EXTENTS_BUFFER_SIZE];
        } buffer;

        BOOL ok = DeviceIoControl(volume_handle,
                                  IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS,
                                  NULL,
                                  0,
                                  buffer.bytes,
                                  sizeof(buffer.bytes),
                                  &bytes_returned,
                                  NULL);
        DWORD code = ok != 0 ? ERROR_SUCCESS : GetLastError();
        CloseHandle(volume_handle);
        if (ok == 0) {
            if (code == ERROR_INVALID_FUNCTION || code == ERROR_NOT_SUPPORTED) {
                continue;
            }
            result = zfs_error_set(error, ZFS_BUSY, "Cannot inspect volume extents; close volume users and retry");
            break;
        }
        VOLUME_DISK_EXTENTS* extents = (VOLUME_DISK_EXTENTS*) buffer.bytes;
        if (bytes_returned < offsetof(VOLUME_DISK_EXTENTS, Extents) ||
            extents->NumberOfDiskExtents >
                (bytes_returned - offsetof(VOLUME_DISK_EXTENTS, Extents)) / sizeof(DISK_EXTENT)) {
            result = zfs_error_set(error, ZFS_BUSY, "Invalid volume extent information");
            break;
        }
        bool matches = false;
        for (DWORD i = 0; i < extents->NumberOfDiskExtents; ++i) {
            if (extents->Extents[i].DiskNumber == device.DeviceNumber) {
                matches = true;
            }
        }
        if (!matches) {
            continue;
        }
        if (length == 0) {
            result = zfs_error_set(error, ZFS_BUSY, "Invalid volume name");
            break;
        }
        volume[length - 1] = L'\\';
        wchar_t paths[32768];
        DWORD required;
        if (GetVolumePathNamesForVolumeNameW(volume, paths, 32768, &required) == 0) {
            result = win_error(error, "Inspect volume mount points");
            break;
        }
        if (*paths != 0) {
            result =
                zfs_error_set(error, ZFS_BUSY, "Device has a mounted volume; unmount it before merging or restoring");
            break;
        }
    } while (FindNextVolumeW(search, volume, MAX_PATH) != 0);
    if (result == ZFS_OK && GetLastError() != ERROR_NO_MORE_FILES) {
        result = win_error(error, "Enumerate volumes");
    }
    FindVolumeClose(search);
    return result;
}

zfs_status zfs_native_guard(zfs_native* fd, const char* path, bool image, bool writing, zfs_error* error)
{
    if (image) {
        OVERLAPPED overlap = {0};
        return LockFileEx(*fd, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, MAXDWORD, MAXDWORD, &overlap) !=
                       0
                   ? ZFS_OK
                   : zfs_error_set(error, ZFS_BUSY, "Image is locked by another operation");
    }
    zfs_status status = unmounted(*fd, error);
    if (status != ZFS_OK) {
        return status;
    }
    STORAGE_DEVICE_NUMBER before;
    STORAGE_DEVICE_NUMBER after;
    DWORD bytes_returned;
    if (DeviceIoControl(*fd,
                        IOCTL_STORAGE_GET_DEVICE_NUMBER,
                        NULL,
                        0,
                        &before,
                        sizeof(before),
                        &bytes_returned,
                        NULL) == 0) {
        return win_error(error, "Identify device");
    }
    CloseHandle(*fd);
    *fd = CreateFileA(path, GENERIC_READ | (writing ? GENERIC_WRITE : 0), 0, NULL, OPEN_EXISTING, 0, NULL);
    if (*fd == INVALID_HANDLE_VALUE) {
        return zfs_error_set(
            error,
            ZFS_BUSY,
            "Cannot exclusively open device; unmount it, close other applications, and check permissions");
    }
    if (DeviceIoControl(*fd, IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL, 0, &after, sizeof(after), &bytes_returned, NULL) ==
            0 ||
        before.DeviceNumber != after.DeviceNumber || before.DeviceType != after.DeviceType) {
        return zfs_error_set(error, ZFS_BUSY, "Device identity changed");
    }
    return ZFS_OK;
}
