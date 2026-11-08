/**
 * SPDX-FileCopyrightText: 2025 Zeal 8-bit Computer <contact@zeal8bit.com>
 *
 * SPDX-License-Identifier: Apache-2.0 */
#include "internal.h"
#include <dirent.h>
#include <ctype.h>
#include <string.h>

zfs_status zfs_discover(zfs_info* out, size_t capacity, size_t* count, zfs_error* e)
{
    if (!count || (!out && capacity)) {
        return zfs_error_set(e, ZFS_INVALID, "Invalid discovery buffer");
    }
    *count = 0;
    DIR* dir = opendir("/dev");
    if (!dir) {
        return zfs_error_set(e, ZFS_IO, "Cannot enumerate /dev");
    }
    struct dirent* entry;
    while ((entry = readdir(dir)) && *count < capacity) {
        const char* name = entry->d_name;
        if (strncmp(name, "rdisk", 5) || !isdigit((unsigned char) name[5])) {
            continue;
        }
        const char* p = name + 5;
        while (isdigit((unsigned char) *p)) {
            ++p;
        }
        if (*p) {
            continue;
        }
        char path[ZFS_PATH_MAX];
        snprintf(path, sizeof(path), "/dev/%s", name);
        zfs_disk* d = NULL;
        if (zfs_open(path, false, &d, NULL) == ZFS_OK) {
            out[(*count)++] = *zfs_get_info(d);
            zfs_close(d);
        }
    }
    closedir(dir);
    return ZFS_OK;
}
