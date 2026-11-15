/**
 * SPDX-FileCopyrightText: 2026 David Higgins <zoul0813@me.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZFS_INTERNAL_H
#define ZFS_INTERNAL_H
#include "disk_tool.h"
#include <stdio.h>
/* Scratch bytes per streamed file/image transfer, not an on-disk format limit.
 * Comparisons may hold two buffers at once. Keep sector transfers aligned when
 * adjusting this value; changing it does not change the supported disk size. */
#define ZFS_TRANSFER_BUFFER_SIZE (64 * 1024)
#ifdef _WIN32
#include <windows.h>
typedef HANDLE zfs_native;
#define ZFS_NATIVE_INVALID INVALID_HANDLE_VALUE
#else
typedef int zfs_native;
#define ZFS_NATIVE_INVALID (-1)
#endif
zfs_status zfs_error_set(zfs_error* error, zfs_status code, const char* format, ...);
zfs_status zfs_native_open(const char* path, bool write, zfs_native* fd, uint64_t* size, zfs_error* error);
zfs_status zfs_native_read(zfs_native fd, uint64_t offset, void* data, size_t size, zfs_error* error);
zfs_status zfs_native_write(zfs_native fd, uint64_t offset, const void* data, size_t size, zfs_error* error);
zfs_status zfs_native_sync(zfs_native fd, zfs_error* error);
void zfs_native_close(zfs_native fd);
/* Refuse mounted/claimed devices and hold exclusive access. Images use native
 * file locks. Device handles may be replaced with a stable exclusive handle. */
zfs_status zfs_native_guard(zfs_native* fd, const char* path, bool image, bool writing, zfs_error* error);
bool zfs_native_same(zfs_native first_device, zfs_native second_device);
/* Host helpers use binary files and 64-bit offsets on every architecture. */
bool zfs_host_resolve(const char* path, char* out, size_t capacity);
bool zfs_host_exists(const char* path);

typedef enum {
    ZFS_HOST_MISSING,
    ZFS_HOST_FILE,
    ZFS_HOST_DIRECTORY,
    ZFS_HOST_OTHER
} zfs_host_type;

zfs_status zfs_host_kind(const char* path, zfs_host_type* type, zfs_error* error);
zfs_status zfs_host_mkdirs(const char* path, zfs_error* error);
bool zfs_host_free_space(const char* path, uint64_t* bytes);
FILE* zfs_host_open(const char* path, const char* mode);
int zfs_seek(FILE* file, uint64_t offset);
int64_t zfs_tell(FILE* file);
FILE* zfs_temp_sibling(const char* path, char* temporary, size_t capacity);
int zfs_host_remove(const char* path);
int zfs_host_publish(const char* temporary, const char* path, bool overwrite);
int zfs_host_sync(FILE* file);
bool zfs_wildcard_match(const char* pattern, const char* name);
zfs_status zfs_host_expand(const char* pattern, char*** paths, size_t* count, zfs_error* error);
void zfs_host_paths_free(char** paths, size_t count);
#endif
