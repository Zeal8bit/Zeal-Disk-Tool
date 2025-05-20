/**
 * SPDX-FileCopyrightText: 2025 Zeal 8-bit Computer <contact@zeal8bit.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include "disk.h"
#include "ui/statusbar.h"
#include "ui/tinyfiledialogs.h"
#include "zealfs_v2.h"

#define ALIGN_UP(size,bound) (((size) + (bound) - 1) & ~((bound) - 1))

static disk_list_state_t s_state;

static const uint64_t s_valid_sizes[] = {
    32*KB, 64*KB, 128*KB, 256*KB, 512*KB,
    1*MB, 2*MB, 4*MB, 8*MB, 16*MB, 32*MB, 64*MB, 128*MB, 256*MB, 512*MB,
    1*GB, 2*GB, 4*GB
};


static void disk_generate_label(disk_info_t* disk)
{
    char size_str[128];
    disk_get_size_str(disk->size_bytes, size_str, sizeof(size_str));
    /* Keep the first character empty, it will be a `*` in case there is any pending change */
    snprintf(disk->label, DISK_LABEL_LEN, " %.*s (%s)", (int) sizeof(disk->name), disk->name, size_str);
}

const char* const *disk_get_partition_size_list(int* count)
{
    static const char* const sizes[] = {
        "32KiB", "64KiB", "128KiB", "256KiB", "512KiB",
        "1MiB", "2MiB", "4MiB", "8MiB", "16MiB", "32MiB", "64MiB", "128MiB", "256MiB", "512MiB",
        "1GiB", "2GiB", "4GiB"
    };
    if (count) {
        *count = DIM(sizes);
    }
    return sizes;
}

uint64_t disk_get_size_of_idx(int index)
{
    if (index < 0 || index >= DIM(s_valid_sizes)) {
        return 0;
    }
    return s_valid_sizes[index];
}


disk_list_state_t* disk_get_state(void)
{
    return &s_state;
}


disk_err_t disks_refresh(void)
{
    /* Check if the current disk has unstaged changes */
    disk_info_t* current = disk_get_current(&s_state);
    if (current && current->has_staged_changes) {
        ui_statusbar_print("Cannot refresh: unstaged changes detected!");
        return ERR_INVALID;
    }

    /* Backup the loaded disk images */
    disk_info_t backup_images[MAX_DISKS];
    int backup_count = 0;
    for (int i = 0; i < s_state.disk_count; ++i) {
        if (s_state.disks[i].is_image) {
            backup_images[backup_count++] = s_state.disks[i];
        }
    }

    /* Refresh the disk list */
    disk_err_t err = disk_list(s_state.disks, MAX_DISKS, &s_state.disk_count);
    if(err != ERR_SUCCESS) {
        return err;
    }

    s_state.selected_disk = -1;

    /* Construct the labels for the disks */
    for (int i = 0; i < s_state.disk_count; ++i) {
        disk_info_t* disk = &s_state.disks[i];
        disk_generate_label(disk);
        printf("[DISK] Refreshed disk: %s\n", disk->label);
        disk_parse_mbr_partitions(disk);

        /* Check for a default disk */
        if (s_state.selected_disk == -1 && disk->valid) {
            s_state.selected_disk = i;
        }
    }

    /* Restore the saved loaded images at the end of the disk array */
    for (int i = 0; i < backup_count; ++i) {
        if (s_state.disk_count >= MAX_DISKS) {
            printf("[DISK] Maximum number of disks reached while restoring images!");
            break;
        }
        printf("[DISK] Refreshed image: %s\n", backup_images[i].label);
        s_state.disks[s_state.disk_count++] = backup_images[i];
    }

    if (s_state.disk_count == 0) {
        ui_statusbar_print("No disk found!\n");
    } else {
        ui_statusbar_print("Disk list refreshed successfully\n");
    }

    return ERR_SUCCESS;
}


