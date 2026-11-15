/**
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZFS_HOST_PRIVATE_H
#define ZFS_HOST_PRIVATE_H
#include "internal.h"

/* One synchronous wildcard expansion. The shared layer validates the pattern
 * and owns the path list; the native layer enumerates matching regular files.
 * Reuse one path buffer across both layers, including the directory prefix. */
typedef struct {
    const char* pattern;
    const char* name;
    char path[ZFS_PATH_MAX];
    size_t prefix;
    char*** paths;
    size_t* count;
} zfs_host_expansion;

bool zfs_host_separator(char c);
/* Start mkdir traversal after a Windows drive or UNC server/share prefix. */
size_t zfs_host_mkdir_start(const char* current);
zfs_status zfs_host_make_directory(const char* path, zfs_error* error);
zfs_status zfs_host_expand_files(zfs_host_expansion* expansion, zfs_error* error);
zfs_status zfs_host_append_path(char*** paths, size_t* count, const char* path, zfs_error* error);
#endif
