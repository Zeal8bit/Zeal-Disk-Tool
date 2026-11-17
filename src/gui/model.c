/**
 * SPDX-FileCopyrightText: 2025-2026 Zeal 8-bit Computer <contact@zeal8bit.com>
 * SPDX-FileCopyrightText: 2026 David Higgins <zoul0813@me.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/* GUI adapter. All disk and filesystem behavior lives in zealfs_core; this
 * translation unit stays plain C and reports through a frontend callback. */
#include "disk.h"
#include <stdlib.h>

static disk_list_state_t s_state = {
    .selected_disk = -1,
    .selected_partition = -1,
};
static zfs_error error;
static unsigned next_revision;
static disk_status_fn s_status_fn;
static void* s_status_user;

static const uint64_t s_valid_sizes[] = {
    32 * KB,  64 * KB,  128 * KB, 256 * KB, 512 * KB, 1 * MB,  2 * MB,  4 * MB,  8 * MB,
    16 * MB,  32 * MB,  64 * MB,  128 * MB, 256 * MB, 512 * MB, 1 * GB,  2 * GB,  4 * GB,
};
static const char* const sizes[] = {
    "32 KiB",  "64 KiB",  "128 KiB", "256 KiB", "512 KiB", "1 MiB",  "2 MiB",  "4 MiB",  "8 MiB",
    "16 MiB",  "32 MiB",  "64 MiB",  "128 MiB", "256 MiB", "512 MiB", "1 GiB",  "2 GiB",  "4 GiB",
};

void disk_set_status_fn(disk_status_fn fn, void* user)
{
    s_status_fn = fn;
    s_status_user = user;
}

static void status(const char* message)
{
    if (s_status_fn != NULL) {
        s_status_fn(message, s_status_user);
    }
}

const char* const* disk_get_partition_size_list(int* count)
{
    if (count != NULL) {
        *count = (int) DIM(s_valid_sizes);
    }
    return sizes;
}

uint64_t disk_get_size_of_idx(int index)
{
    return index >= 0 && (size_t) index < DIM(s_valid_sizes) ? s_valid_sizes[index] : 0;
}

disk_list_state_t* disk_get_state(void)
{
    return &s_state;
}

void disk_sync(disk_info_t* disk)
{
    if (disk == NULL || disk->handle == NULL) {
        return;
    }
    const zfs_info* info = zfs_get_info(disk->handle);
    disk->size_bytes = info->size;
    disk->valid = !info->failed;
    disk->is_image = info->image;
    disk->has_mbr = info->has_mbr;
    disk->has_staged_changes = info->pending;
    snprintf(disk->path, sizeof(disk->path), "%s", info->path);
    /* Extract the filename out of the path */
    snprintf(disk->name, sizeof(disk->name), "%s", disk_get_basename(info->path));
    disk->free_part_idx = -1;
    for (int i = 0; i < MAX_PART_COUNT; ++i) {
        const zfs_partition* part = &info->partitions[i];
        disk->partitions[i] = (partition_t){
            .active = part->active,
            .type = part->type,
            .start_lba = part->start_lba,
            .size_sectors = part->sectors,
        };
        disk->staged_partitions[i] = disk->partitions[i];
        if (!part->active && disk->free_part_idx < 0 && (info->has_mbr || i == 0)) {
            disk->free_part_idx = i;
        }
    }
    char size_str[32];
    disk_get_size_str(info->size, size_str, sizeof(size_str));
    /* Label depends on the name, so it must be done after setting the name */
    /* Keep the first character empty, it will be a `*` in case there is any pending change */
    snprintf(disk->label, sizeof(disk->label), "%c%.400s (%s)", info->pending ? '*' : ' ', disk->name, size_str);
    disk->revision = ++next_revision;
}

/* Synchronize the model, then surface either the error or the success text. */
static void report(disk_info_t* disk, zfs_status result, const char* success)
{
    disk_sync(disk);
    status(result != ZFS_OK ? error.message : success);
}

disk_err_t disks_refresh(void)
{
    /* Check if any disk has staged changes */
    for (int i = 0; i < s_state.disk_count; ++i) {
        if (s_state.disks[i].has_staged_changes) {
            status("Apply or cancel pending changes before refreshing");
            return ERR_INVALID;
        }
    }
    /* Backup the loaded disk images */
    disk_info_t backup_images[MAX_DISKS];
    int backup_count = 0;
    for (int i = 0; i < s_state.disk_count; ++i) {
        if (s_state.disks[i].is_image) {
            backup_images[backup_count++] = s_state.disks[i];
        } else {
            zfs_close(s_state.disks[i].handle);
        }
    }
    memset(s_state.disks, 0, sizeof(s_state.disks));
    s_state.disk_count = 0;
    s_state.selected_disk = -1;
    s_state.selected_partition = -1;
    zfs_info* found = calloc(MAX_DISKS, sizeof(*found));
    size_t count = 0;
    if (found == NULL) {
        status("Not enough memory to enumerate disks");
        return ERR_INVALID;
    }
    /* Refresh the disk list */
    zfs_status result = zfs_discover(found, MAX_DISKS - backup_count, &count, &error);
    for (size_t i = 0; result == ZFS_OK && i < count; ++i) {
        disk_info_t* disk = &s_state.disks[s_state.disk_count];
        if (zfs_open(found[i].path, false, &disk->handle, &error) == ZFS_OK) {
            disk_sync(disk);
            ++s_state.disk_count;
        }
    }
    free(found);
    /* Restore the saved loaded images at the end of the disk array */
    for (int i = 0; i < backup_count; ++i) {
        s_state.disks[s_state.disk_count++] = backup_images[i];
    }
    /* Check for a default disk */
    if (s_state.disk_count != 0) {
        s_state.selected_disk = 0;
    }
    status(result != ZFS_OK ? error.message : "Disk list refreshed");
    return result != ZFS_OK ? ERR_INVALID : ERR_SUCCESS;
}

