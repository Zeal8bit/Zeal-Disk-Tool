/**
 * SPDX-FileCopyrightText: 2025 Zeal 8-bit Computer <contact@zeal8bit.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef DISK_H
#define DISK_H

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#define DIM(arr) (sizeof(arr) / sizeof(*(arr)))

#define GB  (1073741824ULL)
#define MB  (1048576ULL)
#define KB  (1024ULL)
#define MAX(a,b)    ((a) > (b) ? (a) : (b))

#define ZEALFS_TYPE         0x5a

#define MAX_DISKS           32
#define MAX_DISK_SIZE       (32*GB)
#define DISK_LABEL_LEN      512
#define MAX_PART_COUNT      4
#define DISK_SECTOR_SIZE    512UL

#define MBR_PART_ENTRY_SIZE     16
#define MBR_PART_ENTRY_BEGIN    0x1BE

typedef enum {
    ERR_SUCCESS,
    ERR_NOT_ADMIN,  /* Windows   */
    ERR_NOT_ROOT,   /* Linux/Mac */
    ERR_INVALID,
} disk_err_t;


typedef struct {
    bool     active;
    uint8_t  type;
    uint32_t start_lba;
    uint32_t size_sectors;
    /* Formatted data to write to disk */
    uint8_t* data;
    /* We will write at most 64KB*3, 32-bit is more than enough*/
    uint32_t data_len;
} partition_t;


typedef struct {
    char        name[256];
    char        path[256];
    char        label[DISK_LABEL_LEN];

    uint64_t    size_bytes;
    bool        valid;
    bool        is_image;
    /* Original MBR */
    bool        has_mbr;
    uint8_t     mbr[DISK_SECTOR_SIZE];
    partition_t partitions[MAX_PART_COUNT];
    /* Staged changes, to be applied */
    bool        has_staged_changes;
    uint8_t     staged_mbr[DISK_SECTOR_SIZE];
    partition_t staged_partitions[MAX_PART_COUNT];
    int         free_part_idx;
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

extern disk_list_state_t disk_view_state;

static inline disk_info_t* disk_get_current(disk_list_state_t* state)
{
    return (state->disk_count > 0) ? &state->disks[state->selected_disk] : NULL;
}

static inline int disk_is_valid_zealfs_partition(partition_t* part)
{
    return part != NULL && part->active && part->type == 0x5A;
}

static inline int disk_can_be_switched(disk_info_t* disk)
{
    return disk == NULL || !disk->has_staged_changes;
}


static inline const char* disk_get_basename(const char* path)
{
    const char* last_slash = NULL;
#ifdef _WIN32
    last_slash = strrchr(path, '\\');
#else
    last_slash = strrchr(path, '/');
#endif

    return last_slash ? last_slash + 1 : path;
}


/**
 * ============================================================================
 *                              PORTABLE CODE
 * ============================================================================
 */

disk_list_state_t* disk_get_state(void);

disk_err_t disks_refresh(void);

void disk_apply_changes(disk_info_t* disk);

void disk_revert_changes(disk_info_t* disk);

disk_err_t disk_list(disk_info_t* out_disks, int max_disks, int* out_count);

void disk_parse_mbr_partitions(disk_info_t *disk);

const char* const *disk_get_partition_size_list(int* count);

uint64_t disk_get_size_of_idx(int index);

int disk_create_mbr(disk_info_t *disk);

void disk_allocate_partition(disk_info_t *disk, uint32_t lba, uint32_t sectors_count);

const char* disk_format_partition(disk_info_t* disk, int partition);

void disk_delete_partition(disk_info_t* disk, int partition);

uint64_t disk_max_partition_size(disk_info_t *disk, uint32_t align, uint64_t *largest_free_addr);

const char* disk_get_fs_type(uint8_t fs_byte);

void disk_get_size_str(uint64_t size, char* buffer, int buffer_size);

const char* disk_write_changes(disk_info_t* disk);

/**
 * @brief Prompts the user to choose a disk image file to add to the disk list.
 *
 * This function allows the user to select a disk image file, which is then added
 * to the provided disk list state. It returns the index of the newly added disk
 * on success or a negative value if an error occurs.
 *
 * @param state A pointer to the disk list state where the disk image will be added.
 * @return int The index of the newly added disk on success, or a negative value on error.
 */
int disk_open_image_file(disk_list_state_t* state);

int disk_create_image(disk_list_state_t* state, const char* path, uint64_t size, bool init_mbr);


/**
 * ============================================================================
 *                              OS SPECIFIC CODE
 * ============================================================================
 */
/**
 * Opens a disk partition for reading and writing files to ZealFS partitions.
 *
 * @param disk A pointer to the disk_info_t structure containing information about the disk.
 * @param ret_fd A pointer to a location where the abstract file descriptor will be stored on success.
 *               This file descriptor will be used in subsequent operations.
 * @return 0 on success, or a positive value indicating an error.
 *         Logs errors if any occur.
 */
int disk_open(disk_info_t* disk, void** ret_fd);

/**
 * Reads data from a disk partition at a specified offset.
 *
 * @param disk_fd The abstract file descriptor of the disk, obtained from disk_open.
 * @param buffer A pointer to the buffer where the read data will be stored.
 * @param disk_offset The offset on the disk from where the read operation will start.
 *        Guaranteed to be aligned on DISK_SECTOR_SIZE.
 * @param len The number of bytes to read.
 * @return The number of bytes read on success, or a negative value indicating an error.
 *         Logs errors if any occur.
 */
ssize_t disk_read(void* disk_fd, void* buffer, off_t disk_offset, uint32_t len);

/**
 * Writes data to a disk partition at a specified offset.
 *
 * @param disk_fd The abstract file descriptor of the disk, obtained from disk_open.
 * @param buffer A pointer to the buffer containing the data to be written.
 * @param disk_offset The offset on the disk where the write operation will start.
 *        Guaranteed to be aligned on DISK_SECTOR_SIZE.
 * @param len The number of bytes to write.
 * @return The number of bytes written on success, or a negative value indicating an error.
 *         Logs errors if any occur.
 */
ssize_t disk_write(void* disk_fd, const void* buffer, off_t disk_offset, uint32_t len);

/**
 * Closes the disk partition and releases any associated resources.
 *
 * @param disk_fd The abstract file descriptor of the disk, obtained from disk_open.
 *                After this call, the file descriptor is no longer valid.
 *         Logs errors if any occur.
 */
void disk_close(void* disk_fd);


/**
 * OPTIONAL OS FEATURES
 */
/**
 * @brief Initializes the progress bar for disk operations.
 *
 * This function sets up the progress bar to provide visual feedback
 * to the user about the progress of a disk operation.
 */
void disk_init_progress_bar(void);

/**
 * @brief Updates the progress bar with the current progress.
 *
 * @param percent The current progress percentage (0 to 100).
 * This function updates the progress bar to reflect the given
 * percentage, providing real-time feedback to the user.
 */
void disk_update_progress_bar(int percent);

/**
 * @brief Destroys the progress bar after the operation is complete.
 *
 * This function cleans up any resources associated with the progress bar
 * and removes it from the display once the disk operation is finished.
 */
void disk_destroy_progress_bar(void);

#endif // DISK_H
