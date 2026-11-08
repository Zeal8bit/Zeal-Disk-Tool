/* SPDX-License-Identifier: Apache-2.0 */
#ifndef ZFS_DISK_PRIVATE_H
#define ZFS_DISK_PRIVATE_H
#include "internal.h"
#include <sys/stat.h>
#ifdef _WIN32
#include <io.h>
#define fstat _fstat64
#define stat _stat64
#define fileno _fileno
#endif
struct zfs_disk {
    zfs_info info, original;
    FILE *base, *overlay, *undo;
    zfs_native device;
    unsigned char *dirty;
    uint64_t dirty_count, undo_count, revision;
    size_t bitmap_size;
    zfs_callbacks callbacks;
    bool usage_cached[4];
    uint64_t free_cache[4], total_cache[4];
    struct stat stamp;
#ifdef _WIN32
    BY_HANDLE_FILE_INFORMATION win_stamp;
#endif
    bool borrowed_base, guarded, guard_write, verified_apply, recovery_is_merge;
    char recovery_backup[ZFS_PATH_MAX];
    int recovery_partition;
    uint64_t recovery_offset, recovery_size;
};
zfs_status zfs_core_guard(zfs_disk *disk, bool writing, zfs_error *error);
bool zfs_core_same_disk(zfs_disk *a, zfs_disk *b);
bool zfs_core_unchanged(zfs_disk *disk);
zfs_status zfs_core_clone(zfs_disk *disk, zfs_disk **view, zfs_error *error);
void zfs_core_attach(zfs_disk *target, zfs_disk *view);
zfs_status zfs_core_read(zfs_disk *disk, uint64_t offset, void *buffer, size_t size, zfs_error *error);
zfs_status zfs_core_stage_raw(zfs_disk *disk, uint64_t offset, zfs_disk *source, uint64_t size,
                              zfs_error *error);
zfs_status zfs_core_copy_file(zfs_disk *source, int sp, zfs_disk *target, int tp, const char *path,
                              zfs_error *error);
zfs_status zfs_core_copy_metadata(zfs_disk *source, int sp, zfs_disk *target, int tp, const char *path,
                                  zfs_error *error);
zfs_status zfs_core_files_equal(zfs_disk *a, int ap, zfs_disk *b, int bp, const char *path, bool *equal,
                                zfs_error *error);
zfs_status zfs_core_check_file(zfs_disk *disk, int partition, const char *path, zfs_error *error);
zfs_status zfs_core_path_directory(zfs_disk *disk, int partition, const char *path, bool *directory,
                                  zfs_error *error);
zfs_status zfs_core_export_destination(zfs_disk *disk, const char *host, zfs_error *error);
zfs_status zfs_core_export_file(zfs_disk *disk, int partition, const char *path, const char *host,
                               bool overwrite, zfs_error *error);
#endif