static int disk_is_invalid(disk_info_t* disk)
{
    if (disk == NULL || !disk->valid) {
        ui_statusbar_printf("Invalid disk %s", disk->name);
        return true;
    }
    return false;
}

static int disk_find_free_partition(disk_info_t* disk)
{
    if (!disk->has_mbr) {
        /* No MBR, only allow the first partition to be used */
        return disk->staged_partitions[0].active ? -1 : 0;
    }

    /* Find free partition */
    for (int i = 0; i < MAX_PART_COUNT; ++i) {
        if (!disk->staged_partitions[i].active) {
            return i;
        }
    }
    return -1;
}

static void disk_write_mbr_entry(uint8_t *entry, const partition_t *part)
{
    entry[0] = 0x00;
    /* CHS fields not used */
    entry[1] = 0xFF;
    entry[2] = 0xFF;
    entry[3] = 0xFF;
    /* Partition type */
    entry[4] = part->type;
    /* CHS end not used either */
    entry[5] = 0xFF;
    entry[6] = 0xFF;
    entry[7] = 0xFF;
    /* Start LBA */
    entry[8]  = (uint8_t)(part->start_lba & 0xff);
    entry[9]  = (uint8_t)((part->start_lba >> 8) & 0xff);
    entry[10] = (uint8_t)((part->start_lba >> 16) & 0xff);
    entry[11] = (uint8_t)((part->start_lba >> 24) & 0xff);
    /* Size in sectors */
    entry[12] = (uint8_t)(part->size_sectors & 0xff);
    entry[13] = (uint8_t)((part->size_sectors >> 8) & 0xff);
    entry[14] = (uint8_t)((part->size_sectors >> 16) & 0xff);
    entry[15] = (uint8_t)((part->size_sectors >> 24) & 0xff);
}


int disk_create_mbr(disk_info_t *disk)
{
    if (disk == NULL || disk->has_mbr || disk->has_staged_changes || !disk->valid) {
        return 0;
    }
    disk->has_mbr = true;
    /* The OS specific disk layer requires that the `has_staged_changes` is set */
    disk->has_staged_changes = true;
    /* Reset the MBR and set the signature only */
    memset(disk->staged_mbr, 0, sizeof(disk->staged_mbr));
    disk->staged_mbr[510] = 0x55;
    disk->staged_mbr[511] = 0xAA;

    const char* ret = disk_write_changes(disk);
    disk->has_staged_changes = false;
    if (ret == NULL) {
        memcpy(disk->mbr, disk->staged_mbr, sizeof(disk->staged_mbr));
        disk_parse_mbr_partitions(disk);
        return 1;
    }
    return 0;
}


void disk_allocate_partition(disk_info_t *disk, uint32_t lba, uint32_t sectors_count)
{
    const uint64_t part_size_bytes = sectors_count * DISK_SECTOR_SIZE;

    if (disk_is_invalid(disk)) {
        return;
    }

    if (disk->free_part_idx == -1 || (!disk->has_mbr && disk->free_part_idx > 0)) {
        ui_statusbar_print("Error: Could not find a free partition!");
        return;
    }
    if (disk->free_part_idx < 0 || disk->free_part_idx >= 4) {
        ui_statusbar_print("Error: Free partition index out of bounds!");
        return;
    }

    partition_t* part = &disk->staged_partitions[disk->free_part_idx];
    assert(!part->active);
    printf("[DISK] Allocating ZealFS in partition %d\n", disk->free_part_idx);
    disk->has_staged_changes = true;
    part->active = true;
    part->start_lba = lba;
    part->type = 0x5a;
    part->size_sectors = sectors_count;

    /* Encode the partition in the staged MBR */
    uint8_t *entry = &disk->staged_mbr[MBR_PART_ENTRY_BEGIN + disk->free_part_idx * MBR_PART_ENTRY_SIZE];
    disk_write_mbr_entry(entry, part);

    /* Format the partition with data. We need to allocate 3 pages at all time:
     * - One for the header
     * - Two for the FAT
     */
    assert(part->data == NULL && part->data_len == 0);
    const int page_size = zealfsv2_page_size(part_size_bytes);
    part->data_len = page_size * 3;
    part->data = calloc(3, page_size);
    if (part->data == NULL) {
        printf("[DISK] Could not allocate memory!\n");
        exit(1);
    } else {
        printf("[DISK] Allocated %d bytes (3 pages)\n", 3*page_size);
    }
    zealfsv2_format(part->data, part_size_bytes);
    printf("[DISK] Partition %d data: %p, length: %d\n", disk->free_part_idx, part->data, part->data_len);

    /* Inform the user about the operation */
    ui_statusbar_printf("Partition %d allocated", disk->free_part_idx);

    /* Reuse the free partition index */
    disk->free_part_idx = disk_find_free_partition(disk);
}


