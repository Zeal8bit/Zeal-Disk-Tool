/**
 * SPDX-FileCopyrightText: 2025 Zeal 8-bit Computer <contact@zeal8bit.com>
 *
 * SPDX-License-Identifier: Apache-2.0 */
/* GUI adapter. All disk and filesystem behavior lives in zealfs_core. */
#include "disk.h"
#include "ui/statusbar.h"
#include "ui/tinyfiledialogs.h"
#include <stdlib.h>
static disk_list_state_t s_state = {.selected_disk = -1, .selected_partition = -1};
static zfs_error error;
static unsigned next_revision;
static const uint64_t s_valid_sizes[] = {32 * KB,  64 * KB,  128 * KB, 256 * KB, 512 * KB, 1 * MB,
                                         2 * MB,   4 * MB,   8 * MB,   16 * MB,  32 * MB,  64 * MB,
                                         128 * MB, 256 * MB, 512 * MB, 1 * GB,   2 * GB,   4 * GB};
static const char* const sizes[] = {"32KiB", "64KiB", "128KiB", "256KiB", "512KiB", "1MiB",   "2MiB", "4MiB", "8MiB",
                                    "16MiB", "32MiB", "64MiB",  "128MiB", "256MiB", "512MiB", "1GiB", "2GiB", "4GiB"};

