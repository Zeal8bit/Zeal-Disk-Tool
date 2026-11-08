/* SPDX-License-Identifier: Apache-2.0 */
#ifndef ZEALFS_API_H
#define ZEALFS_API_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
#define ZFS_MAX_PARTITIONS 4
#define ZFS_MAX_DISK_SIZE (UINT64_C(64) * 1024 * 1024 * 1024)
#define ZFS_PATH_MAX 4096
#define ZFS_NAME_MAX 16
typedef struct zfs_disk zfs_disk;
typedef enum {
    ZFS_OK,
    ZFS_INVALID,
    ZFS_IO,
    ZFS_PERMISSION,
    ZFS_NOT_FOUND,
    ZFS_EXISTS,
    ZFS_NO_SPACE,
    ZFS_NO_MEMORY,
    ZFS_BUSY,
    ZFS_CORRUPT,
    ZFS_PARTIAL_WRITE
} zfs_status;
typedef struct {
    zfs_status code;
    char message[512];
} zfs_error;
typedef struct {
    bool active;
    uint8_t type;
    uint32_t start_lba, sectors;
} zfs_partition;
typedef struct {
    char path[ZFS_PATH_MAX];
    uint64_t size;
    bool image, has_mbr, pending, new_image, failed;
    zfs_partition partitions[ZFS_MAX_PARTITIONS];
} zfs_info;
typedef struct {
    char name[ZFS_NAME_MAX + 1];
    bool directory;
    uint32_t size;
    unsigned year, month, day, hour, minute, second;
} zfs_entry;
/* Callbacks are synchronous, optional, and must not reenter the session. */
typedef void (*zfs_progress_fn)(void *user, uint64_t completed, uint64_t total);
typedef void (*zfs_log_fn)(void *user, const char *message);
typedef struct {
    zfs_progress_fn progress;
    zfs_log_fn log;
    void *user;
} zfs_callbacks;

/* All errors are caller-owned; NULL is allowed. Each handle is single-threaded.
 * Open/discover never writes. Close discards. Only apply writes the target.
 * Partition indices in this API are zero-based. Paths are absolute ZealFS paths.
 */
zfs_status zfs_discover(zfs_info *out, size_t capacity, size_t *count, zfs_error *error);
zfs_status zfs_open(const char *path, bool image, zfs_disk **out, zfs_error *error);
zfs_status zfs_image_create(const char *path, uint64_t size, bool mbr, zfs_disk **out, zfs_error *error);
void zfs_close(zfs_disk *disk);
void zfs_set_callbacks(zfs_disk *disk, zfs_callbacks callbacks);
const zfs_info *zfs_get_info(const zfs_disk *disk);
zfs_status zfs_apply(zfs_disk *disk, zfs_error *error);
zfs_status zfs_discard(zfs_disk *disk, zfs_error *error);
zfs_status zfs_mbr_create(zfs_disk *disk, zfs_error *error);
zfs_status zfs_partition_space(zfs_disk *disk, uint32_t alignment, uint64_t *start, uint64_t *size,
                               zfs_error *error);
zfs_status zfs_partition_create(zfs_disk *disk, uint64_t size, uint32_t alignment, int *index,
                                zfs_error *error);
zfs_status zfs_partition_format(zfs_disk *disk, int index, zfs_error *error);
zfs_status zfs_partition_delete(zfs_disk *disk, int index, zfs_error *error);
/* Listing allocates entries; release with zfs_entries_free. */
zfs_status zfs_list(zfs_disk *disk, int partition, const char *path, zfs_entry **entries, size_t *count,
                    zfs_error *error);
void zfs_entries_free(zfs_entry *entries);
zfs_status zfs_usage(zfs_disk *disk, int partition, uint64_t *free_bytes, uint64_t *total_bytes,
                     zfs_error *error);
zfs_status zfs_mkdir(zfs_disk *disk, int partition, const char *path, zfs_error *error);
zfs_status zfs_remove(zfs_disk *disk, int partition, const char *path, bool directory, zfs_error *error);
zfs_status zfs_import(zfs_disk *disk, int partition, const char *host_path, const char *path,
                      zfs_error *error);
zfs_status zfs_export(zfs_disk *disk, int partition, const char *path, const char *host_path, bool overwrite,
                      zfs_error *error);
zfs_status zfs_file_read(zfs_disk *disk, int partition, const char *path, uint64_t offset, void *buffer,
                         size_t size, size_t *read_count, zfs_error *error);
/* Creates a new file from bytes; existing files are never overwritten. */
zfs_status zfs_file_write(zfs_disk *disk, int partition, const char *path, const void *buffer, size_t size,
                          zfs_error *error);
const char *zfs_status_string(zfs_status status);
#ifdef __cplusplus
}
#endif
#endif
