/* SPDX-FileCopyrightText: 2025 Zeal 8-bit Computer <contact@zeal8bit.com>
 * SPDX-License-Identifier: Apache-2.0 */
/* GUI adapter. All disk and filesystem behavior lives in zealfs_core. */
#include "disk.h"
#include "ui/statusbar.h"
#include "ui/tinyfiledialogs.h"
#include <stdlib.h>
static disk_list_state_t state = {.selected_disk = -1, .selected_partition = -1};
static zfs_error error;
static unsigned next_revision;
static const uint64_t sizes[] = {32 * KB,  64 * KB,  128 * KB, 256 * KB, 512 * KB, 1 * MB,
                                 2 * MB,   4 * MB,   8 * MB,   16 * MB,  32 * MB,  64 * MB,
                                 128 * MB, 256 * MB, 512 * MB, 1 * GB,   2 * GB,   4 * GB};
static const char *const size_names[] = {"32KiB",  "64KiB",  "128KiB", "256KiB", "512KiB", "1MiB",
                                         "2MiB",   "4MiB",   "8MiB",   "16MiB",  "32MiB",  "64MiB",
                                         "128MiB", "256MiB", "512MiB", "1GiB",   "2GiB",   "4GiB"};
const char *const *disk_get_partition_size_list(int *count) {
    if (count)
        *count = (int)DIM(sizes);
    return size_names;
}
uint64_t disk_get_size_of_idx(int index) {
    return index >= 0 && (size_t)index < DIM(sizes) ? sizes[index] : 0;
}
disk_list_state_t *disk_get_state(void) { return &state; }
void disk_sync(disk_info_t *disk) {
    if (!disk || !disk->handle)
        return;
    const zfs_info *info = zfs_get_info(disk->handle);
    disk->size_bytes = info->size;
    disk->valid = !info->failed;
    disk->is_image = info->image;
    disk->has_mbr = info->has_mbr;
    disk->has_staged_changes = info->pending;
    snprintf(disk->path, sizeof(disk->path), "%s", info->path);
    snprintf(disk->name, sizeof(disk->name), "%s", disk_get_basename(info->path));
    disk->free_part_idx = -1;
    for (int i = 0; i < 4; ++i) {
        const zfs_partition *p = &info->partitions[i];
        disk->partitions[i] = (partition_t){
            .active = p->active, .type = p->type, .start_lba = p->start_lba, .size_sectors = p->sectors};
        disk->staged_partitions[i] = disk->partitions[i];
        if (!p->active && disk->free_part_idx < 0 && (info->has_mbr || i == 0))
            disk->free_part_idx = i;
    }
    char size[32];
    disk_get_size_str(info->size, size, sizeof(size));
    snprintf(disk->label, sizeof(disk->label), "%c%.400s (%s)", info->pending ? '*' : ' ', disk->name, size);
    disk->revision = ++next_revision;
}
static void report(disk_info_t *d, zfs_status s, const char *success) {
    disk_sync(d);
    ui_statusbar_print(s ? error.message : success);
}
disk_err_t disks_refresh(void) {
    for (int i = 0; i < state.disk_count; ++i)
        if (state.disks[i].has_staged_changes) {
            ui_statusbar_print("Apply or cancel pending changes before refreshing");
            return ERR_INVALID;
        }
    disk_info_t images[MAX_DISKS];
    int n = 0;
    for (int i = 0; i < state.disk_count; ++i) {
        if (state.disks[i].is_image)
            images[n++] = state.disks[i];
        else
            zfs_close(state.disks[i].handle);
    }
    memset(state.disks, 0, sizeof(state.disks));
    state.disk_count = 0;
    state.selected_disk = -1;
    state.selected_partition = -1;
    zfs_info *found = calloc(MAX_DISKS, sizeof(*found));
    size_t count = 0;
    if (!found)
        return ERR_INVALID;
    zfs_status s = zfs_discover(found, MAX_DISKS - n, &count, &error);
    for (size_t i = 0; !s && i < count; ++i) {
        disk_info_t *d = &state.disks[state.disk_count];
        if (!zfs_open(found[i].path, false, &d->handle, &error)) {
            disk_sync(d);
            ++state.disk_count;
        }
    }
    free(found);
    for (int i = 0; i < n; ++i)
        state.disks[state.disk_count++] = images[i];
    if (state.disk_count)
        state.selected_disk = 0;
    ui_statusbar_print(s ? error.message : "Disk list refreshed");
    return s ? ERR_INVALID : ERR_SUCCESS;
}
void disks_close(void) {
    for (int i = 0; i < state.disk_count; ++i)
        zfs_close(state.disks[i].handle);
    memset(&state, 0, sizeof(state));
    state.selected_disk = -1;
}
int disk_create_mbr(disk_info_t *d) {
    zfs_status s = zfs_mbr_create(d ? d->handle : NULL, &error);
    report(d, s, "MBR creation staged");
    return !s;
}
void disk_allocate_partition(disk_info_t *d, uint32_t lba, uint32_t sectors) {
    (void)lba;
    uint32_t alignment = d && d->allocation_alignment ? d->allocation_alignment : 512;
    int index;
    zfs_status s =
        zfs_partition_create(d ? d->handle : NULL, (uint64_t)sectors * 512, alignment, &index, &error);
    report(d, s, "Partition creation staged");
}
const char *disk_format_partition(disk_info_t *d, int part) {
    zfs_status s = zfs_partition_format(d ? d->handle : NULL, part, &error);
    report(d, s, "Partition formatting staged");
    return s ? error.message : NULL;
}
void disk_delete_partition(disk_info_t *d, int part) {
    report(d, zfs_partition_delete(d ? d->handle : NULL, part, &error), "Partition deletion staged");
}
void disk_revert_changes(disk_info_t *d) {
    if (d && zfs_get_info(d->handle)->new_image) {
        int index = (int)(d - state.disks);
        zfs_close(d->handle);
        memmove(d, d + 1, (size_t)(state.disk_count - index - 1) * sizeof(*d));
        --state.disk_count;
        memset(&state.disks[state.disk_count], 0, sizeof(*d));
        state.selected_disk = state.disk_count ? 0 : -1;
        state.selected_partition = -1;
        ui_statusbar_print("New image discarded");
        return;
    }
    report(d, zfs_discard(d ? d->handle : NULL, &error), "Changes discarded");
}
const char *disk_write_changes(disk_info_t *d) {
    zfs_status s = zfs_apply(d ? d->handle : NULL, &error);
    report(d, s, "Changes applied");
    return s ? error.message : NULL;
}
uint64_t disk_max_partition_size(disk_info_t *d, uint32_t alignment, uint64_t *start) {
    uint64_t size = 0, address = 0;
    if (d) {
        d->allocation_alignment = alignment;
        zfs_partition_space(d->handle, alignment, &address, &size, &error);
    }
    if (start)
        *start = address;
    return size;
}
int disk_open_image_file_path(disk_list_state_t *s, const char *path) {
    for (int i = 0; i < s->disk_count; ++i)
        if (!strcmp(s->disks[i].path, path))
            return i;
    if (s->disk_count >= MAX_DISKS) {
        ui_statusbar_print("Maximum number of disks reached");
        return -1;
    }
    disk_info_t *d = &s->disks[s->disk_count];
    memset(d, 0, sizeof(*d));
    if (zfs_open(path, true, &d->handle, &error)) {
        ui_statusbar_print(error.message);
        return -1;
    }
    disk_sync(d);
    return s->disk_count++;
}
int disk_open_image_file(disk_list_state_t *s) {
    const char *filters[] = {"*.img"};
    const char *p = tinyfd_openFileDialog("Open disk image", "", 1, filters, "Disk images", 0);
    return p ? disk_open_image_file_path(s, p) : -1;
}
int disk_create_image(disk_list_state_t *s, const char *path, uint64_t size, bool mbr) {
    if (s->disk_count >= MAX_DISKS) {
        ui_statusbar_print("Maximum number of disks reached");
        return -1;
    }
    disk_info_t *d = &s->disks[s->disk_count];
    memset(d, 0, sizeof(*d));
    if (zfs_image_create(path, size, mbr, &d->handle, &error)) {
        ui_statusbar_print(error.message);
        return -1;
    }
    disk_sync(d);
    return s->disk_count++;
}
const char *disk_get_fs_type(uint8_t fs_byte) {
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

void disk_get_size_str(uint64_t size, char *buffer, int buffer_size) {
    if (size < MB) {
        snprintf(buffer, buffer_size, "%.2f KiB", (float)size / KB);
    } else if (size < GB) {
        snprintf(buffer, buffer_size, "%.2f MiB", (float)size / MB);
    } else {
        snprintf(buffer, buffer_size, "%.2f GiB", (float)size / GB);
    }
}
