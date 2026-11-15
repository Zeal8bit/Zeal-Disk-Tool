/**
 * SPDX-FileCopyrightText: 2025 Zeal 8-bit Computer <contact@zeal8bit.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "internal.h"
#include <dirent.h>
#include <ctype.h>
#include <string.h>

zfs_status zfs_discover(zfs_info* out, size_t capacity, size_t* count, zfs_error* error)
{
    if (count == NULL || (out == NULL && capacity != 0)) {
        return zfs_error_set(error, ZFS_INVALID, "Invalid discovery buffer");
    }
    *count = 0;
    DIR* dir = opendir("/dev");
    if (dir == NULL) {
        return zfs_error_set(error, ZFS_IO, "Cannot enumerate /dev");
    }
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL && *count < capacity) {
        const char* name = entry->d_name;
        if (strncmp(name, "rdisk", 5) != 0 || isdigit((unsigned char) name[5]) == 0) {
            continue;
        }
        const char* p = name + 5;
        while (isdigit((unsigned char) *p) != 0) {
            ++p;
        }
        if (*p != 0) {
            continue;
        }
        char path[ZFS_PATH_MAX];
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