void disks_close(void)
{
    for (int i = 0; i < s_state.disk_count; ++i) {
        zfs_close(s_state.disks[i].handle);
    }
    memset(&s_state, 0, sizeof(s_state));
    s_state.selected_disk = -1;
    s_state.selected_partition = -1;
}

int disk_create_mbr(disk_info_t* disk)
{
    zfs_status result = zfs_mbr_create(disk != NULL ? disk->handle : NULL, &error);
    report(disk, result, "MBR creation staged");
    return result == ZFS_OK;
}

int disk_partition_create(disk_info_t* disk, uint64_t size_bytes, uint32_t alignment, int* index)
{
    int slot = -1;
    zfs_status result = zfs_partition_create(disk != NULL ? disk->handle : NULL, size_bytes, alignment, &slot, &error);
    report(disk, result, "Partition creation staged");
    if (index != NULL) {
        *index = result == ZFS_OK ? slot : -1;
    }
    return result == ZFS_OK;
}

const char* disk_format_partition(disk_info_t* disk, int partition)
{
    zfs_status result = zfs_partition_format(disk != NULL ? disk->handle : NULL, partition, &error);
    report(disk, result, "Partition formatting staged");
    return result != ZFS_OK ? error.message : NULL;
}

void disk_delete_partition(disk_info_t* disk, int partition)
{
    report(disk,
           zfs_partition_delete(disk != NULL ? disk->handle : NULL, partition, &error),
           "Partition deletion staged");
}

void disk_revert_changes(disk_info_t* disk)
{
    if (disk == NULL || disk->handle == NULL) {
        return;
    }
    if (zfs_get_info(disk->handle)->new_image) {
        int index = (int) (disk - s_state.disks);
        zfs_close(disk->handle);
        memmove(disk, disk + 1, (size_t) (s_state.disk_count - index - 1) * sizeof(*disk));
        --s_state.disk_count;
        memset(&s_state.disks[s_state.disk_count], 0, sizeof(*disk));
        s_state.selected_disk = s_state.disk_count != 0 ? 0 : -1;
        s_state.selected_partition = -1;
        status("New image discarded");
        return;
    }
    /* Cancel all the changes made to the disk */
    report(disk, zfs_discard(disk->handle, &error), "Changes discarded");
}

const char* disk_write_changes(disk_info_t* disk)
{
    zfs_status result = zfs_apply(disk != NULL ? disk->handle : NULL, &error);
    report(disk, result, "Changes applied");
    return result != ZFS_OK ? error.message : NULL;
}

uint64_t disk_max_partition_size(disk_info_t* disk, uint32_t align, uint64_t* largest_free_addr)
{
    uint64_t free_bytes = 0;
    uint64_t aligned_addr = 0;
    if (disk != NULL) {
        zfs_partition_space(disk->handle, align, &aligned_addr, &free_bytes, &error);
    }
    if (largest_free_addr != NULL) {
        *largest_free_addr = aligned_addr;
    }
    return free_bytes;
}

int disk_open_image_file_path(disk_list_state_t* state, const char* file_path)
{
    if (state == NULL || file_path == NULL) {
        return -1;
    }
    /* Check if the image is already opened */
    for (int i = 0; i < state->disk_count; ++i) {
        if (strcmp(state->disks[i].path, file_path) == 0) {
            return i;
        }
    }
    if (state->disk_count >= MAX_DISKS) {
        status("Maximum number of disks reached");
        return -1;
    }
    disk_info_t* disk = &state->disks[state->disk_count];
    memset(disk, 0, sizeof(*disk));
    if (zfs_open(file_path, true, &disk->handle, &error) != ZFS_OK) {
        status(error.message);
        return -1;
    }
    disk_sync(disk);
    return state->disk_count++;
}

int disk_create_image(disk_list_state_t* state, const char* path, uint64_t size, bool init_mbr)
{
    if (state == NULL || path == NULL || *path == 0) {
        status("A disk image path is required");
        return -1;
    }
    if (state->disk_count >= MAX_DISKS) {
        status("Maximum number of disks reached");
        return -1;
    }
    disk_info_t* disk = &state->disks[state->disk_count];
    memset(disk, 0, sizeof(*disk));
    if (zfs_image_create(path, size, init_mbr, &disk->handle, &error) != ZFS_OK) {
        status(error.message);
        return -1;
    }
    disk_sync(disk);
    return state->disk_count++;
}