const char* disk_format_partition(disk_info_t* disk, int partition)
{
    if (disk_is_invalid(disk)) {
        return "Please select a valid disk!";
    }
    if (partition < 0 || partition >= MAX_PART_COUNT || !disk->staged_partitions[partition].active) {
        return "Please select a valid partition!";
    }

    partition_t* part = &disk->staged_partitions[partition];
    const uint64_t part_size_bytes = part->size_sectors * DISK_SECTOR_SIZE;
    const int page_size = zealfsv2_page_size(part_size_bytes);
    /* Format the partition with data. We need to allocate 3 pages at all time:
     * - One for the header
     * - Two for the FAT
     */
    free(part->data);
    disk->has_staged_changes = true;
    part->type = 0x5a;
    part->data_len = page_size * 3;
    part->data = calloc(3, page_size);
    if (part->data == NULL) {
        printf("[DISK][FORMAT] Could not allocate memory!\n");
        exit(1);
    } else {
        printf("[DISK][FORMAT] Allocated %d bytes (3 pages)\n", 3*page_size);
    }
    zealfsv2_format(part->data, part_size_bytes);
    printf("[DISK][FORMAT] Partition %d data: %p, length: %d\n", disk->free_part_idx, part->data, part->data_len);

    ui_statusbar_printf("Partition %d formatted successfully", partition);

    return NULL;
}


void disk_delete_partition(disk_info_t* disk, int partition)
{
    if (disk_is_invalid(disk) || partition < 0 || partition >= MAX_PART_COUNT) {
        return;
    }

    partition_t* part = &disk->staged_partitions[partition];
    if (part->active) {
        disk->has_staged_changes = true;
        printf("[DISK] Deleting partition %d\n", partition);
        free(part->data);
        memset(part, 0, sizeof(partition_t));
        /* If the disk has no free partition, the current one is free now! */
        if (disk->free_part_idx == -1) {
            disk->free_part_idx = partition;
        }

        /* Encode the partition in the staged MBR */
        uint8_t *entry = &disk->staged_mbr[MBR_PART_ENTRY_BEGIN + partition * MBR_PART_ENTRY_SIZE];
        disk_write_mbr_entry(entry, part);

        ui_statusbar_printf("Partition %d deleted", partition);
    }
}


static void disk_free_staged_partitions_data(disk_info_t* disk)
{
    /* Free the pointers in the partitions */
    for (int i = 0; i < MAX_PART_COUNT; i++) {
        free(disk->staged_partitions[i].data);
        disk->staged_partitions[i].data = NULL;
        disk->staged_partitions[i].data_len = 0;
    }
}


