/* SPDX-License-Identifier: Apache-2.0 */
#ifndef ZFS_INTERNAL_H
#define ZFS_INTERNAL_H
#include "zealfs.h"
#include <stdio.h>
#ifdef _WIN32
#include <windows.h>
typedef HANDLE zfs_native;
#define ZFS_NATIVE_INVALID INVALID_HANDLE_VALUE
#else
typedef int zfs_native;
#define ZFS_NATIVE_INVALID (-1)
#endif
zfs_status zfs_error_set(zfs_error *e, zfs_status code, const char *format, ...);
zfs_status zfs_native_open(const char *path, bool write, zfs_native *fd, uint64_t *size, zfs_error *e);
zfs_status zfs_native_read(zfs_native fd, uint64_t offset, void *data, size_t size, zfs_error *e);
zfs_status zfs_native_write(zfs_native fd, uint64_t offset, const void *data, size_t size, zfs_error *e);
zfs_status zfs_native_sync(zfs_native fd, zfs_error *e);
void zfs_native_close(zfs_native fd);
/* Host helpers use binary files and 64-bit offsets on every architecture. */
bool zfs_host_resolve(const char *path, char *out, size_t capacity);
FILE *zfs_host_open(const char *path, const char *mode);
int zfs_seek(FILE *file, uint64_t offset);
int64_t zfs_tell(FILE *file);
FILE *zfs_temp_sibling(const char *path, char *temporary, size_t capacity);
int zfs_host_remove(const char *path);
int zfs_host_publish(const char *temporary, const char *path, bool overwrite);
int zfs_host_sync(FILE *file);
#endif
