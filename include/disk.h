/**
 * SPDX-FileCopyrightText: 2025 Zeal 8-bit Computer <contact@zeal8bit.com>
 * SPDX-FileCopyrightText: 2026 David Higgins <zoul0813@me.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef DISK_H
#define DISK_H
/* GUI view models and adapters; public functional API is disk_tool.h.
 * This layer, like the rest of the core, is plain C. The FLTK frontend is C++
 * and only consumes these functions. */
#include "disk_tool.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#define DIM(arr) (sizeof(arr) / sizeof(*(arr)))
#define GB UINT64_C(1073741824)
#define MB UINT64_C(1048576)
#define KB UINT64_C(1024)
#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif
#define MAX_DISKS 32
#define MAX_PART_COUNT ZFS_MAX_PARTITIONS
#define MAX_DISK_SIZE ZFS_MAX_DISK_SIZE
#define DISK_LABEL_LEN 512

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ERR_SUCCESS,
    ERR_NOT_ADMIN, /* Windows   */
    ERR_NOT_ROOT,  /* Linux/Mac */
    ERR_INVALID
} disk_err_t;

typedef struct {
    bool active;
    uint8_t type;
    uint32_t start_lba;
    uint32_t size_sectors;
} partition_t;

typedef struct {
    char name[ZFS_PATH_MAX];
    char path[ZFS_PATH_MAX];
    char label[DISK_LABEL_LEN];
    uint64_t size_bytes;
    bool valid;
    bool is_image;
    /* Original MBR */
    bool has_mbr;
    /* Staged changes, to be applied */
    bool has_staged_changes;
    partition_t partitions[MAX_PART_COUNT];
    partition_t staged_partitions[MAX_PART_COUNT];
    int free_part_idx;
    zfs_disk* handle;
    unsigned revision;
} disk_info_t;

/**
 * @brief Type for the disks list state
 */
typedef struct {
    disk_info_t disks[MAX_DISKS];
    int disk_count;
    int selected_disk; /* Index of the selected disk */
    int selected_partition;

    /* View related */
    int selected_new_part_opt;
} disk_list_state_t;

/**
 * @brief Receives user-facing messages produced by the model.
 *
 * The frontend registers a handler to display these messages (status bar,
 * dialogs, ...). It is invoked synchronously from model functions.
 */
typedef void (*disk_status_fn)(const char* message, void* user);

static inline disk_info_t* disk_get_current(disk_list_state_t* state)
{
    return state != NULL && state->selected_disk >= 0 && state->selected_disk < state->disk_count
               ? &state->disks[state->selected_disk]
               : NULL;
}

static inline int disk_is_valid_zealfs_partition(partition_t* part)
{
    return part != NULL && part->active && part->type == ZEALFS_TYPE;
}

static inline int disk_can_be_switched(disk_info_t* disk)
{
    return disk == NULL || !disk->has_staged_changes;
}

static inline const char* disk_get_basename(const char* path)
{
    const char* last_slash = strrchr(path, '/');
#ifdef _WIN32
    const char* last_backslash = strrchr(path, '\\');
    if (last_backslash != NULL && (last_slash == NULL || last_backslash > last_slash)) {
        last_slash = last_backslash;
    }
#endif
    return last_slash != NULL ? last_slash + 1 : path;
}

/**
 * ============================================================================
 *                              PORTABLE CODE
 * ============================================================================
 */

/* Register the message sink used by every model operation below. */
void disk_set_status_fn(disk_status_fn fn, void* user);

disk_list_state_t* disk_get_state(void);
disk_err_t disks_refresh(void);
void disks_close(void);
void disk_sync(disk_info_t* disk);
void disk_revert_changes(disk_info_t* disk);
const char* const* disk_get_partition_size_list(int* count);
uint64_t disk_get_size_of_idx(int index);
int disk_create_mbr(disk_info_t* disk);
/* Stage a partition of `size_bytes`, aligned on `alignment` bytes.
 * On success `index` receives its slot (0..3). */
int disk_partition_create(disk_info_t* disk, uint64_t size_bytes, uint32_t alignment, int* index);
const char* disk_format_partition(disk_info_t* disk, int partition);
void disk_delete_partition(disk_info_t* disk, int partition);
/* Largest free extent, aligned on `align`, and its starting byte address. */
uint64_t disk_max_partition_size(disk_info_t* disk, uint32_t align, uint64_t* largest_free_addr);
const char* disk_get_fs_type(uint8_t fs_byte);
void disk_get_size_str(uint64_t size, char* buffer, int buffer_size);
const char* disk_write_changes(disk_info_t* disk);

/**
 * ============================================================================
 *                          FILESYSTEM ADAPTERS
 * ============================================================================
 * These wrappers own the staged session and report failures through the status
 * handler. They return 0 on success and a negative value on failure.
 */

/* `entries` is allocated and must be released with zfs_entries_free(). */
int disk_fs_list(disk_info_t* disk, int partition, const char* path, zfs_entry** entries, size_t* count);
/* Silent query used to paint usage indicators; returns 0 when available. */
int disk_fs_usage(disk_info_t* disk, int partition, uint64_t* used_bytes, uint64_t* total_bytes);
int disk_fs_mkdir(disk_info_t* disk, int partition, const char* path);
int disk_fs_remove(disk_info_t* disk, int partition, const char* path, bool directory);
/* `destination` is a filesystem directory (trailing `/` allowed) or the full
 * target path when a single file is renamed. */
int disk_fs_import(disk_info_t* disk, int partition, const char* const* host_paths, size_t count,
                   const char* destination, zfs_progress_fn progress, void* user);
int disk_fs_export(disk_info_t* disk, int partition, const char* path, const char* host_path,
                   zfs_progress_fn progress, void* user);

/**
 * @brief Open a disk image file from a given path and add it to the disk list.
 *
 * Unlike the file-dialog entry point of older frontends, this function does not
 * prompt the user; the FLTK frontend owns host file dialogs.
 *
 * @param state A pointer to the disk list state where the disk image will be added.
 * @param path  Path to the image file to open.
 * @return int The index of the newly added disk on success, or a negative value on error.
 */
int disk_open_image_file_path(disk_list_state_t* state, const char* path);
int disk_create_image(disk_list_state_t* state, const char* path, uint64_t size, bool init_mbr);

#ifdef __cplusplus
}
#endif
#endif  // DISK_H