void disk_revert_changes(disk_info_t* disk)
{
    /* Cancel all the changes made to the disk */
    if (!disk->has_staged_changes) {
        ui_statusbar_print("No changes on this disk");
        return;
    }

    /* Free the staged partitions data BEFORE replacing them */
    disk_free_staged_partitions_data(disk);

    /* Create a mirror for the RAM changes */
    disk->has_staged_changes = false;
    memcpy(disk->staged_mbr, disk->mbr, sizeof(disk->mbr));
    memcpy(disk->staged_partitions, disk->partitions, sizeof(disk->partitions));
    /* Make sure to call the function AFTER restoring the stages partitions */
    disk->free_part_idx = disk_find_free_partition(disk);
    ui_statusbar_print("Changes reverted");
}


void disk_apply_changes(disk_info_t* disk)
{
    if (disk_is_invalid(disk)) {
        return;
    }
    disk->has_staged_changes = false;
    /* Before copying the staged partitions as the real partitions, make sure to
     * free the pointers and sizes (since they have been copied to the disk) */
    disk_free_staged_partitions_data(disk);
    memcpy(disk->mbr, disk->staged_mbr, sizeof(disk->mbr));
    memcpy(disk->partitions, disk->staged_partitions, sizeof(disk->partitions));
    ui_statusbar_print("Changes saved to disk!");
}


