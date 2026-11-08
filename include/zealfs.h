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
    ZFS_PARTIAL_WRITE,
    ZFS_CONFLICT
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
/* Files export to host_path; directories recursively export their contents into
 * host_path. Existing unrelated host entries are preserved. Each file publishes
 * atomically; an I/O failure may leave already-exported files/directories. */
zfs_status zfs_export(zfs_disk *disk, int partition, const char *path, const char *host_path, bool overwrite,
                      zfs_error *error);
/* Validate source contents and destination conflicts without host writes. */
zfs_status zfs_export_validate(zfs_disk *disk, int partition, const char *path, const char *host_path,
                               bool overwrite, zfs_error *error);
zfs_status zfs_file_read(zfs_disk *disk, int partition, const char *path, uint64_t offset, void *buffer,
                         size_t size, size_t *read_count, zfs_error *error);
/* Creates a new file from bytes; existing files are never overwritten. */
zfs_status zfs_file_write(zfs_disk *disk, int partition, const char *path, const void *buffer, size_t size,
                          zfs_error *error);
const char *zfs_status_string(zfs_status status);

/* Merge preparation reads both committed filesystems and builds an isolated,
 * bounded-memory staged view. Handles must be clean. Protection acquired here
 * lasts until the handle is closed (including across shell apply/discard).
 * A prepared plan borrows its target; close/free the plan before that target.
 * Reports must initially be zeroed and released with zfs_merge_report_free,
 * including on errors, when they may contain type-conflict paths. */
typedef struct zfs_merge_plan zfs_merge_plan;
typedef struct {
    char source[ZFS_PATH_MAX], target[ZFS_PATH_MAX], backup[ZFS_PATH_MAX];
    int source_partition, target_partition;
    uint64_t files_added, files_replaced, files_unchanged, files_preserved;
    uint64_t directories_created, directories_merged, directories_preserved;
    uint64_t bytes_copied, backup_bytes;
    char **conflicts;
    size_t conflict_count;
    bool restore, noop, backup_created, staged, applied;
} zfs_merge_report;
zfs_status zfs_merge_prepare(zfs_disk *source, int source_partition, zfs_disk *target, int target_partition,
                             zfs_merge_plan **plan, zfs_merge_report *report, zfs_error *error);
/* Verify and publish the original partition backup before exposing the prepared
 * merge as target changes. A NULL backup_path explicitly skips backup.
 * No-op plans create no backup. Apply remains explicit. */
zfs_status zfs_merge_stage(zfs_merge_plan *plan, const char *backup_path, zfs_error *error);
/* Check destination and available host space without creating a backup. Stage
 * repeats the checks and publishes without replacement to handle races. */
zfs_status zfs_merge_check_backup(zfs_merge_plan *plan, const char *backup_path, zfs_error *error);
zfs_status zfs_backup_partition(zfs_disk *disk, int partition, const char *path, zfs_error *error);
zfs_status zfs_restore_prepare(const char *backup_path, zfs_disk *target, int partition,
                               zfs_merge_plan **plan, zfs_merge_report *report, zfs_error *error);
zfs_status zfs_restore_stage(zfs_merge_plan *plan, zfs_error *error);
void zfs_merge_plan_free(zfs_merge_plan *plan);
void zfs_merge_report_free(zfs_merge_report *report);
/* Merge/restore automatically request verified Apply, including subsequent
 * shell apply calls. These identify the retained backup after a write failure. */
const char *zfs_recovery_backup(const zfs_disk *disk);
int zfs_recovery_partition(const zfs_disk *disk);
#ifdef __cplusplus
}
#endif
#endif