const char* disk_get_fs_type(uint8_t fs_byte)
{
    switch (fs_byte) {
    case 0x01:
        return "FAT12";
    case 0x04:
        return "FAT16";
    case 0x06:
        return "FAT16";
    case 0x0b:
        return "FAT32";
    case 0x0c:
        return "FAT32";
    case 0x07:
        return "NTFS";
    case 0x83:
        return "ext3";
    case 0x8e:
        return "ext4";
    case 0xa5:
        return "exFAT";
    case ZEALFS_TYPE:
        return "ZealFS v2";
    case 0x5e:
        return "UFS";
    case 0xaf:
        return "Mac OS Extended (HFS+)";
    case 0xc0:
        return "Mac OS Extended (HFSX)";
    case 0x17:
        return "Mac OS HFS";
    case 0x82:
        return "ext2";
    case 0xee:
        return "GPT";
    case 0xef:
        return "exFAT";
    default:
        return "Unknown";
    }
}

void disk_get_size_str(uint64_t size, char* buffer, int buffer_size)
{
    if (size < MB) {
        snprintf(buffer, buffer_size, "%.2f KiB", (float) size / KB);
    } else if (size < GB) {
        snprintf(buffer, buffer_size, "%.2f MiB", (float) size / MB);
    } else {
        snprintf(buffer, buffer_size, "%.2f GiB", (float) size / GB);
    }
}

/**
 * ============================================================================
 *                          FILESYSTEM ADAPTERS
 * ============================================================================
 */

int disk_fs_list(disk_info_t* disk, int partition, const char* path, zfs_entry** entries, size_t* count)
{
    if (disk == NULL) {
        status("No disk selected");
        return -1;
    }
    zfs_status result = zfs_list(disk->handle, partition, path, entries, count, &error);
    if (result != ZFS_OK) {
        status(error.message);
        return -1;
    }
    return 0;
}

int disk_fs_usage(disk_info_t* disk, int partition, uint64_t* used_bytes, uint64_t* total_bytes)
{
    uint64_t free_bytes = 0;
    uint64_t total = 0;
    if (disk == NULL || disk->handle == NULL ||
        zfs_usage(disk->handle, partition, &free_bytes, &total, NULL) != ZFS_OK) {
        if (used_bytes != NULL) {
            *used_bytes = 0;
        }
        if (total_bytes != NULL) {
            *total_bytes = 0;
        }
        return -1;
    }
    if (used_bytes != NULL) {
        *used_bytes = total >= free_bytes ? total - free_bytes : 0;
    }
    if (total_bytes != NULL) {
        *total_bytes = total;
    }
    return 0;
}

/* Shared tail of staged filesystem mutations: refresh the model and report. */
static int fs_mutation(disk_info_t* disk, zfs_status result, const char* success)
{
    report(disk, result, success);
    return result == ZFS_OK ? 0 : -1;
}

int disk_fs_mkdir(disk_info_t* disk, int partition, const char* path)
{
    if (disk == NULL) {
        status("No disk selected");
        return -1;
    }
    return fs_mutation(disk, zfs_mkdir(disk->handle, partition, path, &error), "Folder creation staged");
}

int disk_fs_remove(disk_info_t* disk, int partition, const char* path, bool directory)
{
    if (disk == NULL) {
        status("No disk selected");
        return -1;
    }
    return fs_mutation(disk, zfs_remove(disk->handle, partition, path, directory, &error), "Deletion staged");
}

int disk_fs_import(disk_info_t* disk, int partition, const char* const* host_paths, size_t count,
                   const char* destination, zfs_progress_fn progress, void* user)
{
    if (disk == NULL) {
        status("No disk selected");
        return -1;
    }
    zfs_callbacks callbacks = {
        .progress = progress,
        .log = NULL,
        .user = user,
    };
    zfs_import_request request = {
        .partition = partition,
        .host_paths = host_paths,
        .count = count,
        .path = destination,
        .overwrite = false,
        .callbacks = &callbacks,
    };
    zfs_status result = zfs_import_files(disk->handle, &request, &error);
    return fs_mutation(disk, result, count > 1 ? "Files imported (staged)" : "File imported (staged)");
}

int disk_fs_export(disk_info_t* disk, int partition, const char* path, const char* host_path,
                   zfs_progress_fn progress, void* user)
{
    if (disk == NULL) {
        status("No disk selected");
        return -1;
    }
    zfs_callbacks callbacks = {
        .progress = progress,
        .log = NULL,
        .user = user,
    };
    zfs_export_request request = {
        .partition = partition,
        .path = path,
        .host_path = host_path,
        .overwrite = true,
        .validate_only = false,
        .callbacks = &callbacks,
    };
    zfs_status result = zfs_export_files(disk->handle, &request, &error);
    if (result != ZFS_OK) {
        status(error.message);
        return -1;
    }
    status("Export complete");
    return 0;
}