const char* const* disk_get_partition_size_list(int* count)
{
    if (count) {
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
    if (!disk || !disk->handle) {
        return;
    }
    const zfs_info* info = zfs_get_info(disk->handle);
    disk->size_bytes = info->size;
    disk->valid = !info->failed;
    disk->is_image = info->image;
    disk->has_mbr = info->has_mbr;
    disk->has_staged_changes = info->pending;
    snprintf(disk->path, sizeof(disk->path), "%s", info->path);
    snprintf(disk->name, sizeof(disk->name), "%s", disk_get_basename(info->path));
    disk->free_part_idx = -1;
    for (int i = 0; i < 4; ++i) {
        const zfs_partition* part = &info->partitions[i];
        disk->partitions[i] = (partition_t){
            .active = part->active, .type = part->type, .start_lba = part->start_lba, .size_sectors = part->sectors};
        disk->staged_partitions[i] = disk->partitions[i];
        if (!part->active && disk->free_part_idx < 0 && (info->has_mbr || i == 0)) {
            disk->free_part_idx = i;
        }
    }
    char size_str[32];
    disk_get_size_str(info->size, size_str, sizeof(size_str));
    snprintf(disk->label, sizeof(disk->label), "%c%.400s (%s)", info->pending ? '*' : ' ', disk->name, size_str);
    disk->revision = ++next_revision;
}

static void report(disk_info_t* d, zfs_status s, const char* success)
{
    disk_sync(d);
    ui_statusbar_print(s ? error.message : success);
}

disk_err_t disks_refresh(void)
{
    for (int i = 0; i < s_state.disk_count; ++i) {
        if (s_state.disks[i].has_staged_changes) {
            ui_statusbar_print("Apply or cancel pending changes before refreshing");
            return ERR_INVALID;
        }
    }
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
    if (!found) {
        return ERR_INVALID;
    }
    zfs_status err = zfs_discover(found, MAX_DISKS - backup_count, &count, &error);
    for (size_t i = 0; !err && i < count; ++i) {
        disk_info_t* disk = &s_state.disks[s_state.disk_count];
        if (!zfs_open(found[i].path, false, &disk->handle, &error)) {
            disk_sync(disk);
            ++s_state.disk_count;
        }
    }
    free(found);
    for (int i = 0; i < backup_count; ++i) {
        s_state.disks[s_state.disk_count++] = backup_images[i];
    }
    if (s_state.disk_count) {
        s_state.selected_disk = 0;
    }
    ui_statusbar_print(err ? error.message : "Disk list refreshed");
    return err ? ERR_INVALID : ERR_SUCCESS;
}

void disks_close(void)
{
    for (int i = 0; i < s_state.disk_count; ++i) {
        zfs_close(s_state.disks[i].handle);
    }
    memset(&s_state, 0, sizeof(s_state));
    s_state.selected_disk = -1;
}

int disk_create_mbr(disk_info_t* disk)
{
    zfs_status ret = zfs_mbr_create(disk ? disk->handle : NULL, &error);
    report(disk, ret, "MBR creation staged");
    return !ret;
}

void disk_allocate_partition(disk_info_t* disk, uint32_t lba, uint32_t sectors_count)
{
    (void) lba;
    uint32_t alignment = disk && disk->allocation_alignment ? disk->allocation_alignment : 512;
    int index;
    zfs_status s =
        zfs_partition_create(disk ? disk->handle : NULL, (uint64_t) sectors_count * 512, alignment, &index, &error);
    report(disk, s, "Partition creation staged");
}

const char* disk_format_partition(disk_info_t* disk, int partition)
{
    zfs_status s = zfs_partition_format(disk ? disk->handle : NULL, partition, &error);
    report(disk, s, "Partition formatting staged");
    return s ? error.message : NULL;
}

void disk_delete_partition(disk_info_t* disk, int partition)
{
    report(disk, zfs_partition_delete(disk ? disk->handle : NULL, partition, &error), "Partition deletion staged");
}

void disk_revert_changes(disk_info_t* disk)
{
    if (disk && zfs_get_info(disk->handle)->new_image) {
        int index = (int) (disk - s_state.disks);
        zfs_close(disk->handle);
        memmove(disk, disk + 1, (size_t) (s_state.disk_count - index - 1) * sizeof(*disk));
        --s_state.disk_count;
        memset(&s_state.disks[s_state.disk_count], 0, sizeof(*disk));
        s_state.selected_disk = s_state.disk_count ? 0 : -1;
        s_state.selected_partition = -1;
        ui_statusbar_print("New image discarded");
        return;
    }
    report(disk, zfs_discard(disk ? disk->handle : NULL, &error), "Changes discarded");
}

const char* disk_write_changes(disk_info_t* disk)
{
    zfs_status s = zfs_apply(disk ? disk->handle : NULL, &error);
    report(disk, s, "Changes applied");
    return s ? error.message : NULL;
}

uint64_t disk_max_partition_size(disk_info_t* disk, uint32_t align, uint64_t* largest_free_addr)
{
    uint64_t free_bytes = 0, aligned_addr = 0;
    if (disk) {
        disk->allocation_alignment = align;
        zfs_partition_space(disk->handle, align, &aligned_addr, &free_bytes, &error);
    }
    if (largest_free_addr) {
        *largest_free_addr = aligned_addr;
    }
    return free_bytes;
}

int disk_open_image_file_path(disk_list_state_t* state, const char* file_path)
{
    for (int i = 0; i < state->disk_count; ++i) {
        if (!strcmp(state->disks[i].path, file_path)) {
            return i;
        }
    }
    if (state->disk_count >= MAX_DISKS) {
        ui_statusbar_print("Maximum number of disks reached");
        return -1;
    }
    disk_info_t* disk = &state->disks[state->disk_count];
    memset(disk, 0, sizeof(*disk));
    if (zfs_open(file_path, true, &disk->handle, &error)) {
        ui_statusbar_print(error.message);
        return -1;
    }
    disk_sync(disk);
    return state->disk_count++;
}

int disk_open_image_file(disk_list_state_t* state)
{
    const char* filter_patterns[] = {"*.img"};
    const char* file_path = tinyfd_openFileDialog("Open disk image", "", 1, filter_patterns, "Disk images", 0);
    return file_path ? disk_open_image_file_path(state, file_path) : -1;
}

int disk_create_image(disk_list_state_t* state, const char* path, uint64_t size, bool init_mbr)
{
    if (state->disk_count >= MAX_DISKS) {
        ui_statusbar_print("Maximum number of disks reached");
        return -1;
    }
    disk_info_t* disk = &state->disks[state->disk_count];
    memset(disk, 0, sizeof(*disk));
    if (zfs_image_create(path, size, init_mbr, &disk->handle, &error)) {
        ui_statusbar_print(error.message);
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
    case 0x5a:
        return "ZealFS";
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
