/**
 * SPDX-FileCopyrightText: 2025 Zeal 8-bit Computer <contact@zeal8bit.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "internal.h"
#include <dirent.h>
#include <string.h>
#include <unistd.h>

zfs_status zfs_discover(zfs_info* out, size_t capacity, size_t* count, zfs_error* error)
{
    if (count == NULL || (out == NULL && capacity != 0)) {
        return zfs_error_set(error, ZFS_INVALID, "Invalid discovery buffer");
    }
    *count = 0;
    DIR* dir = opendir("/sys/class/block");
    if (dir == NULL) {
        return zfs_error_set(error, ZFS_IO, "Cannot enumerate /sys/class/block");
    }
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL && *count < capacity) {
        const char* name = entry->d_name;
        if (strncmp(name, "sd", 2) != 0 && strncmp(name, "mmcblk", 6) != 0 && strncmp(name, "nvme", 4) != 0 &&
            strncmp(name, "vd", 2) != 0) {
            continue;
        }
        char path[ZFS_PATH_MAX];
        char partition[ZFS_PATH_MAX];
        snprintf(partition, sizeof(partition), "/sys/class/block/%s/partition", name);
        if (access(partition, F_OK) == 0 || strstr(name, "boot") != NULL || strstr(name, "rpmb") != NULL) {
            continue;
        }
        snprintf(path, sizeof(path), "/dev/%s", name);
        zfs_disk* disk = NULL;
        if (zfs_open(path, false, &disk, NULL) == ZFS_OK) {
            out[(*count)++] = *zfs_get_info(disk);
            zfs_close(disk);
        }
    }
    closedir(dir);
    return ZFS_OK;
}