const char* disk_get_fs_type(uint8_t fs_byte)
{
    switch (fs_byte) {
        case 0x01:  return "FAT12";
        case 0x04:  return "FAT16";
        case 0x06:  return "FAT16";
        case 0x0b:  return "FAT32";
        case 0x0c:  return "FAT32";
        case 0x07:  return "NTFS";
        case 0x83:  return "ext3";
        case 0x8e:  return "ext4";
        case 0xa5:  return "exFAT";
        case 0x5a:  return "ZealFS";
        case 0x5e:  return "UFS";
        case 0xaf:  return "Mac OS Extended (HFS+)";
        case 0xc0:  return "Mac OS Extended (HFSX)";
        case 0x17:  return "Mac OS HFS";
        case 0x82:  return "ext2";
        case 0xee:  return "GPT";
        case 0xef:  return "exFAT";
        default:    return "Unknown";
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
 * @brief Populate the `partitions` field in the given `disk_info_t`.
 * This will sort the partitions by LBA address.
 */
void disk_parse_mbr_partitions(disk_info_t *disk)
{
    int free_part_idx = -1;

    if (!disk->has_mbr) {
        memset(disk->partitions, 0, sizeof(disk->partitions));
        if (disk->mbr[0] == ZEALFS_TYPE && disk->mbr[1] == 2) {
            disk->partitions[0] = (partition_t) {
                .active       = true,
                .type         = ZEALFS_TYPE,
                .start_lba    = 0,
                .size_sectors = disk->size_bytes / DISK_SECTOR_SIZE,
            };
        } else {
            /* No ZealFS partition found, mark the first partition as free */
            free_part_idx = 0;
        }
    } else {
        for (int i = 0; i < MAX_PART_COUNT; ++i) {
            const uint8_t *entry = disk->mbr + 446 + i * 16;

            partition_t *p  = &disk->partitions[i];
            p->type         = entry[4];
            p->start_lba    = entry[8] | (entry[9] << 8) | (entry[10] << 16) | (entry[11] << 24);
            p->size_sectors = entry[12] | (entry[13] << 8) | (entry[14] << 16) | (entry[15] << 24);
            /* Be very conservative to make sure nothing is erased! */
            p->active       = (entry[0] & 0x80) != 0 || p->type != 0 ||
                            p->start_lba != 0 || p->size_sectors != 0;

            if (!p->active && free_part_idx == -1) {
                free_part_idx = i;
            }
        }
        memcpy(disk->staged_mbr, disk->mbr, sizeof(disk->mbr));
    }

    /* Create a mirror for the RAM changes */
    disk->has_staged_changes = false;
    disk->free_part_idx = free_part_idx;
    memcpy(disk->staged_partitions, disk->partitions, sizeof(disk->partitions));
}


static uint64_t disk_largest_free_space(disk_info_t *disk, uint64_t *largest_free_addr)
{
    /* Total disk sector in bytes */
    const uint32_t disk_size_sectors = disk->size_bytes / DISK_SECTOR_SIZE;
    uint32_t largest_free_space = 0;
    /* Make sure the first sector is taken (MBR), so start checking at sector 1 */
    uint32_t largest_start_address = 1;
    /* Check all the gaps between the sections and keep the maximum in `largest_free_space` */
    uint32_t previous_end_lba = largest_start_address;

    /* If the disk has no MBR, the disk size is the maximum */
    if (!disk->has_mbr) {
        if (largest_free_addr) {
            *largest_free_addr = 0;
        }
        return disk->size_bytes;
    }

    /* Build a sorted list of active partition indexes */
    int sorted_indexes[MAX_PART_COUNT];
    int sorted_count = 0;

    for (int i = 0; i < MAX_PART_COUNT; i++) {
        if (disk->staged_partitions[i].active) {
            int j = sorted_count;
            while (j > 0 &&
                   disk->staged_partitions[sorted_indexes[j - 1]].start_lba >
                   disk->staged_partitions[i].start_lba) {
                sorted_indexes[j] = sorted_indexes[j - 1];
                j--;
            }
            sorted_indexes[j] = i;
            sorted_count++;
        }
    }

    for (int i = 0; i < sorted_count; i++) {
        partition_t *partition = &disk->staged_partitions[sorted_indexes[i]];
        assert(partition->active);

        const uint32_t start_lba = partition->start_lba;
        const uint32_t end_lba = start_lba + partition->size_sectors;

        /* Calculate the free space between the previous partition and the current one */
        if (start_lba > previous_end_lba) {
            const uint32_t free_space = start_lba - previous_end_lba;
            if (free_space > largest_free_space) {
                largest_free_space = free_space;
                largest_start_address = previous_end_lba;
            }
        }

        previous_end_lba = end_lba;
    }

    /* Check for free space after the last partition until the end of the disk */
    const uint32_t free_space = disk_size_sectors - previous_end_lba;
    if (free_space > largest_free_space) {
        largest_free_space = free_space;
        largest_start_address = previous_end_lba;
    }

    if (largest_free_addr) {
        *largest_free_addr = largest_start_address * DISK_SECTOR_SIZE;
    }

    return largest_free_space * DISK_SECTOR_SIZE;
}


/**
 * @brief Get the number of valid entries for a new partition
 */
uint64_t disk_max_partition_size(disk_info_t *disk, uint32_t align, uint64_t *largest_free_addr)
{
    uint64_t free_start_addr = 0;
    uint64_t free_bytes = disk_largest_free_space(disk, &free_start_addr);

    /* Try to align the address on the given alignment */
    uint64_t aligned_addr = ALIGN_UP(free_start_addr, align);
    uint64_t wasted_bytes = aligned_addr - free_start_addr;
    free_bytes -= wasted_bytes;

    if (largest_free_addr) {
        *largest_free_addr = aligned_addr;
    }

    return free_bytes;
}


static bool disk_image_opened(disk_list_state_t* state, const char* path, int* index)
{
    /* Check if the image is already opened */
    for (int i = 0; i < state->disk_count; i++) {
        disk_info_t* disk = &state->disks[i];
        if (disk->is_image && strcmp(disk->path, path) == 0) {
            if (index) {
                *index = i;
            }
            return -1;
        }
    }

    return 0;
}


int disk_open_image_file(disk_list_state_t* state)
{
    if (state->disk_count >= MAX_DISKS) {
        ui_statusbar_print("Maximum number of disks reached!");
        return -1;
    }

    const char* filter_patterns[] = { "*.img" };
    const char* file_path = tinyfd_openFileDialog(
        "Open Disk Image",
        "",
        1,
        filter_patterns,
        "Disk Image Files",
        0
    );

    if (!file_path) {
        ui_statusbar_print("No file selected");
        return -1;
    }

    /* Check if the image is already opened */
    int index = 0;
    if (disk_image_opened(state, file_path, &index)) {
        ui_statusbar_print("Image is already opened!");
        return index;
    }

    FILE* file = fopen(file_path, "rb");
    if (!file) {
        ui_statusbar_printf("Failed to open file: %s", file_path);
        return -1;
    }

    disk_info_t* disk = &state->disks[state->disk_count];
    memset(disk, 0, sizeof(disk_info_t));
    /* Get the size of the file by seeking to the end */
    fseek(file, 0, SEEK_END);
    disk->size_bytes = ftell(file);
    rewind(file);

    if (fread(disk->mbr, 1, sizeof(disk->mbr), file) != sizeof(disk->mbr)) {
        ui_statusbar_printf("Failed to read MBR from file: %s", file_path);
        fclose(file);
        return -1;
    }

    fclose(file);
    disk->valid = true;
    disk->is_image = true;
    disk->has_mbr = (disk->mbr[510] == 0x55 && disk->mbr[511] == 0xAA);
    disk_parse_mbr_partitions(disk);

    snprintf(disk->path, sizeof(disk->name), "%s", file_path);
    /* Extract the filename out of the path */
    const char* base_name = disk_get_basename(file_path);
    snprintf(disk->name, sizeof(disk->name), "%s", base_name);
    /* Label depends on the name, so it must be done after setting the name */
    disk_generate_label(disk);

    state->disk_count++;
    ui_statusbar_print("Disk image loaded successfully!");

    return state->disk_count - 1;
}


int disk_create_image(disk_list_state_t* state, const char* path, uint64_t size, bool init_mbr)
{
    int new_index = state->disk_count;
    /* Allocate a buffer for the MBR and initialize it to 0 */
    uint8_t mbr[DISK_SECTOR_SIZE] = {0};

    if (new_index >= MAX_DISKS) {
        ui_statusbar_print("Maximum number of disks reached!");
        return -1;
    }

    /* Check if the image is already opened */
    if (disk_image_opened(state, path, &new_index)) {
        ui_statusbar_print("Image is already opened!");
    }

    FILE* file = fopen(path, "wb");
    if (!file) {
        ui_statusbar_printf("Failed to create file: %s", path);
        return -1;
    }

    if (init_mbr) {
        /* Set the MBR signature */
        mbr[510] = 0x55;
        mbr[511] = 0xAA;

        /* Write the MBR to the file */
        if (fwrite(mbr, 1, DISK_SECTOR_SIZE, file) != DISK_SECTOR_SIZE) {
            ui_statusbar_printf("Failed to write MBR to file: %s", path);
            fclose(file);
            return -1;
        }
    }

    /* Extend the file to the desired size */
    if (fseek(file, size - 1, SEEK_SET) != 0 || fwrite("", 1, 1, file) != 1) {
        ui_statusbar_printf("Failed to set file size: %s", path);
        fclose(file);
        return -1;
    }

    fclose(file);

    /* Add the new disk to the state */
    disk_info_t* disk = &state->disks[new_index];
    memset(disk, 0, sizeof(disk_info_t));
    disk->size_bytes = size;
    disk->valid = true;
    disk->is_image = true;
    disk->has_mbr = init_mbr;
    if (init_mbr) {
        memcpy(disk->mbr, mbr, sizeof(mbr));
    }
    disk_parse_mbr_partitions(disk);

    snprintf(disk->path, sizeof(disk->path), "%s", path);
    /* Extract the filename out of the path */
    const char* base_name = disk_get_basename(path);
    snprintf(disk->name, sizeof(disk->name), "%s", base_name);
    /* Label depends on the name, so it must be done after setting the name */
    disk_generate_label(disk);

    /* If the new file didn't replace any file, increment the total number of disks */
    if (new_index == state->disk_count) {
        state->disk_count++;
    }
    ui_statusbar_print("Disk image created successfully!");

    return new_index;
}

