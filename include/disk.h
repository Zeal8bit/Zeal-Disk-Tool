/**
 * SPDX-FileCopyrightText: 2025 Zeal 8-bit Computer <contact@zeal8bit.com>
 *
 * SPDX-License-Identifier: Apache-2.0 */
#ifndef DISK_H
#define DISK_H
/* GUI view models and adapters; public functional API is zealfs.h. */
#include "zealfs.h"
#include <stdio.h>
#include <string.h>
#define DIM(arr) (sizeof(arr) / sizeof(*(arr)))
#define GB UINT64_C(1073741824)
#define MB UINT64_C(1048576)
#define KB UINT64_C(1024)
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define ZEALFS_TYPE 0x5a
#define MAX_DISKS 32
#define MAX_PART_COUNT 4
#define MAX_DISK_SIZE ZFS_MAX_DISK_SIZE
#define DISK_LABEL_LEN 512
#define DISK_SECTOR_SIZE UINT64_C(512)

typedef enum {
    ERR_SUCCESS,
    ERR_NOT_ADMIN,
    ERR_NOT_ROOT,
    ERR_INVALID
} disk_err_t;

typedef struct {
    bool active;
    uint8_t type;
    uint32_t start_lba, size_sectors;
} partition_t;

typedef struct {
    char name[ZFS_PATH_MAX], path[ZFS_PATH_MAX], label[DISK_LABEL_LEN];
    uint64_t size_bytes;
    bool valid, is_image, has_mbr, has_staged_changes;
    partition_t partitions[4], staged_partitions[4];
    int free_part_idx;
    zfs_disk* handle;
    unsigned revision;
    uint32_t allocation_alignment;
} disk_info_t;

typedef struct {
    disk_info_t disks[MAX_DISKS];
    int disk_count, selected_disk, selected_partition, selected_new_part_opt;
} disk_list_state_t;

static inline disk_info_t* disk_get_current(disk_list_state_t* state)
{
    return state && state->selected_disk >= 0 && state->selected_disk < state->disk_count
               ? &state->disks[state->selected_disk]
               : NULL;
}

static inline int disk_is_valid_zealfs_partition(partition_t* part)
{
    return part && part->active && part->type == 0x5a;
}

static inline int disk_can_be_switched(disk_info_t* disk)
{
    return !disk || !disk->has_staged_changes;
}

static inline const char* disk_get_basename(const char* path)
{
    const char* last_slash = strrchr(path, '/');
#ifdef _WIN32
    const char* last_backslash = strrchr(path, '\\');
    if (last_backslash && (!last_slash || last_backslash > last_slash)) {
        last_slash = last_backslash;
    }
#endif
    return last_slash ? last_slash + 1 : path;
}

disk_list_state_t* disk_get_state(void);
disk_err_t disks_refresh(void);
void disks_close(void);
void disk_sync(disk_info_t* disk);
void disk_revert_changes(disk_info_t* disk);
const char* const* disk_get_partition_size_list(int* count);
uint64_t disk_get_size_of_idx(int index);
int disk_create_mbr(disk_info_t* disk);
void disk_allocate_partition(disk_info_t* disk, uint32_t lba, uint32_t sectors_count);
const char* disk_format_partition(disk_info_t* disk, int partition);
void disk_delete_partition(disk_info_t* disk, int partition);
uint64_t disk_max_partition_size(disk_info_t* disk, uint32_t align, uint64_t* largest_free_addr);
const char* disk_get_fs_type(uint8_t fs_byte);
void disk_get_size_str(uint64_t size, char* buffer, int buffer_size);
const char* disk_write_changes(disk_info_t* disk);
int disk_open_image_file(disk_list_state_t* state);
int disk_open_image_file_path(disk_list_state_t* state, const char* path);
int disk_create_image(disk_list_state_t* state, const char* path, uint64_t size, bool init_mbr);
void disk_init_progress_bar(void);
void disk_update_progress_bar(int percent);
void disk_destroy_progress_bar(void);
#endif
